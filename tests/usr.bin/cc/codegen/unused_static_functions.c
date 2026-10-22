/*
 * Static functions nothing refers to are not compiled, as gcc does: a
 * static inline one at every level, a plain static one from -O1.  Linux's
 * headers define dozens of static inline helpers per file, some only for
 * the other architecture -- rip_rel_ptr's `leaq ...(%rip)` in an i386
 * build -- and cc emitted every one, so the assembler refused the i386
 * kernel's (394 "rip is a register of 64-bit code").  Kept: whatever is
 * called, taken the address of in code or in a global's initializer,
 * marked used, or reached from those.
 */
#include <stdio.h>

static inline void *rip_rel_ptr(void *p) {
    /* 64-bit only, never called: an i386 build must not assemble it */
    __asm__("leaq %c1(%%rip), %0" : "=r"(p) : "i"(p));
    return p;
}

static int helper(int x) { return x * 2; }
static int via_ptr(int x) { return x + 7; }
static int (*fp)(int) = via_ptr;
static __attribute__((used)) int kept_used(void) { return 3; }
static inline int chain2(int x) { return x + 1; }
static inline int chain1(int x) { return chain2(x) * 3; }

int main(void) {
    printf("%d %d %d\n", helper(4), fp(1), chain1(2));
    return 0;
}
