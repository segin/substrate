/*
 * crc32.h - CRC32 checksum helper
 */

#ifndef _SYS_CRC32_H
#define _SYS_CRC32_H

#include <stdint.h>
#include <stddef.h>

/*
 * Calculate CRC32 checksum using IEEE 802.3 polynomial
 * Returns the inverted CRC32 value (standard usage)
 */
uint32_t crc32(const void *data, size_t len);

/*
 * Initialize the CRC32 lookup table.
 * Must be called before any calls to crc32().
 * Safe to call multiple times (idempotent).
 */
void crc32_init(void);

#endif
