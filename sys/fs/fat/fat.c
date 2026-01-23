#include <fs/fat/fat.h>
#include <drivers/video/vga.h>

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

int fat_parse_lfn(fat_lfn_t *lfn, char *buffer) {
    // In a real driver, we would:
    // 1. Extract characters from name1, name2, name3.
    // 2. Decode UTF-16 to ASCII or UTF-8.
    // 3. Concatenate based on the order field.
    (void)lfn; (void)buffer;
    return 0; // Stub
}

