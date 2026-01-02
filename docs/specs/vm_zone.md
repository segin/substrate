# Zone Allocator (UMA) Specification

## Overview
The Zone Allocator (inspired by BSD's Universal Memory Allocator) provides fixed-size object caching. It aims to reduce allocation overhead and fragmentation by managing "zones" of identical objects.

## Design
- **Unit of Allocation:** Fixed-size items.
- **Growth:** Zones grow by requesting raw pages from the Physical Memory Manager (PMM).
- **Free List:** Each zone maintains a singly-linked list of free items within its pages.
- **Efficiency:** O(1) allocation and deallocation from the free list.

## API
### `vm_zone_t *vm_zone_create(const char *name, size_t size, size_t align)`
Creates a new zone for objects of `size`.
- `name`: Human-readable identifier for debugging.
- `align`: Requested alignment (must be a power of two).

### `void *vm_zone_alloc(vm_zone_t *zone)`
Allocates an item from the zone.
- Triggers `zone_grow` if the free list is empty.

### `void vm_zone_free(vm_zone_t *zone, void *item)`
Returns an item to the zone's free list.

## Constraints
- Maximum number of bootstrap zones is currently 16.
- Does not currently support reclaiming empty pages (garbage collection).
