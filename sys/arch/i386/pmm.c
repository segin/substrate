#include "pmm.h"
#include "../x86-common/include/multiboot.h"
#include "../x86-common/include/e820.h" // Updated path
#include "intr.h"
#include "../../vm/vm_page.h"
#include "../../vm/phys_mem.h" // Generic PMM
#include <string.h>
#include <stdio.h>
#include <sys/lock.h>
#include "../../kern/console.h"

// Note: pmm_lock is no longer needed if we delegate strictly to vm_phys
// However, if we have local state, we might need it.
// Watermark allocator has its own implicit "single threaded boot" assumption usually.
// But we'll keep it simple.

// ==================== PMM Data Structures ====================
// Static bitmap for 128MB (Fallback)
static uint8_t pmm_bitmap_static[4096];

// ==================== Watermark (Bump) Allocator ====================
static uint32_t watermark_base;
static uint32_t watermark_ptr;
static uint32_t watermark_end;

void pmm_watermark_init(uint32_t phys_limit) {
    extern uint32_t _kernel_end;
    uint32_t v_end = (uint32_t)(uintptr_t)&_kernel_end;
    uint32_t p_end = v_end - 0xC0000000;
    
    watermark_base = (p_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    watermark_ptr = watermark_base;
    watermark_end = watermark_base + (4 * 1024 * 1024);
    if (watermark_end > phys_limit) watermark_end = phys_limit;
}

void* pmm_watermark_alloc(size_t bytes) {
    bytes = (bytes + 15) & ~15;
    if (watermark_ptr + bytes > watermark_end) return NULL;
    uint32_t result = watermark_ptr;
    watermark_ptr += bytes;
    return (void*)(uintptr_t)(result + 0xC0000000);
}

uint32_t pmm_watermark_used(void) {
    return watermark_ptr - watermark_base;
} 

// ==================== wrappers for Legacy PMM API ====================

// These used to handle the bitmap directly. Now vm_phys handles generic bitmap.
// But pmm.c exposed pmm_mark_used/free.
// We should forward them or deprecate them.
// Used by pmm_reserve_kernel.

static void pmm_mark_used(uint32_t addr) {
    vm_phys_mark_used(addr);
}

// pmm_init_bitmap removed (logic moved to pmm_init)

static void pmm_reserve_kernel(void) {
    extern uint32_t _kernel_end;

    for (uint32_t i = 0; i < 0x100000; i += PMM_BLOCK_SIZE) {
        pmm_mark_used(i);
    }
    
    uint32_t v_end = (uint32_t)(uintptr_t)&_kernel_end;
    uint32_t p_end = v_end - 0xC0000000;
    
    for (uint32_t i = 0x100000; i < p_end; i += PMM_BLOCK_SIZE) {
        pmm_mark_used(i);
    }
}

// Reclaim setup memory
void pmm_reclaim_setup(void) {
    extern uint32_t _setup_start, _setup_end;
    uint32_t start = (uint32_t)(uintptr_t)&_setup_start;
    uint32_t end = (uint32_t)(uintptr_t)&_setup_end;
    
    kprint("Freeing setup memory... ");
    // ... printing logic omitted for brevity, just reclaim ...
    
    // Round start down, end up
     start &= ~(PMM_BLOCK_SIZE - 1);
     end = (end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);

    for (uint32_t i = start; i < end; i += PMM_BLOCK_SIZE) {
        pmm_free_block((void*)(uintptr_t)i);
    }
    kprint("Done.\n");
}

// Validate memory map entry type
static int pmm_is_usable_type(uint32_t type) {
    return (type == MULTIBOOT_MEMORY_AVAILABLE || 
            type == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE);
}

// Clamp 64-bit address to 32-bit
static phys_addr_t pmm_clamp_addr(uint64_t addr) {
    if (addr > 0xFFFFFFFF) return 0xFFFFFFFF;
    return (phys_addr_t)addr;
}

void pmm_walk_mmap(uint32_t mmap_addr, uint32_t mmap_length, pmm_region_callback cb, void *arg) {
    if (!mmap_addr || !mmap_length || !cb) return;

    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)(uintptr_t)mmap_addr;
    uint32_t end_of_map = mmap_addr + mmap_length;

    while ((uint32_t)(uintptr_t)mmap < end_of_map) {
        if (pmm_is_usable_type(mmap->type)) {
            phys_addr_t start = pmm_clamp_addr(mmap->addr);
            phys_addr_t end = pmm_clamp_addr(mmap->addr + mmap->len);
            
            if (end > start) {
                cb(start, end - start, arg);
            }
        }
        mmap = (multiboot_mmap_entry_t*)((uint32_t)(uintptr_t)mmap + mmap->size + sizeof(mmap->size));
    }
}

struct pmm_stats_ctx {
    uint64_t max_phys;
    uint64_t total_usable;
};

static void pmm_cb_stats(phys_addr_t start, phys_addr_t length, void *arg) {
    struct pmm_stats_ctx *ctx = arg;
    if (start + length > ctx->max_phys) {
        ctx->max_phys = start + length;
    }
    ctx->total_usable += length;
}

