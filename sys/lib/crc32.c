/*
 * crc32.c - CRC32 Implementation (IEEE 802.3 polynomial)
 *
 * Used by GPT and potentially other subsystems.
 */

#include <sys/crc32.h>

static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void crc32_init_table(void) {
    if (crc32_initialized) return;
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

uint32_t crc32(const void *data, size_t len) {
    crc32_init_table();
    
    const uint8_t *buf = data;
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}
