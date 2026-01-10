#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Fuzz Test: SMP Trampoline Code Validation
 * 
 * The trampoline is 16-bit real mode code that boots Application Processors (APs).
 * Tests:
 * 1. Trampoline data structure layout
 * 2. GDT descriptor validation
 * 3. Stack pointer setup
 * 4. Entry point configuration
 * 5. Memory boundary conditions
 */

// Trampoline memory layout (mirrors trampoline.S)
typedef struct __attribute__((packed)) {
    uint8_t code[64];           // 16-bit real mode boot code
    // GDT entries (8 bytes each)
    uint64_t gdt_null;          // Null descriptor
    uint64_t gdt_code;          // Code segment: 0x00CF9A000000FFFF
    uint64_t gdt_data;          // Data segment: 0x00CF92000000FFFF
    // GDT pointer (6 bytes)
    uint16_t gdt_limit;
    uint32_t gdt_base;
    // Parameters set by BSP
    uint32_t stack_ptr;
    uint32_t entry_point;
} trampoline_t;

// Expected GDT values for protected mode
#define GDT_CODE_EXPECTED   0x00CF9A000000FFFFULL
#define GDT_DATA_EXPECTED   0x00CF92000000FFFFULL

// Mock trampoline buffer (must be < 1MB for real mode)
static uint8_t trampoline_buffer[4096] __attribute__((aligned(4096)));
static trampoline_t *trampoline = (trampoline_t *)trampoline_buffer;

// Simple LCG PRNG
static uint32_t fuzz_state = 0;
static uint32_t fuzz_rand(void) {
    fuzz_state = fuzz_state * 1103515245 + 12345;
    return (fuzz_state >> 16) & 0x7FFF;
}

static uint32_t fuzz_rand32(void) {
    return (fuzz_rand() << 16) | fuzz_rand();
}

// Simulate trampoline initialization
static void mock_trampoline_init(uint32_t stack, uint32_t entry) {
    memset(trampoline_buffer, 0, sizeof(trampoline_buffer));
    
    // Setup GDT entries
    trampoline->gdt_null = 0;
    trampoline->gdt_code = GDT_CODE_EXPECTED;
    trampoline->gdt_data = GDT_DATA_EXPECTED;
    
    // GDT pointer
    trampoline->gdt_limit = 3 * 8 - 1; // 3 entries * 8 bytes - 1
    trampoline->gdt_base = (uint32_t)&trampoline->gdt_null;
    
    // Parameters
    trampoline->stack_ptr = stack;
    trampoline->entry_point = entry;
}

// Simulate AP boot validation
static int mock_validate_trampoline(void) {
    // Check GDT entries
    if (trampoline->gdt_null != 0) return -1;
    if (trampoline->gdt_code != GDT_CODE_EXPECTED) return -2;
    if (trampoline->gdt_data != GDT_DATA_EXPECTED) return -3;
    
    // Check GDT limit
    if (trampoline->gdt_limit != 23) return -4; // 3*8-1
    
    // Check stack is valid kernel address
    if (trampoline->stack_ptr < 0xC0000000) return -5;
    
    // Check entry point is valid kernel address
    if (trampoline->entry_point < 0xC0000000) return -6;
    
    return 0;
}

// Simulate setting parameters for a specific AP
static void mock_set_ap_params(uint8_t apic_id, uint32_t stack, uint32_t entry) {
    // In real code, we'd copy trampoline to low memory and set these
    (void)apic_id;
    trampoline->stack_ptr = stack;
    trampoline->entry_point = entry;
}

