#!/usr/bin/env python3
"""%, alone and after an operator, and d/ to a later line (POSIX vi; Vim).

% off a bracket goes to the match of the first bracket after the cursor on
the line: it went to that bracket itself (%x on x (a / b) y deleted the (),
or past an unmatched bracket to one with a match (% on ((a) went to the )).
d% took the region from that bracket rather than from the cursor.  A d
over lines that starts in the indent and leaves only blanks after it takes
the whole lines, as Vim's.  And d/pattern from column 0 to a later line
took the target's whole line when the match was not at its column 0.
Expected results are Vim's (vim -u NONE -N).

Usage: test_vi_percent.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    ("% from a non-bracket, across lines", "x (a\nb) y\n", [b"%x"], "x (a\nb y\n"),
    ("d% from a non-bracket", "f(a,\n b) x\n", [b"d%"], " x\n"),
    ("% on a bracket", "a(b)c\n", [b"%x"], "a(bc\n"),
    ("% back", "a(b)c\n", [b"$%x"], "a(b)\n"),
    ("% outer", "a[b{c}d]e\n", [b"%rZ"], "a[b{c}dZe\n"),
    ("% inner", "a[b{c}d]e\n", [b"lll%rZ"], "a[b{cZd]e\n"),
    ("% with no bracket", "abc\n", [b"%rZ"], "Zbc\n"),
    ("% to a later line", "x {\n y\n}\n", [b"%rZ"], "x {\n y\nZ\n"),
    ("d% over whole lines", "x {\n y\n}\n", [b"d%"], ""),
    ("d% back from a non-bracket", "(a\nb)\n", [b"jd%"], ")\n"),
    ("% to an unmatched )", "a)b(c\n", [b"%rZ"], "Z)b(c\n"),
    ("y%", "x (a\nb) y\n", [b"y%P"], "x (a\nb)x (a\nb) y\n"),
    ("c%", "f(a,\n b) x\n", [b"c%Z\x1b"], "Z x\n"),
    ("% on an unmatched (", "((a)\n", [b"%rZ"], "Z(a)\n"),
    ("c% .", "a(b)c(d)e\n", [b"c%Z\x1bll."], "ZcZe\n"),
    ("d% leaving text after", "a {\nb\n} c\n", [b"d%"], " c\n"),
    ("d/ to a later line's middle", "one\ntwo three\nfour\n", [b"d/three\r"],
     "three\nfour\n"),
    ("d/ to a later line's start", "one\ntwo three\nfour\n", [b"d/two\r"],
     "two three\nfour\n"),
    ("c/ to a later line's middle", "one\ntwo three\nfour\n",
     [b"c/three\rX\x1b"], "Xthree\nfour\n"),
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
    print("PASS: test_vi_percent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
