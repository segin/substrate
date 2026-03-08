# Kmem Allocator Specification

## Overview
`kmalloc()` is the kernel's general-purpose size-based allocator.

Current architecture:
- sizes `16..4096` bytes are served from dedicated UMA malloc zones
- larger allocations bypass UMA and are satisfied as contiguous PMM page runs
- `kfree(ptr, size)` relies on the caller-supplied size to choose the correct
  free path

This makes `kmalloc()` a thin policy layer over:
- UMA for small-object caching
- PMM for page-granular large allocations

## Power-of-Two Zone Selection
The active zone set is:
- `kmem-16`
- `kmem-32`
- `kmem-64`
- `kmem-128`
- `kmem-256`
- `kmem-512`
- `kmem-1024`
- `kmem-2048`
- `kmem-4096`

Selection rule:
1. Find the first power-of-two bucket `>= size`.
2. Allocate from the corresponding UMA zone.
3. If no bucket can satisfy the request, fall back to the large-allocation path.

Examples:
- `kmalloc(1)` -> `kmem-16`
- `kmalloc(33)` -> `kmem-64`
- `kmalloc(3000)` -> `kmem-4096`
- `kmalloc(5000)` -> PMM contiguous allocation

## Small Allocation Path
`kmem_init()` creates one UMA zone per bucket using `UMA_ZONE_MALLOC`.

For sizes up to `4096`:
- `kmalloc(size)` computes the bucket index
- calls `uma_zalloc(zone, M_NOWAIT)`
- returns the allocated object directly

On failure, the current implementation logs the zone name and returns `NULL`.

## Large Allocation Path
Requests larger than the biggest bucket are allocated as page runs:
- total bytes = requested size + `kmem_large_header_t`
- required pages = round-up(total bytes / 4096)
- backing memory comes from `pmm_alloc_contiguous(pages)`

The leading header stores:
- original request size
- a magic value for sanity checking on free

The returned pointer is immediately after that header.

## Free Path
`kfree(ptr, size)` mirrors the selection logic:
- if `size <= 4096`, return the object to the corresponding UMA zone
- otherwise, step back to the large-allocation header, validate the magic,
  compute the page count, and return the run to PMM

This means the size argument is part of the allocator contract.

## Reallocation
`krealloc(ptr, size)` provides a size-recovering resize path for kernel callers:
- `krealloc(NULL, size)` behaves like `kmalloc(size)`
- `krealloc(ptr, 0)` frees the original allocation and returns `NULL`
- small allocations recover their original size from the owning UMA slab/zone
- large allocations recover their original size from `kmem_large_header_t`
- resize is implemented as allocate-copy-free, preserving `min(old, new)` bytes

## Initialization Order
Current bring-up sequence:
1. `uma_startup()`
2. `kmem_init()`
3. `uma_enable_dynamic_alloc()`

That order matters because:
- UMA must exist before `kmalloc()` can build its malloc zones
- `kmem_init()` finishing is what allows later UMA zone metadata to come from
  dynamic allocation instead of the static bootstrap pool

## Constraints
- not safe before PMM and UMA bootstrap are established
- API currently requires sized free
- small allocations are `M_NOWAIT` only in the active implementation
- large allocations are page-granular and may over-allocate up to almost one page
