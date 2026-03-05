#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <vm/vm_kmem.h>

// Integer to ASCII with optional sign/space prefix
static void itoa(char *buf, int64_t val, int force_sign, int space_prefix) {
    char tmp[32];
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
    uint64_t uval;
    if (is_negative) {
        uval = (uint64_t)-val;
    } else {
        uval = (uint64_t)val;
    }
    
    while (uval > 0) {
        tmp[i++] = (uval % 10) + '0';
        uval /= 10;
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
static void utoa_hex(char *buf, uint64_t val, int uppercase) {
    char tmp[32];
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
static void utoa_oct(char *buf, uint64_t val) {
    char tmp[32];
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

// Floating point to ASCII (simplistic)
static void ftoa(char *buf, double val, int precision, int uppercase) {
    if (precision < 0) precision = 6;
    
    // Handle NaN and Inf
    if (val != val) {
        strcpy(buf, uppercase ? "NAN" : "nan");
        return;
    }
    if (val > 1e308 || val < -1e308) { // Crude Infinity check
        strcpy(buf, uppercase ? "INF" : "inf");
        return;
    }

    if (val < 0) {
        *buf++ = '-';
        val = -val;
    }
    
    // Rounding
    double rounding = 0.5;
    for (int i = 0; i < precision; i++) rounding /= 10.0;
    val += rounding;

    int64_t integral = (int64_t)val;
    double fractional = val - (double)integral;
    
    // Print integral part
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

// Scientific notation to ASCII
static void etoa(char *buf, double val, int precision, int uppercase) {
    if (precision < 0) precision = 6;
    
    // Handle NaN and Inf
    if (val != val) {
        strcpy(buf, uppercase ? "NAN" : "nan");
        return;
    }
    if (val > 1e308 || val < -1e308) {
        strcpy(buf, uppercase ? "INF" : "inf");
        return;
    }

    if (val < 0) {
        *buf++ = '-';
        val = -val;
    }

    int exponent = 0;
    if (val > 0) {
        if (val >= 10.0) {
            while (val >= 10.0) {
                val /= 10.0;
                exponent++;
            }
        } else if (val < 1.0) {
            while (val < 1.0) {
                val *= 10.0;
                exponent--;
            }
        }
    }

    // Reuse ftoa logic for the mantissa (which is now in [1, 10))
    ftoa(buf, val, precision, uppercase);
    buf += strlen(buf);
    
    *buf++ = uppercase ? 'E' : 'e';
    *buf++ = (exponent >= 0) ? '+' : '-';
    if (exponent < 0) exponent = -exponent;
    
    // Exponent is usually at least 2 digits
    if (exponent < 10) *buf++ = '0';
    char exp_buf[16];
    itoa(exp_buf, exponent, 0, 0);
    strcpy(buf, exp_buf);
}

// Significant digits to ASCII
static void gtoa(char *buf, double val, int precision, int uppercase, int alternate_form) {
    if (precision < 0) precision = 6;
    if (precision == 0) precision = 1;

    // Handle NaN and Inf
    if (val != val || val > 1e308 || val < -1e308) {
        ftoa(buf, val, precision, uppercase);
        return;
    }

    double abs_val = (val < 0) ? -val : val;
    int exponent = 0;
    if (abs_val > 0) {
        double t = abs_val;
        if (t >= 10.0) {
            while (t >= 10.0) { t /= 10.0; exponent++; }
        } else if (t < 1.0) {
            while (t < 1.0) { t *= 10.0; exponent--; }
        }
    }

    if (exponent < -4 || exponent >= precision) {
        // Use scientific notation
        etoa(buf, val, precision - 1, uppercase);
    } else {
        // Use decimal notation
        ftoa(buf, val, precision - 1 - exponent, uppercase);
    }

    // Trim trailing zeros unless alternate form (#) is set
    if (!alternate_form) {
        char *dot = strchr(buf, '.');
        if (dot) {
            // Find if there's an exponent part (e/E)
            char *e = strchr(dot, 'e');
            if (!e) e = strchr(dot, 'E');
            char *end = e ? e : dot + strlen(dot);
            char *p = end - 1;
            while (p > dot && *p == '0') {
                *p-- = '\0';
            }
            if (*p == '.') {
                *p = '\0';
            }
            // If we had an exponent, shift it back
            if (e) {
                memmove(p + (*p == '\0' ? 0 : 1), e, strlen(e) + 1);
            }
        }
    }
}


enum length_modifier {
    LEN_NONE,
    LEN_HH,
    LEN_H,
    LEN_L,
    LEN_LL,
    LEN_J,
    LEN_Z,
    LEN_T,
    LEN_PTR,
    LEN_LONG_DOUBLE
};

struct printf_info {
    int left_align;
    int force_sign;
    int space_prefix;
    int alternate_form;
    int pad_zero;
    int width;
    int precision;
    enum length_modifier length;
    char specifier;
};

struct printf_ctx {
    char *s;
    size_t remaining;
    size_t len;
};

#define EMIT_CTX(ctx, c) do { \
    if ((ctx)->remaining > 1) { \
        *(ctx)->s++ = (c); \
        (ctx)->remaining--; \
    } \
    (ctx)->len++; \
} while (0)


static void format_integer(struct printf_ctx *ctx, struct printf_info *info, int64_t val) {
    char digits[64];
    int is_negative = (val < 0);
    uint64_t uval = (is_negative) ? (uint64_t)-val : (uint64_t)val;

    if (uval == 0 && info->precision == 0) {
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
    int precision_fill = (info->precision > digits_len) ? (info->precision - digits_len) : 0;

    const char *prefix = "";
    if (is_negative) prefix = "-";
    else if (info->force_sign) prefix = "+";
    else if (info->space_prefix) prefix = " ";
    int prefix_len = strlen(prefix);

    int total_len = prefix_len + precision_fill + digits_len;

    if (!info->left_align && info->pad_zero) {
        for (int i = 0; prefix[i]; i++) EMIT_CTX(ctx, prefix[i]);
        for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, '0');
        for (int i = 0; i < precision_fill; i++) EMIT_CTX(ctx, '0');
        for (int i = 0; i < digits_len; i++) EMIT_CTX(ctx, digits[i]);
    } else if (!info->left_align) {
        for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, ' ');
        for (int i = 0; prefix[i]; i++) EMIT_CTX(ctx, prefix[i]);
        for (int i = 0; i < precision_fill; i++) EMIT_CTX(ctx, '0');
        for (int i = 0; i < digits_len; i++) EMIT_CTX(ctx, digits[i]);
    } else {
        for (int i = 0; prefix[i]; i++) EMIT_CTX(ctx, prefix[i]);
        for (int i = 0; i < precision_fill; i++) EMIT_CTX(ctx, '0');
        for (int i = 0; i < digits_len; i++) EMIT_CTX(ctx, digits[i]);
        for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, ' ');
    }
}

static void format_unsigned(struct printf_ctx *ctx, struct printf_info *info, uint64_t val) {
    char digits[64];
    if (val == 0 && info->precision == 0) {
        digits[0] = '\0';
    } else {
        char b[64];
        int i = 0;
        uint64_t uval = val;
        if (uval == 0) b[i++] = '0';
        else {
            while (uval > 0) { b[i++] = (uval % 10) + '0'; uval /= 10; }
        }
        for (int j = 0; j < i; j++) digits[j] = b[i - j - 1];
        digits[i] = '\0';
    }

    int digits_len = strlen(digits);
    int precision_fill = (info->precision > digits_len) ? (info->precision - digits_len) : 0;
    int total_len = precision_fill + digits_len;

    if (!info->left_align && info->pad_zero) {
         for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, '0');
         for (int i = 0; i < precision_fill; i++) EMIT_CTX(ctx, '0');
         for (int i = 0; i < digits_len; i++) EMIT_CTX(ctx, digits[i]);
    } else if (info->left_align) {
        for (int i = 0; i < precision_fill; i++) EMIT_CTX(ctx, '0');
        for (int i = 0; i < digits_len; i++) EMIT_CTX(ctx, digits[i]);
        for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, ' ');
    } else {
        for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, ' ');
        for (int i = 0; i < precision_fill; i++) EMIT_CTX(ctx, '0');
        for (int i = 0; i < digits_len; i++) EMIT_CTX(ctx, digits[i]);
    }
}

