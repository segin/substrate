/*
 * gdt.c - x86_64 Global Descriptor Table and Task State Segment
 *
 * In long mode the GDT still defines the code and data segments' types,
 * bitness and privilege levels, but 64-bit code ignores their bases and
 * limits.  A 32-bit (compatibility-mode) process does not: its TLS
 * descriptors (slots 6-8) carry real bases, and its LDT can hold any
 * segment it asks for, exactly as on the i386 kernel.
 *
 * The TSS is still needed for:
 * - RSP0: the kernel stack a trap from user mode switches to
 * - IST1-IST3: separate stacks for NMI, double fault and machine check
 * - IOPB: the I/O permission bitmap (none: every port traps)
 */

#include <stdint.h>
#include <string.h>

#include <arch/x86_64/boot.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86-common/msr.h>
#include <sys/smp.h>

/* System descriptor (TSS, LDT): 16 bytes, two GDT slots, in long mode. */
struct sys_desc {
    uint16_t limit_low;
    uint16_t base_0_15;
    uint8_t  base_16_23;
    uint8_t  access;
    uint8_t  limit_flags;
    uint8_t  base_24_31;
    uint32_t base_32_63;
    uint32_t reserved;
} __attribute__((packed));

/* Access byte flags */
#define GDT_PRESENT     0x80
#define GDT_DPL0        0x00
#define GDT_DPL3        0x60
#define GDT_TYPE_CODE   0x1A    /* Execute/Read */
#define GDT_TYPE_DATA   0x12    /* Read/Write */
#define GDT_TYPE_LDT    0x02    /* LDT */
#define GDT_TYPE_TSS    0x09    /* Available 64-bit TSS */

/* Granularity byte flags */
#define GDT_LONG_MODE   0x20    /* L bit: Long Mode code segment */
#define GDT_SIZE_32     0x40    /* D bit: 32-bit default operand size */
#define GDT_GRAN_4K     0x80    /* 4KB granularity */

/* Per-CPU GDT and TSS (index 0 is BSP) */
static gdt_entry_t per_cpu_gdt[MAX_CPUS][GDT_SLOTS] __attribute__((aligned(16)));
static struct tss64 per_cpu_tss[MAX_CPUS] __attribute__((aligned(16)));

/* Interrupt stacks for IST (per-CPU) */
static char per_cpu_ist_stack_nmi[MAX_CPUS][8192] __attribute__((aligned(16)));
static char per_cpu_ist_stack_df[MAX_CPUS][8192]  __attribute__((aligned(16)));
static char per_cpu_ist_stack_mc[MAX_CPUS][8192]  __attribute__((aligned(16)));

static int gdt_cpu(void) {
    int cpu = smp_get_cpu_id();

    return (cpu >= 0 && cpu < MAX_CPUS) ? cpu : 0;
}

/*
 * Set a regular GDT entry (8 bytes)
 */
