#ifndef _ELFOBJ_LEB128_H_
#define _ELFOBJ_LEB128_H_

#include <stdint.h>
#include <stddef.h>

static inline uint64_t read_uleb128(const uint8_t *data, size_t *offset, size_t max_size) {
    uint64_t result = 0;
    int shift = 0;
    /* Cap shift at 63: a malicious varint with a run of continuation bytes
     * would otherwise drive (byte & 0x7f) << shift past 64 bits (UB). */
    while (*offset < max_size && shift < 64) {
        uint8_t byte = data[(*offset)++];
        result |= (uint64_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0)
            break;
        shift += 7;
    }
    return result;
}

static inline int64_t read_sleb128(const uint8_t *data, size_t *offset, size_t max_size) {
    int64_t result = 0;
    int shift = 0;
    uint8_t byte = 0;
    while (*offset < max_size && shift < 64) {
        byte = data[(*offset)++];
        result |= (uint64_t)(byte & 0x7f) << shift;
        shift += 7;
        if ((byte & 0x80) == 0)
            break;
    }
    if (shift < 64 && (byte & 0x40)) {
        result |= -(1ULL << shift);
    }
    return result;
}

#endif
