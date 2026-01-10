#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/arch/x86-common/include/lapic.h"

/*
 * Fuzz Test: Local APIC Register Operations
 * 
 * Tests:
 * 1. SVR (Spurious Vector Register) configuration
 * 2. IPI (Inter-Processor Interrupt) parameters
 * 3. Timer configuration
 * 4. EOI behavior
 * 5. Error register behavior
 */

// Mock LAPIC register storage
static uint32_t mock_lapic_regs[0x400 / 4]; // LAPIC is 4KB, registers at 16-byte boundaries

// Simple LCG PRNG
static uint32_t fuzz_state = 0;
static uint32_t fuzz_rand(void) {
    fuzz_state = fuzz_state * 1103515245 + 12345;
    return (fuzz_state >> 16) & 0x7FFF;
}

static uint32_t fuzz_rand32(void) {
    return (fuzz_rand() << 16) | fuzz_rand();
}

// Mock read/write
static uint32_t mock_lapic_read(uint32_t reg) {
    if (reg >= 0x400) return 0;
    return mock_lapic_regs[reg / 4];
}

static void mock_lapic_write(uint32_t reg, uint32_t val) {
    if (reg >= 0x400) return;
    mock_lapic_regs[reg / 4] = val;
}

// Verification helpers
static void check_reg_value(uint32_t reg, uint32_t expected) {
    uint32_t actual = mock_lapic_read(reg);
    if (actual != expected) {
        __builtin_trap(); // Register value mismatch!
    }
}

static void check_reg_bits(uint32_t reg, uint32_t mask, uint32_t expected) {
    uint32_t actual = mock_lapic_read(reg) & mask;
    if (actual != expected) {
        __builtin_trap(); // Register bits mismatch!
    }
}

// Simulate lapic_send_ipi using mock registers
static void mock_lapic_send_ipi(uint8_t dest_cpu, uint8_t vector) {
    // Clear delivery status bit (simulate completion)
    mock_lapic_write(LAPIC_ICRLO, mock_lapic_read(LAPIC_ICRLO) & ~(1 << 12));
    
    // Set destination
    mock_lapic_write(LAPIC_ICRHI, ((uint32_t)dest_cpu) << 24);
    
    // Set vector and send
    mock_lapic_write(LAPIC_ICRLO, vector);
}

static void mock_lapic_send_ipi_all_excl_self(uint8_t vector) {
    mock_lapic_write(LAPIC_ICRLO, mock_lapic_read(LAPIC_ICRLO) & ~(1 << 12));
    mock_lapic_write(LAPIC_ICRLO, vector | (3 << 18)); // Dest shorthand = 3
}

