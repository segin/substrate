/*
 * crc32.h - CRC32 checksum helper
 */

#ifndef _SYS_CRC32_H
#define _SYS_CRC32_H

#include <stdint.h>
#include <stddef.h>

/*
 * Calculate CRC32 checksum using IEEE 802.3 polynomial.
 * Returns the inverted CRC32 value (standard usage).
 * Automatically initializes the lookup table if needed.
 */
uint32_t crc32(const void *data, size_t len);

/*
 * Initialize the CRC32 lookup table.
 * Called automatically by crc32(), but exposed for explicit initialization.
 * Safe to call multiple times (idempotent).
 */
void crc32_init(void);

#endif
