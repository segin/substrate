#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/vm/vm_map.h"

/*
 * Property-based test: Invariant Check
 * Prop: Map integrity after symmetric alloc/free.
 */

bool prop_mmap_munmap_invariant(uintptr_t base, size_t size) {
    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0xFFFFFFFF);
    
    uint32_t initial_entries = map.nentries;
    size_t initial_size = map.size;
    
    // Action: Map
    if (vm_map_insert(&map, NULL, 0, base, base + size) != 0) {
        return true; // Ignore if mapping was invalid for this test
    }
    
    // Action: Unmap
    vm_map_remove(&map, base, base + size);
    
    // Invariant: State should match initial
    return (map.nentries == initial_entries && map.size == initial_size);
}

void run_mmap_properties(void) {
    // Test with various sizes/bases
    prop_mmap_munmap_invariant(0x2000, 4096);
    prop_mmap_munmap_invariant(0x5000, 8192);
    prop_mmap_munmap_invariant(0x100000, 1024*1024);
}
