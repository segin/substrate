#include "smp.h"
#include <sys/smp.h>
#include <kern/console.h>
#include <arch/i386/early_boot.h>
#include <string.h>
#include <stdint.h>
#include <arch/i386/include/early_boot.h>

#define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))

cpu_info_t cpus[MAX_CPUS];
int cpu_count = 0;

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

#define P2V(x) ((void*)((uintptr_t)(x) + 0xC0000000))

void smp_discover_cores(void) {
    early_uart_print("SMP: Discovering cores...\n");
    
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
    char bsp_buf[4];
    bsp_buf[0] = '0' + (bsp_id % 10);
    bsp_buf[1] = '\0';
    early_uart_print(bsp_buf);
    early_uart_print("\n");

    cpus[0].lapic_id = bsp_id;
    cpus[0].processor_id = 0; // Unknown from LAPIC, will fill from MADT if found?
    cpus[0].flags = 1;

    // 1. Search for RSDP
    // Standard search: 0xE0000 to 0xFFFFF
    struct rsdp_desc *rsdp = NULL;
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        // Access via virtual address in higher half
        if (memcmp(VIRTUAL_d(addr), "RSD PTR ", 8) == 0) {
            rsdp = (struct rsdp_desc*)VIRTUAL_d(addr);
            break;
        }
    }

    if (!rsdp) {
        early_uart_print("SMP: ACPI RSDP not found, falling back to UP.\n");
        return;
    }

    // 2. Locate MADT
    // rsdp->rsdt_addr is physical, convert to virtual
    struct acpi_header *rsdt = (struct acpi_header*)VIRTUAL_d(rsdp->rsdt_addr);

    // Validate RSDT signature
    if (memcmp(rsdt->signature, "RSDT", 4) != 0) {
        early_uart_print("SMP: RSDT Invalid signature!\n");
        return;
    }
    int entries = (rsdt->length - sizeof(struct acpi_header)) / 4;
    uint32_t *ptrs = (uint32_t*)((uintptr_t)rsdt + sizeof(struct acpi_header));

    struct acpi_header *madt = NULL;
    for (int i = 0; i < entries; i++) {
        // ptrs[i] contains physical address of a table
        struct acpi_header *h = (struct acpi_header*)VIRTUAL_d(ptrs[i]);
        if (memcmp(h->signature, "APIC", 4) == 0) {
            madt = h;
            break;
        }
    }

    if (!madt) {
        early_uart_print("SMP: MADT not found.\n");
        return;
    }

    early_uart_print("SMP: MADT found.\n");

    // 3. Parse MADT Entries
    uint8_t *p = (uint8_t*)((uintptr_t)madt + sizeof(struct acpi_header) + 8); // Skip Local APIC Addr and Flags
    uint8_t *end = (uint8_t*)((uintptr_t)madt + madt->length);

    while (p < end) {
        uint8_t type = p[0];
        uint8_t length = p[1];

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
        }
        p += length;
    }

    char buf[32];
    char *c = buf + 31;
    *c = 0;
    int n = cpu_count;
    do { *--c = '0' + (n % 10); n /= 10; } while(n);

    early_uart_print("SMP: Detected CPU count: ");
    early_uart_print(c);
    early_uart_print("\n");
}

extern void trampoline_start(void);
extern void trampoline_end(void);

#define TRAMPOLINE_ADDR 0x8000

// Per-AP stack (static for now, should be dynamically allocated per AP)
static char ap_stacks[MAX_CPUS][4096] __attribute__((aligned(16)));
static volatile int aps_ready = 0;

void smp_ap_entry(void) {
    // Signal that we've started
    __sync_fetch_and_add(&aps_ready, 1);
    
    kprint("SMP: AP Core online (LAPIC ID: ");
    // Get and print LAPIC ID
    extern uint32_t lapic_get_id(void);
    uint32_t id = lapic_get_id();
    char buf[4];
    buf[0] = '0' + (id % 10);
    buf[1] = '\0';
    kprint(buf);
    kprint(")\n");
    
    // Enable LAPIC on this AP
    extern void lapic_enable(uint8_t);
    lapic_enable(0xFF);  // Spurious vector
    
    // TODO: Initialize AP-local state (GDT, TSS, IDT, scheduler)
    
    // Halt and wait for work
    while(1) {
        __asm__ volatile("hlt");
    }
}

// Boot a single AP
int smp_boot_ap(uint8_t apic_id) {
    extern void lapic_send_init(uint8_t);
    extern void lapic_send_sipi(uint8_t, uint8_t);
    
    // 1. Copy trampoline to low memory
    size_t len = (uintptr_t)trampoline_end - (uintptr_t)trampoline_start;
    memcpy((void*)TRAMPOLINE_ADDR, (void*)trampoline_start, len);

    // 2. Set stack and entry point in trampoline
    // Find offsets (they're at the end of the trampoline)
    uint32_t *stk_ptr = (uint32_t*)(TRAMPOLINE_ADDR + len - 8);
    uint32_t *ent_ptr = (uint32_t*)(TRAMPOLINE_ADDR + len - 4);
    
    // Find CPU index for this APIC ID
    int cpu_idx = -1;
    for (int i = 0; i < cpu_count; i++) {
        if (cpus[i].lapic_id == apic_id) {
            cpu_idx = i;
            break;
        }
    }
    if (cpu_idx < 0) return -1;
    
    *stk_ptr = (uint32_t)&ap_stacks[cpu_idx][4096];
    *ent_ptr = (uint32_t)smp_ap_entry;

    int old_ready = aps_ready;

    // 3. Send INIT IPI -> Wait 10ms -> SIPI -> Wait 200us -> SIPI
    lapic_send_init(apic_id);
    
    // Delay 10ms
    lapic_timer_delay_ms(10);
    
    // First SIPI
    lapic_send_sipi(apic_id, TRAMPOLINE_ADDR >> 12);
    
    // Delay 200us
    lapic_timer_delay_us(200);
    
    // Second SIPI (per Intel spec)
    lapic_send_sipi(apic_id, TRAMPOLINE_ADDR >> 12);
    
    // Wait for AP to signal readiness (timeout ~100ms)
    for (volatile int i = 0; i < 10000000 && aps_ready == old_ready; i++) { }
    
    if (aps_ready > old_ready) {
        return 0;  // Success
    }
    
    kprint("SMP: AP ");
    char buf[4];
    buf[0] = '0' + (apic_id % 10);
    buf[1] = '\0';
    kprint(buf);
    kprint(" did not respond!\n");
    return -1;
}

// Boot all APs
void smp_boot_all_aps(void) {
    kprint("SMP: Booting ");
    char buf[4];
    int ap_count = cpu_count - 1;  // Exclude BSP
    buf[0] = '0' + (ap_count % 10);
    buf[1] = '\0';
    kprint(buf);
    kprint(" Application Processor(s)...\n");
    
    for (int i = 1; i < cpu_count; i++) {  // Skip BSP (index 0)
        smp_boot_ap(cpus[i].lapic_id);
    }
    
    kprint("SMP: ");
    buf[0] = '0' + (aps_ready % 10);
    buf[1] = '\0';
    kprint(buf);
    kprint(" AP(s) online.\n");
}

void smp_init(void) {
    smp_discover_cores();
}

int smp_get_cpu_count(void) {
    return cpu_count;
}
