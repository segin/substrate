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
    
    if (vm_map_insert(&map, obj, 0, addr, addr + 4096, 0x3, 0x3, 1) != 0) {
        vm_object_deallocate(obj);
        return false;
    }
    
    return (addr == 0x1000 && map.nentries == 1);
}

bool test_munmap_logic(void) {
    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0x10000000);
    
    vm_map_insert(&map, NULL, 0, 0x2000, 0x3000, 0x3, 0x3, 1);
    if (map.nentries != 1) return false;
    
    vm_map_remove(&map, 0x2000, 0x3000);
    if (map.nentries != 0) return false;
    
    return true;
}

#include <sys/proc.h>
extern void *sys_brk(void *addr);

bool test_sys_brk_logic(void) {
    process_t dummy_proc = {0};
    dummy_proc.brk_start = 0x10000;
    dummy_proc.brk = 0; // will be initialized to brk_start by sys_brk lazily
    vm_map_t dummy_map;
    vm_map_init(&dummy_map, NULL, 0x1000, 0x10000000);
    dummy_proc.vm_map = &dummy_map;

    current_process = &dummy_proc;

    bool passed = true;

    // Test 1: Query uninitialized brk
    void *res1 = sys_brk(NULL);
    if (res1 != (void*)0x10000 || dummy_proc.brk != 0x10000) passed = false;

    // Test 2: Expand brk
    void *res2 = sys_brk((void*)0x12000);
    if (res2 != (void*)0x12000 || dummy_proc.brk != 0x12000) passed = false;

    // Test 3: Shrink brk
    void *res3 = sys_brk((void*)0x11000);
    if (res3 != (void*)0x11000 || dummy_proc.brk != 0x11000) passed = false;

    // Test 4: Below start
    void *res4 = sys_brk((void*)0x0F000);
    if (res4 != (void*)0x11000 || dummy_proc.brk != 0x11000) passed = false;

    current_process = NULL;

    return passed;
}
