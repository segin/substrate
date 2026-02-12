#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_fault.h"
#include <arch/i386/pmap.h"

/*
 * Property-based test: Post-condition Invariant for Fault Handler
 * Prop: Successful fault results in a valid hardware mapping.
 */

bool prop_vm_fault_mapping_invariant(vm_map_t *map, uintptr_t va) {
    // Action: Fault
    if (vm_fault(map, va, VM_PROT_READ) != VM_FAULT_SUCCESS) {
        return true; // Ignore if fault could not be resolved (e.g., no entry)
    }
    
    // Invariant: Mapping must exist in pmap
    uintptr_t pa = pmap_extract(map->pmap, va);
    return (pa != 0);
}

void run_vm_fault_properties(void) {
    vm_map_t map;
    vm_map_init(&map, pmap_kernel(), 0x1000, 0x100000);
    prop_vm_fault_mapping_invariant(&map, 0x5000);
}
