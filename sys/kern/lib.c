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

// Hex conversion helper
static void utoa_hex(char *buf, unsigned int val, int uppercase, int width) {
    char tmp[16];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            tmp[i++] = digits[val & 0xF];
            val >>= 4;
        }
    }
    
    // Pad with zeros
    while (i < width) tmp[i++] = '0';
    
    // Reverse into buf
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
        if (*f == '%') {
            f++;
            
            // Parse flags
            int left_align = 0;
            if (*f == '-') { left_align = 1; f++; }
            
            // Parse width (e.g., %08X or %16s)
            int width = 0;
            int pad_zero = 0;
            if (*f == '0' && !left_align) { pad_zero = 1; f++; }
            while (*f >= '0' && *f <= '9') {
                width = width * 10 + (*f - '0');
                f++;
            }
            
            // Parse precision (e.g., %.16s or %5.2f)
            int precision = -1;
            if (*f == '.') {
                f++;
                precision = 0;
                while (*f >= '0' && *f <= '9') {
                    precision = precision * 10 + (*f - '0');
                    f++;
                }
            }
            (void)pad_zero; // Used implicitly in width
            
            switch (*f) {
                case 'd':
                case 'i': {
                    int val = __builtin_va_arg(ap, int);
                    itoa(s, val);
                    s += strlen(s);
                    break;
                }
                case 'u': {
                    unsigned int val = __builtin_va_arg(ap, unsigned int);
                    itoa(s, val); // Simple - treats as signed
                    s += strlen(s);
                    break;
                }
                case 'x': {
                    unsigned int val = __builtin_va_arg(ap, unsigned int);
                    utoa_hex(s, val, 0, width);
                    s += strlen(s);
                    break;
                }
                case 'X': {
                    unsigned int val = __builtin_va_arg(ap, unsigned int);
                    utoa_hex(s, val, 1, width);
                    s += strlen(s);
                    break;
                }
                case 'p': {
                    unsigned int val = (unsigned int)(uintptr_t)__builtin_va_arg(ap, void*);
                    *s++ = '0'; *s++ = 'x';
                    utoa_hex(s, val, 0, 8);
                    s += strlen(s);
                    break;
                }
                case 's': {
                    const char *val = __builtin_va_arg(ap, const char *);
                    if (!val) val = "(null)";
                    
                    // Calculate string length (limited by precision if set)
                    int len = 0;
                    const char *p = val;
                    while (*p && (precision == -1 || len < precision)) {
                        len++;
                        p++;
                    }
                    
                    // Handle left-align vs right-align
                    if (!left_align && width > len) {
                        // Right-align: add padding before string
                        for (int i = 0; i < width - len; i++) *s++ = ' ';
                    }
                    
                    // Copy the string (up to precision)
                    for (int i = 0; i < len; i++) *s++ = val[i];
                    
                    // Left-align: add padding after string
                    if (left_align && width > len) {
                        for (int i = 0; i < width - len; i++) *s++ = ' ';
                    }
                    break;
                }
                case 'c': {
                    char c = (char)__builtin_va_arg(ap, int);
                    *s++ = c;
                    break;
                }
                case '%':
                    *s++ = '%';
                    break;
                default:
                    *s++ = '%';
                    *s++ = *f;
                    break;
            }
            f++;
        } else {
            *s++ = *f++;
        }
    }
    *s = '\0';
    __builtin_va_end(ap);
    return s - str;
}

char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char *)s;
}
