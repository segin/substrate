/*
 * trap.c - x86_64 exception and interrupt dispatch (bring-up)
 *
 * isr.S saves the general registers and calls exception_handler() or
 * irq_handler() with a pointer to the frame.  For now every exception but a
 * breakpoint is fatal and reported in full; the breakpoint returns, so the
 * entry/exit path is exercised at boot.
 */
#include <stdint.h>

#include <arch/x86-common/io.h>
#include <arch/x86_64/cons.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/trap.h>

volatile uint64_t trap_breakpoints;

static uint64_t read_cr2(void)
{
	uint64_t v;

	__asm__ volatile("movq %%cr2, %0" : "=r"(v));
	return v;
}

static void trap_dump(const struct interrupt_frame *f)
{
	econs_printf("rax %016lx rbx %016lx rcx %016lx rdx %016lx\n",
	             f->rax, f->rbx, f->rcx, f->rdx);
	econs_printf("rsi %016lx rdi %016lx rbp %016lx rsp %016lx\n",
	             f->rsi, f->rdi, f->rbp, f->rsp);
	econs_printf("r8  %016lx r9  %016lx r10 %016lx r11 %016lx\n",
	             f->r8, f->r9, f->r10, f->r11);
	econs_printf("r12 %016lx r13 %016lx r14 %016lx r15 %016lx\n",
	             f->r12, f->r13, f->r14, f->r15);
	econs_printf("rip %016lx cs %04lx rflags %08lx ss %04lx err %lx\n",
	             f->rip, f->cs, f->rflags, f->ss, f->err_code);
}

void exception_handler(struct interrupt_frame *f)
{
	if (f->int_no == INT_BREAKPOINT) {
		trap_breakpoints++;
		return;
	}

	econs_printf("\nFATAL: %s (vector %lu) at %016lx",
	             idt_exception_name((int)f->int_no), f->int_no, f->rip);
	if (f->int_no == INT_PAGE_FAULT) {
		econs_printf(", address %016lx", read_cr2());
	}
	econs_puts("\n");
	trap_dump(f);
	econs_puts("KERNEL PANIC\n");
	for (;;) {
		__asm__ volatile("cli; hlt");
	}
}

void irq_handler(struct interrupt_frame *f)
{
	/* No interrupt controller is programmed yet; acknowledge an 8259
	 * vector anyway so a stray one cannot wedge the line. */
	if (f->int_no >= IRQ_BASE + 8 && f->int_no < IRQ_BASE + 16) {
		outb(0xA0, 0x20);
	}
	if (f->int_no >= IRQ_BASE && f->int_no < IRQ_BASE + 16) {
		outb(0x20, 0x20);
	}
}
