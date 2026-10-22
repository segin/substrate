/*
 * Enumerators have block scope (C11 6.2.1p4).  Linux's arch/x86/lib/
 * cmdline.c declares `enum { st_wordstart = 0, st_wordcmp, st_wordskip }`
 * in one function and the same names plus st_bufcpy in the next.  cc kept
 * one table for the whole file: the second enum's names reused the first
 * one's entries and the value meant for each landed on the table's last
 * entry, so the second switch saw "duplicate/overlapping case value".
 * An enumerator and a variable share a name space: the inner one hides
 * the outer either way round.
 */
#include <stdio.h>

enum { k = 3, m = 4 };

static int f(int i) {
    enum { a = 0, b, c } s = a;

    switch (i) { case a: s = b; break; case b: s = c; break; case c: s = a; break; }
    return s;
}

static int g(int i) {
    enum { a = 0, b, c, d } s = a;

    switch (i) { case a: return 10; case b: return 11; case c: return 12; case d: return 13; }
    return (int)s + d;
}

static int var_hides_enum(void) { int k = 7; return k; }

static int enum_hides_var(void) {
    int x = 1;
    {
        enum { x = 5 };
        if (x != 5) {
            return -1;
        }
    }
    return x;
}

static int inner_var_hides_inner_enum(void) {
    enum { m = 40 };
    {
        int m = 9;
        return m;
    }
}

int main(void) {
    enum { a = 5 };

    printf("%d %d %d %d %d\n", f(0), f(2), g(1), g(3), a);
    printf("%d %d %d %d %d\n", var_hides_enum(), enum_hides_var(), inner_var_hides_inner_enum(), k, m);
    return 0;
}
