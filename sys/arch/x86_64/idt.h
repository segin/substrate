/*
 * idt.h - x86_64 Interrupt Descriptor Table
 */

#ifndef _ARCH_X86_64_IDT_H
#define _ARCH_X86_64_IDT_H

#include <stdint.h>

/* IDT entry (16 bytes in Long Mode) */
struct idt_entry {
    uint16_t offset_low;      /* Target RIP bits 0-15 */
    uint16_t selector;        /* Code segment selector */
    uint8_t  ist;             /* IST index (bits 0-2), reserved (bits 3-7) */
    uint8_t  type_attr;       /* Type and attributes */
    uint16_t offset_mid;      /* Target RIP bits 16-31 */
    uint32_t offset_high;     /* Target RIP bits 32-63 */
    uint32_t reserved;        /* Must be zero */
} __attribute__((packed));

/* IDT pointer */
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* Gate types */
#define IDT_INTERRUPT_GATE  0x8E    /* DPL=0, P=1, Type=1110 (interrupt) */
#define IDT_TRAP_GATE       0x8F    /* DPL=0, P=1, Type=1111 (trap) */
#define IDT_USER_INT_GATE   0xEE    /* DPL=3, P=1, Type=1110 (user callable) */

/* Maximum IDT entries */
#define IDT_ENTRIES 256
/*
 * Interrupt vectors
 */

/* CPU Exceptions (0-31) */
#define INT_DIVIDE_ERROR    0
#define INT_DEBUG           1
#define INT_NMI             2
#define INT_BREAKPOINT      3
#define INT_OVERFLOW        4
#define INT_BOUND_RANGE     5
#define INT_INVALID_OPCODE  6
#define INT_DEVICE_NA       7
#define INT_DOUBLE_FAULT    8
#define INT_COPROCESSOR     9
#define INT_INVALID_TSS     10
#define INT_SEGMENT_NP      11
#define INT_STACK_SEGMENT   12
#define INT_GPF             13
#define INT_PAGE_FAULT      14
#define INT_RESERVED15      15
#define INT_X87_FP          16
#define INT_ALIGNMENT       17
#define INT_MACHINE_CHECK   18
#define INT_SIMD_FP         19
#define INT_VIRTUALIZATION  20
#define INT_CONTROL_PROT    21

/* Hardware IRQs (remapped to 32-47) */
#define IRQ_BASE            32
#define IRQ_TIMER           (IRQ_BASE + 0)
#define IRQ_KEYBOARD        (IRQ_BASE + 1)
#define IRQ_CASCADE         (IRQ_BASE + 2)
#define IRQ_COM2            (IRQ_BASE + 3)
#define IRQ_COM1            (IRQ_BASE + 4)
#define IRQ_LPT2            (IRQ_BASE + 5)
#define IRQ_FLOPPY          (IRQ_BASE + 6)
#define IRQ_LPT1            (IRQ_BASE + 7)
#define IRQ_RTC             (IRQ_BASE + 8)
#define IRQ_ACPI            (IRQ_BASE + 9)
#define IRQ_FREE1           (IRQ_BASE + 10)
#define IRQ_FREE2           (IRQ_BASE + 11)
#define IRQ_MOUSE           (IRQ_BASE + 12)
#define IRQ_FPU             (IRQ_BASE + 13)
#define IRQ_ATA_PRIMARY     (IRQ_BASE + 14)
#define IRQ_ATA_SECONDARY   (IRQ_BASE + 15)

/* APIC vectors */
#define IRQ_APIC_BASE       48
#define IRQ_APIC_TIMER      (IRQ_APIC_BASE + 0)
#define IRQ_APIC_SPURIOUS   0xFF

/* Syscall vector (Linux compat) */
#define INT_SYSCALL         0x80

/* TLB shootdown IPI */
#define INT_TLB_SHOOTDOWN   0xFC

/*
 * Interrupt frame pushed by hardware/ISR stub
 */
struct interrupt_frame {
    /* Pushed by ISR stub */
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;
    uint64_t err_code;
    
    /* Pushed by hardware */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

/*
 * IDT gate types for dynamic registration
 */
#define IDT_TYPE_INT        0x8E    /* Interrupt gate (DPL=0) */
#define IDT_TYPE_TRAP       0x8F    /* Trap gate (DPL=0) */
#define IDT_TYPE_USER_INT   0xEE    /* Interrupt gate (DPL=3) */

/*
 * Functions
 */

/* Initialize IDT */
void idt_init(void);

/* Register a handler for a specific vector */
void idt_set_handler(int vector, uint64_t handler, uint8_t type);

/* Enable/disable interrupts */
void idt_enable(void);
void idt_disable(void);

/* Get exception name for debugging */
const char *idt_exception_name(int vector);

#endif /* _ARCH_X86_64_IDT_H */
