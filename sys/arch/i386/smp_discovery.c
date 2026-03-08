#include <arch/i386/smp.h>
#include <sys/smp.h>
#include <kern/console.h>
#include <arch/i386/early_boot.h>
#include <string.h>
#include <stdint.h>
#include <arch/i386/percpu.h>
#include <arch/i386/gdt.h>
#include <arch/i386/pmap.h>
#include <sys/proc.h>
#include <arch/x86-common/lapic.h>
#include <arch/x86-common/ioapic.h>
#include <stdio.h>

// Externs for Scheduler and IDT
#ifndef HOST_TEST
extern process_t processes[];
extern thread_t *sched_alloc_thread(process_t *proc);

// IDT Pointer (defined in idt.c)
typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;
extern idt_ptr_t idt_ptr;
extern void idt_flush(uint32_t);

// TSS Flush (defined in isr.S)
extern void tss_flush(void);

// GDT Flush (defined in gdt.c/isr.S)
extern void gdt_flush(uint32_t);
#endif

/*
 * Early boot page tables in boot.S map only the first 16MB into the higher
 * half direct-map window (0xC0000000 + phys). ACPI pointers outside this
 * range must not be dereferenced during early SMP discovery.
 */
#define EARLY_DIRECTMAP_LIMIT 0x01000000u
#define FULL_DIRECTMAP_LIMIT  0x3EC00000u

cpu_info_t cpus[MAX_CPUS];
int cpu_count = 0;
static uint32_t smp_mp_config_phys = 0;

static void smp_emit_u32(void (*emit)(const char *), uint32_t value) {
    char buf[16];
    sprintf(buf, "%u", value);
    emit(buf);
}

static uint32_t smp_discovery_map_limit(void) {
    pmap_t kpmap = pmap_kernel();
    if (kpmap && kpmap->pdir_phys) {
        return FULL_DIRECTMAP_LIMIT;
    }
    return EARLY_DIRECTMAP_LIMIT;
}

// ACPI Structure Definitions (Simplified)
struct rsdp_desc {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;
} __attribute__((packed));

struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct mp_floating_ptr {
    char signature[4];
    uint32_t config_table;
    uint8_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    uint8_t feature1;
    uint8_t feature2;
    uint8_t feature3;
    uint8_t feature4;
    uint8_t feature5;
} __attribute__((packed));

struct mp_config_table {
    char signature[4];
    uint16_t base_length;
    uint8_t spec_rev;
    uint8_t checksum;
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table_ptr;
    uint16_t oem_table_size;
    uint16_t entry_count;
    uint32_t lapic_addr;
    uint16_t ext_table_length;
    uint8_t ext_table_checksum;
    uint8_t reserved;
} __attribute__((packed));

struct mp_processor_entry {
    uint8_t type;
    uint8_t local_apic_id;
    uint8_t local_apic_version;
    uint8_t cpu_flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t reserved[2];
} __attribute__((packed));

struct mp_ioapic_entry {
    uint8_t type;
    uint8_t ioapic_id;
    uint8_t ioapic_version;
    uint8_t ioapic_flags;
    uint32_t ioapic_addr;
} __attribute__((packed));

#define MP_PROCESSOR_ENABLED 0x01
#define MP_PROCESSOR_BSP     0x02
#define MP_IOAPIC_ENABLED    0x01

typedef void *(*smp_phys_map_fn_t)(uint32_t phys, uint32_t map_limit);

static void *smp_map_phys_default(uint32_t phys, uint32_t map_limit) {
    if (phys >= map_limit) {
        return NULL;
    }
    return P2V(phys);
}

static int smp_checksum_ok(const void *base, size_t len) {
    const uint8_t *bytes = (const uint8_t *)base;
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }

    return sum == 0;
}

static int smp_mpfp_valid(const struct mp_floating_ptr *mpfp) {
    if (!mpfp || memcmp(mpfp->signature, "_MP_", 4) != 0) {
        return 0;
    }
    if (mpfp->length != 1) {
        return 0;
    }
    if (mpfp->spec_rev != 1 && mpfp->spec_rev != 4) {
        return 0;
    }
    return smp_checksum_ok(mpfp, 16);
}

