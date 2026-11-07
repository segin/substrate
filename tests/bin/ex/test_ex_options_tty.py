#!/usr/bin/env python3
"""POSIX options interactive ex shows (EXVI-53): autoprint, report, warn,
prompt and term.  Each was accepted and did nothing.  Batch use writes
none of their output (POSIX ex -s: no informational messages), so ex runs
on a pty here.

Usage: test_ex_options_tty.py path/to/ex
"""
import os
import pty
import select
import sys
import tempfile
import time

EX = os.path.abspath(sys.argv[1])


def session(path, keys, term="xterm"):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = term
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
        done, _ = os.waitpid(pid, os.WNOHANG)
        if done:
            break
        time.sleep(0.05)
    else:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
    return out.decode("latin1").replace("\r", "")


def lines(*ls):
    return [l.encode() + b"\r" for l in ls]


def main():
    fails = 0

    def check(name, ok, out):
        nonlocal fails
        if not ok:
            print(f"FAIL: {name}: {out!r}")
            fails += 1

    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f")

        def text(n):
            with open(path, "w") as f:
                f.write("".join(f"{i}\n" for i in range(1, n + 1)))

        text(5)
        out = session(path, lines("2d", "q!"))
        check("autoprint after d", ":2d\n2\n" not in out and ":2d\n3\n" in out, out)
        text(5)
        out = session(path, lines("set noap", "2d", "q!"))
        check("noautoprint", ":2d\n:q!" in out, out)
        text(5)
        out = session(path, lines("2d p", "q!"))
        check("a flag prints, not autoprint as well", out.count("\n3\n") == 1, out)

        text(10)
        out = session(path, lines("1,5d", "q!"))
        check("report", "5 fewer lines" in out, out)
        text(10)
        out = session(path, lines("1,4d", "q!"))
        check("report below 5", "fewer lines" not in out, out)

        text(3)
        out = session(path, lines("1d", "!true", "q!"))
        check("warn", "[No write since last change]" in out, out)
        text(3)
        out = session(path, lines("set nowarn", "1d", "!true", "q!"))
        check("nowarn", "No write since last change" not in out, out)

        text(3)
        out = session(path, lines("set noprompt", "1", "q!"))
        check("noprompt", ":1\n" not in out and "\n1\n" in out, out)

        text(3)
        out = session(path, lines("vi", "q!"), term="dumb")
        check("term=dumb has no visual mode", "requires a terminal" in out, out)
    if fails:
        return 1
    print("PASS: test_ex_options_tty")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
