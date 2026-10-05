/*
 * pmm.h - x86_64 physical memory manager interface
 *
 * The allocator itself is arch/x86-common/pmm.c, shared with i386; this
 * header gives it the 64-bit kernel's constants.  pmm_alloc_block()
 * returns a direct-map (DMAP_BASE) address, as on i386 it returns one at
 * KERN_BASE: memset it directly, V2P() it for page tables and hardware.
 *
 * Physical memory is managed in two parts.
 *
 * Low memory, below PMM_PHYS_RAM_CAP, is what the shared allocator sets
 * up exactly as on i386, and is all that pmm_alloc_block() and
 * pmm_alloc_contiguous() hand out.  Every kernel and driver allocation
 * comes from there, which is what lets drivers keep bus addresses in
 * 32-bit fields and drive 32-bit DMA engines.
 *
 * High memory, at and above PMM_HIGHMEM_BASE (4 GiB), is added by
 * pmm_add_high_memory() once pmap_bootstrap() has extended the direct map
 * over it.  It is only reachable through vm_phys_alloc_page(), that is,
 * as pages the VM maps into processes; such a page has a P2V() address
 * like any other, but its physical address does not fit in 32 bits and
 * it must not be handed to a device (the block layer copies through a
 * low buffer when it is given one).
 *
 * Low memory is whatever the firmware reports usable below the cap, which
 * sits at the IOAPIC.  The i386 kernel stops at 3 GiB because its direct
 * map has no room for more; this one maps the whole first 4 GiB, so RAM
 * a machine has between 3 GiB and 4 GiB is RAM like any other.  The PCI
 * layer, which used to treat everything from 3 GiB up as its own, finds
 * room for 32-bit MMIO windows from the memory map instead
 * (pci_alloc_mmio32(), through pmm_low_ram_end() and pmm_reserved_range()).
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

/* End of low memory: the IOAPIC's address.  From there to 4 GiB is the
 * IOAPIC, the HPET, the local APIC and the firmware's flash, never RAM,
 * and stopping short of 4 GiB keeps every low address, and every END of a
 * low range, inside the 32-bit phys_addr_t the shared allocator uses. */
#define PMM_PHYS_RAM_CAP 0xFEC00000ULL

/* Everything managed is direct-mapped: the map has no carve-outs here. */
#define PMM_DIRECTMAP_PHYS_LIMIT ((uint32_t)PMM_PHYS_RAM_CAP)

/* High memory: from 4 GiB up to what one PML4 slot of direct map covers
 * (boot.S gives DMAP_BASE a single PDPT, 512 GiB). */
#define PMM_HIGHMEM_BASE 0x100000000ULL
#define PMM_HIGHMEM_CAP  0x8000000000ULL

/* End of the high memory the firmware reported (PMM_HIGHMEM_BASE if
 * none), and the call that hands it to the allocator.  The caller must
 * have direct-mapped [PMM_HIGHMEM_BASE, pmm_high_memory_end()) first. */
uint64_t pmm_high_memory_end(void);
void pmm_add_high_memory(void);

/* End of the direct-mapped high memory; PMM_HIGHMEM_BASE until
 * pmm_add_high_memory() has run. */
extern uint64_t pmm_high_mapped_end;

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

/* The firmware memory map below the cap, for whoever has to place
 * something in physical address space: the end of the highest usable RAM,
 * and the ranges the firmware marked reserved (ACPI, NVS, bad, reserved).
 * pmm_reserved_range() returns 0 and fills in range `idx`, or -1 past the
 * last; a range may appear more than once. */
uint32_t pmm_low_ram_end(void);
int pmm_reserved_range(int idx, uint32_t *start, uint32_t *end);

void pmm_walk_e820(const e820_entry_t *map, uint32_t count, pmm_region_callback cb, void *arg);
void pmm_dump_e820(const e820_entry_t *map, uint32_t count);

void pmm_watermark_init(uint32_t start, uint32_t end);
void* pmm_watermark_alloc(size_t bytes, size_t align);
uint32_t pmm_watermark_used(void);

struct vm_page;
extern struct vm_page *pmm_page_array;
extern size_t pmm_total_pages;
struct vm_page *pmm_get_page(uintptr_t pa);

static inline int pmm_phys_is_direct_mapped(uintptr_t pa) {
    return pa < PMM_DIRECTMAP_PHYS_LIMIT ||
           (pa >= PMM_HIGHMEM_BASE && pa < pmm_high_mapped_end);
}

static inline int pmm_virt_is_direct_mapped(uintptr_t va) {
    return va >= PMM_PHYS_VIRT_BASE &&
           pmm_phys_is_direct_mapped(va - PMM_PHYS_VIRT_BASE);
}

/* True for memory a device can be given: below the low-memory cap. */
static inline int pmm_phys_is_low(uintptr_t pa) {
    return pa < PMM_DIRECTMAP_PHYS_LIMIT;
}

/* True for the direct-map address of a high-memory page: a buffer whose
 * physical address does not fit in 32 bits, which I/O must copy through
 * low memory instead of handing to a device. */
static inline int pmm_virt_is_high(uintptr_t va) {
    return va >= PMM_PHYS_VIRT_BASE + PMM_HIGHMEM_BASE &&
           va < PMM_PHYS_VIRT_BASE + pmm_high_mapped_end;
}

#endif
