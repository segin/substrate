#ifndef _PMM_H
#define _PMM_H

#include <stdint.h>
#include <stddef.h>
#include <arch/i386/signal_arch.h>
#include <arch/x86-common/e820.h>
#include <arch/x86-common/multiboot.h>
#include <sys/param.h>

// Simple Bitmap Physical Memory Manager
// Assumes 32-bit address space
// Block size = 4KB

#define PMM_BLOCK_SIZE 4096
#define PMM_BLOCKS_PER_BYTE 8
#define PMM_PHYS_VIRT_BASE KERN_BASE
/*
 * Physical ceiling for the higher-half direct map: generic PMM callers that
 * expect phys+KERN_BASE to be a valid kernel address must stay below it.
 *
 * The first thing above the direct map is the signal trampoline page, which
 * pmap_map_signal_trampoline() pins at SIG_TRAMPOLINE_ADDR so every
 * personality's sigreturn stub has a fixed address to return through.  Derive
 * the ceiling from that address rather than hardcoding one: the two are the
 * same boundary, and when they disagree the machine corrupts itself.
 *
 * They did disagree.  The limit used to be 0x3EC00000 (the IOAPIC PDE, the
 * next landmark up), which puts the top of the direct map at 0xFEC00000 --
 * straight through the trampoline at 0xFE000000.  Below ~992 MiB of RAM
 * nothing notices, because no physical frame maps that high.  At or above it
 * the frame at 0x3E000000 is ordinary free RAM, the buddy allocator hands it
 * out, and the new owner's first write through the direct map lands on the
 * trampoline.  The next signal to return finds shredded code at a fixed
 * address, so the process dies with eip exactly SIG_TRAMPOLINE_ADDR -- and
 * since init's shell takes that path during rc.d, the boot dies with it.
 */
#define PMM_DIRECTMAP_PHYS_LIMIT \
        ((uint32_t)(SIG_TRAMPOLINE_ADDR - PMM_PHYS_VIRT_BASE))

typedef uint32_t phys_addr_t;

/* Iterator callback for memory regions */
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
void pmm_reclaim_range(uint32_t start, uint32_t end);
void pmm_reclaim_setup(void);
void pmm_dump_map(void);
void pmm_dump_mmap(uint32_t mmap_addr, uint32_t mmap_length);
uint32_t pmm_get_total_memory(void);
uint32_t pmm_get_free_memory(void);

/* E820 Memory Map Support */
void pmm_walk_e820(const e820_entry_t *map, uint32_t count, pmm_region_callback cb, void *arg);
void pmm_dump_e820(const e820_entry_t *map, uint32_t count);

// Watermark Allocator (Early Boot)
void pmm_watermark_init(uint32_t start, uint32_t end);
void* pmm_watermark_alloc(size_t bytes, size_t align);
uint32_t pmm_watermark_used(void);

// VM Page Integration
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
