#include <vm/vm_map.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <sys/proc.h>
#include <sys/syscall_impl.h>

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

/* vm_map_host.c's record of the last vm_map_inherit() */
extern uintptr_t host_inherit_start;
extern uintptr_t host_inherit_end;
extern int host_inherit_value;

/*
 * The BSD minherit() records the inheritance on the range's entries
 * (vm_map_inherit(), which in the kernel splits them at its ends) for
 * vm_map_fork() to honour, and refuses a value or an address it does not
 * take.  It was a stub that returned 0 for anything and recorded nothing.
 */
bool test_vm_minherit_records(void) {
    static process_t proc;
    process_t *saved = current_process;
    vm_map_t map;
    bool ok = true;

    vm_map_init(&map, NULL, 0x1000, 0x10000);
    if (vm_map_insert(&map, NULL, 0, 0x2000, 0x6000, 0x3, 0x3, VM_INHERIT_COPY) != 0) return false;
    proc.vm_map = &map;
    current_process = &proc;
    host_inherit_value = -1;

    if (sys_minherit((void *)0x3000, 0x800, VM_INHERIT_NONE) != 0) ok = false;
    if (host_inherit_value != VM_INHERIT_NONE || host_inherit_start != 0x3000 ||
        host_inherit_end != 0x4000) ok = false;

    host_inherit_value = -1;
    if (sys_minherit((void *)0x3000, 0x1000, 7) != -EINVAL) ok = false;
    if (sys_minherit((void *)0x3001, 0x1000, VM_INHERIT_ZERO) != -EINVAL) ok = false;
    if (sys_minherit((void *)0xBFFFF000, 0x2000, VM_INHERIT_ZERO) != -EINVAL) ok = false;
    if (host_inherit_value != -1) ok = false;      /* none of those reached the map */

    current_process = saved;
    return ok;
}
