/*
 * <endian.h> — byte-order constants and conversion macros.
 *
 * Linux/glibc-style.  Substrate targets little-endian i386 / x86_64
 * exclusively, so the host order is fixed at compile time.  Code that
 * does runtime byte-order detection (rare) still works because the
 * htobe/be64toh family is provided.
 */
#ifndef _ENDIAN_H
#define _ENDIAN_H

#include <stdint.h>

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412

#define LITTLE_ENDIAN   __LITTLE_ENDIAN
#define BIG_ENDIAN      __BIG_ENDIAN
#define PDP_ENDIAN      __PDP_ENDIAN

/* Substrate is little-endian on every supported architecture.  */
#define __BYTE_ORDER    __LITTLE_ENDIAN
#define BYTE_ORDER      __BYTE_ORDER

static inline uint16_t __bswap16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t __bswap32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t __bswap64(uint64_t x) { return __builtin_bswap64(x); }

#define htobe16(x) __bswap16(x)
#define htole16(x) (x)
#define be16toh(x) __bswap16(x)
#define le16toh(x) (x)

#define htobe32(x) __bswap32(x)
#define htole32(x) (x)
#define be32toh(x) __bswap32(x)
#define le32toh(x) (x)

#define htobe64(x) __bswap64(x)
#define htole64(x) (x)
#define be64toh(x) __bswap64(x)
#define le64toh(x) (x)

#endif /* _ENDIAN_H */
