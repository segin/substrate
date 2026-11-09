#!/usr/bin/env python3
"""SIGTERM, SIGHUP, ^Z and SIGCONT in vi.  (SIGHUP is SIGTERM's case;
both must leave the terminal in canonical mode with echo, as found.)

SIGTERM's handler saved the buffer from inside the handler, walking lines
the interrupted code might have been changing, then _exit()ed past the
terminal restore: the tty was left raw on the alternate screen.  Now the
handler notes the signal; vi preserves the buffer and exits through
exit(), so the alternate screen is left (ESC [?1049l).

^Z did nothing (ISIG is off, so it is a byte and fell to the command
switch's default).  Now vi gives the terminal back and stops with SIGTSTP;
on SIGCONT it takes the terminal again (ESC [?1049h) and redraws.

SIGTSTP's default stop is discarded in an orphaned process group, which is
what pty.fork() makes, so the child plays the shell: it puts vi in a
process group of its own in the foreground, reports the stop, and
continues it.

Usage: test_vi_signals.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import termios
import time

VI = sys.argv[1]
LEAVE = b"\x1b[?1049l"
ENTER = b"\x1b[?1049h"


def read_for(fd, secs):
    out = b""
    end = time.time() + secs
    while time.time() < end:
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
    return out


def drain(fd, secs):
    """Read what arrives within secs, without blocking past it."""
    import select
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


def term_signal(d, sig):
    """SIGTERM or SIGHUP: preserve, exit 1, the terminal restored -- left
    the alternate screen, and in canonical mode with echo again."""
    name = signal.Signals(sig).name
    path = os.path.join(d, "%s.txt" % name)
    with open(path, "w") as f:
        f.write("abc\n")
    rdir = os.path.join(d, "rec-%s" % name)
    os.mkdir(rdir)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.environ["EXINIT"] = "set dir=%s" % rdir
        os.execv(VI, [VI, path])
    drain(fd, 0.6)
    os.write(fd, b"x")
    drain(fd, 0.4)
    os.kill(pid, sig)
    out = read_for(fd, 3)
    done, status = os.waitpid(pid, os.WNOHANG)
    if done == 0:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        return "vi did not exit on %s" % name
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 1:
        return "%s: status %r, expected exit 1" % (name, status)
    if LEAVE not in out:
        return "%s left the terminal on the alternate screen: %r" % (name, out[-80:])
    try:
        lflag = termios.tcgetattr(fd)[3]
    except termios.error:
        lflag = None
    if lflag is not None and (lflag & (termios.ICANON | termios.ECHO)) != \
            (termios.ICANON | termios.ECHO):
        return "%s left the terminal raw (lflag %#o)" % (name, lflag)
    recs = []
    for root, _, files in os.walk(rdir):
        recs += files
    if not recs:
        return "%s preserved nothing in %s" % (name, rdir)
    return None


def test_sigterm(d):
    return term_signal(d, signal.SIGTERM)


def test_sighup(d):
    return term_signal(d, signal.SIGHUP)


def test_suspend(d):
    path = os.path.join(d, "z.txt")
    with open(path, "w") as f:
        f.write("abc\n")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        child = os.fork()
        if child == 0:
            os.setpgid(0, 0)
            signal.signal(signal.SIGTTOU, signal.SIG_IGN)
            os.tcsetpgrp(0, os.getpid())
            signal.signal(signal.SIGTTOU, signal.SIG_DFL)
            os.execv(VI, [VI, path])
        _, st = os.waitpid(child, os.WUNTRACED)
        if os.WIFSTOPPED(st):
            os.write(1, b"\r\n<STOPPED>\r\n")
            time.sleep(0.3)
            os.kill(child, signal.SIGCONT)
            _, st = os.waitpid(child, 0)
        os._exit(os.WEXITSTATUS(st) if os.WIFEXITED(st) else 99)
    drain(fd, 0.6)
    os.write(fd, b"\x1a")
    out = drain(fd, 1.5)
    if b"<STOPPED>" not in out:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        return "^Z did not stop vi: %r" % out[-80:]
    stop_at = out.find(b"<STOPPED>")
    if LEAVE not in out[:stop_at]:
        return "^Z stopped vi without leaving the alternate screen"
    if ENTER not in out[stop_at:]:
        return "SIGCONT did not bring vi back to its screen: %r" % out[stop_at:][-80:]
    os.write(fd, b"x")
    drain(fd, 0.4)
    os.write(fd, b":wq\r")
    # wait for the exit, not a fixed 3 s: an ASan build on a CI runner
    # (its leak check at exit) took longer, and this reported a vi that
    # was still writing as one that had not taken :wq
    done = 0
    deadline = time.time() + 30
    while done == 0 and time.time() < deadline:
        read_for(fd, 0.5)
        done, status = os.waitpid(pid, os.WNOHANG)
    if done == 0:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        return "vi did not take :wq after SIGCONT"
    with open(path) as f:
        got = f.read()
    if got != "bc\n":
        return "after SIGCONT the file is %r, expected 'bc\\n'" % got
    return None


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        for t in (test_sigterm, test_sighup, test_suspend):
            err = t(d)
            if err:
                print("FAIL: %s: %s" % (t.__name__, err))
                fails += 1
    if fails:
        sys.exit(1)
    print("PASS: test_vi_signals")


main()
