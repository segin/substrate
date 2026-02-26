/*
 * gdt.c - x86_64 Global Descriptor Table and Task State Segment
 *
 * In Long Mode, segmentation is mostly disabled. The GDT still defines
 * code/data segment types and privilege levels, but segment bases and
 * limits are ignored (except for FS/GS bases used for TLS).
 *
 * The TSS is still needed for:
 * - RSP0-RSP2: Stack pointers for privilege transitions
 * - IST1-IST7: Interrupt Stack Table (for NMI, double fault, etc.)
 * - IOPB: I/O permission bitmap
 */

#include <stdint.h>
#include <string.h>
#include <sys/smp.h>
#include "gdt.h"

/* GDT Entry (8 bytes for normal, 16 bytes for system descriptors in LM) */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/* TSS Entry - 16 bytes in Long Mode (system descriptor) */
struct tss_entry {
    uint16_t limit_low;
    uint16_t base_0_15;
    uint8_t  base_16_23;
    uint8_t  access;
    uint8_t  limit_flags;
    uint8_t  base_24_31;
    uint32_t base_32_63;
    uint32_t reserved;
} __attribute__((packed));

/* GDT Pointer */
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* GDT Selectors */
#define GDT_NULL        0x00
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18    /* Note: User data before code for SYSRET */
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28    /* TSS is 16 bytes (2 GDT slots) */

/* Access byte flags */
#define GDT_PRESENT     0x80
#define GDT_DPL0        0x00
#define GDT_DPL3        0x60
#define GDT_TYPE_CODE   0x1A    /* Execute/Read */
#define GDT_TYPE_DATA   0x12    /* Read/Write */
#define GDT_TYPE_TSS    0x09    /* Available 64-bit TSS */

/* Granularity byte flags */
#define GDT_LONG_MODE   0x20    /* L bit: Long Mode code segment */
#define GDT_GRAN_4K     0x80    /* 4KB granularity */

/* Per-CPU GDT and TSS (index 0 is BSP) */
static struct gdt_entry per_cpu_gdt[MAX_CPUS][7] __attribute__((aligned(16)));
static struct tss64 per_cpu_tss[MAX_CPUS] __attribute__((aligned(16)));

/* Interrupt stacks for IST (per-CPU) */
static char per_cpu_ist_stack_nmi[MAX_CPUS][8192] __attribute__((aligned(16)));
static char per_cpu_ist_stack_df[MAX_CPUS][8192]  __attribute__((aligned(16)));
static char per_cpu_ist_stack_mc[MAX_CPUS][8192]  __attribute__((aligned(16)));

/*
 * Set a regular GDT entry (8 bytes)
 */
