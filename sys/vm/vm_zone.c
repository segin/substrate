#include "vm_zone.h"
#include <stddef.h>
#include "../arch/i386/pmm.h" // For raw page allocation

// Internal structure for free list items
struct vm_zone_item {
    struct vm_zone_item *next;
};

// Static pool for bootstrap zones
#define MAX_BOOTSTRAP_ZONES 16
static vm_zone_t bootstrap_zones[MAX_BOOTSTRAP_ZONES];
static int next_bootstrap_zone = 0;

void vm_zone_init(void) {
    next_bootstrap_zone = 0;
}

vm_zone_t *vm_zone_create(const char *name, size_t size, size_t align) {
    if (next_bootstrap_zone >= MAX_BOOTSTRAP_ZONES) return NULL;

    vm_zone_t *zone = &bootstrap_zones[next_bootstrap_zone++];
    zone->name = name;
    zone->size = size;
    zone->align = align;
    zone->free_list = NULL;
    zone->free_count = 0;
    zone->total_items = 0;
    zone->active_items = 0;

    return zone;
}

static void zone_grow(vm_zone_t *zone) {
    // Allocate a raw page from PMM
    void *page = pmm_alloc_block();
    if (!page) return;

    // Carve page into objects
    size_t items = 4096 / zone->size;
    for (size_t i = 0; i < items; i++) {
        struct vm_zone_item *item = (struct vm_zone_item *)((uintptr_t)page + (i * zone->size));
        item->next = zone->free_list;
        zone->free_list = item;
    }
    
    zone->free_count += items;
    zone->total_items += items;
}

void *vm_zone_alloc(vm_zone_t *zone) {
    if (!zone->free_list) {
        zone_grow(zone);
    }

    if (!zone->free_list) return NULL;

    struct vm_zone_item *item = zone->free_list;
    zone->free_list = item->next;
    zone->free_count--;
    zone->active_items++;

    return (void *)item;
}

void vm_zone_free(vm_zone_t *zone, void *item_ptr) {
    struct vm_zone_item *item = (struct vm_zone_item *)item_ptr;
    item->next = zone->free_list;
    zone->free_list = item;
    zone->free_count++;
    zone->active_items--;
}
