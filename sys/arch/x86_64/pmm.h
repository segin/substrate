/*
 * pmm.h - x86_64 physical memory manager interface
 *
 * The allocator itself is arch/x86-common/pmm.c, shared with i386; this
 * header gives it the 64-bit kernel's constants.  pmm_alloc_block()
 * returns a direct-map (DMAP_BASE) address, as on i386 it returns one at
 * KERN_BASE: memset it directly, V2P() it for page tables and hardware.
 *
 * Physical memory is managed below PMM_PHYS_RAM_CAP only, as on i386.
 * Drivers still keep bus addresses in 32-bit fields and most DMA engines
 * they drive are 32-bit, so RAM above the cap waits for a DMA-zone
 * allocator; the boot direct map covers all of it either way.
 */
#ifndef _ARCH_X86_64_PMM_H
#define _ARCH_X86_64_PMM_H

#include <stdint.h>
#include <stddef.h>
#include <arch/x86-common/e820.h>
#include <arch/x86-common/multiboot.h>
#include <machine/vmparam.h>
#include <sys/param.h>

#define PMM_BLOCK_SIZE 4096
#define PMM_BLOCKS_PER_BYTE 8
#define PMM_PHYS_VIRT_BASE KERN_BASE

/* Highest RAM managed, and the PCI layer's floor for 32-bit MMIO windows
 * it has to place itself (see pci_alloc_mmio32()). */
#define PMM_PHYS_RAM_CAP 0xC0000000ULL

/* Everything managed is direct-mapped: the map has no carve-outs here. */
#define PMM_DIRECTMAP_PHYS_LIMIT ((uint32_t)PMM_PHYS_RAM_CAP)

typedef uint32_t phys_addr_t;

typedef void (*pmm_region_callback)(phys_addr_t start, phys_addr_t len, void *arg);
void pmm_walk_mmap(uint32_t mmap_addr, uint32_t mmap_length, pmm_region_callback cb, void *arg);
void pmm_record_boot_info(const multiboot_info_t *mbi);

void pmm_init(uint32_t mmap_addr, uint32_t mmap_length,
              uint32_t mem_lower_kb, uint32_t mem_upper_kb);
void pmm_init_e820(e820_entry_t *map, uint32_t count);
void pmm_enable_highmem(void);
void* pmm_alloc_block(void);
void* pmm_alloc_contiguous(size_t count);
void pmm_free_block(void* p);
void pmm_free_contiguous(void* p, size_t count);
void pmm_reclaim_range(uintptr_t start, uintptr_t end);
void pmm_reclaim_setup(void);
void pmm_dump_map(void);
void pmm_dump_managed(void);
void pmm_dump_mmap(uintptr_t mmap_addr, uint32_t mmap_length);
uint32_t pmm_get_total_memory(void);
uint32_t pmm_get_free_memory(void);

void pmm_walk_e820(const e820_entry_t *map, uint32_t count, pmm_region_callback cb, void *arg);
void pmm_dump_e820(const e820_entry_t *map, uint32_t count);

void pmm_watermark_init(uint32_t start, uint32_t end);
void* pmm_watermark_alloc(size_t bytes, size_t align);
uint32_t pmm_watermark_used(void);

struct vm_page;
extern struct vm_page *pmm_page_array;
extern size_t pmm_total_pages;
struct vm_page *pmm_get_page(uintptr_t pa);

static inline int pmm_phys_is_direct_mapped(uint32_t pa) {
    return pa < PMM_DIRECTMAP_PHYS_LIMIT;
}

static inline int pmm_virt_is_direct_mapped(uintptr_t va) {
    return va >= PMM_PHYS_VIRT_BASE &&
           (va - PMM_PHYS_VIRT_BASE) < PMM_DIRECTMAP_PHYS_LIMIT;
}

#endif
