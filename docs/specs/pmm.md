# Physical Memory Manager (PMM) Specification

## Overview
The PMM manages the physical RAM of the system. It tracks which pages are free and which are used using a **Buddy Allocator** for efficient allocation and coalescing.

## Design

### Buddy Allocator
- **Orders:** 0-10 (4KB to 4MB blocks).
- **Free Lists:** Per-order doubly-linked lists for O(1) enqueue/dequeue.
- **Allocation:** O(log N) - Find smallest available order ≥ requested, split down.
- **Free:** O(log N) - Coalesce with buddy if both free, repeat up to max order.

### Watermark (Bootstrap) Allocator
- Used during early boot before buddy allocator is initialized.
- Simple bump allocator that allocates upward from kernel end.
- Memory allocated here is **never freed**.
- Returns kernel direct-mapped virtual addresses (`0xC0000000 + phys`).
- Used to reserve bootstrap metadata such as the PMM bitmap and `vm_page_t` array before the buddy allocator is available.

### Memory Discovery
- Initializes from Multiboot `mmap` or BIOS `e820` structures.
- Validates and sanitizes memory regions.
- Reserves kernel text/data/bss regions.

### Bitmap (Diagnostics Only)
- A bitmap is maintained for debugging and visualization.
- **Not used** for allocation decisions - buddy system is authoritative.

## API

### Initialization
```c
void pmm_watermark_init(uint32_t phys_limit);  // Early boot setup
void pmm_init(uint32_t mmap_addr, uint32_t mmap_length);  // Multiboot
void pmm_init_e820(e820_entry_t *map, uint32_t count);    // Raw e820
```

### Single Page Allocation
```c
void *pmm_alloc_block(void);   // Allocate 4KB page (returns kernel virt addr)
void pmm_free_block(void *p);  // Free single page
```

### Contiguous Allocation (Buddy System)
```c
void *pmm_alloc_contiguous(size_t count);           // Allocate count pages
void pmm_free_contiguous(void *p, size_t count);    // Free contiguous range
```

### Watermark Allocation (Early Boot)
```c
void *pmm_watermark_alloc(size_t bytes);  // Returns kernel virt addr
```

## Implementation Details
- **Page Metadata:** `vm_page_t` structure tracks physical address, flags, order, and free list links.
- **Transition:** boot starts on the watermark allocator, then `vm_phys_early_init()` and `vm_phys_add_range()` establish the machine-independent buddy allocator and page database.
- **Global Lock:** Spinlock protects free lists for SMP safety.
- **Low Memory Safeguards:** Reserves pages below 1MB for BIOS/legacy.

## Addressing Convention
- `pmm_alloc_block()` and `pmm_alloc_contiguous()` return kernel virtual addresses in the direct map.
- `pmm_free_block()` and `pmm_free_contiguous()` expect those same virtual addresses.
- `pmm_get_page(phys_addr)` and `vm_phys_paddr_to_page(phys_addr)` are the physical-address lookup path when code needs the backing `vm_page_t`.
- Callers handing pages to page tables or hardware must convert the returned kernel virtual address back to a physical address by subtracting the direct-map base.

## Memory Limits
- **32-bit:** Up to 4GB physical memory supported via PAE (future).
- **Current:** Limited by available RAM detected at boot.
- **No Static Limit:** Dynamic metadata sizing (no 128MB cap).
