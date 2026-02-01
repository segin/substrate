#include <stddef.h>
#include <stdint.h>
#include <string.h>

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
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
    size_t len = 0;
    while (s[len]) len++;
    return len;
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

char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char *)s;
}
