#!/usr/bin/env python3
"""w, b and e, alone and after d, c and y (POSIX vi).

A word is a run of letters, digits and underscores, or a run of the other
non-blanks; w took only the first kind for words, so it went past the . in
foo.bar and dw deleted it.  The word operators did not leave the line (3dw
on a b / c d, c3w), e under an operator did not go on from a word's last
character (lde on ab cd), and c on a one-character region did nothing.
Expected results are Vim's (vim -u NONE -N), but for cw on a blank, which
POSIX has change that blank alone (Vim changes the blanks).

Usage: test_vi_words.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
FB = "foo.bar baz\n"

CASES = [
    ("dw stops at punctuation", FB, [b"dw"], ".bar baz\n"),
    ("w stops at punctuation", FB, [b"wrZ"], "fooZbar baz\n"),
    ("2w", FB, [b"2wrZ"], "foo.Zar baz\n"),
    ("w onto punctuation after blanks", "x  ;;y z\n", [b"wrZ"], "x  Z;y z\n"),
    ("dw over blanks", "x  ;;y z\n", [b"dw"], ";;y z\n"),
    ("w from a word to punctuation", "ab_c+d\n", [b"wrZ"], "ab_cZd\n"),
    ("3dw across lines", "a b\nc d\n", [b"3dw"], "d\n"),
    ("2dw stops at the line's end", "a b\n\nc\n", [b"2dw"], "\n\nc\n"),
    ("3dw over an empty line", "a b\n\nc\n", [b"3dw"], "c\n"),
    ("w2dw at the last word", "ab cd\nef\n", [b"w2dw"], "ab \n"),
    ("c3w across lines", "a b\nc d\ne\n", [b"c3wX\x1b"], "X d\ne\n"),
    ("lde from a word's end", "ab cd\n", [b"lde"], "a\n"),
    ("lce from a word's end", "ab cd\n", [b"lceX\x1b"], "aX\n"),
    ("ye from a one-letter word", "a.b c\n", [b"yeP"], "a.a.b c\n"),
    ("y3w across lines", "ab cd\nef gh\n", [b"y3wP"], "ab cd\nef ab cd\nef gh\n"),
    ("3ye across lines", "ab cd\nef gh\n", [b"3yeP"], "ab cd\nefab cd\nef gh\n"),
    ("3dW to the end", "a.b c.d e\n", [b"3dW"], "\n"),
    ("$cw on the last character", "ab\n", [b"$cwX\x1b"], "aX\n"),
    ("cw on a word", "foo bar\n", [b"cwX\x1b"], "X bar\n"),
    ("c2w", "foo  bar\n", [b"c2wX\x1b"], "X\n"),
    ("cw on a blank changes the blank", "a   b\n", [b"lcwX\x1b"], "aX  b\n"),
    ("dw at a line's end", "ab\ncd\n", [b"$dw"], "a\ncd\n"),
    ("dw on trailing blanks", "ab  \ncd\n", [b"lldw"], "ab\ncd\n"),
    ("dw.", "ab cd ef\n", [b"dw."], "ef\n"),
    ("cw.", "ab cd ef\n", [b"cwX\x1bw."], "X X ef\n"),
    ("ce.", "ab cd ef\n", [b"ceX\x1bw."], "X X ef\n"),
    ("e over an empty line", "ab\n\ncd\n", [b"2erZ"], "ab\n\ncZ\n"),
    ("b onto an empty line", "ab\n\n\ncd\n", [b"GbrZ"], "ab\n\n\ncd\n"),
    ("2b across lines", "a\n b\n", [b"$2bD"], "\n b\n"),
    # \< inside a pattern sees the character before the search's start
    ("/ with \\< after an alternation", "aa a ba\n", [b"/x\\|\\<a\r", b"rZ"],
     "aa Z ba\n"),
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
    print("PASS: test_vi_words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
