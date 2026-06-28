#include <stdio.h>
#include <stdint.h>
#define FAT_SYNTH_INO_BASE 0x8000000000000000ULL

void update_dirent(uint64_t inode, uint32_t new_cluster, uint32_t file_size) {
    if ((inode & FAT_SYNTH_INO_BASE) == FAT_SYNTH_INO_BASE) {
        uint32_t sector = (uint32_t)((inode & ~FAT_SYNTH_INO_BASE) >> 16);
        uint32_t offset = (uint32_t)(inode & 0xFFFF) * 32;
        printf("Sector %u offset %u\n", sector, offset);
    }
}
int main() {
    uint64_t inode = FAT_SYNTH_INO_BASE | (12345ULL << 16) | 5;
    update_dirent(inode, 42, 1000);
    return 0;
}
