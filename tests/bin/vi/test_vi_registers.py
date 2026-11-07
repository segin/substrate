#!/usr/bin/env python3
"""vi's named buffers: "x names the buffer the next command uses.

The register name was read into the command loop's key variable, so the
loop's end took the command for something other than `"` and cleared the
pending name at once: every "x command used the unnamed buffer.  "ayy then
"ap looked right only because both went to the unnamed buffer.  Here line 1
goes to a and line 2 to b; "ap at the end must put line 1.

Usage: test_vi_registers.py path/to/vi
"""
import os
import pty
import select
import signal
import sys
import tempfile
import time

VI = sys.argv[1]


def main():
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "w") as f:
            f.write("one\ntwo\n")
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm"
            os.execv(VI, [VI, path])

        def drain(secs):
            end = time.time() + secs
            while time.time() < end:
                r, _, _ = select.select([fd], [], [], 0.05)
                if r:
                    try:
                        os.read(fd, 65536)
                    except OSError:
                        return

        drain(0.5)
        for keys in (b"\"ayy", b"j", b"\"byy", b"G", b"\"ap", b":wq\r"):
            os.write(fd, keys)
            drain(0.3)
        drain(1.0)
        done, _ = os.waitpid(pid, os.WNOHANG)
        if done == 0:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            print("FAIL: vi did not quit")
            sys.exit(1)
        with open(path) as f:
            got = f.read()
        if got != "one\ntwo\none\n":
            print("FAIL: \"ap put the wrong buffer: file is %r" % got)
            sys.exit(1)
    print("PASS: test_vi_registers")


main()
