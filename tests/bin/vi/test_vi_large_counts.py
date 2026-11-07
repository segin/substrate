#!/usr/bin/env python3
"""Large counts: one change, linear time, bounded scrolls, and ^C stops them.

yy8000p took one copy of the buffer per put, ran for 40 s and was 8000
changes for u; 999999999^E scrolled the whole count, for seconds; and with
ISIG off ^C reached vi only as a key after the command, so nothing could
stop a long one (999999999. after ix<ESC> ran until killed).  Each session
here must finish well within its timeout.

Usage: test_vi_large_counts.py path/to/vi
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_vi_pty import run_vi_session  # noqa: E402

VI = os.path.abspath(sys.argv[1])

# name, text, keys, final keys, check(saved)
CASES = [
    ("yy8000p", "x\n", [b"yy8000p"], b":wq\r", lambda s: s == "x\n" * 8001),
    ("u after yy8000p is one change", "x\n", [b"yy8000p", b"u"], b":wq\r",
     lambda s: s == "x\n"),
    ("x100000P", "ab\n", [b"x100000P"], b":wq\r",
     lambda s: s == "a" * 100000 + "b\n"),
    ("999999999^E", "a\nb\nc\n", [b"999999999\x05"], b":wq\r",
     lambda s: s == "a\nb\nc\n"),
    ("999999999^Y", "a\nb\nc\n", [b"G999999999\x19"], b":wq\r",
     lambda s: s == "a\nb\nc\n"),
    ("^C stops a long .", "a\n", [b"ix\x1b", b"999999999.", b"\x03"], b":q!\r",
     lambda s: s == "a\n"),
]


def main():
    fails = 0
    for name, text, keys, final, ok in CASES:
        try:
            status, _, saved = run_vi_session(VI, text, keys, final_keys=final,
                                              session_timeout=20)
        except RuntimeError:
            print(f"FAIL: {name}: still running after 20 s")
            fails += 1
            continue
        if status != 0 or not ok(saved):
            print(f"FAIL: {name}: status {status}, {len(saved)} bytes: {saved[:40]!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_vi_large_counts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
