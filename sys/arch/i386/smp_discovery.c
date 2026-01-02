#include "smp.h"
#include "../../drivers/video/vga.h"

// Simple memcmp for freestanding environment
static int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

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
    vga_write("SMP: Discovering cores...\n", 26);
    
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
        vga_write("SMP: ACPI not found, falling back to UP.\n", 41);
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

int smp_get_cpu_count(void) {
    return cpu_count;
}
