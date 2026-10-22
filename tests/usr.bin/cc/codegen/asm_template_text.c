/*
 * The text of an inline asm template, as gcc hands it to the assembler.
 *
 * The template is its string literal's value, decoded once: "\\reg" is a
 * backslash and "reg", a GAS macro argument.  cc decoded it twice, so
 * Linux's extable_type_reg macro, `.ifc \reg, %%\rs` and `.long \type`,
 * came out as a carriage return and "eg", a tab and "ype".
 *
 * And operand modifiers name the register at the width asked for: Linux
 * loads a segment with `movl %k0, %%ds` from a u16 and checks the
 * register with `extable_type_reg reg=%k0`, which wants %eax, not the
 * operand's own %ax.  cc ignored every modifier.
 */
#include <stdio.h>

static int via_macro(int x) {
    int r = x;

    __asm__(".macro add_it reg:req, val:req\n\t"
            "addl $\\val, %%\\reg\n\t"
            ".endm\n\t"
            "add_it eax, 5\n\t"
            ".purgem add_it"
            : "+a"(r));
    return r;
}

static unsigned modifiers(unsigned short h) {
    unsigned r = 0;
    unsigned short x = h;

    /* %k0: the 32-bit name of a u16's register; %b and %h its bytes */
    __asm__("movzwl %w1, %k0\n\t"
            "addb %h1, %b0"
            : "=&a"(r) : "b"(x));
    return r;
}

static int negated(void) {
    int r;

    __asm__("movl $%n1, %0" : "=r"(r) : "i"(9));
    return r;
}

int main(void) {
    printf("%d %u %d\n", via_macro(37), modifiers(0x1234), negated());
    return 0;
}
