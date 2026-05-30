#!/usr/bin/env python3
"""POSIX.1-2024 mandated grep behavior (REQ-GREP-001..066, 120..127)."""
from grep_testlib import main, make_files

F = "alpha\nBeta\ngamma123\nDELTA\nthe end\n"
G = "foo bar\nbarfoo\nfoo\n"


def body(g, s):
    d, p = make_files(f=F, g=G, dot="a.b\naxb\n")
    f, gg, dot = p["f"], p["g"], p["dot"]

    # Basic selection and dialects
    s.check("BRE basic", g.run([ "gamma", f])[1], "gamma123\n")
    s.check("ERE alternation", g.run(["-E", "alpha|gamma", f])[1],
            "alpha\ngamma123\n")
    s.check("fixed treats . literally", g.run(["-F", "a.b", dot])[1], "a.b\n")
    s.check("BRE . is wildcard", g.run(["a.b", dot])[1], "a.b\naxb\n")
    s.check("empty pattern matches all",
            g.run(["-c", "", gg])[1], "3\n")

    # Program-name dialects
    s.check("egrep == -E", g.run(["alpha|gamma", f], prog="egrep")[1],
            "alpha\ngamma123\n")
    s.check("fgrep == -F", g.run(["a.b", dot], prog="fgrep")[1], "a.b\n")
    s.check("explicit -F overrides egrep name",
            g.run(["-F", "a.b", dot], prog="egrep")[1], "a.b\n")

    # -e / -f
    s.check("multiple -e", g.run(["-e", "alpha", "-e", "gamma", f])[1],
            "alpha\ngamma123\n")
    df, pf = make_files(pat="alpha\ngamma\n")
    s.check("-f file", g.run(["-f", pf["pat"], f])[1], "alpha\ngamma123\n")

    # -i -v
    s.check("-i", g.run(["-i", "beta", f])[1], "Beta\n")
    s.check("-v", g.run(["-v", "gamma", f])[1],
            "alpha\nBeta\nDELTA\nthe end\n")
    s.check("-vc", g.run(["-vc", "gamma", f])[1], "4\n")

    # -w -x
    s.check("-w whole word", g.run(["-w", "foo", gg])[1], "foo bar\nfoo\n")
    s.check("-w no submatch", g.run(["-w", "oob", gg])[1], "")
    s.check("-x whole line", g.run(["-x", "foo", gg])[1], "foo\n")
    s.check("-Fx whole line", g.run(["-Fx", "foo", gg])[1], "foo\n")

    # -c -l -n -o -q -s
    s.check("-c", g.run(["-c", "DELTA", f])[1], "1\n")
    s.check("-n", g.run(["-n", "gamma", f])[1], "3:gamma123\n")
    s.check("-l", g.run(["-l", "gamma", f])[1], f + "\n")
    s.check("-o", g.run(["-oE", "[0-9]+", f])[1], "123\n")
    s.check("-q stdout empty", g.run(["-q", "gamma", f])[1], "")

    # filename prefixing
    s.check("multi-file prefix",
            g.run(["gamma", f, gg])[1], f + ":gamma123\n")
    s.check("-h suppresses name",
            g.run(["-h", "gamma", f, gg])[1], "gamma123\n")
    s.check("-H forces name",
            g.run(["-H", "gamma", f])[1], f + ":gamma123\n")

    # stdin and -
    s.check("stdin", g.run(["gamma"], stdin="x\ngamma123\n")[1], "gamma123\n")
    s.check("dash is stdin",
            g.run(["gamma", "-"], stdin="gamma123\n")[1], "gamma123\n")
    s.check("--label", g.run(["--label=IN", "-H", "gamma"],
            stdin="gamma123\n")[1], "IN:gamma123\n")

    # -- end of options
    de, pe = make_files(dash="-n hit\nmiss\n")
    s.check("-- ends options",
            g.run(["-e", "-n", "--", pe["dash"]])[1], "-n hit\n")

    # anchors
    s.check("caret anchor", g.run(["^alpha", f])[1], "alpha\n")
    s.check("dollar anchor", g.run(["end$", f])[1], "the end\n")

    # BRE back-references (REQ-GREP, POSIX BRE)
    s.check("backref double char",
            g.run([r"\(.\)\1"], stdin="abc\nbook\nfoo\n")[1], "book\nfoo\n")
    s.check("backref word repeat",
            g.run([r"\(abc\)\1"], stdin="abcabc\nabcdef\n")[1], "abcabc\n")
    s.check("backref no match",
            g.run([r"\(a\)\1"], stdin="ab\nba\n")[1], "")
    s.check("backref nested groups",
            g.run([r"\(\(a\)\(b\)\)\3\2"], stdin="abba\nabab\n")[1], "abba\n")
    s.check("backref with -i",
            g.run(["-i", r"\(a\)\1"], stdin="aA\nxy\n")[1], "aA\n")
    s.check_rc("backref to absent group errors",
               g.run([r"\1x"], stdin="z\n")[0], 2)
    # ERE keeps \N literal (BSD: no ERE back-references)
    s.check("ERE backslash-1 is literal",
            g.run(["-E", r"(a)\1"], stdin="a1\naa\n")[1], "a1\n")

    # no final newline is still processed (REQ-127)
    dn, pn = make_files(nonl="last line no nl")
    s.check("no trailing newline",
            g.run(["last", pn["nonl"]])[1], "last line no nl\n")

    # exit status
    s.check_rc("exit 0 on match", g.run(["gamma", f])[0], 0)
    s.check_rc("exit 1 on no match", g.run(["zzz", f])[0], 1)
    s.check_rc("exit 2 on bad pattern", g.run(["["], stdin="x\n")[0], 2)
    s.check_rc("exit 2 on bad option", g.run(["--bogus", f])[0], 2)
    s.check_rc("quiet exit 0", g.run(["-q", "gamma", f])[0], 0)
    s.check_rc("quiet exit 1", g.run(["-q", "zzz", f])[0], 1)
    s.check_rc("missing file exit 2", g.run(["x", "/no/such/file"])[0], 2)
    s.check("-s suppresses fs errors",
            g.run(["-s", "x", "/no/such/file"])[2], "")


if __name__ == "__main__":
    main("posix", body)
