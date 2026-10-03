/*
 * early_boot.h - x86_64 early boot console and tables
 *
 * The same entry points as arch/i386/early_boot.h, which kmain() calls
 * before anything else is up.
 */
#ifndef _ARCH_X86_64_EARLY_BOOT_H
#define _ARCH_X86_64_EARLY_BOOT_H

#include <stdint.h>

void early_uart_print(const char *s);
void early_gdt_init(void);
void early_idt_init(void);

#endif