static void format_octal(struct printf_ctx *ctx, struct printf_info *info, uint64_t val) {
    char tmp[64];
    char *ptr = tmp;
    if (info->alternate_form && val != 0) *ptr++ = '0';
    utoa_oct(ptr, val);
    int tmp_len = strlen(tmp);

    if (info->width > tmp_len) {
        if (info->pad_zero) {
             for (int i = 0; i < info->width - tmp_len; i++) EMIT_CTX(ctx, '0');
             for (int i = 0; i < tmp_len; i++) EMIT_CTX(ctx, tmp[i]);
        } else if (info->left_align) {
            for (int i = 0; i < tmp_len; i++) EMIT_CTX(ctx, tmp[i]);
            for (int i = 0; i < info->width - tmp_len; i++) EMIT_CTX(ctx, ' ');
        } else {
            for (int i = 0; i < info->width - tmp_len; i++) EMIT_CTX(ctx, ' ');
            for (int i = 0; i < tmp_len; i++) EMIT_CTX(ctx, tmp[i]);
        }
    } else {
        for (int i = 0; i < tmp_len; i++) EMIT_CTX(ctx, tmp[i]);
    }
}

static void format_hex(struct printf_ctx *ctx, struct printf_info *info, uint64_t val, int uppercase) {
    char tmp[64];
    utoa_hex(tmp, val, uppercase);
    int len_val = strlen(tmp);
    int prefix_len = (info->alternate_form && val != 0) ? 2 : 0;
    int total_len = len_val + prefix_len;

    if (info->width > total_len) {
        if (info->pad_zero && !info->left_align) {
            if (prefix_len) {
                EMIT_CTX(ctx, '0');
                EMIT_CTX(ctx, uppercase ? 'X' : 'x');
            }
            for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, '0');
            for (int i = 0; i < len_val; i++) EMIT_CTX(ctx, tmp[i]);
        } else if (info->left_align) {
            if (prefix_len) {
                EMIT_CTX(ctx, '0');
                EMIT_CTX(ctx, uppercase ? 'X' : 'x');
            }
            for (int i = 0; i < len_val; i++) EMIT_CTX(ctx, tmp[i]);
            for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, ' ');
        } else {
            for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, ' ');
            if (prefix_len) {
                EMIT_CTX(ctx, '0');
                EMIT_CTX(ctx, uppercase ? 'X' : 'x');
            }
            for (int i = 0; i < len_val; i++) EMIT_CTX(ctx, tmp[i]);
        }
    } else {
        if (prefix_len) {
            EMIT_CTX(ctx, '0');
            EMIT_CTX(ctx, uppercase ? 'X' : 'x');
        }
        for (int i = 0; i < len_val; i++) EMIT_CTX(ctx, tmp[i]);
    }
}

