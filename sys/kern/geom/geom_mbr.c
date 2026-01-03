#include "geom.h"
#include "../console.h"
#include <string.h>
#include <stdio.h>

// MBR Partition Entry
struct mbr_entry {
    uint8_t status;
    uint8_t chs_start[3];
    uint8_t type;
    uint8_t chs_end[3];
    uint32_t lba_start;
    uint32_t size;
} __attribute__((packed));

struct mbr {
    uint8_t boot_code[446];
    struct mbr_entry entries[4];
    uint16_t signature;
} __attribute__((packed));

void geom_scan(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix);

static int geom_mbr_sniff(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix) {
    uint8_t buf[512];
    if (geom_read_sector(disk, offset, buf) != 0) return -1;
    
    struct mbr *mbr = (struct mbr *)buf;
    if (mbr->signature != 0xAA55) return -1;

    // Print MBR partition table summary
    if (depth == 0) {
        kprint(disk->name);
        kprint(": ");
    } else {
        // Indent for nested partitions
        for (int d = 0; d < depth; d++) kprint("  ");
        kprint(prefix);
        kprint(": ");
    }
    
    int printed_partitions = 0;
    
    for (int i = 0; i < 4; i++) {
        if (mbr->entries[i].type != 0) {
            if (printed_partitions) kprint(" ");
            
            char part_name[32];
            sprintf(part_name, "%sp%d", prefix, i+1);
            kprint(part_name);
            
            printed_partitions = 1;
        }
    }
    
    if (!printed_partitions && depth == 0) {
        kprint("(no slices)");
    }
    kprint("\n");
    
    // Recursively scan partitions (e.g., BSD slices inside MBR)
    for (int i = 0; i < 4; i++) {
        // Check for container types (0xA5 = FreeBSD/386BSD, 0x05/0x0F = Extended)
        if (mbr->entries[i].type == 0xA5) {
             char new_prefix[64];
             sprintf(new_prefix, "%sp%d", prefix, i+1);
             
             uint64_t part_offset = offset + mbr->entries[i].lba_start;
             geom_scan(disk, part_offset, depth + 1, new_prefix);
        }
    }
    
    return 0;
}

static geom_class_t geom_mbr = {
    .name = "MBR",
    .sniff = geom_mbr_sniff,
    .next = NULL
};

void geom_mbr_init(void) {
    geom_register_class(&geom_mbr);
}
