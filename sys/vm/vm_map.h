#ifndef _VM_MAP_H
#define _VM_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include "vm_page.h"
#include "../arch/i386/pmap.h" // Note: This should ideally be abstracted

// Forward declarations
struct vm_object;

// VM Map Entry: Represents a contiguous range of virtual addresses.
typedef struct vm_map_entry {
    struct vm_map_entry *prev;
    struct vm_map_entry *next;
    
    uintptr_t start;
    uintptr_t end;
    
    struct vm_object *object;
    uint64_t offset;
    
    uint8_t protection;
    uint8_t max_protection;
    uint8_t inheritance;
    
    // Additional flags like wire_count, etc.
} vm_map_entry_t;

// VM Map: Represents a complete virtual address space.
typedef struct vm_map {
    pmap_t pmap;            // Machine-dependent part
    vm_map_entry_t *header; // Sentinel node for the entry list
    uint32_t nentries;      // Number of entries
    size_t size;            // Total virtual size
    uintptr_t min_offset;   // Lower bound of map
    uintptr_t max_offset;   // Upper bound of map
    
    // Lock for synchronization
    // mutex_t lock; 
} vm_map_t;

// API
void vm_map_init(vm_map_t *map, pmap_t pmap, uintptr_t min, uintptr_t max);
vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max);
int vm_map_insert(vm_map_t *map, struct vm_object *obj, uint64_t offset, uintptr_t start, uintptr_t end);
int vm_map_remove(vm_map_t *map, uintptr_t start, uintptr_t end);
int vm_map_find_space(vm_map_t *map, uintptr_t *addr, size_t length);
vm_map_entry_t *vm_map_lookup(vm_map_t *map, uintptr_t va);
void vm_map_destroy(vm_map_t *map);

#endif
