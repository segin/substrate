#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#ifndef HZ
#define HZ 250
#endif

#define USEC_PER_TICK (1000000 / HZ)

/* Memory layout (Temporary defaults, should be arch-specific) */
#define KERN_BASE       0xC0000000
#define USER_STACK_MIN  0x00001000  /* Minimum valid user stack address */
#define USER_STACK_MAX  0x00800000  /* 8 MiB grow-down user-stack ceiling
                                     * (RLIMIT_STACK; also the exec-time
                                     * demand-paged stack limit). */

/* Page geometry (x86, 4 KiB pages). */
#define PAGE_SHIFT      12
#define PAGE_SIZE       (1U << PAGE_SHIFT)   /* 4096 */
#define PAGE_MASK       (PAGE_SIZE - 1U)

#endif
