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

// Integer to ASCII with optional sign/space prefix
static void itoa(char *buf, int val, int force_sign, int space_prefix) {
    char tmp[16];
    int i = 0;
    int is_negative = (val < 0);
    
    if (val == 0) {
        if (force_sign) {
            buf[0] = '+';
            buf[1] = '0';
            buf[2] = '\0';
        } else if (space_prefix) {
            buf[0] = ' ';
            buf[1] = '0';
            buf[2] = '\0';
        } else {
            buf[0] = '0';
            buf[1] = '\0';
        }
        return;
    }
    
    // Handle negative
    if (is_negative) val = -val;
    
    while (val > 0) {
        tmp[i++] = (val % 10) + '0';
        val /= 10;
    }
    
    // Add sign or space
    int j = 0;
    if (is_negative) {
        buf[j++] = '-';
    } else if (force_sign) {
        buf[j++] = '+';
    } else if (space_prefix) {
        buf[j++] = ' ';
    }
    
    // Reverse digits
    for (int k = 0; k < i; k++) {
        buf[j++] = tmp[i - k - 1];
    }
    buf[j] = '\0';
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

// Octal conversion helper
static void utoa_oct(char *buf, unsigned int val) {
    char tmp[16];
    int i = 0;
    
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            tmp[i++] = '0' + (val & 7);
            val >>= 3;
        }
    }
    
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
            int force_sign = 0;
            int space_prefix = 0;
            int alternate_form = 0;
            if (*f == '-') { left_align = 1; f++; }
            if (*f == '+') { force_sign = 1; f++; }
            if (*f == ' ' && !force_sign) { space_prefix = 1; f++; }
            if (*f == '#') { alternate_form = 1; f++; }
            
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
                    char tmp[32];
                    itoa(tmp, val, force_sign, space_prefix);
                    int len = strlen(tmp);
                    
                    // Apply padding if width specified
                    if (width > len) {
                        if (pad_zero) {
                            // Zero-padding: sign first, then zeros
                            int has_sign = (tmp[0] == '+' || tmp[0] == '-' || tmp[0] == ' ');
                            if (has_sign) {
                                *s++ = tmp[0];
                                for (int i = 0; i < width - len; i++) *s++ = '0';
                                strcpy(s, tmp + 1);
                            } else {
                                for (int i = 0; i < width - len; i++) *s++ = '0';
                                strcpy(s, tmp);
                            }
                        } else if (left_align) {
                            // Left-align: value first, then spaces
                            strcpy(s, tmp);
                            s += len;
                            for (int i = 0; i < width - len; i++) *s++ = ' ';
                            *s = '\0';
                        } else {
                            // Right-align (default): spaces first, then value
                            for (int i = 0; i < width - len; i++) *s++ = ' ';
                            strcpy(s, tmp);
                        }
                    } else {
                        strcpy(s, tmp);
                    }
                    s += strlen(s);
                    break;
                }
                case 'u': {
                    unsigned int val = __builtin_va_arg(ap, unsigned int);
                    itoa(s, (int)val, 0, 0); // No sign for unsigned
                    s += strlen(s);
                    break;
                }
                case 'o': {
                    unsigned int val = __builtin_va_arg(ap, unsigned int);
                    if (alternate_form && val != 0) {
                        *s++ = '0'; // Octal prefix
                    }
                    utoa_oct(s, val);
                    s += strlen(s);
                    break;
                }
                case 'x': {
                    unsigned int val = __builtin_va_arg(ap, unsigned int);
                    if (alternate_form && val != 0) {
                        *s++ = '0';
                        *s++ = 'x';
                    }
                    utoa_hex(s, val, 0, width);
                    s += strlen(s);
                    break;
                }
                case 'X': {
                    unsigned int val = __builtin_va_arg(ap, unsigned int);
                    if (alternate_form && val != 0) {
                        *s++ = '0';
                        *s++ = 'X';
                    }
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

/*
 * 64-bit arithmetic helpers for i386
 * 
 * GCC emits calls to these when performing 64-bit arithmetic on 32-bit.
 * We implement them here to avoid libgcc dependency, which can have
 * multilib configuration issues across different systems.
 *
 * These implementations are UB-free and handle edge cases properly:
 * - Division by zero traps
 * - INT64_MIN / -1 overflow traps
 * - No signed overflow UB
 */

/* INT64_MIN for freestanding environment (may already be in stdint.h) */
#ifndef INT64_MIN
#define INT64_MIN (-9223372036854775807LL - 1)
#endif

/* Core 64-bit unsigned division with remainder - no UB */
static uint64_t udiv64(uint64_t n, uint64_t d, uint64_t *rem) {
    if (d == 0) {
        __builtin_trap(); /* Division by zero */
    }

    uint64_t q = 0;
    uint64_t r = 0;

    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1ULL);
        if (r >= d) {
            r -= d;
            q |= (1ULL << i);
        }
    }

    if (rem)
        *rem = r;
    return q;
}

