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

/* 64-bit Task State Segment */
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;          /* Ring 0 stack pointer */
    uint64_t rsp1;          /* Ring 1 stack pointer */
    uint64_t rsp2;          /* Ring 2 stack pointer */
    uint64_t reserved1;
    uint64_t ist1;          /* Interrupt Stack Table 1 */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   /* I/O Permission Bitmap offset */
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

/* GDT with enough entries: null, kcode, kdata, udata, ucode, tss (2 slots) */
static struct gdt_entry gdt[7] __attribute__((aligned(16)));
static struct tss64 tss __attribute__((aligned(16)));
static struct gdt_ptr gdt_pointer;

/* Per-CPU TSS (for SMP, index 0 is BSP) */
#define MAX_CPUS 64
static struct tss64 per_cpu_tss[MAX_CPUS] __attribute__((aligned(16)));

/* Interrupt stacks for IST */
static char ist_stack_nmi[8192] __attribute__((aligned(16)));
static char ist_stack_df[8192]  __attribute__((aligned(16)));
static char ist_stack_mc[8192]  __attribute__((aligned(16)));

/*
 * Set a regular GDT entry (8 bytes)
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity) {
    gdt[index].limit_low = limit & 0xFFFF;
    gdt[index].base_low = base & 0xFFFF;
    gdt[index].base_mid = (base >> 16) & 0xFF;
    gdt[index].access = access;
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    gdt[index].base_high = (base >> 24) & 0xFF;
}

/*
 * Set a TSS entry (16 bytes - spans 2 GDT slots)
 */
static void gdt_set_tss(int index, uint64_t base, uint32_t limit) {
    struct tss_entry *te = (struct tss_entry *)&gdt[index];
    
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
static void tss_init(struct tss64 *tss_ptr, uint64_t rsp0) {
    memset(tss_ptr, 0, sizeof(struct tss64));
    
    tss_ptr->rsp0 = rsp0;
    tss_ptr->rsp1 = 0;
    tss_ptr->rsp2 = 0;
    
    /* Set up Interrupt Stack Table for critical exceptions */
    tss_ptr->ist1 = (uint64_t)ist_stack_nmi + sizeof(ist_stack_nmi);  /* NMI */
    tss_ptr->ist2 = (uint64_t)ist_stack_df + sizeof(ist_stack_df);    /* Double Fault */
    tss_ptr->ist3 = (uint64_t)ist_stack_mc + sizeof(ist_stack_mc);    /* Machine Check */
    tss_ptr->ist4 = 0;
    tss_ptr->ist5 = 0;
    tss_ptr->ist6 = 0;
    tss_ptr->ist7 = 0;
    
    /* No IOPB (I/O Permission Bitmap) - set offset past TSS end */
    tss_ptr->iopb_offset = sizeof(struct tss64);
}

/*
 * Initialize GDT and TSS for Long Mode
 */
void gdt_init(void) {
    /* Null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);
    
    /* Kernel code segment (selector 0x08)
     * Long Mode: base and limit ignored, but L bit must be set */
    gdt_set_entry(1, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL0 | GDT_TYPE_CODE,
                  GDT_LONG_MODE | GDT_GRAN_4K);
    
    /* Kernel data segment (selector 0x10) */
    gdt_set_entry(2, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL0 | GDT_TYPE_DATA,
                  GDT_GRAN_4K);
    
    /* User data segment (selector 0x18)
     * Must come before user code for SYSRET to work correctly */
    gdt_set_entry(3, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL3 | GDT_TYPE_DATA,
                  GDT_GRAN_4K);
    
    /* User code segment (selector 0x20) */
    gdt_set_entry(4, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL3 | GDT_TYPE_CODE,
                  GDT_LONG_MODE | GDT_GRAN_4K);
    
    /* TSS (selector 0x28, spans slots 5-6) */
    extern char stack_top[];  /* From boot.S */
    tss_init(&tss, (uint64_t)stack_top);
    gdt_set_tss(5, (uint64_t)&tss, sizeof(struct tss64) - 1);
    
    /* Load GDT */
    gdt_pointer.limit = sizeof(gdt) - 1;
    gdt_pointer.base = (uint64_t)&gdt;
    
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
        : "m"(gdt_pointer)
        : "rax", "memory"
    );
    
    /* Load TSS */
    __asm__ volatile(
        "ltr %w0"
        :
        : "r"((uint16_t)GDT_TSS)
    );
}

/*
 * Set kernel stack pointer in TSS (for syscall/interrupt)
 */
void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

/*
 * Get pointer to TSS (for per-CPU access)
 */
struct tss64 *tss_get(void) {
    return &tss;
}

/*
 * Initialize per-CPU GDT/TSS for SMP
 */
void gdt_init_percpu(int cpu_id, uint64_t rsp0) {
    if (cpu_id >= MAX_CPUS) return;
    
    tss_init(&per_cpu_tss[cpu_id], rsp0);
    
    /* Each CPU needs its own TSS entry in GDT
     * For now, just update the global TSS pointer */
    /* TODO: Implement full per-CPU GDT for SMP */
}

/*
 * Set FS base (used for TLS in userspace)
 */
void set_fs_base(uint64_t base) {
    /* FS.base is set via MSR 0xC0000100 (IA32_FS_BASE) */
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(0xC0000100), "a"((uint32_t)base), "d"((uint32_t)(base >> 32))
    );
}

/*
 * Set GS base (used for per-CPU data in kernel)
 */
void set_gs_base(uint64_t base) {
    /* GS.base is set via MSR 0xC0000101 (IA32_GS_BASE) */
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(0xC0000101), "a"((uint32_t)base), "d"((uint32_t)(base >> 32))
    );
}

/*
 * Set kernel GS base (swapped on SWAPGS instruction)
 */
void set_kernel_gs_base(uint64_t base) {
    /* KernelGSbase is set via MSR 0xC0000102 (IA32_KERNEL_GS_BASE) */
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(0xC0000102), "a"((uint32_t)base), "d"((uint32_t)(base >> 32))
    );
}
