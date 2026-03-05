#include <stddef.h>
#include <stdint.h>
#include <string.h>

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;

    // Small copy optimization / setup for alignment
    if (n < sizeof(unsigned long)) {
        while (n--) *d++ = *s++;
        return dest;
    }

    // Check alignment compatibility
    if ((uintptr_t)d % sizeof(unsigned long) == (uintptr_t)s % sizeof(unsigned long)) {
        // Align dest to word boundary
        while ((uintptr_t)d % sizeof(unsigned long)) {
            *d++ = *s++;
            n--;
        }

        // Copy words
        unsigned long *ld = (unsigned long *)d;
        const unsigned long *ls = (const unsigned long *)s;

        while (n >= sizeof(unsigned long) * 4) {
             ld[0] = ls[0];
             ld[1] = ls[1];
             ld[2] = ls[2];
             ld[3] = ls[3];
             ld += 4;
             ls += 4;
             n -= sizeof(unsigned long) * 4;
        }

        while (n >= sizeof(unsigned long)) {
            *ld++ = *ls++;
            n -= sizeof(unsigned long);
        }

        d = (unsigned char *)ld;
        s = (const unsigned char *)ls;
    }
    // Copy remaining bytes
    while (n--) *d++ = *s++;

    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;

    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

size_t strlen(const char *s);

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t src_len = strlen(src);
    if (size > 0) {
        size_t copy_len = (src_len >= size) ? size - 1 : src_len;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    unsigned char val = (unsigned char)c;

    // Align to word boundary
    while (n > 0 && ((uintptr_t)p & (sizeof(unsigned long) - 1))) {
        *p++ = val;
        n--;
    }

    // Fill words
    if (n >= sizeof(unsigned long)) {
        unsigned long word_val = val;
        word_val |= (word_val << 8);
        word_val |= (word_val << 16);
#if UINTPTR_MAX > 0xFFFFFFFF
        if (sizeof(unsigned long) > 4) {
             word_val |= (word_val << 32);
        }
#endif

        unsigned long *lp = (unsigned long *)p;

        // Unroll loop 4x
        while (n >= sizeof(unsigned long) * 4) {
            lp[0] = word_val;
            lp[1] = word_val;
            lp[2] = word_val;
            lp[3] = word_val;
            lp += 4;
            n -= sizeof(unsigned long) * 4;
        }

        while (n >= sizeof(unsigned long)) {
            *lp++ = word_val;
            n -= sizeof(unsigned long);
        }
        p = (unsigned char *)lp;
    }

    // Fill remaining bytes
    while (n > 0) {
        *p++ = val;
        n--;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *start = s;

    // Align to word boundary
    while ((uintptr_t)s % sizeof(unsigned long)) {
        if (!*s) return s - start;
        s++;
    }

    const unsigned long *ls = (const unsigned long *)s;
    unsigned long himagic = 0x80808080UL;
    unsigned long lomagic = 0x01010101UL;
    if (sizeof(unsigned long) > 4) {
        // Expand to 64-bit constants
        himagic = ((himagic << 16) << 16) | himagic;
        lomagic = ((lomagic << 16) << 16) | lomagic;
    }

    while (1) {
        unsigned long word = *ls++;
        if (((word - lomagic) & ~word & himagic) != 0) {
            const char *cp = (const char *)(ls - 1);
            if (!cp[0]) return cp - start;
            if (!cp[1]) return cp - start + 1;
            if (!cp[2]) return cp - start + 2;
            if (!cp[3]) return cp - start + 3;
            if (sizeof(unsigned long) > 4) {
                if (!cp[4]) return cp - start + 4;
                if (!cp[5]) return cp - start + 5;
                if (!cp[6]) return cp - start + 6;
                if (!cp[7]) return cp - start + 7;
            }
        }
    }
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char *)s;
}

size_t strspn(const char *s1, const char *s2) {
    size_t n;
    for (n = 0; *s1; s1++, n++) {
        const char *p;
        for (p = s2; *p && *p != *s1; p++);
        if (!*p) break;
    }
    return n;
}

size_t strcspn(const char *s1, const char *s2) {
    size_t n;
    for (n = 0; *s1; s1++, n++) {
        const char *p;
        for (p = s2; *p && *p != *s1; p++);
        if (*p) break;
    }
    return n;
}

char *strpbrk(const char *s1, const char *s2) {
    while (*s1) {
        if (strchr(s2, *s1++)) return (char *)s1 - 1;
    }
    return NULL;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    while (n-- && (*d++ = *src++));
    *d = '\0';
    return dest;
}
