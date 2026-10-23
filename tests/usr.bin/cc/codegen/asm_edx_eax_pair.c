/*
 * On i386 "A" of a 64-bit operand is the pair %edx:%eax.  cc took it for
 * %eax alone, or for nothing: an input "A" (n) loaded neither word, and
 * an output "=A" (n) kept the low word, sign-extended.  Linux's do_div()
 * hands its dividend in and its quotient out so, and register_refined_
 * jiffies() divided by the junk that made: a divide error in early boot.
 * (Elsewhere the same values are made in C, for the same output.)
 */
#include <stdio.h>

static unsigned long long join(unsigned lo, unsigned hi) {
    unsigned long long n;

#if defined(__i386__)
    __asm__("" : "=A"(n) : "a"(lo), "d"(hi));
#else
    n = (unsigned long long)hi << 32 | lo;
#endif
    return n;
}

static unsigned split(unsigned long long n, unsigned *hi_out) {
    unsigned lo, hi;

#if defined(__i386__)
    __asm__("" : "=a"(lo), "=d"(hi) : "A"(n));
#else
    lo = (unsigned)n;
    hi = (unsigned)(n >> 32);
#endif
    *hi_out = hi;
    return lo;
}

/* do_div(): the quotient back in n, the remainder returned */
static unsigned div_u64(unsigned long long *n, unsigned base) {
    unsigned long long q = *n / base;
    unsigned rem = (unsigned)(*n % base);

#if defined(__i386__)
    unsigned lo, hi;

    __asm__("" : "=a"(lo), "=d"(hi) : "A"(q));
    __asm__("" : "=A"(*n) : "a"(lo), "d"(hi));
#else
    *n = q;
#endif
    return rem;
}

int main(void) {
    unsigned hi;
    unsigned lo = split(0x0000003b9aca0000ull, &hi);
    unsigned long long shift_hz = (unsigned long long)1193182 << 8;
    unsigned rem = div_u64(&shift_hz, 11932);

    printf("join %llx\n", join(0x9aca0000u, 0x3bu));
    printf("split %x %x\n", lo, hi);
    printf("div %llu %u\n", shift_hz, rem);
    return 0;
}
