#!/usr/bin/env python3
"""^D, 0^D, ^^D and ^T in text input, and >> and <<, by shiftwidth (POSIX vi).

They shifted by tabstop, shiftwidth having no effect; ^D lowered the indent
wherever it was typed; and 0 and ^ before ^D were taken for text (o0^Dx
with autoindent on \\tfoo gave 0x).  ^T and ^D now go to the next or
previous multiple of shiftwidth, >> and << by one shiftwidth; 0^D takes
away the indent, ^^D for the current line only.  Expected results are
Vim's (vim -u NONE -N), but for ^D after text, which POSIX has change
nothing (Vim lowers the indent).

Usage: test_vi_indent_keys.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

CASES = [
    ('^D to the previous shiftwidth', '\tfoo\n', [b':set ai sw=4\r', b'o\x04x\x1b'], '\tfoo\n    x\n'),
    ('0^D', '\tfoo\n', [b':set ai sw=4\r', b'o0\x04x\x1b'], '\tfoo\nx\n'),
    ('^^D', '\tfoo\n', [b':set ai sw=4\r', b'o^\x04x\ry\x1b'], '\tfoo\nx\n\ty\n'),
    ('0^D, next line', '\tfoo\n', [b':set ai sw=4\r', b'o0\x04x\ry\x1b'], '\tfoo\nx\ny\n'),
    ('^T to the next shiftwidth', '\tfoo\n', [b':set ai sw=4\r', b'o\x14x\x1b'], '\tfoo\n\t    x\n'),
    ('^T twice', 'foo\n', [b':set ai sw=4\r', b'o\x14\x14x\x1b'], 'foo\n\tx\n'),
    ('^T ^T ^D', 'foo\n', [b':set ai sw=4\r', b'o\x14\x14\x04x\x1b'], 'foo\n    x\n'),
    ('^D past 0', '\tfoo\n', [b':set ai sw=4\r', b'o\x04\x04\x04x\x1b'], '\tfoo\nx\n'),
    ('^D on spaces', '        foo\n', [b':set ai sw=4\r', b'o\x04x\x1b'], '        foo\n    x\n'),
    ('^D, sw=8', '\tfoo\n', [b':set ai sw=8\r', b'o\x04x\x1b'], '\tfoo\nx\n'),
    ('^T after text', '\tfoo\n', [b':set ai sw=4\r', b'oab\x14\x1b'], '\tfoo\n\t    ab\n'),
    ('^T at column 0', 'foo\n', [b':set ai sw=4\r', b'i\x14\x1b'], '    foo\n'),
    ('^T rounds', '  foo\n', [b':set ai sw=4\r', b'A\x14\x1b'], '    foo\n'),
    ('^D, noautoindent', '\tfoo\n', [b':set noai sw=4\r', b'o\x04x\x1b'], '\tfoo\nx\n'),
    ('0 is text', '\tfoo\n', [b':set ai sw=4\r', b'o0x\x1b'], '\tfoo\n\t0x\n'),
    ('>> by shiftwidth', 'foo\n', [b':set sw=4\r', b'>>'], '    foo\n'),
    ('>> twice', 'foo\n', [b':set sw=4\r', b'>>>>'], '\tfoo\n'),
    ('<< by shiftwidth', '\tfoo\n', [b':set sw=4\r', b'<<'], '    foo\n'),
    ('>> adds shiftwidth', '  foo\n', [b':set sw=4\r', b'>>'], '      foo\n'),
    ('>j', 'foo\nbar\n', [b':set sw=2\r', b'>j'], '  foo\n  bar\n'),
    ('>> default', 'foo\n', [b'>>'], '\tfoo\n'),
    ('^^D, two lines on', '\tfoo\n', [b':set ai sw=4\r', b'o^\x04x\ry\rz\x1b'], '\tfoo\nx\n\ty\n\tz\n'),
    ('^^D, ESC forgets', '\tfoo\n', [b':set ai sw=4\r', b'o^\x04x\x1bo\x1b'], '\tfoo\nx\n\n'),
    # POSIX: after text other than the autoindent, ^D changes nothing
    ("^D after text", "\tfoo\n", [b":set ai sw=4\r", b"oab\x04\x1b"], "\tfoo\n\tab\n"),
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
    print("PASS: test_vi_indent_keys")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
