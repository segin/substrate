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
// Static fallback metadata for 128MB when bootstrap allocation cannot fit dynamic tables.
#define PMM_STATIC_METADATA_BLOCKS (4096U * 8U)
static uint8_t pmm_bitmap_static[PMM_STATIC_METADATA_BLOCKS / 8U];
static vm_page_t pmm_page_array_static[PMM_STATIC_METADATA_BLOCKS];

// ==================== Boot Memory Detection State ====================

/* Memory region tracking for overlap detection and validation */
#define PMM_MAX_REGIONS 128
#define PMM_MAX_USABLE_RANGES 128
#define PMM_MAX_MODULE_REGIONS 8
#define PMM_MAX_BOOT_REGIONS 16
#define PMM_PHYS_VIRT_BASE 0xC0000000U
#define PMM_BOOTSTRAP_LOWMEM_LIMIT (8U * 1024U * 1024U)
#define PMM_CONSTRAINED_RAM_LIMIT (4U * 1024U * 1024U)
#define PMM_PHYS_RAM_CAP 0xC0000000ULL
/*
 * Higher-half direct map remains contiguous only until the LAPIC slot at
 * 0xFEC00000 (PDE 1019). Keep generic PMM allocations below that mark.
 */
#define PMM_DIRECTMAP_PHYS_LIMIT 0x3EC00000U

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
static phys_addr_t module_regions_start[PMM_MAX_MODULE_REGIONS];
static phys_addr_t module_regions_end[PMM_MAX_MODULE_REGIONS];
static int module_region_count = 0;

/* Boot metadata bounds (mmap, cmdline, module list, etc.) */
static phys_addr_t boot_regions_start[PMM_MAX_BOOT_REGIONS];
static phys_addr_t boot_regions_end[PMM_MAX_BOOT_REGIONS];
static int boot_region_count = 0;

/* Usable regions accepted after overlap rejection */
typedef struct pmm_usable_range {
    phys_addr_t start;
    phys_addr_t end;
} pmm_usable_range_t;

static pmm_usable_range_t pmm_usable_ranges[PMM_MAX_USABLE_RANGES];
static int pmm_usable_range_count = 0;
static int pmm_bootstrap_lowmem_only = 1;
static int pmm_highmem_seeded = 0;
static uint32_t pmm_seed_phys_limit = PMM_BOOTSTRAP_LOWMEM_LIMIT;
static uint32_t pmm_detected_max_phys = PMM_BLOCK_SIZE;
static size_t pmm_target_total_blocks = 0;
static size_t pmm_target_bitmap_bytes = 0;
static size_t pmm_target_page_array_bytes = 0;
static int pmm_metadata_needs_promotion = 0;

typedef struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

// ==================== Boot Memory Detection Helpers ====================

static void pmm_reset_walk_state(void) {
    memset(pmm_regions, 0, sizeof(pmm_regions));
    memset(pmm_usable_ranges, 0, sizeof(pmm_usable_ranges));
    pmm_region_count = 0;
    pmm_usable_range_count = 0;
    pmm_total_usable_ram = 0;
    pmm_total_reserved_ram = 0;
}

static void pmm_clear_boot_exclusions(void) {
    mboot_info_start = 0;
    mboot_info_end = 0;
    module_region_count = 0;
    boot_region_count = 0;
    memset(module_regions_start, 0, sizeof(module_regions_start));
    memset(module_regions_end, 0, sizeof(module_regions_end));
    memset(boot_regions_start, 0, sizeof(boot_regions_start));
    memset(boot_regions_end, 0, sizeof(boot_regions_end));
}

static uint32_t pmm_virt_to_phys(uint32_t addr) {
    if (addr >= PMM_PHYS_VIRT_BASE) {
        return addr - PMM_PHYS_VIRT_BASE;
    }
    return addr;
}

