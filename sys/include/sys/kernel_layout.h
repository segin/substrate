/*
 * sys/kernel_layout.h - symbols the linker scripts and boot.S define
 *
 * Addresses only: there is no object behind them, so they are declared as
 * arrays of char and used by name (_kernel_start, not &_kernel_start).
 * The same form as arch/x86_64/boot.h, which declares some of them too.
 */
#ifndef _SYS_KERNEL_LAYOUT_H
#define _SYS_KERNEL_LAYOUT_H

/* The kernel image, from its first byte to the end of its BSS. */
extern char _kernel_start[], _kernel_end[];

/* The setup section, given back to the allocator after boot. */
extern char _setup_start[], _setup_end[];

/* The top of the boot stack (boot.S). */
extern char stack_top[];

#endif /* _SYS_KERNEL_LAYOUT_H */
