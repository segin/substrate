#include <string.h>
#include <stdint.h>
#include <stdlib.h>

char *strfry(char *string) {
    if (!string) return NULL;
    size_t len = strlen(string);
    if (len == 0) return string;
    
    for (size_t i = 0; i < len; i++) {
        size_t r = rand() % len;
        char tmp = string[i];
        string[i] = string[r];
        string[r] = tmp;
    }
    return string;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while(n--) *p++ = (unsigned char)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) return (void*)p;
        p++;
    }
    return NULL;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (n && (*dest++ = *src++)) n--;
    while (n--) *dest++ = 0;
    return ret;
}

char *strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return ret;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (*dest) dest++;
    while (n-- && *src) *dest++ = *src++;
    *dest = 0;
    return ret;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char*)s;
}

char *strrchr(const char *s, int c) {
    const char *ret = NULL;
    do {
        if (*s == (char)c) ret = s;
    } while (*s++);
    return (char*)ret;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *new_s = malloc(len);
    if (new_s) {
        memcpy(new_s, s, len);
    }
    return new_s;
}

size_t strspn(const char *s, const char *accept) {
    const char *p = s;
    const char *a;
    while (*p) {
        for (a = accept; *a; a++) {
            if (*p == *a) break;
        }
        if (*a == 0) return p - s;
        p++;
    }
    return p - s;
}

size_t strcspn(const char *s, const char *reject) {
    const char *p = s;
    const char *r;
    while (*p) {
        for (r = reject; *r; r++) {
            if (*p == *r) return p - s;
        }
        p++;
    }
    return p - s;
}

char *strtok(char *str, const char *delim) {
    static char *saveptr;
    if (str) saveptr = str;
    else if (!saveptr) return NULL;
    
    str = saveptr + strspn(saveptr, delim);
    if (*str == 0) {
        saveptr = NULL;
        return NULL;
    }
    
    char *end = str + strcspn(str, delim);
    if (*end) {
        *end = 0;
        saveptr = end + 1;
    } else {
        saveptr = NULL;
    }
    return str;
}

char *strpbrk(const char *s1, const char *s2) {
    while (*s1) {
        if (strchr(s2, *s1++)) return (char *)s1 - 1;
    }
    return NULL;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *s = str;
    if (!s) s = *saveptr;
    if (!s) return NULL;

    s += strspn(s, delim);
    if (*s == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char *token = s;
    s += strcspn(s, delim);
    if (*s == '\0') {
        *saveptr = NULL;
    } else {
        *s = '\0';
        *saveptr = s + 1;
    }
    return token;
}

char *strerror(int errnum) {
    (void)errnum;
    return "Error";
}