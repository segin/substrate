#!/usr/bin/env python3
"""Regression tests for bugs fixed during bring-up."""
from grep_testlib import main, make_files


def body(g, s):
    # REGRESSION: the regex engine's per-thread visited marker
    # (nfa_state.last_list_id) was not reset between calls, so a compiled
    # pattern matched the first input line and then never again.  A pattern
    # that occurs on several lines must select all of them.
    s.check("repeated literal match across lines",
            g.run(["aaa"], stdin="aaa\nbbb\naaa\nccc\naaa\n")[1],
            "aaa\naaa\naaa\n")
    s.check("repeated class match across lines",
            g.run(["[0-9]"], stdin="x1\ny\nz2\nw\nv3\n")[1],
            "x1\nz2\nv3\n")
    s.check("alternation across many lines",
            g.run(["-E", "a|b"], stdin="a\nc\nb\nd\na\n")[1],
            "a\nb\na\n")

    # REGRESSION: -o went into an infinite loop after the first match because
    # the engine's no-match return was width-mismatched on the test host and
    # read as a (bogus) match, leaving the scan position unchanged.
    s.check("-o terminates with multiple matches per line",
            g.run(["-oE", "[0-9]+"], stdin="x12y34z56\n")[1],
            "12\n34\n56\n")
    s.check("-o no match terminates",
            g.run(["-oE", "[0-9]+"], stdin="abc\n")[1], "")

    # -o must not loop forever on a pattern that can match empty
    rc, out, _ = g.run(["-oE", "x*"], stdin="axbxc\n")
    s.check_rc("-o with possibly-empty pattern terminates", rc, 0)
    s.check("-o emits only non-empty matches", out, "x\nx\n")

    # Lines longer than the initial buffer must grow, not truncate.
    long_line = "z" * 5000 + "needle" + "z" * 5000
    d, p = make_files(big=long_line + "\n")
    s.check("long line handled",
            g.run(["-o", "needle", p["big"]])[1], "needle\n")

    # Count with no matches is 0, not absent.
    s.check("zero count", g.run(["-c", "zzz"], stdin="a\nb\n")[1], "0\n")


if __name__ == "__main__":
    main("regression", body)
