#!/usr/bin/env python3
"""autoindent in ex's text input on a terminal; a!, i!, c!; ^D (EXVI-54).

ex took the lines entered as typed whatever autoindent was, and a! was
"Unknown command".  On a terminal each line entered starts with the
indent of the line before it -- appended after, inserted before, first
changed, then each line entered.  ^D at a line's start takes the indent
back a shiftwidth, 0^D all of it, ^^D all of it for that line only; the
terminal's eof character ends the read, which is how ex sees them.  ^D
alone in command mode writes the next scroll lines.  Batch use (-s, or
input that is not a terminal) ignores autoindent (POSIX ex -s).

Usage: test_ex_autoindent.py path/to/ex
"""
import os
import pty
import select
import subprocess
import sys
import tempfile
import time

EX = os.path.abspath(sys.argv[1])


def session(path, keys):
    """ex on a pty, keys typed one chunk at a time; its output, and status"""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.environ.pop("EXINIT", None)
        os.execv(EX, [EX, path])
    out = b""
    for k in [b""] + keys:
        os.write(fd, k)
        end = time.time() + 0.3
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    out += os.read(fd, 65536)
                except OSError:
                    break
    end = time.time() + 5
    while time.time() < end:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            return out, os.waitstatus_to_exitcode(status)
        time.sleep(0.05)
    os.kill(pid, 9)
    os.waitpid(pid, 0)
    return out, None


def lines(*ls):
    return [l.encode() + b"\r" for l in ls]


CASES = [
    ("a", lines("set ai", "1a", "x", "y", "."), "\tfoo\n\tx\n\ty\nbar\n"),
    ("a, blanks typed", lines("set ai", "1a", "  x", "y", "."), "\tfoo\n\t  x\n\t  y\nbar\n"),
    ("i", lines("set ai", "2i", "x", "."), "\tfoo\nx\nbar\n"),
    ("c", lines("set ai", "1c", "x", "."), "\tx\nbar\n"),
    ("noautoindent", lines("1a", "x", "."), "\tfoo\nx\nbar\n"),
    ("a! with noautoindent", lines("1a!", "x", "."), "\tfoo\n\tx\nbar\n"),
    ("a! with autoindent", lines("set ai", "1a!", "x", "."), "\tfoo\nx\nbar\n"),
    ("i! with noautoindent", lines("1i!", "x", "."), "\tx\n\tfoo\nbar\n"),
    ("i! with autoindent", lines("set ai", "1i!", "x", "."), "x\n\tfoo\nbar\n"),
    ("c! with noautoindent", lines("1c!", "x", "."), "\tx\nbar\n"),
    ("c! with autoindent", lines("set ai", "1c!", "x", "."), "x\nbar\n"),
    ("^D", lines("set ai sw=4", "1a") + [b"\x04", b"x\r"] + lines("y", "."),
     "\tfoo\n    x\n    y\nbar\n"),
    ("0^D", lines("set ai", "1a") + [b"0", b"\x04", b"x\r"] + lines("y", "."),
     "\tfoo\nx\ny\nbar\n"),
    ("^^D", lines("set ai", "1a") + [b"^", b"\x04", b"x\r"] + lines("y", "."),
     "\tfoo\nx\n\ty\nbar\n"),
]


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f")
        for name, keys, want in CASES:
            with open(path, "w") as f:
                f.write("\tfoo\nbar\n")
            out, status = session(path, keys + lines("wq"))
            with open(path) as f:
                got = f.read()
            if status != 0 or got != want:
                print(f"FAIL: {name}: status {status}, {got!r}, want {want!r}")
                fails += 1

        # ^D alone in command mode: the next scroll lines, at once
        with open(path, "w") as f:
            f.write("".join(f"{i}\n" for i in range(1, 11)))
        out, status = session(path, lines("set scroll=2", "3") + [b"\x04"] + lines(".=", "q"))
        # 4 and 5 after the prompt ^D leaves on its line; .= then says 5
        text = out.decode("latin1").replace("\r", "")
        if "3\n:4\n5\n" not in text or ":.=\n5\n" not in text:
            print(f"FAIL: ^D command: {text!r}")
            fails += 1

        # batch: autoindent ignored
        with open(path, "w") as f:
            f.write("\tfoo\nbar\n")
        subprocess.run([EX, "-s", path], input=b"set ai\n1a\nx\n.\nwq\n")
        with open(path) as f:
            got = f.read()
        if got != "\tfoo\nx\nbar\n":
            print(f"FAIL: batch autoindent: {got!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_ex_autoindent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
