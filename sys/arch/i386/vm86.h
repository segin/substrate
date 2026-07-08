#ifndef _ARCH_I386_VM86_H
#define _ARCH_I386_VM86_H

#include <arch/i386/idt.h> // for registers_t

struct vm86_struct;

void vm86_gpf_handler(registers_t *regs);
int vm86_init_bsd(void *args);

/* Assembly stubs (vm86_asm.S) */
void vm86_enter(struct vm86_struct *info);
void vm86_bios_ret_point(void);

#endif