static void format_float_helper(struct printf_ctx *ctx, struct printf_info *info, const char *tmp) {
    int tmp_len = strlen(tmp);
    if (info->width > tmp_len && !info->left_align) {
        for (int i = 0; i < info->width - tmp_len; i++) EMIT_CTX(ctx, ' ');
    }
    for (int i = 0; i < tmp_len; i++) EMIT_CTX(ctx, tmp[i]);
    if (info->width > tmp_len && info->left_align) {
        for (int i = 0; i < info->width - tmp_len; i++) EMIT_CTX(ctx, ' ');
    }
}

static void format_pointer(struct printf_ctx *ctx, struct printf_info *info, unsigned int val) {
    char tmp[32];
    utoa_hex(tmp, val, 0); // digits only
    int len_val = strlen(tmp);
    int zeros = 8 - len_val;
    if (zeros < 0) zeros = 0;

    int total_len = 2 + zeros + len_val; // 0x + zeros + digits

    if (info->width > total_len && !info->left_align) {
        for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, ' ');
    }

    EMIT_CTX(ctx, '0'); EMIT_CTX(ctx, 'x');
    for (int i = 0; i < zeros; i++) EMIT_CTX(ctx, '0');
    for (int i = 0; i < len_val; i++) EMIT_CTX(ctx, tmp[i]);

    if (info->width > total_len && info->left_align) {
        for (int i = 0; i < info->width - total_len; i++) EMIT_CTX(ctx, ' ');
    }
}

