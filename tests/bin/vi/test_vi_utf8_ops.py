#!/usr/bin/env python3
"""vi's operators and motions count characters, not bytes.

dl, dh, x, X, Delete, 3x, cl, an inclusive motion's end and p after the
cursor took one byte where the character under the cursor was several,
and f, F, t and T read one byte as their argument and matched bytes: dl
on "éa" wrote "\\xa9a", dfé wrote "\\xa9b" and ran the second byte as a
command, and Delete on "日x" left "\\x97\\xa5x".  Each case's file is
checked against what vim writes.

Usage: test_vi_utf8_ops.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]
DEL = b"\x1b[3~"
ESC = b"\x1b"

CASES = [
    # (text, keys, want)
    ("éa\n", [b"dl"], "a\n"),
    ("éa\n", [b"x"], "a\n"),
    ("日本語x\n", [b"3x"], "x\n"),
    ("日x\n", [DEL], "x\n"),
    ("éa\n", [b"$", b"X"], "a\n"),
    ("éa\n", [b"$", b"dh"], "a\n"),
    ("xéb\n", ["dfé".encode()], "b\n"),
    ("xéb\n", ["dté".encode()], "éb\n"),
    ("é日b\n", [b"$", "dFé".encode()], "b\n"),
    ("é日b\n", [b"$", "dTé".encode()], "éb\n"),
    ("aébéc\n", ["fé;".encode(), b"x"], "aébc\n"),
    ("aé\n", [b"y$", b"P"], "aéaé\n"),
    ("éa\n", [b"yl", b"p"], "ééa\n"),
    ("éa\n", [b"cl", b"X", ESC], "Xa\n"),
    ("éab\n", [b"cl", b"X", ESC, b"l", b"."], "XXb\n"),
    ("ab日\n", [b"l", b"d$"], "a\n"),
]


def run(text, keys):
    """Edit a file of text with keys typed, then :wq; return the file's bytes."""
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "wb") as f:
            f.write(text.encode())
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm"
            os.environ["LANG"] = "C.UTF-8"
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
    for text, keys, want in CASES:
        got = run(text, keys)
        if got != want.encode():
            print("FAIL: %r on %r: %r, expected %r" % (keys, text, got, want.encode()))
            fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_utf8_ops")


main()
