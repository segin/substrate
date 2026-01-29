#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <vm/vm_kmem.h>

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

// Hex conversion helper - fixed to not overflow tmp
static void utoa_hex(char *buf, unsigned int val, int uppercase) {
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

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    char *s = str;
    const char *f = format;
    size_t remaining = size;

    // Helper to emit a character
    #define EMIT(c) do { \
        if (remaining > 1) { \
            *s++ = (c); \
            remaining--; \
        } \
        len++; \
    } while (0)

    size_t len = 0; // Total characters that would be written

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
            
            // Parse width
            int width = 0;
            int pad_zero = 0;
            if (*f == '0' && !left_align) { pad_zero = 1; f++; }
            while (*f >= '0' && *f <= '9') {
                width = width * 10 + (*f - '0');
                f++;
            }
            
            // Parse precision
            int precision = -1;
            if (*f == '.') {
                f++;
                precision = 0;
                while (*f >= '0' && *f <= '9') {
                    precision = precision * 10 + (*f - '0');
                    f++;
                }
            }
            
            switch (*f) {
                case 'd':
                case 'i': {
                    int val = va_arg(ap, int);
                    char tmp[32];
                    itoa(tmp, val, force_sign, space_prefix);
                    int tmp_len = strlen(tmp);
                    
                    if (width > tmp_len) {
                        if (pad_zero) {
                            int has_sign = (tmp[0] == '+' || tmp[0] == '-' || tmp[0] == ' ');
                            if (has_sign) {
                                EMIT(tmp[0]);
                                for (int i = 0; i < width - tmp_len; i++) EMIT('0');
                                for (int i = 1; i < tmp_len; i++) EMIT(tmp[i]);
                            } else {
                                for (int i = 0; i < width - tmp_len; i++) EMIT('0');
                                for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                            }
                        } else if (left_align) {
                            for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                            for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                        } else {
                            for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                            for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                        }
                    } else {
                        for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                    }
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(ap, unsigned int);
                    char tmp[32];
                    itoa(tmp, (int)val, 0, 0);
                    int tmp_len = strlen(tmp);

                    if (width > tmp_len) {
                        if (pad_zero) {
                             for (int i = 0; i < width - tmp_len; i++) EMIT('0');
                             for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                        } else if (left_align) {
                            for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                            for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                        } else {
                            for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                            for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                        }
                    } else {
                        for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                    }
                    break;
                }
                case 'o': {
                    unsigned int val = va_arg(ap, unsigned int);
                    char tmp[32];
                    char *ptr = tmp;
                    if (alternate_form && val != 0) *ptr++ = '0';
                    utoa_oct(ptr, val);
                    int tmp_len = strlen(tmp);

                    if (width > tmp_len) {
                        if (pad_zero) {
                             for (int i = 0; i < width - tmp_len; i++) EMIT('0');
                             for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                        } else if (left_align) {
                            for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                            for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                        } else {
                            for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                            for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                        }
                    } else {
                        for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                    }
                    break;
                }
                case 'x':
                case 'X': {
                    unsigned int val = va_arg(ap, unsigned int);
                    char tmp[32];
                    utoa_hex(tmp, val, (*f == 'X'));
                    int len_val = strlen(tmp);
                    int prefix_len = (alternate_form && val != 0) ? 2 : 0;
                    int total_len = len_val + prefix_len;

                    if (width > total_len) {
                        if (pad_zero && !left_align) {
                            // Zero padding: prefix -> zeros -> value
                            if (prefix_len) {
                                EMIT('0');
                                EMIT((*f == 'X') ? 'X' : 'x');
                            }
                            for (int i = 0; i < width - total_len; i++) EMIT('0');
                            for (int i = 0; i < len_val; i++) EMIT(tmp[i]);
                        } else if (left_align) {
                            // Left align: prefix -> value -> spaces
                            if (prefix_len) {
                                EMIT('0');
                                EMIT((*f == 'X') ? 'X' : 'x');
                            }
                            for (int i = 0; i < len_val; i++) EMIT(tmp[i]);
                            for (int i = 0; i < width - total_len; i++) EMIT(' ');
                        } else {
                            // Right align: spaces -> prefix -> value
                            for (int i = 0; i < width - total_len; i++) EMIT(' ');
                            if (prefix_len) {
                                EMIT('0');
                                EMIT((*f == 'X') ? 'X' : 'x');
                            }
                            for (int i = 0; i < len_val; i++) EMIT(tmp[i]);
                        }
                    } else {
                        // No padding needed
                        if (prefix_len) {
                            EMIT('0');
                            EMIT((*f == 'X') ? 'X' : 'x');
                        }
                        for (int i = 0; i < len_val; i++) EMIT(tmp[i]);
                    }
                    break;
                }
                case 'p': {
                    unsigned int val = (unsigned int)(uintptr_t)va_arg(ap, void*);
                    char tmp[32];
                    utoa_hex(tmp, val, 0); // digits only
                    int len_val = strlen(tmp);
                    int zeros = 8 - len_val;
                    if (zeros < 0) zeros = 0;

                    int total_len = 2 + zeros + len_val; // 0x + zeros + digits

                    if (width > total_len && !left_align) {
                        for (int i = 0; i < width - total_len; i++) EMIT(' ');
                    }

                    EMIT('0'); EMIT('x');
                    for (int i = 0; i < zeros; i++) EMIT('0');
                    for (int i = 0; i < len_val; i++) EMIT(tmp[i]);

                    if (width > total_len && left_align) {
                        for (int i = 0; i < width - total_len; i++) EMIT(' ');
                    }
                    break;
                }
                case 's': {
                    const char *val = va_arg(ap, const char *);
                    if (!val) val = "(null)";
                    
                    int s_len = 0;
                    const char *p = val;
                    while (*p && (precision == -1 || s_len < precision)) {
                        s_len++;
                        p++;
                    }
                    
                    if (!left_align && width > s_len) {
                        for (int i = 0; i < width - s_len; i++) EMIT(' ');
                    }
                    
                    for (int i = 0; i < s_len; i++) EMIT(val[i]);
                    
                    if (left_align && width > s_len) {
                         for (int i = 0; i < width - s_len; i++) EMIT(' ');
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    EMIT(c);
                    break;
                }
                case '%':
                    EMIT('%');
                    break;
                default:
                    EMIT('%');
                    EMIT(*f);
                    break;
            }
            f++;
        } else {
            EMIT(*f);
            f++;
        }
    }

    if (size > 0) {
        if (remaining > 0) {
            *s = '\0';
        } else {
            // Buffer full, ensure null termination at end
            str[size - 1] = '\0';
        }
    }

    return len;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    // Use SIZE_MAX to simulate "infinite" buffer, but vsnprintf handles this
    int ret = vsnprintf(str, SIZE_MAX, format, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, SIZE_MAX, format, ap);
}

char *vasprintf(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);

    // Calculate length (pass NULL and 0 size)
    int len = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);

    if (len < 0) return NULL;

    char *str = kmalloc(len + 1);
    if (!str) return NULL;

    vsnprintf(str, len + 1, fmt, ap);
    return str;
}

char *kasprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *str = vasprintf(fmt, ap);
    va_end(ap);
    return str;
}
