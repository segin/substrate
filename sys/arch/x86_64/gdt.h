/*
 * gdt.h - x86_64 Global Descriptor Table and TSS
 */

#ifndef _ARCH_X86_64_GDT_H
#define _ARCH_X86_64_GDT_H

#include <stdint.h>

/*
 * GDT segment selectors.
 *
 * The order of the first five is fixed by SYSCALL/SYSRET.  STAR[47:32] =
 * SEL_KCODE: SYSCALL loads CS from it and SS from it + 8 (SEL_KDATA).
 * STAR[63:48] = SEL_UCODE32: a 64-bit SYSRET loads CS from it + 16
 * (SEL_UCODE) and SS from it + 8 (SEL_UDATA); a 32-bit SYSRET loads CS
 * from it directly.  SEL_UCODE32 is also what 32-bit (IA-32 compatibility
 * mode) processes run on -- the existing i386 userland
 * (docs/specs/abi-amd64.md, section 10).  One data segment serves both
 * bitnesses.
 *
 * The user selectors come out the same as the i386 kernel's (0x1B code,
 * 0x23 data), and so do the three TLS slots 6-8 that set_thread_area()
 * and the %gs TLS base use (selector 0x33 is slot 6), so a 32-bit
 * process sees the same selector values on either kernel.
 */
#define SEL_NULL        0x00
#define SEL_KCODE       0x08    /* kernel code, 64-bit */
#define SEL_KDATA       0x10    /* kernel data */
#define SEL_UCODE32     0x18    /* user code, 32-bit (compatibility mode) */
#define SEL_UDATA       0x20    /* user data */
#define SEL_UCODE       0x28    /* user code, 64-bit */
#define SEL_TLS         0x30    /* GDT_TLS_START: three TLS descriptors */
#define SEL_TSS         0x48    /* TSS: a 16-byte descriptor, two slots */
#define SEL_LDT         0x58    /* LDT: a 16-byte descriptor, two slots */
#define GDT_SLOTS       13

/* Selector with RPL (Ring Privilege Level) */
#define SEL_UCODE32_RPL3 (SEL_UCODE32 | 3)
#define SEL_UCODE_RPL3   (SEL_UCODE | 3)
#define SEL_UDATA_RPL3   (SEL_UDATA | 3)

/* An 8-byte segment descriptor.  The field names are arch/i386/gdt.h's,
 * which <sys/ldt.h> and the segmented personalities read. */
struct gdt_entry_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));
typedef struct gdt_entry_struct gdt_entry_t;

struct gdt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
typedef struct gdt_ptr_struct gdt_ptr_t;

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

/* Initialize the boot CPU's GDT and TSS, and load them. */
void gdt_init(void);

/* Initialize and load CPU cpu_id's GDT and TSS, kernel stack rsp0. */
void gdt_init_percpu(int cpu_id, uint64_t rsp0);

/* The kernel stack a trap from user mode switches to (TSS.RSP0). */
void set_kernel_stack(uintptr_t stack);
void tss_set_rsp0(uint64_t rsp0);
struct tss64 *tss_get(void);

/* Write an 8-byte descriptor into slot `num` of this CPU's GDT, as
 * arch/i386/gdt.h's gdt_set_gate does (the TLS slots). */
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access,
                  uint8_t gran);

/* Point this CPU's LDT descriptor at `base` (`limit` bytes - 1) and load
 * it; base 0 loads the null LDT. */
void gdt_load_ldt(uintptr_t base, uint32_t limit);

/* Enter a 32-bit process at `entry` with stack `stack` and %ebx = `ebx`,
 * on the flat user selectors (isr.S). */
void jump_to_userspace(uint32_t entry, uint32_t stack, uint32_t ebx);

/* The same with the given selectors, for segmented programs; %edx =
 * dx_value at entry. */
void jump_to_elks(uint32_t entry, uint32_t stack, uint32_t cs, uint32_t ds,
                  uint32_t ss, uint32_t es, uint32_t dx_value);

#endif /* _ARCH_X86_64_GDT_H */
