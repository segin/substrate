#!/usr/bin/env python3
"""Typed UTF-8 in text input, R and r (POSIX vi: any character).

Text input and r kept a key only if isprint() said so, and the C locale
says no to every byte above 0x7f: aé<ESC> on ab left ab.  r and R wrote one
byte over one byte, so a character of another length was cut in two.
Backspace in R must restore what each typed character replaced, however
many bytes either is.  Expected results are Vim's (vim -u NONE -N).

Usage: test_vi_utf8_input.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
E = "é".encode()
JA = "日本".encode()

CASES = [
    ("a with é", "ab\n", [b"a", E, b"\x1b"], "aéb\n"),
    ("i with 日本", "ab\n", [b"i", JA, b"\x1b"], "日本ab\n"),
    ("o with é", "ab\n", [b"o", E + b"x", b"\x1b"], "ab\néx\n"),
    ("r with é", "abc\n", [b"r", E], "ébc\n"),
    ("r on é", "ébc\n", [b"r", b"x"], "xbc\n"),
    ("r with 日 on é", "ébc\n", [b"r", "日".encode()], "日bc\n"),
    ("R with é", "abc\n", [b"R", E + b"x", b"\x1b"], "éxc\n"),
    ("R over é", "éb\n", [b"R", b"xy", b"\x1b"], "xy\n"),
    ("R past the end", "a\n", [b"R", b"x" + E, b"\x1b"], "xé\n"),
    ("R backspace", "abc\n", [b"R", E + E, b"\x7f\x7f", b"\x1b"], "abc\n"),
    ("R backspace over é", "éb\n", [b"R", b"x" + JA, b"\x7f\x7f\x7f", b"\x1b"],
     "éb\n"),
]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        # bytes: a character cut in two is not UTF-8
        status, _, saved = run_vi_session(VI, text, keys,
                                          return_saved_bytes=True)
        if status != 0 or saved != want.encode():
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_utf8_input")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
