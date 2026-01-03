#include "pmm.h"
#include "multiboot.h"
#include "e820.h"
#include "intr.h"
#include "../../vm/vm_page.h"
#include <string.h>
#include <stdio.h>
#include <sys/lock.h>
#include "../../kern/console.h"

// ==================== PMM Data Structures ====================
// Hybrid approach: Free list for O(1) single-page, bitmap for contiguous

// Free List (O(1) single-page allocation)
static uint32_t pmm_free_list_head;   // Physical address of first free page (0 = empty)
static size_t pmm_free_count;         // Number of pages in free list

// Bitmap (for contiguous allocation and tracking)
static uint8_t* pmm_bitmap;
static size_t pmm_bitmap_size;
static size_t pmm_total_blocks;
static size_t pmm_used_blocks;

// VM Page Array
vm_page_t *pmm_page_array = NULL;
size_t pmm_total_pages = 0;

// Static bitmap for 128MB (32768 blocks -> 4096 bytes)
static uint8_t pmm_bitmap_static[4096];

// SMP Lock
static spinlock_t pmm_lock;

// ==================== Watermark (Bump) Allocator ====================
// Used during early boot before free list is ready
// Allocates from kernel end, only grows, never frees

static uint32_t watermark_base;     // Start of watermark region (physical)
static uint32_t watermark_ptr;      // Current allocation pointer (physical)
static uint32_t watermark_end;      // End of available early-boot memory

