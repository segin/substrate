#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

/*
 * User Memory System Call Unit Tests
 */

// Since we haven't integrated with sys_mmap properly yet due to proc structure,
// we test the underlying logic.

bool test_mmap_logic(void) {
    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0x10000000);
    
    // Simulate MAP_ANONYMOUS allocation
    uintptr_t addr;
    if (vm_map_find_space(&map, &addr, 4096) != 0) return false;
    
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 4096);
    if (!obj) return false;
    
    if (vm_map_insert(&map, obj, 0, addr, addr + 4096, 0x3, 0x3, 0x1) != 0) {
        vm_object_deallocate(obj);
        return false;
    }
    
    return (addr == 0x1000 && map.nentries == 1);
}

bool test_munmap_logic(void) {
    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0x10000000);
    
    vm_map_insert(&map, NULL, 0, 0x2000, 0x3000, 0x3, 0x3, 0x1);
    if (map.nentries != 1) return false;
    
    vm_map_remove(&map, 0x2000, 0x3000);
    if (map.nentries != 0) return false;
    
    return true;
}
