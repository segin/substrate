/*
 * Each arm of ?: is converted to the result's type.  When one arm is
 * unsigned long long and the other narrower, the narrow arm must be
 * widened; on i386 its high word was once left uninitialized.
 */
#include <stdio.h>

unsigned long long v0 = 7;
short v1 = -3;
signed char v4 = 0;
int c = 1;
int z = 0;

int main(void) {
    unsigned long long a = c ? (v1 ? v4 : v1) : v0;
    unsigned long long b = c ? (int)v1 : v0;
    unsigned long long d = c ? v1 : v0;
    long long e = z ? v0 : v1;
    unsigned long long f = 1 ? v1 : v0;

    printf("%llx %llx %llx %lld %llx\n", a, b, d, e, f);
    return 0;
}
