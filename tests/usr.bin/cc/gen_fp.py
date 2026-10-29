#!/usr/bin/env python3
"""gen_fp.py SEED: print a random C program of floating arithmetic.

Each function reads its inputs from a volatile array, so nothing is folded,
and keeps many long double locals live at once -- more than the x87
stack's eight registers -- each read several times, through loops,
compares, conversions to int and stores to float, double and long double
arrays read back, then prints every local with %La.  The locals and the
constants are long doubles, so that no operation has excess precision to
keep or drop: x87 code computes exactly what C says, and gcc's -O0 and
-O2 and cc must agree bit for bit (with doubles and floats they need not:
gcc keeps excess precision or not as its registers fall).  A store to an
array rounds as C says in every compiler.  run_x87_stack.sh builds them.
"""
import random
import sys

TYPES = ["long double"]


def gen_function(r, idx):
    nvars = r.randint(6, 14)
    vars_ = [("v%d" % i, r.choice(TYPES)) for i in range(nvars)]
    lines = []
    lines.append("static void __attribute__((noinline)) f%d(void) {" % idx)
    for i, (n, t) in enumerate(vars_):
        lines.append("    %s %s = (%s)in[%d];" % (t, n, t, (idx * 7 + i) % 16))
    lines.append("    float fa[4];")
    lines.append("    double da[4];")
    lines.append("    long double la[4];")
    lines.append("    int k, k0, k1, k2, n = 0;")

    def atom(depth):
        c = r.random()
        if c < 0.6 or depth > 4:
            n, t = r.choice(vars_)
            return n
        if c < 0.7:
            return r.choice(["0.5", "3.0", "1e-3", "-2.25", "7.0"]) + "L"
        if c < 0.8:
            return "(%s)(%s)" % (r.choice(TYPES), expr(depth + 1))
        return "(%s)" % expr(depth + 1)

    def expr(depth=0):
        if depth > 3 or r.random() < 0.3:
            return atom(depth)
        op = r.choice(["+", "-", "*", "/", "+", "*"])
        return "%s %s %s" % (atom(depth), op, atom(depth))

    def stmt(depth):
        c = r.random()
        n, t = r.choice(vars_)
        if c < 0.45:
            return ["%s = %s;" % (n, expr())]
        if c < 0.55:
            return ["%s %s= %s;" % (n, r.choice("+-*"), expr())]
        if c < 0.65 and depth < 2:
            a, b = r.choice(vars_)[0], r.choice(vars_)[0]
            body = [s for _ in range(r.randint(1, 3)) for s in stmt(depth + 1)]
            els = [s for _ in range(r.randint(0, 2)) for s in stmt(depth + 1)]
            out = ["if (%s %s %s) {" % (a, r.choice(["<", "<=", ">", ">=", "==", "!="]), b)]
            out += ["    " + s for s in body]
            if els:
                out.append("} else {")
                out += ["    " + s for s in els]
            out.append("}")
            return out
        if c < 0.75 and depth < 2:
            body = [s for _ in range(r.randint(2, 4)) for s in stmt(depth + 1)]
            out = ["for (k%d = 0; k%d < %d; ++k%d) {" % (depth, depth, r.randint(2, 5), depth)]
            out += ["    " + s for s in body]
            out.append("}")
            return out
        if c < 0.85:
            arr = r.choice(["fa", "da", "la"])
            i = r.randint(0, 3)
            return ["%s[%d] = %s;" % (arr, i, expr()), "%s = %s[%d] * %s;" % (n, arr, i, r.choice(vars_)[0])]
        if c < 0.88:
            # temporaries of one block, more live at once than the stack has registers, each read twice
            nt = r.randint(9, 13)
            out = ["{"]
            for i in range(nt):
                out.append("    double t%d = %s;" % (i, expr(2)))
            terms = ["t%d * t%d" % (i, nt - 1 - i) for i in range(nt)]
            out.append("    %s = %s;" % (n, " + ".join(terms)))
            out.append("    %s = %s;" % (r.choice(vars_)[0], " - ".join("t%d" % i for i in range(nt))))
            out.append("}")
            return out
        if c < 0.92:
            return ["n += (%s > -1e6 && %s < 1e6) ? (int)%s : 1;" % (n, n, n)]
        return ["%s = n + %s;" % (n, expr())]

    for a in ("fa", "da", "la"):
        lines.append("    for (k = 0; k < 4; ++k) %s[k] = in[k];" % a)
    for _ in range(r.randint(8, 18)):
        lines += ["    " + s for s in stmt(0)]
    for n, t in vars_:
        if t == "long double":
            lines.append('    printf("%%La\\n", %s);' % n)
        else:
            lines.append('    printf("%%a\\n", (double)%s);' % n)
    lines.append('    printf("%a %a %La %d\\n", (double)fa[1], da[2], la[3], n);')
    lines.append("}")
    return lines


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    r = random.Random(seed)
    out = ["#include <stdio.h>", ""]
    vals = [r.choice(["1.5", "-0.3", "1e10", "3.0e-7", "0.1", "-7.75", "2.5e-30", "123456.789"]) for _ in range(16)]
    out.append("static volatile long double in[16] = {%s};" % ", ".join(v + "L" for v in vals))
    nf = r.randint(3, 6)
    for i in range(nf):
        out.append("")
        out += gen_function(r, i)
    out.append("")
    out.append("int main(void) {")
    for i in range(nf):
        out.append("    f%d();" % i)
    out.append("    return 0;")
    out.append("}")
    print("\n".join(out))


main()
