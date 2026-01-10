#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/arch/i386/idt.h"

/*
 * Fuzz Test: Random IDT gate configurations
 * Validation: Struct packing, Flag preservation, Boundary checks
 */

extern idt_entry_t idt_entries[256];

// Simple LCG PRNG
static uint32_t fuzz_state = 0;
static uint32_t fuzz_rand(void) {
    fuzz_state = fuzz_state * 1103515245 + 12345;
    return (fuzz_state / 65536) % 32768;
}

static uint32_t fuzz_rand32(void) {
    return (fuzz_rand() << 16) | fuzz_rand();
}

static void check_idt_match(int num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entry_t *e = &idt_entries[num];
    
    // Check Base Split
    uint32_t read_base = e->base_low | (e->base_high << 16);
    if (read_base != base) {
        // printf("FAIL: Base mismatch idx=%d expected=%08x actual=%08x\n", num, base, read_base);
        // Assuming panic or abort in real test runner, here just silent or flag
        __builtin_trap(); 
    }
    
    // Check Selector
    if (e->sel != sel) {
        __builtin_trap(); 
    }
    
    // Check Flags
    if (e->flags != flags) {
        __builtin_trap(); 
    }
    
    // Check Always0
    if (e->always0 != 0) {
        __builtin_trap(); 
    }
}

