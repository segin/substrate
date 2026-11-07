#!/usr/bin/env python3
"""An operator whose region is empty changes nothing (POSIX vi).

dw, d$ and D on an empty line deleted nothing, yet emptied the unnamed
buffer and marked the file modified.  For each case: after the command,
:q must quit (the buffer is not modified); and after yy, the command, and
Gp, the line yanked must be what p puts.  Expected results are Vim's
(vim -u NONE -N).

Usage: test_vi_empty_ops.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

# text, keys reaching an empty region
CASES = [
    ("dw on the empty last line", "ab\n\n", [b"jdw"]),
    ("d$ on an empty line", "ab\n\n", [b"jd$"]),
    ("D on an empty line", "ab\n\n", [b"jD"]),
    ("dl on an empty line", "ab\n\n", [b"jdl"]),
    ("x on an empty line", "ab\n\n", [b"jx"]),
    ("d0 at column 0", "ab\n", [b"d0"]),
    ("dh at column 0", "ab\n", [b"dh"]),
    ("d^ at the first non-blank", "  ab\n", [b"^d^"]),
    ("df with no match", "ab\n", [b"dfz"]),
]


def main():
    fails = 0
    for name, text, keys in CASES:
        try:
            run_vi_session(VI, text, keys, final_keys=b":q\r", session_timeout=5)
        except RuntimeError:
            print(f"FAIL: {name}: the buffer is marked modified")
            fails += 1
        # yy took line 1; Gp puts it after the last line
        want = text + text.split("\n")[0] + "\n"
        _, _, saved = run_vi_session(VI, text, [b"yy"] + keys + [b"Gp"])
        if saved != want:
            print(f"FAIL: {name}: the unnamed buffer changed: {saved!r}, want {want!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_empty_ops")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
