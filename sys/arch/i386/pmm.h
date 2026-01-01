#ifndef _PMM_H
#define _PMM_H

#include <stdint.h>
#include <stddef.h>

// Simple Bitmap Physical Memory Manager
// Assumes 32-bit address space
// Block size = 4KB

#define PMM_BLOCK_SIZE 4096
#define PMM_BLOCKS_PER_BYTE 8

void pmm_init(size_t mem_size);
void* pmm_alloc_block(void);
void pmm_free_block(void* p);

#endif
