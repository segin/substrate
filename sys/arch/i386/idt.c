/*
 * idt.c - i386 interrupt descriptor table
 *
 * Builds the IDT that points every vector at its isr.S stub.  What the
 * stubs dispatch to, isr_handler(), is shared with the x86_64 kernel and
 * lives in arch/x86-common/trap.c.
 */
#include <string.h>

#include <kern/sched.h>
#include <sys/irq.h>
#include <arch/i386/idt.h>
#include <arch/x86-common/lapic.h>
#include <arch/x86-common/trap.h>

idt_entry_t idt_entries[256] __attribute__((aligned(16)));
idt_ptr_t   idt_ptr;

enum {
    IDT_VECTOR_COUNT = 256,
    IDT_EXCEPTION_BASE = 0,
    IDT_EXCEPTION_COUNT = 32,
    IDT_IRQ_BASE = 32,
    IDT_IRQ_COUNT = 16,
    IDT_SYSCALL_VECTOR = 0x80,
    KERNEL_CODE_SELECTOR = 0x08,
    IDT_FLAG_KERNEL_INT_GATE = IDT_FLAG_PRESENT | IDT_FLAG_INT32_GATE
};

static void idt_install_range(uint8_t first_vector, const void *const *handlers,
                              size_t count, uint16_t selector, uint8_t flags) {
    size_t i;

    for (i = 0; i < count; i++) {
        idt_set_gate((uint8_t)(first_vector + i),
                     (uint32_t)(uintptr_t)handlers[i],
                     selector,
                     flags);
    }
}

void idt_init(void) {
    static const void *const exception_handlers[] = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };
    static const void *const irq_handlers[] = {
        isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
        isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
    };

    /*
     * The IDT is a dense 256-entry table covering CPU exceptions, remapped
     * PIC IRQs, and user-callable software gates such as INT 0x80.
     */
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_VECTOR_COUNT - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    memset(&idt_entries, 0, sizeof(idt_entry_t) * IDT_VECTOR_COUNT);

    x86_pic_remap();
    idt_install_range(IDT_EXCEPTION_BASE, exception_handlers,
                      IDT_EXCEPTION_COUNT, KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);
    idt_install_range(IDT_IRQ_BASE, irq_handlers,
                      IDT_IRQ_COUNT, KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);
    idt_set_gate(IDT_SYSCALL_VECTOR, (uint32_t)isr128,
                 KERNEL_CODE_SELECTOR, IDT_FLAG_USER_INT_GATE);

    /* Panic IPI (0xFB).  Handler is a bare cli;hlt loop in isr.S —
     * intentionally bypasses the common stub since we never return. */
        idt_set_gate(0xFB, (uint32_t)(uintptr_t)isr_panic_ipi,
                     KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);

    /* SMP IPIs routed through the common stub -> isr_handler dispatch.
     * Without these gates the first shootdown/preemption IPI on an SMP boot
     * GP-faults (vector-not-present) and cascades into a panic IPI storm. */
    idt_set_gate(SCHED_IPI_VECTOR, (uint32_t)(uintptr_t)isr253,
                 KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);
    idt_set_gate(TLB_SHOOTDOWN_VECTOR, (uint32_t)(uintptr_t)isr254,
                 KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);

    /* LAPIC spurious-interrupt vector.  lapic_enable(0xFF) puts
     * 0xFF in the SVR on the BSP and on every AP, but nothing ever installed
     * a gate for it, so the entry stayed not-present and a spurious interrupt
     * raised #GP.  See isr_spurious in isr.S for why it does not EOI. */
    idt_set_gate(LAPIC_SPURIOUS_VECTOR, (uint32_t)(uintptr_t)isr_spurious,
                 KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);

    /* Dynamic (MSI/MSI-X) vectors 0x50..0xBF: install a gate per vector so
     * LAPIC-delivered messages reach isr_handler, which routes them to
     * irq_dispatch() + a LAPIC EOI. */
    for (unsigned v = IRQ_VECTOR_FIRST; v <= IRQ_VECTOR_LAST; v++) {
        if (v == IDT_SYSCALL_VECTOR)   /* 0x80 keeps its user-accessible gate */
            continue;
        idt_set_gate((uint8_t)v,
                     (uint32_t)(uintptr_t)msi_isr_stubs[v - IRQ_VECTOR_FIRST],
                     KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);
    }

    idt_flush((uint32_t)&idt_ptr);
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel     = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags = flags;
}