static int smp_parse_mp_config(const struct mp_config_table *mpc, uint32_t bsp_id,
                               uint32_t map_limit) {
    const uint8_t *entry;
    int found = 0;
    int saw_bsp = 0;
    int saw_cpu = 0;

    if (!mpc || memcmp(mpc->signature, "PCMP", 4) != 0) {
        return 0;
    }
    if (mpc->base_length < sizeof(*mpc) || !smp_checksum_ok(mpc, mpc->base_length)) {
        return 0;
    }

    lapic_set_base(mpc->lapic_addr);

    entry = (const uint8_t *)mpc + sizeof(*mpc);
    for (uint16_t i = 0; i < mpc->entry_count && entry < (const uint8_t *)mpc + mpc->base_length; i++) {
        switch (entry[0]) {
        case 0: {
            const struct mp_processor_entry *cpu = (const struct mp_processor_entry *)entry;
            entry += sizeof(*cpu);

            if ((cpu->cpu_flags & MP_PROCESSOR_ENABLED) == 0) {
                break;
            }
            saw_cpu = 1;

            if ((cpu->cpu_flags & MP_PROCESSOR_BSP) != 0 || cpu->local_apic_id == bsp_id) {
                cpus[0].lapic_id = cpu->local_apic_id;
                cpus[0].processor_id = cpu->local_apic_id;
                cpus[0].flags = cpu->cpu_flags;
                saw_bsp = 1;
                found = 1;
            } else if (cpu_count < MAX_CPUS) {
                cpus[cpu_count].lapic_id = cpu->local_apic_id;
                cpus[cpu_count].processor_id = cpu->local_apic_id;
                cpus[cpu_count].flags = cpu->cpu_flags;
                cpu_count++;
                found = 1;
            }
            break;
        }
        case 1: /* bus entry */
            entry += 8;
            break;
        case 2: {
            const struct mp_ioapic_entry *ioapic = (const struct mp_ioapic_entry *)entry;

            if ((ioapic->ioapic_flags & MP_IOAPIC_ENABLED) != 0 &&
                map_limit == FULL_DIRECTMAP_LIMIT) {
                ioapic_register(ioapic->ioapic_addr, ioapic->ioapic_id, 0);
            }
            entry += sizeof(*ioapic);
            break;
        }
        case 3: /* IO interrupt assignment */
        case 4: /* local interrupt assignment */
            entry += 8;
            break;
        default:
            return found;
        }
    }

    if (saw_cpu && !saw_bsp) {
        cpus[0].lapic_id = bsp_id;
        cpus[0].processor_id = bsp_id;
        cpus[0].flags = MP_PROCESSOR_ENABLED | MP_PROCESSOR_BSP;
    }

    return found;
}

static int smp_try_mp_config_phys(uint32_t config_phys, uint32_t bsp_id,
                                  uint32_t map_limit, smp_phys_map_fn_t mapper) {
    const struct mp_config_table *mpc;

    if (config_phys == 0) {
        return 0;
    }

    mpc = (const struct mp_config_table *)mapper(config_phys, map_limit);
    if (!mpc) {
        return 0;
    }

    if (!smp_parse_mp_config(mpc, bsp_id, map_limit)) {
        return 0;
    }

    smp_mp_config_phys = config_phys;
    return 1;
}

static int smp_try_mp_search_range(uint32_t start, uint32_t end, uint32_t stride,
                                   uint32_t bsp_id, uint32_t map_limit,
                                   smp_phys_map_fn_t mapper) {
    for (uint32_t addr = start;
         addr + sizeof(struct mp_floating_ptr) <= end;
         addr += stride) {
        const struct mp_floating_ptr *mpfp =
            (const struct mp_floating_ptr *)mapper(addr, map_limit);

        if (!smp_mpfp_valid(mpfp)) {
            continue;
        }
        if (smp_try_mp_config_phys(mpfp->config_table, bsp_id, map_limit, mapper)) {
            return 1;
        }
    }

    return 0;
}

