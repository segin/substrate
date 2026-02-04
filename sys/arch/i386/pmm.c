#include <arch/i386/pmm.h>
#include <arch/x86-common/include/multiboot.h>
#include <arch/x86-common/include/e820.h>
#include "intr.h"
#include <vm/vm_page.h>
#include <vm/phys_mem.h> // Generic PMM
#include <string.h>
#include <stdio.h>
#include <sys/lock.h>
#include <kern/console.h>

// Locking Strategy:
// The machine-dependent PMM (this file) primarily delegates to the Machine Independent
// `vm_phys` subsystem for page allocation and management. The `vm_phys` layer implements
// its own fine-grained locking (`vm_phys_lock`) to protect the free lists and page queues.
// 
// Local pmm_lock is reserved for protecting architecture-specific metadata if necessary,
// though currently most state is managed by `vm_phys`. The watermark allocator runs
// only during single-threaded early boot, thus requiring no explicit synchronization.

// ==================== PMM Data Structures ====================
// Static bitmap for 128MB (Fallback)
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

/*
 * pmm_validate_mmap_entry - Validate a multiboot memory map entry
 *
 * Returns 0 if entry is valid and usable, non-zero to skip.
 * Validates: type, non-zero length, address range within 32-bit.
 */
static int pmm_validate_mmap_entry(const multiboot_mmap_entry_t *entry,
                                   phys_addr_t *out_start, phys_addr_t *out_end) {
    /* Check for usable memory types */
    if (!pmm_is_usable_type(entry->type)) {
        return -1; /* Not usable type */
    }
    
    /* Skip zero-length entries */
    if (entry->len == 0) {
        return -2; /* Zero length */
    }
    
    /* Clamp 64-bit addresses to 32-bit address space */
    uint64_t start64 = entry->addr;
    uint64_t end64 = entry->addr + entry->len;
    
    /* Check for 64-bit wrap-around */
    if (end64 < start64) {
        return -3; /* Wrap-around detected */
    }
    
    /* Skip entries entirely above 4GB (32-bit limit) */
    if (start64 >= 0x100000000ULL) {
        return -4; /* Above 32-bit space */
    }
    
    /* Clamp end to 4GB boundary */
    if (end64 > 0xFFFFFFFFULL) {
        end64 = 0xFFFFFFFFULL;
    }
    
    phys_addr_t start = (phys_addr_t)start64;
    phys_addr_t end = (phys_addr_t)end64;
    
    /* Final sanity check */
    if (end <= start) {
        return -5; /* Invalid after clamping */
    }
    
    *out_start = start;
    *out_end = end;
    return 0; /* Valid */
}

/*
 * pmm_walk_mmap - Iterate over multiboot memory map entries
 *
 * Walks the multiboot memory map, validates each entry, and calls
 * the callback for each valid usable region.
 *
 * @mmap_addr: Physical address of memory map (kernel-mapped)
 * @mmap_length: Total length of memory map in bytes
 * @cb: Callback function for each valid region
 * @arg: Opaque argument passed to callback
 *
 * Safety features:
 * - Validates entry size field to prevent infinite loops
 * - Skips zero-length entries
 * - Clamps 64-bit addresses to 32-bit
 * - Detects wrap-around in entry iteration
 */
