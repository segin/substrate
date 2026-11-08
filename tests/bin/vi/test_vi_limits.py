#!/usr/bin/env python3
"""Counts past their range in vi, and the Q path's leak.

N% names the line N percent of the way through the file, so N past 100 is
an error; the operator form computed N times the line count in an int, and
999999999d% deleted line 1.  And Q (to ex) left the last inserted text
allocated: run against an AddressSanitizer build, the leak report fails
this test.

Usage: test_vi_limits.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]


def run(path, keys):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.execv(VI, [VI, path])
    time.sleep(0.5)
    for k in keys:
        os.write(fd, k)
        time.sleep(0.3)
    out = b""
    end = time.time() + 5
    while time.time() < end:
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
    done, status = os.waitpid(pid, os.WNOHANG)
    if done == 0:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        status = None
    return out, status


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "w") as f:
            f.write("one\ntwo\nthree\n")
        out, status = run(path, [b"999999999d%", b":wq\r"])
        with open(path) as f:
            got = f.read()
        if got != "one\ntwo\nthree\n":
            print("FAIL: 999999999d%% changed the file: %r" % got)
            fails += 1
        if b"\a" not in out:
            print("FAIL: 999999999d% reported no error")
            fails += 1
        # a count over 100 on % is an error (POSIX vi Move to Line)
        out, status = run(path, [b"101%", b"x", b":wq\r"])
        with open(path) as f:
            got = f.read()
        if b"\a" not in out or got != "ne\ntwo\nthree\n":
            print("FAIL: 101%% moved, or reported no error: %r" % got)
            fails += 1

        out, status = run(path, [b"ifoo\x1b", b"Q", b"q!\n"])
        at = out.find(b"LeakSanitizer")
        if at < 0:
            at = out.find(b"AddressSanitizer")
        if at >= 0:
            print("FAIL: %s" % out[at:at + 160])
            fails += 1
        elif status is None:
            print("FAIL: vi did not quit by Q, q!")
            fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_limits")


main()
