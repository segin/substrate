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
void pmm_free_block(void* p);

#endif
