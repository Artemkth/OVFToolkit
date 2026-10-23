#!/usr/bin/env python3

import importlib.util
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        return 2
    if shutil.which("nvidia-smi") is None or subprocess.run(
        ["nvidia-smi", "-L"], stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, check=False
    ).returncode != 0:
        print("No accessible NVIDIA GPU; skipping.")
        return 77

    launcher, slave = map(Path, sys.argv[1:])
    spec = importlib.util.spec_from_file_location("mumax_runner", launcher)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load mumax runner module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    with tempfile.TemporaryDirectory(prefix="mumax-socket-test-") as output:
        with module.MumaxEngine(
            executable=slave,
            output_directory=output,
            additional_arguments=["-http="],
        ) as engine:
            result = engine.request("1 + 2", wait=True, timeout=10)
            if float(result) != 3.0:
                raise RuntimeError(f"unexpected ticket result {result!r}")
            engine.post('print("socket log forwarding is alive")',
                        wait=True, timeout=10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
