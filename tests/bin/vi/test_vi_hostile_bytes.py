#!/usr/bin/env python3
"""A file cannot send the terminal bytes it would obey.

vi drew a high byte in no UTF-8 sequence as '^' and the byte plus 64,
which for 0xc0-0xdf is a control byte: 0xdb is ESC, so a file holding
"A\\xdb]2;PWNED\\xc7" set the window title when opened.  ex's p wrote
every byte as it was, ESC too.  Now vi and ex's p and l show such a byte
as \\ooo, a control byte as ^X, and the bytes of a C1 control (U+0080 to
U+009F, which a terminal may obey too) as \\ooo each; a printable UTF-8
character stays itself.

Usage: test_vi_hostile_bytes.py path/to/vi path/to/ex
"""
import os
import pty
import select
import signal
import subprocess
import sys
import tempfile
import time

VI = sys.argv[1]
EX = sys.argv[2]
HOSTILE = b"A\xdb]2;PWNED\xc7B\x1b[31mR\xc2\x9bZ \xc3\xa9\n"
SHOWN = b"A\\333]2;PWNED\\307B^[[31mR\\302\\233Z \xc3\xa9"
RAW = [b"\xdb", b"\xc7", b"\xc2\x9b", b"\x1b]2;PWNED", b"\x1b[31mR"]


def vi_screen(path):
    """Open path in vi, quit, and return everything vi wrote to the terminal."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.environ["LC_ALL"] = "C.UTF-8"
        os.execv(VI, [VI, path])
    out = b""
    end = time.time() + 1.5
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                out += os.read(fd, 65536)
            except OSError:
                break
    os.write(fd, b":q!\r")
    end = time.time() + 2
    while time.time() < end:
        done, _ = os.waitpid(pid, os.WNOHANG)
        if done:
            break
        time.sleep(0.1)
    else:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
    return out


def main():
    fails = []
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "f.txt")
        with open(path, "wb") as f:
            f.write(HOSTILE)

        screen = vi_screen(path)
        if SHOWN not in screen:
            fails.append("vi did not draw the line as %r" % SHOWN)
        for raw in RAW:
            if raw in screen:
                fails.append("vi wrote %r to the terminal" % raw)

        for cmd, want in ((b"%p\nq!\n", SHOWN + b"\n"), (b"%l\nq!\n", SHOWN + b"$\n")):
            got = subprocess.run([EX, "-s", path], input=cmd, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=10).stdout
            if got != want:
                fails.append("ex %r: %r, expected %r" % (cmd, got, want))
    for f in fails:
        print("FAIL: " + f)
    if fails:
        sys.exit(1)
    print("PASS: test_vi_hostile_bytes")


main()
