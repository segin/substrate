#!/usr/bin/env python3
"""^H, ^W and ^U in text input (POSIX vi).

They erase only what this input entered on the current line, leave the
buffers alone, and are part of the one change u undoes.  They went through
the delete operator: A^W<ESC>jp on foo bar erased bar and p put it back, u
after A baz^Wqq<ESC> undid only part, A<BS> on ab erased the b, backspace
at the start of a line joined it to the one before, and ^H erased one byte
of é.  ^W also took a.b as one word.  Expected results are Vim's with
backspace= (vim -u NONE -N --cmd 'set backspace='), traditional vi's.

Usage: test_vi_input_erase.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    ("^W before the input", "foo bar\nz\n", [b"A\x17\x1b", b"jp"],
     "foo bar\nz\n"),
    ("u after ^W", "foo bar\n", [b"A baz\x17qq\x1b", b"u"], "foo bar\n"),
    ("^H before the input", "ab\n", [b"A\x7f\x1b"], "ab\n"),
    ("^H (0x08) before the input", "ab\n", [b"A\x08\x1b"], "ab\n"),
    ("^H", "Z\n", [b"ixyz\x7f\x1b"], "xyZ\n"),
    ("^H does not join lines", "Z\n", [b"iab\rc\x7f\x7f\x7f\x1b"], "ab\nZ\n"),
    ("^U", "Z\n", [b"ifoo bar\x15baz\x1b"], "bazZ\n"),
    ("^W", "Z\n", [b"ifoo bar\x17qux\x1b"], "foo quxZ\n"),
    ("^H on é", "Z\n", [b"i\xc3\xa9\x7fx\x1b"], "xZ\n"),
    ("^U to the input's start", "one two\n", [b"A x\x15\x1b"], "one two\n"),
    ("^U on a new line", "Z\n", [b"ia\rb\x15c\x1b"], "a\ncZ\n"),
    ("^W at the input's start", "ab cd\n", [b"$i\x17\x1b"], "ab cd\n"),
    ("^W over blanks", "Z\n", [b"ifoo  \x17x\x1b"], "xZ\n"),
    ("^W word classes", "Z\n", [b"ia.b\x17\x17x\x1b"], "axZ\n"),
    ("^H leaves the buffer", "x\n", [b"yy", b"A\x7f\x7f\x1b", b"p"], "x\nx\n"),
    ("^U leaves the buffer", "q\nz\n", [b"yy", b"ifoo bar\x15\x1b", b"jp"],
     "q\nz\nq\n"),
    ("^W leaves the buffer", "q\nz\n", [b"yy", b"ifoo bar\x17\x1b", b"jp"],
     "foo q\nz\nq\n"),
    ("u after ^U", "ab\n", [b"Ax\x15y\x1b", b"u"], "ab\n"),
]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        status, _, saved = run_vi_session(VI, text, keys, return_saved_bytes=True)
        if status != 0 or saved != want.encode():
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_input_erase")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
