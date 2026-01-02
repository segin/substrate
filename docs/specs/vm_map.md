# VM Map Specification

## Overview
A `vm_map` represents the virtual address space of a process (or the kernel). It consists of a sorted list of `vm_map_entry` structures, each describing a contiguous region of virtual memory and its mapping to a `vm_object`.

## Design
- **Map Structure:** Circular doubly-linked list with a sentinel header.
- **Entry Structure:** Defines `start`, `end`, `offset`, and protection flags.
- **Address Space Search:** The map tracks gaps between entries to fulfill new allocation requests.

## API
### `void vm_map_init(vm_map_t *map, pmap_t pmap, uintptr_t min, uintptr_t max)`
Initializes an existing map structure.

### `int vm_map_insert(vm_map_t *map, struct vm_object *obj, uint64_t offset, uintptr_t start, uintptr_t end)`
Creates a new mapping in the specified range. Fails if the range overlaps existing entries.

### `int vm_map_remove(vm_map_t *map, uintptr_t start, uintptr_t end)`
Removes mappings in the specified range, potentially trimming existing entries.

### `int vm_map_find_space(vm_map_t *map, uintptr_t *addr, size_t length)`
Searches for a free gap of at least `length` bytes and returns the starting address.

## Constraints
- Does not currently support automatic merging of adjacent entries with identical properties.
- Bootstrap pool is limited to 64 entries.
