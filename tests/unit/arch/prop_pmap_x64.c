#include <stdbool.h>
#include <stddef.h>
#include <arch/x86_64/pmap.h"

/*
 * Property-based test: Identity Invariant for PMAP (x86_64)
 * Prop: Extraction must match Entry for all valid 64-bit inputs.
 */

bool prop_pmap_x64_identity(uint64_t va, uint64_t pa) {
    // Basic sanity checks for 64-bit test inputs
    if ((va & 0xFFF) || (pa & 0xFFF)) return true;
    
    // Canonical address check (simplified)
    if (va > 0x00007FFFFFFFFFFFULL && va < 0xFFFF800000000000ULL) return true;

    pmap_t kernel_pmap = pmap_kernel();
    
    // Action: Enter
    if (pmap_enter(kernel_pmap, va, pa, 0, 0) != 0) {
        return true; 
    }
    
    // Invariant: Extract must match
    uint64_t extracted = pmap_extract(kernel_pmap, va);
    bool match = (extracted == pa);
    
    // Cleanup
    pmap_remove(kernel_pmap, va);
    
    return match;
}
