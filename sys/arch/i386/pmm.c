#include <arch/i386/pmm.h>
#include <arch/x86-common/multiboot.h>
#include <arch/x86-common/e820.h>
#include "intr.h"
#include <vm/vm_page.h>
#include <vm/phys_mem.h> // Generic PMM
#include <string.h>
#include <stdio.h>
#include <sys/lock.h>
#include <kern/console.h>

/*
 * Locking Strategy:
 * The machine-dependent PMM (this file) primarily delegates to the Machine Independent
 * `vm_phys` subsystem for page allocation and management. The `vm_phys` layer implements
 * its own fine-grained locking (`vm_phys_lock`) to protect the free lists and page queues.
 * 
 * Local pmm_lock is reserved for protecting architecture-specific metadata if necessary,
 * though currently most state is managed by `vm_phys`. The watermark allocator runs
 * only during single-threaded early boot, thus requiring no explicit synchronization.
 */

// ==================== PMM Data Structures ====================
// Static bitmap for 128MB (Fallback)
static uint8_t pmm_bitmap_static[4096];

// ==================== Boot Memory Detection State ====================

/* Memory region tracking for overlap detection and validation */
#define PMM_MAX_REGIONS 128

typedef struct pmm_region {
    phys_addr_t start;
    phys_addr_t end;
    uint32_t type;
    int valid;
} pmm_region_t;

static pmm_region_t pmm_regions[PMM_MAX_REGIONS];
static int pmm_region_count = 0;
static uint64_t pmm_total_usable_ram = 0;
static uint64_t pmm_total_reserved_ram = 0;

/* Kernel bounds from linker symbols */
static phys_addr_t kernel_phys_start = 0;
static phys_addr_t kernel_phys_end = 0;

/* Multiboot info bounds */
static phys_addr_t mboot_info_start = 0;
static phys_addr_t mboot_info_end = 0;

/* Module bounds */
static phys_addr_t module_regions_start[8];
static phys_addr_t module_regions_end[8];
static int module_region_count = 0;

// ==================== Boot Memory Detection Helpers ====================

/*
 * pmm_init_kernel_bounds - Initialize kernel physical bounds from linker symbols
 *
 * Uses both _kernel_start and _kernel_end to properly identify the kernel
 * region that must be excluded from the free memory pool.
 */