static void gdt_set_entry_at(gdt_entry_t *gdt_base, int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity) {
    gdt_base[index].limit_low = limit & 0xFFFF;
    gdt_base[index].base_low = base & 0xFFFF;
    gdt_base[index].base_middle = (base >> 16) & 0xFF;
    gdt_base[index].access = access;
    gdt_base[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    gdt_base[index].base_high = (base >> 24) & 0xFF;
}

/*
 * Set a system descriptor (16 bytes - spans 2 GDT slots)
 */
static void gdt_set_sys_at(gdt_entry_t *gdt_base, int index, uint8_t type,
                           uint64_t base, uint32_t limit) {
    struct sys_desc *d = (struct sys_desc *)&gdt_base[index];

    d->limit_low = limit & 0xFFFF;
    d->base_0_15 = base & 0xFFFF;
    d->base_16_23 = (base >> 16) & 0xFF;
    d->access = GDT_PRESENT | type;
    d->limit_flags = ((limit >> 16) & 0x0F);
    d->base_24_31 = (base >> 24) & 0xFF;
    d->base_32_63 = (base >> 32) & 0xFFFFFFFF;
    d->reserved = 0;
}

/*
 * Initialize the TSS
 */
static void tss_init(struct tss64 *tss_ptr, int cpu_id, uint64_t rsp0) {
    memset(tss_ptr, 0, sizeof(struct tss64));

    tss_ptr->rsp0 = rsp0;
    tss_ptr->rsp1 = 0;
    tss_ptr->rsp2 = 0;

    /* Set up Interrupt Stack Table for critical exceptions */
    tss_ptr->ist1 = (uint64_t)per_cpu_ist_stack_nmi[cpu_id] + sizeof(per_cpu_ist_stack_nmi[cpu_id]);  /* NMI */
    tss_ptr->ist2 = (uint64_t)per_cpu_ist_stack_df[cpu_id] + sizeof(per_cpu_ist_stack_df[cpu_id]);    /* Double Fault */
    tss_ptr->ist3 = (uint64_t)per_cpu_ist_stack_mc[cpu_id] + sizeof(per_cpu_ist_stack_mc[cpu_id]);    /* Machine Check */
    tss_ptr->ist4 = 0;
    tss_ptr->ist5 = 0;
    tss_ptr->ist6 = 0;
    tss_ptr->ist7 = 0;

    /* No IOPB (I/O Permission Bitmap) - set offset past TSS end */
    tss_ptr->iopb_offset = sizeof(struct tss64);
}

/*
 * Initialize per-CPU GDT/TSS for SMP
 */
void gdt_init_percpu(int cpu_id, uint64_t rsp0) {
    if (cpu_id < 0 || cpu_id >= MAX_CPUS) return;

    /* Pointer to this CPU's GDT */
    gdt_entry_t *gdt = per_cpu_gdt[cpu_id];

    memset(gdt, 0, sizeof(per_cpu_gdt[cpu_id]));

    /* Kernel code (0x08).  Long mode ignores base and limit; L is set. */
    gdt_set_entry_at(gdt, SEL_KCODE >> 3, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL0 | GDT_TYPE_CODE,
                  GDT_LONG_MODE | GDT_GRAN_4K);

    /* Kernel data (0x10) */
    gdt_set_entry_at(gdt, SEL_KDATA >> 3, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL0 | GDT_TYPE_DATA,
                  GDT_SIZE_32 | GDT_GRAN_4K);

    /* User code, 32-bit (0x18): flat 4 GiB, D set, L clear -- IA-32
     * compatibility mode for the i386 userland. */
    gdt_set_entry_at(gdt, SEL_UCODE32 >> 3, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL3 | GDT_TYPE_CODE,
                  GDT_SIZE_32 | GDT_GRAN_4K);

    /* User data (0x20): flat 4 GiB, for both bitnesses. */
    gdt_set_entry_at(gdt, SEL_UDATA >> 3, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL3 | GDT_TYPE_DATA,
                  GDT_SIZE_32 | GDT_GRAN_4K);

    /* User code, 64-bit (0x28) */
    gdt_set_entry_at(gdt, SEL_UCODE >> 3, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL3 | GDT_TYPE_CODE,
                  GDT_LONG_MODE | GDT_GRAN_4K);

    /* TLS slots (0x30-0x40) start empty: not present.  The LDT descriptor
     * (0x58) too, until a process installs one. */

    /* TSS (0x48, two slots) */
    tss_init(&per_cpu_tss[cpu_id], cpu_id, rsp0);
    gdt_set_sys_at(gdt, SEL_TSS >> 3, GDT_TYPE_TSS,
                   (uint64_t)&per_cpu_tss[cpu_id], sizeof(struct tss64) - 1);

    /* Load GDT */
    gdt_ptr_t gp;
    gp.limit = sizeof(per_cpu_gdt[cpu_id]) - 1;
    gp.base = (uint64_t)gdt;

#ifndef HOST_TEST
    /*
     * Load it, then reload every segment register from it: CS through a
     * far return, the data segments directly.  The loader's GDT (boot.S)
     * sits at a physical address that stops being mapped once the
     * identity window is dropped.
     */
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw %w2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "m"(gp), "i"((uint64_t)SEL_KCODE), "i"(SEL_KDATA)
        : "rax", "memory"
    );

    __asm__ volatile("ltr %w0" : : "r"((uint16_t)SEL_TSS));
#endif
}

/*
 * Initialize GDT and TSS for Long Mode (BSP)
 */
void gdt_init(void) {
    /* The boot stack until there are threads with their own. */
    gdt_init_percpu(0, (uint64_t)boot_stack_top);
    syscall_msr_init();
}

void set_kernel_stack(uintptr_t stack) {
    tss_set_rsp0((uint64_t)stack);
}

/* The kernel stack SYSCALL switches to: the instruction, unlike an
 * interrupt gate, does not take it from the TSS (isr.S, syscall_entry64). */
uint64_t syscall_kernel_rsp;
uint64_t syscall_user_rsp;

void tss_set_rsp0(uint64_t rsp0) {
    per_cpu_tss[gdt_cpu()].rsp0 = rsp0;
    syscall_kernel_rsp = rsp0;
}

/*
 * Enable SYSCALL for native 64-bit processes (docs/specs/abi-amd64.md,
 * section 3).  STAR names the kernel selectors SYSCALL loads; the return
 * is by IRETQ through the common trap exit, so the SYSRET half is unused.
 * FMASK clears IF until the entry stub is on the kernel stack, and DF/TF.
 */
void syscall_msr_init(void) {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    wrmsr(0xC0000081 /* STAR */,
          ((uint64_t)SEL_KCODE << 32) | ((uint64_t)SEL_UCODE32_RPL3 << 48));
    wrmsr(0xC0000082 /* LSTAR */, (uint64_t)(uintptr_t)syscall_entry64);
    wrmsr(0xC0000084 /* FMASK */, 0x200 | 0x400 | 0x100);
}

struct tss64 *tss_get(void) {
    return &per_cpu_tss[gdt_cpu()];
}

void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access,
                  uint8_t gran) {
    if (num <= 0 || num >= GDT_SLOTS)
        return;
    /* Never overwrite a system descriptor's second half. */
    if (num == (SEL_TSS >> 3) || num == (SEL_TSS >> 3) + 1 ||
        num == (SEL_LDT >> 3) || num == (SEL_LDT >> 3) + 1)
        return;
    gdt_set_entry_at(per_cpu_gdt[gdt_cpu()], num, base, limit, access, gran);
}

void gdt_load_ldt(uintptr_t base, uint32_t limit) {
    gdt_entry_t *gdt = per_cpu_gdt[gdt_cpu()];

    if (base == 0) {
        memset(&gdt[SEL_LDT >> 3], 0, 2 * sizeof(gdt_entry_t));
#ifndef HOST_TEST
        __asm__ volatile("lldt %w0" : : "r"((uint16_t)0));
#endif
        return;
    }
    gdt_set_sys_at(gdt, SEL_LDT >> 3, GDT_TYPE_LDT, base, limit);
#ifndef HOST_TEST
    __asm__ volatile("lldt %w0" : : "r"((uint16_t)SEL_LDT));
#endif
}
