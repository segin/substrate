/*
 * A flag output ("=@ccz") is the condition the asm leaves in the flags,
 * set in the output by setcc after it.  cc gave such an output a
 * register and wrote it back without ever setting it: Linux 7.3's
 * try_cmpxchg(), whose only way out of its asm is "=@ccz", failed at
 * random, and mutex_lock() went to sleep in early boot.
 */
#include <stdio.h>

static int try_cas(int *p, int *old, int new) {
    _Bool ok;

    __asm__ volatile("lock cmpxchgl %[new], %[ptr]"
                     : "=@ccz"(ok), [ptr] "+m"(*p), [old] "+a"(*old)
                     : [new] "q"(new)
                     : "memory");
    return ok;
}

static int carry_of(unsigned a, unsigned b) {
    int carry;

    __asm__("addl %2, %1" : "=@ccc"(carry), "+r"(a) : "r"(b));
    return carry;
}

static int not_zero(unsigned v) {
    _Bool nz;

    __asm__("testl %1, %1" : "=@ccnz"(nz) : "r"(v));
    return nz;
}

int main(void) {
    int word = 5;
    int expect = 5;
    int r1 = try_cas(&word, &expect, 9);
    int r2;

    printf("cas %d word %d expect %d\n", r1, word, expect);
    expect = 5;
    r2 = try_cas(&word, &expect, 11);
    printf("cas %d word %d expect %d\n", r2, word, expect);
    printf("carry %d %d\n", carry_of(0xffffffffu, 1), carry_of(1, 2));
    printf("nz %d %d\n", not_zero(0), not_zero(7));
    return 0;
}
