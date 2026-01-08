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

// Buddy Allocator (O(log N) contiguous allocation)
#define PMM_MAX_ORDER 11   // 2^10 = 1024 pages = 4MB block
static vm_page_t *pmm_buddy_free_lists[PMM_MAX_ORDER];
static size_t pmm_free_count;         // Total free pages across all orders

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

// Forward declarations for Buddy Allocator
static void pmm_buddy_free_locked(vm_page_t *page, int order);
static vm_page_t* pmm_buddy_alloc_locked(int order);
static void pmm_buddy_init_range(uint32_t start, uint32_t end);
static void pmm_buddy_enqueue(int order, vm_page_t *page);
static void pmm_buddy_dequeue(int order, vm_page_t *page);

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
    
    // Default 4MB for early-boot structures, but clamped to physical RAM limit
    watermark_end = watermark_base + (4 * 1024 * 1024);
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
            // NOTE: Do NOT call buddy_free here - bitmap is for diagnostics only
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
        // Use proper buddy initialization for fallback range
        extern uint32_t _kernel_end;
        uint32_t k_end = ((uint32_t)(uintptr_t)&_kernel_end - 0xC0000000);
        k_end = (k_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
        pmm_buddy_init_range(k_end, 16 * 1024 * 1024);
        pmm_reserve_kernel();
        return;
    }

    // Phase 2: Initialize buddy allocator with usable memory ranges
    // First, reserve kernel memory
    pmm_reserve_kernel();
    
    // Then add usable ranges to buddy allocator
    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)(uintptr_t)mmap_addr;
    while ((uint32_t)(uintptr_t)mmap < mmap_addr + mmap_length) {
        if (pmm_is_usable_type(mmap->type)) {
            uint32_t start = pmm_clamp_addr(mmap->addr);
            uint32_t end = pmm_clamp_addr(mmap->addr + mmap->len);
            
            // Cap to total blocks
            if (end > pmm_total_blocks * PMM_BLOCK_SIZE) {
                end = pmm_total_blocks * PMM_BLOCK_SIZE;
            }
            
            // Skip first 1MB (BIOS/Multiboot) and kernel region
            extern uint32_t _kernel_end;
            uint32_t k_end = ((uint32_t)(uintptr_t)&_kernel_end - 0xC0000000);
            k_end = (k_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
            
            // Skip watermark region too
            uint32_t wm_end = (watermark_ptr + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
            
            uint32_t safe_start = start;
            if (safe_start < wm_end) safe_start = wm_end;
            
            if (safe_start < end) {
                pmm_buddy_init_range(safe_start, end);
            }
        }
        mmap = (multiboot_mmap_entry_t*)((uint32_t)(uintptr_t)mmap + mmap->size + sizeof(mmap->size));
    }
}

// Initialize buddy allocator for a contiguous range of usable memory
// This properly coalesces pages into the largest possible buddy blocks
static void pmm_buddy_init_range(uint32_t start, uint32_t end) {
    // Align start up, end down to page boundaries
    start = (start + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    end = end & ~(PMM_BLOCK_SIZE - 1);
    
    if (start >= end) return;
    
    uint32_t addr = start;
    while (addr < end) {
        // Find the largest order block that fits and is naturally aligned
        int order = 0;
        while (order < PMM_MAX_ORDER - 1) {
            uint32_t block_size = (1 << (order + 1)) * PMM_BLOCK_SIZE;
            // Check alignment and fit
            if ((addr & (block_size - 1)) != 0) break;  // Not aligned for larger order
            if (addr + block_size > end) break;          // Doesn't fit
            order++;
        }
        
        // Add this block to the buddy free list
        vm_page_t *page = pmm_get_page(addr);
        if (page) {
            // Clear used bits in bitmap for the entire block
            for (uint32_t i = 0; i < (1U << order); i++) {
                uint32_t pa = addr + (i * PMM_BLOCK_SIZE);
                uint32_t block = pa / PMM_BLOCK_SIZE;
                uint32_t idx = block / 8;
                uint32_t bit = block % 8;
                if (idx < pmm_bitmap_size) {
                    pmm_bitmap[idx] &= ~(1 << bit);
                    pmm_used_blocks--;
                }
            }
            pmm_buddy_enqueue(order, page);
            pmm_free_count += (1 << order);
        }
        
        addr += (1 << order) * PMM_BLOCK_SIZE;
    }
}

vm_page_t *pmm_get_page(uintptr_t pa) {
    size_t idx = pa / PMM_BLOCK_SIZE;
    if (idx < pmm_total_pages) {
        return &pmm_page_array[idx];
    }
    return NULL;
}

// ==================== Buddy Allocator Implementation ====================

// Local helpers for queue management (same as vm_page.c but for pmm_buddy_free_lists)
static void pmm_buddy_enqueue(int order, vm_page_t *page) {
    page->next = pmm_buddy_free_lists[order];
    page->prev = NULL;
    if (pmm_buddy_free_lists[order]) {
        pmm_buddy_free_lists[order]->prev = page;
    }
    pmm_buddy_free_lists[order] = page;
    page->order = order;
    page->flags |= PG_FREE;
}

static void pmm_buddy_dequeue(int order, vm_page_t *page) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        pmm_buddy_free_lists[order] = page->next;
    }
    if (page->next) {
        page->next->prev = page->prev;
    }
    page->next = NULL;
    page->prev = NULL;
    page->flags &= ~PG_FREE;
}

