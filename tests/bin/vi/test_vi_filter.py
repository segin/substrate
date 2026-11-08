#!/usr/bin/env python3
"""! (Replace Text with Results from Shell Command) and . repeating it.

vi had no ! at all: !!tr a-z A-Z ran t, r, a and the rest as commands,
appending text.  POSIX vi: [count] ! motion shell-commands <newline> is
ex's ! over the motion's lines -- !! the current line and count - 1 after
it -- the lines replaced go to the unnamed buffer, and the current line is
the last line read in (where Vim goes back to the first).  . repeats it,
command and all.  The expected results are POSIX's.

Usage: test_vi_filter.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
UP = b"tr a-z A-Z\r"

CASES = [
    ("!!", "ab\ncd\n", [b"!!", UP], "AB\ncd\n"),
    ("2!!", "ab\ncd\nef\n", [b"2!!", UP], "AB\nCD\nef\n"),
    ("!j", "ab\ncd\nef\n", [b"!j", UP], "AB\nCD\nef\n"),
    ("!G", "ab\ncd\n", [b"!G", UP], "AB\nCD\n"),
    ("the current line is the last read in", "ab\ncd\nef\n", [b"!j", UP, b"rZ"],
     "AB\nZD\nef\n"),
    ("the lines replaced go to the unnamed buffer", "ab\ncd\n", [b"!!", UP, b"p"],
     "AB\nab\ncd\n"),
    ("u undoes it", "ab\ncd\n", [b"!!", UP, b"u"], "ab\ncd\n"),
    ("ESC at the prompt does nothing", "ab\n", [b"!!", b"\x1b", b"x"], "b\n"),
    ("a count past the end is an error", "ab\n", [b"3!!"], "ab\n"),
    ("!! then j.", "ab\ncd\nef\n", [b"!!", UP, b"j", b"."], "AB\nCD\nef\n"),
    ("!j then j.", "ab\ncd\nef\ngh\n", [b"!j", UP, b"j", b"."],
     "AB\nCD\nEF\nGH\n"),
    ("a count on .", "ab\ncd\nef\ngh\n", [b"!!", UP, b"j", b"3."],
     "AB\nCD\nEF\nGH\n"),
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
    print("PASS: test_vi_filter")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
