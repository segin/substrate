#!/usr/bin/env python3
"""Where commands leave the cursor (POSIX vi).

x that deleted the line's last character left the cursor past the new
end, where r found nothing, and j kept that column.  yy, Y and a line
yank toward the end (yj, yG, yL) moved the cursor to column 1; they leave
it where it was.  A yank toward the start (yk) goes to the region's first
line at its first character, where POSIX and Vim (which keeps the column)
part: the expected result is POSIX's.

Usage: test_vi_cursor_after.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    # x: onto the new last character when it took the last
    ("x on the last character", "ab\n", [b"lxrZ"], "Z\n"),
    ("x then j", "abc\nabcdef\n", [b"$xjrZ"], "ab\naZcdef\n"),
    ("x on a two-character line, then j", "ab\nab\nabcdef\n", [b"jlxjrZ"],
     "ab\na\nZbcdef\n"),
    ("x. on the last character", "abc\n", [b"$x.rZ"], "Z\n"),
    ("x in the middle stays", "abc\n", [b"lxrZ"], "aZ\n"),
    ("5x past the end", "abcd\n", [b"l5xrZ"], "Z\n"),
    # a line yank toward the end: the cursor stays
    ("yy", "abc\nd\n", [b"llyyrZ"], "abZ\nd\n"),
    ("2yy", "abc\nd\n", [b"ll2yyrZ"], "abZ\nd\n"),
    ("Y", "abc\nd\n", [b"llYrZ"], "abZ\nd\n"),
    ("yj", "abc\nabc\n", [b"llyjrZ"], "abZ\nabc\n"),
    ("yG", "abc\nabc\n", [b"llyGrZ"], "abZ\nabc\n"),
    ("yy then j", "abcd\nabcd\n", [b"llyyjrZ"], "abcd\nabZd\n"),
    # toward the start: the first line, its first character
    ("yk", "abc\nabc\n", [b"jllykrZ"], "Zbc\nabc\n"),
    ("y'a backward", "abc\nabc\nabc\n", [b"llmajjly'arZ"],
     "Zbc\nabc\nabc\n"),
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
    print("PASS: test_vi_cursor_after")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
