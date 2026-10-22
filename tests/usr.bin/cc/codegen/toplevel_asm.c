/*
 * A file-scope asm reaches the assembler as written, in its own section
 * when it switches to one, as gcc passes it: cc parsed it and dropped it,
 * so the symbol it defined was undefined (Linux's static-call trampolines,
 * __SCT__x86_pmu_add and the rest, at the vmlinux link).  The code after
 * it is back in .text.
 */
#include <stdio.h>

struct key {
    void *func;
    long type;
};

int toplevel_answer(void);
extern const int toplevel_datum;

struct key toplevel_key = {
    .func = 0,
    .type = 1,
};
asm(".pushsection .text.toplevel_asm, \"ax\"\n"
    ".globl toplevel_answer\n"
    "toplevel_answer:\n"
    "\tmovl $42, %eax\n"
    "\tret\n"
    ".popsection\n");
__asm__(".section .rodata.toplevel_asm, \"a\"\n"
        ".globl toplevel_datum\n"
        ".balign 4\n"
        "toplevel_datum:\n"
        "\t.long 7\n");

int after_asm(int x) {
    return x * 2;
}

int main(void) {
    printf("%d %d %d %ld\n", toplevel_answer(), toplevel_datum, after_asm(3), toplevel_key.type);
    return 0;
}
