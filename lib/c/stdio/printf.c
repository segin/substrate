#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stddef.h>

// Helpers from kernel printf.c adapted for libc

static void itoa(char *buf, int64_t val, int force_sign, int space_prefix) {
    char tmp[32];
    int i = 0;
    int is_negative = (val < 0);
    
    if (val == 0) {
        if (force_sign) {
            buf[0] = '+'; buf[1] = '0'; buf[2] = '\0';
        } else if (space_prefix) {
            buf[0] = ' '; buf[1] = '0'; buf[2] = '\0';
        } else {
            buf[0] = '0'; buf[1] = '\0';
        }
        return;
    }
    
    uint64_t uval;
    if (is_negative) uval = (uint64_t)-val;
    else uval = (uint64_t)val;
    
    while (uval > 0) {
        tmp[i++] = (uval % 10) + '0';
        uval /= 10;
    }
    
    int j = 0;
    if (is_negative) buf[j++] = '-';
    else if (force_sign) buf[j++] = '+';
    else if (space_prefix) buf[j++] = ' ';
    
    for (int k = 0; k < i; k++) buf[j++] = tmp[i - k - 1];
    buf[j] = '\0';
}

static void utoa_hex(char *buf, uint64_t val, int uppercase) {
    char tmp[32];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    else {
        while (val > 0) {
            tmp[i++] = digits[val & 0xF];
            val >>= 4;
        }
    }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - j - 1];
    buf[i] = '\0';
}

static void ftoa(char *buf, double val, int precision, int uppercase) {
    if (precision < 0) precision = 6;
    if (val != val) { strcpy(buf, uppercase ? "NAN" : "nan"); return; }
    if (val > 1e308 || val < -1e308) { strcpy(buf, uppercase ? "INF" : "inf"); return; }
    if (val < 0) { *buf++ = '-'; val = -val; }
    
    double rounding = 0.5;
    for (int i = 0; i < precision; i++) rounding /= 10.0;
    val += rounding;

    int64_t integral = (int64_t)val;
    double fractional = val - (double)integral;
    
    char tmp[64];
    itoa(tmp, integral, 0, 0);
    strcpy(buf, tmp);
    buf += strlen(tmp);
    
    if (precision > 0) {
        *buf++ = '.';
        for (int i = 0; i < precision; i++) {
            fractional *= 10.0;
            int digit = (int)fractional;
            *buf++ = digit + '0';
            fractional -= digit;
        }
    }
    *buf = '\0';
}

static void etoa(char *buf, double val, int precision, int uppercase) {
    if (precision < 0) precision = 6;
    if (val != val) { strcpy(buf, uppercase ? "NAN" : "nan"); return; }
    if (val > 1e308 || val < -1e308) { strcpy(buf, uppercase ? "INF" : "inf"); return; }
    if (val < 0) { *buf++ = '-'; val = -val; }

    int exponent = 0;
    if (val > 0) {
        if (val >= 10.0) {
            while (val >= 10.0) { val /= 10.0; exponent++; }
        } else if (val < 1.0) {
            while (val < 1.0) { val *= 10.0; exponent--; }
        }
    }

    ftoa(buf, val, precision, uppercase);
    buf += strlen(buf);
    *buf++ = uppercase ? 'E' : 'e';
    *buf++ = (exponent >= 0) ? '+' : '-';
    if (exponent < 0) exponent = -exponent;
    if (exponent < 10) *buf++ = '0';
    char exp_buf[16];
    itoa(exp_buf, exponent, 0, 0);
    strcpy(buf, exp_buf);
}

