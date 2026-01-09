#include "lapic.h"
#include "pmap.h"
#include "../../kern/console.h"

// Virtual address where LAPIC is mapped
static uintptr_t lapic_base = 0xFEE00000; 

static inline uint32_t lapic_read(uint32_t reg) {
    return *((volatile uint32_t*)(lapic_base + reg));
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    *((volatile uint32_t*)(lapic_base + reg)) = val;
}

void lapic_init(void) {
    kprint("LAPIC: Initializing...\n");

    // 1. Ensure LAPIC is mapped in PMAP
    // pmap_enter(pmap_kernel(), lapic_base, lapic_phys, VM_PROT_READ|VM_PROT_WRITE, 0);

    // 2. Set Spurious Interrupt Vector (and enable APIC)
    // Use vector 0xFF for spurious interrupts
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | LAPIC_SVR_ENABLE | 0xFF);

    // 3. Configure Timer (Divide by 16)
    lapic_write(LAPIC_TDCR, 0x03);
    
    // 4. Set Initial Count
    // lapic_write(LAPIC_TICF, 10000000); 

    kprint("LAPIC: Enabled.\n");
}

void lapic_send_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_get_id(void) {
    return lapic_read(LAPIC_ID) >> 24;
}

// Send IPI to a specific CPU
// dest_cpu: target LAPIC ID (0xFF = all CPUs including self)
// vector: interrupt vector to send
void lapic_send_ipi(uint8_t dest_cpu, uint8_t vector) {
    // Wait for any pending IPI to complete
    while (lapic_read(LAPIC_ICRLO) & (1 << 12)) {
        __asm__ volatile("pause");
    }
    
    // Set destination CPU in ICRHI
    lapic_write(LAPIC_ICRHI, ((uint32_t)dest_cpu) << 24);
    
    // Set vector and send (fixed delivery, level assert, physical mode)
    lapic_write(LAPIC_ICRLO, vector);
}

// Send IPI to all other CPUs (excluding self)
void lapic_send_ipi_all_excl_self(uint8_t vector) {
    // Wait for any pending IPI to complete
    while (lapic_read(LAPIC_ICRLO) & (1 << 12)) {
        __asm__ volatile("pause");
    }
    
    // All excluding self: dest shorthand = 3 (bits 18:19)
    // Physical mode, fixed delivery, level assert
    lapic_write(LAPIC_ICRLO, vector | (3 << 18));
}