void pmm_walk_mmap(uint32_t mmap_addr, uint32_t mmap_length, pmm_region_callback cb, void *arg) {
    if (!mmap_addr || !mmap_length || !cb) {
        return;
    }

    /* Validate map address is in kernel space (already mapped) */
    if (mmap_addr < 0xC0000000) {
        /* Physical address - need to add kernel offset */
        mmap_addr += 0xC0000000;
    }

    const uint8_t *map_start = (const uint8_t *)(uintptr_t)mmap_addr;
    const uint8_t *map_end = map_start + mmap_length;
    const uint8_t *ptr = map_start;
    
    uint32_t entries_processed = 0;
    const uint32_t max_entries = 256; /* Sanity limit */

    while (ptr < map_end && entries_processed < max_entries) {
        const multiboot_mmap_entry_t *entry = (const multiboot_mmap_entry_t *)ptr;
        
        /* Validate entry size field (must be at least the structure minus size field) */
        uint32_t entry_size = entry->size + sizeof(entry->size);
        if (entry_size < sizeof(multiboot_mmap_entry_t)) {
            /* Malformed entry - stop processing */
            break;
        }
        
        /* Prevent infinite loop from zero-size entry */
        if (entry->size == 0) {
            break;
        }
        
        /* Validate and process entry */
        phys_addr_t start, end;
        if (pmm_validate_mmap_entry(entry, &start, &end) == 0) {
            cb(start, end - start, arg);
        }
        
        /* Advance to next entry */
        ptr += entry_size;
        entries_processed++;
        
        /* Detect wrap-around in pointer */
        if (ptr < map_start) {
            break;
        }
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

uint32_t pmm_get_total_memory(void) {
    // vm_phys_get_used() + vm_phys_get_free() = total
    return (uint32_t)((vm_phys_get_used() + vm_phys_get_free()) * PMM_BLOCK_SIZE);
}

uint32_t pmm_get_free_memory(void) {
    return (uint32_t)(vm_phys_get_free() * PMM_BLOCK_SIZE);
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

// Redundant definitions removed

void pmm_reclaim_range(uint32_t start, uint32_t end) {
    vm_phys_add_range(start, end);
}

struct vm_page *pmm_get_page(uintptr_t pa) {
    return vm_phys_paddr_to_page(pa);
}

// Redundant definitions removed



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

/* ==================== E820 Memory Map Support ==================== */

/*
 * pmm_is_e820_usable_type - Check if e820 type is usable memory
 */
static int pmm_is_e820_usable_type(uint32_t type) {
    return (type == E820_USABLE);
}

/*
 * pmm_validate_e820_entry - Validate an e820 memory map entry
 *
 * Returns 0 if entry is valid and usable, non-zero to skip.
 * Validates: type, non-zero length, address range within 32-bit.
 */
static int pmm_validate_e820_entry(const e820_entry_t *entry,
                                   phys_addr_t *out_start, phys_addr_t *out_end) {
    /* Check for usable memory types */
    if (!pmm_is_e820_usable_type(entry->type)) {
        return -1; /* Not usable type */
    }
    
    /* Skip zero-length entries */
    if (entry->len == 0) {
        return -2; /* Zero length */
    }
    
    /* Clamp 64-bit addresses to 32-bit address space */
    uint64_t start64 = entry->addr;
    uint64_t end64 = entry->addr + entry->len;
    
    /* Check for 64-bit wrap-around */
    if (end64 < start64) {
        return -3; /* Wrap-around detected */
    }
    
    /* Skip entries entirely above 4GB (32-bit limit) */
    if (start64 >= 0x100000000ULL) {
        return -4; /* Above 32-bit space */
    }
    
    /* Clamp end to 4GB boundary */
    if (end64 > 0xFFFFFFFFULL) {
        end64 = 0xFFFFFFFFULL;
    }
    
    phys_addr_t start = (phys_addr_t)start64;
    phys_addr_t end = (phys_addr_t)end64;
    
    /* Final sanity check */
    if (end <= start) {
        return -5; /* Invalid after clamping */
    }
    
    *out_start = start;
    *out_end = end;
    return 0; /* Valid */
}

/*
 * pmm_walk_e820 - Iterate over e820 memory map entries
 *
 * Walks the e820 memory map (array of fixed-size entries), validates
 * each entry, and calls the callback for each valid usable region.
 *
 * @map: Pointer to e820 entry array
 * @count: Number of entries in the array
 * @cb: Callback function for each valid region
 * @arg: Opaque argument passed to callback
 *
 * Safety features:
 * - Validates each entry before processing
 * - Skips zero-length entries
 * - Clamps 64-bit addresses to 32-bit
 * - Limits to max 256 entries
 */
void pmm_walk_e820(const e820_entry_t *map, uint32_t count, 
                   pmm_region_callback cb, void *arg) {
    if (!map || !count || !cb) {
        return;
    }
    
    /* Sanity limit on entry count */
    if (count > 256) {
        count = 256;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        phys_addr_t start, end;
        if (pmm_validate_e820_entry(&map[i], &start, &end) == 0) {
            cb(start, end - start, arg);
        }
    }
}

/*
 * pmm_dump_e820 - Print e820 memory map for debugging
 */
void pmm_dump_e820(const e820_entry_t *map, uint32_t count) {
    kprint("E820 Memory Map:\n");
    
    for (uint32_t i = 0; i < count && i < 64; i++) {
        char buf[128];
        const char *type_str = "unknown";
        
        switch (map[i].type) {
        case E820_USABLE:   type_str = "usable"; break;
        case E820_RESERVED: type_str = "reserved"; break;
        case E820_ACPI:     type_str = "ACPI data"; break;
        case E820_NVS:      type_str = "ACPI NVS"; break;
        case E820_BAD:      type_str = "bad RAM"; break;
        }
        
        uint64_t start = map[i].addr;
        uint64_t end = map[i].addr + map[i].len - 1;
        
        sprintf(buf, " [0x%08x%08x - 0x%08x%08x] %s\n", 
            (uint32_t)(start >> 32), (uint32_t)(start & 0xFFFFFFFF),
            (uint32_t)(end >> 32), (uint32_t)(end & 0xFFFFFFFF),
            type_str);
        kprint(buf);
    }
}

// Redundant definitions removed

/*
 * pmm_init_e820 - Initialize PMM from e820 memory map
 *
 * This is the entry point for legacy BIOS systems that provide
 * memory information via INT 15h E820 instead of Multiboot.
 *
 * @map: Pointer to e820 entry array
 * @count: Number of entries in the array
 */
void pmm_init_e820(e820_entry_t *map, uint32_t count) {
    if (!map || count == 0) {
        kprint("PMM: No e820 map provided, using fallback 16MB\n");
        extern uint32_t _kernel_end;
        uint32_t k_end = ((uint32_t)(uintptr_t)&_kernel_end - 0xC0000000);
        k_end = (k_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
        
        pmm_watermark_init(16 * 1024 * 1024);
        
        /* Minimal init */
        size_t total_blocks = (16 * 1024 * 1024) / PMM_BLOCK_SIZE;
        size_t bitmap_bytes = (total_blocks + 7) / 8;
        uint8_t *pmm_bitmap = (uint8_t *)pmm_watermark_alloc(bitmap_bytes);
        
        if (!pmm_bitmap) {
            pmm_bitmap = pmm_bitmap_static;
            bitmap_bytes = sizeof(pmm_bitmap_static);
            total_blocks = bitmap_bytes * 8;
        }
        
        vm_phys_early_init(pmm_bitmap, bitmap_bytes, NULL, total_blocks);
        vm_phys_add_range(k_end, 16 * 1024 * 1024);
        pmm_reserve_kernel();
        return;
    }
    
    /* Debug dump */
    pmm_dump_e820(map, count);
    
    /* Pass 1: Find limits */
    struct pmm_stats_ctx stats = { .max_phys = 0x1000000, .total_usable = 0 };
    pmm_walk_e820(map, count, pmm_cb_stats, &stats);
    
    /* Init Watermark */
    pmm_watermark_init((uint32_t)stats.max_phys);
    
    uint32_t max_phys_addr = (uint32_t)stats.max_phys;
    if (max_phys_addr & (PMM_BLOCK_SIZE - 1)) {
        max_phys_addr = (max_phys_addr + PMM_BLOCK_SIZE) & ~(PMM_BLOCK_SIZE - 1);
    }
    
    size_t total_blocks = max_phys_addr / PMM_BLOCK_SIZE;
    size_t bitmap_bytes = (total_blocks + 7) / 8;
    
    uint8_t *pmm_bitmap = (uint8_t *)pmm_watermark_alloc(bitmap_bytes);
    size_t array_bytes = total_blocks * sizeof(vm_page_t);
    vm_page_t *pmm_page_array = (vm_page_t *)pmm_watermark_alloc(array_bytes);
    
    if (!pmm_bitmap) {
        kprint("PMM: Warn - using static bitmap.\n");
        pmm_bitmap = pmm_bitmap_static;
        bitmap_bytes = sizeof(pmm_bitmap_static);
        total_blocks = bitmap_bytes * 8;
        pmm_page_array = NULL;
    }
    
    /* Init Generic PMM */
    vm_phys_early_init(pmm_bitmap, bitmap_bytes, pmm_page_array, total_blocks);
    
    char buf[64];
    sprintf(buf, "%u MB RAM detected.\n", max_phys_addr / (1024 * 1024));
    kprint(buf);
    
    /* Reserve kernel and add usable ranges */
    pmm_reserve_kernel();
    pmm_walk_e820(map, count, pmm_cb_init_buddy, NULL);
    pmm_reserve_kernel(); /* Re-safety */
    
    /* Mark watermark as used */
    uint32_t wm_start = watermark_base;
    uint32_t wm_end_val = watermark_ptr;
    for (uint32_t a = wm_start; a < wm_end_val; a += PMM_BLOCK_SIZE) {
        vm_phys_mark_used(a);
    }
}