void fuzz_trampoline_params(uint32_t seed) {
    fuzz_state = seed;
    
    // ========================================
    // Phase 1: Basic Initialization
    // ========================================
    
    uint32_t valid_stack = 0xC0100000;
    uint32_t valid_entry = 0xC0200000;
    
    mock_trampoline_init(valid_stack, valid_entry);
    
    int result = mock_validate_trampoline();
    if (result != 0) {
        __builtin_trap(); // Basic init failed!
    }
    
    // Verify stack and entry
    if (trampoline->stack_ptr != valid_stack) {
        __builtin_trap(); // Stack mismatch!
    }
    if (trampoline->entry_point != valid_entry) {
        __builtin_trap(); // Entry mismatch!
    }
    
    // ========================================
    // Phase 2: GDT Descriptor Validation
    // ========================================
    
    // Verify code segment descriptor bits
    // Base = 0, Limit = 0xFFFFF (4GB), Access = 0x9A (present, ring 0, code, exec/read)
    // Flags = 0xC (4KB granularity, 32-bit)
    uint64_t code_desc = trampoline->gdt_code;
    
    uint32_t code_base = ((code_desc >> 16) & 0xFFFF) | 
                         ((code_desc >> 32) & 0xFF) << 16 |
                         ((code_desc >> 56) & 0xFF) << 24;
    if (code_base != 0) {
        __builtin_trap(); // Code base should be 0!
    }
    
    uint32_t code_limit = (code_desc & 0xFFFF) | ((code_desc >> 48) & 0x0F) << 16;
    if (code_limit != 0xFFFFF) {
        __builtin_trap(); // Code limit should be 0xFFFFF!
    }
    
    uint8_t code_access = (code_desc >> 40) & 0xFF;
    if (code_access != 0x9A) {
        __builtin_trap(); // Code access byte wrong!
    }
    
    // Verify data segment descriptor
    uint64_t data_desc = trampoline->gdt_data;
    uint8_t data_access = (data_desc >> 40) & 0xFF;
    if (data_access != 0x92) {
        __builtin_trap(); // Data access byte wrong!
    }
    
    // ========================================
    // Phase 3: Multiple AP Parameters
    // ========================================
    
    // Simulate booting 255 APs
    for (uint8_t apic_id = 1; apic_id != 0; apic_id++) {
        uint32_t ap_stack = 0xC0100000 + (apic_id * 0x10000); // 64KB per AP
        uint32_t ap_entry = 0xC0200000; // Same entry for all
        
        mock_set_ap_params(apic_id, ap_stack, ap_entry);
        
        // Verify
        if (trampoline->stack_ptr != ap_stack) {
            __builtin_trap(); // AP stack mismatch!
        }
        if (trampoline->entry_point != ap_entry) {
            __builtin_trap(); // AP entry mismatch!
        }
    }
    
    // ========================================
    // Phase 4: Boundary Conditions
    // ========================================
    
    // Edge case: Stack at very end of address space
    mock_set_ap_params(1, 0xFFFFFFF0, 0xC0200000);
    if (trampoline->stack_ptr != 0xFFFFFFF0) {
        __builtin_trap();
    }
    
    // Edge case: Entry at kernel base
    mock_set_ap_params(1, 0xC0100000, 0xC0000000);
    if (trampoline->entry_point != 0xC0000000) {
        __builtin_trap();
    }
    
    // ========================================
    // Phase 5: Memory Layout Integrity
    // ========================================
    
    // Verify modifying stack doesn't corrupt GDT
    uint64_t saved_code = trampoline->gdt_code;
    uint64_t saved_data = trampoline->gdt_data;
    
    for (int i = 0; i < 10000; i++) {
        uint32_t random_stack = fuzz_rand32() | 0xC0000000;
        uint32_t random_entry = fuzz_rand32() | 0xC0000000;
        
        mock_set_ap_params(i % 255, random_stack, random_entry);
        
        // Verify GDT wasn't corrupted
        if (trampoline->gdt_code != saved_code) {
            __builtin_trap(); // GDT code corrupted!
        }
        if (trampoline->gdt_data != saved_data) {
            __builtin_trap(); // GDT data corrupted!
        }
    }
    
    // ========================================
    // Phase 6: GDT Pointer Validation
    // ========================================
    
    // Limit must be 23 (3 entries * 8 - 1)
    if (trampoline->gdt_limit != 23) {
        __builtin_trap(); // GDT limit wrong!
    }
    
    // Base must point to null descriptor
    // (In real usage, this would be a physical address in low memory)
    
    // ========================================
    // Phase 7: Random Fuzzing
    // ========================================
    
    for (int i = 0; i < 100000; i++) {
        uint8_t apic_id = fuzz_rand() % 255;
        uint32_t stack = fuzz_rand32();
        uint32_t entry = fuzz_rand32();
        
        // Save current GDT state
        uint64_t pre_gdt_code = trampoline->gdt_code;
        uint64_t pre_gdt_data = trampoline->gdt_data;
        uint16_t pre_gdt_limit = trampoline->gdt_limit;
        
        mock_set_ap_params(apic_id, stack, entry);
        
        // Verify values stored
        if (trampoline->stack_ptr != stack) {
            __builtin_trap();
        }
        if (trampoline->entry_point != entry) {
            __builtin_trap();
        }
        
        // Verify GDT integrity
        if (trampoline->gdt_code != pre_gdt_code ||
            trampoline->gdt_data != pre_gdt_data ||
            trampoline->gdt_limit != pre_gdt_limit) {
            __builtin_trap(); // Parameter write corrupted GDT!
        }
    }
    
    // ========================================
    // Phase 8: Trampoline Size Constraints
    // ========================================
    
    // Trampoline must fit in a single page and be < 1MB
    // (Real mode can only address first 1MB)
    size_t trampoline_size = sizeof(trampoline_t);
    if (trampoline_size > 4096) {
        __builtin_trap(); // Trampoline too large for a page!
    }
    
    // Verify alignment
    if ((uintptr_t)trampoline_buffer & 0xFFF) {
        __builtin_trap(); // Trampoline not page-aligned!
    }
}
