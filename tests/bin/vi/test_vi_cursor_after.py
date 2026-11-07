#!/usr/bin/env python3
"""Where commands leave the cursor (POSIX vi).

x that deleted the line's last character left the cursor past the new
end, where r found nothing, and j kept that column.  yy, Y and a line
yank toward the end (yj, yG, yL) moved the cursor to column 1; they leave
it where it was.  A yank toward the start (yk) goes to the region's first
line at its first character, where POSIX and Vim (which keeps the column)
part: the expected result is POSIX's.  ^F and ^B kept the column; they
go to the first non-blank.  And they moved the cursor rows - 2 lines, a
line more than POSIX's count x (window - 2), and went past the last line
where POSIX keeps it at the bottom of the screen, and the current line
moves with the screen; Vim differs there, and the expected results are
POSIX's.

Usage: test_vi_cursor_after.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
INDENTED = "".join(f"  l{i}\n" for i in range(1, 61))


def at(n, col=2):
    """INDENTED with line n's column col replaced by Z: where r found it."""
    lines = INDENTED.split("\n")
    lines[n - 1] = lines[n - 1][:col] + "Z" + lines[n - 1][col + 1:]
    return "\n".join(lines)

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
    # ^F and ^B (24 rows, window 23): the first non-blank of the line
    # count x 21 lines on, or the new screen's first line
    ("^F", INDENTED, [b"\x06rZ"], at(22)),
    ("^F from a later column", INDENTED, [b"$\x06rZ"], at(22)),
    ("^F from mid-screen", INDENTED, [b"10G\x06rZ"], at(31)),
    ("^F^F stops with the last line at the bottom", INDENTED,
     [b"\x06\x06rZ"], at(38)),
    ("2^F", INDENTED, [b"2\x06rZ"], at(38)),
    ("^F on the last line", INDENTED, [b"G$\x06rZ"], at(60, 4)),
    ("^F with window 2", INDENTED, [b":set window=2\r\x06rZ"], at(24)),
    ("^B", INDENTED, [b"\x06\x06\x02rZ"], at(17)),
    ("^B from the last line", INDENTED, [b"G\x02rZ"], at(39)),
    ("^B from a later column", INDENTED, [b"G$\x02rZ"], at(39)),
    ("^B on the first line", INDENTED, [b"$\x02rZ"], at(1, 3)),
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
