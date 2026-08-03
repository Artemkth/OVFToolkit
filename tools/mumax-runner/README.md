# OVFToolkit mumax runner

This optional tool embeds mumax3 in a Go subprocess and controls it through an
authenticated JSON protocol over a loopback TCP socket. Human-readable mumax3
stdout and stderr remain available to the Python terminal and are not parsed as
control messages.

The Python launcher binds `127.0.0.1:0`, retains the listener, and passes the
selected address and a random token to `mumax-slave`. The socket never listens
on an external interface.

This directory is GPL-3.0-or-later because the Go executable incorporates
mumax3. The rest of OVFToolkit remains under its top-level MIT license.
