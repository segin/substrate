#include <vm/vm_fault.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <arch/i386/pmap.h>
#include <stddef.h>

#ifndef P2V
#define P2V(x) ((uintptr_t)(x) + 0xC0000000)
#endif

// Helper to copy a page (Optimized with pmap_copy_page)
static void page_copy(uintptr_t src_pa, uintptr_t dst_pa) {
    pmap_copy_page(src_pa, dst_pa);
}

// Helper to zero a page (Optimized with pmap_zero_page)
static void page_zero(uintptr_t pa) {
    pmap_zero_page(pa);
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
    if ((prot & VM_PROT_WRITE) && (entry->inheritance != VM_INHERIT_SHARE)) {
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
        
        // Decrement refcount on original shared page
        vm_page_unhold(m);
        
        m = new_m; // Use the new page
        vm_object_add_page(first_obj, m);
    }
    
    // 5. Page not present - allocate it
    if (!m) {
        // If we found nothing in the chain, allocate in top-level
        pindex = offset / 4096;
        m = vm_page_alloc(first_obj, pindex, 0);
        if (!m) return VM_FAULT_ERROR;

        if (first_obj->pager && vm_pager_has_page(first_obj->pager, pindex)) {
            vm_page_t *pages[2] = { m, NULL };
            int count = 1;

            // Prefaulting: Try to read next page too
            uint64_t next_idx = pindex + 1;
            if (vm_pager_has_page(first_obj->pager, next_idx)) {
                // Check if not resident
                if (!vm_object_lookup_page(first_obj, next_idx)) {
                     vm_page_t *m2 = vm_page_alloc(first_obj, next_idx, 0);
                     if (m2) {
                         pages[1] = m2;
                         count++;
                     }
                }
            }

            if (vm_pager_get_pages(first_obj->pager, pages, count, true) != 0) {
                // Pager failed (IO error?)
                // If double fetch failed, try just the single urgent page
                if (count > 1) {
                    vm_page_free(pages[1]);
                    count = 1;
                    if (vm_pager_get_pages(first_obj->pager, &m, 1, true) != 0) {
                        vm_page_free(m);
                        return VM_FAULT_ERROR;
                    }
                } else {
                    vm_page_free(m);
                    return VM_FAULT_ERROR;
                }
            }
            
            m->flags |= PG_VALID;
            // Add prefaulted page if successful
            if (count > 1 && pages[1]) {
                pages[1]->flags |= PG_VALID;
                vm_object_add_page(first_obj, pages[1]);
                vm_page_deactivate(pages[1]); // Move to inactive queue immediately (heuristically)
            }
        } else if (first_obj->type == VM_OBJ_TYPE_DEFAULT) {
            page_zero(m->phys_addr);
            m->flags |= PG_ZERO | PG_VALID;
        } else {
            // Unhandled object type or missing page in file
            // For VNode objects, this usually means zero-fill extended region
            page_zero(m->phys_addr);
            m->flags |= PG_ZERO | PG_VALID;
        }
        vm_object_add_page(first_obj, m);
    }
    
    // 6. Enter mapping
    // If it's a COW mapping (read-only view of shared page), reduce permissions
    // If it's a COW mapping (read-only view of shared page), reduce permissions
    uint8_t enter_prot = entry->protection;
    if ((prot & VM_PROT_WRITE) == 0 && (obj->ref_count > 1) && (entry->inheritance != VM_INHERIT_SHARE)) {
       enter_prot &= ~VM_PROT_WRITE; // Force Read-Only for COW
    }

    int err = pmap_enter(map->pmap, page_va, m->phys_addr, enter_prot, 0);
    if (err < 0) return VM_FAULT_ERROR;

    return VM_FAULT_SUCCESS;
}
