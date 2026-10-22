/*
 * Basic asm -- no `:`, no operands -- is its template as written: a `%`
 * in it is the assembler's (`%%` would be wrong), not an operand reference
 * for cc to check or render.  cc refused `asm("nop # 100% done")` with
 * "asm template has unsupported '% ' reference", and Linux's basic asm
 * names registers with a single `%`.
 */
#include <stdio.h>

static int x = 5;

int main(void) {
    __asm__("nop # 100% basic: %eax is the assembler's");
    __asm__ __volatile__("" ::: "memory"); /* extended, for contrast */
    printf("%d\n", x);
    return 0;
}
