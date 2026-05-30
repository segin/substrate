#!/usr/bin/env python3
"""GNU/BSD grep extensions (REQ-GREP-052..058, 061, 080..094, 100..114)."""
import os
from grep_testlib import main, make_files

F = "alpha\nBeta\ngamma123\nDELTA\nthe end\n"


def body(g, s):
    d, p = make_files(f=F, g="foo\nbar\nfoo\n")
    f = p["f"]

    # -L files-without-match
    s.check("-L", g.run(["-L", "gamma", f, p["g"]])[1], p["g"] + "\n")

    # -m max-count
    s.check("-m2", g.run(["-m2", "foo", p["g"]])[1], "foo\nfoo\n")
    s.check("-c capped by -m", g.run(["-c", "-m1", "foo", p["g"]])[1], "1\n")

    # -o with -c counts matches
    s.check("-o multiple per line",
            g.run(["-oE", "[0-9]"], stdin="a1b2c3\n")[1], "1\n2\n3\n")
    s.check("-co counts matches not lines",
            g.run(["-coE", "[0-9]"], stdin="a1b2\nc3\n")[1], "3\n")

    # -b byte offset
    s.check("-b line offset",
            g.run(["-bo", "bar"], stdin="xxbar\n")[1], "2:bar\n")

    # context
    s.check("-A1", g.run(["-A1", "gamma", f])[1], "gamma123\nDELTA\n")
    s.check("-B1", g.run(["-B1", "gamma", f])[1], "Beta\ngamma123\n")
    s.check("-C1", g.run(["-C1", "gamma", f])[1],
            "Beta\ngamma123\nDELTA\n")
    s.check("-NUM shorthand", g.run(["-1", "gamma", f])[1],
            "Beta\ngamma123\nDELTA\n")
    s.check("context separator",
            g.run(["-A1", "M"],
                  stdin="a\nM\nb\nc\nd\nM\ne\n")[1],
            "M\nb\n--\nM\ne\n")

    # recursion
    tree = os.path.join(d, "tree")
    os.makedirs(os.path.join(tree, "sub"))
    with open(os.path.join(tree, "a.txt"), "w") as fh:
        fh.write("hit one\nmiss\n")
    with open(os.path.join(tree, "sub", "b.log"), "w") as fh:
        fh.write("hit two\n")
    with open(os.path.join(tree, "sub", "c.txt"), "w") as fh:
        fh.write("hit three\n")

    out = sorted(g.run(["-rl", "hit", tree])[1].split())
    s.check("-rl finds all", out, sorted([
        os.path.join(tree, "a.txt"),
        os.path.join(tree, "sub", "b.log"),
        os.path.join(tree, "sub", "c.txt")]))

    out = sorted(g.run(["-rl", "--include=*.txt", "hit", tree])[1].split())
    s.check("--include filters", out, sorted([
        os.path.join(tree, "a.txt"),
        os.path.join(tree, "sub", "c.txt")]))

    out = sorted(g.run(["-rl", "--exclude=*.log", "hit", tree])[1].split())
    s.check("--exclude filters", out, sorted([
        os.path.join(tree, "a.txt"),
        os.path.join(tree, "sub", "c.txt")]))

    # directory without -r is skipped (BSD)
    rc, _, err = g.run(["hit", tree])
    s.check("dir skip warns", "Is a directory" in err, True)

    # binary handling
    db, pb = make_files()  # empty dir
    binpath = os.path.join(db, "bin.dat")
    with open(binpath, "wb") as fh:
        fh.write(b"abc\x00def hit\n")
    s.check("binary default reports",
            g.run(["hit", binpath])[1], "Binary file %s matches\n" % binpath)
    s.check("binary -a as text",
            g.run(["-a", "hit", binpath])[1], "abc\x00def hit\n")
    s.check_rc("binary -I no match", g.run(["-I", "hit", binpath])[0], 1)
    s.check("--binary-files=text",
            g.run(["--binary-files=text", "hit", binpath])[1],
            "abc\x00def hit\n")

    # -z null data
    s.check("-z null delim",
            g.run(["-z", "hit"], stdin="r1hit\x00r2miss\x00")[1],
            "r1hit\x00")

    # --color
    out = g.run(["--color=always", "foo"], stdin="xfooy\n")[1]
    s.check("--color wraps match",
            out, "x\033[01;31m\033[Kfoo\033[m\033[Ky\n")
    s.check("--color=never plain",
            g.run(["--color=never", "foo"], stdin="xfooy\n")[1], "xfooy\n")

    # --version / --help
    s.check_rc("--version exit 0", g.run(["--version"])[0], 0)
    s.check_rc("--help exit 0", g.run(["--help"])[0], 0)


if __name__ == "__main__":
    main("ext", body)
