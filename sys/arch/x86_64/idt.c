/*
 * idt.c - x86_64 Interrupt Descriptor Table
 *
 * Handles CPU exceptions, hardware interrupts, and software interrupts
 * (syscalls via int 0x80 for compatibility, though syscall/sysret preferred).
 */

#include "idt.h"
#include "gdt.h"
#include <stdint.h>
#include <string.h>


static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr idt_pointer;

/* Exception handler table (defined in isr.S) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ handlers (remapped to vectors 32-47) */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* Syscall handler (int 0x80 for Linux compatibility) */
extern void isr128(void);

/* Set an IDT entry */
static void idt_set_gate(int index, uint64_t handler, uint16_t selector,
                         uint8_t ist, uint8_t type_attr) {
    idt[index].offset_low = handler & 0xFFFF;
    idt[index].selector = selector;
    idt[index].ist = ist & 0x07;
    idt[index].type_attr = type_attr;
    idt[index].offset_mid = (handler >> 16) & 0xFFFF;
    idt[index].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[index].reserved = 0;
}

/*
 * idt_init - Initialize the Interrupt Descriptor Table
 */
void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    
    /* CPU Exceptions (0-31) */
    idt_set_gate(0,  (uint64_t)isr0,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);  /* Divide Error */
    idt_set_gate(1,  (uint64_t)isr1,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);  /* Debug */
    idt_set_gate(2,  (uint64_t)isr2,  SEL_KCODE, IST_NMI, IDT_INTERRUPT_GATE);  /* NMI */
    idt_set_gate(3,  (uint64_t)isr3,  SEL_KCODE, 0, IDT_USER_INT_GATE);   /* Breakpoint (user) */
    idt_set_gate(4,  (uint64_t)isr4,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);  /* Overflow */
    idt_set_gate(5,  (uint64_t)isr5,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);  /* Bound Range */
    idt_set_gate(6,  (uint64_t)isr6,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);  /* Invalid Opcode */
    idt_set_gate(7,  (uint64_t)isr7,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);  /* Device Not Avail */
    idt_set_gate(8,  (uint64_t)isr8,  SEL_KCODE, IST_DF, IDT_INTERRUPT_GATE);  /* Double Fault */
    idt_set_gate(9,  (uint64_t)isr9,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);  /* Coprocessor Segment */
    idt_set_gate(10, (uint64_t)isr10, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Invalid TSS */
    idt_set_gate(11, (uint64_t)isr11, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Segment Not Present */
    idt_set_gate(12, (uint64_t)isr12, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Stack Segment */
    idt_set_gate(13, (uint64_t)isr13, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* General Protection */
    idt_set_gate(14, (uint64_t)isr14, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Page Fault */
    idt_set_gate(15, (uint64_t)isr15, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Reserved */
    idt_set_gate(16, (uint64_t)isr16, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* x87 FP */
    idt_set_gate(17, (uint64_t)isr17, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Alignment Check */
    idt_set_gate(18, (uint64_t)isr18, SEL_KCODE, IST_MC, IDT_INTERRUPT_GATE); /* Machine Check */
    idt_set_gate(19, (uint64_t)isr19, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* SIMD FP */
    idt_set_gate(20, (uint64_t)isr20, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Virtualization */
    idt_set_gate(21, (uint64_t)isr21, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Control Protection */
    idt_set_gate(22, (uint64_t)isr22, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Reserved */
    idt_set_gate(23, (uint64_t)isr23, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Reserved */
    idt_set_gate(24, (uint64_t)isr24, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Reserved */
    idt_set_gate(25, (uint64_t)isr25, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Reserved */
    idt_set_gate(26, (uint64_t)isr26, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Reserved */
    idt_set_gate(27, (uint64_t)isr27, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Reserved */
    idt_set_gate(28, (uint64_t)isr28, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Hypervisor Injection */
    idt_set_gate(29, (uint64_t)isr29, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* VMM Communication */
    idt_set_gate(30, (uint64_t)isr30, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Security Exception */
    idt_set_gate(31, (uint64_t)isr31, SEL_KCODE, 0, IDT_INTERRUPT_GATE); /* Reserved */
    
    /* Hardware IRQs (32-47) */
    idt_set_gate(32, (uint64_t)irq0,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(33, (uint64_t)irq1,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(34, (uint64_t)irq2,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(35, (uint64_t)irq3,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(36, (uint64_t)irq4,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(37, (uint64_t)irq5,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(38, (uint64_t)irq6,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(39, (uint64_t)irq7,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(40, (uint64_t)irq8,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(41, (uint64_t)irq9,  SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(42, (uint64_t)irq10, SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(43, (uint64_t)irq11, SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(44, (uint64_t)irq12, SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(45, (uint64_t)irq13, SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(46, (uint64_t)irq14, SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    idt_set_gate(47, (uint64_t)irq15, SEL_KCODE, 0, IDT_INTERRUPT_GATE);
    
    /* Syscall via int 0x80 (Linux compatibility) */
    idt_set_gate(0x80, (uint64_t)isr128, SEL_KCODE, 0, IDT_USER_INT_GATE);
    
    /* Load IDT */
    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (uint64_t)&idt;
    
    __asm__ volatile("lidt %0" :: "m"(idt_pointer));
}

/*
 * Register an interrupt handler dynamically
 */
void idt_set_handler(int vector, uint64_t handler, uint8_t type) {
    if (vector < 0 || vector >= IDT_ENTRIES) return;
    idt_set_gate(vector, handler, SEL_KCODE, 0, type);
}

/*
 * Enable interrupts
 */
void idt_enable(void) {
    __asm__ volatile("sti");
}

/*
 * Disable interrupts
 */
void idt_disable(void) {
    __asm__ volatile("cli");
}

/* Exception names for debugging */
static const char *exception_names[] = {
    "Divide Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

/*
 * Get exception name for debugging
 */
const char *idt_exception_name(int vector) {
    if (vector < 0 || vector > 31) return "Unknown";
    return exception_names[vector];
}
