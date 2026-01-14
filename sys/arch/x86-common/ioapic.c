/*
 * ioapic.c - IO-APIC Driver
 * 
 * Manages interrupt routing from external devices to CPUs via the I/O APIC.
 */

#include "include/ioapic.h"
#include "../../kern/console.h"

// Maximum number of IO-APICs supported
#define MAX_IOAPICS 8

// IO-APIC descriptor
typedef struct {
    uintptr_t base;         // MMIO base address
    uint8_t id;             // IO-APIC ID
    uint32_t gsi_base;      // Global System Interrupt base
    uint8_t max_redir;      // Maximum redirection entries
    bool present;           // Is this entry valid?
} ioapic_t;

// Global IO-APIC array
static ioapic_t ioapics[MAX_IOAPICS];
static int ioapic_count = 0;

// Read IO-APIC register
static inline uint32_t ioapic_read(ioapic_t *apic, uint8_t reg) {
    *((volatile uint32_t*)(apic->base + IOREGSEL)) = reg;
    return *((volatile uint32_t*)(apic->base + IOWIN));
}

// Write IO-APIC register
static inline void ioapic_write(ioapic_t *apic, uint8_t reg, uint32_t val) {
    *((volatile uint32_t*)(apic->base + IOREGSEL)) = reg;
    *((volatile uint32_t*)(apic->base + IOWIN)) = val;
}

// Register an IO-APIC (called from MADT parsing)
int ioapic_register(uintptr_t base, uint8_t id, uint32_t gsi_base) {
    if (ioapic_count >= MAX_IOAPICS) {
        kprint("IO-APIC: Maximum number of IO-APICs reached!\n");
        return -1;
    }
    
    ioapic_t *apic = &ioapics[ioapic_count];
    apic->base = base;
    apic->id = id;
    apic->gsi_base = gsi_base;
    apic->present = true;
    
    // Read version register to get max redirection entries
    uint32_t ver = ioapic_read(apic, IOAPICVER);
    apic->max_redir = ((ver >> 16) & 0xFF) + 1;
    
    kprint("IO-APIC: Registered ID=");
    char buf[4];
    buf[0] = '0' + id;
    buf[1] = '\0';
    kprint(buf);
    kprint(" at 0x");
    // Print hex base
    uint32_t n = (uint32_t)base;
    for (int i = 7; i >= 0; i--) {
        int d = (n >> (i * 4)) & 0xF;
        buf[0] = (d < 10) ? ('0' + d) : ('A' + d - 10);
        buf[1] = '\0';
        kprint(buf);
    }
    kprint(" GSI=");
    buf[0] = '0' + (gsi_base / 10);
    buf[1] = '0' + (gsi_base % 10);
    buf[2] = '\0';
    if (gsi_base < 10) { buf[0] = buf[1]; buf[1] = '\0'; }
    kprint(buf);
    kprint(" Entries=");
    buf[0] = '0' + (apic->max_redir / 10);
    buf[1] = '0' + (apic->max_redir % 10);
    buf[2] = '\0';
    if (apic->max_redir < 10) { buf[0] = buf[1]; buf[1] = '\0'; }
    kprint(buf);
    kprint("\n");
    
    ioapic_count++;
    return 0;
}

// Initialize IO-APIC (legacy function for backward compatibility)
void ioapic_init(uintptr_t base) {
    ioapic_register(base, 0, 0);
}

// Find IO-APIC responsible for a given GSI
static ioapic_t *ioapic_find_gsi(uint32_t gsi) {
    for (int i = 0; i < ioapic_count; i++) {
        ioapic_t *apic = &ioapics[i];
        if (apic->present && 
            gsi >= apic->gsi_base && 
            gsi < apic->gsi_base + apic->max_redir) {
            return apic;
        }
    }
    return NULL;
}

// Get number of registered IO-APICs
int ioapic_get_count(void) {
    return ioapic_count;
}

// ==================== Redirection Table ====================

// Set routing for an IRQ
void ioapic_set_routing(uint8_t irq, uint8_t vector, uint32_t cpu_id) {
    ioapic_t *apic = ioapic_find_gsi(irq);
    if (!apic) return;
    
    uint8_t entry = irq - apic->gsi_base;
    
    // Build redirection entry
    // Low 32 bits: vector, delivery mode, dest mode, polarity, trigger, mask
    uint32_t low = vector;  // Fixed delivery, physical mode, active high, edge
    uint32_t high = cpu_id << 24;
    
    ioapic_write(apic, IOREDTBL(entry), low);
    ioapic_write(apic, IOREDTBL(entry) + 1, high);
}

// Set routing with full control over delivery options
void ioapic_set_routing_ex(uint32_t gsi, uint8_t vector, uint32_t dest_cpu,
                           uint8_t delivery_mode, uint8_t dest_mode,
                           uint8_t polarity, uint8_t trigger) {
    ioapic_t *apic = ioapic_find_gsi(gsi);
    if (!apic) return;
    
    uint8_t entry = gsi - apic->gsi_base;
    
    // Build redirection entry (64-bit split into low/high)
    uint32_t low = vector;
    low |= (delivery_mode & 0x7) << 8;  // Delivery mode (bits 10:8)
    low |= (dest_mode & 0x1) << 11;     // Destination mode (bit 11)
    low |= (polarity & 0x1) << 13;      // Polarity (bit 13)
    low |= (trigger & 0x1) << 15;       // Trigger mode (bit 15)
    // Bit 16 = mask (leave unmasked by default)
    
    uint32_t high = dest_cpu << 24;     // Destination APIC ID
    
    ioapic_write(apic, IOREDTBL(entry), low);
    ioapic_write(apic, IOREDTBL(entry) + 1, high);
}

// Mask/Unmask an IRQ
void ioapic_set_mask(uint8_t irq, bool mask) {
    ioapic_t *apic = ioapic_find_gsi(irq);
    if (!apic) return;
    
    uint8_t entry = irq - apic->gsi_base;
    
    uint32_t low = ioapic_read(apic, IOREDTBL(entry));
    if (mask) {
        low |= IOAPIC_MASKED;
    } else {
        low &= ~IOAPIC_MASKED;
    }
    ioapic_write(apic, IOREDTBL(entry), low);
}

// Mask all IRQs on all IO-APICs
void ioapic_mask_all(void) {
    for (int i = 0; i < ioapic_count; i++) {
        ioapic_t *apic = &ioapics[i];
        if (!apic->present) continue;
        
        for (int j = 0; j < apic->max_redir; j++) {
            uint32_t low = ioapic_read(apic, IOREDTBL(j));
            low |= IOAPIC_MASKED;
            ioapic_write(apic, IOREDTBL(j), low);
        }
    }
}