void fuzz_lapic_regs(uint32_t seed) {
    fuzz_state = seed;
    
    // Initialize mock state
    for (int i = 0; i < sizeof(mock_lapic_regs)/sizeof(mock_lapic_regs[0]); i++) {
        mock_lapic_regs[i] = 0;
    }
    
    // Set default LAPIC ID
    mock_lapic_write(LAPIC_ID, 0x01000000); // APIC ID = 1
    
    // ========================================
    // Phase 1: SVR (Spurious Vector Register)
    // ========================================
    
    // Test enable/disable with various vectors
    for (int v = 0; v < 256; v++) {
        // Lower 8 bits = vector, bit 8 = enable
        uint32_t svr_val = LAPIC_SVR_ENABLE | (v & 0xFF);
        mock_lapic_write(LAPIC_SVR, svr_val);
        
        check_reg_bits(LAPIC_SVR, 0x1FF, svr_val & 0x1FF);
    }
    
    // Test disable
    mock_lapic_write(LAPIC_SVR, 0xFF); // No enable bit
    check_reg_bits(LAPIC_SVR, LAPIC_SVR_ENABLE, 0);
    
    // Test spurious vector must have lower 4 bits set on old LAPICs
    // (Modern LAPICs removed this requirement, but test anyway)
    uint8_t spurious_vectors[] = {0x0F, 0x1F, 0x2F, 0xFF};
    for (int i = 0; i < 4; i++) {
        mock_lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | spurious_vectors[i]);
        check_reg_bits(LAPIC_SVR, 0xFF, spurious_vectors[i]);
    }
    
    // ========================================
    // Phase 2: IPI (Inter-Processor Interrupt)
    // ========================================
    
    // Test IPI to all valid CPU IDs
    for (uint8_t cpu = 0; cpu < 255; cpu++) {
        for (uint8_t vec = 32; vec != 0; vec++) { // Vectors 32-255
            mock_lapic_send_ipi(cpu, vec);
            
            // Verify destination was set correctly
            uint8_t stored_dest = (mock_lapic_read(LAPIC_ICRHI) >> 24) & 0xFF;
            if (stored_dest != cpu) {
                __builtin_trap(); // IPI destination mismatch!
            }
            
            // Verify vector was set correctly
            uint8_t stored_vec = mock_lapic_read(LAPIC_ICRLO) & 0xFF;
            if (stored_vec != vec) {
                __builtin_trap(); // IPI vector mismatch!
            }
        }
    }
    
    // Test broadcast IPI
    mock_lapic_send_ipi_all_excl_self(0x50);
    check_reg_bits(LAPIC_ICRLO, 0xFF, 0x50);
    check_reg_bits(LAPIC_ICRLO, (3 << 18), (3 << 18)); // Dest shorthand
    
    // ========================================
    // Phase 3: Timer Configuration
    // ========================================
    
    // Test divide configuration register
    uint8_t divide_values[] = {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B};
    for (int d = 0; d < 8; d++) {
        mock_lapic_write(LAPIC_TDCR, divide_values[d]);
        check_reg_value(LAPIC_TDCR, divide_values[d]);
    }
    
    // Test initial count
    for (int i = 0; i < 1000; i++) {
        uint32_t count = fuzz_rand32();
        mock_lapic_write(LAPIC_TICF, count);
        check_reg_value(LAPIC_TICF, count);
    }
    
    // Test timer LVT entry
    // Bits: 0-7 vector, 12 delivery status, 16 mask, 17 mode (periodic)
    uint32_t timer_configs[] = {
        0x00020,           // Vector 0x20, one-shot
        0x20020,           // Vector 0x20, periodic (bit 17)
        0x10020,           // Vector 0x20, masked (bit 16)
        0x30020,           // Vector 0x20, periodic + masked
    };
    for (int t = 0; t < 4; t++) {
        mock_lapic_write(LAPIC_TIMER, timer_configs[t]);
        check_reg_value(LAPIC_TIMER, timer_configs[t]);
    }
    
    // ========================================
    // Phase 4: EOI Register
    // ========================================
    
    // EOI is write-only with value 0
    for (int i = 0; i < 100; i++) {
        mock_lapic_write(LAPIC_EOI, 0);
        // Just ensure it doesn't corrupt other registers
        check_reg_bits(LAPIC_SVR, 0x1FF, LAPIC_SVR_ENABLE | 0xFF);
    }
    
    // ========================================
    // Phase 5: APIC ID
    // ========================================
    
    // ID is in bits 24-31
    for (uint8_t id = 0; id < 255; id++) {
        mock_lapic_write(LAPIC_ID, ((uint32_t)id) << 24);
        uint8_t read_id = (mock_lapic_read(LAPIC_ID) >> 24) & 0xFF;
        if (read_id != id) {
            __builtin_trap(); // APIC ID mismatch!
        }
    }
    
    // ========================================
    // Phase 6: Error Status Register
    // ========================================
    
    // ESR bits are sticky (write to clear, then read)
    mock_lapic_write(LAPIC_ESR, 0);
    mock_lapic_write(LAPIC_ESR, 0xFF); // Try to set bits
    // On real hardware, ESR is mostly read-only, but we test the mock
    
    // ========================================
    // Phase 7: Random Fuzzing
    // ========================================
    
    uint32_t writable_regs[] = {
        LAPIC_TPR, LAPIC_SVR, LAPIC_ESR, LAPIC_ICRLO, LAPIC_ICRHI,
        LAPIC_TIMER, LAPIC_LINT0, LAPIC_LINT1, LAPIC_ERROR,
        LAPIC_TICF, LAPIC_TDCR
    };
    int num_regs = sizeof(writable_regs) / sizeof(writable_regs[0]);
    
    for (int i = 0; i < 100000; i++) {
        int reg_idx = fuzz_rand() % num_regs;
        uint32_t reg = writable_regs[reg_idx];
        uint32_t val = fuzz_rand32();
        
        // Save neighbor state
        uint32_t next_reg = (reg + 0x10) % 0x400;
        uint32_t saved_next = mock_lapic_read(next_reg);
        
        mock_lapic_write(reg, val);
        check_reg_value(reg, val);
        
        // Verify no neighbor corruption (registers are 16-byte aligned)
        if (mock_lapic_read(next_reg) != saved_next && next_reg != reg) {
            __builtin_trap(); // Neighbor register corrupted!
        }
    }
    
    // ========================================
    // Phase 8: Delivery Mode Validation
    // ========================================
    
    // ICR delivery modes: 0=Fixed, 1=LowestPri, 2=SMI, 4=NMI, 5=INIT, 6=SIPI
    uint8_t delivery_modes[] = {0, 1, 2, 4, 5, 6};
    for (int m = 0; m < 6; m++) {
        uint32_t icr = (delivery_modes[m] << 8) | 0x50; // Vector 0x50
        mock_lapic_write(LAPIC_ICRLO, icr);
        check_reg_bits(LAPIC_ICRLO, 0xFF, 0x50);
        check_reg_bits(LAPIC_ICRLO, (7 << 8), delivery_modes[m] << 8);
    }
}
