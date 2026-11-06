#!/usr/bin/env python3
"""ZZ does not quit when its write fails.

ZZ wrote the file and quit whether or not the write worked: when it
failed, the change was lost and vi exited 0.  A file size limit (with
SIGXFSZ ignored, so the write fails with EFBIG) makes the write fail.

Usage: test_vi_zz_fail.py path/to/vi
"""
import os
import pty
import resource
import signal
import sys
import tempfile
import time

VI = sys.argv[1]


def main():
    big = "".join("%d\n" % i for i in range(1, 2001))
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "w") as f:
            f.write(big)
        pid, fd = pty.fork()
        if pid == 0:
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))
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
        with open(path) as f:
            got = f.read()
    if not alive:
        print("FAIL: ZZ quit although its write failed")
        sys.exit(1)
    if got != big:
        print("FAIL: the file changed: %d bytes, was %d" % (len(got), len(big)))
        sys.exit(1)
    print("PASS: test_vi_zz_fail")


main()
