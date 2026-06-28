#include <stdio.h>
#include <stdint.h>

int main() {
    uint64_t inode = 0x8000000000000000ULL | (12345ULL << 16) | 5;
    if (inode & 0x8000000000000000ULL) {
        uint32_t sector = (uint32_t)((inode & 0x7FFFFFFFFFFF0000ULL) >> 16);
        uint32_t offset = (uint32_t)(inode & 0xFFFFULL) * 32;
        printf("Sector %u offset %u\n", sector, offset);
    }
    return 0;
}