static int smp_try_mp_tables(uint32_t bsp_id, uint32_t map_limit, smp_phys_map_fn_t mapper) {
    static const struct {
        uint32_t start;
        uint32_t end;
        uint32_t stride;
    } fixed_searches[] = {
        { 0xF0000u, 0x100000u, 16u },
        { 0xE0000u, 0x100000u, 16u },
    };
    uint16_t *ebda_seg_ptr;
    uint16_t *base_mem_kb_ptr;
    uint32_t ebda_start = 0;
    uint32_t base_mem_top = 0;

    if (!mapper) {
        mapper = smp_map_phys_default;
    }

    if (smp_mp_config_phys != 0 &&
        smp_try_mp_config_phys(smp_mp_config_phys, bsp_id, map_limit, mapper)) {
        return 1;
    }

    if (map_limit == EARLY_DIRECTMAP_LIMIT) {
        ebda_seg_ptr = (uint16_t *)mapper(0x40Eu, map_limit);
        if (ebda_seg_ptr != NULL) {
            ebda_start = (uint32_t)(*ebda_seg_ptr) << 4;
        }

        if (ebda_start >= 0x400 && ebda_start < 0xA0000) {
            if (smp_try_mp_search_range(ebda_start, ebda_start + 1024u, 16u,
                                        bsp_id, map_limit, mapper)) {
                return 1;
            }
        }

        base_mem_kb_ptr = (uint16_t *)mapper(0x413u, map_limit);
        if (base_mem_kb_ptr != NULL) {
            base_mem_top = (uint32_t)(*base_mem_kb_ptr) * 1024u;
        }
        if (base_mem_top >= 1024u && base_mem_top <= 0xA0000u) {
            uint32_t start = base_mem_top - 1024u;
            if (smp_try_mp_search_range(start, base_mem_top, 16u,
                                        bsp_id, map_limit, mapper)) {
                return 1;
            }
        }
    }

    for (size_t i = 0; i < sizeof(fixed_searches) / sizeof(fixed_searches[0]); i++) {
        if (smp_try_mp_search_range(fixed_searches[i].start, fixed_searches[i].end,
                                    fixed_searches[i].stride, bsp_id,
                                    map_limit, mapper)) {
            return 1;
        }
    }

    return 0;
}

