/*
 * A floating object may be an asm operand whose constraint is memory
 * ("m"): the operand is its address, whatever its type.  Linux checks
 * for the Pentium FDIV bug by handing doubles to fldl and fdivl as "m";
 * cc refused every floating operand ("floating asm inputs are not
 * supported yet").  Here x87 code divides doubles in memory, as Linux's
 * does, and stores floats and doubles through "=m" outputs.
 */
#include <stdio.h>

static double x = 4195835.0;
static double y = 3145727.0;

int main(void) {
    int fdiv_bug;
    double q;
    float half;
    float two = 2.0f;

    __asm__("fninit\n\t"
            "fldl %1\n\t"
            "fdivl %2\n\t"
            "fmull %2\n\t"
            "fldl %1\n\t"
            "fsubp %%st,%%st(1)\n\t"
            "fistpl %0\n\t"
            "fwait\n\t"
            "fninit"
            : "=m"(*&fdiv_bug)
            : "m"(*&x), "m"(*&y));
    __asm__("fldl %1\n\t"
            "fdivl %2\n\t"
            "fstpl %0"
            : "=m"(q)
            : "m"(x), "m"(y));
    __asm__("fld1\n\t"
            "fdivs %1\n\t"
            "fstps %0"
            : "=m"(half)
            : "m"(two));
    printf("%d %.6f %.2f\n", fdiv_bug, q, (double)half);
    return 0;
}