static void format_string(struct printf_ctx *ctx, struct printf_info *info, const char *val) {
    if (!val) val = "(null)";

    int s_len = 0;
    const char *p = val;
    while (*p && (info->precision == -1 || s_len < info->precision)) {
        s_len++;
        p++;
    }

    if (!info->left_align && info->width > s_len) {
        for (int i = 0; i < info->width - s_len; i++) EMIT_CTX(ctx, ' ');
    }

    for (int i = 0; i < s_len; i++) EMIT_CTX(ctx, val[i]);

    if (info->left_align && info->width > s_len) {
         for (int i = 0; i < info->width - s_len; i++) EMIT_CTX(ctx, ' ');
    }
}


int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    struct printf_ctx ctx = { .s = str, .remaining = size, .len = 0 };
    const char *f = format;

    while (*f) {
        if (*f == '%') {
            f++;
            
            struct printf_info info = {
                .left_align = 0,
                .force_sign = 0,
                .space_prefix = 0,
                .alternate_form = 0,
                .pad_zero = 0,
                .width = 0,
                .precision = -1,
                .length = LEN_NONE,
                .specifier = 0
            };
            
            while (1) {
                if (*f == '-') { info.left_align = 1; info.pad_zero = 0; f++; }
                else if (*f == '+') { info.force_sign = 1; info.space_prefix = 0; f++; }
                else if (*f == ' ') { if (!info.force_sign) info.space_prefix = 1; f++; }
                else if (*f == '#') { info.alternate_form = 1; f++; }
                else if (*f == '0') { if (!info.left_align) info.pad_zero = 1; f++; }
                else break;
            }

            if (*f == '*') {
                info.width = va_arg(ap, int);
                if (info.width < 0) { info.left_align = 1; info.width = -info.width; }
                f++;
            } else {
                while (*f >= '0' && *f <= '9') { info.width = info.width * 10 + (*f - '0'); f++; }
            }

            if (info.left_align) info.pad_zero = 0;

            if (*f == '.') {
                f++;
                if (*f == '*') {
                    info.precision = va_arg(ap, int);
                    if (info.precision < 0) info.precision = -1;
                    f++;
                } else {
                    info.precision = 0;
                    while (*f >= '0' && *f <= '9') { info.precision = info.precision * 10 + (*f - '0'); f++; }
                }
            }

            if (*f == 'h') {
                f++;
                if (*f == 'h') { info.length = LEN_HH; f++; }
                else info.length = LEN_H;
            } else if (*f == 'l') {
                f++;
                if (*f == 'l') { info.length = LEN_LL; f++; }
                else info.length = LEN_L;
            } else if (*f == 'j') { info.length = LEN_J; f++;
            } else if (*f == 'z') { info.length = LEN_Z; f++;
            } else if (*f == 't') { info.length = LEN_T; f++;
            } else if (*f == 'L') { info.length = LEN_LONG_DOUBLE; f++; }

            info.specifier = *f;
            
            switch (*f) {
                case 'd':
                case 'i': {
                    int64_t val;
                    if (info.length == LEN_LL || info.length == LEN_J) val = va_arg(ap, long long);
                    else if (info.length == LEN_L) val = va_arg(ap, long);
                    else if (info.length == LEN_H) val = (short)va_arg(ap, int);
                    else if (info.length == LEN_HH) val = (signed char)va_arg(ap, int);
                    else if (info.length == LEN_Z) val = va_arg(ap, ssize_t);
                    else if (info.length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, int);

                    if (info.precision != -1) info.pad_zero = 0;
                    format_integer(&ctx, &info, val);
                    break;
                }
                case 'u': {
                    uint64_t val;
                    if (info.length == LEN_LL || info.length == LEN_J) val = va_arg(ap, unsigned long long);
                    else if (info.length == LEN_L) val = va_arg(ap, unsigned long);
                    else if (info.length == LEN_H) val = (unsigned short)va_arg(ap, unsigned int);
                    else if (info.length == LEN_HH) val = (unsigned char)va_arg(ap, unsigned int);
                    else if (info.length == LEN_Z) val = va_arg(ap, size_t);
                    else if (info.length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, unsigned int);

                    if (info.precision != -1) info.pad_zero = 0;
                    format_unsigned(&ctx, &info, val);
                    break;
                }
                case 'o': {
                    uint64_t val;
                    if (info.length == LEN_LL || info.length == LEN_J) val = va_arg(ap, unsigned long long);
                    else if (info.length == LEN_L) val = va_arg(ap, unsigned long);
                    else if (info.length == LEN_H) val = (unsigned short)va_arg(ap, unsigned int);
                    else if (info.length == LEN_HH) val = (unsigned char)va_arg(ap, unsigned int);
                    else if (info.length == LEN_Z) val = va_arg(ap, size_t);
                    else if (info.length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, unsigned int);
                    format_octal(&ctx, &info, val);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t val;
                    if (info.length == LEN_LL || info.length == LEN_J) val = va_arg(ap, unsigned long long);
                    else if (info.length == LEN_L) val = va_arg(ap, unsigned long);
                    else if (info.length == LEN_H) val = (unsigned short)va_arg(ap, unsigned int);
                    else if (info.length == LEN_HH) val = (unsigned char)va_arg(ap, unsigned int);
                    else if (info.length == LEN_Z) val = va_arg(ap, size_t);
                    else if (info.length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, unsigned int);
                    format_hex(&ctx, &info, val, (*f == 'X'));
                    break;
                }
                case 'f':
                case 'F': {
                    double val = (info.length == LEN_LONG_DOUBLE) ? (double)va_arg(ap, long double) : va_arg(ap, double);
                    char tmp[128];
                    ftoa(tmp, val, info.precision, (*f == 'F'));
                    format_float_helper(&ctx, &info, tmp);
                    break;
                }
                case 'e':
                case 'E': {
                    double val = (info.length == LEN_LONG_DOUBLE) ? (double)va_arg(ap, long double) : va_arg(ap, double);
                    char tmp[128];
                    etoa(tmp, val, info.precision, (*f == 'E'));
                    format_float_helper(&ctx, &info, tmp);
                    break;
                }
                case 'g':
                case 'G': {
                    double val = (info.length == LEN_LONG_DOUBLE) ? (double)va_arg(ap, long double) : va_arg(ap, double);
                    char tmp[128];
                    gtoa(tmp, val, info.precision, (*f == 'G'), info.alternate_form);
                    format_float_helper(&ctx, &info, tmp);
                    break;
                }
                case 'a':
                case 'A': {
                    double val = va_arg(ap, double);
                    (void)val;
                    char tmp[128];
                    strcpy(tmp, (*f == 'A') ? "0X1.0P+0" : "0x1.0p+0");
                    format_float_helper(&ctx, &info, tmp);
                    break;
                }
                case 'p': {
                    unsigned int val = (unsigned int)(uintptr_t)va_arg(ap, void*);
                    format_pointer(&ctx, &info, val);
                    break;
                }
                case 's': {
                    const char *val = (info.length == LEN_L) ? (const char *)va_arg(ap, void *) : va_arg(ap, const char *);
                    format_string(&ctx, &info, val);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    EMIT_CTX(&ctx, c);
                    break;
                }
                case 'n': {
                    void *ptr = va_arg(ap, void*);
                    if (ptr) {
                        if (info.length == LEN_LL) *(long long *)ptr = (long long)ctx.len;
                        else if (info.length == LEN_L) *(long *)ptr = (long)ctx.len;
                        else if (info.length == LEN_HH) *(signed char *)ptr = (signed char)ctx.len;
                        else if (info.length == LEN_H) *(short *)ptr = (short)ctx.len;
                        else if (info.length == LEN_J) *(intmax_t *)ptr = (intmax_t)ctx.len;
                        else if (info.length == LEN_Z) *(ssize_t *)ptr = (ssize_t)ctx.len;
                        else if (info.length == LEN_T) *(ptrdiff_t *)ptr = (ptrdiff_t)ctx.len;
                        else *(int *)ptr = (int)ctx.len;
                    }
                    break;
                }
                case '%':
                    EMIT_CTX(&ctx, '%');
                    break;
                default:
                    EMIT_CTX(&ctx, '%');
                    EMIT_CTX(&ctx, *f);
                    break;
            }
            f++;
        } else {
            EMIT_CTX(&ctx, *f);
            f++;
        }
    }

    if (size > 0) {
        if (ctx.remaining > 0) {
            *ctx.s = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    return ctx.len;
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

char *kvasprintf(const char *fmt, va_list ap) {
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
    char *str = kvasprintf(fmt, ap);
    va_end(ap);
    return str;
}
