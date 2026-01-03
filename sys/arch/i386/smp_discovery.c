#include "smp.h"
#include "../../kern/console.h"
#include <string.h>
#include <stdint.h>

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

void smp_discover_cores(void) {
    kprint("SMP: Discovering cores...\n");
    
    // Default to 1 CPU (Bootstrap Processor)
    cpu_count = 1;
    cpus[0].lapic_id = 0; 
    cpus[0].processor_id = 0;
    cpus[0].flags = 1;

    // 1. Search for RSDP
    // Standard search: 0xE0000 to 0xFFFFF
    struct rsdp_desc *rsdp = NULL;
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        if (memcmp((void*)addr, "RSD PTR ", 8) == 0) {
            rsdp = (struct rsdp_desc*)addr;
            break;
        }
    }

    if (!rsdp) {
        kprint("SMP: ACPI not found, falling back to UP.\n");
        return;
    }

    // 2. Locate MADT
    struct acpi_header *rsdt = (struct acpi_header*)rsdp->rsdt_addr;
    int entries = (rsdt->length - sizeof(struct acpi_header)) / 4;
    uint32_t *ptrs = (uint32_t*)((uint32_t)rsdt + sizeof(struct acpi_header));

    struct acpi_header *madt = NULL;
    for (int i = 0; i < entries; i++) {
        struct acpi_header *h = (struct acpi_header*)ptrs[i];
        if (memcmp(h->signature, "APIC", 4) == 0) {
            madt = h;
            break;
        }
    }

    if (!madt) return;

    // 3. Parse MADT Entries
    uint8_t *p = (uint8_t*)((uint32_t)madt + sizeof(struct acpi_header) + 8); // Skip Local APIC Addr and Flags
    uint8_t *end = (uint8_t*)((uint32_t)madt + madt->length);

    while (p < end) {
        uint8_t type = p[0];
        uint8_t length = p[1];

        if (type == 0) { // Processor Local APIC
            uint8_t proc_id = p[2];
            uint8_t apic_id = p[3];
            uint32_t flags = *((uint32_t*)&p[4]);

            if (flags & 1) { // Enabled
                if (cpu_count < MAX_CPUS && apic_id != 0) { // Don't re-add BSP
                    cpus[cpu_count].processor_id = proc_id;
                    cpus[cpu_count].lapic_id = apic_id;
                    cpus[cpu_count].flags = (uint8_t)flags;
                    cpu_count++;
                }
            }
        }
        p += length;
    }
}

extern void trampoline_start(void);
extern void trampoline_end(void);

#define TRAMPOLINE_ADDR 0x8000

void smp_ap_entry(void) {
    kprint("SMP: AP Core started.\n");
    while(1) __asm__ volatile("hlt");
}

void smp_boot_ap(uint8_t apic_id) {
    (void)apic_id;
    // 1. Copy trampoline to low memory
    size_t len = (uintptr_t)trampoline_end - (uintptr_t)trampoline_start;
    memcpy((void*)TRAMPOLINE_ADDR, trampoline_start, len);

    // 2. Set stack and entry point in trampoline
    // (Offsets match the .S file)
    uint32_t *stk_ptr = (uint32_t*)(TRAMPOLINE_ADDR + (len - 8));
    uint32_t *ent_ptr = (uint32_t*)(TRAMPOLINE_ADDR + (len - 4));
    
    static char ap_stack[4096];
    *stk_ptr = (uint32_t)ap_stack + 4096;
    *ent_ptr = (uint32_t)smp_ap_entry;

    // 3. Send INIT IPI
    // (Requires lapic_write from lapic.c)
    // lapic_write(LAPIC_ICRHI, apic_id << 24);
    // lapic_write(LAPIC_ICRLO, 0x00004500); // INIT

    // 4. Send STARTUP IPI
    // lapic_write(LAPIC_ICRHI, apic_id << 24);
    // lapic_write(LAPIC_ICRLO, 0x00004600 | (TRAMPOLINE_ADDR >> 12));
}
