# Kmem Allocator Specification

## Overview
The `kmem` allocator is a general-purpose, variable-size memory allocator for the Substrate kernel. It is built upon the Zone Allocator (UMA) to provide efficient allocation for small objects while maintaining power-of-two alignment.

## Design
- **Architecture:** Power-of-two buckets.
- **Backing:** Leverages `vm_zone` for each bucket size.
- **Minimum Allocation:** 16 bytes.
- **Maximum Allocation:** 2048 bytes.
- **Alignment:** All allocations are aligned to their size (power-of-two).

## API
### `void kmem_init(void)`
Initializes the internal zones for each power-of-two bucket.

### `void *kmalloc(size_t size)`
Allocates `size` bytes of kernel memory. 
- Returns a pointer to the allocated memory.
- Returns `NULL` if the size is too large or if memory is exhausted.

### `void kfree(void *ptr, size_t size)`
Frees the memory pointed to by `ptr`.
- `size` must match the size requested during `kmalloc` (or be within the same bucket range).

## Constraints
- Not safe for use before `vm_zone_init` and `pmm_init`.
- Current implementation is limited to a maximum of 2048 bytes per allocation.
- Does not currently support `M_WAITOK` / `M_NOWAIT` flags.
