/*
 * Inline asm operand registers.
 *
 * An "=r" output and an "=a" output are two registers: cc gave the "=r"
 * one %eax as well, so the asm's two results landed in one register
 * (Linux's __get_user_asm: "=r" (err), "=a" (x) -- and then
 * extable_type_reg refused the operand).  Nor may an operand take a
 * register the asm clobbers.
 *
 * And a callee-saved register the asm uses -- an operand's, or a clobbered
 * one -- must still hold the caller's value when the function returns:
 * cc neither saved nor restored %ebx/%esi/%edi (%rbx, %r12-%r15).
 */
#include <stdio.h>

static int two_outputs(void) {
    int e;
    short s;

    __asm__("movl $7, %[e]\n\tmovw $5, %[s]" : [e] "=r"(e), [s] "=a"(s));
    return e * 10 + s;
}

static int clobbered_not_used(int x) {
    int r;

    /* %ecx/%rcx is wiped inside the asm; r must not live there */
    __asm__("movl %1, %0\n\tmovl $-1, %%ecx" : "=r"(r) : "r"(x) : "ecx");
    return r;
}

static int fixed_input_and_free_input(int a, int b) {
    int r;

    __asm__("movl %1, %0\n\taddl %2, %0" : "=&r"(r) : "S"(a), "r"(b));
    return r;
}

__attribute__((noinline)) int smash_callee_saved(void) {
    int r;

#if defined(__x86_64__)
    __asm__ volatile("movq $-1, %%rbx\n\tmovq $-1, %%r12\n\tmovl $3, %0" : "=r"(r) : : "rbx", "r12");
#else
    __asm__ volatile("movl $-1, %%ebx\n\tmovl $-1, %%edi\n\tmovl $3, %0" : "=r"(r) : : "ebx", "edi");
#endif
    return r;
}

static long caller_view(void) {
    long kept;

    /* hold a value in a callee-saved register across a call, as compiled code may */
#if defined(__x86_64__)
    __asm__ volatile("movq $0x5150, %%rbx\n\tcall smash_callee_saved\n\tmovq %%rbx, %0"
                     : "=r"(kept) : : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory");
#else
    __asm__ volatile("movl $0x5150, %%ebx\n\tcall smash_callee_saved\n\tmovl %%ebx, %0"
                     : "=r"(kept) : : "eax", "ecx", "edx", "memory");
#endif
    return kept;
}

int main(void) {
    printf("%d %d %d\n", two_outputs(), clobbered_not_used(41), fixed_input_and_free_input(30, 12));
    printf("%lx %d\n", caller_view(), smash_callee_saved());
    return 0;
}
