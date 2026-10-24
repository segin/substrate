/*
 * A local register variable, `register T x asm("reg")`, named by an asm
 * operand is in that register there, whatever the operand's constraint
 * lets it be, as gcc has it: Linux's put_user() passes its value in
 * %rax as an "r" operand of a call of __put_user_8, and the value it
 * stored was the pointer, which was in %rax -- a cc-built kernel gave
 * every program an argc that was an address.  Registers with a letter
 * (a b c d S D) and, on x86-64, without (r10, r12), inputs and outputs.
 */
#include <stdio.h>

#if defined(__x86_64__)
static long __attribute__((noinline)) via(long v, long *p) {
    register long val asm("%rax") = v;
    register long r10 asm("r10") = 7;
    register int ecx asm("ecx");
    long ret;

    asm volatile("movq %%rax, (%%rdx)\n\tleaq (%%r10,%%rax), %%rax\n\tmovl $5, %%ecx"
                 : "=a"(ret), "=r"(ecx)
                 : "r"(val), "d"(p), "r"(r10)
                 : "memory");
    return ret + ecx;
}

static long __attribute__((noinline)) high(long v) {
    register long x asm("r12") = v;
    long out;

    asm("movq %%r12, %0" : "=r"(out) : "r"(x));
    return out;
}
#else
static long __attribute__((noinline)) via(long v, long *p) {
    register long val asm("%eax") = v;
    register long esi asm("esi") = 7;
    register int ecx asm("ecx");
    long ret;

    asm volatile("movl %%eax, (%%edx)\n\tleal (%%esi,%%eax), %%eax\n\tmovl $5, %%ecx"
                 : "=a"(ret), "=r"(ecx)
                 : "r"(val), "d"(p), "r"(esi)
                 : "memory");
    return ret + ecx;
}

static long __attribute__((noinline)) high(long v) {
    register long x asm("edi") = v;
    long out;

    asm("movl %%edi, %0" : "=r"(out) : "r"(x));
    return out;
}
#endif

int main(void) {
    long slot = 0;
    long r = via(40, &slot);

    printf("%ld %ld %ld\n", r, slot, high(99));
    return 0;
}
