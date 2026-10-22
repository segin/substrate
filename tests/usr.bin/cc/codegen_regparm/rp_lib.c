/*
 * The callee half of the regparm ABI test (run_codegen_regparm.sh): every
 * way an argument travels under gcc's -mregparm=3, and the attributes
 * that change it.  Built by gcc against a cc-built rp_main.c and the
 * other way round; the output must be the all-gcc build's.
 */
#include "rp.h"

/* in %eax, %edx, %ecx */
int rp_ints(int a, int b, int c) { return a * 100 + b * 10 + c; }
/* the fourth on the stack */
int rp_four(int a, int b, int c, int d) { return a + 2 * b + 3 * c + 4 * d; }
/* a long long in %eax:%edx, then %ecx */
long long rp_ll_first(long long x, int y, int z) { return x * 3 + y - z; }
/* a long long that does not fit the one register left: it, and all after it, on the stack */
long long rp_ll_late(int a, int b, long long x, int d) { return a + b + x + d; }
/* chars and shorts in registers, each its own */
int rp_narrow(char c, short s, unsigned char u, int i) { return c + s + u + i; }
/* structures of 2, 4, 6, 8 and 12 bytes in one to three registers */
int rp_s2(struct s2 s, int d) { return s.a * 10 + d; }
int rp_s4(struct s4 s, int d, int e) { return s.x + d * 2 + e * 3; }
int rp_s6(struct s6 s, int d) { return s.a + s.b + s.c + d; }
int rp_s8(int a, struct s8 s, int d) { return a + s.x * 2 + s.y * 3 + d * 4; }
int rp_s12(struct s12 s, int d, int e) { return s.x + s.y + s.z + d * 10 + e * 100; }
/* 16 bytes: the stack, and every argument after it */
int rp_s16(int a, struct s16 s, int d) { return a + s.a + s.b + s.c + s.d + d * 1000; }
/* a float, a double and a structure of one double take no register */
int rp_fp(float f, int a, double d, int b, struct sd s, int c) { return (int)(f + d + s.d) + a + b * 10 + c * 100; }
/* variadic: the stack, whatever -mregparm says */
int rp_var(int n, ...) {
    __builtin_va_list ap;
    int sum = 0;

    __builtin_va_start(ap, n);
    while (n-- > 0) {
        sum += __builtin_va_arg(ap, int);
    }
    __builtin_va_end(ap);
    return sum;
}
/* regparm(0), as Linux's asmlinkage, and regparm(2), under -mregparm=3 */
int __attribute__((regparm(0))) rp_attr0(int a, int b, int c) { return a - b - c; }
int __attribute__((regparm(2))) rp_attr2(int a, int b, int c) { return a * b - c; }
/* a pointer to a function, called with -mregparm's registers */
int rp_call_ptr(int (*fn)(int, int, int), int a) { return fn(a, a + 1, a + 2); }
/* a structure returned through the hidden pointer, the first register */
struct s12 rp_ret(int a, int b) {
    struct s12 r = { a, b, a + b };
    return r;
}

/* each size and shape -freg-struct-return treats its own way */
struct r1 rp_r1(int a) { struct r1 r = { (char)a }; return r; }
struct s2 rp_r2(int a) { struct s2 r = { (short)(a * 2) }; return r; }
struct r3 rp_r3(int a) { struct r3 r = { { (char)a, (char)(a + 1), (char)(a + 2) } }; return r; }
struct s4 rp_r4(int a) { struct s4 r = { a * 4 }; return r; }
struct r5 rp_r5(int a) { struct r5 r = { { (char)a, 0, 0, 0, (char)(a + 4) } }; return r; }
struct s8 rp_r8(int a, int b) { struct s8 r = { a, b }; return r; }
struct sf rp_rf(int a) { struct sf r = { a + 0.5f }; return r; }
struct sd rp_rd(int a) { struct sd r = { a + 0.25 }; return r; }
struct sff rp_rff(int a) { struct sff r = { a + 0.5f, a + 1.5f }; return r; }
