#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_zone.h>

/*
 * Property-based test: Allocation Symmetry for Zone Allocator
 * Prop: free_count(after alloc + free) == free_count(initial).
 */

bool prop_vm_zone_alloc_free_symmetry(vm_zone_t *zone) {
    uint32_t initial_free = zone->free_count;
    uint32_t initial_active = zone->active_items;
    
    void *item = vm_zone_alloc(zone);
    if (!item) return true; // Ignore if OOM
    
    vm_zone_free(zone, item);
    
    return (zone->free_count == initial_free && zone->active_items == initial_active);
}

void run_vm_zone_properties(void) {
    vm_zone_t *z = vm_zone_create("prop-test", 64, 64);
    prop_vm_zone_alloc_free_symmetry(z);
}