// Low-level buddy allocation (requires lock held)
static vm_page_t* pmm_buddy_alloc_locked(int order) {
    if (order >= PMM_MAX_ORDER) return NULL;

    // 1. Find a block at target order or higher
    for (int i = order; i < PMM_MAX_ORDER; i++) {
        if (pmm_buddy_free_lists[i]) {
            vm_page_t *page = pmm_buddy_free_lists[i];
            pmm_buddy_dequeue(i, page);

            // 2. Split blocks down to requested order
            while (i > order) {
                i--;
                // Brother buddy is half-way through the block
                uint32_t buddy_pa = page->phys_addr + ((1 << i) * PMM_BLOCK_SIZE);
                vm_page_t *buddy = pmm_get_page(buddy_pa);
                if (buddy) {
                    pmm_buddy_enqueue(i, buddy);
                }
            }
            
            pmm_free_count -= (1 << order);
            return page;
        }
    }
    return NULL;
}

// Low-level buddy free (requires lock held)
static void pmm_buddy_free_locked(vm_page_t *page, int order) {
    if (!page || order >= PMM_MAX_ORDER) return;

    pmm_free_count += (1 << order);

    while (order < PMM_MAX_ORDER - 1) {
        // Calculate buddy address: bitwise XOR of the block address with its size
        uint32_t buddy_pa = page->phys_addr ^ ((1 << order) * PMM_BLOCK_SIZE);
        vm_page_t *buddy = pmm_get_page(buddy_pa);

        // Can only merge if buddy exists, is free, and is the SAME order
        if (buddy && (buddy->flags & PG_FREE) && (buddy->order == order)) {
            // Found buddy, merge them!
            pmm_buddy_dequeue(order, buddy);
            
            // The combined block starts at the lower address
            if (buddy->phys_addr < page->phys_addr) {
                page = buddy;
            }
            order++;
        } else {
            // No more merging possible
            break;
        }
    }

    pmm_buddy_enqueue(order, page);
}

static int pmm_get_order(size_t count) {
    int order = 0;
    while ((1UL << order) < count) {
        order++;
    }
    return order;
}

void* pmm_alloc_block(void) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&pmm_lock);

    vm_page_t *page = pmm_buddy_alloc_locked(0);
    
    if (page) {
        // Still track in bitmap for safety/diagnostic compatibility
        pmm_mark_used(page->phys_addr);
        
        uint32_t phys_addr = page->phys_addr;
        spinlock_release(&pmm_lock);
        intr_restore(flags);
        // Return virtual address (kernel direct mapping at 0xC0000000)
        return (void*)(uintptr_t)(phys_addr + 0xC0000000);
    }
    
    spinlock_release(&pmm_lock);
    intr_restore(flags);
    return NULL; 
}

void pmm_free_block(void* p) {
    uint32_t virt_addr = (uint32_t)(uintptr_t)p;
    if (virt_addr == 0) return;
    
    // Convert virtual to physical (kernel direct mapping)
    uint32_t phys_addr = virt_addr - 0xC0000000;
    
    uint32_t flags = intr_disable();
    spinlock_acquire(&pmm_lock);

    vm_page_t *page = pmm_get_page(phys_addr);
    if (page && !(page->flags & PG_FREE)) {
        // Update bitmap for diagnostics
        pmm_mark_free(phys_addr);
        // Free to buddy system
        pmm_buddy_free_locked(page, 0);
    }

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
    if (count == 0) return NULL;
    int order = pmm_get_order(count);
    
    uint32_t flags = intr_disable();
    spinlock_acquire(&pmm_lock);

    vm_page_t *page = pmm_buddy_alloc_locked(order);
    
    if (page) {
        // Mark all pages as used in bitmap
        for (size_t i = 0; i < (1UL << order); i++) {
            pmm_mark_used(page->phys_addr + (i * PMM_BLOCK_SIZE));
        }
        
        uint32_t phys_addr = page->phys_addr;
        spinlock_release(&pmm_lock);
        intr_restore(flags);
        // Return virtual address (kernel direct mapping at 0xC0000000)
        return (void*)(uintptr_t)(phys_addr + 0xC0000000);
    }

    spinlock_release(&pmm_lock);
    intr_restore(flags);
    return NULL;
}

void pmm_free_contiguous(void* p, size_t count) {
    if (!p || count == 0) return;
    int order = pmm_get_order(count);
    uint32_t virt_addr = (uint32_t)(uintptr_t)p;
    // Convert virtual to physical (kernel direct mapping)
    uint32_t phys_addr = virt_addr - 0xC0000000;

    uint32_t flags = intr_disable();
    spinlock_acquire(&pmm_lock);

    vm_page_t *page = pmm_get_page(phys_addr);
    if (page) {
         // Mark all pages as free in bitmap
        for (size_t i = 0; i < (1UL << order); i++) {
            // We use a modified loop because pmm_mark_free would call buddy_free 
            // per block, which we don't want here (we want to free the whole block).
            // Actually, pmm_mark_free currently calls pmm_buddy_free_locked(page, 0).
            // If we are freeing a contiguous block of order N, we should call buddy_free(page, N).
            
            uint32_t p_addr = phys_addr + (i * PMM_BLOCK_SIZE);
            uint32_t block = p_addr / PMM_BLOCK_SIZE;
            uint32_t idx = block / 8;
            uint32_t bit = block % 8;
            if (idx < pmm_bitmap_size && (pmm_bitmap[idx] & (1 << bit))) {
                pmm_bitmap[idx] &= ~(1 << bit);
                pmm_used_blocks--;
            }
        }
        pmm_buddy_free_locked(page, order);
    }

    intr_restore(flags);
}

size_t pmm_get_used_blocks(void) {
    return pmm_used_blocks;
}
