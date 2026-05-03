#include "../sys/vm/vm_map.h"
#include "../sys/vm/vm_object.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Fixed host-side implementation of vm_map.c

static vm_map_entry_t *alloc_entry(void) {
    vm_map_entry_t *entry = malloc(sizeof(vm_map_entry_t));
    if (!entry) {
        perror("malloc");
        abort();
    }
    memset(entry, 0, sizeof(vm_map_entry_t));
    return entry;
}

void vm_map_init(vm_map_t *map, pmap_t pmap, uintptr_t min, uintptr_t max) {
    map->pmap = pmap;
    map->min_offset = min;
    map->max_offset = max;
    map->nentries = 0;
    map->size = 0;
    
    // Fix: Allocate unique sentinel
    vm_map_entry_t *sentinel = alloc_entry();
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    sentinel->start = sentinel->end = min;
    map->header = sentinel;
    map->hint = sentinel;
    map->root = NULL;
    map->holes_root = NULL;
}

vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max) {
    vm_map_t *map = malloc(sizeof(vm_map_t));
    if (!map) {
        perror("malloc");
        abort();
    }
    vm_map_init(map, pmap, min, max);
    return map;
}

void vm_map_destroy(vm_map_t *map) {
    if (!map) return;

    vm_map_entry_t *header = map->header;
    if (header) {
        vm_map_entry_t *cur = header->next;
        while (cur != header) {
            vm_map_entry_t *next = cur->next;
            if (cur->object) {
                vm_object_deallocate(cur->object);
            }
            free(cur);
            cur = next;
        }
        free(header);
    }
    free(map);
}

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

int vm_map_insert(vm_map_t *map, struct vm_object *obj, uint64_t offset, uintptr_t start, uintptr_t end, uint8_t prot, uint8_t max_prot, uint8_t inheritance) {
    vm_map_entry_t *prev_entry;
    if (vm_map_lookup_entry(map, start, &prev_entry)) return -1;
    if (prev_entry->next != map->header && prev_entry->next->start < end) return -1;
    vm_map_entry_t *new_entry = alloc_entry();
    new_entry->start = start;
    new_entry->end = end;
    new_entry->object = obj;
    new_entry->offset = offset;
    new_entry->protection = prot;
    new_entry->max_protection = max_prot;
    new_entry->inheritance = inheritance;
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
    return -1;
}

int vm_map_remove(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_entry_t *cur, *tmp;
    vm_map_entry_t *header = map->header;
    for (cur = header->next; cur != header; ) {
        tmp = cur->next;
        if (cur->start >= start && cur->end <= end) {
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            map->nentries--;
            map->size -= (cur->end - cur->start);
            if (cur->object) {
                vm_object_deallocate(cur->object);
            }
            free(cur);
        }
        cur = tmp;
    }
    return 0;
}

// Mocks for linking
int pmap_is_referenced(pmap_t pmap, uintptr_t va) { (void)pmap; (void)va; return 0; }
int pmap_is_referenced_old(pmap_t pmap, uintptr_t pa) {
    (void)pmap; (void)pa;
    return 0;
}

void pmap_clear_reference(pmap_t pmap, uintptr_t va) { (void)pmap; (void)va; }
void pmap_clear_reference_old(pmap_t pmap, uintptr_t pa) {
    (void)pmap; (void)pa;
}

int syscall_trace_enabled = 0;
