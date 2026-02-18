#ifndef _ARCH_I386_VM86_H
#define _ARCH_I386_VM86_H

#include <arch/i386/idt.h> // for registers_t

void vm86_gpf_handler(registers_t *regs);

#endif
