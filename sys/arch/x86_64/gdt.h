/*
 * gdt.h - x86_64 Global Descriptor Table and TSS
 */

#ifndef _ARCH_X86_64_GDT_H
#define _ARCH_X86_64_GDT_H

#include <stdint.h>

/*
 * GDT segment selectors.
 *
 * The order is fixed by SYSCALL/SYSRET.  STAR[47:32] = SEL_KCODE: SYSCALL
 * loads CS from it and SS from it + 8 (SEL_KDATA).  STAR[63:48] =
 * SEL_UCODE32: a 64-bit SYSRET loads CS from it + 16 (SEL_UCODE) and SS
 * from it + 8 (SEL_UDATA); a 32-bit SYSRET loads CS from it directly.
 * SEL_UCODE32 is also what 32-bit (IA-32 compatibility mode) processes
 * run on -- the existing i386 userland (docs/specs/abi-amd64.md,
 * section 10).  One data segment serves both bitnesses.
 */
#define SEL_NULL        0x00
#define SEL_KCODE       0x08    /* kernel code, 64-bit */
#define SEL_KDATA       0x10    /* kernel data */
#define SEL_UCODE32     0x18    /* user code, 32-bit (compatibility mode) */
#define SEL_UDATA       0x20    /* user data */
#define SEL_UCODE       0x28    /* user code, 64-bit */
#define SEL_TSS         0x30    /* TSS: a 16-byte descriptor, two slots */
#define GDT_SLOTS       8

/* Selector with RPL (Ring Privilege Level) */
#define SEL_UCODE32_RPL3 (SEL_UCODE32 | 3)
#define SEL_UCODE_RPL3   (SEL_UCODE | 3)
#define SEL_UDATA_RPL3   (SEL_UDATA | 3)

/*
 * 64-bit Task State Segment
 */
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;          /* Ring 0 stack pointer */
    uint64_t rsp1;          /* Ring 1 stack pointer (unused) */
    uint64_t rsp2;          /* Ring 2 stack pointer (unused) */
    uint64_t reserved1;
    uint64_t ist1;          /* Interrupt Stack Table 1 (NMI) */
    uint64_t ist2;          /* IST 2 (Double Fault) */
    uint64_t ist3;          /* IST 3 (Machine Check) */
    uint64_t ist4;          /* IST 4 */
    uint64_t ist5;          /* IST 5 */
    uint64_t ist6;          /* IST 6 */
    uint64_t ist7;          /* IST 7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   /* I/O Permission Bitmap offset */
} __attribute__((packed));

/*
 * IST indices for IDT entries
 */
#define IST_NMI         1       /* Non-Maskable Interrupt */
#define IST_DF          2       /* Double Fault */
#define IST_MC          3       /* Machine Check */

/*
 * GDT/TSS Functions
 */

/* Initialize GDT and TSS */
void gdt_init(void);

/* Set kernel stack pointer in TSS */
void tss_set_rsp0(uint64_t rsp0);

/* Get pointer to current TSS */
struct tss64 *tss_get(void);

/* Initialize per-CPU GDT/TSS for SMP */
void gdt_init_percpu(int cpu_id, uint64_t rsp0);

/* Set FS base register (TLS for userspace) */
void set_fs_base(uint64_t base);

/* Set GS base register (per-CPU data in kernel) */
void set_gs_base(uint64_t base);

/* Set kernel GS base (swapped on SWAPGS) */
void set_kernel_gs_base(uint64_t base);

#endif /* _ARCH_X86_64_GDT_H */
