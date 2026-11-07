#!/usr/bin/env python3
"""Display columns, ^Y at the bottom, and where j, k and l leave the cursor.

Every character drew one column wide, so wide characters (日本語) and
combining marks put the cursor, and the column j and k keep, in the wrong
place.  ^Y that scrolled the cursor's line off the bottom left it current
(50Gz<CR>L^Y^Ydd deleted line 72, not 70).  j and k onto a shorter line,
and l, left the cursor past the line's end, where r and x did nothing, and
the column j and k aim for was not kept (lllljj on abcdef / ab / abcdef
came back at column 2).  Expected results are Vim's (vim -u NONE -N).

Usage: test_vi_columns.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
WIDE = "日本語x\nabcdefgh\n"
COMB = "éxyz\nabcdef\n"
HUNDRED = "".join(f"{i}\n" for i in range(1, 101))
SHORT = "abcdef\nab\nabcdef\n"


def without(n):
    return "".join(f"{i}\n" for i in range(1, 101) if i != n)


CASES = [
    ("j from after wide characters", WIDE, [b"fxjrZ"], "日本語x\nabcdefZh\n"),
    ("| onto a wide character", WIDE, [b"5|rZ"], "日本Zx\nabcdefgh\n"),
    ("k onto wide characters", WIDE, [b"jfgkrZ"], "日本語Z\nabcdefgh\n"),
    ("k onto a combining mark's line", COMB, [b"jllllkrZ"], "éxyZ\nabcdef\n"),
    ("^Y twice from the bottom row", HUNDRED, [b"50Gz\rL\x19\x19dd"], without(70)),
    ("^Y once from the bottom row", HUNDRED, [b"50Gz\rL\x19dd"], without(71)),
    ("^Y at the end", HUNDRED, [b"G\x19\x19dd"], without(98)),
    ("j onto a shorter line", "abcdef\nab\n", [b"lllljrZ"], "abcdef\naZ\n"),
    ("x after j onto a shorter line", "abcdef\nab\n", [b"llllj", b"x"], "abcdef\na\n"),
    ("j keeps its column", SHORT, [b"lllljjrZ"], "abcdef\nab\nabcdZf\n"),
    ("2j keeps its column", SHORT + "abcdef\n", [b"llllj2jrZ"],
     "abcdef\nab\nabcdef\nabcdZf\n"),
    ("over an empty line", "abcdef\n\nabcdef\n", [b"lllljjrZ"], "abcdef\n\nabcdZf\n"),
    ("a failed l keeps the column", SHORT, [b"lllljljrZ"], "abcdef\nab\nabcdZf\n"),
    ("h ends the column", "ab\nabcdef\n", [b"jllllkhjrZ"], "ab\nZbcdef\n"),
    ("j onto a tab", "abc\n\tx\n", [b"lljrZ"], "abc\nZx\n"),
    ("l stops at the last character", "ab\n", [b"lllrZ"], "aZ\n"),
    ("x after l past the end", "ab\n", [b"lllx"], "a\n"),
    ("5l", "ab\n", [b"5lrZ"], "aZ\n"),
    ("a after l", "ab\n", [b"lla!\x1b"], "ab!\n"),
]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        status, _, saved = run_vi_session(VI, text, keys)
        if status != 0 or saved != want:
            short = saved if len(saved) < 60 else saved[:60] + "..."
            print(f"FAIL: {name}: status {status}, {short!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_columns")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
