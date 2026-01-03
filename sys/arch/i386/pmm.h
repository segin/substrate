#ifndef _PMM_H
#define _PMM_H

#include <stdint.h>
#include <stddef.h>
#include "e820.h"

// Simple Bitmap Physical Memory Manager
// Assumes 32-bit address space
// Block size = 4KB

#define PMM_BLOCK_SIZE 4096
#define PMM_BLOCKS_PER_BYTE 8

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

// Watermark Allocator (Early Boot)
void pmm_watermark_init(void);
void* pmm_watermark_alloc(size_t bytes);
uint32_t pmm_watermark_used(void);

#endif
