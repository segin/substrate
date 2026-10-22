/*
 * An asm operand whose constraint offers an immediate with a range and a
 * register or memory alternative takes the alternative for a value out of
 * that range or not constant at all, as gcc does.  Linux's set_bit is
 * `btsl %1, %0` with "Ir" (nr): I is 0..31, and the assembler refuses
 * `btsl $35` -- or with nr a parameter, cc guessed a constant from an
 * unrelated store and wrote `btsl $0`.  Its outw/inw take the port as
 * "Nd": N is 0..255, so port 0x3f8 must be in %dx.
 */
#include <stdio.h>

static void set_bit(int nr, volatile unsigned int *addr) {
    __asm__ volatile("btsl %1, %0" : "+m"(*addr) : "Ir"(nr) : "memory");
}

static int add_k(int x) {
    int r = x;

    /* K is -128..127: 100 an immediate, 1000 a register */
    __asm__("addl %1, %0" : "+r"(r) : "Kr"(100));
    __asm__("addl %1, %0" : "+r"(r) : "Kr"(1000));
    return r;
}

static int port_text_ok(void) {
    volatile int zero = 0;

    if (zero) {
        /* assembled, not run: 0x3f8 needs %dx, 0x80 may be an immediate */
        __asm__ volatile("outw %w0, %w1" : : "a"((unsigned short)0), "Nd"((unsigned short)0x3f8));
        __asm__ volatile("outb %b0, %w1" : : "a"((unsigned char)0), "Nd"((unsigned short)0x80));
    }
    return 1;
}

int main(void) {
    unsigned int w[3] = { 0, 0, 0 };

    set_bit(3, w);
    set_bit(35, w);
    set_bit(31, &w[2]);
    printf("%x %x %x %d %d\n", w[0], w[1], w[2], add_k(1), port_text_ok());
    return 0;
}
