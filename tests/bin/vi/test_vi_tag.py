#!/usr/bin/env python3
"""^] follows a tag to its address and runs nothing.

A tags entry's third field is a line number or a search pattern.  vi took
it as an address only when it was nothing but one, and otherwise ran it as
an ex command: a tags file in the project could run `!` shell commands
when ^] was pressed on a word.

Usage: test_vi_tag.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]


def run(cwd, path, keys):
    """Run vi on path in cwd through a pty, type keys; return the status."""
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        os.environ["TERM"] = "xterm"
        os.execv(VI, [VI, path])
    time.sleep(0.5)
    for k in keys:
        os.write(fd, k)
        time.sleep(0.3)
    end = time.time() + 5
    while time.time() < end:
        try:
            if not os.read(fd, 65536):
                break
        except OSError:
            break
    done, status = os.waitpid(pid, os.WNOHANG)
    if done == 0:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        status = None
    return status


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "f.c"), "w") as f:
            f.write("evil\ngood\none\ntwo\n")
        with open(os.path.join(d, "tags"), "w") as f:
            f.write("evil\tf.c\t!touch PWNED\ngood\tf.c\t4\n")
        # ^] on "evil" (line 1): must not run the shell command
        run(d, "f.c", [b"\x1d", b":q!\r"])
        if os.path.exists(os.path.join(d, "PWNED")):
            print("FAIL: ^] ran a tags entry's shell command")
            fails += 1
        # ^] on "good" (line 2): goes to line 4, where x deletes the t of two
        run(d, "f.c", [b"j", b"\x1d", b"x", b":w\r", b":q!\r"])
        with open(os.path.join(d, "f.c")) as f:
            got = f.read()
        if got != "evil\ngood\none\nwo\n":
            print("FAIL: ^] to a line-number tag: file is %r" % got)
            fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_tag")


main()
