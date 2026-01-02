#include "lapic.h"
#include "pmap.h"
#include "../../drivers/video/vga.h"

// Default physical address for LAPIC
static uintptr_t lapic_phys = 0xFEE00000;
// Virtual address where LAPIC is mapped
static uintptr_t lapic_base = 0xFEE00000; 

static inline uint32_t lapic_read(uint32_t reg) {
    return *((volatile uint32_t*)(lapic_base + reg));
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    *((volatile uint32_t*)(lapic_base + reg)) = val;
}

void lapic_init(void) {
    vga_write("LAPIC: Initializing...\n", 23);

    // 1. Ensure LAPIC is mapped in PMAP
    // pmap_enter(pmap_kernel(), lapic_base, lapic_phys, VM_PROT_READ|VM_PROT_WRITE, 0);

    // 2. Set Spurious Interrupt Vector (and enable APIC)
    // Use vector 0xFF for spurious interrupts
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | LAPIC_SVR_ENABLE | 0xFF);

    // 3. Configure Timer (Divide by 16)
    lapic_write(LAPIC_TDCR, 0x03);
    
    // 4. Set Initial Count
    // lapic_write(LAPIC_TICF, 10000000); 

    vga_write("LAPIC: Enabled.\n", 16);
}

void lapic_send_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_get_id(void) {
    return lapic_read(LAPIC_ID) >> 24;
}

