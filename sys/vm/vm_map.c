#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <stddef.h>

#include <vm/vm_kmem.h>

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
        sentinel->start = sentinel->end = min;
        sentinel->object = NULL;
    }
    
    map->header = sentinel;
    map->hint = sentinel;
    map->root = NULL;
}

vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max) {
    vm_map_t *map = kmalloc(sizeof(vm_map_t));
    if (!map) return NULL;
    vm_map_init(map, pmap, min, max);
    if (!map->header) {
        kfree(map, sizeof(vm_map_t));
        return NULL;
    }
    // Initialize hint to header to match vm_map_init logic
    map->hint = map->header;
    return map;
}

static void vm_map_splay(vm_map_t *map, uintptr_t va) {
    vm_map_entry_t *root = map->root;
    if (!root) return;

    struct vm_map_entry N, *l, *r, *y;
    N.left = N.right = NULL;
    l = r = &N;

    for (;;) {
        if (va < root->start) {
            if (!root->left) break;
            if (va < root->left->start) {
                y = root->left;
                root->left = y->right;
                y->right = root;
                root = y;
                if (!root->left) break;
            }
            r->left = root;
            r = root;
            root = root->left;
        } else if (va >= root->end) {
            if (!root->right) break;
            if (va >= root->right->end) {
                y = root->right;
                root->right = y->left;
                y->left = root;
                root = y;
                if (!root->right) break;
            }
            l->right = root;
            l = root;
            root = root->right;
        } else {
            break;
        }
    }
    l->right = root->left;
    r->left = root->right;
    root->left = N.right;
    root->right = N.left;
    map->root = root;
}

// Internal helper: find the entry containing VA, or the entry immediately preceding it.
static bool vm_map_lookup_entry(vm_map_t *map, uintptr_t va, vm_map_entry_t **entry) {
    // Optimization: Check hint first
    vm_map_entry_t *hint = map->hint;
    if (hint && hint != map->header && va >= hint->start && va < hint->end) {
        *entry = hint;
        return true;
    }

    // Optimization: Check root
    vm_map_entry_t *root = map->root;
    if (root && va >= root->start && va < root->end) {
        *entry = root;
        map->hint = root;
        return true;
    }

    vm_map_splay(map, va);
    root = map->root;

    if (!root) {
        *entry = map->header; // Effectively header->prev since header is empty/sentinel
        return false;
    }

    if (va >= root->start && va < root->end) {
        *entry = root;
        map->hint = root;
        return true;
    }

    if (va < root->start) {
        *entry = root->prev;
    } else {
        *entry = root;
    }
    return false;
}

int vm_map_insert(vm_map_t *map, struct vm_object *obj, uint64_t offset, uintptr_t start, uintptr_t end, uint8_t prot, uint8_t max_prot, uint8_t inheritance) {
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
    
    // Initialize protection and inheritance
    new_entry->protection = prot;
    new_entry->max_protection = max_prot;
    new_entry->inheritance = inheritance;
    new_entry->flags = 0;
    new_entry->wire_count = 0;
    
    // Insert into sorted list
    new_entry->prev = prev_entry;
    new_entry->next = prev_entry->next;
    prev_entry->next->prev = new_entry;
    prev_entry->next = new_entry;
    
    map->hint = new_entry;

    // Insert into splay tree
    if (!map->root) {
        map->root = new_entry;
        new_entry->left = new_entry->right = NULL;
    } else {
        vm_map_splay(map, start);
        if (start < map->root->start) {
            new_entry->left = map->root->left;
            new_entry->right = map->root;
            map->root->left = NULL;
            map->root = new_entry;
        } else {
            new_entry->right = map->root->right;
            new_entry->left = map->root;
            map->root->right = NULL;
            map->root = new_entry;
        }
    }

    map->nentries++;
    map->size += (end - start);
    return 0;
}

