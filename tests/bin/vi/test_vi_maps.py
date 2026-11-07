#!/usr/bin/env python3
"""map, map!, unmap, abbreviate (POSIX vi) -- all "Unknown command".

A command-mode map's keys give way to its right-hand side, mapped again
only with remap set; a prefix of a longer lhs that goes on otherwise is
taken as typed.  map! maps keys in text input, and an abbreviation's word,
ended by a non-word character, gives way to its rhs.  Expected results
are Vim's (vim -u NONE -N).

Usage: test_vi_maps.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
T = "a\nb\nc\n"

CASES = [
    ("map", T, [b":map K dd\r", b"K"], "b\nc\n"),
    ("map!", T, [b":map! jj X\r", b"ijjY\x1b"], "XYa\nb\nc\n"),
    ("abbreviate", T, [b":ab teh the\r", b"iteh \x1b"], "the a\nb\nc\n"),
    ("abbreviation inside a word", T, [b":ab teh the\r", b"ixteh \x1b"], "xteh a\nb\nc\n"),
    ("abbreviation at the input's start", T, [b":ab teh the\r", b"Ateh\x1b"], "athe\nb\nc\n"),
    ("unmap", T, [b":map K dd\r", b":unmap K\r", b"Kx"], "\nb\nc\n"),
    ("noremap", T, [b":set noremap\r", b":map K L\r", b":map L dd\r", b"K"], T),
    ("remap", T, [b":map K L\r", b":map L dd\r", b"K"], "b\nc\n"),
    ("a prefix that goes on otherwise", T, [b":map ab dd\r", b"ax\x1b"], "ax\nb\nc\n"),
    ("a two-key lhs", T, [b":map ab dd\r", b"ab"], "b\nc\n"),
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
    print("PASS: test_vi_maps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
