#!/usr/bin/env python3
"""^@ in text input: the text last entered, then the end of text input.

POSIX vi gives ^@ as the first character of text input this meaning; it
was ignored, and only ^A inserted the last text (without ending input), so
i^@ left vi in text input.  Expected results are Vim's (vim -u NONE -N),
which takes ^@ so anywhere in the input.

Usage: test_vi_ctrl_at.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    ("^@ first", "a\nb\n", [b"ifoo\x1b", b"j0i\x00", b"x"], "fooa\nfob\n"),
    ("^A still inserts", "a\nb\n", [b"ifoo\x1b", b"j0i\x01\x1b"], "fooa\nfoob\n"),
    ("^@ after text", "a\nb\n", [b"ifoo\x1b", b"j0ix\x00", b"rZ"], "fooa\nxfoZb\n"),
    ("^@ with two lines", "a\nb\n", [b"ifoo\rbar\x1b", b"G0i\x00"],
     "foo\nbara\nfoo\nbarb\n"),
    ("^@ with no text before", "a\n", [b"i\x00", b"x"], "\n"),
    (". after A^@", "a\nb\n", [b"ifoo\x1b", b"jA\x00", b"."], "fooa\nbfoofoo\n"),
]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        try:
            status, _, saved = run_vi_session(VI, text, keys, session_timeout=5)
        except RuntimeError:
            status, saved = "hung", "(still in text input)"
        if status != 0 or saved != want:
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_ctrl_at")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
