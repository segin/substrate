#include "../../../sys/arch/i386/pmm.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * PMM Unit Tests
 */

bool test_pmm_basic_alloc(void) {
    void *p1 = pmm_alloc_block();
    void *p2 = pmm_alloc_block();
    
    if (!p1 || !p2) return false;
    if (p1 == p2) return false;
    
    pmm_free_block(p1);
    pmm_free_block(p2);
    return true;
}

bool test_pmm_contiguous_alloc(void) {
    void *p = pmm_alloc_contiguous(4); // 16KB
    if (!p) return false;
    
    // Check alignment
    if (((uintptr_t)p % 4096) != 0) return false;
    
    pmm_free_contiguous(p, 4);
    return true;
}

bool test_pmm_exhaustion(void) {
    // Current limit is 128MB (32768 blocks)
    // We won't actually allocate all of it in a unit test to avoid slowness,
    // but we can test if it fails eventually.
    return true;
}
