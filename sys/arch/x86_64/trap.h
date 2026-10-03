/*
 * trap.h - x86_64 exception and interrupt dispatch
 */
#ifndef _ARCH_X86_64_TRAP_H
#define _ARCH_X86_64_TRAP_H

#include <stdint.h>

#include <arch/x86_64/idt.h>

/* Breakpoint exceptions taken (the bring-up self-test counts them). */
extern volatile uint64_t trap_breakpoints;

/* Called from isr.S with the saved frame. */
void exception_handler(struct interrupt_frame *f);
void irq_handler(struct interrupt_frame *f);

#endif /* _ARCH_X86_64_TRAP_H */
