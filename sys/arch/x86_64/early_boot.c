/*
 * early_boot.c - x86_64 early boot hooks for kmain()
 *
 * kmain() calls these before anything else.  boot.S has already left the
 * CPU in long mode on the loader's GDT, and kmain64() has the serial
 * console up, so the "early" tables are simply the real ones.
 */
#include <arch/x86_64/cons.h>
#include <arch/x86_64/early_boot.h>
#include <arch/x86_64/gdt.h>
#include <machine/idt.h>

void early_uart_print(const char *s) {
    econs_puts(s);
}

void early_gdt_init(void) {
    gdt_init();
}

void early_idt_init(void) {
    idt_init();
}
