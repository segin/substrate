/*
 * An input matched to an output ("0"(id)) is what the output's register
 * holds as the asm begins, even when the output is another object.  cc
 * loaded the register from the output's own slot, written only after the
 * asm, so Linux's cpuid_count() -- "=a"(*a) ... : "0"(id), "2"(count) --
 * ran cpuid with leftovers for the leaf, and the boot code took the CPU
 * for an 8086.
 */
#include <stdio.h>

static void bump(unsigned id, unsigned *a, unsigned *b) {
    __asm__("addl $1, %0\n\tmovl %0, %1" : "=r"(*a), "=r"(*b) : "0"(id));
}

static void fixed(unsigned id, unsigned count, unsigned *a, unsigned *c) {
    __asm__("addl %%ecx, %%eax\n\tshll $1, %%ecx" : "=a"(*a), "=c"(*c) : "0"(id), "1"(count));
}

static unsigned same(unsigned x) {
    __asm__("addl $7, %0" : "=r"(x) : "0"(x));
    return x;
}

int main(void) {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;

    bump(41, &a, &b);
    printf("bump %u %u\n", a, b);
    fixed(100, 5, &a, &c);
    printf("fixed %u %u\n", a, c);
    printf("same %u\n", same(35));
    return 0;
}
