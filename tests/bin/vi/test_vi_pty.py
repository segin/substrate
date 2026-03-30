#!/usr/bin/env python3

import os
import pty
import select
import sys
import tempfile
import time


def read_some(master_fd, timeout):
    out = bytearray()
    end = time.time() + timeout
    while time.time() < end:
        ready, _, _ = select.select([master_fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(master_fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        out.extend(chunk)
    return bytes(out)


def require(cond, msg):
    if not cond:
        print(f"FAIL: {msg}", file=sys.stderr)
        raise SystemExit(1)


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/vi", file=sys.stderr)
        return 2

    vi_path = sys.argv[1]
    fd, temp_path = tempfile.mkstemp(prefix="exvi-", text=True)
    os.write(fd, b"one\ntwo\nthree\nfour\nfive\n")
    os.close(fd)

    pid, master_fd = pty.fork()
    if pid == 0:
        os.execv(vi_path, [vi_path, temp_path])

    output = read_some(master_fd, 0.4)
    os.write(master_fd, b"G")
    output += read_some(master_fd, 0.2)
    os.write(master_fd, b"gg")
    output += read_some(master_fd, 0.2)
    os.write(master_fd, b"\x06")
    output += read_some(master_fd, 0.2)
    os.write(master_fd, b"\x02")
    output += read_some(master_fd, 0.2)
    os.write(master_fd, b":q\r")
    output += read_some(master_fd, 0.3)

    _, status = os.waitpid(pid, 0)
    os.close(master_fd)
    os.unlink(temp_path)

    exit_code = os.waitstatus_to_exitcode(status)
    decoded = output.decode("latin1", "replace")

    require(exit_code == 0, f"vi exited with status {exit_code}")
    require("\x1b[" in decoded, "missing ANSI screen output")
    require("one" in decoded, "missing initial buffer content")
    require("line 5/5" in decoded, "missing G navigation status")
    require("line 1/5" in decoded, "missing gg navigation status")
    require(":q" in decoded, "missing ex command prompt rendering")
    print("vi pty test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
