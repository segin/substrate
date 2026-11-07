#!/usr/bin/env python3
"""Counts on i a I A o O R r s S, and on . after them (POSIX vi).

None of these took its count: 3ix<ESC> put one x, 3rx replaced one
character, and the count was left for the next command, so 3ix<ESC>0x
became 30x and deleted the line.  The text of a counted insert goes in
count times; r, s and S act on count characters or lines; u undoes each
as one change.  Expected results are Vim's (vim -u NONE -N).

Usage: test_vi_counts.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    ("3i", "ab\n", [b"3ix\x1b"], "xxxab\n"),
    ("3i two characters", "ab\n", [b"3ixy\x1b"], "xyxyxyab\n"),
    ("3a", "ab\n", [b"3ax\x1b"], "axxxb\n"),
    ("3A", "ab\n", [b"3Ax\x1b"], "abxxx\n"),
    ("3I", "  ab\n", [b"3Ix\x1b"], "  xxxab\n"),
    ("3o", "ab\ncd\n", [b"3ofoo\x1b"], "ab\nfoo\nfoo\nfoo\ncd\n"),
    ("3O", "ab\ncd\n", [b"3Ofoo\x1b"], "foo\nfoo\nfoo\nab\ncd\n"),
    ("2o two lines", "ab\ncd\n", [b"2ofoo\rbar\x1b"],
     "ab\nfoo\nbar\nfoo\nbar\ncd\n"),
    ("3R", "xyzwvu\n", [b"3Rab\x1b"], "ababab\n"),
    ("3R past the end", "xyz\n", [b"3Rab\x1b"], "ababab\n"),
    ("3r", "abcd\n", [b"3rx"], "xxxd\n"),
    ("3r on two", "ab\n", [b"3rx"], "ab\n"),
    ("2s", "abcd\n", [b"2sX\x1b"], "Xcd\n"),
    ("9s", "abcd\n", [b"9sX\x1b"], "X\n"),
    ("2S", "a\nb\nc\nd\n", [b"2SX\x1b"], "X\nc\nd\n"),
    ("5S", "a\nb\n", [b"5SX\x1b"], "X\n"),
    ("the count is used up", "ab\n", [b"3ix\x1b0x"], "xxab\n"),
    ("3.", "ab\n", [b"ixy\x1b", b"3."], "xxyxyxyyab\n"),
    (". keeps the count", "ab\n", [b"2ix\x1b", b"."], "xxxxab\n"),
    (". after 2r", "abcd\n", [b"2rx", b"l."], "xxxx\n"),
    (". after 2s", "abcdef\n", [b"2sX\x1b", b"l."], "XXef\n"),
    ("u after 3o", "ab\ncd\n", [b"3ofoo\x1b", b"u"], "ab\ncd\n"),
    ("u after 2S", "a\nb\nc\n", [b"2SX\x1b", b"u"], "a\nb\nc\n"),
    ("u after 3r", "abcd\n", [b"3rx", b"u"], "abcd\n"),
    ("2S to a buffer", "a\nb\nc\n", [b'"q2SX\x1b', b'G"qp'], "X\nc\na\nb\n"),
]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        try:
            status, _, saved = run_vi_session(VI, text, keys)
        except RuntimeError as e:
            status, saved = "hung", str(e)[:60]
        if status != 0 or saved != want:
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_counts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
