#ifndef _ARCH_I386_EARLY_BOOT_H
#define _ARCH_I386_EARLY_BOOT_H

#include <stdint.h>

void early_uart_print(const char *s);
void early_gdt_init(void);
void early_idt_init(void);

#endif
