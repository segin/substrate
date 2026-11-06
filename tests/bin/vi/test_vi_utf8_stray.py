#!/usr/bin/env python3
"""A byte that is not part of a valid UTF-8 sequence is a character of its own.

vi stepped back over every continuation byte (0x80-0xbf) to find where a
character started, whether or not a lead byte before it made a valid
sequence with it.  At a stray one -- Latin-1 text, say `20\\xb0C` -- the
step forward came back to the same byte, and `$`, `A` and the screen code
looped for ever, with raw mode keeping ^C from stopping it.

Usage: test_vi_utf8_stray.py path/to/vi
"""
import os
import pty
import select
import signal
import sys
import tempfile
import time

VI = sys.argv[1]

CASES = [
    # name, file, keys, file afterwards
    ("$ x on a Latin-1 line", b"temp 20\xb0C\n", [b"$", b"x", b":wq\r"], b"temp 20\xb0\n"),
    ("A on a Latin-1 line", b"temp 20\xb0C\n", [b"A", b"x", b"\x1b", b":wq\r"], b"temp 20\xb0Cx\n"),
    ("l x onto a stray byte", b"a\xb0b\n", [b"0", b"l", b"x", b":wq\r"], b"ab\n"),
    ("$ x after a valid character and a stray byte", b"\xc3\xa9\x80z\n",
     [b"$", b"x", b":wq\r"], b"\xc3\xa9\x80\n"),
    ("l x from a valid character onto a stray byte", b"\xc3\xa9\x80z\n",
     [b"0", b"l", b"x", b":wq\r"], b"\xc3\xa9z\n"),
]


def run(path, keys):
    """Run vi on path through a pty, type keys; return the status, or None if it hung."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.execv(VI, [VI, path])
    time.sleep(0.5)
    for k in keys:
        os.write(fd, k)
        time.sleep(0.3)
    end = time.time() + 5
    while time.time() < end:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            return status
        # (a hung vi writes nothing: never block on the read)
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            try:
                os.read(fd, 65536)
            except OSError:
                pass
    os.kill(pid, signal.SIGKILL)
    os.waitpid(pid, 0)
    return None


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        for name, text, keys, want in CASES:
            path = os.path.join(d, "f.txt")
            with open(path, "wb") as f:
                f.write(text)
            status = run(path, keys)
            with open(path, "rb") as f:
                got = f.read()
            if status is None:
                print("FAIL: %s: vi hung" % name)
                fails += 1
            elif got != want:
                print("FAIL: %s: file is %r, expected %r" % (name, got, want))
                fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_utf8_stray")


main()
