#include "geom.h"
#include "../console.h"
#include <string.h>

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

    // Found MBR
    // Print logic
    if (depth == 0) {
        kprint(disk->name); // Should check if prefix matches disk->name?
        kprint(": ");
    }
    
    int printed = 0;
    
    for (int i = 0; i < 4; i++) {
        if (mbr->entries[i].type != 0) {
            if (printed) kprint(" ");
            
            // Construct name: prefix + "p" + (i+1)
            // Need a simpler way to print without sprintf buffer? 
            // Or just print pieces provided we don't have threading issues.
            kprint(prefix); 
            kprint("p");
            char idx[2] = { '1' + i, 0 };
            kprint(idx);
            
            printed = 1;
        }
    }
    kprint("\n");
    
    // Pass 2: Recursion
    // Now we need to recurse for sub-slices (like BSD or Ext)
    for (int i = 0; i < 4; i++) {
        // BSD Partition ID (386BSD, FreeBSD, etc)
        if (mbr->entries[i].type == 0xA5) {
             // Construct new prefix for child
             char new_prefix[32];
             
             // strcpy(new_prefix, prefix)
             int j=0;
             const char *s = prefix;
             while(*s && j < 20) new_prefix[j++] = *s++;
             new_prefix[j++] = 'p';
             new_prefix[j++] = '1' + i;
             new_prefix[j] = 0;
             
             // For sub-partitions, we indent?
             // User output: "  ide0p2: ide0p2a ..."
             // If we are about to check this partition, we should scan it.
             // If the scan finds something, it will print.
             
             // Calculate absolute offset
             uint64_t part_offset = offset + mbr->entries[i].lba_start;
             
             // We need to print indentation BEFORE the child prints its label?
             // But the child prints.
             // Child prints: "prefix: ..."
             // We want "  prefix: ..."
             // Let's modify kprint to handle indentation? No.
             // Let's assume child is responsible for indentation based on depth.
             
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
