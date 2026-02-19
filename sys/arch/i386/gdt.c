#include <arch/i386/gdt.h>
#include <arch/i386/percpu.h>
#include <stdint.h>
#include <string.h>



// Static helper to set GDT gate
static void set_gate(gdt_entry_t *gdt, int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;

    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

// Public wrapper for current CPU
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    set_gate(THIS_CPU()->gdt, num, base, limit, access, gran);
}

// Helper to setup TSS in a specific GDT
static void setup_tss(gdt_entry_t *gdt, tss_entry_t *tss, int32_t num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t) tss;
    // Limit is the SIZE, not base + size!
    uint32_t limit = sizeof(tss_entry_t) - 1;

    set_gate(gdt, num, base, limit, 0xE9, 0x00);

    memset(tss, 0, sizeof(tss_entry_t));

    tss->ss0  = ss0;
    tss->esp0 = esp0;
    
    // Set kernel segments (0x10 = Data Segment)
    tss->cs   = 0x08; // Code Segment (just in case)
    tss->ss = tss->ds = tss->es = tss->fs = tss->gs = 0x10;
    
    // Initialize I/O bitmap offset
    tss->iomap_base = (uint16_t)((uintptr_t)&tss->iomap - (uintptr_t)tss);
    
    // Initialize all ports to 1 (deny access)
    memset(tss->iomap, 0xFF, sizeof(tss->iomap));
    
    // Terminator byte
    tss->iomap_end = 0xFF;
}

// Initialize GDT and TSS for a specific CPU
void gdt_init_cpu(int cpu_id) {
    struct percpu_data *pcpu = percpu_get_cpu(cpu_id);
    if (!pcpu) return;

    gdt_entry_t *gdt = pcpu->gdt;
    tss_entry_t *tss = &pcpu->tss;

    // Set up GDT pointer
    pcpu->gdt_ptr.limit = (sizeof(gdt_entry_t) * 10) - 1;
    pcpu->gdt_ptr.base  = (uint32_t)gdt;

    // 0: Null segment
    set_gate(gdt, 0, 0, 0, 0, 0);
    // 1: Code segment (0x08)
    set_gate(gdt, 1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    // 2: Data segment (0x10)
    set_gate(gdt, 2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    // 3: User mode code segment (0x1B)
    set_gate(gdt, 3, 0, 0xFFFFFFFF, 0xFB, 0xCF);
    // 4: User mode data segment (0x23)
    set_gate(gdt, 4, 0, 0xFFFFFFFF, 0xF3, 0xCF);
    
    // 5: TSS
    setup_tss(gdt, tss, 5, 0x10, 0x0); // ESP0 set later

    // 6: TLS segment (initially empty)
    set_gate(gdt, 6, 0, 0xFFFFFFFF, 0xF3, 0xCF);

    // If initializing the current CPU (e.g. BSP boot), load it now
    // Note: checking cpu_id == 0 is heuristic.
    // For APs, this function is called from BSP, so we DON'T load it here.
    if (cpu_id == 0) {
        gdt_flush((uint32_t)&pcpu->gdt_ptr);
        tss_flush();
    }
}

// Initialize GDT for BSP
void gdt_init() {
    gdt_init_cpu(0);
}

// Set kernel stack in current CPU's TSS
void set_kernel_stack(uint32_t stack) {
    THIS_CPU()->tss.esp0 = stack;
}

/*
 * tss_iomap_init - Initialize I/O permission bitmap
 */
void tss_iomap_init(void) {
    tss_entry_t *tss = &THIS_CPU()->tss;
    memset(tss->iomap, 0xFF, sizeof(tss->iomap));
    tss->iomap_end = 0xFF;
}

/*
 * tss_set_iomap - Allow or deny access to a single I/O port
 */
void tss_set_iomap(uint16_t port, int allow) {
    tss_entry_t *tss = &THIS_CPU()->tss;
    int byte_idx = port / 8;
    int bit_idx = port % 8;
    
    if (allow) {
        tss->iomap[byte_idx] &= ~(1 << bit_idx);
    } else {
        tss->iomap[byte_idx] |= (1 << bit_idx);
    }
}

/*
 * tss_set_iomap_range - Allow or deny access to a range of I/O ports
 */
void tss_set_iomap_range(uint16_t start, uint16_t end, int allow) {
    for (uint32_t port = start; port <= end; port++) {
        tss_set_iomap((uint16_t)port, allow);
    }
}
