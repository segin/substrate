#!/usr/bin/env python3
"""ZZ does not quit when its write fails.

ZZ wrote the file and quit whether or not the write worked: in a directory
the file could not be replaced in, the change was lost and vi exited 0.

Usage: test_vi_zz_fail.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]


def main():
    if os.getuid() == 0:
        print("test_vi_zz_fail: skip (root writes anywhere)")
        return
    with tempfile.TemporaryDirectory() as d:
        ro = os.path.join(d, "ro")
        os.mkdir(ro)
        path = os.path.join(ro, "f.txt")
        with open(path, "w") as f:
            f.write("abc\n")
        os.chmod(ro, 0o555)
        try:
            pid, fd = pty.fork()
            if pid == 0:
                os.environ["TERM"] = "xterm"
                os.execv(VI, [VI, path])
            time.sleep(0.5)
            for k in (b"x", b"ZZ"):
                os.write(fd, k)
                time.sleep(0.4)
            done, _ = os.waitpid(pid, os.WNOHANG)
            alive = done == 0
            if alive:
                os.write(fd, b":q!\r")
                time.sleep(0.5)
                done, _ = os.waitpid(pid, os.WNOHANG)
                if done == 0:
                    os.kill(pid, signal.SIGKILL)
                    os.waitpid(pid, 0)
        finally:
            os.chmod(ro, 0o755)
        with open(path) as f:
            got = f.read()
    if not alive:
        print("FAIL: ZZ quit although its write failed")
        sys.exit(1)
    if got != "abc\n":
        print("FAIL: the file changed: %r" % got)
        sys.exit(1)
    print("PASS: test_vi_zz_fail")


main()
