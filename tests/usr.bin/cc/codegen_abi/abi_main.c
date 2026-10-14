/*
 * Calls across compilers: run_codegen_abi.sh builds this with cc and
 * abi_lib.c with gcc, then the other way round.  Struct returns,
 * struct and narrow arguments, 64-bit values and libc's div() must all
 * cross the boundary intact.
 */
#include <stdio.h>
#include <stdlib.h>

struct Q {
    int a, b;
};

struct R {
    int a, b, c, d, e;
};

struct Q lib_q(int a);
struct R lib_r(int a, int b);
int lib_sum(char c, short s, int i, long long ll, double d, struct Q q);
long long lib_wide(long long a, unsigned long long b);

int main(void) {
    struct Q q = lib_q(5);
    struct R r = lib_r(3, 4);
    div_t dv = div(17, 5);
    ldiv_t lv = ldiv(-100L, 7L);
    struct Q arg = {7, 8};

    printf("%d %d | %d %d %d %d %d\n", q.a, q.b, r.a, r.b, r.c, r.d, r.e);
    printf("%d %d %ld %ld\n", dv.quot, dv.rem, lv.quot, lv.rem);
    printf("%d %lld\n", lib_sum(1, 2, 3, 4000000005LL, 1.5, arg), lib_wide(-5000000000LL, 0xfedcba9876543210ULL));
    return 0;
}
