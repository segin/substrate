#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* idt.c and the machine headers it reaches are the 64-bit kernel's. */
#define SUBSTRATE_ARCH_X86_64 1
#define HOST_TEST 1

#define DECL_ISR(name) void name(void) {}
DECL_ISR(isr0) DECL_ISR(isr1) DECL_ISR(isr2) DECL_ISR(isr3)
DECL_ISR(isr4) DECL_ISR(isr5) DECL_ISR(isr6) DECL_ISR(isr7)
DECL_ISR(isr8) DECL_ISR(isr9) DECL_ISR(isr10) DECL_ISR(isr11)
DECL_ISR(isr12) DECL_ISR(isr13) DECL_ISR(isr14) DECL_ISR(isr15)
DECL_ISR(isr16) DECL_ISR(isr17) DECL_ISR(isr18) DECL_ISR(isr19)
DECL_ISR(isr20) DECL_ISR(isr21) DECL_ISR(isr22) DECL_ISR(isr23)
DECL_ISR(isr24) DECL_ISR(isr25) DECL_ISR(isr26) DECL_ISR(isr27)
DECL_ISR(isr28) DECL_ISR(isr29) DECL_ISR(isr30) DECL_ISR(isr31)
DECL_ISR(isr32) DECL_ISR(isr33) DECL_ISR(isr34) DECL_ISR(isr35)
DECL_ISR(isr36) DECL_ISR(isr37) DECL_ISR(isr38) DECL_ISR(isr39)
DECL_ISR(isr40) DECL_ISR(isr41) DECL_ISR(isr42) DECL_ISR(isr43)
DECL_ISR(isr44) DECL_ISR(isr45) DECL_ISR(isr46) DECL_ISR(isr47)
DECL_ISR(isr128) DECL_ISR(isr253) DECL_ISR(isr254)
DECL_ISR(isr_panic_ipi) DECL_ISR(isr_spurious)
DECL_ISR(msi_stub)

void *msi_isr_stubs[0xBF - 0x50 + 1];
static int pic_remapped;
static uintptr_t flushed_ptr;

void x86_pic_remap(void) { pic_remapped = 1; }
void idt_flush(uintptr_t p) { flushed_ptr = p; }
const char *trap_exception_name(unsigned int v) {
    return v == 6 ? "Invalid Opcode" : "Unknown Exception";
}

#include "../../sys/arch/x86_64/idt.c"

static uint64_t entry_target(const idt_entry_t *entry) {
    return (uint64_t)entry->base_low |
           ((uint64_t)entry->base_mid << 16) |
           ((uint64_t)entry->base_high << 32);
}

int main(void) {
    for (size_t i = 0; i < sizeof(msi_isr_stubs) / sizeof(msi_isr_stubs[0]); i++)
        msi_isr_stubs[i] = (void *)msi_stub;

    idt_init();

    assert(pic_remapped);
    assert(flushed_ptr == (uintptr_t)&idt_ptr);
    assert(idt_ptr.limit == sizeof(idt_entries) - 1);
    assert(idt_ptr.base == (uint64_t)(uintptr_t)&idt_entries);

    /* Exceptions: kernel interrupt gates on the 64-bit code segment. */
    assert(idt_entries[0].sel == SEL_KCODE);
    assert(idt_entries[0].flags == (IDT_FLAG_PRESENT | IDT_FLAG_INT32_GATE));
    assert(entry_target(&idt_entries[0]) == (uint64_t)(uintptr_t)isr0);
    assert(entry_target(&idt_entries[14]) == (uint64_t)(uintptr_t)isr14);

    /* NMI, double fault and machine check on their IST stacks. */
    assert(idt_entries[2].ist == IST_NMI);
    assert(idt_entries[8].ist == IST_DF);
    assert(idt_entries[18].ist == IST_MC);
    assert(idt_entries[14].ist == IST_NONE);

    /* The 8259 IRQs at 32-47, the i386 system-call gate at 0x80. */
    assert(entry_target(&idt_entries[32]) == (uint64_t)(uintptr_t)isr32);
    assert(entry_target(&idt_entries[47]) == (uint64_t)(uintptr_t)isr47);
    assert(idt_entries[0x80].flags == IDT_FLAG_USER_INT_GATE);
    assert(entry_target(&idt_entries[0x80]) == (uint64_t)(uintptr_t)isr128);

    /* MSI vectors, with 0x80 left as the system-call gate. */
    assert(entry_target(&idt_entries[0x50]) == (uint64_t)(uintptr_t)msi_stub);
    assert(entry_target(&idt_entries[0xBF]) == (uint64_t)(uintptr_t)msi_stub);

    assert(strcmp(idt_exception_name(6), "Invalid Opcode") == 0);

    puts("host_test_x86_64_idt: PASS");
    return 0;
}
