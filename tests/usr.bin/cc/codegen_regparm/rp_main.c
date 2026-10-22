/*
 * The caller half of the regparm ABI test (run_codegen_regparm.sh).
 * printf is variadic, so it is called on the stack whatever -mregparm
 * says, and the host's C library takes it as usual.
 */
#include <stdio.h>

#include "rp.h"

static int sub3(int a, int b, int c) { return a - b + c; }

int main(void) {
    struct s2 s2 = { 7 };
    struct s4 s4 = { 9 };
    struct s6 s6 = { 1, 2, 3 };
    struct s8 s8 = { 4, 5 };
    struct s12 s12 = { 6, 7, 8 }, r;
    struct s16 s16 = { 1, 2, 3, 4 };
    struct sd sd = { 0.5 };

    printf("%d %d %lld %lld %d\n", rp_ints(1, 2, 3), rp_four(1, 2, 3, 4), rp_ll_first(5000000000LL, 7, 2),
           rp_ll_late(1, 2, 3000000000LL, 4), rp_narrow(-3, -300, 200, 9));
    printf("%d %d %d %d %d %d\n", rp_s2(s2, 3), rp_s4(s4, 1, 2), rp_s6(s6, 4), rp_s8(1, s8, 2), rp_s12(s12, 1, 2),
           rp_s16(5, s16, 6));
    printf("%d %d %d %d %d\n", rp_fp(1.5f, 2, 2.0, 3, sd, 4), rp_var(4, 1, 2, 3, 4), rp_attr0(10, 3, 2),
           rp_attr2(6, 7, 8), rp_call_ptr(sub3, 10));
    r = rp_ret(11, 12);
    printf("%d %d %d\n", r.x, r.y, r.z);
    {
        /* each beside a sentinel: a store too wide for its structure would reach it */
        struct { struct r1 v; char after; } a1 = { { 0 }, 'z' };
        struct { struct s2 v; short after; } a2 = { { 0 }, 77 };
        struct r3 v3 = rp_r3(30);
        struct s4 v4 = rp_r4(4);
        struct r5 v5 = rp_r5(50);
        struct s8 v8 = rp_r8(80, 81);
        struct sf vf = rp_rf(3);
        struct sd vd = rp_rd(4);
        struct sff vff = rp_rff(5);

        a1.v = rp_r1(10);
        a2.v = rp_r2(20);
        printf("%d %c %d %d | %d %d %d | %d | %d %d | %d %d\n", a1.v.a, a1.after, a2.v.a, a2.after, v3.a[0], v3.a[1],
               v3.a[2], v4.x, v5.a[0], v5.a[4], v8.x, v8.y);
        printf("%.2f %.2f %.2f %.2f\n", vf.f, vd.d, vff.a, vff.b);
    }
    return 0;
}
