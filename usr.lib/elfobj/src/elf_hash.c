#include <elf_private.h>

/* Symbol names are expected NUL-terminated (the parser guarantees every
 * SHT_STRTAB ends in '\0'). This is a defensive ceiling so that a caller
 * passing an unterminated buffer cannot make the hash walk run off the end
 * indefinitely; it is far larger than any real (even C++-mangled) name. */
#define ELFOBJ_MAX_NAME_HASH_LEN (1u << 20)

uint32_t elf_hash_sysv(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    uint32_t h = 0;
    uint32_t g;
    size_t n = 0;

    if (p == NULL) {
        return 0;
    }

    while (*p != '\0' && n < ELFOBJ_MAX_NAME_HASH_LEN) {
        h = (h << 4) + *p++;
        g = h & 0xf0000000u;
        if (g != 0) {
            h ^= g >> 24;
        }
        h &= ~g;
        n++;
    }
    return h;
}

uint32_t elf_hash_gnu(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    uint32_t h = 5381;
    size_t n = 0;

    if (p == NULL) {
        return 0;
    }

    while (*p != '\0' && n < ELFOBJ_MAX_NAME_HASH_LEN) {
        h = (h << 5) + h + (uint32_t)*p++;
        n++;
    }
    return h;
}
