#!/usr/bin/env python3
"""POSIX character-class translation (REQ-GREP-070..074)."""
from grep_testlib import main


def body(g, s):
    txt = "abc\nABC\n123\n a b\n!@#\nx9z\n"

    cases = [
        ("[[:digit:]]", "123\nx9z\n"),
        ("[[:alpha:]]", "abc\nABC\n a b\nx9z\n"),
        ("[[:upper:]]", "ABC\n"),
        ("[[:lower:]]", "abc\n a b\nx9z\n"),
        ("[[:space:]]", " a b\n"),
        ("[[:punct:]]", "!@#\n"),
        ("[[:alnum:]]", "abc\nABC\n123\n a b\nx9z\n"),
        # ' a b' contains hex digits a and b, so it matches [[:xdigit:]]
        ("[[:xdigit:]]", "abc\nABC\n123\n a b\nx9z\n"),
    ]
    for pat, exp in cases:
        s.check("class %s" % pat, g.run([pat], stdin=txt)[1], exp)

    # negated class
    s.check("negated [^[:digit:]]",
            g.run(["-x", "[^[:digit:]]*"], stdin="123\nabc\n")[1], "abc\n")

    # class combined with literal members in one bracket expr
    s.check("mixed [[:digit:]x]",
            g.run(["-oE", "[[:digit:]x]+"], stdin="x9y\n")[1], "x9\n")

    # ERE with class
    s.check("ERE class +",
            g.run(["-oE", "[[:alpha:]]+"], stdin="ab12cd\n")[1], "ab\ncd\n")

    # leading ] is literal, then class
    s.check("literal ] in bracket",
            g.run(["-o", "[]a]"], stdin="]a\n")[1], "]\na\n")

    # unknown class is an error (exit 2)
    s.check_rc("unknown class errors",
               g.run(["[[:bogus:]]"], stdin="x\n")[0], 2)


if __name__ == "__main__":
    main("class", body)
