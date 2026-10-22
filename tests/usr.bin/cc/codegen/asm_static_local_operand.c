/*
 * A block-scope static is an asm memory operand by its symbol.  cc took
 * every block-scope variable for an automatic one and failed ("AST to SSA
 * lowering failed", no message) on Linux's fpu__init_system_mxcsr(),
 * which fxsaves into `static struct fxregs_state fxregs` through "+m".
 */
#include <stdio.h>

struct regs {
    unsigned int words[8];
};

static unsigned int mask_from(int c) {
    unsigned int mask = 0;

    if (c) {
        static struct regs saved;

        __asm__ volatile("movl $0xffbf, %0" : "+m"(saved));
        mask = saved.words[0];
    }
    return mask;
}

static int counter(void) {
    static int n;
    static int out;

    __asm__ volatile("incl %0" : "+m"(n));
    __asm__ volatile("movl $7, %0" : "=m"(out));
    return n * 10 + out;
}

int main(int argc, char **argv) {
    int a;
    int b;

    (void)argv;
    a = counter();
    b = counter();
    printf("%#x %u %d %d\n", mask_from(argc), mask_from(0), a, b);
    return 0;
}
