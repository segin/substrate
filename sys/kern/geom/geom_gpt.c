#include "geom.h"
#include "../console.h"
#include <string.h>

// GPT Header at LBA 1
// Magic "EFI PART"

static int geom_gpt_sniff(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix) {
    // Only check GPT at offset 0 (Disk start)
    if (offset != 0) return -1; // GPT inside MBR? rare/hybrid.
    
    // Check LBA 1
    uint8_t buf[512];
    if (geom_read_sector(disk, 1, buf) != 0) return -1;
    
    if (memcmp(buf, "EFI PART", 8) != 0) return -1;
    
    // Found GPT
    if (depth == 0) kprint(disk->name);
    else kprint(prefix);
    kprint(": ");
    kprint("GPT Detected (Enumeration TODO)"); // Just a stub for now as requested
    kprint("\n");
    
    return 0; 
}

static geom_class_t geom_gpt = {
    .name = "GPT",
    .sniff = geom_gpt_sniff,
    .next = NULL
};

void geom_gpt_init(void) {
    geom_register_class(&geom_gpt);
}