// Initialize watermark allocator (called very early, before pmm_init)
void pmm_watermark_init(uint32_t phys_limit) {
    extern uint32_t _kernel_end;
    uint32_t v_end = (uint32_t)(uintptr_t)&_kernel_end;
    uint32_t p_end = v_end - 0xC0000000;  // Convert to physical
    
    // Align to page boundary
    watermark_base = (p_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    watermark_ptr = watermark_base;
    
    // Default 1MB for early-boot structures, but clamped to physical RAM limit
    watermark_end = watermark_base + (1024 * 1024);
    if (watermark_end > phys_limit) {
        watermark_end = phys_limit;
    }
}

// Allocate from watermark (O(1) bump allocation, never freed)
void* pmm_watermark_alloc(size_t bytes) {
    // Align to 16 bytes
    bytes = (bytes + 15) & ~15;
    
    if (watermark_ptr + bytes > watermark_end) {
        return NULL;  // Out of early-boot memory
    }
    
    uint32_t result = watermark_ptr;
    watermark_ptr += bytes;
    
    // Return virtual address (kernel mapping)
    return (void*)(uintptr_t)(result + 0xC0000000);
}

// Get watermark high-water mark for later reservation
uint32_t pmm_watermark_used(void) {
    return watermark_ptr - watermark_base;
} 

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
    // 0. Initialize Lock
    spinlock_init(&pmm_lock, "pmm");

    // 1. Find maximum physical memory address FIRST
    // (We need this to init watermark allocator safely)
    uint64_t max_phys_addr = 0x1000000; // Assume at least 16MB
    uint64_t total_usable_bytes = 0;
    uint32_t usable_regions = 0;

    if (mmap_addr != 0 && mmap_length != 0) {
        multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)(uintptr_t)mmap_addr;
        while ((uint32_t)(uintptr_t)mmap < mmap_addr + mmap_length) {
            if (pmm_is_usable_type(mmap->type)) {
                uint64_t end = mmap->addr + mmap->len;
                if (end > max_phys_addr) max_phys_addr = end;
                
                // Track usable stats
                 uint32_t start_clamped = pmm_clamp_addr(mmap->addr);
                uint32_t end_clamped = pmm_clamp_addr(end);
                 if (end_clamped > start_clamped) {
                    total_usable_bytes += (end_clamped - start_clamped);
                    usable_regions++;
                }
            }
            mmap = (multiboot_mmap_entry_t*)((uint32_t)(uintptr_t)mmap + mmap->size + sizeof(mmap->size));
        }
    }

    // 2. Initialize bootstrap watermark allocator with limit
    pmm_watermark_init(pmm_clamp_addr(max_phys_addr));
    // max_phys_addr / 4096 = total blocks
    // total blocks / 8 = bytes needed
    max_phys_addr = pmm_clamp_addr(max_phys_addr);
    
    // Align max_phys to block size
    if (max_phys_addr & (PMM_BLOCK_SIZE - 1)) {
        max_phys_addr = (max_phys_addr + PMM_BLOCK_SIZE) & ~(PMM_BLOCK_SIZE - 1);
    }
    
    pmm_total_blocks = max_phys_addr / PMM_BLOCK_SIZE;
    size_t bitmap_bytes = (pmm_total_blocks + 7) / 8;
    
    // Attempt dynamic allocation
    pmm_bitmap = (uint8_t*)pmm_watermark_alloc(bitmap_bytes);
    
    // Also allocate vm_page_t array
    size_t array_bytes = pmm_total_blocks * sizeof(vm_page_t);
    pmm_page_array = (vm_page_t*)pmm_watermark_alloc(array_bytes);
    pmm_total_pages = pmm_total_blocks;
    
    if (pmm_bitmap) {
        pmm_bitmap_size = bitmap_bytes;
        kprint("PMM: Dynamic bitmap allocated. ");
    } else {
        // Fallback to static bitmap
        pmm_bitmap = pmm_bitmap_static;
        pmm_bitmap_size = sizeof(pmm_bitmap_static);
        pmm_total_blocks = pmm_bitmap_size * 8; // Cap at 128MB
        kprint("PMM: Using static bitmap (128MB limit). ");
    }

    if (pmm_page_array) {
        memset(pmm_page_array, 0, pmm_total_blocks * sizeof(vm_page_t));
        for (size_t i = 0; i < pmm_total_blocks; i++) {
            pmm_page_array[i].phys_addr = i * PMM_BLOCK_SIZE;
            pmm_page_array[i].flags = PG_FREE;
        }
        kprint("Page array allocated.\n");
    }

    // Mark everything as used initially
    memset(pmm_bitmap, 0xFF, pmm_bitmap_size); 
    pmm_used_blocks = pmm_total_blocks;

    // Report stats
    char buf[64];
    sprintf(buf, "%u MB RAM detected.\n", (uint32_t)(max_phys_addr / (1024 * 1024)));
    kprint(buf);

    if (mmap_addr == 0 || mmap_length == 0) {
        // Fallback: assume 16MB conventional
         kprint("PMM: No memory map provided, assuming 16MB.\n");
        for (uint32_t i = 0x100000; i < (16 * 1024 * 1024); i += PMM_BLOCK_SIZE) {
            if (i < pmm_total_blocks * PMM_BLOCK_SIZE)
                pmm_mark_free(i);
        }
        pmm_reserve_kernel();
        return;
    }

    // Phase 2: Mark usable pages
    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)(uintptr_t)mmap_addr;
    while ((uint32_t)(uintptr_t)mmap < mmap_addr + mmap_length) {
        if (pmm_is_usable_type(mmap->type)) {
            uint32_t start = pmm_clamp_addr(mmap->addr);
            uint32_t end = pmm_clamp_addr(mmap->addr + mmap->len);
            
            // Align to page boundaries
            start = (start + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
            end = end & ~(PMM_BLOCK_SIZE - 1);
            
            // Mark free, respecting total blocks (bitmap size)
            for (uint32_t addr = start; addr < end; addr += PMM_BLOCK_SIZE) {
                 if (addr < pmm_total_blocks * PMM_BLOCK_SIZE)
                    pmm_mark_free(addr);
            }
        }
        mmap = (multiboot_mmap_entry_t*)((uint32_t)(uintptr_t)mmap + mmap->size + sizeof(mmap->size));
    }
    
    // Mark watermark region as used! 
    // (Actually pmm_mark_free normally marks pages free, and we started with 0xFF (used). 
    // Watermark allocated pages were never marked free, so they remain used. Correct.)

    pmm_reserve_kernel();
}

