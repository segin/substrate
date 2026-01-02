#include "vm_map.h"
#include <stddef.h>

// Placeholder for memory allocation of structures
// Since kmalloc isn't implemented, we use a static pool for bootstrap map entries
#define MAX_BOOTSTRAP_ENTRIES 64
static vm_map_entry_t bootstrap_entries[MAX_BOOTSTRAP_ENTRIES];
static int next_bootstrap_entry = 0;

static vm_map_entry_t *alloc_entry(void) {
    if (next_bootstrap_entry < MAX_BOOTSTRAP_ENTRIES) {
        return &bootstrap_entries[next_bootstrap_entry++];
    }
    return NULL;
}

void vm_map_init(vm_map_t *map, pmap_t pmap, uintptr_t min, uintptr_t max) {
    map->pmap = pmap;
    map->min_offset = min;
    map->max_offset = max;
    map->nentries = 0;
    map->size = 0;
    
    // Setup sentinel header
    static vm_map_entry_t sentinel;
    sentinel.next = &sentinel;
    sentinel.prev = &sentinel;
    sentinel.start = sentinel.end = 0;
    
    map->header = &sentinel;
}

vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max) {
    // Requires kmalloc. For now, we can't create dynamic maps easily.
    (void)pmap; (void)min; (void)max;
    return NULL; 
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
            // TODO: kfree(cur) if dynamic
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
