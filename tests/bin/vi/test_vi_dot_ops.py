#!/usr/bin/env python3
""". after d, < and > with any motion, and the cursor after D (POSIX vi).

. repeated only the deletes that kept a record of their own -- not dl, dh,
de, db, d), d}, d(, d{, d/ or d?, nor any < or >: de. deleted one word, and
>j. repeated whatever change came before.  Now the operator keeps the keys
it read after its motion key, and . replays them, with the count given to
. or the operator's own; dG with no count stays to the last line.  D and d$
left the cursor past the line's end, so x and . after them found nothing.
Expected results are Vim's (vim -u NONE -N).

Usage: test_vi_dot_ops.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    ('de.', 'aa bb cc dd ee ff\n', [b'de.'], ' cc dd ee ff\n'),
    ('dl.', 'aa bb cc dd ee ff\n', [b'wwdl.'], 'aa bb  dd ee ff\n'),
    ('dh.', 'aa bb cc dd ee ff\n', [b'$dh.'], 'aa bb cc dd eef\n'),
    ('db.', 'aa bb cc dd ee ff\n', [b'$db.'], 'aa bb cc dd f\n'),
    ('dB.', 'aa bb cc dd ee ff\n', [b'$dB.'], 'aa bb cc dd f\n'),
    ('d).', 'one. two. three. four. five.\n', [b'd).'], 'three. four. five.\n'),
    ('d}.', 'a\n\nb\n\nc\n\nd\n', [b'd}.'], '\nc\n\nd\n'),
    ('d(.', 'one. two. three. four. five.\n', [b'$d(.'], 'one. two. three. .\n'),
    ('d{.', 'a\n\nb\n\nc\n\nd\n', [b'Gd{.'], 'a\n\nb\nd\n'),
    ('d/a.', 'x a y a z a\n', [b'd/a\r', b'.'], 'a z a\n'),
    ('d?a.', 'x a y a z a\n', [b'$d?a\r', b'.'], 'x a\n'),
    ('>j.', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'>j.'], '\t\tl1\n\t\tl2\nl3\nl4\nl5\nl6\nl7\nl8\n'),
    ('<j after >j', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'<j>j.'], '\t\tl1\n\t\tl2\nl3\nl4\nl5\nl6\nl7\nl8\n'),
    ('>>.', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'>>.'], '\t\tl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n'),
    ('2>> j .', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'2>>j.'], '\tl1\n\t\tl2\n\tl3\nl4\nl5\nl6\nl7\nl8\n'),
    ('dj.', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'dj.'], 'l5\nl6\nl7\nl8\n'),
    ("d'a.", 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b"jjmakkd'a."], 'l4\nl5\nl6\nl7\nl8\n'),
    ('dG.', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'jjdG.'], 'l1\n'),
    ('dgg.', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'5Gdgg.'], 'l7\nl8\n'),
    ('dfx.', 'axbxcxdx\n', [b'dfx.'], 'cxdx\n'),
    ('dtx.', 'axbxcxdx\n', [b'dtx.'], 'xcxdx\n'),
    ('d2w then 3.', 'aa bb cc dd ee ff\n', [b'd2w3.'], 'ff\n'),
    ('2dw.', 'aa bb cc dd ee ff\n', [b'2dw.'], 'ee ff\n'),
    ('dd then 2.', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'dd2.'], 'l4\nl5\nl6\nl7\nl8\n'),
    ('>2j.', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'>2j.'], '\t\tl1\n\t\tl2\n\t\tl3\nl4\nl5\nl6\nl7\nl8\n'),
    ('dw..', 'aa bb cc dd ee ff\n', [b'dw..'], 'dd ee ff\n'),
    ('u then .', 'abc def\n', [b'dl', b'u', b'.'], 'bc def\n'),
    ('3>> u .', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'3>>u.'], '\tl1\n\tl2\n\tl3\nl4\nl5\nl6\nl7\nl8\n'),
    ('d$.', 'aa bb cc dd ee ff\n', [b'wd$.'], 'aa\n'),
    ('d$ then x', 'aa bb cc dd ee ff\n', [b'wd$x'], 'aa\n'),
    ('D then x', 'aa bb cc dd ee ff\n', [b'wDx'], 'aa\n'),
    ('d3G.', 'l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n', [b'jjd3G.'], 'l1\nl2\nl5\nl6\nl7\nl8\n'),
    ('D.', 'aa bb cc dd ee ff\n', [b'wD.'], 'aa\n'),]


def main():
    fails = 0
    for name, text, keys, want in CASES:
        status, _, saved = run_vi_session(VI, text, keys)
        if status != 0 or saved != want:
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_dot_ops")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
