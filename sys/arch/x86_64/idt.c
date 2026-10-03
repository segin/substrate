/*
 * idt.c - x86_64 interrupt descriptor table
 *
 * Every vector the i386 kernel installs, at the same numbers, pointing at
 * the isr.S stubs; what they dispatch to is shared with i386
 * (arch/x86-common/trap.c).  NMI, double fault and machine check run on
 * their own IST stacks (gdt.c), so a fault with a bad stack pointer still
 * reaches its handler.
 */
#include <stddef.h>
#include <string.h>

#include <kern/sched.h>
#include <sys/irq.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86-common/lapic.h>
#include <arch/x86-common/trap.h>

idt_entry_t idt_entries[256] __attribute__((aligned(16)));
idt_ptr_t   idt_ptr;

enum {
    IDT_VECTOR_COUNT = 256,
    IDT_EXCEPTION_COUNT = 32,
    IDT_IRQ_BASE = 32,
    IDT_IRQ_COUNT = 16,
    IDT_SYSCALL_VECTOR = 0x80,
    IDT_PANIC_IPI_VECTOR = 0xFB,
    IDT_FLAG_KERNEL_INT_GATE = IDT_FLAG_PRESENT | IDT_FLAG_INT32_GATE
};

void idt_set_gate_ist(uint8_t num, uintptr_t base, uint16_t sel, uint8_t flags,
                      uint8_t ist) {
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].sel = sel;
    idt_entries[num].ist = ist & 7;
    idt_entries[num].flags = flags;
    idt_entries[num].base_mid = (base >> 16) & 0xFFFF;
    idt_entries[num].base_high = (uint32_t)(base >> 32);
    idt_entries[num].reserved = 0;
}

void idt_set_gate(uint8_t num, uintptr_t base, uint16_t sel, uint8_t flags) {
    idt_set_gate_ist(num, base, sel, flags, IST_NONE);
}

const char *idt_exception_name(unsigned int vector) {
    return trap_exception_name(vector);
}

void idt_init(void) {
    static void (*const exception_handlers[])(void) = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };
    static void (*const irq_handlers[])(void) = {
        isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
        isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
    };

    idt_ptr.limit = sizeof(idt_entry_t) * IDT_VECTOR_COUNT - 1;
    idt_ptr.base = (uint64_t)(uintptr_t)&idt_entries;
    memset(&idt_entries, 0, sizeof(idt_entries));

    x86_pic_remap();

    for (unsigned v = 0; v < IDT_EXCEPTION_COUNT; v++) {
        idt_set_gate((uint8_t)v, (uintptr_t)exception_handlers[v], SEL_KCODE,
                     IDT_FLAG_KERNEL_INT_GATE);
    }
    idt_set_gate_ist(2, (uintptr_t)isr2, SEL_KCODE, IDT_FLAG_KERNEL_INT_GATE, IST_NMI);
    idt_set_gate_ist(8, (uintptr_t)isr8, SEL_KCODE, IDT_FLAG_KERNEL_INT_GATE, IST_DF);
    idt_set_gate_ist(18, (uintptr_t)isr18, SEL_KCODE, IDT_FLAG_KERNEL_INT_GATE, IST_MC);

    for (unsigned i = 0; i < IDT_IRQ_COUNT; i++) {
        idt_set_gate((uint8_t)(IDT_IRQ_BASE + i), (uintptr_t)irq_handlers[i],
                     SEL_KCODE, IDT_FLAG_KERNEL_INT_GATE);
    }

    /* int $0x80: the i386 system-call gate, callable from ring 3. */
    idt_set_gate(IDT_SYSCALL_VECTOR, (uintptr_t)isr128, SEL_KCODE,
                 IDT_FLAG_USER_INT_GATE);

    idt_set_gate(IDT_PANIC_IPI_VECTOR, (uintptr_t)isr_panic_ipi, SEL_KCODE,
                 IDT_FLAG_KERNEL_INT_GATE);
    idt_set_gate(SCHED_IPI_VECTOR, (uintptr_t)isr253, SEL_KCODE,
                 IDT_FLAG_KERNEL_INT_GATE);
    idt_set_gate(TLB_SHOOTDOWN_VECTOR, (uintptr_t)isr254, SEL_KCODE,
                 IDT_FLAG_KERNEL_INT_GATE);
    idt_set_gate(LAPIC_SPURIOUS_VECTOR, (uintptr_t)isr_spurious, SEL_KCODE,
                 IDT_FLAG_KERNEL_INT_GATE);

    /* Dynamic (MSI/MSI-X) vectors, 0x80 excepted. */
    for (unsigned v = IRQ_VECTOR_FIRST; v <= IRQ_VECTOR_LAST; v++) {
        if (v == IDT_SYSCALL_VECTOR)
            continue;
        idt_set_gate((uint8_t)v, (uintptr_t)msi_isr_stubs[v - IRQ_VECTOR_FIRST],
                     SEL_KCODE, IDT_FLAG_KERNEL_INT_GATE);
    }

    idt_flush((uintptr_t)&idt_ptr);
}