void smp_discover_cores(void) {
    early_uart_print("SMP: Discovering cores...\n");
    uint32_t map_limit = smp_discovery_map_limit();
    
    // Default to 1 CPU (Bootstrap Processor)
    cpu_count = 1;

    // Try to detect BSP LAPIC ID directly from hardware
    // Accessing default LAPIC address (mapped at 0xFEE00000 by boot.S)
    // Note: We assume LAPIC is enabled or at least in a state where ID can be read.
    // If this causes a fault, it means mapping is missing (unlikely given boot.S).
    uint32_t bsp_id = 0;
    uint32_t *lapic_id_reg = (uint32_t*)(0xFEE00000 + 0x20);
    bsp_id = (*lapic_id_reg) >> 24;

    early_uart_print("SMP: BSP LAPIC ID: ");
    smp_emit_u32(early_uart_print, bsp_id);
    early_uart_print("\n");

    cpus[0].lapic_id = bsp_id;
    cpus[0].processor_id = 0; // Unknown from LAPIC, will fill from MADT if found?
    cpus[0].flags = 1;

    // 1. Search for RSDP
    // Standard search: 0xE0000 to 0xFFFFF
    struct rsdp_desc *rsdp = NULL;
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        // Access via virtual address in higher half
        if (memcmp(P2V(addr), "RSD PTR ", 8) == 0) {
            rsdp = (struct rsdp_desc*)P2V(addr);
            break;
        }
    }

    if (!rsdp) {
        if (smp_try_mp_tables(bsp_id, map_limit, smp_map_phys_default)) {
            early_uart_print("SMP: MP Tables found.\n");
            early_uart_print("SMP: Detected CPU count: ");
            smp_emit_u32(early_uart_print, (uint32_t)cpu_count);
            early_uart_print("\n");
            return;
        }
        early_uart_print("SMP: ACPI RSDP not found, falling back to UP.\n");
        return;
    }

    // 2. Locate MADT
    // rsdp->rsdt_addr is physical, convert to virtual
    if (rsdp->rsdt_addr >= map_limit) {
        if (smp_try_mp_tables(bsp_id, map_limit, smp_map_phys_default)) {
            early_uart_print("SMP: MP Tables found.\n");
            early_uart_print("SMP: Detected CPU count: ");
            smp_emit_u32(early_uart_print, (uint32_t)cpu_count);
            early_uart_print("\n");
            return;
        }
        early_uart_print("SMP: RSDT above early map, falling back to UP.\n");
        return;
    }
    struct acpi_header *rsdt = (struct acpi_header*)P2V(rsdp->rsdt_addr);

    // Validate RSDT signature
    if (memcmp(rsdt->signature, "RSDT", 4) != 0) {
        if (smp_try_mp_tables(bsp_id, map_limit, smp_map_phys_default)) {
            early_uart_print("SMP: MP Tables found.\n");
            early_uart_print("SMP: Detected CPU count: ");
            smp_emit_u32(early_uart_print, (uint32_t)cpu_count);
            early_uart_print("\n");
            return;
        }
        early_uart_print("SMP: RSDT Invalid signature!\n");
        return;
    }
    int entries = (rsdt->length - sizeof(struct acpi_header)) / 4;
    uint32_t *ptrs = (uint32_t*)((uintptr_t)rsdt + sizeof(struct acpi_header));

    struct acpi_header *madt = NULL;
    for (int i = 0; i < entries; i++) {
        // ptrs[i] contains physical address of a table
        if (ptrs[i] >= map_limit) {
            continue;
        }
        struct acpi_header *h = (struct acpi_header*)P2V(ptrs[i]);
        if (memcmp(h->signature, "APIC", 4) == 0) {
            madt = h;
            break;
        }
    }

    if (!madt) {
        if (smp_try_mp_tables(bsp_id, map_limit, smp_map_phys_default)) {
            early_uart_print("SMP: MP Tables found.\n");
            early_uart_print("SMP: Detected CPU count: ");
            smp_emit_u32(early_uart_print, (uint32_t)cpu_count);
            early_uart_print("\n");
            return;
        }
        early_uart_print("SMP: MADT not found.\n");
        return;
    }

    early_uart_print("SMP: MADT found.\n");

    // 3. Parse MADT Entries
    uint32_t lapic_base = *(uint32_t *)((uintptr_t)madt + sizeof(struct acpi_header));
    lapic_set_base(lapic_base);

    uint8_t *p = (uint8_t*)((uintptr_t)madt + sizeof(struct acpi_header) + 8); // Skip Local APIC Addr and Flags
    uint8_t *end = (uint8_t*)((uintptr_t)madt + madt->length);

    while (p < end) {
        uint8_t type = p[0];
        uint8_t length = p[1];

        if (length < 2) {
            break;
        }

        if (type == 0) { // Processor Local APIC
            uint8_t proc_id = p[2];
            uint8_t apic_id = p[3];
            uint32_t flags = *((uint32_t*)&p[4]);

            if (flags & 1) { // Enabled
                if (apic_id == bsp_id) {
                    // Update BSP info from MADT
                    cpus[0].processor_id = proc_id;
                    // flags?
                } else if (cpu_count < MAX_CPUS) {
                    cpus[cpu_count].processor_id = proc_id;
                    cpus[cpu_count].lapic_id = apic_id;
                    cpus[cpu_count].flags = (uint8_t)flags;
                    cpu_count++;
                }
            }
        } else if (type == 1 && length >= 12 && map_limit == FULL_DIRECTMAP_LIMIT) { // IO APIC
            uint8_t ioapic_id = p[2];
            uint32_t ioapic_addr = *((uint32_t *)&p[4]);
            uint32_t gsi_base = *((uint32_t *)&p[8]);
            ioapic_register(ioapic_addr, ioapic_id, gsi_base);
        } else if (type == 2 && length >= 10 && map_limit == FULL_DIRECTMAP_LIMIT) { // Interrupt Source Override
            uint8_t bus = p[2];
            uint8_t source_irq = p[3];
            uint32_t gsi = *((uint32_t *)&p[4]);
            uint16_t flags = *((uint16_t *)&p[8]);
            ioapic_register_isa_override(bus, source_irq, gsi, flags);
        }
        p += length;
    }

    early_uart_print("SMP: Detected CPU count: ");
    smp_emit_u32(early_uart_print, (uint32_t)cpu_count);
    early_uart_print("\n");
}

