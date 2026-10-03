/*
 * layout.h - x86_64 kernel virtual address space
 *
 * Kernel-internal; the user/kernel boundary is part of the ABI
 * (docs/specs/abi-amd64.md, section 4), the rest is not.
 *
 *   0x0000000000000000 .. 0x00007FFFFFFFFFFF  user space
 *   0xFFFFF80000000000 .. (DMAP_SIZE)          direct map of physical memory
 *   0xFFFFFFFF80000000 .. 0xFFFFFFFFFFFFFFFF  kernel image (top 2 GiB, so
 *                                             -mcmodel=kernel addressing works)
 *
 * Shared by C and assembly.
 */
#ifndef _ARCH_X86_64_LAYOUT_H
#define _ARCH_X86_64_LAYOUT_H

#ifdef __ASSEMBLER__
#define _LAYOUT_UL(x)   x
#else
#define _LAYOUT_UL(x)   x##UL
#endif

#define KERNEL_VMA          _LAYOUT_UL(0xFFFFFFFF80000000)
#define KERNEL_LOAD_PHYS    _LAYOUT_UL(0x0000000000100000)
#define DMAP_BASE           _LAYOUT_UL(0xFFFFF80000000000)
/* What the boot page tables direct-map; the pmap extends it to all RAM. */
#define DMAP_BOOT_SIZE      _LAYOUT_UL(0x0000000100000000)

#define VM_MAXUSER_ADDRESS  _LAYOUT_UL(0x0000800000000000)

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Physical to direct-map virtual, and back (direct-mapped addresses only). */
static inline void *phys_to_dmap(uint64_t pa)
{
	return (void *)(uintptr_t)(DMAP_BASE + pa);
}

static inline uint64_t dmap_to_phys(const void *va)
{
	return (uint64_t)(uintptr_t)va - DMAP_BASE;
}

/* A kernel-image virtual address's physical address. */
static inline uint64_t kva_to_phys(const void *va)
{
	return (uint64_t)(uintptr_t)va - KERNEL_VMA;
}
#endif

#endif /* _ARCH_X86_64_LAYOUT_H */
