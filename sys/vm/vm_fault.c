#include "vm_fault.h"
#include "vm_object.h"
#include "vm_page.h"
#include "../arch/i386/pmap.h"
#include <stddef.h>

#define P2V(x) ((uintptr_t)(x) + 0xC0000000)

// Helper to copy a page (FIXME: Optimize with pmap_copy_page)
static void page_copy(uintptr_t src_pa, uintptr_t dst_pa) {
    uint32_t *src = (uint32_t *)P2V(src_pa);
    uint32_t *dst = (uint32_t *)P2V(dst_pa);
    for (int i = 0; i < 1024; i++) dst[i] = src[i];
}

// Helper to zero a page (FIXME: Optimize with pmap_zero_page)
static void page_zero(uintptr_t pa) {
    uint32_t *p = (uint32_t *)P2V(pa);
    for (int i = 0; i < 1024; i++) p[i] = 0;
}

int vm_fault(vm_map_t *map, uintptr_t va, uint8_t prot) {
    uintptr_t page_va = va & ~0xFFF;
    
    // 1. Find the map entry
    vm_map_entry_t *entry = vm_map_lookup(map, va);
    if (!entry) return VM_FAULT_ERROR;

    // 2. Check protection
    if ((entry->protection & prot) != prot)
        return VM_FAULT_ERROR;

    // 3. Resolve page against the object chain
    vm_object_t *first_obj = entry->object;
    if (!first_obj) return VM_FAULT_ERROR;
    
    vm_object_t *obj = first_obj;
    vm_page_t *m = NULL;
    uint64_t offset = (page_va - entry->start) + entry->offset;
    uint64_t pindex = offset / 4096;
    
    // Walk shadow chain
    while (obj) {
        m = vm_object_lookup_page(obj, pindex);
        if (m) break;
        
        // Move to backing object
        if (obj->shadow) {
            pindex += obj->shadow_offset / 4096; // Adjust index for shadow
            obj = obj->shadow;
        } else {
            break; // No more objects
        }
    }
    
    // 4. Handle Copy-on-Write (COW) fault
    // If we want to WRITE, and the page is in a shared object (ref_count > 1) 
    // OR it's not in the top-level object, we need to copy it.
    bool needs_copy = false;
    if (prot & VM_PROT_WRITE) {
        if (m && (obj != first_obj || obj->ref_count > 1)) {
            needs_copy = true;
        } else if (!m && first_obj->ref_count > 1) {
            // Allocate new page in first_obj, potentially zero-filled if no backing
            // If we fall through to allocation, ensure it's private
        }
    }

    if (m && needs_copy) {
        // Allocate new page in top-level object
        vm_page_t *new_m = vm_page_alloc(first_obj, (offset / 4096), 0);
        if (!new_m) return VM_FAULT_ERROR;
        
        page_copy(m->phys_addr, new_m->phys_addr);
        new_m->flags |= PG_VALID | PG_DIRTY;
        
        m = new_m; // Use the new page
        vm_object_add_page(first_obj, m);
    }
    
    // 5. Page not present - allocate it
    if (!m) {
        // If we found nothing in the chain, allocate in top-level
        pindex = offset / 4096;
        m = vm_page_alloc(first_obj, pindex, 0);
        if (!m) return VM_FAULT_ERROR;

        if (first_obj->type == VM_OBJ_TYPE_DEFAULT) {
            page_zero(m->phys_addr);
            m->flags |= PG_ZERO | PG_VALID;
        } else {
            // TODO: Call pager
        }
        vm_object_add_page(first_obj, m);
    }
    
    // 6. Enter mapping
    // If it's a COW mapping (read-only view of shared page), reduce permissions
    uint8_t enter_prot = entry->protection;
    if ((prot & VM_PROT_WRITE) == 0 && (obj->ref_count > 1)) {
       enter_prot &= ~VM_PROT_WRITE; // Force Read-Only for COW
    }

    int err = pmap_enter(map->pmap, page_va, m->phys_addr, enter_prot, 0);
    if (err < 0) return VM_FAULT_ERROR;

    return VM_FAULT_SUCCESS;
}
