#!/usr/bin/env python3
"""Interactive and programmatic controller for the OVFToolkit mumax slave."""

from __future__ import annotations

import json
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import threading
from pathlib import Path
from typing import Any

try:
    import readline
except ImportError:  # Windows does not ship GNU readline.
    readline = None


PROMPT = "\033[1;32mMumax3\033[m$ "
_ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")


class MumaxEngine:
    """Own a mumax slave process and its authenticated loopback connection."""

    def __init__(
        self,
        executable: str | os.PathLike[str] | None = None,
        gpu: int = -1,
        output_directory: str | os.PathLike[str] = "πμMax_interactive",
        log_prefix: str = "",
        additional_arguments: list[str] | None = None,
        startup_timeout: float = 30.0,
    ) -> None:
        self._log_prefix = log_prefix
        self._output_lock = threading.Lock()
        self._send_lock = threading.Lock()
        self._queue_lock = threading.Lock()
        self._wait_queue: dict[int, tuple[threading.Event, list[Any]]] = {}
        self._last_ticket = -1
        self._closed = False
        self._ready = threading.Event()
        self._prompt_active = False
        self._prompt_lock_held = False

        if executable is None:
            beside_launcher = Path(__file__).resolve().with_name(
                "mumax-slave.exe" if os.name == "nt" else "mumax-slave"
            )
            executable = (
                beside_launcher
                if beside_launcher.is_file()
                else shutil.which("mumax-slave")
            )
        if executable is None:
            raise FileNotFoundError("mumax-slave was not found beside the launcher or in PATH")

        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(1)
        self._listener.settimeout(startup_timeout)
        address, port = self._listener.getsockname()
        token = secrets.token_urlsafe(32)

        arguments = [
            os.fspath(executable),
            "-o", os.fspath(output_directory),
            "-ipc", f"{address}:{port}",
            "-ipc-token", token,
        ]
        if gpu >= 0:
            arguments.extend(("-gpu", str(gpu)))
        if additional_arguments:
            arguments.extend(map(str, additional_arguments))

        self._print(f"Launching mumax3 with command line: {arguments!r}")
        self._process = subprocess.Popen(
            arguments,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self._log_thread = threading.Thread(target=self._forward_logs, daemon=True)
        self._log_thread.start()

        try:
            self._connection, peer = self._listener.accept()
            if peer[0] != "127.0.0.1":
                raise ConnectionError(f"rejected non-loopback IPC peer {peer!r}")
            self._reader = self._connection.makefile("r", encoding="utf-8")
            hello = self._read_message()
            if (hello.get("type"), hello.get("token"), hello.get("protocol")) != (
                "hello", token, 1
            ):
                raise ConnectionError("invalid mumax IPC handshake")
            self._mumax_version = str(hello.get("version", "unknown"))
            self._print(f"Connected to mumax3 {self._mumax_version}")

            self._protocol_thread = threading.Thread(
                target=self._receive_messages, daemon=True
            )
            self._protocol_thread.start()
            if not self._ready.wait(startup_timeout):
                raise TimeoutError("timed out waiting for mumax to initialize")
        except Exception:
            self._process.kill()
            self._process.wait()
            self._listener.close()
            raise
        finally:
            self._listener.close()

    def _print(self, message: str) -> None:
        with self._output_lock:
            if self._prompt_active and sys.stdout.isatty():
                current_input = readline.get_line_buffer() if readline else ""
                prompt_width = len(_ANSI_ESCAPE.sub("", PROMPT))
                columns = max(1, shutil.get_terminal_size().columns)
                wrapped_rows = (prompt_width + len(current_input)) // columns
                if wrapped_rows:
                    sys.stdout.write(f"\033[{wrapped_rows}A")
                sys.stdout.write("\r\033[J")
                sys.stdout.write(f"{self._log_prefix}{message}\n")
                sys.stdout.write(PROMPT + current_input)
                sys.stdout.flush()
                if readline:
                    readline.redisplay()
            else:
                print(f"{self._log_prefix}{message}", flush=True)

    def _prompt_startup(self) -> None:
        self._output_lock.acquire()
        self._prompt_lock_held = True

    def _prompt_ready(self) -> None:
        self._prompt_active = True
        if self._prompt_lock_held:
            self._prompt_lock_held = False
            self._output_lock.release()

    def _prompt_finished(self) -> None:
        with self._output_lock:
            self._prompt_active = False

    def _forward_logs(self) -> None:
        assert self._process.stdout is not None
        for line in self._process.stdout:
            self._print(line.rstrip("\r\n"))

    def _read_message(self) -> dict[str, Any]:
        line = self._reader.readline()
        if not line:
            raise ConnectionError("mumax IPC connection closed")
        message = json.loads(line)
        if not isinstance(message, dict) or "type" not in message:
            raise ValueError("invalid mumax IPC message")
        return message

    def _fail_waiters(self, error: BaseException) -> None:
        with self._queue_lock:
            for event, result in self._wait_queue.values():
                result[:] = [None, error]
                event.set()

    def _receive_messages(self) -> None:
        try:
            while True:
                message = self._read_message()
                message_type = message["type"]
                if message_type == "ready":
                    self._ready.set()
                elif message_type in {"result", "error", "complete"}:
                    ticket = int(message.get("id", 0))
                    with self._queue_lock:
                        pending = self._wait_queue.get(ticket)
                        if pending is None:
                            self._print(f"Warning: unsolicited ticket #{ticket} response")
                            continue
                        event, result = pending
                        if message_type == "error":
                            result[:] = [None, RuntimeError(message.get("message", "mumax error"))]
                        else:
                            result[:] = [message.get("value"), None]
                        event.set()
                elif message_type == "terminating":
                    return
                else:
                    self._print(f"Warning: unknown IPC event {message_type!r}")
        except (ConnectionError, OSError, ValueError, json.JSONDecodeError) as error:
            if not self._closed:
                self._print(f"Mumax IPC stopped: {error}")
            self._fail_waiters(error)

    def _send(self, message: dict[str, Any]) -> None:
        encoded = (json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8")
        with self._send_lock:
            self._connection.sendall(encoded)

    def _new_ticket(self) -> int:
        with self._queue_lock:
            self._last_ticket += 1
            ticket = self._last_ticket
            self._wait_queue[ticket] = (threading.Event(), [None, None])
            return ticket

    def get_ticket(self, ticket: int, timeout: float | None = None) -> str | None:
        with self._queue_lock:
            if ticket not in self._wait_queue:
                raise KeyError(f"ticket {ticket} is not registered")
            event = self._wait_queue[ticket][0]
        if not event.wait(timeout):
            raise TimeoutError(f"timed out waiting for mumax ticket {ticket}")
        with self._queue_lock:
            _, result = self._wait_queue.pop(ticket)
        if result[1] is not None:
            raise result[1]
        return result[0]

    def request(
        self,
        expression: str = "",
        prerequisite: str = "",
        wait: bool = False,
        timeout: float | None = None,
    ) -> int | str | None:
        if "\n" in expression:
            raise ValueError("expression must be a single line; use prerequisite for setup")
        ticket = self._new_ticket()
        lines = [line.rstrip() for line in prerequisite.splitlines() if line.strip()]
        lines.append(f"EvalTicket({ticket}, {expression or 'Done!'})")
        self._send({"type": "request", "id": ticket, "script": "\n".join(lines)})
        return self.get_ticket(ticket, timeout) if wait else ticket

    def post(self, script: str, wait: bool = False,
             timeout: float | None = None) -> None:
        ticket = self._new_ticket() if wait else 0
        self._send({"type": "command", "id": ticket, "script": script})
        if wait:
            self.get_ticket(ticket, timeout)

    def alive(self) -> bool:
        return self._process.poll() is None

    def join(self) -> int:
        return self._process.wait()

    def terminate(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self.alive():
            try:
                self._send({"type": "shutdown"})
                self._process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                self._process.kill()
                self._process.wait()
        self._fail_waiters(RuntimeError("mumax process terminated"))
        try:
            self._connection.close()
        except OSError:
            pass

    def __enter__(self) -> "MumaxEngine":
        return self

    def __exit__(self, *_: object) -> None:
        self.terminate()


mumax_engine = MumaxEngine


def main() -> int:
    print("Running mumax3 interactively, hold on a bit...")
    with MumaxEngine(log_prefix="\033[1;34mMumax3-int: \033[m") as engine:
        if readline:
            readline.set_startup_hook(engine._prompt_startup)
            readline.set_pre_input_hook(engine._prompt_ready)
        while engine.alive():
            try:
                if not readline:
                    engine._prompt_ready()
                line = input(PROMPT).rstrip()
            except (EOFError, KeyboardInterrupt):
                print()
                break
            finally:
                engine._prompt_finished()
            if line:
                engine.post(line)
        if readline:
            readline.set_startup_hook()
            readline.set_pre_input_hook()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
