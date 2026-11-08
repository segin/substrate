#!/usr/bin/env python3
"""A character and its combining marks are one cursor position.

e followed by U+0301 is é on the screen, and Vim's cursor treats it as one
character: l and h step over it whole, x, X, dl and r take or replace it
whole, and i and a insert before or after it.  vi made each mark a position
of its own: l from the e landed on the accent (in the same column), x took
the e from under it, r put Z under the accent and i after l inserted
between the e and its accent.  POSIX says nothing of combining marks, so the
expected results are Vim's (vim -u NONE -N).

Usage: test_vi_combining.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
E = "é"
A = "á"
B = "b́"

CASES = [
    ("l over a combining mark", E + "xyz\n", [b"lrZ"], E + "Zyz\n"),
    ("x takes the mark too", E + "xyz\n", [b"x"], "xyz\n"),
    ("r replaces the mark too", E + "xyz\n", [b"rZ"], "Zxyz\n"),
    ("dl takes the mark too", E + "xyz\n", [b"dl"], "xyz\n"),
    ("X takes the mark too", E + "xyz\n", [b"lX"], "xyz\n"),
    ("h onto a combining mark", E + "xyz\n", [b"$hhrZ"], E + "Zyz\n"),
    ("$ on a line ending in a mark", "ab" + E + "\n", [b"$rZ"], "abZ\n"),
    ("| after a mark", "a" + E + "bc\n", [b"3|rZ"], "a" + E + "Zc\n"),
    ("w over a mark", "a" + E + " xyz\n", [b"wrZ"], "a" + E + " Zyz\n"),
    ("i after l", E + "xyz\n", [b"liQ\x1b"], E + "Qxyz\n"),
    ("a on a mark's character", E + "xyz\n", [b"aQ\x1b"], E + "Qxyz\n"),
    ("2l over two", E + A + B + "c\n", [b"2lrZ"], E + A + "Zc\n"),
    ("3x over marks", E + A + B + "c\n", [b"3x"], "c\n"),
    ("~ keeps the mark", E + "xyz\n", [b"~"], "Éxyz\n"),
    ("j keeps the column over marks", E + A + "c\nxyzw\n", [b"2ljrZ"],
     E + A + "c\nxyZw\n"),
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
    print("PASS: test_vi_combining")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
