#include "vm_fault.h"
#include "vm_object.h"
#include "vm_page.h"
#include <stddef.h>

int vm_fault(vm_map_t *map, uintptr_t va, uint8_t prot) {
    vm_map_entry_t *entry = NULL;
    uintptr_t page_va = va & ~0xFFF; // Align to page boundary

    // 1. Find the map entry covering this VA
    // (In a real implementation, we'd have a search function)
    vm_map_entry_t *header = map->header;
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (va >= cur->start && va < cur->end) {
            entry = cur;
            break;
        }
    }

    if (!entry) return VM_FAULT_ERROR;

    // 2. Check protection
    if ((entry->protection & prot) != prot) {
        return VM_FAULT_ERROR; // Protection violation
    }

    // 3. Resolve page against the object
    vm_object_t *obj = entry->object;
    if (!obj) return VM_FAULT_ERROR;

    uint64_t pindex = (uint64_t)(page_va - entry->start + entry->offset) / 4096;

    // 4. Lookup existing page
    vm_page_t *m = vm_object_lookup_page(obj, pindex);

    if (!m) {
        // 5. Page not present, allocate it
        m = vm_page_alloc(obj, pindex, 0);
        if (!m) return VM_FAULT_ERROR; // OOM or pager error

        // If it's a default object, zero it
        if (obj->type == VM_OBJ_TYPE_DEFAULT) {
            // TODO: pmap_zero_page(m->phys_addr);
            m->flags |= PG_ZERO | PG_VALID;
        }
        
        vm_object_add_page(obj, m);
    }

    // 6. Enter the mapping into the PMAP (hardware tables)
    // We pass VM_PROT flags to pmap_enter.
    int err = pmap_enter(map->pmap, page_va, m->phys_addr, entry->protection, 0);
    
    if (err < 0) return VM_FAULT_ERROR;

    return VM_FAULT_SUCCESS;
}
