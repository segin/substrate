#include "include/lapic.h"
#include "../i386/pmap.h"
#include "../../kern/console.h"

// Default LAPIC physical address (can be overridden by MADT)
#define LAPIC_DEFAULT_BASE  0xFEE00000

// Virtual address where LAPIC is mapped 
static uintptr_t lapic_base = 0;
static uint32_t lapic_phys_base = LAPIC_DEFAULT_BASE;
static bool lapic_initialized = false;

// Read LAPIC register
static inline uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) return 0;
    return *((volatile uint32_t*)(lapic_base + reg));
}

// Write LAPIC register
static inline void lapic_write(uint32_t reg, uint32_t val) {
    if (!lapic_base) return;
    *((volatile uint32_t*)(lapic_base + reg)) = val;
}

// Set LAPIC base address (called from MADT parsing)
void lapic_set_base(uint32_t phys_addr) {
    lapic_phys_base = phys_addr;
}

// Get LAPIC base physical address
uint32_t lapic_get_base(void) {
    return lapic_phys_base;
}

void lapic_init(void) {
    kprint("LAPIC: Initializing at physical 0x");
    // Print hex (simple for now)
    char buf[9];
    uint32_t n = lapic_phys_base;
    for (int i = 7; i >= 0; i--) {
        int d = n & 0xF;
        buf[i] = (d < 10) ? ('0' + d) : ('A' + d - 10);
        n >>= 4;
    }
    buf[8] = '\0';
    kprint(buf);
    kprint("...\n");

    // 1. Map LAPIC MMIO region
    // The LAPIC is typically identity-mapped in early boot or needs explicit mapping.
    // For i386 with higher-half kernel, use direct physical access if identity mapped,
    // or map via pmap. For simplicity, assume identity-mapped in low 4GB (common for MMIO).
    // On x86, LAPIC at 0xFEE00000 is usually accessible directly.
    lapic_base = lapic_phys_base;  // Identity map assumption
    
    // Alternatively, map if needed:
    // extern pmap_t pmap_kernel(void);
    // pmap_enter(pmap_kernel(), lapic_phys_base, lapic_phys_base, 
    //            VM_PROT_READ | VM_PROT_WRITE, PTE_PCD);
    // lapic_base = lapic_phys_base;

    // 2. Verify LAPIC is present by reading version register
    uint32_t ver = lapic_read(LAPIC_VER);
    uint8_t version = ver & 0xFF;
    uint8_t max_lvt = ((ver >> 16) & 0xFF) + 1;
    
    kprint("LAPIC: Version 0x");
    buf[0] = (version >> 4) < 10 ? '0' + (version >> 4) : 'A' + (version >> 4) - 10;
    buf[1] = (version & 0xF) < 10 ? '0' + (version & 0xF) : 'A' + (version & 0xF) - 10;
    buf[2] = '\0';
    kprint(buf);
    kprint(", Max LVT entries: ");
    if (max_lvt >= 10) { kprint("1"); buf[0] = '0' + (max_lvt - 10); buf[1] = '\0'; }
    else { buf[0] = '0' + max_lvt; buf[1] = '\0'; }
    kprint(buf);
    kprint("\n");

    // 3. Enable LAPIC via SVR (will be done in separate task)
    // 4. Configure timer (separate task)
    // 5. Setup error handling (separate task)

    lapic_initialized = true;
    kprint("LAPIC: Initialized successfully.\n");
}

// Check if LAPIC is initialized
bool lapic_is_initialized(void) {
    return lapic_initialized;
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

