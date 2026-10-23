/*
 * Frame slots: a chain of values each live across a block or two.  The
 * results are checked against gcc here; run_i386_frame_slots.sh checks
 * that the frames are small.
 */
#include <stdio.h>

__attribute__((noinline)) unsigned chain_blocks(unsigned x, int n) {
    unsigned t0 = x;
    unsigned t1 = t0 * 0x9e3779b1u + 1;
    if ((t1 & 3) == (unsigned)n)
        t1 ^= x;
    unsigned t2 = t1 * 0x9e3779b1u + 2;
    if ((t2 & 3) == (unsigned)n)
        t2 ^= x;
    unsigned t3 = t2 * 0x9e3779b1u + 3;
    if ((t3 & 3) == (unsigned)n)
        t3 ^= x;
    unsigned t4 = t3 * 0x9e3779b1u + 4;
    if ((t4 & 3) == (unsigned)n)
        t4 ^= x;
    unsigned t5 = t4 * 0x9e3779b1u + 5;
    if ((t5 & 3) == (unsigned)n)
        t5 ^= x;
    unsigned t6 = t5 * 0x9e3779b1u + 6;
    if ((t6 & 3) == (unsigned)n)
        t6 ^= x;
    unsigned t7 = t6 * 0x9e3779b1u + 7;
    if ((t7 & 3) == (unsigned)n)
        t7 ^= x;
    unsigned t8 = t7 * 0x9e3779b1u + 8;
    if ((t8 & 3) == (unsigned)n)
        t8 ^= x;
    unsigned t9 = t8 * 0x9e3779b1u + 9;
    if ((t9 & 3) == (unsigned)n)
        t9 ^= x;
    unsigned t10 = t9 * 0x9e3779b1u + 10;
    if ((t10 & 3) == (unsigned)n)
        t10 ^= x;
    unsigned t11 = t10 * 0x9e3779b1u + 11;
    if ((t11 & 3) == (unsigned)n)
        t11 ^= x;
    unsigned t12 = t11 * 0x9e3779b1u + 12;
    if ((t12 & 3) == (unsigned)n)
        t12 ^= x;
    unsigned t13 = t12 * 0x9e3779b1u + 13;
    if ((t13 & 3) == (unsigned)n)
        t13 ^= x;
    unsigned t14 = t13 * 0x9e3779b1u + 14;
    if ((t14 & 3) == (unsigned)n)
        t14 ^= x;
    unsigned t15 = t14 * 0x9e3779b1u + 15;
    if ((t15 & 3) == (unsigned)n)
        t15 ^= x;
    unsigned t16 = t15 * 0x9e3779b1u + 16;
    if ((t16 & 3) == (unsigned)n)
        t16 ^= x;
    unsigned t17 = t16 * 0x9e3779b1u + 17;
    if ((t17 & 3) == (unsigned)n)
        t17 ^= x;
    unsigned t18 = t17 * 0x9e3779b1u + 18;
    if ((t18 & 3) == (unsigned)n)
        t18 ^= x;
    unsigned t19 = t18 * 0x9e3779b1u + 19;
    if ((t19 & 3) == (unsigned)n)
        t19 ^= x;
    unsigned t20 = t19 * 0x9e3779b1u + 20;
    if ((t20 & 3) == (unsigned)n)
        t20 ^= x;
    unsigned t21 = t20 * 0x9e3779b1u + 21;
    if ((t21 & 3) == (unsigned)n)
        t21 ^= x;
    unsigned t22 = t21 * 0x9e3779b1u + 22;
    if ((t22 & 3) == (unsigned)n)
        t22 ^= x;
    unsigned t23 = t22 * 0x9e3779b1u + 23;
    if ((t23 & 3) == (unsigned)n)
        t23 ^= x;
    unsigned t24 = t23 * 0x9e3779b1u + 24;
    if ((t24 & 3) == (unsigned)n)
        t24 ^= x;
    unsigned t25 = t24 * 0x9e3779b1u + 25;
    if ((t25 & 3) == (unsigned)n)
        t25 ^= x;
    unsigned t26 = t25 * 0x9e3779b1u + 26;
    if ((t26 & 3) == (unsigned)n)
        t26 ^= x;
    unsigned t27 = t26 * 0x9e3779b1u + 27;
    if ((t27 & 3) == (unsigned)n)
        t27 ^= x;
    unsigned t28 = t27 * 0x9e3779b1u + 28;
    if ((t28 & 3) == (unsigned)n)
        t28 ^= x;
    unsigned t29 = t28 * 0x9e3779b1u + 29;
    if ((t29 & 3) == (unsigned)n)
        t29 ^= x;
    unsigned t30 = t29 * 0x9e3779b1u + 30;
    if ((t30 & 3) == (unsigned)n)
        t30 ^= x;
    unsigned t31 = t30 * 0x9e3779b1u + 31;
    if ((t31 & 3) == (unsigned)n)
        t31 ^= x;
    unsigned t32 = t31 * 0x9e3779b1u + 32;
    if ((t32 & 3) == (unsigned)n)
        t32 ^= x;
    unsigned t33 = t32 * 0x9e3779b1u + 33;
    if ((t33 & 3) == (unsigned)n)
        t33 ^= x;
    unsigned t34 = t33 * 0x9e3779b1u + 34;
    if ((t34 & 3) == (unsigned)n)
        t34 ^= x;
    unsigned t35 = t34 * 0x9e3779b1u + 35;
    if ((t35 & 3) == (unsigned)n)
        t35 ^= x;
    unsigned t36 = t35 * 0x9e3779b1u + 36;
    if ((t36 & 3) == (unsigned)n)
        t36 ^= x;
    unsigned t37 = t36 * 0x9e3779b1u + 37;
    if ((t37 & 3) == (unsigned)n)
        t37 ^= x;
    unsigned t38 = t37 * 0x9e3779b1u + 38;
    if ((t38 & 3) == (unsigned)n)
        t38 ^= x;
    unsigned t39 = t38 * 0x9e3779b1u + 39;
    if ((t39 & 3) == (unsigned)n)
        t39 ^= x;
    unsigned t40 = t39 * 0x9e3779b1u + 40;
    if ((t40 & 3) == (unsigned)n)
        t40 ^= x;
    unsigned t41 = t40 * 0x9e3779b1u + 41;
    if ((t41 & 3) == (unsigned)n)
        t41 ^= x;
    unsigned t42 = t41 * 0x9e3779b1u + 42;
    if ((t42 & 3) == (unsigned)n)
        t42 ^= x;
    unsigned t43 = t42 * 0x9e3779b1u + 43;
    if ((t43 & 3) == (unsigned)n)
        t43 ^= x;
    unsigned t44 = t43 * 0x9e3779b1u + 44;
    if ((t44 & 3) == (unsigned)n)
        t44 ^= x;
    unsigned t45 = t44 * 0x9e3779b1u + 45;
    if ((t45 & 3) == (unsigned)n)
        t45 ^= x;
    unsigned t46 = t45 * 0x9e3779b1u + 46;
    if ((t46 & 3) == (unsigned)n)
        t46 ^= x;
    unsigned t47 = t46 * 0x9e3779b1u + 47;
    if ((t47 & 3) == (unsigned)n)
        t47 ^= x;
    return t47;
}

int main(void) {
    int n;

    for (n = 0; n < 4; ++n) {
        printf("%u\n", chain_blocks(0x1234u + n, n));
    }
    return 0;
}