static void pmm_init_kernel_bounds(void) {
    extern uint32_t _kernel_start;
    extern uint32_t _kernel_end;
    
    uint32_t v_start = (uint32_t)(uintptr_t)&_kernel_start;
    uint32_t v_end = (uint32_t)(uintptr_t)&_kernel_end;
    
    kernel_phys_start = v_start - 0xC0000000;
    kernel_phys_end = v_end - 0xC0000000;
    
    /* Round to page boundaries for safety */
    kernel_phys_start &= ~(PMM_BLOCK_SIZE - 1);
    kernel_phys_end = (kernel_phys_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
}

/*
 * pmm_record_multiboot_info - Record Multiboot info structure bounds
 *
 * The Multiboot info structure and its pointed data (mmap, modules, cmdline)
 * must be preserved until we've finished using them.
 */
static void pmm_record_multiboot_info(uint32_t mboot_addr) {
    if (!mboot_addr) return;
    
    /* Convert to physical if virtual */
    if (mboot_addr >= 0xC0000000) {
        mboot_addr -= 0xC0000000;
    }
    
    mboot_info_start = mboot_addr;
    mboot_info_end = mboot_addr + sizeof(multiboot_info_t);
    
    /* Round to page boundaries */
    mboot_info_start &= ~(PMM_BLOCK_SIZE - 1);
    mboot_info_end = (mboot_info_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
}

/*
 * pmm_record_module_regions - Record module memory regions
 *
 * Modules loaded by the bootloader must not be overwritten.
 */
static void pmm_record_module_regions(uint32_t mods_addr, uint32_t mods_count) {
    if (!mods_addr || mods_count == 0) return;
    
    module_region_count = 0;
    
    /* Convert to virtual if physical */
    if (mods_addr < 0xC0000000) {
        mods_addr += 0xC0000000;
    }
    
    /* Limit to our array size */
    if (mods_count > 8) mods_count = 8;
    
    for (uint32_t i = 0; i < mods_count && module_region_count < 8; i++) {
        uint32_t *mod_entry = (uint32_t *)(uintptr_t)(mods_addr + i * 16);
        uint32_t mod_start = mod_entry[0];
        uint32_t mod_end = mod_entry[1];
        
        if (mod_start < mod_end) {
            /* Convert to virtual if physical */
            if (mod_start < 0xC0000000) mod_start += 0xC0000000;
            if (mod_end < 0xC0000000) mod_end += 0xC0000000;
            
            /* Round to page boundaries */
            mod_start &= ~(PMM_BLOCK_SIZE - 1);
            mod_end = (mod_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
            
            module_regions_start[module_region_count] = mod_start;
            module_regions_end[module_region_count] = mod_end;
            module_region_count++;
        }
    }
}

/*
 * pmm_add_region - Add a memory region to tracking array
 *
 * Returns 0 on success, -1 if region array is full.
 */
static int pmm_add_region(phys_addr_t start, phys_addr_t end, uint32_t type) {
    if (pmm_region_count >= PMM_MAX_REGIONS) {
        return -1;
    }
    
    pmm_regions[pmm_region_count].start = start;
    pmm_regions[pmm_region_count].end = end;
    pmm_regions[pmm_region_count].type = type;
    pmm_regions[pmm_region_count].valid = 1;
    pmm_region_count++;
    
    return 0;
}

/*
 * pmm_check_overlap - Check if a region overlaps with any existing region
 *
 * Returns 1 if overlap detected, 0 otherwise. If overlap is detected,
 * out_start and out_end are set to the overlapping range.
 */
static int pmm_check_overlap(phys_addr_t start, phys_addr_t end,
                             phys_addr_t *out_start, phys_addr_t *out_end) {
    for (int i = 0; i < pmm_region_count; i++) {
        if (!pmm_regions[i].valid) continue;
        
        phys_addr_t r_start = pmm_regions[i].start;
        phys_addr_t r_end = pmm_regions[i].end;
        
        /* Check for overlap: [start, end) overlaps with [r_start, r_end) */
        if (start < r_end && end > r_start) {
            if (out_start) *out_start = (start > r_start) ? start : r_start;
            if (out_end) *out_end = (end < r_end) ? end : r_end;
            return 1;
        }
    }
    return 0;
}

/*
 * pmm_reserve_regions - Mark all tracked regions as used in the PMM
 *
 * This reserves kernel, Multiboot info, modules, and reserved memory types.
 */
static void pmm_reserve_regions(void) {
    for (int i = 0; i < pmm_region_count; i++) {
        if (!pmm_regions[i].valid) continue;
        
        uint32_t type = pmm_regions[i].type;
        phys_addr_t start = pmm_regions[i].start;
        phys_addr_t end = pmm_regions[i].end;
        
        /* Reserve non-usable regions */
        if (type != MULTIBOOT_MEMORY_AVAILABLE && type != E820_USABLE) {
            for (phys_addr_t addr = start; addr < end; addr += PMM_BLOCK_SIZE) {
                vm_phys_mark_used(addr);
            }
        }
    }
    
    /* Reserve kernel region */
    for (phys_addr_t addr = kernel_phys_start; addr < kernel_phys_end; addr += PMM_BLOCK_SIZE) {
        vm_phys_mark_used(addr);
    }
    
    /* Reserve Multiboot info region */
    if (mboot_info_start < mboot_info_end) {
        for (phys_addr_t addr = mboot_info_start; addr < mboot_info_end; addr += PMM_BLOCK_SIZE) {
            vm_phys_mark_used(addr);
        }
    }
    
    /* Reserve module regions */
    for (int i = 0; i < module_region_count; i++) {
        for (phys_addr_t addr = module_regions_start[i]; 
             addr < module_regions_end[i]; 
             addr += PMM_BLOCK_SIZE) {
            vm_phys_mark_used(addr);
        }
    }
}

/*
 * pmm_report_memory_stats - Report total usable and reserved RAM
 *
 * Uses 64-bit accumulation to handle systems with >4GB physical RAM.
 */
static void pmm_report_memory_stats(void) {
    char buf[128];
    
    /* Report total usable RAM */
    uint64_t usable_mb = pmm_total_usable_ram / (1024 * 1024);
    uint64_t usable_gb = usable_mb / 1024;
    
    if (usable_gb > 0) {
        sprintf(buf, "Total usable RAM: %lu MB (%lu GB)\n", 
                (unsigned long)usable_mb, (unsigned long)usable_gb);
    } else {
        sprintf(buf, "Total usable RAM: %lu MB\n", (unsigned long)usable_mb);
    }
    kprint(buf);
    
    /* Report reserved RAM if significant */
    if (pmm_total_reserved_ram > 0) {
        uint64_t reserved_mb = pmm_total_reserved_ram / (1024 * 1024);
        sprintf(buf, "Reserved RAM: %lu MB\n", (unsigned long)reserved_mb);
        kprint(buf);
    }
    
    /* Report detected regions */
    sprintf(buf, "Memory regions detected: %d\n", pmm_region_count);
    kprint(buf);
}



// ==================== Watermark (Bump) Allocator ====================
static uint32_t watermark_base;
static uint32_t watermark_ptr;
static uint32_t watermark_end;

void pmm_watermark_init(uint32_t start, uint32_t end) {
    watermark_base = (start + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    watermark_ptr = watermark_base;
    watermark_end = end;
}

void* pmm_watermark_alloc(size_t bytes, size_t align) {
    if (align == 0) align = 16;
    watermark_ptr = (watermark_ptr + align - 1) & ~(align - 1);
    
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
    /* Reserve first 1MB (BIOS/legacy area) */
    for (uint32_t i = 0; i < 0x100000; i += PMM_BLOCK_SIZE) {
        pmm_mark_used(i);
    }
    
    /* Reserve kernel image region using proper bounds */
    for (phys_addr_t i = kernel_phys_start; i < kernel_phys_end; i += PMM_BLOCK_SIZE) {
        if (i >= 0x100000) {  /* Already marked below 1MB */
            pmm_mark_used(i);
        }
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
        
        /* Track reserved/ACPI/NVS/BAD regions for statistics */
        if (entry->type == MULTIBOOT_MEMORY_RESERVED || 
            entry->type == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE ||
            entry->type == MULTIBOOT_MEMORY_NVS ||
            entry->type == MULTIBOOT_MEMORY_BADRAM) {
            /* Clamp 64-bit addresses to 32-bit */
            uint64_t r_start64 = entry->addr;
            uint64_t r_end64 = entry->addr + entry->len;
            if (r_start64 >= 0x100000000ULL) {
                /* Skip entries entirely above 4GB */
                ptr += entry_size;
                entries_processed++;
                continue;
            }
            if (r_end64 > 0xFFFFFFFFULL) r_end64 = 0xFFFFFFFFULL;
            pmm_add_region((phys_addr_t)r_start64, (phys_addr_t)r_end64, entry->type);
            pmm_total_reserved_ram += (r_end64 - r_start64);
            ptr += entry_size;
            entries_processed++;
            continue;
        }
        
        /* Validate and process usable entry */
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
    uint64_t total_reserved;
};

static void pmm_cb_stats(phys_addr_t start, phys_addr_t length, void *arg) {
    struct pmm_stats_ctx *ctx = arg;
    if (start + length > ctx->max_phys) {
        ctx->max_phys = start + length;
    }
    ctx->total_usable += length;
    
    /* Check for overlap with existing regions before adding */
    phys_addr_t overlap_start, overlap_end;
    if (pmm_check_overlap(start, start + length, &overlap_start, &overlap_end)) {
        /* Subtract overlap from usable count */
        ctx->total_usable -= (overlap_end - overlap_start);
    }
    
    pmm_add_region(start, start + length, MULTIBOOT_MEMORY_AVAILABLE);
}

static void pmm_cb_init_buddy(phys_addr_t start, phys_addr_t length, void *arg) {
    (void)arg;
    phys_addr_t end = start + length;
    
    /* Use proper kernel bounds from initialized globals */
    uint32_t k_phys_start = kernel_phys_start;
    uint32_t k_phys_end = kernel_phys_end;
    
    uint32_t wm_end = (watermark_ptr + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    
    uint32_t safe_start = start;
    
    /* Skip regions below 1MB (legacy BIOS area) - they're handled specially */
    if (end <= 0x100000) {
        return;
    }
    
    /* Start at 1MB if region begins below it */
    if (safe_start < 0x100000) {
        safe_start = 0x100000;
    }
    
    /* If overlaps kernel, skip to after kernel */
    if (safe_start < k_phys_end && end > k_phys_start) {
        safe_start = k_phys_end;
    }
    
    /* If overlaps watermark, skip to after watermark */
    if (safe_start < wm_end) {
        safe_start = wm_end;
    }
    
    /* Check for overlap with Multiboot info region */
    if (safe_start < mboot_info_end && end > mboot_info_start) {
        if (safe_start < mboot_info_start) {
            /* Add range before multiboot info */
            vm_phys_add_range(safe_start, mboot_info_start);
        }
        safe_start = mboot_info_end;
    }
    
    /* Check for overlap with module regions */
    for (int i = 0; i < module_region_count; i++) {
        phys_addr_t mod_start = module_regions_start[i];
        phys_addr_t mod_end = module_regions_end[i];
        
        if (safe_start < mod_end && end > mod_start) {
            if (safe_start < mod_start) {
                /* Add range before module */
                vm_phys_add_range(safe_start, mod_start);
            }
            safe_start = mod_end;
        }
    }
    
    /* Add remaining range */
    if (safe_start < end) {
        vm_phys_add_range(safe_start, end);
    }
}

void pmm_init(uint32_t mmap_addr, uint32_t mmap_length) {
    // 0. Initialize kernel bounds first (needed for all subsequent operations)
    pmm_init_kernel_bounds();
    
    // 1. Record Multiboot info and module regions for exclusion
    if (mmap_addr) {
        /* mmap_addr is virtual from boot.S, but we might need physical for marking */
        uint32_t mboot_phys = mmap_addr;
        if (mboot_phys >= 0xC0000000) {
            mboot_phys -= 0xC0000000;
        }
        pmm_record_multiboot_info(mboot_phys);
        
        /* Check if we have modules */
        multiboot_info_t *mboot = (multiboot_info_t *)(uintptr_t)mmap_addr;
        if (mboot->flags & MULTIBOOT_INFO_MODS) {
            pmm_record_module_regions(mboot->mods_addr, mboot->mods_count);
        }
    }

    // 2. Pass 1: Find limits with 64-bit accumulation for >4GB systems
    struct pmm_stats_ctx stats = { .max_phys = 0x1000000, .total_usable = 0, .total_reserved = 0 };
    if (mmap_addr && mmap_length) {
        pmm_walk_mmap(mmap_addr, mmap_length, pmm_cb_stats, &stats);
    }
    
    /* Save global stats for reporting */
    pmm_total_usable_ram = stats.total_usable;
    pmm_total_reserved_ram = stats.total_reserved;

    // 3. Init Watermark
    pmm_watermark_init(kernel_phys_end, (uint32_t)stats.max_phys);
    
    uint32_t max_phys_addr = (uint32_t)stats.max_phys;
    if (max_phys_addr & (PMM_BLOCK_SIZE - 1)) {
        max_phys_addr = (max_phys_addr + PMM_BLOCK_SIZE) & ~(PMM_BLOCK_SIZE - 1);
    }
    
    size_t total_blocks = max_phys_addr / PMM_BLOCK_SIZE;
    size_t bitmap_bytes = (total_blocks + 7) / 8;
    
    uint8_t* pmm_bitmap = (uint8_t*)pmm_watermark_alloc(bitmap_bytes, 16);
    size_t array_bytes = total_blocks * sizeof(vm_page_t);
    vm_page_t* pmm_page_array = (vm_page_t*)pmm_watermark_alloc(array_bytes, PMM_BLOCK_SIZE);
    
    if (!pmm_bitmap) {
        // Fallback
         kprint("PMM: Warn - using static bitmap.\n");
         pmm_bitmap = pmm_bitmap_static;
         bitmap_bytes = sizeof(pmm_bitmap_static);
         total_blocks = bitmap_bytes * 8;
         pmm_page_array = NULL; // No page array in fallback? Or alloc smaller?
         // vm_phys handles NULL page array gracefully (lookup returns NULL)
    }

    // 4. Init Generic PMM
    // We pass the "total_page_count" as total_blocks
    vm_phys_early_init(pmm_bitmap, bitmap_bytes, pmm_page_array, total_blocks);

    // 5. Report memory statistics with 64-bit accumulation
    pmm_report_memory_stats();

    if (mmap_addr == 0 || mmap_length == 0) {
        kprint("PMM: Fallback 16MB.\n");
        // Fallback init...
        // ... (Similar to before)
        uint32_t k_end = kernel_phys_end;
        vm_phys_add_range(k_end, 16 * 1024 * 1024);
        pmm_reserve_kernel();
        return;
    }

    // 6. Reserve kernel and excluded regions first
    pmm_reserve_kernel();
    pmm_reserve_regions();
    
    // 7. Init Ranges via Buddy using Iterator (skips reserved areas)
    pmm_walk_mmap(mmap_addr, mmap_length, pmm_cb_init_buddy, NULL);
    
    // 8. Final safety reservation
    pmm_reserve_kernel();
    
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
        
        /* Track reserved/ACPI/NVS regions for statistics */
        if (map[i].type == E820_RESERVED || map[i].type == E820_ACPI || 
            map[i].type == E820_NVS || map[i].type == E820_BAD) {
            /* Clamp to 32-bit for tracking */
            uint64_t r_start64 = map[i].addr;
            uint64_t r_end64 = map[i].addr + map[i].len;
            if (r_start64 < 0x100000000ULL) {
                if (r_end64 > 0x100000000ULL) r_end64 = 0x100000000ULL;
                pmm_add_region((phys_addr_t)r_start64, (phys_addr_t)r_end64, map[i].type);
                pmm_total_reserved_ram += (r_end64 - r_start64);
            }
            continue;  /* Don't process as usable */
        }
        
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
    /* Initialize kernel bounds first */
    pmm_init_kernel_bounds();
    
    if (!map || count == 0) {
        kprint("PMM: No e820 map provided, using fallback 16MB\n");
        uint32_t k_end = kernel_phys_end;
        
        pmm_watermark_init(k_end, 16 * 1024 * 1024);
        
        /* Minimal init */
        size_t total_blocks = (16 * 1024 * 1024) / PMM_BLOCK_SIZE;
        size_t bitmap_bytes = (total_blocks + 7) / 8;
        uint8_t *pmm_bitmap = (uint8_t *)pmm_watermark_alloc(bitmap_bytes, 16); // Assuming 16-byte alignment for bitmap
        
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
    
    /* Pass 1: Find limits with 64-bit accumulation */
    struct pmm_stats_ctx stats = { .max_phys = 0x1000000, .total_usable = 0, .total_reserved = 0 };
    pmm_walk_e820(map, count, pmm_cb_stats, &stats);
    
    /* Save global stats */
    pmm_total_usable_ram = stats.total_usable;
    pmm_total_reserved_ram = stats.total_reserved;
    
    /* Init Watermark */
    pmm_watermark_init(kernel_phys_end, (uint32_t)stats.max_phys);
    
    uint32_t max_phys_addr = (uint32_t)stats.max_phys;
    if (max_phys_addr & (PMM_BLOCK_SIZE - 1)) {
        max_phys_addr = (max_phys_addr + PMM_BLOCK_SIZE) & ~(PMM_BLOCK_SIZE - 1);
    }
    
    size_t total_blocks = max_phys_addr / PMM_BLOCK_SIZE;
    size_t bitmap_bytes = (total_blocks + 7) / 8;
    
    uint8_t* pmm_bitmap = (uint8_t*)pmm_watermark_alloc(bitmap_bytes, 16);
    size_t array_bytes = total_blocks * sizeof(vm_page_t);
    vm_page_t* pmm_page_array = (vm_page_t*)pmm_watermark_alloc(array_bytes, PMM_BLOCK_SIZE);
    
    if (!pmm_bitmap) {
        kprint("PMM: Warn - using static bitmap.\n");
        pmm_bitmap = pmm_bitmap_static;
        bitmap_bytes = sizeof(pmm_bitmap_static);
        total_blocks = bitmap_bytes * 8;
        pmm_page_array = NULL;
    }
    
    /* Init Generic PMM */
    vm_phys_early_init(pmm_bitmap, bitmap_bytes, pmm_page_array, total_blocks);
    
    /* Report memory statistics */
    pmm_report_memory_stats();
    
    /* Reserve kernel and excluded regions */
    pmm_reserve_kernel();
    pmm_reserve_regions();
    
    /* Add usable ranges */
    pmm_walk_e820(map, count, pmm_cb_init_buddy, NULL);
    
    /* Final safety reservation */
    pmm_reserve_kernel();
    
    /* Mark watermark as used */
    uint32_t wm_start = watermark_base;
    uint32_t wm_end_val = watermark_ptr;
    for (uint32_t a = wm_start; a < wm_end_val; a += PMM_BLOCK_SIZE) {
        vm_phys_mark_used(a);
    }
}
