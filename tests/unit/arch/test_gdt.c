#include "../../../sys/arch/i386/gdt.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * GDT/TSS Unit Tests
 */

extern gdt_entry_t gdt_entries[6];
extern tss_entry_t tss_entry;

bool test_gdt_structure(void) {
    // Check Kernel Code Segment (Entry 1)
    if (gdt_entries[1].access != 0x9A) return false;
    
    // Check User Data Segment (Entry 4)
    if (gdt_entries[4].access != 0xF2) return false;
    
    return true;
}

bool test_tss_stack_update(void) {
    uint32_t test_stack = 0x12345678;
    set_kernel_stack(test_stack);
    
    return (tss_entry.esp0 == test_stack);
}