static void gtoa(char *buf, double val, int precision, int uppercase, int alternate_form) {
    if (precision < 0) precision = 6;
    if (precision == 0) precision = 1;
    if (val != val || val > 1e308 || val < -1e308) { ftoa(buf, val, precision, uppercase); return; }

    double abs_val = (val < 0) ? -val : val;
    int exponent = 0;
    if (abs_val > 0) {
        double t = abs_val;
        if (t >= 10.0) { while (t >= 10.0) { t /= 10.0; exponent++; } }
        else if (t < 1.0) { while (t < 1.0) { t *= 10.0; exponent--; } }
    }

    if (exponent < -4 || exponent >= precision) etoa(buf, val, precision - 1, uppercase);
    else ftoa(buf, val, precision - 1 - exponent, uppercase);

    if (!alternate_form) {
        char *dot = strchr(buf, '.');
        if (dot) {
            char *e = strchr(dot, 'e');
            if (!e) e = strchr(dot, 'E');
            char *end = e ? e : dot + strlen(dot);
            char *p = end - 1;
            while (p > dot && *p == '0') *p-- = '\0';
            if (*p == '.') *p = '\0';
            if (e) memmove(p + (*p == '\0' ? 0 : 1), e, strlen(e) + 1);
        }
    }
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    char *s = str;
    const char *f = format;
    size_t remaining = size;
    size_t len = 0;

    #define EMIT(c) do { \
        if (remaining > 1 && s) { \
            *s++ = (c); \
            remaining--; \
        } \
        len++; \
    } while (0)

    while (*f) {
        if (*f == '%') {
            f++;
            int left_align = 0, force_sign = 0, space_prefix = 0, alternate_form = 0;
            while (1) {
                if (*f == '-') left_align = 1;
                else if (*f == '+') force_sign = 1;
                else if (*f == ' ') space_prefix = 1;
                else if (*f == '#') alternate_form = 1;
                else break;
                f++;
            }
            if (force_sign) space_prefix = 0;
            
            int width = 0, pad_zero = 0;
            if (*f == '0' && !left_align) { pad_zero = 1; f++; }
            if (*f == '*') {
                width = va_arg(ap, int);
                if (width < 0) { left_align = 1; width = -width; }
                f++;
            } else {
                while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
            }
            if (left_align) pad_zero = 0;
            
            int precision = -1;
            if (*f == '.') {
                f++;
                if (*f == '*') {
                    precision = va_arg(ap, int);
                    if (precision < 0) precision = -1;
                    f++;
                } else {
                    precision = 0;
                    while (*f >= '0' && *f <= '9') { precision = precision * 10 + (*f - '0'); f++; }
                }
            }

            enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_LONG_DOUBLE } length = LEN_NONE;
            if (*f == 'h') { f++; if (*f == 'h') { length = LEN_HH; f++; } else length = LEN_H; }
            else if (*f == 'l') { f++; if (*f == 'l') { length = LEN_LL; f++; } else length = LEN_L; }
            else if (*f == 'j') { length = LEN_J; f++; }
            else if (*f == 'z') { length = LEN_Z; f++; }
            else if (*f == 't') { length = LEN_T; f++; }
            else if (*f == 'L') { length = LEN_LONG_DOUBLE; f++; }
            
            switch (*f) {
                case 'd':
                case 'i': {
                    int64_t val;
                    if (length == LEN_LL || length == LEN_J) val = va_arg(ap, long long);
                    else if (length == LEN_L) val = va_arg(ap, long);
                    else if (length == LEN_H) val = (short)va_arg(ap, int);
                    else if (length == LEN_HH) val = (signed char)va_arg(ap, int);
                    else if (length == LEN_Z) val = va_arg(ap, ssize_t);
                    else if (length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, int);

                    if (precision != -1) pad_zero = 0;

                    char digits[64];
                    int is_negative = (val < 0);
                    uint64_t uval = (is_negative) ? (uint64_t)-val : (uint64_t)val;
                    
                    if (uval == 0 && precision == 0) {
                        digits[0] = '\0';
                    } else {
                        char b[64];
                        int i = 0;
                        if (uval == 0) b[i++] = '0';
                        else {
                            while (uval > 0) { b[i++] = (uval % 10) + '0'; uval /= 10; }
                        }
                        for (int j = 0; j < i; j++) digits[j] = b[i - j - 1];
                        digits[i] = '\0';
                    }

                    int digits_len = strlen(digits);
                    int precision_fill = (precision > digits_len) ? (precision - digits_len) : 0;
                    
                    const char *prefix = "";
                    if (is_negative) prefix = "-";
                    else if (force_sign) prefix = "+";
                    else if (space_prefix) prefix = " ";
                    int prefix_len = strlen(prefix);

                    int total_len = prefix_len + precision_fill + digits_len;
                    
                    if (!left_align && pad_zero) {
                        for (int i = 0; prefix[i]; i++) EMIT(prefix[i]);
                        for (int i = 0; i < width - total_len; i++) EMIT('0');
                        for (int i = 0; i < precision_fill; i++) EMIT('0');
                        for (int i = 0; i < digits_len; i++) EMIT(digits[i]);
                    } else if (!left_align) {
                        for (int i = 0; i < width - total_len; i++) EMIT(' ');
                        for (int i = 0; prefix[i]; i++) EMIT(prefix[i]);
                        for (int i = 0; i < precision_fill; i++) EMIT('0');
                        for (int i = 0; i < digits_len; i++) EMIT(digits[i]);
                    } else {
                        for (int i = 0; prefix[i]; i++) EMIT(prefix[i]);
                        for (int i = 0; i < precision_fill; i++) EMIT('0');
                        for (int i = 0; i < digits_len; i++) EMIT(digits[i]);
                        for (int i = 0; i < width - total_len; i++) EMIT(' ');
                    }
                    break;
                }
                case 'u':
                case 'o':
                case 'x':
                case 'X': {
                    uint64_t val;
                    if (length == LEN_LL || length == LEN_J) val = va_arg(ap, unsigned long long);
                    else if (length == LEN_L) val = va_arg(ap, unsigned long);
                    else if (length == LEN_H) val = (unsigned short)va_arg(ap, unsigned int);
                    else if (length == LEN_HH) val = (unsigned char)va_arg(ap, unsigned int);
                    else if (length == LEN_Z) val = va_arg(ap, size_t);
                    else if (length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, unsigned int);

                    if (precision != -1) pad_zero = 0;

                    char digits[64];
                    if (val == 0 && precision == 0) {
                        digits[0] = '\0';
                    } else {
                        char b[64];
                        int i = 0;
                        int base = (*f == 'o') ? 8 : ((*f == 'u') ? 10 : 16);
                        const char *symbols = (*f == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                        uint64_t uval = val;
                        if (uval == 0) b[i++] = '0';
                        else {
                            while (uval > 0) { b[i++] = symbols[uval % base]; uval /= base; }
                        }
                        for (int j = 0; j < i; j++) digits[j] = b[i - j - 1];
                        digits[i] = '\0';
                    }

                    int digits_len = strlen(digits);
                    int precision_fill = (precision > digits_len) ? (precision - digits_len) : 0;
                    
                    const char *prefix = "";
                    if (*f == 'o' && alternate_form && digits[0] != '0') prefix = "0";
                    else if ((*f == 'x' || *f == 'X') && alternate_form && val != 0) prefix = (*f == 'X') ? "0X" : "0x";
                    int prefix_len = strlen(prefix);

                    int total_len = prefix_len + precision_fill + digits_len;

                    if (!left_align && pad_zero) {
                        for (int i = 0; prefix[i]; i++) EMIT(prefix[i]);
                        for (int i = 0; i < width - total_len; i++) EMIT('0');
                        for (int i = 0; i < precision_fill; i++) EMIT('0');
                        for (int i = 0; i < digits_len; i++) EMIT(digits[i]);
                    } else if (left_align) {
                        for (int i = 0; prefix[i]; i++) EMIT(prefix[i]);
                        for (int i = 0; i < precision_fill; i++) EMIT('0');
                        for (int i = 0; i < digits_len; i++) EMIT(digits[i]);
                        for (int i = 0; i < width - total_len; i++) EMIT(' ');
                    } else {
                        for (int i = 0; i < width - total_len; i++) EMIT(' ');
                        for (int i = 0; prefix[i]; i++) EMIT(prefix[i]);
                        for (int i = 0; i < precision_fill; i++) EMIT('0');
                        for (int i = 0; i < digits_len; i++) EMIT(digits[i]);
                    }
                    break;
                }
                case 'f':
                case 'F':
                case 'e':
                case 'E':
                case 'g':
                case 'G': {
                    double val;
                    if (length == LEN_LONG_DOUBLE) val = (double)va_arg(ap, long double);
                    else val = va_arg(ap, double);
                    char tmp[128];
                    if (*f == 'f' || *f == 'F') ftoa(tmp, val, precision, (*f == 'F'));
                    else if (*f == 'e' || *f == 'E') etoa(tmp, val, precision, (*f == 'E'));
                    else gtoa(tmp, val, precision, (*f == 'G'), alternate_form);
                    int tmp_len = strlen(tmp);
                    if (width > tmp_len && !left_align) for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                    for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                    if (width > tmp_len && left_align) for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                    break;
                }
                case 'a':
                case 'A': {
                    va_arg(ap, double);
                    char tmp[16]; strcpy(tmp, (*f == 'A') ? "0X1.0P+0" : "0x1.0p+0");
                    int tmp_len = strlen(tmp);
                    if (width > tmp_len && !left_align) for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                    for (int i = 0; i < tmp_len; i++) EMIT(tmp[i]);
                    if (width > tmp_len && left_align) for (int i = 0; i < width - tmp_len; i++) EMIT(' ');
                    break;
                }
                case 'p': {
                    uintptr_t val = (uintptr_t)va_arg(ap, void*);
                    char tmp[32]; utoa_hex(tmp, val, 0);
                    int len_val = strlen(tmp);
                    int total_len = 2 + (len_val < 8 ? 8 : len_val);
                    if (width > total_len && !left_align) for (int i = 0; i < width - total_len; i++) EMIT(' ');
                    EMIT('0'); EMIT('x');
                    for (int i = 0; i < 8 - len_val; i++) EMIT('0');
                    for (int i = 0; i < len_val; i++) EMIT(tmp[i]);
                    if (width > total_len && left_align) for (int i = 0; i < width - total_len; i++) EMIT(' ');
                    break;
                }
                case 's': {
                    const char *val;
                    if (length == LEN_L) val = (const char *)va_arg(ap, void *);
                    else val = va_arg(ap, const char *);
                    if (!val) val = "(null)";
                    int s_len = 0;
                    const char *p = val;
                    while (*p && (precision == -1 || s_len < precision)) { s_len++; p++; }
                    if (!left_align && width > s_len) for (int i = 0; i < width - s_len; i++) EMIT(' ');
                    for (int i = 0; i < s_len; i++) EMIT(val[i]);
                    if (left_align && width > s_len) for (int i = 0; i < width - s_len; i++) EMIT(' ');
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    EMIT(c);
                    break;
                }
                case 'n': {
                    void *ptr = va_arg(ap, void*);
                    if (ptr) {
                        if (length == LEN_LL) *(long long *)ptr = len;
                        else if (length == LEN_L) *(long *)ptr = len;
                        else *(int *)ptr = len;
                    }
                    break;
                }
                case '%': EMIT('%'); break;
                default: EMIT('%'); EMIT(*f); break;
            }
            f++;
        } else { EMIT(*f); f++; }
    }
    if (size > 0 && s) {
        if (remaining > 0) *s = '\0';
        else str[size - 1] = '\0';
    }
    return len;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int ret = vsnprintf(str, INT_MAX, format, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, INT_MAX, format, ap);
}

char *__vasprintf_core(const char *fmt, va_list ap) {
    va_list ap2; va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (len < 0) return NULL;
    char *str = malloc(len + 1);
    if (!str) return NULL;
    vsnprintf(str, len + 1, fmt, ap);
    return str;
}

int vasprintf(char **strp, const char *fmt, va_list ap) {
    *strp = __vasprintf_core(fmt, ap);
    if (!*strp) return -1;
    return (int)strlen(*strp);
}

int asprintf(char **strp, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int ret = vasprintf(strp, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *format, ...) {
    va_list ap; va_start(ap, format);
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    fwrite(buf, 1, ret, stdout);
    return ret;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap; va_start(ap, format);
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    fwrite(buf, 1, ret, stream);
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