#ifndef HOST_TEST
extern void trampoline_start(void);
extern void trampoline_end(void);
extern void trampoline_cr3(void);
extern void trampoline_cr4(void);
extern void trampoline_cr0(void);
extern void trampoline_stack(void);
extern void trampoline_entry(void);

#define SMP_LOWMEM_PAGE_SIZE       0x1000u
#define SMP_LOWMEM_TRAMP_BASE      0x8000u
#define SMP_LOWMEM_TRAMP_LIMIT     0x100000u

// Per-AP stack (static for now, should be dynamically allocated per AP)
static char ap_stacks[MAX_CPUS][4096] __attribute__((aligned(16)));
static volatile int aps_ready = 0;
static uint32_t smp_lowmem_cursor = SMP_LOWMEM_TRAMP_BASE;
static uint32_t smp_trampoline_phys = 0;

static uint32_t trampoline_offset(void *symbol) {
    return (uint32_t)((uintptr_t)symbol - (uintptr_t)&trampoline_start);
}

static uint32_t smp_alloc_trampoline_page(void) {
    if (smp_trampoline_phys != 0) {
        return smp_trampoline_phys;
    }

    uint32_t phys = (smp_lowmem_cursor + SMP_LOWMEM_PAGE_SIZE - 1) &
                    ~(SMP_LOWMEM_PAGE_SIZE - 1);
    if (phys + SMP_LOWMEM_PAGE_SIZE > SMP_LOWMEM_TRAMP_LIMIT) {
        return 0;
    }

    smp_lowmem_cursor = phys + SMP_LOWMEM_PAGE_SIZE;
    smp_trampoline_phys = phys;
    return phys;
}

void smp_ap_entry(void) {
    // 1. Initialize AP-local state (GDT, TSS, IDT)
    // Get per-cpu data (using LAPIC ID)
    struct percpu_data *pcpu = percpu_get();

    // Load GDT
    gdt_flush((uint32_t)&pcpu->gdt_ptr);

    // Load IDT
    idt_flush((uint32_t)&idt_ptr);

    // Load TSS
    tss_flush();

    // Load LDT (null)
    __asm__ volatile("lldt %%ax" : : "a" (0));

    // Signal that we've started and initialized
    __sync_fetch_and_add(&aps_ready, 1);
    
    kprint("SMP: AP Core online (LAPIC ID: ");
    // Get and print LAPIC ID
    uint32_t id = lapic_get_id();
    smp_emit_u32(kprint, id);
    kprint(")\n");
    
    // Enable LAPIC on this AP
    lapic_enable(0xFF);  // Spurious vector
    
    // Enable Interrupts
    __asm__ volatile("sti");
    
    // Halt and wait for work
    while(1) {
        __asm__ volatile("hlt");
    }
}

