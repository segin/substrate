#!/usr/bin/env python3
"""U: the current line as it was when it last became the current line.

U was not implemented: it fell to the unknown-command beep, so xxU on abc
left c.  It is a change u undoes, and U again undoes it.  Expected results
are Vim's (vim -u NONE -N), except where Vim's U, which takes the line of
the latest change wherever the cursor is, parts from POSIX's, which takes
the current line as it was when it became current: those follow POSIX.

Usage: test_vi_undo_line.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    ("xxU", "abc\n", [b"xxU"], "abc\n"),
    ("U twice", "abc\n", [b"xxUU"], "c\n"),
    ("u after U", "abc\n", [b"xxUu"], "c\n"),
    ("x after U", "abc\n", [b"xxUx"], "bc\n"),
    ("U after o", "ab\n", [b"ofoo\x1bU"], "ab\n\n"),
    ("U after A", "ab\n", [b"Afoo\x1bU"], "ab\n"),
    ("U after :s", "ab\ncd\n", [b"x:s/b/Z/\rU"], "ab\ncd\n"),
    (". after U repeats x", "abc\n", [b"xU."], "bc\n"),
    ("U after dd", "a\nb\n", [b"ddU"], "b\n"),
    # u and redo leave the cursor on the line they put back (Vim); u put
    # it on line 1
    ("cursor after u", "a\nb\nc\nd\n", [b"jjxurZ"], "a\nb\nZ\nd\n"),
    ("cursor after u of dd", "a\nb\nc\nd\n", [b"jjddurZ"], "a\nb\nZ\nd\n"),
    ("cursor after redo", "a\nb\nc\nd\n", [b"jjxu\x12rZ"], "a\nb\n\nd\n"),
    ("cursor after u from elsewhere", "a\nb\nc\nd\n", [b"Gddggu", b"rZ"],
     "a\nb\nc\nZ\n"),
    ("u, u, redo, redo", "a\nb\nc\nd\n", [b"jdduu\x12\x12rZ"], "a\nZ\nd\n"),
    # POSIX: the current line, as it was when it became current
    ("U on another line", "abc\nd\n", [b"xxjU"], "c\nd\n"),
    ("U after coming back", "abc\nd\n", [b"xxjkU"], "c\nd\n"),
    ("U after changes on two lines", "ab\ncd\n", [b"xjxkU"], "b\nd\n"),
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
    print("PASS: test_vi_undo_line")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
