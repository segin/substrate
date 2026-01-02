# VM Page Specification

## Overview
A `vm_page` tracks the state of a single physical page of memory. It serves as the bridge between the machine-independent VM layer and the physical memory manager.

## Design
- **States:**
    - `PG_FREE`: On the free list, ready for allocation.
    - `PG_ACTIVE`: Currently mapped and in use.
    - `PG_INACTIVE`: Mapped but a candidate for replacement (LRU).
    - `PG_BUSY`: Undergoing I/O or transitional state.
- **Queues:** Pages are managed in doubly-linked lists representing their current state.
- **Identity:** Each page is identified by its `phys_addr` and its relationship to a `vm_object` + `pindex`.

## API
### `void vm_page_init(void)`
Initializes the global page queues.

### `vm_page_t *vm_page_alloc(struct vm_object *object, uint64_t pindex, int req)`
Allocates a page from the free list and associates it with an object.

### `void vm_page_free(vm_page_t *m)`
Returns a page to the free list and disassociates it from any object.

## Constraints
- Does not currently implement a background "page daemon" for automatic reclamation.
- Current prototype is limited by the lack of a kernel heap for dynamic `vm_page_t` structures.
