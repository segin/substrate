#include "pmm.h"
#include "multiboot.h"
#include "e820.h"
#include <string.h>
#include <stdio.h>
#include "../../kern/console.h"

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
    uint32_t v_end = (uint32_t)(uintptr_t)&_kernel_end;
    
    // Convert higher-half virtual end to physical end
    // (Assuming linear mapping V = P + 0xC0000000)
    uint32_t p_end = v_end - 0xC0000000;
    
    // Reserve first 1MB for BIOS/Multiboot safety
    for (uint32_t i = 0; i < 0x100000; i += PMM_BLOCK_SIZE) {
        pmm_mark_used(i);
    }
    
    // Reserve kernel physical range (starts at 1MB)
    for (uint32_t i = 0x100000; i < p_end; i += PMM_BLOCK_SIZE) {
        pmm_mark_used(i);
    }
}

// Generic range reclamation (physical addresses)
void pmm_reclaim_range(uint32_t start, uint32_t end) {
    // Round start down to block boundary
    start &= ~(PMM_BLOCK_SIZE - 1);
    // Round end up to block boundary
    end = (end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);

    for (uint32_t i = start; i < end; i += PMM_BLOCK_SIZE) {
        pmm_free_block((void*)(uintptr_t)i);
    }
}

// Reclaim early boot setup memory
void pmm_reclaim_setup(void) {
    extern uint32_t _setup_start, _setup_end;
    // These are virtual addresses in Higher Half, but .setup is identity mapped at 1MB
    // Actually, in the linker script, .setup is at 1M physical.
    // _setup_start/end are the addresses of the section.
    uint32_t start = (uint32_t)(uintptr_t)&_setup_start;
    uint32_t end = (uint32_t)(uintptr_t)&_setup_end;
    uint32_t size = end - start;
    
    kprint("Freeing setup memory: ");
    // Print size in KB
    char buf[16];
    uint32_t kb = size / 1024;
    int pos = 0;
    if (kb == 0) {
        buf[pos++] = '0';
    } else {
        int started = 0;
        for (int div = 1000; div >= 1; div /= 10) {
            int digit = (kb / div) % 10;
            if (digit || started) {
                buf[pos++] = '0' + digit;
                started = 1;
            }
        }
    }
    buf[pos] = '\0';
    kprint(buf);
    kprint("K\n");
    
    pmm_reclaim_range(start, end);
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


// Validate memory map entry type
static int pmm_is_usable_type(uint32_t type) {
    return (type == MULTIBOOT_MEMORY_AVAILABLE || 
            type == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE);
}

// Clamp 64-bit address to 32-bit (ignore high memory for now)
static uint32_t pmm_clamp_addr(uint64_t addr) {
    if (addr > 0xFFFFFFFF) return 0xFFFFFFFF;
    return (uint32_t)addr;
}

void pmm_init(uint32_t mmap_addr, uint32_t mmap_length) {
    pmm_init_bitmap();
    
    uint64_t total_usable_bytes = 0;
    uint32_t usable_regions = 0;

    if (mmap_addr == 0 || mmap_length == 0) {
        // Fallback: assume 16MB conventional (very conservative)
        kprint("PMM: No memory map provided, assuming 16MB.\n");
        for (uint32_t i = 0x100000; i < (16 * 1024 * 1024); i += PMM_BLOCK_SIZE) {
            pmm_mark_free(i);
        }
        pmm_reserve_kernel();
        return;
    }

    // Phase 1: Calculate total usable RAM (first pass)
    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)(uintptr_t)mmap_addr;
    while ((uint32_t)(uintptr_t)mmap < mmap_addr + mmap_length) {
        if (pmm_is_usable_type(mmap->type)) {
            // Clamp to 32-bit address space
            uint32_t start = pmm_clamp_addr(mmap->addr);
            uint32_t end = pmm_clamp_addr(mmap->addr + mmap->len);
            
            if (end > start && start < 0xFFFFFFFF) {
                uint32_t region_size = end - start;
                total_usable_bytes += region_size;
                usable_regions++;
            }
        }
        mmap = (multiboot_mmap_entry_t*)((uint32_t)(uintptr_t)mmap + mmap->size + sizeof(mmap->size));
    }

    // Report to console
    char buf[64];
    sprintf(buf, "PMM: %u usable regions, %u MB total\n", 
            usable_regions, (uint32_t)(total_usable_bytes / (1024 * 1024)));
    kprint(buf);

    // Phase 2: Mark usable pages (second pass)
    mmap = (multiboot_mmap_entry_t*)(uintptr_t)mmap_addr;
    while ((uint32_t)(uintptr_t)mmap < mmap_addr + mmap_length) {
        if (pmm_is_usable_type(mmap->type)) {
            uint32_t start = pmm_clamp_addr(mmap->addr);
            uint32_t end = pmm_clamp_addr(mmap->addr + mmap->len);
            
            // Align to page boundaries
            start = (start + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
            end = end & ~(PMM_BLOCK_SIZE - 1);
            
            for (uint32_t addr = start; addr < end && addr < (128 * 1024 * 1024); addr += PMM_BLOCK_SIZE) {
                pmm_mark_free(addr);
            }
        }
        mmap = (multiboot_mmap_entry_t*)((uint32_t)(uintptr_t)mmap + mmap->size + sizeof(mmap->size));
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
    uint32_t addr = (uint32_t)(uintptr_t)p;
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

void pmm_dump_mmap(uint32_t mmap_addr, uint32_t mmap_length) {
    kprint("BIOS-e820 physical RAM map:\n");
    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)(uintptr_t)mmap_addr;
    while((uint32_t)mmap < mmap_addr + mmap_length) {
        char buf[128];
        const char* type_str = "unknown";
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) type_str = "usable";
        else if (mmap->type == MULTIBOOT_MEMORY_RESERVED) type_str = "reserved";
        else if (mmap->type == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE) type_str = "ACPI data";
        else if (mmap->type == MULTIBOOT_MEMORY_NVS) type_str = "ACPI NVS";
        else if (mmap->type == MULTIBOOT_MEMORY_BADRAM) type_str = "bad RAM";

        // Print in format: [0x0000000000000000 - 0x000000000009ffff] usable
        uint64_t start = mmap->addr;
        uint64_t end = mmap->addr + mmap->len - 1;
        
        sprintf(buf, " [0x%08x%08x - 0x%08x%08x] %s\n", 
            (uint32_t)(start >> 32), (uint32_t)(start & 0xFFFFFFFF),
            (uint32_t)(end >> 32), (uint32_t)(end & 0xFFFFFFFF),
            type_str);
        kprint(buf);
        
        mmap = (multiboot_mmap_entry_t*) ((uint32_t)(uintptr_t)mmap + mmap->size + sizeof(mmap->size));
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
    uint32_t start_block = (uint32_t)(uintptr_t)p / PMM_BLOCK_SIZE;
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
