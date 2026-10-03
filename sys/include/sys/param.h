#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#ifndef HZ
#define HZ 250
#endif

#define USEC_PER_TICK (1000000 / HZ)

/* Kernel address space (KERN_BASE, P2V/V2P, ...) is per architecture. */
#include <machine/vmparam.h>

#define USER_STACK_MIN  0x00001000  /* Minimum valid user stack address */
#define USER_STACK_MAX  0x00800000  /* 8 MiB grow-down user-stack ceiling
                                     * (RLIMIT_STACK; also the exec-time
                                     * demand-paged stack limit). */

/* Page geometry (x86, 4 KiB pages). */
#define PAGE_SHIFT      12
#define PAGE_SIZE       (1U << PAGE_SHIFT)   /* 4096 */
/* Pointer-wide, so ~PAGE_MASK keeps the high bits of a 64-bit address. */
#define PAGE_MASK       ((uintptr_t)PAGE_SIZE - 1)

#endif
