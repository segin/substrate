/*
 * gdt.h - x86_64 Global Descriptor Table and TSS
 */

#ifndef _ARCH_X86_64_GDT_H
#define _ARCH_X86_64_GDT_H

#include <stdint.h>

/*
 * GDT Segment Selectors
 */
#define SEL_NULL        0x00
#define SEL_KCODE       0x08    /* Kernel code segment */
#define SEL_KDATA       0x10    /* Kernel data segment */
#define SEL_UDATA       0x18    /* User data segment (before code for SYSRET) */
#define SEL_UCODE       0x20    /* User code segment */
#define SEL_TSS         0x28    /* Task State Segment */

/* Selector with RPL (Ring Privilege Level) */
#define SEL_UCODE_RPL3  (SEL_UCODE | 3)
#define SEL_UDATA_RPL3  (SEL_UDATA | 3)

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
