#include "pmm.h"
#include "multiboot.h"
#include "e820.h"
#include <string.h>

static uint8_t* pmm_bitmap;
static size_t pmm_bitmap_size;
static size_t pmm_total_blocks;
static size_t pmm_used_blocks;

// Static bitmap for 128MB (32768 blocks -> 4096 bytes)
static uint8_t pmm_bitmap_static[4096]; 

static void pmm_mark_used(uint32_t addr) {
    uint32_t block = addr / PMM_BLOCK_SIZE;
    uint32_t idx = block / 8;
    uint32_t bit = block % 8;
    if (idx < pmm_bitmap_size) {
        if (!(pmm_bitmap[idx] & (1 << bit))) {
            pmm_bitmap[idx] |= (1 << bit);
            pmm_used_blocks++;
        }
    }
}

static void pmm_mark_free(uint32_t addr) {
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

static void pmm_init_bitmap(void) {
    // For now, assume a max of 128MB for the static bitmap
    pmm_total_blocks = (128 * 1024 * 1024) / PMM_BLOCK_SIZE;
    pmm_bitmap = pmm_bitmap_static;
    pmm_bitmap_size = 4096;
    
    // Mark everything as used/reserved by default
    memset(pmm_bitmap, 0xFF, pmm_bitmap_size); 
    pmm_used_blocks = pmm_total_blocks;
}

static void pmm_reserve_kernel(void) {
    extern uint32_t _kernel_end;
    uint32_t kernel_end = (uint32_t)&_kernel_end;
    
    // Reserve everything from 0 up to kernel_end, aligned to next block
    // Also ensuring at least 1MB is reserved if kernel is small (which it isn't, but for safety)
    if (kernel_end < 0x100000) kernel_end = 0x100000;
    
    for (uint32_t i = 0; i < kernel_end; i += PMM_BLOCK_SIZE) {
        pmm_mark_used(i);
    }
}

void pmm_init_e820(e820_entry_t *map, uint32_t count) {
    pmm_init_bitmap();

    for (uint32_t i = 0; i < count; i++) {
        if (map[i].type == E820_USABLE || map[i].type == E820_ACPI) {
            for (uint64_t j = 0; j < map[i].len; j += PMM_BLOCK_SIZE) {
                uint32_t addr = (uint32_t)(map[i].addr + j);
                if (addr < (128 * 1024 * 1024)) {
                    pmm_mark_free(addr);
                }
            }
        }
    }
    
    pmm_reserve_kernel();
}

void pmm_init(uint32_t mmap_addr, uint32_t mmap_length) {
    pmm_init_bitmap();

    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)mmap_addr;
    while((uint32_t)mmap < mmap_addr + mmap_length) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE || mmap->type == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE) {
            for (uint64_t i = 0; i < mmap->len; i += PMM_BLOCK_SIZE) {
                uint32_t addr = (uint32_t)(mmap->addr + i);
                if (addr < (128 * 1024 * 1024)) {
                    pmm_mark_free(addr);
                }
            }
        }
        mmap = (multiboot_mmap_entry_t*) ((uint32_t)mmap + mmap->size + sizeof(mmap->size));
    }

    pmm_reserve_kernel();
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

void* pmm_alloc_contiguous(size_t count) {
    if (pmm_used_blocks + count > pmm_total_blocks) return NULL;

    // Brute force search for 'count' consecutive free blocks
    // Note: This is slow and inefficient for a large bitmap, but sufficient for a basic PMM.
    // A proper implementation would use a better data structure (buddy allocator or free lists).
    
    for (size_t i = 0; i < pmm_total_blocks; i++) {
        size_t free_run = 0;
        for (size_t j = 0; j < count; j++) {
            if (i + j >= pmm_total_blocks) break;
            
            size_t idx = (i + j) / 8;
            size_t bit = (i + j) % 8;
            
            if (pmm_bitmap[idx] & (1 << bit)) {
                // Used (bit is 1)
                break;
            }
            free_run++;
        }
        
        if (free_run == count) {
            // Found a run, mark as used
            for (size_t j = 0; j < count; j++) {
                size_t idx = (i + j) / 8;
                size_t bit = (i + j) % 8;
                pmm_bitmap[idx] |= (1 << bit);
            }
            pmm_used_blocks += count;
            return (void*)(i * PMM_BLOCK_SIZE);
        }
        
        // Optimization: skip the used block we hit
        i += free_run;
    }
    return NULL;
}

void pmm_free_contiguous(void* p, size_t count) {
    uint32_t start_block = (uint32_t)p / PMM_BLOCK_SIZE;
    for (size_t i = 0; i < count; i++) {
        uint32_t block = start_block + i;
        uint32_t idx = block / 8;
        uint32_t bit = block % 8;
        
        if (idx < pmm_bitmap_size) {
             if (pmm_bitmap[idx] & (1 << bit)) {
                pmm_bitmap[idx] &= ~(1 << bit);
                pmm_used_blocks--;
            }
        }
    }
}