/* Unsigned 64-bit division */
uint64_t __udivdi3(uint64_t n, uint64_t d) {
    return udiv64(n, d, 0);
}

/* Unsigned 64-bit modulo */
uint64_t __umoddi3(uint64_t n, uint64_t d) {
    uint64_t r;
    udiv64(n, d, &r);
    return r;
}

/* Signed 64-bit division with overflow handling */
int64_t __divdi3(int64_t a, int64_t b) {
    if (b == 0)
        __builtin_trap();

    if (a == INT64_MIN && b == -1)
        __builtin_trap(); /* Overflow: result not representable */

    int neg = ((a ^ b) < 0);

    uint64_t ua = (a < 0) ? (uint64_t)(-(uint64_t)a) : (uint64_t)a;
    uint64_t ub = (b < 0) ? (uint64_t)(-(uint64_t)b) : (uint64_t)b;

    uint64_t q = udiv64(ua, ub, 0);
    return neg ? -(int64_t)q : (int64_t)q;
}

/* Signed 64-bit modulo */
int64_t __moddi3(int64_t a, int64_t b) {
    if (b == 0)
        __builtin_trap();

    uint64_t r;
    uint64_t ua = (a < 0) ? (uint64_t)(-(uint64_t)a) : (uint64_t)a;
    uint64_t ub = (b < 0) ? (uint64_t)(-(uint64_t)b) : (uint64_t)b;

    udiv64(ua, ub, &r);
    return (a < 0) ? -(int64_t)r : (int64_t)r;
}

/* 64-bit left shift */
uint64_t __ashldi3(uint64_t a, int b) {
    b &= 63;
    if (b == 0) return a;
    return (a << b);
}

/* 64-bit logical right shift */
uint64_t __lshrdi3(uint64_t a, int b) {
    b &= 63;
    if (b == 0) return a;
    return (a >> b);
}

/* 64-bit arithmetic right shift */
int64_t __ashrdi3(int64_t a, int b) {
    b &= 63;
    if (b == 0) return a;
    
    uint64_t ua = (uint64_t)a;
    if (a >= 0) return (int64_t)(ua >> b);
    
    /* Arithmetic right shift for negative: shift then fill high bits with 1s */
    uint64_t shifted = ua >> b;
    uint64_t mask = (~0ULL) << (64 - b);
    return (int64_t)(shifted | mask);
}

/* Signed 64x64 -> 64 multiply */
int64_t __muldi3(int64_t a, int64_t b) {
    /* Use 32-bit multiplication with proper overflow handling */
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;

    uint32_t a_lo = (uint32_t)ua;
    uint32_t a_hi = (uint32_t)(ua >> 32);
    uint32_t b_lo = (uint32_t)ub;
    uint32_t b_hi = (uint32_t)(ub >> 32);

    uint64_t lo_lo = (uint64_t)a_lo * b_lo;
    uint64_t hi_lo = (uint64_t)a_hi * b_lo;
    uint64_t lo_hi = (uint64_t)a_lo * b_hi;
    /* hi_hi discarded (exceeds 64-bit result) */
    
    uint64_t cross = hi_lo + lo_hi;
    uint64_t result = lo_lo + (cross << 32);
    
    return (int64_t)result;
}

/* 64-bit negate */
int64_t __negdi2(int64_t a) {
    return -a;
}
