/*
 * vmparam.h - i386 kernel address-space constants
 *
 * The kernel occupies the top gigabyte, 0xC0000000 up, and reaches
 * physical memory through a direct map that starts at the same address:
 * physical 0 is virtual KERN_BASE, and the kernel image itself lives
 * inside that map.  Everything below is user space.
 */
#ifndef _ARCH_I386_VMPARAM_H
#define _ARCH_I386_VMPARAM_H

#include <stdint.h>

/* Physical address 0 in the kernel direct map. */
#define KERN_BASE           0xC0000000U

/* Lowest kernel virtual address: anything at or above is kernel. */
#define KERNEL_VA_START     0xC0000000U

/* End of a 32-bit process's address space (exclusive). */
#define USER32_VA_END       0xC0000000U

/* Physical address to its direct-map kernel address, and back.  The kernel
 * image sits inside the direct map, so V2P works for any kernel address. */
#define P2V(x) ((void *)((uintptr_t)(x) + KERN_BASE))
#define V2P(x) ((uintptr_t)(x) - KERN_BASE)

#endif