vm_page_t *pmm_get_page(uintptr_t pa) {
    size_t idx = pa / PMM_BLOCK_SIZE;
    if (idx < pmm_total_pages) {
        return &pmm_page_array[idx];
    }
    return NULL;
}

// ==================== O(1) Free List Allocator ====================
// Each free page stores the physical address of the next free page at offset 0
// We use the kernel's higher-half mapping: VA = PA + 0xC0000000

#define PHYS_TO_VIRT(p) ((void*)((uintptr_t)(p) + 0xC0000000))
#define VIRT_TO_PHYS(v) ((uint32_t)((uintptr_t)(v) - 0xC0000000))

// Add a page to the free list (O(1) push)
static void pmm_free_list_push(uint32_t phys_addr) {
    uint32_t* page_ptr = (uint32_t*)PHYS_TO_VIRT(phys_addr);
    *page_ptr = pmm_free_list_head;  // Store current head in new page
    pmm_free_list_head = phys_addr;  // New page becomes head
    pmm_free_count++;
}

// Remove a page from the free list (O(1) pop)
static uint32_t pmm_free_list_pop(void) {
    if (pmm_free_list_head == 0) return 0;  // Empty list
    
    uint32_t result = pmm_free_list_head;
    uint32_t* page_ptr = (uint32_t*)PHYS_TO_VIRT(result);
    pmm_free_list_head = *page_ptr;  // Next page becomes head
    pmm_free_count--;
    return result;
}

void* pmm_alloc_block(void) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&pmm_lock);

    // Try free list first (O(1))
    uint32_t addr = pmm_free_list_pop();
    if (addr != 0) {
        // Also update bitmap for contiguous allocation tracking
        uint32_t block = addr / PMM_BLOCK_SIZE;
        uint32_t idx = block / 8;
        uint32_t bit = block % 8;
        if (idx < pmm_bitmap_size) {
            pmm_bitmap[idx] |= (1 << bit);
            pmm_used_blocks++;
        }
        
        spinlock_release(&pmm_lock);
        intr_restore(flags);
        return (void*)(uintptr_t)addr;
    }
    
    spinlock_release(&pmm_lock);
    intr_restore(flags);
    return NULL;  // Out of memory
}

void pmm_free_block(void* p) {
    uint32_t addr = (uint32_t)(uintptr_t)p;
    if (addr == 0) return;  // NULL check
    
    uint32_t flags = intr_disable();
    spinlock_acquire(&pmm_lock);

    uint32_t block = addr / PMM_BLOCK_SIZE;
    uint32_t idx = block / 8;
    uint32_t bit = block % 8;

    // Update bitmap
    if (idx < pmm_bitmap_size) {
        if (pmm_bitmap[idx] & (1 << bit)) {
            pmm_bitmap[idx] &= ~(1 << bit);
            pmm_used_blocks--;
        }
    }
    
    // Add to free list
    pmm_free_list_push(addr);

    spinlock_release(&pmm_lock);
    intr_restore(flags);
}

void pmm_dump_mmap(uint32_t mmap_addr, uint32_t mmap_length) {
    kprint("BIOS-e820 physical RAM map:\n");
    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)(uintptr_t)mmap_addr;
    while((uint32_t)(uintptr_t)mmap < mmap_addr + mmap_length) {
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
    uint32_t flags = intr_disable();
    spinlock_acquire(&pmm_lock);

    if (pmm_used_blocks + count > pmm_total_blocks) {
        spinlock_release(&pmm_lock);
        intr_restore(flags);
        return NULL;
    }

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
            
            spinlock_release(&pmm_lock);
            intr_restore(flags);
            return (void*)(i * PMM_BLOCK_SIZE);
        }
        
        // Optimization: skip the used block we hit
        i += free_run;
    }

    spinlock_release(&pmm_lock);
    intr_restore(flags);
    return NULL;
}

void pmm_free_contiguous(void* p, size_t count) {
    uint32_t start_block = (uint32_t)(uintptr_t)p / PMM_BLOCK_SIZE;

    uint32_t flags = intr_disable();
    spinlock_acquire(&pmm_lock);

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

    spinlock_release(&pmm_lock);
    intr_restore(flags);
}
