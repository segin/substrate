#!/usr/bin/env python3
"""Every way out of vi leaves cleanly.

vi pointed active_visual, which the atexit handler restoring the terminal
reads, at a structure on exvi_visual_main's stack, and never cleared it:
after every exit the handler read a dead frame (stack-use-after-return
under AddressSanitizer), and could call tcsetattr with whatever was there.
Each return to visual mode also registered the handler again.

On a plain build the old fault is silent; run this on an AddressSanitizer
build (detect_stack_use_after_return) to see it.

Usage: test_vi_exit_paths.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]
ESC = b"\x1b"

CASES = [
    # name, keys, file afterwards
    (":q!", [b"x", b":q!\r"], "abc\n"),
    ("ZZ", [b"x", b"ZZ"], "bc\n"),
    (":wq", [b"x", b":wq\r"], "bc\n"),
    ("Q to ex, q", [b"Q", b"q!\n"], "abc\n"),
    ("Q, :vi, Q, :vi, ZZ",
     [b"Q", b"vi\n", b"x", b"Q", b"vi\n", b"ZZ"], "bc\n"),
]


def run(path, keys):
    """Run vi on path through a pty, type keys; return (status, output)."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.execv(VI, [VI, path])
    out = b""
    time.sleep(0.5)
    for k in keys:
        os.write(fd, k)
        time.sleep(0.3)
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
    return status, out


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        for name, keys, want in CASES:
            path = os.path.join(d, "f.txt")
            with open(path, "w") as f:
                f.write("abc\n")
            status, out = run(path, keys)
            with open(path) as f:
                got = f.read()
            at = out.find(b"AddressSanitizer")
            if at >= 0:
                print("FAIL: %s: %s" % (name, out[at:at + 120]))
                fails += 1
            elif status is None or not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
                print("FAIL: %s: vi did not exit cleanly (status %r)" % (name, status))
                fails += 1
            elif got != want:
                print("FAIL: %s: file is %r, expected %r" % (name, got, want))
                fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_exit_paths")


main()
