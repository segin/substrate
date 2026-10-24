/*
 * Small structures passed and returned by value, of each size that goes
 * in general registers on x86-64 -- 1, 2, 4, 8, 12 and 16 bytes, mixed
 * field sizes, and some past the six argument registers onto the stack:
 * the register allocator passes them itself now (run_x64_struct_regs.sh),
 * as struct timespec64 is all through Linux.
 */
#include <stdio.h>

struct s1 { char a; };
struct s2 { short a; };
struct s4 { int a; };
struct s8 { int a, b; };
struct s12 { int a, b, c; };
struct ts { long sec; long nsec; };
struct mix { char c; short s; int i; long l; };

static long __attribute__((noinline)) take(struct s1 a, struct s2 b, struct s4 c, struct s8 d, struct s12 e, struct ts f) {
    return a.a + 10L * b.a + 100L * c.a + 1000L * d.a + 3 * d.b + e.a * 7L + e.b * 11L + e.c * 13L + f.sec * 17 + f.nsec;
}
static long __attribute__((noinline)) over(long x1, long x2, long x3, long x4, long x5, struct ts t, struct s12 u, struct mix m) {
    return x1 + x2 * 2 + x3 * 3 + x4 * 4 + x5 * 5 + t.sec * 6 + t.nsec * 7 + u.a + u.b * 8 + u.c * 9 + m.c + m.s * 3 + m.i * 5 + m.l * 7;
}
static struct ts __attribute__((noinline)) tsadd(struct ts a, struct ts b) {
    struct ts r = {a.sec + b.sec, a.nsec + b.nsec};
    if (r.nsec >= 1000000000L) { r.nsec -= 1000000000L; r.sec++; }
    return r;
}

int main(void) {
    struct s1 a = {-3}; struct s2 b = {-300}; struct s4 c = {7000}; struct s8 d = {-5, 9};
    struct s12 e = {1, -2, 3}; struct ts f = {1234567L, -42}; struct mix m = {-1, 2000, -3000000, 1L << 20};
    struct ts g = tsadd(f, (struct ts){1, 999999999L});
    printf("%ld %ld %ld %ld\n", take(a, b, c, d, e, f), over(1, 2, 3, 4, 5, f, e, m), g.sec, g.nsec);
    return 0;
}
