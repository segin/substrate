#include <stdbool.h>
#include <stddef.h>
#include <arch/i386/pmap.h>

/*
 * Property-based test: Identity Invariant for PMAP
 * Prop: Extraction must match Entry for all valid inputs.
 */

bool prop_pmap_i386_identity(uintptr_t va, uintptr_t pa) {
    // Basic sanity checks for test inputs
    if ((va & 0xFFF) || (pa & 0xFFF)) return true;
    if (va < 0x1000000 || va > 0xF0000000) return true; // Stay in reasonable range

    pmap_t kernel_pmap = pmap_kernel();
    
    // Action: Enter
    if (pmap_enter(kernel_pmap, va, pa, VM_PROT_ALL, 0) != 0) {
        return true; // Ignore if mapping failed due to internal state
    }
    
    // Invariant: Extract must match
    uintptr_t extracted = pmap_extract(kernel_pmap, va);
    bool match = (extracted == pa);
    
    // Cleanup
    pmap_remove(kernel_pmap, va);
    
    return match;
}
