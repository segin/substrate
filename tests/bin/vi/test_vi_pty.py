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


def run_vi_session(vi_path, initial_text, key_steps, final_timeout=0.3):
    fd, temp_path = tempfile.mkstemp(prefix="exvi-", text=True)
    os.write(fd, initial_text.encode("utf-8"))
    os.close(fd)

    pid, master_fd = pty.fork()
    if pid == 0:
        os.execv(vi_path, [vi_path, temp_path])

    output = read_some(master_fd, 0.4)
    for step in key_steps:
        output = send_keys(master_fd, output, step)
    output = send_keys(master_fd, output, b":")
    output = send_keys(master_fd, output, b"wq\r", final_timeout)

    _, status = os.waitpid(pid, 0)
    os.close(master_fd)

    with open(temp_path, "r", encoding="utf-8") as f:
        saved = f.read()
    os.unlink(temp_path)

    return os.waitstatus_to_exitcode(status), output.decode("latin1", "replace"), saved


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/vi", file=sys.stderr)
        return 2

    vi_path = sys.argv[1]
    exit_code, decoded, saved = run_vi_session(vi_path, "one\ntwo\nthree\ntwo again\nfive\n", [
        b"G", b"g", b"g", b"/", b"two\r", b"n", b"N", b"?", b"one\r", b"g", b"g",
        b"i", b"X", b"\x1b", b"j", b"x", b"j", b"a", b"!", b"\x1b", b"u",
        b"O", b"T", b"o", b"p", b"\x1b", b"o", b"t", b"a", b"i", b"l", b"\x1b",
        b"g", b"g", b"$", b"a", b"-", b"s", b"p", b"l", b"i", b"t", b"\r",
        b"l", b"i", b"n", b"e", b"\x1b", b"w", b"e", b"b", b"y", b"y", b"p",
        b"P", b"d", b"d", b"/", b"wo\r", b"c", b"w", b"T", b"W", b"O", b"\x1b",
        b"/", b"two again\r", b"w", b"e", b"b", b"D", b"/", b"tail\r", b"C",
        b"T", b"A", b"I", b"L", b"1", b"\x1b", b"/", b"three\r", b"s", b"T",
        b"H", b"\x1b", b"/", b"two \r", b"R", b"T", b"w", b"o", b"!", b"\x1b",
        b"/", b"five\r", b"r", b"F", b"/", b"THhree\r", b"A", b"!", b"\x1b",
        b"/", b"Two!\r", b"I", b">", b"\x1b", b"/", b"Five\r", b"S", b"F",
        b"i", b"n", b"a", b"l", b"\x1b", b"/", b"Xone\r", b"J", b"/", b"tail\r",
        b"d", b"w", b"/", b"Final\r", b"c", b"c", b"D", b"o", b"n", b"e",
        b"\x1b", b"/", b"Xone\r", b"3", b"J", b"3", b"G", b"2", b"x", b".",
        b"g", b"g", b"l", b"l", b"^", b"\x06", b"H", b"M", b"L",
    ])
    require(exit_code == 0, f"vi exited with status {exit_code}")
    require("\x1b[" in decoded, "missing ANSI screen output")
    require("one" in decoded, "missing initial buffer content")
    require("line 5/5" in decoded, "missing G navigation status")
    require("line 1/5" in decoded, "missing gg navigation status")
    require("line 2/5" in decoded, "missing forward search status")
    require("line 4/5" in decoded, "missing repeat search status")
    require("line 3/6" in decoded, "missing count-based G navigation status")
    require("line 6/6" in decoded, "missing page-scroll status")
    require("-- INSERT --" in decoded, "missing insert mode status")
    require("-- REPLACE --" in decoded, "missing replace mode status")
    require(":wq" in decoded, "missing ex command prompt rendering")
    require(saved == "Top-split\nline\n1\n\nXone TWO THhree! >Two!\nDone\n",
            f"unexpected saved buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(vi_path, "alpha beta gamma\nsecond line here\n", [
        b"w",
        b"d", b"e",
        b"w",
        b"c", b"$", b"D", b"O", b"N", b"E", b"\x1b",
        b"j",
        b"0", b"w",
        b"d", b"$",
    ])
    require(exit_code == 0, f"operator vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing operator insert status")
    require("line 2/2" in decoded, "missing operator second-line status")
    require(saved == "alpha  DONE\nsecond \n",
            f"unexpected operator-motion buffer: {saved!r}")
    print("vi pty test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
