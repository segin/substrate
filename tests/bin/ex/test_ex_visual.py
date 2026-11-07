#!/usr/bin/env python3
"""ex's :vi is visual mode on the same buffer.

ex used to refuse :vi on a modified buffer and otherwise exec a `vi` with
the file's name -- `../vi/vi` beside the binary in the source tree, else
whatever `vi` was on PATH (Vim, on a build host) -- so undo history, marks
and named buffers did not survive the switch.  Now the switch stays in
this process: here ex changes line 1, marks line 2 and yanks it into
buffer a, then :vi; in visual mode u undoes the ex change, "ap puts
buffer a, 'a goes to the mark, and :wq writes the result.

Usage: test_ex_visual.py path/to/ex
"""
import os
import pty
import signal
import sys
import tempfile
import time

EX = sys.argv[1]


def main():
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "w") as f:
            f.write("one\ntwo\n")
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm"
            os.environ.pop("EXINIT", None)
            os.execv(EX, [EX, path])
        out = b""

        def send(keys, wait=0.4):
            nonlocal out
            os.write(fd, keys)
            end = time.time() + wait
            while time.time() < end:
                try:
                    import select
                    r, _, _ = select.select([fd], [], [], 0.05)
                    if r:
                        out += os.read(fd, 65536)
                except OSError:
                    break

        send(b"", 0.5)
        send(b"1s/one/ONE/\r")          # modified: :vi refused this
        send(b"2ka\r")
        send(b"2y a\r")
        send(b"vi\r", 0.8)
        send(b"u")                      # undoes the ex substitute
        send(b"G\"ap")                  # buffer a, yanked in ex
        send(b"'a")                     # the mark, set in ex
        send(b"x")                      # on line 2: "two" -> "wo"
        send(b":wq\r", 1.0)
        done, status = os.waitpid(pid, os.WNOHANG)
        if done == 0:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            print("FAIL: ex did not finish after :vi and :wq: %r" % out[-200:])
            sys.exit(1)
        with open(path) as f:
            got = f.read()
        if b"Vim" in out:
            print("FAIL: :vi ran Vim")
            sys.exit(1)
        if got != "one\nwo\ntwo\n":
            print("FAIL: file is %r, expected 'one\\nwo\\ntwo\\n'" % got)
            sys.exit(1)
    print("PASS: test_ex_visual")


main()
