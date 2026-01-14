#ifndef _ARCH_X86_IOAPIC_H
#define _ARCH_X86_IOAPIC_H

#include <stdint.h>
#include <stdbool.h>

// IO-APIC Register Offsets (via IOREGSEL/IOWIN)
#define IOREGSEL            0x00    // Register Select
#define IOWIN               0x10    // Window Register

// Indirect Register Indices
#define IOAPICID            0x00    // IO-APIC ID
#define IOAPICVER           0x01    // IO-APIC Version
#define IOAPICARB           0x02    // IO-APIC Arbitration ID
#define IOREDTBL(n)         (0x10 + 2 * (n))  // Redirection Table Entry

// Redirection Entry Flags (Low 32 bits)
#define IOAPIC_DELIVERY_FIXED       0x000   // Fixed delivery
#define IOAPIC_DELIVERY_LOWPRI      0x100   // Lowest priority
#define IOAPIC_DELIVERY_SMI         0x200   // SMI
#define IOAPIC_DELIVERY_NMI         0x400   // NMI
#define IOAPIC_DELIVERY_INIT        0x500   // INIT
#define IOAPIC_DELIVERY_EXTINT      0x700   // ExtINT

#define IOAPIC_DESTMODE_PHYSICAL    0x000   // Physical destination
#define IOAPIC_DESTMODE_LOGICAL     0x800   // Logical destination

#define IOAPIC_POLARITY_HIGH        0x0000  // Active high
#define IOAPIC_POLARITY_LOW         0x2000  // Active low

#define IOAPIC_TRIGGER_EDGE         0x0000  // Edge triggered
#define IOAPIC_TRIGGER_LEVEL        0x8000  // Level triggered

#define IOAPIC_MASKED               0x10000 // Interrupt masked

// Initialization
void ioapic_init(uintptr_t base);
int ioapic_register(uintptr_t base, uint8_t id, uint32_t gsi_base);
int ioapic_get_count(void);

// Routing
void ioapic_set_routing(uint8_t irq, uint8_t vector, uint32_t cpu_id);
void ioapic_set_routing_ex(uint32_t gsi, uint8_t vector, uint32_t dest_cpu,
                           uint8_t delivery_mode, uint8_t dest_mode,
                           uint8_t polarity, uint8_t trigger);

// Masking
void ioapic_set_mask(uint8_t irq, bool mask);
void ioapic_mask_all(void);

#endif /* _ARCH_X86_IOAPIC_H */