static void pmm_select_metadata(uint64_t max_phys, uint64_t total_usable,
                                uint8_t **out_bitmap,
                                size_t *out_bitmap_bytes,
                                vm_page_t **out_page_array,
                                size_t *out_total_blocks);
static void pmm_mark_watermark_used(void);
static int pmm_promote_metadata(void);
static void pmm_reserve_kernel(void);

static void pmm_mark_used_range(phys_addr_t start, phys_addr_t end) {
    uint32_t page_start = start & ~(PMM_BLOCK_SIZE - 1);
    uint32_t page_end = end & ~(PMM_BLOCK_SIZE - 1);

    for (uint32_t addr = page_start; addr < page_end; addr += PMM_BLOCK_SIZE) {
        vm_phys_mark_used(addr);
        if (addr > 0xFFFFFFFFU - PMM_BLOCK_SIZE) {
            break;
        }
    }
}

static void pmm_cb_init_buddy(phys_addr_t start, phys_addr_t length, void *arg);

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

    mboot_addr = pmm_virt_to_phys(mboot_addr);

    mboot_info_start = mboot_addr;
    mboot_info_end = mboot_addr + sizeof(multiboot_info_t);

    /* Round to page boundaries */
    mboot_info_start &= ~(PMM_BLOCK_SIZE - 1);
    mboot_info_end = (mboot_info_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
}

static void pmm_record_boot_region(uint32_t start, uint32_t end) {
    if (!start || end <= start || boot_region_count >= PMM_MAX_BOOT_REGIONS) {
        return;
    }

    uint32_t page_start = start & ~(PMM_BLOCK_SIZE - 1);
    uint64_t page_end64 = ((uint64_t)end + PMM_BLOCK_SIZE - 1) & ~((uint64_t)PMM_BLOCK_SIZE - 1);
    if (page_end64 > 0xFFFFFFFFULL) {
        page_end64 = 0xFFFFFFFFULL;
    }
    uint32_t page_end = (uint32_t)page_end64;
    if (page_end <= page_start) {
        return;
    }

    boot_regions_start[boot_region_count] = page_start;
    boot_regions_end[boot_region_count] = page_end;
    boot_region_count++;
}

static void pmm_record_multiboot_string(uint32_t phys_addr) {
    if (!phys_addr) {
        return;
    }

    phys_addr = pmm_virt_to_phys(phys_addr);
    if (phys_addr >= PMM_BOOTSTRAP_LOWMEM_LIMIT) {
        pmm_record_boot_region(phys_addr, phys_addr + PMM_BLOCK_SIZE);
        return;
    }

    const char *s = (const char *)(uintptr_t)(phys_addr + PMM_PHYS_VIRT_BASE);
    size_t len = strnlen(s, 4096);
    pmm_record_boot_region(phys_addr, phys_addr + (uint32_t)len + 1);
}

/*
 * pmm_record_module_regions - Record module memory regions
 *
 * Modules loaded by the bootloader must not be overwritten.
 */
