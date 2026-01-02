#include "ioapic.h"
#include "../../drivers/video/vga.h"

static uintptr_t ioapic_base = 0;

static inline uint32_t ioapic_read(uint8_t reg) {
    *((volatile uint32_t*)(ioapic_base + IOREGSEL)) = reg;
    return *((volatile uint32_t*)(ioapic_base + IOWIN));
}

static inline void ioapic_write(uint8_t reg, uint32_t val) {
    *((volatile uint32_t*)(ioapic_base + IOREGSEL)) = reg;
    *((volatile uint32_t*)(ioapic_base + IOWIN)) = val;
}

void ioapic_init(uintptr_t base) {
    ioapic_base = base;
    vga_write("IO-APIC: Initialized at 0x", 26);
    // TODO: print hex base
}

void ioapic_set_routing(uint8_t irq, uint8_t vector, uint32_t cpu_id) {
    uint32_t low = vector; // Delivery mode: Fixed, Phys Dest, etc (all 0)
    uint32_t high = cpu_id << 24;

    ioapic_write(IOREDTBL(irq), low);
    ioapic_write(IOREDTBL(irq) + 1, high);
}

void ioapic_set_mask(uint8_t irq, bool mask) {
    uint32_t low = ioapic_read(IOREDTBL(irq));
    if (mask) {
        low |= (1 << 16);
    } else {
        low &= ~(1 << 16);
    }
    ioapic_write(IOREDTBL(irq), low);
}
