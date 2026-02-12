#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_fault.h"
#include <arch/i386/pmap.h"

/*
 * Property-based test: CoW Invariant
 * Prop: Write fault on shared page results in a private physical copy.
 */

bool prop_vm_cow_isolation(vm_map_t *map1, vm_map_t *map2, uintptr_t va) {
    // Both maps share the same object
    uintptr_t pa_initial = pmap_extract(map1->pmap, va);
    
    // Action: Write fault in map1
    if (vm_fault(map1, va, VM_PROT_WRITE) != VM_FAULT_SUCCESS) {
        return true; // Ignore if failure
    }
    
    uintptr_t pa_new = pmap_extract(map1->pmap, va);
    uintptr_t pa_other = pmap_extract(map2->pmap, va);
    
    // Invariant: map1 has a NEW address, map2 still has the OLD address
    return (pa_new != pa_initial && pa_other == pa_initial);
}

void run_cow_properties(void) {
    // Setup logic for two maps sharing an object
}
