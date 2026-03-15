#include <drivers/storage/ide/ide.h>

#include <string.h>

int ide_prdt_build_entries(prdt_entry_t *prdt, size_t max_entries,
                           uint32_t phys_addr, uint32_t byte_count) {
    uint32_t remaining = byte_count;
    size_t entry = 0;

    if (prdt == NULL || max_entries == 0) {
        return -1;
    }
    if (byte_count == 0 || byte_count > 256U * 512U) {
        return -1;
    }

    memset(prdt, 0, sizeof(*prdt) * max_entries);

    while (remaining > 0 && entry < max_entries) {
        uint32_t region_size = remaining;
        uint32_t boundary = (phys_addr & ~0xFFFFUL) + 0x10000UL;

        if (region_size > 65536U) {
            region_size = 65536U;
        }
        if (phys_addr + region_size > boundary) {
            region_size = boundary - phys_addr;
        }
        if (region_size == 0) {
            return -1;
        }

        prdt[entry].phys_addr = phys_addr;
        prdt[entry].byte_count = (region_size == 65536U) ? 0 : (uint16_t)region_size;
        prdt[entry].reserved = 0;
        prdt[entry].eot = 0;

        phys_addr += region_size;
        remaining -= region_size;
        entry++;
    }

    if (remaining != 0 || entry == 0) {
        return -1;
    }

    prdt[entry - 1].eot = 1;
    return (int)entry;
}
