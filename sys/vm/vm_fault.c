#include <vm/vm_fault.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <arch/i386/pmap.h>
#include <stddef.h>

typedef struct vm_fault_source {
    vm_object_t *object;
    uint64_t pindex;
    vm_page_t *page;
} vm_fault_source_t;

static vm_fault_source_t vm_fault_resolve_source(vm_object_t *first_obj, uint64_t base_pindex) {
    vm_fault_source_t source = {0};
    vm_object_t *obj = first_obj;
    uint64_t pindex = base_pindex;

    while (obj) {
        vm_page_t *page = vm_object_lookup_page(obj, pindex);
        if (page) {
            source.object = obj;
            source.pindex = pindex;
            source.page = page;
            return source;
        }

        if (!source.object && obj->pager && vm_pager_has_page(obj->pager, pindex)) {
            source.object = obj;
            source.pindex = pindex;
        }

        if (!obj->shadow) {
            break;
        }

        pindex += obj->shadow_offset / 4096;
        obj = obj->shadow;
    }

    return source;
}

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
    int result = VM_FAULT_ERROR;
    vm_map_entry_t *entry = NULL;
    
    // 1. Find the map entry
    vm_map_lock_read(map);
    for (vm_map_entry_t *cur = map->header->next; cur != map->header; cur = cur->next) {
        if (va >= cur->start && va < cur->end) {
            entry = cur;
            break;
        }
        if (va < cur->start) {
            break;
        }
    }
    if (!entry) {
        goto out;
    }

    // 2. Check protection
    if ((entry->max_protection & prot) != prot) {
        goto out;
    }
    if ((entry->protection & prot) != prot) {
        if ((prot & VM_PROT_WRITE) == 0 || entry->inheritance == VM_INHERIT_SHARE) {
            goto out;
        }
    }

    // 3. Resolve page against the object chain
    vm_object_t *first_obj = entry->object;
    if (!first_obj) {
        goto out;
    }
    
    vm_object_t *obj = first_obj;
    vm_page_t *m = NULL;
    uint64_t offset = (page_va - entry->start) + entry->offset;
    uint64_t pindex = offset / 4096;
    vm_fault_source_t source = vm_fault_resolve_source(first_obj, pindex);
    obj = source.object ? source.object : first_obj;
    m = source.page;

    // 4. Page not resident yet - page it in or zero-fill it.
    if (!m) {
        vm_object_t *fill_obj = source.object ? source.object : first_obj;
        uint64_t fill_pindex = source.object ? source.pindex : pindex;

        m = vm_page_alloc(fill_obj, fill_pindex, 0);
        if (!m) {
            goto out;
        }

        if (fill_obj->pager && vm_pager_has_page(fill_obj->pager, fill_pindex)) {
            vm_page_t *pages[2] = { m, NULL };
            int count = 1;

            // Prefaulting: Try to read next page too
            uint64_t next_idx = fill_pindex + 1;
            if (vm_pager_has_page(fill_obj->pager, next_idx)) {
                // Check if not resident
                if (!vm_object_lookup_page(fill_obj, next_idx)) {
                     vm_page_t *m2 = vm_page_alloc(fill_obj, next_idx, 0);
                     if (m2) {
                         pages[1] = m2;
                         count++;
                     }
                }
            }

            if (vm_pager_get_pages(fill_obj->pager, pages, count, true) != 0) {
                // Pager failed (IO error?)
                // If double fetch failed, try just the single urgent page
                if (count > 1) {
                    vm_page_free(pages[1]);
                    count = 1;
                    if (vm_pager_get_pages(fill_obj->pager, &m, 1, true) != 0) {
                        vm_page_free(m);
                        goto out;
                    }
                } else {
                    vm_page_free(m);
                    goto out;
                }
            }
            
            m->flags |= PG_VALID;
            // Add prefaulted page if successful
            if (count > 1 && pages[1]) {
                pages[1]->flags |= PG_VALID;
                vm_object_add_page(fill_obj, pages[1]);
                vm_page_deactivate(pages[1]); // Move to inactive queue immediately (heuristically)
            }
        } else if (fill_obj->type == VM_OBJ_TYPE_DEFAULT) {
            page_zero(m->phys_addr);
            m->flags |= PG_ZERO | PG_VALID;
        } else {
            // Unhandled object type or missing page in file
            // For VNode objects, this usually means zero-fill extended region
            page_zero(m->phys_addr);
            m->flags |= PG_ZERO | PG_VALID;
        }
        vm_object_add_page(fill_obj, m);
        obj = fill_obj;
    }

    // 5. Handle Copy-on-Write faults after we have a source page.
    if ((prot & VM_PROT_WRITE) && (entry->inheritance != VM_INHERIT_SHARE) &&
        (obj != first_obj || obj->ref_count > 1)) {
        vm_page_t *new_m = vm_page_alloc(first_obj, offset / 4096, 0);
        if (!new_m) {
            goto out;
        }

        page_copy(m->phys_addr, new_m->phys_addr);
        new_m->flags |= PG_VALID | PG_DIRTY;
        vm_object_add_page(first_obj, new_m);

        m = new_m;
        obj = first_obj;
    }

    // 6. Enter mapping
    uint8_t enter_prot = entry->protection;
    if ((prot & VM_PROT_WRITE) && (entry->max_protection & VM_PROT_WRITE) &&
        (entry->inheritance != VM_INHERIT_SHARE)) {
        enter_prot |= VM_PROT_WRITE;
    }
    if ((entry->inheritance != VM_INHERIT_SHARE) &&
        ((obj != first_obj) || ((prot & VM_PROT_WRITE) == 0 && obj->ref_count > 1))) {
        enter_prot &= ~VM_PROT_WRITE;
    }

    int err = pmap_enter(map->pmap, page_va, m->phys_addr, enter_prot, 0);
    if (err < 0) {
        goto out;
    }

    result = VM_FAULT_SUCCESS;
out:
    vm_map_unlock_read(map);
    return result;
}
