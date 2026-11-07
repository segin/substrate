#!/usr/bin/env python3
"""vi keeps a NUL in a line: typed with ^V^@, or read from the file.

vi rebuilt a line it edited as a C string, so the text after a NUL went:
ifoo^V^@bar<ESC> on "x" wrote "foobar", without the NUL and the rest.
Each file is checked against what vim writes.

Usage: test_vi_nul.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]
ESC = b"\x1b"

CASES = [
    # (file bytes, keys, want)
    (b"x\n", [b"ifoo", b"\x16\x00", b"bar", ESC], b"foo\x00barx\n"),
    (b"ab\x00cd\n", [b"x"], b"b\x00cd\n"),
    (b"ab\x00cd\n", [b"$", b"x"], b"ab\x00c\n"),
    (b"ab\x00cd\n", [b"A", b"e", ESC], b"ab\x00cde\n"),
    (b"ab\x00cd\nx\n", [b"j", b"dd"], b"ab\x00cd\n"),
    (b"ab\x00cd\n", [b"yy", b"p"], b"ab\x00cd\nab\x00cd\n"),
    (b"ab\x00cd\n", [b"ll", b"i\r", ESC], b"ab\n\x00cd\n"),
]


def run(data, keys):
    """Edit a file of data with keys typed, then :wq; return the file's bytes."""
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "wb") as f:
            f.write(data)
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm"
            os.environ["LC_ALL"] = "C.UTF-8"
            os.execv(VI, [VI, path])
        time.sleep(0.5)
        for k in keys + [b":wq\r"]:
            os.write(fd, k)
            time.sleep(0.3)
        for _ in range(20):
            done, _ = os.waitpid(pid, os.WNOHANG)
            if done:
                break
            time.sleep(0.1)
        else:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        with open(path, "rb") as f:
            return f.read()


def main():
    fails = 0
    for data, keys, want in CASES:
        got = run(data, keys)
        if got != want:
            print("FAIL: %r on %r: %r, expected %r" % (keys, data, got, want))
            fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_nul")


main()
