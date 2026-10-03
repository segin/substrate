/*
 * vmparam.h - x86_64 kernel address-space constants for machine-independent
 * code
 *
 * Unlike i386, the kernel image and the direct map are separate regions
 * (layout.h): the image is linked at KERNEL_VMA in the top 2 GiB, physical
 * memory is reached through the direct map at DMAP_BASE.  V2P accepts an
 * address in either.
 *
 * Today's userland is 32-bit: its processes run in compatibility mode with
 * exactly the i386 address space, ending at USER32_VA_END.
 */
#ifndef _ARCH_X86_64_VMPARAM_H
#define _ARCH_X86_64_VMPARAM_H

#include <stdint.h>
#include <arch/x86_64/layout.h>

/* Physical address 0 in the kernel direct map. */
#define KERN_BASE           DMAP_BASE

/* Lowest kernel virtual address: the start of the upper canonical half. */
#define KERNEL_VA_START     0xFFFF800000000000UL

/* End of a 32-bit process's address space (exclusive), as on i386. */
#define USER32_VA_END       0xC0000000UL

/* Kernel VA that ioremap() hands out for device mappings: the top 16 MiB
 * window of the last gigabyte, above the kernel image's.  Every address
 * space shares it through the kernel's PML4 slot. */
#define IOREMAP_BASE        0xFFFFFFFFC0000000UL
#define IOREMAP_LIMIT       0xFFFFFFFFC1000000UL

static inline uintptr_t vmparam_kva_to_phys(uintptr_t va) {
    return va >= KERNEL_VMA ? va - KERNEL_VMA : va - DMAP_BASE;
}

#define P2V(x) ((void *)((uintptr_t)(x) + DMAP_BASE))
#define V2P(x) vmparam_kva_to_phys((uintptr_t)(x))

#endif
