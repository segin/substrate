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


def send_keys(master_fd, output, data, timeout=0.2):
    os.write(master_fd, data)
    output += read_some(master_fd, timeout)
    return output


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/vi", file=sys.stderr)
        return 2

    vi_path = sys.argv[1]
    fd, temp_path = tempfile.mkstemp(prefix="exvi-", text=True)
    os.write(fd, b"one\ntwo\nthree\ntwo again\nfive\n")
    os.close(fd)

    pid, master_fd = pty.fork()
    if pid == 0:
        os.execv(vi_path, [vi_path, temp_path])

    output = read_some(master_fd, 0.4)
    output = send_keys(master_fd, output, b"G")
    output = send_keys(master_fd, output, b"g")
    output = send_keys(master_fd, output, b"g")
    output = send_keys(master_fd, output, b"/")
    output = send_keys(master_fd, output, b"two\r")
    output = send_keys(master_fd, output, b"n")
    output = send_keys(master_fd, output, b"N")
    output = send_keys(master_fd, output, b"?")
    output = send_keys(master_fd, output, b"one\r")
    output = send_keys(master_fd, output, b"g")
    output = send_keys(master_fd, output, b"g")
    output = send_keys(master_fd, output, b"i")
    output = send_keys(master_fd, output, b"X")
    output = send_keys(master_fd, output, b"\x1b")
    output = send_keys(master_fd, output, b"j")
    output = send_keys(master_fd, output, b"x")
    output = send_keys(master_fd, output, b"j")
    output = send_keys(master_fd, output, b"a")
    output = send_keys(master_fd, output, b"!")
    output = send_keys(master_fd, output, b"\x1b")
    output = send_keys(master_fd, output, b"u")
    output = send_keys(master_fd, output, b"O")
    output = send_keys(master_fd, output, b"T")
    output = send_keys(master_fd, output, b"o")
    output = send_keys(master_fd, output, b"p")
    output = send_keys(master_fd, output, b"\x1b")
    output = send_keys(master_fd, output, b"o")
    output = send_keys(master_fd, output, b"t")
    output = send_keys(master_fd, output, b"a")
    output = send_keys(master_fd, output, b"i")
    output = send_keys(master_fd, output, b"l")
    output = send_keys(master_fd, output, b"\x1b")
    output = send_keys(master_fd, output, b"g")
    output = send_keys(master_fd, output, b"g")
    output = send_keys(master_fd, output, b"$")
    output = send_keys(master_fd, output, b"a")
    output = send_keys(master_fd, output, b"-")
    output = send_keys(master_fd, output, b"s")
    output = send_keys(master_fd, output, b"p")
    output = send_keys(master_fd, output, b"l")
    output = send_keys(master_fd, output, b"i")
    output = send_keys(master_fd, output, b"t")
    output = send_keys(master_fd, output, b"\r")
    output = send_keys(master_fd, output, b"l")
    output = send_keys(master_fd, output, b"i")
    output = send_keys(master_fd, output, b"n")
    output = send_keys(master_fd, output, b"e")
    output = send_keys(master_fd, output, b"\x1b")
    output = send_keys(master_fd, output, b":")
    output = send_keys(master_fd, output, b"wq\r", 0.3)

    _, status = os.waitpid(pid, 0)
    os.close(master_fd)

    exit_code = os.waitstatus_to_exitcode(status)
    decoded = output.decode("latin1", "replace")

    require(exit_code == 0, f"vi exited with status {exit_code}")
    require("\x1b[" in decoded, "missing ANSI screen output")
    require("one" in decoded, "missing initial buffer content")
    require("line 5/5" in decoded, "missing G navigation status")
    require("line 1/5" in decoded, "missing gg navigation status")
    require("line 2/5" in decoded, "missing forward search status")
    require("line 4/5" in decoded, "missing repeat search status")
    require("-- INSERT --" in decoded, "missing insert mode status")
    require(":wq" in decoded, "missing ex command prompt rendering")
    with open(temp_path, "r", encoding="utf-8") as f:
        saved = f.read()
    os.unlink(temp_path)
    require(saved == "Top-split\nline\ntail\nXone\nwo\nthree\ntwo again\nfive\n",
            f"unexpected saved buffer: {saved!r}")
    print("vi pty test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
