#!/usr/bin/env python3
"""A search from inside a line does not match `^` there.

vi searched the rest of the cursor's line from the cursor, as a string of
its own, so `^` matched at the cursor: /^. on "abc" from column 0 landed on
the "b", and ?^. from the "c" on the "b" too.  As POSIX has it, `^` is the
start of a line only: /^. goes to the next line's start, ?^. to this one's.

Usage: test_vi_search_bol.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]


def run(text, keys):
    """Edit a file of text with keys typed, then :wq; return the file."""
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "w") as f:
            f.write(text)
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm"
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
        with open(path) as f:
            return f.read()


def main():
    fails = 0
    cases = [
        # (text, keys, want): x deletes where the search left the cursor
        ("abc\nxyz\n", [b"/^.\r", b"x"], "abc\nyz\n"),
        ("abc\nxyz\n", [b"$", b"?^.\r", b"x"], "bc\nxyz\n"),
        ("abb\nxbz\n", [b"/b$\r", b"x"], "ab\nxbz\n"),
    ]
    for text, keys, want in cases:
        got = run(text, keys)
        if got != want:
            print("FAIL: %r on %r: %r, expected %r" % (keys, text, got, want))
            fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_search_bol")


main()
