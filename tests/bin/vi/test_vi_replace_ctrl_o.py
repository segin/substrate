#!/usr/bin/env python3
"""Backspace in replace mode after a ^O command that changed the buffer.

Replace mode (R) keeps, for each character it replaced, the line and column
it was at, so that backspace can put the old character back.  ^O runs one
command and returns to replace mode; that history was kept across it.  When
the command freed lines (dd, u, :e!), the next backspace wrote through a
freed line.  Now a ^O command that changes the buffer ends the history:
backspace has nothing before it to restore and rings the bell.

Run it on an AddressSanitizer build to see the old fault; on a plain build
the old code wrote into freed memory and the result was undefined.

Usage: test_vi_replace_ctrl_o.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]
CTRL_O = b"\x0f"
BS = b"\x7f"
ESC = b"\x1b"

CASES = [
    # name, file, keys, file afterwards
    ("R, ^O dd, backspace", "abc\ndef\nghi\n",
     [b"j", b"R", b"x", b"y", CTRL_O, b"dd", BS, BS, ESC, b":wq\r"],
     "abc\nghi\n"),
    ("R, ^O u, backspace", "abc\n",
     [b"R", b"x", b"y", CTRL_O, b"u", BS, ESC, b":wq\r"],
     "abc\n"),
]


def run(path, keys):
    """Run vi on path through a pty, type keys; return (status, output)."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.execv(VI, [VI, path])
    out = b""
    time.sleep(0.5)
    for k in keys:
        os.write(fd, k)
        time.sleep(0.2)
    end = time.time() + 5
    status = None
    while time.time() < end:
        try:
            chunk = os.read(fd, 65536)
            if not chunk:
                break
            out += chunk
        except OSError:
            break
    done, status = os.waitpid(pid, os.WNOHANG)
    if done == 0:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        status = None
    return status, out


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        for name, text, keys, want in CASES:
            path = os.path.join(d, "f.txt")
            with open(path, "w") as f:
                f.write(text)
            status, out = run(path, keys)
            with open(path) as f:
                got = f.read()
            if b"AddressSanitizer" in out:
                print("FAIL: %s: AddressSanitizer: %s" % (name, out[out.find(b"AddressSanitizer"):][:120]))
                fails += 1
            elif status is None or not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
                print("FAIL: %s: vi did not exit cleanly (status %r)" % (name, status))
                fails += 1
            elif got != want:
                print("FAIL: %s: file is %r, expected %r" % (name, got, want))
                fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_replace_ctrl_o")


main()
