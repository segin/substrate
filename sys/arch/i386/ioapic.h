#ifndef _ARCH_I386_IOAPIC_H
#define _ARCH_I386_IOAPIC_H

#include <stdint.h>
#include <stdbool.h>

// IO-APIC Registers
#define IOREGSEL            0x00
#define IOWIN               0x10

// Indirect Register Offsets
#define IOAPICID            0x00
#define IOAPICVER           0x01
#define IOAPICARB           0x02
#define IOREDTBL(n)         (0x10 + 2 * (n))

void ioapic_init(uintptr_t base);
void ioapic_set_routing(uint8_t irq, uint8_t vector, uint32_t cpu_id);
void ioapic_set_mask(uint8_t irq, bool mask);

#endif
