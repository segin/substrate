#!/usr/bin/env python3
"""POSIX options vi shows (EXVI-53), each accepted and without effect:
paragraphs and sections (nroff macros as { } [[ ]] boundaries),
wrapmargin, showmatch, slowopen and mesg.  Expected results are Vim's
where Vim has them (vim -u NONE -N).

Usage: test_vi_options.py path/to/vi
"""
import os
import stat
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])
N = "a\nb\n.IP\nc\nd\n.SH X\ne\n"

CASES = [
    ("} to a paragraph macro", N, [b"}rZ"], {}, "a\nb\nZIP\nc\nd\n.SH X\ne\n"),
    ("2} to a section macro", N, [b"2}rZ"], {}, "a\nb\n.IP\nc\nd\nZSH X\ne\n"),
    ("{ to a section macro", N, [b"G{rZ"], {}, "a\nb\n.IP\nc\nd\nZSH X\ne\n"),
    ("]] to a section macro", N, [b"]]rZ"], {}, "a\nb\n.IP\nc\nd\nZSH X\ne\n"),
    ("[[ to a section macro", N, [b"G[[rZ"], {}, "a\nb\n.IP\nc\nd\nZSH X\ne\n"),
    ("paragraphs emptied", N, [b":set para=\r", b"}rZ"], {}, "a\nb\n.IP\nc\nd\nZSH X\ne\n"),
    ("wrapmargin", "x\n", [b":set wm=5\r", b"Aone two three four five six\x1b"],
     {"cols": 20}, "xone two three\nfour five six\n"),
    ("wrapmargin, word moved", "x\n", [b":set wm=5\r", b"Aaaa bbb ccc ddd eee fff ggg\x1b"],
     {"cols": 20}, "xaaa bbb ccc\nddd eee fff ggg\n"),
]


def main():
    fails = 0
    for name, text, keys, kw, want in CASES:
        status, _, saved = run_vi_session(VI, text, keys, **kw)
        if status != 0 or saved != want:
            print(f"FAIL: {name}: status {status}, {saved!r}, want {want!r}")
            fails += 1

    # showmatch: after ), the cursor goes back to the ( (row 1, column 3)
    for sm in (True, False):
        keys = ([b":set sm\r"] if sm else []) + [b"Aq(a)", b"\x1b"]
        _, decoded, saved = run_vi_session(VI, "x\n", keys)
        after = decoded[decoded.rfind("q(a)"):]
        if saved != "xq(a)\n" or ("\x1b[1;3H" in after) != sm:
            print(f"FAIL: showmatch={sm}: {saved!r} {after[:80]!r}")
            fails += 1

    # slowopen: in text input the status line is not redrawn
    for slow in (True, False):
        keys = [b":set smd slowopen\r" if slow else b":set smd\r", b"ixyz", b"\x1b"]
        _, decoded, _ = run_vi_session(VI, "x\n", keys)
        if ("-- INSERT --" in decoded) == slow:
            print(f"FAIL: slowopen={slow}: banner {'shown' if slow else 'missing'}")
            fails += 1

    # mesg: with nomesg, others cannot write to the terminal while vi runs
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "mode")
        os.environ["EXINIT"] = "set nomesg"
        try:
            # \% : ! takes a bare % for the file's name
            run_vi_session(VI, "x\n", [f":!stat -c \\%a $(tty) > {out}\r".encode(), b"\r"])
        finally:
            del os.environ["EXINIT"]
        try:
            with open(out) as f:
                mode = int(f.read().strip(), 8)
        except (OSError, ValueError):
            mode = None
        if mode is None or mode & (stat.S_IWGRP | stat.S_IWOTH):
            print(f"FAIL: nomesg: terminal mode {mode!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_options")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
