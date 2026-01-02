#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/arch/i386/pmm.h"

/*
 * Property-based test: Invariant Check for PMM
 * Prop: Allocation symmetry restores free block count.
 */

// Note: Requires access to pmm_used_blocks (extern or getter)
// For now, we mock the result check based on return values.

bool prop_pmm_alloc_free_symmetry(size_t n) {
    if (n == 0 || n > 100) return true; // Limit for unit test

    void *ptrs[100];
    
    // Action: Allocate
    for (size_t i = 0; i < n; i++) {
        ptrs[i] = pmm_alloc_block();
        if (!ptrs[i]) {
            // Free the ones we got and bail
            for (size_t j = 0; j < i; j++) pmm_free_block(ptrs[j]);
            return true; 
        }
    }
    
    // Action: Free
    for (size_t i = 0; i < n; i++) {
        pmm_free_block(ptrs[i]);
    }
    
    // Invariant: The next allocation should be able to get the first pointer back 
    // (Assuming LIFO/Bitmap behavior)
    void *p_retry = pmm_alloc_block();
    bool result = (p_retry == ptrs[n-1]);
    pmm_free_block(p_retry);
    
    return result;
}
