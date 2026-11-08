#!/usr/bin/env python3
"""ex's suspend and stop (POSIX ex Suspend): stop with SIGTSTP, as ^Z in
a shell does, and carry on after SIGCONT.  They were "Unknown command".
A restricted ex (rex) refuses them.

SIGTSTP's default stop is discarded in an orphaned process group, which is
what pty.fork() makes, so the child plays the shell: it puts ex in a
process group of its own in the foreground, reports the stop, and
continues it.

Usage: test_ex_suspend.py path/to/ex
"""
import os
import pty
import select
import signal
import sys
import tempfile
import time

EX = os.path.abspath(sys.argv[1])


def drain(fd, secs):
    out = b""
    end = time.time() + secs
    while True:
        left = end - time.time()
        if left <= 0:
            break
        r, _, _ = select.select([fd], [], [], left)
        if not r:
            break
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
    return out


def run(argv0, path, command):
    """ex on a pty under a job-control parent; command, then 1p and q.
    The transcript and the parent's exit status (the child's)."""
    pid, fd = pty.fork()
    if pid == 0:
        child = os.fork()
        if child == 0:
            os.setpgid(0, 0)
            signal.signal(signal.SIGTTOU, signal.SIG_IGN)
            os.tcsetpgrp(0, os.getpid())
            signal.signal(signal.SIGTTOU, signal.SIG_DFL)
            os.execv(EX, [argv0, path])
        _, st = os.waitpid(child, os.WUNTRACED)
        if os.WIFSTOPPED(st):
            os.write(1, b"\r\n<STOPPED>\r\n")
            time.sleep(0.3)
            os.kill(child, signal.SIGCONT)
            _, st = os.waitpid(child, 0)
        os._exit(os.WEXITSTATUS(st) if os.WIFEXITED(st) else 99)
    out = drain(fd, 0.5)
    os.write(fd, command + b"\n")
    out += drain(fd, 1.0)
    os.write(fd, b"1p\nq\n")
    out += drain(fd, 2.0)
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
            f.write("one\n")
        for cmd in (b"suspend", b"stop", b"su", b"st"):
            out, status = run(EX, path, cmd)
            stop = out.find(b"<STOPPED>")
            if stop < 0:
                print("FAIL: %s did not stop ex: %r" % (cmd.decode(), out[-120:]))
                fails += 1
            elif b"one" not in out[stop:] or status is None or \
                    not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
                print("FAIL: ex did not carry on after %s: status %r, %r"
                      % (cmd.decode(), status, out[stop:][-120:]))
                fails += 1
        rex = os.path.join(d, "rex")
        os.symlink(EX, rex)
        out, status = run(rex, path, b"suspend")
        if b"<STOPPED>" in out or b"not allowed" not in out:
            print("FAIL: rex did not refuse suspend: %r" % out[-120:])
            fails += 1
    if fails:
        return 1
    print("PASS: test_ex_suspend")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
