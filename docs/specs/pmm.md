# Physical Memory Manager (PMM) Specification

## Overview
The PMM manages the physical RAM of the system. it tracks which pages are free and which are used using a bitmap-based allocator. It handles memory discovery from Multiboot and e820 maps.

## Design
- **Unit of Allocation:** 4KB blocks (pages).
- **Tracking:** A bitmap where 1 represents a used block and 0 represents a free block.
- **Regions:** Supports non-contiguous memory regions by marking unavailable ranges as "used" in the bitmap.
- **Bootstrapping:** Initializes from Multiboot `mmap` or BIOS `e820` structures.

## API
### `void pmm_init(uint32_t mmap_addr, uint32_t mmap_length)`
Initializes the PMM from a Multiboot memory map.

### `void pmm_init_e820(e820_entry_t *map, uint32_t count)`
Initializes the PMM from a raw e820 map.

### `void *pmm_alloc_block(void)`
Allocates a single 4KB block. Returns physical address or NULL.

### `void *pmm_alloc_contiguous(size_t count)`
Allocates a contiguous range of `count` physical blocks.

### `void pmm_free_block(void *p)`
Frees a previously allocated block.

## Constraints
- Max memory supported by current static bitmap: 128MB.
- Bitmap itself is statically allocated during bootstrap.
