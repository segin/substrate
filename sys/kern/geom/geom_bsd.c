#include "geom.h"
#include "../console.h"
#include <string.h>

#define BSD_DISKMAGIC     ((uint32_t)0x82564557)

struct bsd_partition {
    uint32_t size;
    uint32_t offset;
    uint32_t fsize;
    uint8_t  fstype;
    uint8_t  frag;
    uint16_t cpg;
} __attribute__((packed));

struct bsd_disklabel {
    uint32_t d_magic;       // THe disk magic number
    uint16_t d_type;
    uint16_t d_subtype;
    char     d_typename[16];
    // skip
} __attribute__((packed));

static int geom_bsd_sniff(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix) {
    uint8_t buf[512];
    // Check sector 1 relative to offset (usually)
    if (geom_read_sector(disk, offset + 1, buf) != 0) return -1;
    
    // Check various offsets for magic (0, 64)
    int label_offset = -1;
    uint32_t *magic_ptr = (uint32_t*)buf;
    if (*magic_ptr == BSD_DISKMAGIC) label_offset = 0;
    else if (*(uint32_t*)(buf + 64) == BSD_DISKMAGIC) label_offset = 64;
    
    if (label_offset == -1) return -1; // Not BSD
    
    // Found BSD Label
    // Print Indentation
    for(int i=0; i<depth; i++) kprint("  ");
    
    kprint(prefix);
    kprint(": ");
    
    struct bsd_partition *parts = (struct bsd_partition *)(buf + label_offset + 148);
    
    int printed = 0;
    for (int i = 0; i < 8; i++) { // Usually 8 (a-h)
        if (parts[i].size > 0 && parts[i].fstype != 0) { // Check unused?
            if (printed) kprint(" ");
            kprint(prefix);
            char suffix[2] = { 'a' + i, 0 };
            kprint(suffix);
            printed = 1;
        }
    }
    kprint("\n");
    
    return 0;
}

static geom_class_t geom_bsd = {
    .name = "BSD",
    .sniff = geom_bsd_sniff,
    .next = NULL
};

void geom_bsd_init(void) {
    geom_register_class(&geom_bsd);
}
