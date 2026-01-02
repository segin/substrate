#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../../../sys/arch/i386/gdt.h"

/*
 * Property-based test: TSS Invariant
 * Prop: set_kernel_stack(s) -> tss.esp0 == s && tss.rest == unchanged.
 */

extern tss_entry_t tss_entry;

bool prop_tss_update_invariant(uintptr_t stack) {
    // 1. Capture initial state
    tss_entry_t initial;
    memcpy(&initial, &tss_entry, sizeof(tss_entry_t));
    
    // 2. Update
    set_kernel_stack(stack);
    
    // 3. Invariant check
    if (tss_entry.esp0 != stack) return false;
    
    // Reset esp0 in clone to compare the rest
    initial.esp0 = stack;
    return (memcmp(&initial, &tss_entry, sizeof(tss_entry_t)) == 0);
}

void run_gdt_properties(void) {
    prop_tss_update_invariant(0x1000);
    prop_tss_update_invariant(0xFFFFFFFC);
}
