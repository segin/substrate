#!/usr/bin/env python3
"""SIGINT in vi acts as <ESC> and vi carries on.

Raw mode turns ISIG off, so ^C is a key; SIGINT comes from kill.  Its
handler longjmp'd to the ex main loop, whose jmp_buf was not yet set in vi:
vi crashed and left the terminal raw.  POSIX vi: an interrupt during text
input ends it as <ESC> does; in command mode it ends the command -- a long
one running too (999999999. after ox<ESC>), vi carrying on to take :q!.

Usage: test_vi_interrupt.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "w") as f:
            f.write("abc\n")
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm"
            os.execv(VI, [VI, path])
        out = b""

        def send(keys):
            os.write(fd, keys)
            time.sleep(0.4)

        time.sleep(0.5)
        send(b"ifoo")
        os.kill(pid, signal.SIGINT)     # ends the insert, as <ESC>
        time.sleep(0.4)
        send(b"x")                      # a command: deletes the second o
        os.kill(pid, signal.SIGINT)     # in command mode; seen again
        time.sleep(0.4)
        send(b":wq\r")
        end = time.time() + 5
        while time.time() < end:
            try:
                chunk = os.read(fd, 65536)
                if not chunk:
                    break
                out += chunk
            except OSError:
                break
        done, status = os.waitpid(pid, os.WNOHANG)
        if done == 0:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            status = None
        with open(path) as f:
            got = f.read()
        at = out.find(b"AddressSanitizer")
        if at >= 0:
            print("FAIL: %s" % out[at:at + 120])
            fails += 1
        elif status is None or not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
            print("FAIL: vi did not exit cleanly after SIGINT (status %r)" % (status,))
            fails += 1
        elif got != "foabc\n":
            print("FAIL: file is %r, expected 'foabc\\n'" % got)
            fails += 1

        # SIGINT while a long command runs stops it, and vi goes on:
        # 999999999. after ox<ESC>, a line at a time, then :q!
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm"
            os.execv(VI, [VI, path])
        time.sleep(0.5)
        send(b"ox\x1b")
        os.write(fd, b"999999999.")
        time.sleep(0.5)
        os.kill(pid, signal.SIGINT)
        time.sleep(0.4)
        os.write(fd, b":q!\r")
        out = b""
        end = time.time() + 5
        while time.time() < end:
            done, status = os.waitpid(pid, os.WNOHANG)
            if done:
                break
            try:
                import select
                if select.select([fd], [], [], 0.1)[0]:
                    out += os.read(fd, 65536)
            except OSError:
                pass
        else:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            done, status = pid, None
        if status is None or not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
            print("FAIL: SIGINT did not stop a long . (status %r)" % (status,))
            fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_interrupt")


main()
