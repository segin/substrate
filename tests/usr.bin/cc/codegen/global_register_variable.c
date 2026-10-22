/*
 * A global register variable -- `register T x asm("reg")` at file scope --
 * has no storage: reading it reads the register.  Linux has
 * `register unsigned long current_stack_pointer asm("esp")` in a header;
 * cc made it a .bss object, so it read zero, and every file including the
 * header defined the symbol (the vDSO's link failed: "duplicate strong
 * definition of `current_stack_pointer`").  The register name may be
 * written in adjacent strings, as asm labels are.
 */
#include <stdio.h>

#ifdef __x86_64__
register unsigned long current_stack_pointer asm("rsp");
#else
register unsigned long current_stack_pointer asm("e" "sp");
#endif

static __attribute__((noinline)) unsigned long sp(void) {
    return current_stack_pointer;
}

int main(void) {
    char local;
    unsigned long a = sp();
    unsigned long here = (unsigned long)&local;

    printf("%d %d\n", a < here + 4096 && a + 4096 > here, (int)sizeof(current_stack_pointer));
    return 0;
}