static void pmm_record_module_regions(uint32_t mods_addr, uint32_t mods_count) {
    if (!mods_addr || mods_count == 0) return;

    uint32_t mods_addr_phys = pmm_virt_to_phys(mods_addr);
    if (mods_addr_phys >= PMM_BOOTSTRAP_LOWMEM_LIMIT) {
        return;
    }
    uint32_t mods_addr_virt = mods_addr_phys + PMM_PHYS_VIRT_BASE;

    /* Limit to our array size */
    if (mods_count > PMM_MAX_MODULE_REGIONS) mods_count = PMM_MAX_MODULE_REGIONS;

    for (uint32_t i = 0; i < mods_count && module_region_count < PMM_MAX_MODULE_REGIONS; i++) {
        const multiboot_module_t *mod = (const multiboot_module_t *)(uintptr_t)(
            mods_addr_virt + i * sizeof(multiboot_module_t));
        uint32_t mod_start = mod->mod_start;
        uint32_t mod_end = mod->mod_end;

        if (mod_start < mod_end) {
            /* Round to page boundaries */
            mod_start &= ~(PMM_BLOCK_SIZE - 1);
            mod_end = (mod_end + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
            if (mod_end <= mod_start) {
                continue;
            }

            module_regions_start[module_region_count] = mod_start;
            module_regions_end[module_region_count] = mod_end;
            module_region_count++;
        }
    }
}

void pmm_record_boot_info(const multiboot_info_t *mbi) {
    pmm_clear_boot_exclusions();

    if (!mbi) {
        return;
    }

    uint32_t mbi_virt = (uint32_t)(uintptr_t)mbi;
    uint32_t mbi_phys = pmm_virt_to_phys(mbi_virt);

    pmm_record_multiboot_info(mbi_phys);
    pmm_record_boot_region(mbi_phys, mbi_phys + sizeof(*mbi));

    if ((mbi->flags & MULTIBOOT_INFO_MEM_MAP) && mbi->mmap_addr && mbi->mmap_length) {
        pmm_record_boot_region(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);
    }

    if ((mbi->flags & MULTIBOOT_INFO_MODS) && mbi->mods_addr && mbi->mods_count) {
        uint32_t mods_count = mbi->mods_count;
        if (mods_count > PMM_MAX_MODULE_REGIONS) {
            mods_count = PMM_MAX_MODULE_REGIONS;
        }
        uint32_t mod_list_bytes = mods_count * sizeof(multiboot_module_t);
        pmm_record_boot_region(mbi->mods_addr, mbi->mods_addr + mod_list_bytes);
        pmm_record_module_regions(mbi->mods_addr, mbi->mods_count);
    }

    if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
        pmm_record_multiboot_string(mbi->cmdline);
    }
    if (mbi->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME) {
        pmm_record_multiboot_string(mbi->boot_loader_name);
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
static int pmm_check_usable_overlap(phys_addr_t start, phys_addr_t end,
                                    phys_addr_t *out_start, phys_addr_t *out_end) {
    for (int i = 0; i < pmm_usable_range_count; i++) {
        phys_addr_t r_start = pmm_usable_ranges[i].start;
        phys_addr_t r_end = pmm_usable_ranges[i].end;

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
static void __attribute__((unused)) pmm_reserve_regions(void) {
    for (int i = 0; i < pmm_region_count; i++) {
        if (!pmm_regions[i].valid) continue;
        
        uint32_t type = pmm_regions[i].type;
        phys_addr_t start = pmm_regions[i].start;
        phys_addr_t end = pmm_regions[i].end;
        
        /* Reserve non-usable regions */
        if (type != MULTIBOOT_MEMORY_AVAILABLE && type != E820_USABLE) {
            pmm_mark_used_range(start, end);
        }
    }
    
    /* Reserve kernel region */
    pmm_mark_used_range(kernel_phys_start, kernel_phys_end);
    
    /* Reserve Multiboot info region */
    if (mboot_info_start < mboot_info_end) {
        pmm_mark_used_range(mboot_info_start, mboot_info_end);
    }

    for (int i = 0; i < boot_region_count; i++) {
        pmm_mark_used_range(boot_regions_start[i], boot_regions_end[i]);
    }
    
    /* Reserve module regions */
    for (int i = 0; i < module_region_count; i++) {
        pmm_mark_used_range(module_regions_start[i], module_regions_end[i]);
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
        snprintf(buf, sizeof(buf), "Total usable RAM: %lu MB (%lu GB)\n",
                (unsigned long)usable_mb, (unsigned long)usable_gb);
    } else {
        snprintf(buf, sizeof(buf), "Total usable RAM: %lu MB\n", (unsigned long)usable_mb);
    }
    kprint(buf);
    
    /* Report reserved RAM if significant */
    if (pmm_total_reserved_ram > 0) {
        uint64_t reserved_mb = pmm_total_reserved_ram / (1024 * 1024);
        snprintf(buf, sizeof(buf), "Reserved RAM: %lu MB\n", (unsigned long)reserved_mb);
        kprint(buf);
    }
    
    /* Report detected regions */
    snprintf(buf, sizeof(buf), "Memory regions detected: %d\n", pmm_region_count);
    kprint(buf);
}



// ==================== Watermark (Bump) Allocator ====================
static uint32_t watermark_base;
static uint32_t watermark_ptr;
static uint32_t watermark_end;

void pmm_watermark_init(uint32_t start, uint32_t end) {
    uint32_t clamped_end = end;
    if (clamped_end > PMM_BOOTSTRAP_LOWMEM_LIMIT) {
        clamped_end = PMM_BOOTSTRAP_LOWMEM_LIMIT;
    }

    watermark_base = (start + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    watermark_ptr = watermark_base;
    watermark_end = clamped_end & ~(PMM_BLOCK_SIZE - 1);
    if (watermark_end < watermark_base) {
        watermark_end = watermark_base;
    }
}

void* pmm_watermark_alloc(size_t bytes, size_t align) {
    if (bytes == 0) {
        return (void *)(uintptr_t)(watermark_ptr + PMM_PHYS_VIRT_BASE);
    }

    if (align == 0) align = 16;
    if ((align & (align - 1)) != 0) {
        align = PMM_BLOCK_SIZE;
    }

    uint32_t aligned_ptr = (watermark_ptr + (uint32_t)align - 1U) & ~((uint32_t)align - 1U);
    if (aligned_ptr < watermark_ptr) {
        return NULL;
    }

    uint32_t alloc_bytes = (uint32_t)bytes;
    uint32_t new_ptr = aligned_ptr + alloc_bytes;
    if (new_ptr < aligned_ptr || new_ptr > watermark_end) {
        return NULL;
    }

    watermark_ptr = new_ptr;
    return (void *)(uintptr_t)(aligned_ptr + PMM_PHYS_VIRT_BASE);
}

uint32_t pmm_watermark_used(void) {
    return watermark_ptr - watermark_base;
} 

static void pmm_select_metadata(uint64_t max_phys, uint64_t total_usable,
                                uint8_t **out_bitmap,
                                size_t *out_bitmap_bytes,
                                vm_page_t **out_page_array,
                                size_t *out_total_blocks) {
    uint32_t max_phys_addr = (uint32_t)max_phys;
    if (max_phys_addr & (PMM_BLOCK_SIZE - 1)) {
        max_phys_addr = (max_phys_addr + PMM_BLOCK_SIZE) & ~(PMM_BLOCK_SIZE - 1);
    }
    if (max_phys_addr < PMM_BLOCK_SIZE) {
        max_phys_addr = PMM_BLOCK_SIZE;
    }

    if (max_phys_addr < PMM_CONSTRAINED_RAM_LIMIT || total_usable < PMM_CONSTRAINED_RAM_LIMIT) {
        kprint("PMM: constrained RAM (<4MB), using static bitmap metadata.\n");
        *out_bitmap = pmm_bitmap_static;
        *out_bitmap_bytes = sizeof(pmm_bitmap_static);
        *out_total_blocks = PMM_STATIC_METADATA_BLOCKS;
        *out_page_array = pmm_page_array_static;
        return;
    }

    size_t total_blocks = max_phys_addr / PMM_BLOCK_SIZE;
    size_t bitmap_bytes = (total_blocks + 7) / 8;
    size_t array_bytes = total_blocks * sizeof(vm_page_t);
    uint32_t saved_ptr = watermark_ptr;

    pmm_detected_max_phys = max_phys_addr;
    pmm_target_total_blocks = total_blocks;
    pmm_target_bitmap_bytes = bitmap_bytes;
    pmm_target_page_array_bytes = array_bytes;
    pmm_metadata_needs_promotion = 0;

    uint8_t *bitmap = (uint8_t *)pmm_watermark_alloc(bitmap_bytes, 16);
    vm_page_t *page_array = NULL;
    if (bitmap) {
        page_array = (vm_page_t *)pmm_watermark_alloc(array_bytes, PMM_BLOCK_SIZE);
    }

    if (!bitmap || !page_array) {
        watermark_ptr = saved_ptr;
        kprint("PMM: Warn - metadata allocation exceeded bootstrap low memory, using static bitmap.\n");
        *out_bitmap = pmm_bitmap_static;
        *out_bitmap_bytes = sizeof(pmm_bitmap_static);
        *out_total_blocks = PMM_STATIC_METADATA_BLOCKS;
        *out_page_array = pmm_page_array_static;
        pmm_metadata_needs_promotion = (total_blocks > PMM_STATIC_METADATA_BLOCKS);
        return;
    }

    *out_bitmap = bitmap;
    *out_bitmap_bytes = bitmap_bytes;
    *out_total_blocks = total_blocks;
    *out_page_array = page_array;
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

    pmm_reclaim_range(start, end);
    kprint("Done.\n");
}

// Validate memory map entry type
static int pmm_is_usable_type(uint32_t type) {
    return (type == MULTIBOOT_MEMORY_AVAILABLE);
}

/*
 * pmm_validate_mmap_entry - Validate a multiboot memory map entry
 *
 * Returns 0 if entry is valid and usable, non-zero to skip.
 * Validates: type, non-zero length, address range within current i386 cap.
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
    
    /* Skip entries entirely above the current kernel-managed physical cap. */
    if (start64 >= PMM_PHYS_RAM_CAP) {
        return -4; /* Above kernel-managed physical cap */
    }
    
    /* Clamp end to the current managed physical ceiling. */
    if (end64 > PMM_PHYS_RAM_CAP) {
        end64 = PMM_PHYS_RAM_CAP;
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
 * - Clamps 64-bit addresses to the current i386 physical cap
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
            /* Clamp reserved regions to the current kernel-managed ceiling. */
            uint64_t r_start64 = entry->addr;
            uint64_t r_end64 = entry->addr + entry->len;
            if (r_start64 >= PMM_PHYS_RAM_CAP) {
                /* Skip entries entirely above the managed ceiling */
                ptr += entry_size;
                entries_processed++;
                continue;
            }
            if (r_end64 > PMM_PHYS_RAM_CAP) r_end64 = PMM_PHYS_RAM_CAP;
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
};

static void pmm_cb_stats(phys_addr_t start, phys_addr_t length, void *arg) {
    struct pmm_stats_ctx *ctx = arg;
    phys_addr_t end = start + length;

    if (end <= start) {
        return;
    }
    if (end > ctx->max_phys) {
        ctx->max_phys = end;
    }

    /*
     * Reject overlapping usable regions; we only keep the first accepted region.
     * Pass 2 seeds the buddy allocator strictly from this accepted list.
     */
    if (pmm_check_usable_overlap(start, end, NULL, NULL)) {
        return;
    }

    if (pmm_usable_range_count >= PMM_MAX_USABLE_RANGES) {
        return;
    }

    pmm_usable_ranges[pmm_usable_range_count].start = start;
    pmm_usable_ranges[pmm_usable_range_count].end = end;
    pmm_usable_range_count++;
    ctx->total_usable += (uint64_t)(end - start);
    pmm_add_region(start, end, MULTIBOOT_MEMORY_AVAILABLE);
}

static void pmm_add_range_excluding_regions(phys_addr_t start, phys_addr_t end) {
    phys_addr_t cursor = start;

    while (cursor < end) {
        int found = 0;
        phys_addr_t hit_start = end;
        phys_addr_t hit_end = end;

        for (int i = 0; i < boot_region_count; i++) {
            phys_addr_t ex_start = boot_regions_start[i];
            phys_addr_t ex_end = boot_regions_end[i];

            if (ex_end <= cursor || ex_start >= end) {
                continue;
            }

            phys_addr_t clipped_start = (ex_start > cursor) ? ex_start : cursor;
            if (!found || clipped_start < hit_start) {
                found = 1;
                hit_start = clipped_start;
                hit_end = ex_end;
            }
        }

        for (int i = 0; i < module_region_count; i++) {
            phys_addr_t ex_start = module_regions_start[i];
            phys_addr_t ex_end = module_regions_end[i];

            if (ex_end <= cursor || ex_start >= end) {
                continue;
            }

            phys_addr_t clipped_start = (ex_start > cursor) ? ex_start : cursor;
            if (!found || clipped_start < hit_start) {
                found = 1;
                hit_start = clipped_start;
                hit_end = ex_end;
            }
        }

        if (!found) {
            vm_phys_add_range(cursor, end);
            break;
        }

        if (cursor < hit_start) {
            vm_phys_add_range(cursor, hit_start);
        }

        if (hit_end <= cursor) {
            break;
        }
        cursor = hit_end;
    }
}

static void pmm_seed_usable_ranges(void) {
    for (int i = 0; i < pmm_usable_range_count; i++) {
        phys_addr_t start = pmm_usable_ranges[i].start;
        phys_addr_t end = pmm_usable_ranges[i].end;
        if (end > start) {
            pmm_cb_init_buddy(start, end - start, NULL);
        }
    }
}

static void pmm_mark_watermark_used(void) {
    uint32_t wm_start = watermark_base;
    uint32_t wm_end = watermark_ptr;

    for (uint32_t a = wm_start; a < wm_end; a += PMM_BLOCK_SIZE) {
        vm_phys_mark_used(a);
    }
}

static void pmm_cb_init_buddy(phys_addr_t start, phys_addr_t length, void *arg) {
    (void)arg;
    phys_addr_t end = start + length;

    if (end > pmm_seed_phys_limit) {
        end = pmm_seed_phys_limit;
    }
    
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
    
    if (safe_start < end) {
        pmm_add_range_excluding_regions(safe_start, end);
    }
}

static int pmm_promote_metadata(void) {
    if (!pmm_metadata_needs_promotion) {
        return 0;
    }

    uint32_t metadata_limit = pmm_detected_max_phys;
    if (metadata_limit > PMM_DIRECTMAP_PHYS_LIMIT) {
        metadata_limit = PMM_DIRECTMAP_PHYS_LIMIT;
    }
    metadata_limit &= ~(PMM_BLOCK_SIZE - 1);

    if (metadata_limit <= watermark_ptr) {
        kprint("PMM: Warn - no direct-mapped headroom left for full metadata.\n");
        return -1;
    }

    watermark_end = metadata_limit;
    uint32_t saved_ptr = watermark_ptr;

    uint8_t *pmm_bitmap = (uint8_t *)pmm_watermark_alloc(pmm_target_bitmap_bytes, 16);
    vm_page_t *pmm_page_array = NULL;
    if (pmm_bitmap) {
        pmm_page_array = (vm_page_t *)pmm_watermark_alloc(pmm_target_page_array_bytes,
                                                          PMM_BLOCK_SIZE);
    }

    if (!pmm_bitmap || !pmm_page_array) {
        watermark_ptr = saved_ptr;
        kprint("PMM: Warn - unable to promote metadata into direct-mapped window.\n");
        return -1;
    }

    vm_phys_early_init(pmm_bitmap, pmm_target_bitmap_bytes, pmm_page_array,
                       pmm_target_total_blocks);
    pmm_reserve_kernel();
    pmm_seed_usable_ranges();
    pmm_reserve_kernel();
    pmm_mark_watermark_used();
    pmm_metadata_needs_promotion = 0;

    kprint("PMM: promoted bootstrap metadata into direct-mapped RAM.\n");
    return 0;
}

void pmm_enable_highmem(void) {
    uint32_t seed_limit = PMM_DIRECTMAP_PHYS_LIMIT;

    if (pmm_highmem_seeded) {
        return;
    }

    if (pmm_promote_metadata() != 0) {
        kprint("PMM: continuing with bootstrap metadata only.\n");
        if (pmm_metadata_needs_promotion) {
            seed_limit = PMM_STATIC_METADATA_BLOCKS * PMM_BLOCK_SIZE;
        }
    }

    pmm_bootstrap_lowmem_only = 0;
    pmm_seed_phys_limit = seed_limit;

    for (int i = 0; i < pmm_usable_range_count; i++) {
        phys_addr_t start = pmm_usable_ranges[i].start;
        phys_addr_t end = pmm_usable_ranges[i].end;
        if (start < PMM_BOOTSTRAP_LOWMEM_LIMIT) {
            start = PMM_BOOTSTRAP_LOWMEM_LIMIT;
        }
        if (end > start) {
            pmm_cb_init_buddy(start, end - start, NULL);
        }
    }

    pmm_highmem_seeded = 1;
    kprint("PMM: direct-mapped RAM ranges enabled after bootstrap paging.\n");
}

void pmm_init(uint32_t mmap_addr, uint32_t mmap_length) {
    // 0. Initialize kernel bounds first (needed for all subsequent operations)
    pmm_init_kernel_bounds();
    pmm_reset_walk_state();
    pmm_bootstrap_lowmem_only = 1;
    pmm_highmem_seeded = 0;
    pmm_seed_phys_limit = PMM_BOOTSTRAP_LOWMEM_LIMIT;
    pmm_metadata_needs_promotion = 0;

    // 2. Pass 1: Find limits with 64-bit accumulation for >4GB systems
    struct pmm_stats_ctx stats = { .max_phys = 0x1000000, .total_usable = 0 };
    if (mmap_addr && mmap_length) {
        pmm_walk_mmap(mmap_addr, mmap_length, pmm_cb_stats, &stats);
    }
    
    /* Save global stats for reporting */
    pmm_total_usable_ram = stats.total_usable;

    // 3. Init Watermark
    pmm_watermark_init(kernel_phys_end, PMM_BOOTSTRAP_LOWMEM_LIMIT);
    
    size_t total_blocks = 0;
    size_t bitmap_bytes = 0;
    uint8_t* pmm_bitmap = NULL;
    vm_page_t* pmm_page_array = NULL;
    pmm_select_metadata(stats.max_phys, stats.total_usable,
                        &pmm_bitmap, &bitmap_bytes, &pmm_page_array, &total_blocks);

    // 4. Init Generic PMM
    // We pass the "total_page_count" as total_blocks
    vm_phys_early_init(pmm_bitmap, bitmap_bytes, pmm_page_array, total_blocks);

    // 5. Report memory statistics with 64-bit accumulation
    pmm_report_memory_stats();

    if (mmap_addr == 0 || mmap_length == 0) {
        kprint("PMM: Fallback bootstrap allocator.\n");
        // Fallback init...
        // ... (Similar to before)
        uint32_t k_end = kernel_phys_end;
        vm_phys_add_range(k_end, PMM_BOOTSTRAP_LOWMEM_LIMIT);
        pmm_reserve_kernel();
        return;
    }

    // 6. Reserve kernel image first (non-usable ranges are never added as free)
    pmm_reserve_kernel();
    
    // 7. Seed buddy only from accepted non-overlapping usable ranges
    pmm_seed_usable_ranges();
    
    // 8. Final safety reservation
    pmm_reserve_kernel();
    
    pmm_mark_watermark_used();
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
    start = pmm_virt_to_phys(start);
    end = pmm_virt_to_phys(end);
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
        
        snprintf(buf, sizeof(buf), " [0x%08x%08x - 0x%08x%08x] %s\n",
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
 * Validates: type, non-zero length, address range within current i386 cap.
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
    
    /* Skip entries entirely above the current kernel-managed physical cap. */
    if (start64 >= PMM_PHYS_RAM_CAP) {
        return -4; /* Above kernel-managed physical cap */
    }
    
    /* Clamp end to the current managed physical ceiling. */
    if (end64 > PMM_PHYS_RAM_CAP) {
        end64 = PMM_PHYS_RAM_CAP;
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
 * - Clamps 64-bit addresses to the current i386 physical cap
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
            /* Clamp reserved regions to the current kernel-managed ceiling. */
            uint64_t r_start64 = map[i].addr;
            uint64_t r_end64 = map[i].addr + map[i].len;
            if (r_start64 < PMM_PHYS_RAM_CAP) {
                if (r_end64 > PMM_PHYS_RAM_CAP) r_end64 = PMM_PHYS_RAM_CAP;
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
        
        snprintf(buf, sizeof(buf), " [0x%08x%08x - 0x%08x%08x] %s\n",
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
    pmm_reset_walk_state();
    pmm_clear_boot_exclusions();
    pmm_bootstrap_lowmem_only = 1;
    pmm_highmem_seeded = 0;
    
    if (!map || count == 0) {
        kprint("PMM: No e820 map provided, using fallback 15MB\n");
        uint32_t k_end = kernel_phys_end;
        
        pmm_watermark_init(k_end, PMM_BOOTSTRAP_LOWMEM_LIMIT);
        
        /* Minimal init */
        size_t total_blocks = PMM_BOOTSTRAP_LOWMEM_LIMIT / PMM_BLOCK_SIZE;
        size_t bitmap_bytes = (total_blocks + 7) / 8;
        uint8_t *pmm_bitmap = (uint8_t *)pmm_watermark_alloc(bitmap_bytes, 16); // Assuming 16-byte alignment for bitmap
        
        if (!pmm_bitmap) {
            pmm_bitmap = pmm_bitmap_static;
            bitmap_bytes = sizeof(pmm_bitmap_static);
            total_blocks = bitmap_bytes * 8;
        }
        
        vm_phys_early_init(pmm_bitmap, bitmap_bytes, NULL, total_blocks);
        vm_phys_add_range(k_end, PMM_BOOTSTRAP_LOWMEM_LIMIT);
        pmm_reserve_kernel();
        return;
    }
    
    /* Debug dump */
    pmm_dump_e820(map, count);
    
    /* Pass 1: Find limits with 64-bit accumulation */
    struct pmm_stats_ctx stats = { .max_phys = 0x1000000, .total_usable = 0 };
    pmm_walk_e820(map, count, pmm_cb_stats, &stats);
    
    /* Save global stats */
    pmm_total_usable_ram = stats.total_usable;
    
    /* Init Watermark */
    pmm_watermark_init(kernel_phys_end, (uint32_t)stats.max_phys);
    
    size_t total_blocks = 0;
    size_t bitmap_bytes = 0;
    uint8_t* pmm_bitmap = NULL;
    vm_page_t* pmm_page_array = NULL;
    pmm_select_metadata(stats.max_phys, stats.total_usable,
                        &pmm_bitmap, &bitmap_bytes, &pmm_page_array, &total_blocks);
    
    /* Init Generic PMM */
    vm_phys_early_init(pmm_bitmap, bitmap_bytes, pmm_page_array, total_blocks);
    
    /* Report memory statistics */
    pmm_report_memory_stats();
    
    /* Reserve kernel and excluded regions */
    pmm_reserve_kernel();
    pmm_reserve_regions();
    
    /* Add accepted usable ranges */
    pmm_seed_usable_ranges();
    
    /* Final safety reservation */
    pmm_reserve_kernel();
    
    /* Mark watermark as used */
    uint32_t wm_start = watermark_base;
    uint32_t wm_end_val = watermark_ptr;
    for (uint32_t a = wm_start; a < wm_end_val; a += PMM_BLOCK_SIZE) {
        vm_phys_mark_used(a);
    }
}
