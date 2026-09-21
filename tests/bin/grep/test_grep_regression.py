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

    # A regex-engine resource failure must not read as "no matches".  A
    # pattern whose DFA blows past the state cap, over a high-entropy line:
    # grep used to print 0 and exit 1, indistinguishable from a clean miss.
    import random
    rnd = random.Random(7)
    longline = "".join(rnd.choice("ab") for _ in range(20000)) + "abbbbbbbbbbbbbbc"
    d, p2 = make_files(poison=longline + "\n" + ("ab" * 8 + "abbbbbbbbbbbbbbc\n") * 5)
    rc, out, err = g.run(["-cE", "^(a|b)*a(a|b){14}c", p2["poison"]])
    # Either it copes (rc 0/1 with a real count) or it REPORTS the failure
    # with exit 2 -- what it must never do is exit 1 claiming zero matches
    # while having given up.
    s.check_rc("engine failure is not a silent no-match",
               0 if (rc == 2 or rc == 0) else 1, 0)

    # The DFA cache lives on the compiled pattern, so a subject that fills it
    # used to poison every LATER subject: the same file gave a different count
    # depending on whether the pathological line came first or last.
    longfirst = longline + "\n" + ("ab" * 8 + "abbbbbbbbbbbbbbc\n") * 5
    longlast = ("ab" * 8 + "abbbbbbbbbbbbbbc\n") * 5 + longline + "\n"
    d3, p3 = make_files(first=longfirst, last=longlast)
    _, out_first, _ = g.run(["-cE", "^(a|b)*a(a|b){14}c", p3["first"]])
    _, out_last, _ = g.run(["-cE", "^(a|b)*a(a|b){14}c", p3["last"]])
    s.check("match count does not depend on line order",
            out_first.strip(), out_last.strip())

    # Count with no matches is 0, not absent.
    s.check("zero count", g.run(["-c", "zzz"], stdin="a\nb\n")[1], "0\n")


if __name__ == "__main__":
    main("regression", body)
