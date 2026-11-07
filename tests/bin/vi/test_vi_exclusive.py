#!/usr/bin/env python3
"""An exclusive region that ends at column 0 of a later line (POSIX vi, Vim).

It ends at the end of the line before instead; when it also starts at or
before its line's first non-blank, the operator takes the whole lines.  d`a
from line 1 to a mark at column 0 of line 3 left an empty line where it
deleted lines 1 and 2, and 2d) over text, blank, text kept the blank line.
A mark set after j onto a shorter line recorded the remembered column, past
the line's end, so the region took that line too.  Expected results are
Vim's (vim -u NONE -N).

Usage: test_vi_exclusive.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
PARA = "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n"

CASES = [
    ("d`a to column 0", "a\nb\nc\nd\n", [b"jjmakk", b"d`a"], "c\nd\n"),
    ("y`a to column 0", "a\nb\nc\nd\n", [b"jjmakk", b"y`a", b"P"],
     "a\nb\na\nb\nc\nd\n"),
    ("c`a to column 0", "a\nb\nc\nd\n", [b"jjmakk", b"c`aZ\x1b"], "Z\nc\nd\n"),
    ("d`a from the indent", "  x\nb\nc\n", [b"jjmakk", b"d`a"], "c\n"),
    ("d`a past the first non-blank", "x a\nb\nc\n",
     [b"l", b"jjmakk", b"d`a"], "x\nc\n"),
    ("2d) over a blank line", PARA, [b"2", b"d", b")"],
     "alpha beta gamma delta\n\nsec\n{\nbody\n}\n"),
    ("2y) over a blank line", PARA, [b"2", b"y", b")", b"P"],
     "one two three four five six\n\n" + PARA),
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
    print("PASS: test_vi_exclusive")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
