#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DECL_ISR(name) void name(void) {}
DECL_ISR(isr0) DECL_ISR(isr1) DECL_ISR(isr2) DECL_ISR(isr3)
DECL_ISR(isr4) DECL_ISR(isr5) DECL_ISR(isr6) DECL_ISR(isr7)
DECL_ISR(isr8) DECL_ISR(isr9) DECL_ISR(isr10) DECL_ISR(isr11)
DECL_ISR(isr12) DECL_ISR(isr13) DECL_ISR(isr14) DECL_ISR(isr15)
DECL_ISR(isr16) DECL_ISR(isr17) DECL_ISR(isr18) DECL_ISR(isr19)
DECL_ISR(isr20) DECL_ISR(isr21) DECL_ISR(isr22) DECL_ISR(isr23)
DECL_ISR(isr24) DECL_ISR(isr25) DECL_ISR(isr26) DECL_ISR(isr27)
DECL_ISR(isr28) DECL_ISR(isr29) DECL_ISR(isr30) DECL_ISR(isr31)
DECL_ISR(irq0) DECL_ISR(irq1) DECL_ISR(irq2) DECL_ISR(irq3)
DECL_ISR(irq4) DECL_ISR(irq5) DECL_ISR(irq6) DECL_ISR(irq7)
DECL_ISR(irq8) DECL_ISR(irq9) DECL_ISR(irq10) DECL_ISR(irq11)
DECL_ISR(irq12) DECL_ISR(irq13) DECL_ISR(irq14) DECL_ISR(irq15)
DECL_ISR(isr128)

#define HOST_TEST 1
#include "../../sys/arch/x86_64/idt.c"

static uint64_t entry_target(const struct idt_entry *entry) {
    return (uint64_t)entry->offset_low |
           ((uint64_t)entry->offset_mid << 16) |
           ((uint64_t)entry->offset_high << 32);
}

int main(void) {
    memset(idt, 0, sizeof(idt));
    idt_init();

    assert(idt[INT_DIVIDE_ERROR].selector == SEL_KCODE);
    assert(idt[INT_DIVIDE_ERROR].type_attr == IDT_INTERRUPT_GATE);
    assert(entry_target(&idt[INT_DIVIDE_ERROR]) == (uint64_t)isr0);

    assert(idt[INT_NMI].ist == IST_NMI);
    assert(idt[INT_DOUBLE_FAULT].ist == IST_DF);
    assert(idt[INT_MACHINE_CHECK].ist == IST_MC);
    assert(idt[INT_SYSCALL].type_attr == IDT_USER_INT_GATE);
    assert(entry_target(&idt[INT_SYSCALL]) == (uint64_t)isr128);
    assert(idt_pointer.limit == sizeof(idt) - 1);
    assert(idt_pointer.base == (uint64_t)&idt);
    assert(strcmp(idt_exception_name(INT_INVALID_OPCODE), "Invalid Opcode") == 0);
    assert(strcmp(idt_exception_name(255), "Unknown") == 0);

    puts("host_test_x86_64_idt: PASS");
    return 0;
}
