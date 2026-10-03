/*
 * cons.h - x86_64 early console (COM1, polled)
 *
 * The bring-up console, usable from the first instruction of kmain64():
 * no interrupts, no allocation, no locks.  It stands in for the MI
 * console until the MI kernel is built for x86_64.
 */
#ifndef _ARCH_X86_64_CONS_H
#define _ARCH_X86_64_CONS_H

#include <stdarg.h>

void econs_init(void);
void econs_putc(char c);
void econs_puts(const char *s);
/* printf subset: %c %s %d %i %u %x %X %p %%, with h/l/ll and 0-padded width. */
int  econs_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  econs_vprintf(const char *fmt, va_list ap);

#endif /* _ARCH_X86_64_CONS_H */
