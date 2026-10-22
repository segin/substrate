/*
 * The arms of `switch (sizeof(x))` and `if (0)` that cannot run are not
 * compiled, at -O0 as at -O2, as gcc does.  Linux writes size-generic
 * helpers so (xchg, cmpxchg, __get_user): each arm holds inline asm for
 * one operand size, and in an i386 build the 8-byte arm's `xaddq` and
 * %rax are refused by the assembler -- 2,619 "cmpxchgq does not take
 * these operands" and 2,140 "rax is a register of 64-bit code" before.
 * cc folded the compares but forgot at each label what it had folded,
 * and never removed the code that could no longer be reached.
 *
 * Basic asm (no `:`) is its text as written: `%rax` is the assembler's,
 * not an operand reference cc should check or render.
 */
#include <stdio.h>

#define xadd(p, v) ({                                                       \
    __typeof__(*(p)) __v = (v);                                             \
    switch (sizeof(*(p))) {                                                 \
    case 1: __asm__ volatile("xaddb %b0, %1" : "+q"(__v), "+m"(*(p))); break; \
    case 2: __asm__ volatile("xaddw %w0, %1" : "+r"(__v), "+m"(*(p))); break; \
    case 4: __asm__ volatile("xaddl %k0, %1" : "+r"(__v), "+m"(*(p))); break; \
    case 8: __asm__ volatile("xaddq %q0, %1" : "+r"(__v), "+m"(*(p))); break; \
    }                                                                       \
    __v;                                                                    \
})

static int counter = 40;
static short half = 7;
static unsigned char byte = 200;

int main(void) {
    int old = xadd(&counter, 2);
    short h = xadd(&half, 3);
    unsigned char b = xadd(&byte, 5);

    if (0) {
        __asm__ volatile("movq %rax, %rbx"); /* 64-bit only: never compiled */
    }
    if (sizeof(void *) == 2) {
        __asm__ volatile("bogus_instruction");
    }
    __asm__ volatile("nop # 100% basic asm");
    printf("%d %d %d %d %d %d\n", old, counter, h, half, b, byte);
    return 0;
}
