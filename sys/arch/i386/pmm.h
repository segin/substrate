#ifndef _PMM_H
#define _PMM_H

#include <stdint.h>
#include <stddef.h>
#include <arch/x86-common/e820.h>
#include <arch/x86-common/multiboot.h>

// Simple Bitmap Physical Memory Manager
// Assumes 32-bit address space
// Block size = 4KB

#define PMM_BLOCK_SIZE 4096
#define PMM_BLOCKS_PER_BYTE 8

typedef uint32_t phys_addr_t;

/* Iterator callback for memory regions */
typedef void (*pmm_region_callback)(phys_addr_t start, phys_addr_t len, void *arg);
void pmm_walk_mmap(uint32_t mmap_addr, uint32_t mmap_length, pmm_region_callback cb, void *arg);
void pmm_record_boot_info(const multiboot_info_t *mbi);

void pmm_init(uint32_t mmap_addr, uint32_t mmap_length);
void pmm_init_e820(e820_entry_t *map, uint32_t count);
void* pmm_alloc_block(void);
void* pmm_alloc_contiguous(size_t count);
void pmm_free_block(void* p);
void pmm_free_contiguous(void* p, size_t count);
void pmm_reclaim_range(uint32_t start, uint32_t end);
void pmm_reclaim_setup(void);
void pmm_dump_map(void);
void pmm_dump_mmap(uint32_t mmap_addr, uint32_t mmap_length);

/* E820 Memory Map Support */
void pmm_walk_e820(const e820_entry_t *map, uint32_t count, pmm_region_callback cb, void *arg);
void pmm_dump_e820(const e820_entry_t *map, uint32_t count);

// Watermark Allocator (Early Boot)
void pmm_watermark_init(uint32_t start, uint32_t end);
void* pmm_watermark_alloc(size_t bytes, size_t align);
uint32_t pmm_watermark_used(void);

// VM Page Integration
struct vm_page;
extern struct vm_page *pmm_page_array;
extern size_t pmm_total_pages;
struct vm_page *pmm_get_page(uintptr_t pa);

#endif
