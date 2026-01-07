#include "vm_map.h"
#include "vm_object.h"
#include <stddef.h>

#include "vm_kmem.h"

static vm_map_entry_t *alloc_entry(void) {
    return kmalloc(sizeof(vm_map_entry_t));
}

static void free_entry(vm_map_entry_t *entry) {
    kfree(entry, sizeof(vm_map_entry_t));
}

void vm_map_init(vm_map_t *map, pmap_t pmap, uintptr_t min, uintptr_t max) {
    map->pmap = pmap;
    map->min_offset = min;
    map->max_offset = max;
    map->nentries = 0;
    map->size = 0;
    
    // Setup sentinel header
    vm_map_entry_t *sentinel = alloc_entry();
    if (sentinel) {
        sentinel->next = sentinel;
        sentinel->prev = sentinel;
        sentinel->start = sentinel->end = 0;
        sentinel->object = NULL;
    }
    
    map->header = sentinel;
}

vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max) {
    vm_map_t *map = kmalloc(sizeof(vm_map_t));
    if (!map) return NULL;
    vm_map_init(map, pmap, min, max);
    if (!map->header) {
        kfree(map, sizeof(vm_map_t));
        return NULL;
    }
    return map;
}

// Internal helper: find the entry containing VA, or the entry immediately preceding it.
static bool vm_map_lookup_entry(vm_map_t *map, uintptr_t va, vm_map_entry_t **entry) {
    vm_map_entry_t *cur;
    vm_map_entry_t *header = map->header;

    for (cur = header->next; cur != header; cur = cur->next) {
        if (va >= cur->start && va < cur->end) {
            *entry = cur;
            return true;
        }
        if (va < cur->start) {
            *entry = cur->prev;
            return false;
        }
    }
    
    *entry = header->prev;
    return false;
}

int vm_map_insert(vm_map_t *map, struct vm_object *obj, uint64_t offset, uintptr_t start, uintptr_t end) {
    vm_map_entry_t *prev_entry;
    
    // Check for overlap
    if (vm_map_lookup_entry(map, start, &prev_entry)) {
        return -1; // Overlaps with existing entry
    }
    
    // Ensure the entire range is free
    if (prev_entry->next != map->header && prev_entry->next->start < end) {
        return -1; // Overlaps with next entry
    }

    vm_map_entry_t *new_entry = alloc_entry();
    if (!new_entry) return -1;
    
    new_entry->start = start;
    new_entry->end = end;
    new_entry->object = obj;
    new_entry->offset = offset;
    
    // Insert into sorted list
    new_entry->prev = prev_entry;
    new_entry->next = prev_entry->next;
    prev_entry->next->prev = new_entry;
    prev_entry->next = new_entry;
    
    map->nentries++;
    map->size += (end - start);
    return 0;
}

int vm_map_find_space(vm_map_t *map, uintptr_t *addr, size_t length) {
    vm_map_entry_t *cur;
    vm_map_entry_t *header = map->header;
    
    uintptr_t start = map->min_offset;
    
    for (cur = header->next; ; cur = cur->next) {
        if (cur == header || cur->start > start) {
            uintptr_t next_start = (cur == header) ? map->max_offset : cur->start;
            if (next_start - start >= length) {
                *addr = start;
                return 0;
            }
        }
        if (cur == header) break;
        start = cur->end;
    }
    
    return -1; // No space found
}

int vm_map_remove(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_entry_t *cur, *tmp;
    vm_map_entry_t *header = map->header;
    
    for (cur = header->next; cur != header; ) {
        tmp = cur->next;
        
        if (cur->start >= start && cur->end <= end) {
            // Entirely within range, remove it
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            map->nentries--;
            map->size -= (cur->end - cur->start);
            free_entry(cur);
        } else if (cur->start < start && cur->end > end) {
            // Split entry (not implemented yet)
            return -1;
        } else if (cur->start < end && cur->end > start) {
            // Partial overlap (trimming)
            if (cur->start < start) {
                map->size -= (cur->end - start);
                cur->end = start;
            } else {
                map->size -= (end - cur->start);
                cur->start = end;
            }
        }
        
        cur = tmp;
    }
    return 0;
}

// Lookup entry containing the given virtual address
vm_map_entry_t *vm_map_lookup(vm_map_t *map, uintptr_t va) {
    vm_map_entry_t *header = map->header;
    vm_map_entry_t *cur;
    
    for (cur = header->next; cur != header; cur = cur->next) {
        if (va >= cur->start && va < cur->end) {
            return cur;
        }
    }
    return NULL;
}

// Destroy a vm_map and free all its entries
void vm_map_destroy(vm_map_t *map) {
    if (!map) return;
    
    vm_map_entry_t *header = map->header;
    if (header) {
        vm_map_entry_t *cur = header->next;
        while (cur != header) {
            vm_map_entry_t *next = cur->next;
            // Dereference the object if present
            if (cur->object) {
                vm_object_deallocate(cur->object);
            }
            free_entry(cur);
            cur = next;
        }
        free_entry(header);
    }
    
    // Destroy the associated pmap
    if (map->pmap) {
        pmap_destroy(map->pmap);
    }
    
    kfree(map, sizeof(vm_map_t));
}

// Change protection on a range of addresses
int vm_map_protect(vm_map_t *map, uintptr_t start, uintptr_t end, uint8_t prot) {
    vm_map_entry_t *header = map->header;
    
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end)
            break;
        if (cur->end <= start)
            continue;
            
        // Check if requested protection exceeds max
        if ((prot & ~cur->max_protection) != 0)
            return -1;
            
        cur->protection = prot;
        
        // Update pmap for overlapping range
        uintptr_t rs = (cur->start > start) ? cur->start : start;
        uintptr_t re = (cur->end < end) ? cur->end : end;
        pmap_protect(map->pmap, rs, re, prot);
    }
    return 0;
}

// Wire pages in a range (make unpageable)
int vm_map_wire(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_entry_t *header = map->header;
    
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end)
            break;
        if (cur->end <= start)
            continue;
            
        cur->wire_count++;
        cur->flags |= VME_WIRED;
    }
    return 0;
}

// Unwire pages in a range
int vm_map_unwire(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_entry_t *header = map->header;
    
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end)
            break;
        if (cur->end <= start)
            continue;
            
        if (cur->wire_count > 0) {
            cur->wire_count--;
            if (cur->wire_count == 0)
                cur->flags &= ~VME_WIRED;
        }
    }
    return 0;
}
