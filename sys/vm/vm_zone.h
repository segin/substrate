#ifndef _VM_ZONE_H
#define _VM_ZONE_H

#include <stdint.h>
#include <stddef.h>

// VM Zone: A collection of fixed-size objects.
typedef struct vm_zone {
    const char *name;
    size_t size;            // Object size
    size_t align;           // Alignment
    
    struct vm_zone_item *free_list;
    uint32_t free_count;
    
    // Statistics
    uint32_t total_items;
    uint32_t active_items;

    // Mutex for synchronization
    // mutex_t lock;
} vm_zone_t;

// API
void vm_zone_init(void);
vm_zone_t *vm_zone_create(const char *name, size_t size, size_t align);
void *vm_zone_alloc(vm_zone_t *zone);
void vm_zone_free(vm_zone_t *zone, void *item);

#endif
