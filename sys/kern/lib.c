#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
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

// Very simple sprintf for procfs
static void itoa(char *buf, int val) {
    char tmp[16];
    int i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = (val % 10) + '0';
        val /= 10;
    }
    for (int j = 0; j < i; j++) {
        buf[j] = tmp[i - j - 1];
    }
    buf[i] = '\0';
}

int sprintf(char *str, const char *format, ...) {
    char *s = str;
    const char *f = format;
    __builtin_va_list ap;
    __builtin_va_start(ap, format);

    while (*f) {
        if (*f == '%' && *(f+1) == 'd') {
            int val = __builtin_va_arg(ap, int);
            itoa(s, val);
            s += strlen(s);
            f += 2;
        } else if (*f == '%' && *(f+1) == 's') {
            const char *val = __builtin_va_arg(ap, const char *);
            strcpy(s, val);
            s += strlen(s);
            f += 2;
        } else {
            *s++ = *f++;
        }
    }
    *s = '\0';
    __builtin_va_end(ap);
    return s - str;
}
