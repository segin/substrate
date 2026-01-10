#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/arch/x86-common/include/ioapic.h"

/*
 * Fuzz Test: IO-APIC Redirection Table Manipulation
 * 
 * Tests:
 * 1. Routing configuration (vector, destination CPU)
 * 2. Mask/unmask operations
 * 3. Edge cases (IRQ boundaries, vector ranges)
 * 4. Invariant preservation
 */

// Mock IO-APIC register state for testing
// Each IRQ has a 64-bit redirection entry (low 32 + high 32)
static uint32_t mock_ioredtbl_low[24];
static uint32_t mock_ioredtbl_high[24];
static bool ioapic_initialized = false;

// Simple LCG PRNG
static uint32_t fuzz_state = 0;
static uint32_t fuzz_rand(void) {
    fuzz_state = fuzz_state * 1103515245 + 12345;
    return (fuzz_state >> 16) & 0x7FFF;
}

static uint32_t fuzz_rand32(void) {
    return (fuzz_rand() << 16) | fuzz_rand();
}

// Mock implementations for testing without real hardware
static void mock_ioapic_set_routing(uint8_t irq, uint8_t vector, uint32_t cpu_id) {
    if (irq >= 24) return; // IO-APIC typically has 24 IRQ lines
    
    // Low 32 bits: vector (bits 0-7), delivery mode (8-10), dest mode (11),
    //              delivery status (12), polarity (13), remote IRR (14),
    //              trigger mode (15), mask (16)
    mock_ioredtbl_low[irq] = (mock_ioredtbl_low[irq] & 0xFFFFFF00) | vector;
    
    // High 32 bits: destination APIC ID in bits 24-31
    mock_ioredtbl_high[irq] = cpu_id << 24;
}

static void mock_ioapic_set_mask(uint8_t irq, bool mask) {
    if (irq >= 24) return;
    
    if (mask) {
        mock_ioredtbl_low[irq] |= (1 << 16);
    } else {
        mock_ioredtbl_low[irq] &= ~(1 << 16);
    }
}

static uint8_t mock_ioapic_get_vector(uint8_t irq) {
    if (irq >= 24) return 0;
    return mock_ioredtbl_low[irq] & 0xFF;
}

static uint8_t mock_ioapic_get_dest(uint8_t irq) {
    if (irq >= 24) return 0;
    return (mock_ioredtbl_high[irq] >> 24) & 0xFF;
}

static bool mock_ioapic_get_mask(uint8_t irq) {
    if (irq >= 24) return true;
    return (mock_ioredtbl_low[irq] >> 16) & 1;
}

// Validation helper
static void check_routing(uint8_t irq, uint8_t expected_vector, uint32_t expected_cpu) {
    uint8_t actual_vector = mock_ioapic_get_vector(irq);
    uint8_t actual_dest = mock_ioapic_get_dest(irq);
    
    if (actual_vector != expected_vector) {
        __builtin_trap(); // Vector mismatch!
    }
    if (actual_dest != (expected_cpu & 0xFF)) {
        __builtin_trap(); // Destination CPU mismatch!
    }
}

static void check_mask(uint8_t irq, bool expected_mask) {
    bool actual_mask = mock_ioapic_get_mask(irq);
    if (actual_mask != expected_mask) {
        __builtin_trap(); // Mask state mismatch!
    }
}

