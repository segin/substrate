#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

// Full-ish printf implementation

static char *print_int(char *buf, uint64_t val, int base, int is_signed, int width, int pad_zero, int upper) {
    char tmp[64];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    
    if (is_signed && (int64_t)val < 0) {
        val = -val;
    } else {
        is_signed = 0;
    }

    if (val == 0) tmp[i++] = '0';
    else {
        while (val != 0) {
            tmp[i++] = digits[val % base];
            val /= base;
        }
    }

    if (is_signed) tmp[i++] = '-';

    while (i < width && pad_zero) tmp[i++] = '0';
    while (i < width && !pad_zero) { 
        // Padding spaces usually go before, handled by caller or here
        // If we pad here, we append to digits, reversing later puts them at start
        // But '0' padding is internal, ' ' padding is external usually.
        // For simplicity:
        // We'll ignore space padding here and handle buffer logic for alignment?
        // Let's just handle zero padding.
        break; 
    }

    while (i > 0) *buf++ = tmp[--i];
    *buf = 0;
    return buf;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    char *start = str;
    char *end = str + size - 1;
    
    while (*format && str < end) {
        if (*format != '%') {
            *str++ = *format++;
            continue;
        }
        format++;
        
        int left_align = 0;
        int pad_zero = 0;
        
        // Parse flags
        while (*format == '-' || *format == '0') {
            if (*format == '-') left_align = 1;
            else if (*format == '0') pad_zero = 1;
            format++;
        }
        // Standard: '-' overrides '0'
        if (left_align) pad_zero = 0;
        
        // Parse Width
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        
        // Parse Precision
        int precision = -1;
        if (*format == '.') {
            format++;
            precision = 0;
            while (*format >= '0' && *format <= '9') {
                precision = precision * 10 + (*format - '0');
                format++;
            }
        }
        
        int len_l = 0; // l=1, ll=2, h=-1, hh=-2
        if (*format == 'l') { len_l++; format++; if (*format == 'l') { len_l++; format++; } }
        else if (*format == 'h') { len_l--; format++; if (*format == 'h') { len_l--; format++; } }
        else if (*format == 'z') { len_l=1; format++; } // size_t like long
        
        char buf[64];
        char *s_arg;
        uint64_t u_val;
        int64_t s_val;
        
        switch (*format) {
            case 'd':
            case 'i':
                if (len_l == 2) s_val = va_arg(ap, int64_t);
                else if (len_l == 1) s_val = va_arg(ap, long);
                else s_val = va_arg(ap, int); // char/short promoted to int
                // Only pass width to print_int if we want ZERO padding.
                print_int(buf, (uint64_t)s_val, 10, 1, pad_zero ? width : 0, pad_zero, 0);
                s_arg = buf;
                break;
            case 'u':
            case 'x':
            case 'X':
            case 'p':
                if (*format == 'p') {
                    u_val = (uintptr_t)va_arg(ap, void*);
                    strcpy(buf, "0x");
                    // Pointers usually zero padded if width specified? Let's assume standard behavior is just hex.
                    print_int(buf+2, u_val, 16, 0, pad_zero ? width : 0, pad_zero, 0); 
                    s_arg = buf;
                } else {
                    int base = (*format == 'u') ? 10 : 16;
                    int upper = (*format == 'X');
                    if (len_l == 2) u_val = va_arg(ap, uint64_t);
                    else if (len_l == 1) u_val = va_arg(ap, unsigned long);
                    else u_val = va_arg(ap, unsigned int);
                    print_int(buf, u_val, base, 0, pad_zero ? width : 0, pad_zero, upper);
                    s_arg = buf;
                }
                break;
            case 's':
                s_arg = va_arg(ap, char*);
                if (!s_arg) s_arg = "(null)";
                break;
            case 'c':
                buf[0] = (char)va_arg(ap, int);
                buf[1] = 0;
                s_arg = buf;
                break;
            case '%':
                buf[0] = '%'; buf[1] = 0; s_arg = buf;
                break;
            default:
                buf[0] = *format; buf[1] = 0; s_arg = buf;
                break;
        }
        
        // Calculate length of argument string with precision cap
        size_t len = 0;
        const char *tmp = s_arg;
        while (*tmp && (precision < 0 || len < (size_t)precision)) {
            len++;
            tmp++;
        }
        
        int pad = (width > (int)len) ? (width - (int)len) : 0;
        
        // Right alignment (padding before)
        if (!left_align) {
            while (pad-- > 0 && str < end) *str++ = ' ';
        }
        
        // String/Number content
        size_t print_len = 0;
        while (*s_arg && str < end && (precision < 0 || print_len < (size_t)precision)) {
             *str++ = *s_arg++;
             print_len++;
        }
        
        // Left alignment (padding after)
        if (left_align) {
            while (pad-- > 0 && str < end) *str++ = ' ';
        }
        
        format++;
    }
    *str = 0;
    return str - start;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, 0x7FFFFFFF, format, ap);
    va_end(ap);
    return ret;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    // Buffered output: we need to use stream buffer.
    // For prototype, we'll alloc a temp buffer.
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    fwrite(buf, 1, ret, stream);
    return ret;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    fwrite(buf, 1, ret, stdout);
    return ret;
}

int vprintf(const char *format, va_list ap) {
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    fwrite(buf, 1, ret, stdout);
    return ret;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    fwrite(buf, 1, ret, stream);
    return ret;
}
