/*
 * crc32c.c — Castagnoli CRC-32 software implementation.
 *
 * 256-entry table, byte-at-a-time slice.  No hardware acceleration
 * (substrate's i486 baseline predates SSE4.2 CRC32 instructions).
 * Polynomial is 0x1EDC6F41 reflected to 0x82F63B78.
 */
#include <crc32c.h>

/* Slice-by-1 table.  Generated at compile time by walking each
 * byte 0..255 through 8 shifts of the reflected polynomial.  We
 * compute it lazily on first call instead of statically because
 * a kernel-time initialiser is awkward in substrate's build setup
 * and the 1KB table fits a single cacheline pool.  */
static uint32_t crc32c_table[256];
static int      crc32c_table_built = 0;

static void crc32c_table_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (c >> 1) ^ 0x82F63B78u : (c >> 1);
        crc32c_table[i] = c;
    }
    crc32c_table_built = 1;
}

uint32_t crc32c_update(uint32_t crc, const void *buf, size_t len) {
    if (!crc32c_table_built) crc32c_table_init();
    const uint8_t *p = (const uint8_t *)buf;
    while (len--) {
        crc = (crc >> 8) ^ crc32c_table[(crc ^ *p++) & 0xFF];
    }
    return crc;
}