static void gdt_set_entry_at(struct gdt_entry *gdt_base, int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity) {
    gdt_base[index].limit_low = limit & 0xFFFF;
    gdt_base[index].base_low = base & 0xFFFF;
    gdt_base[index].base_mid = (base >> 16) & 0xFF;
    gdt_base[index].access = access;
    gdt_base[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    gdt_base[index].base_high = (base >> 24) & 0xFF;
}

/*
 * Set a TSS entry (16 bytes - spans 2 GDT slots)
 */
static void gdt_set_tss_at(struct gdt_entry *gdt_base, int index, uint64_t base, uint32_t limit) {
    struct tss_entry *te = (struct tss_entry *)&gdt_base[index];
    
    te->limit_low = limit & 0xFFFF;
    te->base_0_15 = base & 0xFFFF;
    te->base_16_23 = (base >> 16) & 0xFF;
    te->access = GDT_PRESENT | GDT_TYPE_TSS;
    te->limit_flags = ((limit >> 16) & 0x0F);
    te->base_24_31 = (base >> 24) & 0xFF;
    te->base_32_63 = (base >> 32) & 0xFFFFFFFF;
    te->reserved = 0;
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
    if (cpu_id >= MAX_CPUS) return;

    /* Pointer to this CPU's GDT */
    struct gdt_entry *gdt = per_cpu_gdt[cpu_id];

    /* Null descriptor */
    gdt_set_entry_at(gdt, 0, 0, 0, 0, 0);
    
    /* Kernel code segment (selector 0x08)
     * Long Mode: base and limit ignored, but L bit must be set */
    gdt_set_entry_at(gdt, 1, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL0 | GDT_TYPE_CODE,
                  GDT_LONG_MODE | GDT_GRAN_4K);
    
    /* Kernel data segment (selector 0x10) */
    gdt_set_entry_at(gdt, 2, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL0 | GDT_TYPE_DATA,
                  GDT_GRAN_4K);
    
    /* User data segment (selector 0x18)
     * Must come before user code for SYSRET to work correctly */
    gdt_set_entry_at(gdt, 3, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL3 | GDT_TYPE_DATA,
                  GDT_GRAN_4K);
    
    /* User code segment (selector 0x20) */
    gdt_set_entry_at(gdt, 4, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL3 | GDT_TYPE_CODE,
                  GDT_LONG_MODE | GDT_GRAN_4K);
    
    /* TSS (selector 0x28, spans slots 5-6) */
    tss_init(&per_cpu_tss[cpu_id], cpu_id, rsp0);
    gdt_set_tss_at(gdt, 5, (uint64_t)&per_cpu_tss[cpu_id], sizeof(struct tss64) - 1);
    
    /* Load GDT */
    struct gdt_ptr gp;
    gp.limit = sizeof(per_cpu_gdt[cpu_id]) - 1;
    gp.base = (uint64_t)gdt;
    
#ifndef HOST_TEST
    __asm__ volatile(
        "lgdt %0\n\t"
        /* Reload segment registers */
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"    /* Clear FS/GS for now (TLS sets later) */
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "m"(gp)
        : "rax", "memory"
    );
#endif
    
    /* Load TSS */
#ifndef HOST_TEST
    __asm__ volatile(
        "ltr %w0"
        :
        : "r"((uint16_t)GDT_TSS)
    );
#endif
}

/*
 * Initialize GDT and TSS for Long Mode (BSP)
 */
void gdt_init(void) {
    extern char stack_top[];  /* From boot.S */
    /* Initialize BSP (CPU 0) */
    gdt_init_percpu(0, (uint64_t)stack_top);
}

/*
 * Set kernel stack pointer in TSS (for syscall/interrupt)
 */
void tss_set_rsp0(uint64_t rsp0) {
    /* Use smp_get_cpu_id() to update correct TSS */
    int cpu = smp_get_cpu_id();
    if (cpu < MAX_CPUS) {
        per_cpu_tss[cpu].rsp0 = rsp0;
    }
}

/*
 * Get pointer to TSS (for per-CPU access)
 */
struct tss64 *tss_get(void) {
    int cpu = smp_get_cpu_id();
    if (cpu < MAX_CPUS) {
        return &per_cpu_tss[cpu];
    }
    return &per_cpu_tss[0]; /* Fallback */
}

/*
 * Set FS base (used for TLS in userspace)
 */
void set_fs_base(uint64_t base) {
    /* FS.base is set via MSR 0xC0000100 (IA32_FS_BASE) */
#ifndef HOST_TEST
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(0xC0000100), "a"((uint32_t)base), "d"((uint32_t)(base >> 32))
    );
#endif
}

/*
 * Set GS base (used for per-CPU data in kernel)
 */
void set_gs_base(uint64_t base) {
    /* GS.base is set via MSR 0xC0000101 (IA32_GS_BASE) */
#ifndef HOST_TEST
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(0xC0000101), "a"((uint32_t)base), "d"((uint32_t)(base >> 32))
    );
#endif
}

/*
 * Set kernel GS base (swapped on SWAPGS instruction)
 */
void set_kernel_gs_base(uint64_t base) {
    /* KernelGSbase is set via MSR 0xC0000102 (IA32_KERNEL_GS_BASE) */
#ifndef HOST_TEST
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(0xC0000102), "a"((uint32_t)base), "d"((uint32_t)(base >> 32))
    );
#endif
}
