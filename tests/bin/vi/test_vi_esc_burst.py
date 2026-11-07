#!/usr/bin/env python3
"""ESC followed at once by keys that make no terminal key sequence.

vi read the byte after an ESC, within 50 ms, as the start of an escape
sequence, and when it made none dropped the ESC and that byte both:
ifoo<ESC>Obar<ESC> written at once (fast typing, a paste, a slow link)
left fooarwq.  ESC must act, and each key after it after.  Each case is
one write.  Expected results are Vim's (vim -u NONE -N).

Usage: test_vi_esc_burst.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    ("ESC O", "x\n", b"ifoo\x1bObar\x1b", "bar\nfoox\n"),
    ("ESC :", "x\n", b"Ay\x1b:s/x/z/\r", "zy\n"),
    ("ESC ESC", "x\n", b"ia\x1b\x1bib\x1b", "bax\n"),
    ("ESC [ not a key", "x\n", b"ia\x1b[x\x1b", "ax\n"),
    ("arrow still a key", "ab\n", b"\x1b[Cix\x1b", "axb\n"),
]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        try:
            status, _, saved = run_vi_session(VI, text, [keys])
        except RuntimeError as e:      # still in insert mode: :wq is text
            status, saved = "hung", str(e)[:60]
        if status != 0 or saved != want:
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_esc_burst")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
