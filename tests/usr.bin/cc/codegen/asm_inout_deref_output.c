/*
 * A read-write output that is another object ("+r" (*p), "+a" (s->m))
 * comes into the asm with that object's value.  cc wrote it back after
 * the asm but never read it before: the asm saw whatever the register
 * held, and Linux's try_cmpxchg() -- "+a" (*old) -- compared against
 * junk.
 */
#include <stdio.h>

struct counter {
    int pad;
    int value;
};

static void bump(int *p) {
    __asm__("addl $1, %0" : "+r"(*p));
}

static void double_it(struct counter *c) {
    __asm__("addl %%eax, %%eax" : "+a"(c->value));
}

int main(void) {
    int n = 41;
    struct counter c = {7, 21};

    bump(&n);
    double_it(&c);
    printf("%d %d %d\n", n, c.pad, c.value);
    return 0;
}
