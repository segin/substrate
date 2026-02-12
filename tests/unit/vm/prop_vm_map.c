#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_map.h>

/*
 * Property-based test: Order and Overlap Invariant for VM Map
 * Prop: All entries in the map are sorted by address and do not overlap.
 */

bool prop_vm_map_integrity(vm_map_t *map) {
    vm_map_entry_t *header = map->header;
    vm_map_entry_t *cur;
    
    uintptr_t last_end = map->min_offset;
    
    for (cur = header->next; cur != header; cur = cur->next) {
        // Sorted Check
        if (cur->start < last_end) return false;
        
        // Sanity Check
        if (cur->end <= cur->start) return false;
        
        last_end = cur->end;
    }
    
    if (last_end > map->max_offset) return false;
    
    return true;
}

void run_vm_map_properties(void) {
    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0xFFFFFFFF);
    
    // Add some random entries and check
    vm_map_insert(&map, NULL, 0, 0x2000, 0x3000);
    vm_map_insert(&map, NULL, 0, 0x5000, 0x6000);
    
    prop_vm_map_integrity(&map);
}