static void pmm_cb_init_buddy(phys_addr_t start, phys_addr_t length, void *arg) {
    (void)arg;
    phys_addr_t end = start + length;
    
    // Safety clamps (Kernel, Watermark)
    extern uint32_t _kernel_end;
    uint32_t k_phys_end = ((uint32_t)(uintptr_t)&_kernel_end - 0xC0000000);
    k_phys_end = (k_phys_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    
    uint32_t wm_end = (watermark_ptr + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    
    uint32_t safe_start = start;
    
    // If overlaps kernel/watermark, skip
    if (safe_start < k_phys_end) safe_start = k_phys_end;
    if (safe_start < wm_end) safe_start = wm_end;
    
    if (safe_start < end) {
        vm_phys_add_range(safe_start, end);
    }
}

void pmm_init(uint32_t mmap_addr, uint32_t mmap_length) {
    // 0. No more local lock init (vm_phys has its own)

    // 1. Pass 1: Find limits
    struct pmm_stats_ctx stats = { .max_phys = 0x1000000, .total_usable = 0 };
    if (mmap_addr && mmap_length) {
        pmm_walk_mmap(mmap_addr, mmap_length, pmm_cb_stats, &stats);
    }

    // 2. Init Watermark
    pmm_watermark_init((uint32_t)stats.max_phys);
    
    uint32_t max_phys_addr = (uint32_t)stats.max_phys;
    if (max_phys_addr & (PMM_BLOCK_SIZE - 1)) {
        max_phys_addr = (max_phys_addr + PMM_BLOCK_SIZE) & ~(PMM_BLOCK_SIZE - 1);
    }
    
    size_t total_blocks = max_phys_addr / PMM_BLOCK_SIZE;
    size_t bitmap_bytes = (total_blocks + 7) / 8;
    
    uint8_t* pmm_bitmap = (uint8_t*)pmm_watermark_alloc(bitmap_bytes);
    size_t array_bytes = total_blocks * sizeof(vm_page_t);
    vm_page_t* pmm_page_array = (vm_page_t*)pmm_watermark_alloc(array_bytes);
    
    if (!pmm_bitmap) {
        // Fallback
         kprint("PMM: Warn - using static bitmap.\n");
         pmm_bitmap = pmm_bitmap_static;
         bitmap_bytes = sizeof(pmm_bitmap_static);
         total_blocks = bitmap_bytes * 8;
         pmm_page_array = NULL; // No page array in fallback? Or alloc smaller?
         // vm_phys handles NULL page array gracefully (lookup returns NULL)
    }

    // 3. Init Generic PMM
    // We pass the "total_page_count" as total_blocks
    vm_phys_early_init(pmm_bitmap, bitmap_bytes, pmm_page_array, total_blocks);

    char buf[64];
    sprintf(buf, "%u MB RAM detected.\n", max_phys_addr / (1024 * 1024));
    kprint(buf);

    if (mmap_addr == 0 || mmap_length == 0) {
        kprint("PMM: Fallback 16MB.\n");
        // Fallback init...
        // ... (Similar to before)
         extern uint32_t _kernel_end;
        uint32_t k_end = ((uint32_t)(uintptr_t)&_kernel_end - 0xC0000000);
        k_end = (k_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
        vm_phys_add_range(k_end, 16 * 1024 * 1024);
        pmm_reserve_kernel();
        return;
    }

    // 4. Init Ranges via Buddy using Iterator
    pmm_reserve_kernel();
    pmm_walk_mmap(mmap_addr, mmap_length, pmm_cb_init_buddy, NULL);
    pmm_reserve_kernel(); // Re-safety
    
    // Explicitly mark watermark as used (generic PMM bitmap marking)
    uint32_t wm_start = watermark_base;
    uint32_t wm_end = watermark_ptr;
    // Iterate blocks
    for (uint32_t a = wm_start; a < wm_end; a+=PMM_BLOCK_SIZE) {
        vm_phys_mark_used(a);
    }
}

// Allocation Hooks
void* pmm_alloc_block(void) {
    vm_page_t *p = vm_phys_alloc_page();
    if (!p) return NULL;
    return (void*)(uintptr_t)(p->phys_addr + 0xC0000000);
}

void pmm_free_block(void* p) {
    uintptr_t v = (uintptr_t)p;
    if (v == 0) return;
    vm_page_t *page = vm_phys_paddr_to_page(v - 0xC0000000);
    vm_phys_free_page(page);
}

void* pmm_alloc_contiguous(size_t count) {
    vm_page_t *p = vm_phys_alloc_contiguous(count);
    if (!p) return NULL;
    return (void*)(uintptr_t)(p->phys_addr + 0xC0000000);
}

void pmm_free_contiguous(void* p, size_t count) {
     uintptr_t v = (uintptr_t)p;
     if (!v) return;
     vm_page_t *page = vm_phys_paddr_to_page(v - 0xC0000000);
     vm_phys_free_contiguous(page, count);
}

size_t pmm_get_used_blocks(void) {
    return vm_phys_get_used();
}

void pmm_reclaim_range(uint32_t start, uint32_t end) {
    vm_phys_add_range(start, end);
}

struct vm_page *pmm_get_page(uintptr_t pa) {
    return vm_phys_paddr_to_page(pa);
}

void pmm_dump_mmap(uint32_t mmap_addr, uint32_t mmap_length) {
    // Keep existing dump implementation
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