void fuzz_ioapic_routing(uint32_t seed) {
    fuzz_state = seed;
    
    // Initialize mock state
    for (int i = 0; i < 24; i++) {
        mock_ioredtbl_low[i] = 0x00010000; // Default: masked, vector 0
        mock_ioredtbl_high[i] = 0;
    }
    ioapic_initialized = true;
    
    // ========================================
    // Phase 1: Basic Routing Validation
    // ========================================
    for (uint8_t irq = 0; irq < 24; irq++) {
        uint8_t vector = 32 + irq; // Vectors 32-55 for IRQs
        uint32_t cpu = irq % 8;    // Distribute across 8 CPUs
        
        mock_ioapic_set_routing(irq, vector, cpu);
        check_routing(irq, vector, cpu);
    }
    
    // ========================================
    // Phase 2: Mask/Unmask Cycles
    // ========================================
    for (uint8_t irq = 0; irq < 24; irq++) {
        // Verify initial masked state from Phase 1
        // (set_routing doesn't change mask bit in our mock)
        
        // Unmask
        mock_ioapic_set_mask(irq, false);
        check_mask(irq, false);
        
        // Mask again
        mock_ioapic_set_mask(irq, true);
        check_mask(irq, true);
        
        // Toggle rapidly
        for (int t = 0; t < 100; t++) {
            bool m = (t % 2) == 0;
            mock_ioapic_set_mask(irq, m);
            check_mask(irq, m);
        }
    }
    
    // ========================================
    // Phase 3: Random Fuzzing
    // ========================================
    for (int i = 0; i < 100000; i++) {
        uint8_t irq = fuzz_rand() % 24;
        uint8_t vector = fuzz_rand() % 256;
        uint32_t cpu = fuzz_rand() % 256; // APIC ID can be 0-255
        bool mask = fuzz_rand() % 2;
        
        // Save neighbor state
        uint8_t neighbor = (irq + 1) % 24;
        uint32_t saved_low = mock_ioredtbl_low[neighbor];
        uint32_t saved_high = mock_ioredtbl_high[neighbor];
        
        // Apply routing
        mock_ioapic_set_routing(irq, vector, cpu);
        check_routing(irq, vector, cpu);
        
        // Apply mask
        mock_ioapic_set_mask(irq, mask);
        check_mask(irq, mask);
        
        // Verify routing wasn't corrupted by mask operation
        check_routing(irq, vector, cpu);
        
        // Verify neighbor wasn't corrupted
        if (mock_ioredtbl_low[neighbor] != saved_low ||
            mock_ioredtbl_high[neighbor] != saved_high) {
            __builtin_trap(); // Neighbor corruption!
        }
    }
    
    // ========================================
    // Phase 4: Boundary Conditions
    // ========================================
    
    // Test edge IRQs
    uint8_t edge_irqs[] = {0, 1, 22, 23};
    for (int i = 0; i < 4; i++) {
        uint8_t irq = edge_irqs[i];
        mock_ioapic_set_routing(irq, 0xFF, 0xFF);
        check_routing(irq, 0xFF, 0xFF);
        
        mock_ioapic_set_routing(irq, 0x00, 0x00);
        check_routing(irq, 0x00, 0x00);
    }
    
    // Test reserved/invalid vector range (0-15 are reserved for exceptions, 16-31 are awkward)
    for (uint8_t v = 0; v < 32; v++) {
        mock_ioapic_set_routing(0, v, 0);
        // Should still store the value even if semantically invalid
        check_routing(0, v, 0);
    }
    
    // ========================================
    // Phase 5: Delivery Mode Bits (Extended)
    // ========================================
    // Test that vector bits don't interfere with delivery mode bits
    uint8_t delivery_modes[] = {
        0x00, // Fixed
        0x01, // Lowest Priority
        0x02, // SMI
        0x04, // NMI
        0x05, // INIT
        0x07, // ExtINT
    };
    
    for (int d = 0; d < sizeof(delivery_modes); d++) {
        uint8_t irq = 10; // Use IRQ 10 for this test
        uint8_t vector = 0x50;
        
        // Manually set delivery mode in low bits 8-10
        mock_ioapic_set_routing(irq, vector, 0);
        mock_ioredtbl_low[irq] = (mock_ioredtbl_low[irq] & ~(7 << 8)) | (delivery_modes[d] << 8);
        
        // Verify vector wasn't corrupted
        if (mock_ioapic_get_vector(irq) != vector) {
            __builtin_trap(); // Delivery mode write corrupted vector!
        }
    }
    
    // ========================================
    // Phase 6: Trigger Mode & Polarity
    // ========================================
    {
        uint8_t irq = 15;
        mock_ioapic_set_routing(irq, 0x40, 0);
        
        // Set level-triggered (bit 15)
        mock_ioredtbl_low[irq] |= (1 << 15);
        
        // Set active-low polarity (bit 13)
        mock_ioredtbl_low[irq] |= (1 << 13);
        
        // Verify mask operation doesn't corrupt these bits
        mock_ioapic_set_mask(irq, true);
        if (!((mock_ioredtbl_low[irq] >> 15) & 1)) {
            __builtin_trap(); // Trigger mode bit corrupted!
        }
        if (!((mock_ioredtbl_low[irq] >> 13) & 1)) {
            __builtin_trap(); // Polarity bit corrupted!
        }
        
        mock_ioapic_set_mask(irq, false);
        if (!((mock_ioredtbl_low[irq] >> 15) & 1)) {
            __builtin_trap(); // Trigger mode bit corrupted on unmask!
        }
    }
}