// Boot a single AP
int smp_boot_ap(uint8_t apic_id) {
    uint32_t trampoline_phys = smp_alloc_trampoline_page();
    if (trampoline_phys == 0) {
        kprint("SMP: failed to allocate low-memory trampoline page.\n");
        return -1;
    }

    uint8_t *trampoline_va = (uint8_t *)(uintptr_t)P2V(trampoline_phys);

    // 1. Copy trampoline to low memory
    size_t len = (uintptr_t)trampoline_end - (uintptr_t)trampoline_start;
    memcpy(trampoline_va, (void *)trampoline_start, len);

    // 2. Patch the copied trampoline with the live paging and stack state.
    uint32_t *cr3_ptr = (uint32_t *)(trampoline_va + trampoline_offset((void *)&trampoline_cr3));
    uint32_t *cr4_ptr = (uint32_t *)(trampoline_va + trampoline_offset((void *)&trampoline_cr4));
    uint32_t *cr0_ptr = (uint32_t *)(trampoline_va + trampoline_offset((void *)&trampoline_cr0));
    uint32_t *stk_ptr = (uint32_t *)(trampoline_va + trampoline_offset((void *)&trampoline_stack));
    uint32_t *ent_ptr = (uint32_t *)(trampoline_va + trampoline_offset((void *)&trampoline_entry));
    
    // Find CPU index for this APIC ID
    int cpu_idx = -1;
    for (int i = 0; i < cpu_count; i++) {
        if (cpus[i].lapic_id == apic_id) {
            cpu_idx = i;
            break;
        }
    }
    if (cpu_idx < 0) return -1;
    
    // 2a. Initialize Per-CPU structures for the new core
    percpu_init_cpu(cpu_idx);

    // 2b. Initialize GDT and TSS for the new core
    gdt_init_cpu(cpu_idx);

    // 2c. Set Kernel Stack (ESP0) in TSS
    struct percpu_data *pcpu = percpu_get_cpu(cpu_idx);
    pcpu->tss.esp0 = (uint32_t)&ap_stacks[cpu_idx][4096];

    // 2d. Create Idle Thread for the new core
    thread_t *idle = sched_alloc_thread(&processes[0]);
    if (idle) {
        idle->state = THREAD_RUNNING;
        idle->on_runqueue = 0;
        idle->kstack_ptr = pcpu->tss.esp0;
        idle->kstack_top = pcpu->tss.esp0;
        idle->priority = 0;
        idle->sched_class = SCHED_IDLE;
        idle->proc = &processes[0]; // Ensure it belongs to kernel process
        idle->bound_cpu = cpu_idx;

        pcpu->idle = idle;
        pcpu->current = idle;
    }

    uint32_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    *cr3_ptr = pmap_kernel()->pdir_phys;
    *cr4_ptr = cr4;
    *cr0_ptr = cr0;
    *stk_ptr = (uint32_t)&ap_stacks[cpu_idx][4096];
    *ent_ptr = (uint32_t)smp_ap_entry;

    int old_ready = aps_ready;

    // 3. Send INIT IPI -> Wait 10ms -> SIPI -> Wait 200us -> SIPI
    lapic_send_init(apic_id);
    
    // Delay 10ms
    lapic_timer_delay_ms(10);
    
    // First SIPI
    lapic_send_sipi(apic_id, trampoline_phys >> 12);
    
    // Delay 200us
    lapic_timer_delay_us(200);
    
    // Second SIPI (per Intel spec)
    lapic_send_sipi(apic_id, trampoline_phys >> 12);
    
    // Wait for AP to signal readiness (timeout 100ms)
    for (int i = 0; i < 100; i++) {
        if (aps_ready > old_ready) break;
        lapic_timer_delay_ms(1);
    }
    
    if (aps_ready > old_ready) {
        return 0;  // Success
    }
    
    kprint("SMP: AP ");
    smp_emit_u32(kprint, apic_id);
    kprint(" did not respond!\n");
    return -1;
}

// Boot all APs
void smp_boot_all_aps(void) {
    kprint("SMP: Booting ");
    int ap_count = cpu_count - 1;  // Exclude BSP
    smp_emit_u32(kprint, (uint32_t)ap_count);
    kprint(" Application Processor(s)...\n");
    
    for (int i = 1; i < cpu_count; i++) {  // Skip BSP (index 0)
        smp_boot_ap(cpus[i].lapic_id);
    }

    cpu_count = aps_ready + 1;  // BSP + online APs

    kprint("SMP: ");
    smp_emit_u32(kprint, (uint32_t)aps_ready);
    kprint(" AP(s) online.\n");

    kprint("SMP: Brought up ");
    smp_emit_u32(kprint, (uint32_t)cpu_count);
    kprint(" CPU(s)!\n");
}

void smp_init(void) {
    smp_discover_cores();
}

int smp_get_cpu_count(void) {
    return cpu_count;
}

int smp_get_cpu_id(void) {
    return percpu_get_cpu_id();
}
#endif
