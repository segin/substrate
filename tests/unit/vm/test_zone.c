#include "../../../sys/vm/vm_zone.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * Zone Allocator Unit Tests
 */

bool test_zone_create_and_alloc(void) {
    vm_zone_t *z = vm_zone_create("test-32", 32, 32);
    if (!z) return false;
    
    void *item = vm_zone_alloc(z);
    if (!item) return false;
    
    // Check if alignment is correct
    if (((uintptr_t)item % 32) != 0) {
        vm_zone_free(z, item);
        return false;
    }
    
    vm_zone_free(z, item);
    return true;
}

bool test_zone_exhaustion(void) {
    // A 4KB page should hold 128 items of 32 bytes
    vm_zone_t *z = vm_zone_create("test-exhaust", 32, 32);
    if (!z) return false;
    
    void *items[129];
    for (int i = 0; i < 129; i++) {
        items[i] = vm_zone_alloc(z);
        if (!items[i]) return false; // Should succeed as it grows
    }
    
    for (int i = 0; i < 129; i++) {
        vm_zone_free(z, items[i]);
    }
    
    return true;
}
