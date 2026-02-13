#include <arch/i386/pmap.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * PMAP (i386) Unit Tests
 */

bool test_pmap_i386_enter_extract(void) {
    pmap_t kernel_pmap = pmap_kernel();
    if (!kernel_pmap) return false;
    
    uintptr_t va = 0x2000000; // 32MB
    uintptr_t pa = 0x1000000; // 16MB
    
    // Map
    if (pmap_enter(kernel_pmap, va, pa, VM_PROT_READ|VM_PROT_WRITE, 0) != 0) {
        return false;
    }
    
    // Extract
    uintptr_t extracted = pmap_extract(kernel_pmap, va);
    if (extracted != pa) return false;
    
    // Cleanup
    pmap_remove(kernel_pmap, va);
    if (pmap_extract(kernel_pmap, va) != 0) return false;
    
    return true;
}

bool test_pmap_i386_invalid_va(void) {
    pmap_t kernel_pmap = pmap_kernel();
    uintptr_t extracted = pmap_extract(kernel_pmap, 0xDEADC000);
    return (extracted == 0);
}
