#include "elf_private.h"

uint32_t elf_hash_sysv(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    uint32_t h = 0;
    uint32_t g;

    if (p == NULL) {
        return 0;
    }

    while (*p != '\0') {
        h = (h << 4) + *p++;
        g = h & 0xf0000000u;
        if (g != 0) {
            h ^= g >> 24;
        }
        h &= ~g;
    }
    return h;
}

uint32_t elf_hash_gnu(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    uint32_t h = 5381;

    if (p == NULL) {
        return 0;
    }

    while (*p != '\0') {
        h = (h << 5) + h + (uint32_t)*p++;
    }
    return h;
}
