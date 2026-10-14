#!/usr/bin/env python3
"""gen_arith.py SEED: print a random C program of integer arithmetic,
conversions and comparisons with no undefined behaviour; the program
prints a checksum of its variables.  Signed overflow is avoided by
doing + - * << in unsigned types; shifts are masked to the width;
divisors are made non-zero and signed division avoids INT_MIN / -1.
The same SEED always gives the same program.  run_codegen_random.sh
compares cc's build of these programs with the host gcc's."""
import random, sys

TYPES = [
    ('signed char', 8, True), ('unsigned char', 8, False),
    ('short', 16, True), ('unsigned short', 16, False),
    ('int', 32, True), ('unsigned', 32, False),
    ('long long', 64, True), ('unsigned long long', 64, False),
]
UNS = {8: 'unsigned char', 16: 'unsigned short', 32: 'unsigned', 64: 'unsigned long long'}


def lit(r, t):
    name, bits, signed = t
    v = r.getrandbits(bits)
    if r.random() < 0.3:
        v = r.choice([0, 1, 2, 3, 7, (1 << bits) - 1, 1 << (bits - 1), (1 << (bits - 1)) - 1])
    if signed and v >= 1 << (bits - 1):
        v -= 1 << bits
    if bits == 64:
        return ('(%dLL)' % v) if signed else ('%dULL' % v)
    return '((%s)%d)' % (name, v) if bits < 32 else (('(%d)' % v) if signed else ('%dU' % v))


def expr(r, vs, depth):
    if depth <= 0 or r.random() < 0.25:
        if r.random() < 0.7:
            return r.choice(vs)[0]
        return lit(r, r.choice(TYPES))
    k = r.random()
    a = expr(r, vs, depth - 1)
    b = expr(r, vs, depth - 1)
    if k < 0.40:
        # arithmetic done in an unsigned type of a random width
        w = r.choice([32, 64])
        u = UNS[w]
        op = r.choice(['+', '-', '*', '&', '|', '^'])
        return '((%s)(%s) %s (%s)(%s))' % (u, a, op, u, b)
    if k < 0.52:
        w = r.choice([32, 64])
        u = UNS[w]
        op = r.choice(['<<', '>>'])
        return '((%s)(%s) %s ((%s) & %d))' % (u, a, op, b, w - 1)
    if k < 0.62:
        # signed right shift of a signed value (implementation-defined, arithmetic on both)
        st = r.choice(['int', 'long long'])
        w = 32 if st == 'int' else 64
        return '((%s)(%s) >> ((%s) & %d))' % (st, a, b, w - 1)
    if k < 0.72:
        u = r.choice(['unsigned', 'unsigned long long'])
        op = r.choice(['/', '%'])
        return '((%s)(%s) %s ((%s)(%s) | 1))' % (u, a, op, u, b)
    if k < 0.80:
        # signed division with a small positive divisor
        st = r.choice(['int', 'long long'])
        op = r.choice(['/', '%'])
        return '((%s)(%s) %s ((%s)((%s) & 15) + 1))' % (st, a, op, st, b)
    if k < 0.90:
        op = r.choice(['<', '<=', '>', '>=', '==', '!='])
        return '((%s) %s (%s))' % (a, op, b)
    if k < 0.95:
        t = r.choice(TYPES)[0]
        return '((%s)(%s))' % (t, a)
    return '((%s) ? (%s) : (%s))' % (a, b, expr(r, vs, depth - 1))


def main():
    seed = int(sys.argv[1])
    r = random.Random(seed)
    vs = []
    out = ['#include <stdio.h>']
    for i in range(r.randint(4, 9)):
        t = r.choice(TYPES)
        vs.append(('v%d' % i, t))
        out.append('%s %s%s = %s;' % ('static' if r.random() < 0.5 else '', t[0], ' v%d' % i, lit(r, t)))
    out.append('int main(void) {')
    for i in range(r.randint(1, 3)):
        t = r.choice(TYPES)
        out.append('    %s l%d = %s;' % (t[0], i, lit(r, t)))
        vs.append(('l%d' % i, t))
    for _ in range(r.randint(6, 16)):
        dst = r.choice(vs)
        out.append('    %s = (%s)(%s);' % (dst[0], dst[1][0], expr(r, vs, r.randint(1, 4))))
    out.append('    unsigned long long h = 1469598103934665603ULL;')
    for v in vs:
        out.append('    h = (h ^ (unsigned long long)(long long)%s) * 1099511628211ULL;' % v[0])
    out.append('    printf("%016llx\\n", h);')
    out.append('    return 0;')
    out.append('}')
    print('\n'.join(out))


main()
