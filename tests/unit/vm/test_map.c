#include <vm/vm_map.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * VM Map Unit Tests
 */

bool test_vm_map_init(void) {
    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0xFFFF0000);
    
    if (map.min_offset != 0x1000) return false;
    if (map.header->next != map.header) return false;
    if (map.nentries != 0) return false;
    
    return true;
}

bool test_vm_map_insert_and_find(void) {
    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0x10000);
    
    // 1. Insert at 0x2000
    if (vm_map_insert(&map, NULL, 0, 0x2000, 0x3000, 0x3, 0x3, 1) != 0) return false;
    if (map.nentries != 1) return false;
    
    // 2. Find space for 4KB (should find 0x1000)
    uintptr_t addr;
    if (vm_map_find_space(&map, &addr, 4096) != 0) return false;
    if (addr != 0x1000) return false;
    
    // 3. Find space for 8KB (should find 0x3000)
    if (vm_map_find_space(&map, &addr, 8192) != 0) return false;
    if (addr != 0x3000) return false;
    
    return true;
}

bool test_vm_map_remove(void) {
    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0x10000);
    
    vm_map_insert(&map, NULL, 0, 0x2000, 0x3000, 0x3, 0x3, 1);
    vm_map_insert(&map, NULL, 0, 0x4000, 0x5000, 0x3, 0x3, 1);
    if (map.nentries != 2) return false;
    
    // Remove 0x4000-0x5000 (entire entry)
    vm_map_remove(&map, 0x4000, 0x5000);
    if (map.nentries != 1) return false;
    
    // Remove 0x2000-0x3000 (entire entry)
    vm_map_remove(&map, 0x2000, 0x3000);
    if (map.nentries != 0) return false;
    
    return true;
}
