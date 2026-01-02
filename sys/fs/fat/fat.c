#include "fat.h"
#include "../../drivers/video/vga.h"

void fat_init(void) {
    // Initialized by VFS or dev driver
}

uint32_t fat_get_next_cluster(uint32_t cluster) {
    // In a real driver, we would:
    // 1. Calculate sector containing this cluster's FAT entry.
    // 2. Read the sector from disk.
    // 3. Extract the entry (16-bit or 32-bit).
    // 4. Return the next cluster index.
    (void)cluster;
    return 0x0FFFFFFF; // EOC stub
}

