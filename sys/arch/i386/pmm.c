#include "pmm.h"
#include <string.h>

static uint8_t* pmm_bitmap;
static size_t pmm_bitmap_size;
static size_t pmm_total_blocks;
static size_t pmm_used_blocks;

// Assuming the bitmap is placed after the kernel code.
// For now, we'll just hardcode a location for the bitmap or use a static array 
// if memory is small, but a proper OS allocates this dynamically.
// Here we use a static array large enough for 128MB RAM (32768 blocks -> 4096 bytes bitmap)
static uint8_t pmm_bitmap_static[4096]; 

void pmm_init(size_t mem_size) {
    pmm_total_blocks = mem_size / PMM_BLOCK_SIZE;
    pmm_bitmap = pmm_bitmap_static;
    pmm_bitmap_size = pmm_total_blocks / PMM_BLOCKS_PER_BYTE;
    pmm_used_blocks = pmm_total_blocks; // Initially mark all as used until freed? 
                                        // Or mark all free. Let's mark all free.
    
    memset(pmm_bitmap, 0, 4096); 
    pmm_used_blocks = 0;
}

void* pmm_alloc_block(void) {
    if (pmm_used_blocks >= pmm_total_blocks) return NULL;

    for (size_t i = 0; i < pmm_bitmap_size; i++) {
        if (pmm_bitmap[i] != 0xFF) {
            for (int j = 0; j < 8; j++) {
                if (!(pmm_bitmap[i] & (1 << j))) {
                    pmm_bitmap[i] |= (1 << j);
                    pmm_used_blocks++;
                    return (void*)((i * 8 + j) * PMM_BLOCK_SIZE);
                }
            }
        }
    }
    return NULL;
}

void pmm_free_block(void* p) {
    uint32_t addr = (uint32_t)p;
    uint32_t block = addr / PMM_BLOCK_SIZE;
    uint32_t idx = block / 8;
    uint32_t bit = block % 8;

    if (idx < pmm_bitmap_size) {
        if (pmm_bitmap[idx] & (1 << bit)) {
            pmm_bitmap[idx] &= ~(1 << bit);
            pmm_used_blocks--;
        }
    }
}
