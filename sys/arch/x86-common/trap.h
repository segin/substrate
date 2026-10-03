/*
 * trap.h - x86 trap dispatch shared by the i386 and x86_64 kernels
 * (trap.c); isr_handler() itself is declared in <machine/idt.h>.
 */
#ifndef _ARCH_X86_COMMON_TRAP_H
#define _ARCH_X86_COMMON_TRAP_H

/* Move the 8259 PIC's IRQs to vectors 32..47, clear of the exceptions. */
void x86_pic_remap(void);

/* Name of CPU exception `vector` (0..31), for diagnostics. */
const char *trap_exception_name(unsigned int vector);

#endif
