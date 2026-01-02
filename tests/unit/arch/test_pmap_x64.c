#include "../../../sys/arch/x86_64/pmap.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * PMAP (x86_64) Unit Tests
 */

bool test_pmap_x64_enter_extract(void) {
    pmap_t kernel_pmap = pmap_kernel();
    if (!kernel_pmap) return false;
    
    uint64_t va = 0x2000000000ULL; // Large 64-bit VA
    uint64_t pa = 0x1000000ULL;
    
    // Map
    if (pmap_enter(kernel_pmap, va, pa, 0, 0) != 0) {
        return false;
    }
    
    // Extract
    uint64_t extracted = pmap_extract(kernel_pmap, va);
    if (extracted != pa) return false;
    
    // Cleanup
    pmap_remove(kernel_pmap, va);
    if (pmap_extract(kernel_pmap, va) != 0) return false;
    
    return true;
}

bool test_pmap_x64_alignment(void) {
    pmap_t kernel_pmap = pmap_kernel();
    // 0x123 is not aligned, pmap_extract should return aligned PA
    uint64_t extracted = pmap_extract(kernel_pmap, 0x1000123);
    return ((extracted & 0xFFF) == 0x123 || extracted == 0);
}
