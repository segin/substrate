#!/usr/bin/env python3
"""Counts past the buffer's ends, and $'s line's-end column (POSIX vi).

A count running past the first or last line is an error -- the terminal
alerted, nothing changed -- for j, ^J, +, <carriage-return>, k, -, $, dd,
yy, Y, <<, >> and _, and for j, + and $ as an operator's motion: exvi went
as far as there were lines (as Vim does).  And after $ the current column
is the line's end: commands that change the line and not the column (j,
k) keep to the end of each line, until a command sets the column; exvi
kept the column number $ had found.  Where POSIX and Vim agree (the $
column), the expected results are Vim's.

Usage: test_vi_count_ends.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
T = "a\nb\nc\n"
COLS = "abcdef\nab\nabcd\nabcdefgh\n"

CASES = [
    # in reach: the move or the change
    ("2j", T, [b"2jrZ"], "a\nb\nZ\n"),
    ("G2k", T, [b"G2krZ"], "Z\nb\nc\n"),
    ("3dd", T, [b"3dd"], ""),
    ("j2yyP", T, [b"j2yyP"], "a\nb\nc\nb\nc\n"),
    ("3>>", T, [b"3>>"], "\ta\n\tb\n\tc\n"),
    ("d2j", T, [b"d2j"], ""),
    ("3$", T, [b"3$rZ"], "a\nb\nZ\n"),
    # past the end or the start: an error, nothing moved or changed
    ("3j", T, [b"3jrZ"], "Z\nb\nc\n"),
    ("3+", T, [b"3+rZ"], "Z\nb\nc\n"),
    ("3<CR>", T, [b"3\rrZ"], "Z\nb\nc\n"),
    ("G3k", T, [b"G3krZ"], "a\nb\nZ\n"),
    ("G3-", T, [b"G3-rZ"], "a\nb\nZ\n"),
    ("4dd", T, [b"4dd"], T),
    ("j3yy", T, [b"j3yyP"], T),
    ("j3Y", T, [b"j3YP"], T),
    ("4>>", T, [b"4>>"], T),
    ("j3d_", T, [b"j3d_"], T),
    ("d3j", T, [b"d3j"], T),
    ("3d3j", T, [b"3d3j"], T),
    ("dd then 2. on the last line", T, [b"jdd2."], "a\nc\n"),
    ("4$", T, [b"4$rZ"], "Z\nb\nc\n"),
    ("d4$", T, [b"d4$"], T),
    # $ keeps to each line's end
    ("$jjj", COLS, [b"$jjjrQ"], "abcdef\nab\nabcd\nabcdefgQ\n"),
    ("$j onto a longer line", "abc\nabcdef\n", [b"$jrZ"], "abc\nabcdeZ\n"),
    ("$jk", "ab\nabcdef\n", [b"$jkjrZ"], "ab\nabcdeZ\n"),
    # until a command sets the column: r, h, 0, text input
    ("$j r", COLS, [b"$jrZjrYjrX"], "abcdef\naZ\naYcd\naXcdefgh\n"),
    ("$h", "abcdef\nabcdef\nabcdefgh\n", [b"$hjrZ"], "abcdef\nabcdZf\nabcdefgh\n"),
    ("$j0j", "abcdef\nab\nabcd\n", [b"$j0jrZ"], "abcdef\nab\nZbcd\n"),
    ("$jA", "abcdef\nab\nabcdefgh\n", [b"$jAq\x1bjrZ"], "abcdef\nabq\nabZdefgh\n"),
]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        status, _, saved = run_vi_session(VI, text, keys)
        if status != 0 or saved != want:
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_count_ends")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
