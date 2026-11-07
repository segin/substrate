#!/usr/bin/env python3
"""ex's open: vi on the line addressed, at the pattern's match (EXVI-49).

open was "Unknown command".  POSIX's open mode shows the one line; exvi's
open is visual mode, which POSIX lets stand in where open mode is not
supported.  Here 2open /c/ must leave the cursor on line 2's c, so rZ
changes it.

Usage: test_ex_open.py path/to/ex
"""
import os
import pty
import select
import sys
import tempfile
import time

EX = sys.argv[1]


def session(path, keys):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.environ.pop("EXINIT", None)
        os.execv(EX, [EX, path])
    for k, wait in keys:
        os.write(fd, k)
        end = time.time() + wait
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    os.read(fd, 65536)
                except OSError:
                    break
    end = time.time() + 5
    while time.time() < end:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            return os.waitstatus_to_exitcode(status)
        time.sleep(0.05)
    os.kill(pid, 9)
    os.waitpid(pid, 0)
    return None


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        for keys, want in (
            ([(b"", 0.5), (b"2open /c/\r", 0.8), (b"rZ", 0.3), (b":wq\r", 1.0)],
             "abc\nxbZd\n"),
            ([(b"", 0.5), (b"2o\r", 0.8), (b"rZ", 0.3), (b":wq\r", 1.0)],
             "abc\nZbcd\n"),
        ):
            with open(path, "w") as f:
                f.write("abc\nxbcd\n")
            status = session(path, keys)
            with open(path) as f:
                got = f.read()
            if status != 0 or got != want:
                print(f"FAIL: {keys[1][0]!r}: status {status}, {got!r}, want {want!r}")
                fails += 1
    if fails:
        return 1
    print("PASS: test_ex_open")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
