/*
 * boot.h - symbols defined by boot/boot.S and linker.ld
 */
#ifndef _ARCH_X86_64_BOOT_H
#define _ARCH_X86_64_BOOT_H

#include <stdint.h>

/* boot.S */
extern char boot_stack_top[];          /* the boot CPU's initial stack */
extern uint64_t boot_pml4[512];        /* the boot page tables' root */

/* linker.ld */
extern char _kernel_start[], _load_end[], _bss_end[], _kernel_end[];

/* Entered from boot.S in 64-bit mode at the higher half. */
void kmain64(uint32_t magic, uint32_t info_phys);

#endif /* _ARCH_X86_64_BOOT_H */