void fuzz_idt_gates(uint32_t seed) {
    fuzz_state = seed;
    
    // Phase 1: Boundary Vectors
    uint8_t boundary_vectors[] = {0, 1, 31, 32, 128, 255};
    for (int i = 0; i < sizeof(boundary_vectors); i++) {
        uint8_t v = boundary_vectors[i];
        uint32_t base = fuzz_rand32();
        uint16_t sel = fuzz_rand();
        uint8_t flags = fuzz_rand() & 0xFF;
        
        idt_set_gate(v, base, sel, flags);
        check_idt_match(v, base, sel, flags);
    }
    
    // Phase 2: Random Fuzzing
    for (int i = 0; i < 100000; i++) {
        // Generate valid params
        uint8_t num = fuzz_rand() % 256;
        uint32_t base = fuzz_rand32();
        uint16_t sel = fuzz_rand();
        uint8_t flags = fuzz_rand() & 0xFF;

        // Neighbor check: Corruption detection
        uint8_t neighbor = (num + 1) % 256;
        idt_entry_t saved_neighbor = idt_entries[neighbor];
        
        // Execute SUT
        idt_set_gate(num, base, sel, flags);
        
        // Validate Target
        check_idt_match(num, base, sel, flags);
        
        // Validate Neighbor (ensure no overwrite)
        idt_entry_t *n = &idt_entries[neighbor];
        if (n->base_low != saved_neighbor.base_low || 
            n->base_high != saved_neighbor.base_high ||
            n->sel != saved_neighbor.sel ||
            n->flags != saved_neighbor.flags) {
            __builtin_trap(); // Neighbor corruption detected!
        }
    }
    
    // Phase 3: Gate Type Constraints
    // Test valid gate types and flag combinations
    
    // x86 IDT Gate Type Constants
    #define IDT_GATE_INTERRUPT32  0x0E  // 32-bit Interrupt Gate
    #define IDT_GATE_TRAP32       0x0F  // 32-bit Trap Gate
    #define IDT_GATE_TASK         0x05  // Task Gate
    #define IDT_FLAG_PRESENT      0x80  // Present bit
    #define IDT_FLAG_DPL_RING0    0x00  // DPL = 0
    #define IDT_FLAG_DPL_RING3    0x60  // DPL = 3
    
    // Valid flag combinations
    uint8_t valid_flags[] = {
        IDT_FLAG_PRESENT | IDT_FLAG_DPL_RING0 | IDT_GATE_INTERRUPT32, // 0x8E - Kernel interrupt
        IDT_FLAG_PRESENT | IDT_FLAG_DPL_RING0 | IDT_GATE_TRAP32,      // 0x8F - Kernel trap
        IDT_FLAG_PRESENT | IDT_FLAG_DPL_RING3 | IDT_GATE_INTERRUPT32, // 0xEE - User interrupt (syscall)
        IDT_FLAG_PRESENT | IDT_FLAG_DPL_RING3 | IDT_GATE_TRAP32,      // 0xEF - User trap
        0x00, // Not present - should be accepted but gate inactive
    };
    
    // Test valid combinations on exception vectors (0-31)
    for (int v = 0; v < 32; v++) {
        for (int f = 0; f < sizeof(valid_flags); f++) {
            uint32_t base = 0xC0100000 + (v * 0x100); // Simulated handler addresses
            uint16_t sel = 0x08; // Kernel code segment
            uint8_t flags = valid_flags[f];
            
            idt_set_gate(v, base, sel, flags);
            check_idt_match(v, base, sel, flags);
        }
    }
    
    // Test IRQ vectors (32-47) with interrupt gates only
    for (int v = 32; v < 48; v++) {
        uint32_t base = 0xC0110000 + ((v - 32) * 0x100);
        uint16_t sel = 0x08;
        uint8_t flags = IDT_FLAG_PRESENT | IDT_FLAG_DPL_RING0 | IDT_GATE_INTERRUPT32;
        
        idt_set_gate(v, base, sel, flags);
        check_idt_match(v, base, sel, flags);
    }
    
    // Test syscall vector (0x80) with Ring 3 access
    {
        uint32_t base = 0xC0120000;
        uint16_t sel = 0x08;
        uint8_t flags = IDT_FLAG_PRESENT | IDT_FLAG_DPL_RING3 | IDT_GATE_INTERRUPT32; // 0xEE
        
        idt_set_gate(0x80, base, sel, flags);
        check_idt_match(0x80, base, sel, flags);
        
        // Verify DPL is correctly encoded (bits 5-6)
        uint8_t dpl = (idt_entries[0x80].flags >> 5) & 0x03;
        if (dpl != 3) {
            __builtin_trap(); // DPL encoding error!
        }
    }
    
    // Phase 4: Invalid/Edge Case Flags
    // These are technically "valid" writes but semantically questionable
    uint8_t edge_flags[] = {
        0x00,                           // Not present, type 0
        0x80,                           // Present but invalid gate type (0)
        0x85,                           // Task gate (rarely used)
        0x8C,                           // 16-bit interrupt gate (legacy)
        0x8D,                           // 16-bit trap gate (legacy)
        0xFF,                           // All bits set
    };
    
    for (int v = 200; v < 206; v++) { // Use high vectors to avoid overwriting important ones
        uint8_t flags = edge_flags[v - 200];
        uint32_t base = fuzz_rand32();
        uint16_t sel = 0x08;
        
        idt_set_gate(v, base, sel, flags);
        check_idt_match(v, base, sel, flags);
        
        // Verify Present bit extraction
        bool present = (idt_entries[v].flags & IDT_FLAG_PRESENT) != 0;
        bool expected_present = (flags & IDT_FLAG_PRESENT) != 0;
        if (present != expected_present) {
            __builtin_trap(); // Present bit encoding error!
        }
    }
    
    // Phase 5: Selector Validation
    // Common valid selectors: 0x08 (kernel code), 0x1B (user code)
    uint16_t valid_selectors[] = {0x08, 0x10, 0x18, 0x1B, 0x23, 0x2B, 0x33};
    for (int s = 0; s < sizeof(valid_selectors)/sizeof(valid_selectors[0]); s++) {
        uint8_t vec = 100 + s;
        uint32_t base = fuzz_rand32();
        uint16_t sel = valid_selectors[s];
        uint8_t flags = 0x8E;
        
        idt_set_gate(vec, base, sel, flags);
        check_idt_match(vec, base, sel, flags);
        
        // Verify RPL bits (bits 0-1 of selector)
        uint8_t rpl = idt_entries[vec].sel & 0x03;
        uint8_t expected_rpl = sel & 0x03;
        if (rpl != expected_rpl) {
            __builtin_trap(); // Selector RPL encoding error!
        }
    }
}
