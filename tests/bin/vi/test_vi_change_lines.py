#!/usr/bin/env python3
"""cc with a count (POSIX vi, Change; S is c_).

ncc gave its new line no indent where autoindent was set, though POSIX
gives it the first changed line's; and it changed to the end when fewer
than count - 1 lines followed, which POSIX makes an error that changes
nothing (Vim changes to the end; POSIX is followed).  Expected results are
otherwise Vim's (vim -u NONE -N).

Usage: test_vi_change_lines.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
IND = "\tfoo\n  bar\nbaz\n"

CASES = [
    # an error: nothing changes, and X and the rest are commands
    ("5cc on two lines", "a\nb\n", [b"5ccX\x1b"], "a\nb\n"),
    ("3cc with one line after", "a\nb\nc\n", [b"j3ccX\x1b"], "a\nb\nc\n"),
    ("2cc with one line after", "a\nb\nc\n", [b"j2ccX\x1b"], "a\nX\n"),
    ("2cc, autoindent", IND, [b":set ai\r", b"2ccX\x1b"], "\tX\nbaz\n"),
    ("cc, autoindent", IND, [b":set ai\r", b"ccX\x1b"], "\tX\n  bar\nbaz\n"),
    ("2cc, noautoindent", IND, [b"2ccX\x1b"], "X\nbaz\n"),
    ("cj, autoindent", IND, [b":set ai\r", b"cjX\x1b"], "\tX\nbaz\n"),
    ("2S, autoindent", "  foo\nbar\n", [b":set ai\r", b"2SX\x1b"], "  X\n"),
    ("u after 2cc", "a\nb\nc\n", [b"2ccX\x1bu"], "a\nb\nc\n"),
    (". after 2cc on the last line", "a\nb\nc\n", [b"2ccX\x1bj."], "X\nc\n"),
    ("2cc to a buffer", "a\nb\nc\n", [b'"q2ccX\x1bG"qp'], "X\nc\na\nb\n"),
]


# the error is reported: the terminal alerted (and not for one in reach)
BELLS = [
    ("5cc on two lines alerts", "a\nb\n", [b"5cc"], True),
    ("3cc with one line after alerts", "a\nb\nc\n", [b"j3cc"], True),
    ("2cc in reach does not", "a\nb\n", [b"2ccX\x1b"], False),
]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        status, _, saved = run_vi_session(VI, text, keys)
        if status != 0 or saved != want:
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1
    for name, text, keys, bell in BELLS:
        status, out, _ = run_vi_session(VI, text, keys)
        if status != 0 or ("\a" in out) != bell:
            print(f"FAIL: {name}: status {status}, alerted {'\a' in out}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_change_lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