int vm_map_find_space(vm_map_t *map, uintptr_t *addr, size_t length) {
    vm_map_entry_t *header = map->header;
    vm_map_entry_t *hint = map->hint;
    vm_map_entry_t *cur;
    
    // Default to header if hint is invalid/null
    if (!hint) hint = header;

    uintptr_t start;

    // First pass: From hint to end of list
    if (hint == header) {
        cur = header->next;
        start = map->min_offset;
    } else {
        cur = hint->next;
        start = hint->end;
    }

    // Optimization: Unroll common sequential scan
    // We scan from cur to header
    while (cur != header) {
        if (cur->start >= start && cur->start - start >= length) {
            *addr = start;
            return 0;
        }
        start = cur->end;
        cur = cur->next;
        if (cur == header) break;

        if (cur->start >= start && cur->start - start >= length) {
            *addr = start;
            return 0;
        }
        start = cur->end;
        cur = cur->next;
        if (cur == header) break;

        if (cur->start >= start && cur->start - start >= length) {
            *addr = start;
            return 0;
        }
        start = cur->end;
        cur = cur->next;
        if (cur == header) break;

        if (cur->start >= start && cur->start - start >= length) {
            *addr = start;
            return 0;
        }
        start = cur->end;
        cur = cur->next;
    }

    // Check tail gap (gap between last entry and max_offset)
    if (map->max_offset >= start && map->max_offset - start >= length) {
        *addr = start;
        return 0;
    }

    // Second pass: From start of list to hint (if we started mid-list)
    if (hint != header) {
        cur = header->next;
        start = map->min_offset;

        while (cur != hint->next) {
            if (cur->start >= start && cur->start - start >= length) {
                *addr = start;
                return 0;
            }
            start = cur->end;
            cur = cur->next;
        }
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
            if (map->hint == cur) map->hint = cur->prev;
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;

            // Remove from splay tree
            vm_map_splay(map, cur->start);
            if (map->root == cur) {
                vm_map_entry_t *left = cur->left;
                vm_map_entry_t *right = cur->right;
                if (!left) {
                    map->root = right;
                } else {
                    map->root = left;
                    vm_map_splay(map, cur->start); // Splay max of left to root
                    map->root->right = right;
                }
            }

            map->nentries--;
            map->size -= (cur->end - cur->start);
            if (cur->object)
                vm_object_deallocate(cur->object);
            free_entry(cur);
        } else if (cur->start < start && cur->end > end) {
            // Split entry
            vm_map_entry_t *new_entry = alloc_entry();
            if (!new_entry) return -1;

            // Initialize new entry (right part)
            new_entry->start = end;
            new_entry->end = cur->end;
            new_entry->object = cur->object;
            new_entry->offset = cur->offset + (end - cur->start);

            // Reference object if present
            if (new_entry->object) {
                vm_object_reference(new_entry->object);
            }

            // Copy properties
            new_entry->protection = cur->protection;
            new_entry->max_protection = cur->max_protection;
            new_entry->inheritance = cur->inheritance;
            new_entry->flags = cur->flags;
            new_entry->wire_count = cur->wire_count;

            // Insert into list
            new_entry->prev = cur;
            new_entry->next = cur->next;
            cur->next->prev = new_entry;
            cur->next = new_entry;

            // Insert into splay tree
            vm_map_splay(map, new_entry->start);
            if (!map->root) { // Should not happen as cur is in tree
                map->root = new_entry;
                new_entry->left = new_entry->right = NULL;
            } else {
                if (new_entry->start < map->root->start) {
                    new_entry->left = map->root->left;
                    new_entry->right = map->root;
                    map->root->left = NULL;
                    map->root = new_entry;
                } else {
                    new_entry->right = map->root->right;
                    new_entry->left = map->root;
                    map->root->right = NULL;
                    map->root = new_entry;
                }
            }

            // Adjust original entry (left part)
            cur->end = start;

            // Adjust map stats
            map->nentries++;
            map->size -= (end - start);

            // Continue loop (next iteration will see new_entry, but its start (end) >= end,
            // so it won't be processed again for this range)

        } else if (cur->start < end && cur->end > start) {
            // Partial overlap (trimming)
            if (cur->start < start) {
                map->size -= (cur->end - start);
                cur->end = start;
            } else {
                map->size -= (end - cur->start);
                cur->offset += (end - cur->start);
                cur->start = end;
            }
        }
        
        cur = tmp;
    }
    return 0;
}

// Lookup entry containing the given virtual address
vm_map_entry_t *vm_map_lookup(vm_map_t *map, uintptr_t va) {
    vm_map_entry_t *entry;
    if (vm_map_lookup_entry(map, va, &entry)) {
        return entry;
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

// Fork a vm_map (duplicate entries for child process)
vm_map_t *vm_map_fork(vm_map_t *src_map, pmap_t dst_pmap) {
    vm_map_t *dst_map = vm_map_create(dst_pmap, src_map->min_offset, src_map->max_offset);
    if (!dst_map) return NULL;
    
    vm_map_entry_t *src_entry;
    vm_map_entry_t *header = src_map->header;
    
    for (src_entry = header->next; src_entry != header; src_entry = src_entry->next) {
        vm_object_t *obj = src_entry->object;
        
        // Determine inheritance behavior
        if (src_entry->inheritance == VM_INHERIT_NONE)
            continue;
            
        vm_object_t *new_obj = NULL;
        uint64_t new_offset = src_entry->offset;
        
        if (src_entry->inheritance == VM_INHERIT_SHARE) {
            // Shared mapping
            new_obj = obj;
            if (new_obj) vm_object_reference(new_obj);
            
        } else if (src_entry->inheritance == VM_INHERIT_COPY) {
            // Copy-on-Write
            if (obj) {
                obj->flags |= VM_OBJ_COPY;
                new_obj = obj;
                vm_object_reference(new_obj);
                
                // Downgrade protection in parent to ensure COW trap happens
                pmap_protect(src_map->pmap, src_entry->start, src_entry->end, src_entry->protection & ~VM_PROT_WRITE);
            }
        } else if (src_entry->inheritance == VM_INHERIT_ZERO) {
            // Child gets new zero-filled object
            new_obj = NULL; // Lazy allocation
        }
        
        if (vm_map_insert(dst_map, new_obj, new_offset, src_entry->start, src_entry->end, src_entry->protection, src_entry->max_protection, src_entry->inheritance) != 0) {
            vm_map_destroy(dst_map);
            return NULL;
        }
        
        // Copy flags (excluding wired state)
        vm_map_entry_t *dst_entry = vm_map_lookup(dst_map, src_entry->start);
        if (dst_entry) {
            dst_entry->flags = src_entry->flags & ~VME_WIRED; 
        }
    }
    
    return dst_map;
}
