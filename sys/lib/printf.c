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

enum format_length {
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

struct format_flags {
    int left_align;
    int force_sign;
    int space_prefix;
    int alternate_form;
    int pad_zero;
    int width;
    int precision;
    enum format_length length;
};

struct format_state {
    char *str;
    size_t remaining;
    size_t len;
    size_t total_size;
};

static void emit_char(struct format_state *state, char c) {
    if (state->remaining > 1) {
        *state->str++ = c;
        state->remaining--;
    }
    state->len++;
}

static void emit_string(struct format_state *state, const char *s, int len) {
    for (int i = 0; i < len; i++) {
        emit_char(state, s[i]);
    }
}

static void emit_padding(struct format_state *state, int count, char pad_char) {
    for (int i = 0; i < count; i++) {
        emit_char(state, pad_char);
    }
}

static void format_int(struct format_state *state, struct format_flags *flags, int64_t val) {
    if (flags->precision != -1) flags->pad_zero = 0;

    char digits[64];
    int is_negative = (val < 0);
    uint64_t uval = (is_negative) ? (uint64_t)-val : (uint64_t)val;

    if (uval == 0 && flags->precision == 0) {
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
    int precision_fill = (flags->precision > digits_len) ? (flags->precision - digits_len) : 0;

    const char *prefix = "";
    if (is_negative) prefix = "-";
    else if (flags->force_sign) prefix = "+";
    else if (flags->space_prefix) prefix = " ";
    int prefix_len = strlen(prefix);

    int total_len = prefix_len + precision_fill + digits_len;

    if (!flags->left_align && flags->pad_zero) {
        emit_string(state, prefix, prefix_len);
        emit_padding(state, flags->width - total_len, '0');
        emit_padding(state, precision_fill, '0');
        emit_string(state, digits, digits_len);
    } else if (!flags->left_align) {
        emit_padding(state, flags->width - total_len, ' ');
        emit_string(state, prefix, prefix_len);
        emit_padding(state, precision_fill, '0');
        emit_string(state, digits, digits_len);
    } else {
        emit_string(state, prefix, prefix_len);
        emit_padding(state, precision_fill, '0');
        emit_string(state, digits, digits_len);
        emit_padding(state, flags->width - total_len, ' ');
    }
}

static void format_uint(struct format_state *state, struct format_flags *flags, uint64_t val) {
    if (flags->precision != -1) flags->pad_zero = 0;

    char digits[64];
    if (val == 0 && flags->precision == 0) {
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
    int precision_fill = (flags->precision > digits_len) ? (flags->precision - digits_len) : 0;
    int total_len = precision_fill + digits_len;

    if (!flags->left_align && flags->pad_zero) {
        emit_padding(state, flags->width - total_len, '0');
        emit_padding(state, precision_fill, '0');
        emit_string(state, digits, digits_len);
    } else if (flags->left_align) {
        emit_padding(state, precision_fill, '0');
        emit_string(state, digits, digits_len);
        emit_padding(state, flags->width - total_len, ' ');
    } else {
        emit_padding(state, flags->width - total_len, ' ');
        emit_padding(state, precision_fill, '0');
        emit_string(state, digits, digits_len);
    }
}

static void format_oct(struct format_state *state, struct format_flags *flags, uint64_t val) {
    char tmp[64];
    char *ptr = tmp;
    if (flags->alternate_form && val != 0) *ptr++ = '0';
    utoa_oct(ptr, val);
    int tmp_len = strlen(tmp);

    if (flags->width > tmp_len) {
        if (flags->pad_zero) {
            emit_padding(state, flags->width - tmp_len, '0');
            emit_string(state, tmp, tmp_len);
        } else if (flags->left_align) {
            emit_string(state, tmp, tmp_len);
            emit_padding(state, flags->width - tmp_len, ' ');
        } else {
            emit_padding(state, flags->width - tmp_len, ' ');
            emit_string(state, tmp, tmp_len);
        }
    } else {
        emit_string(state, tmp, tmp_len);
    }
}

static void format_hex(struct format_state *state, struct format_flags *flags, uint64_t val, int uppercase) {
    char tmp[64];
    utoa_hex(tmp, val, uppercase);
    int len_val = strlen(tmp);
    int prefix_len = (flags->alternate_form && val != 0) ? 2 : 0;
    int total_len = len_val + prefix_len;

    if (flags->width > total_len) {
        if (flags->pad_zero && !flags->left_align) {
            if (prefix_len) {
                emit_char(state, '0');
                emit_char(state, uppercase ? 'X' : 'x');
            }
            emit_padding(state, flags->width - total_len, '0');
            emit_string(state, tmp, len_val);
        } else if (flags->left_align) {
            if (prefix_len) {
                emit_char(state, '0');
                emit_char(state, uppercase ? 'X' : 'x');
            }
            emit_string(state, tmp, len_val);
            emit_padding(state, flags->width - total_len, ' ');
        } else {
            emit_padding(state, flags->width - total_len, ' ');
            if (prefix_len) {
                emit_char(state, '0');
                emit_char(state, uppercase ? 'X' : 'x');
            }
            emit_string(state, tmp, len_val);
        }
    } else {
        if (prefix_len) {
            emit_char(state, '0');
            emit_char(state, uppercase ? 'X' : 'x');
        }
        emit_string(state, tmp, len_val);
    }
}

static void format_float(struct format_state *state, struct format_flags *flags, double val, char specifier) {
    char tmp[128];
    if (specifier == 'f' || specifier == 'F') {
        ftoa(tmp, val, flags->precision, (specifier == 'F'));
    } else if (specifier == 'e' || specifier == 'E') {
        etoa(tmp, val, flags->precision, (specifier == 'E'));
    } else if (specifier == 'g' || specifier == 'G') {
        gtoa(tmp, val, flags->precision, (specifier == 'G'), flags->alternate_form);
    } else if (specifier == 'a' || specifier == 'A') {
        strcpy(tmp, (specifier == 'A') ? "0X1.0P+0" : "0x1.0p+0");
    }

    int tmp_len = strlen(tmp);

    if (flags->width > tmp_len && !flags->left_align) {
        emit_padding(state, flags->width - tmp_len, ' ');
    }
    emit_string(state, tmp, tmp_len);
    if (flags->width > tmp_len && flags->left_align) {
        emit_padding(state, flags->width - tmp_len, ' ');
    }
}

static void format_ptr(struct format_state *state, struct format_flags *flags, unsigned int val) {
    char tmp[32];
    utoa_hex(tmp, val, 0); // digits only
    int len_val = strlen(tmp);
    int zeros = 8 - len_val;
    if (zeros < 0) zeros = 0;

    int total_len = 2 + zeros + len_val; // 0x + zeros + digits

    if (flags->width > total_len && !flags->left_align) {
        emit_padding(state, flags->width - total_len, ' ');
    }

    emit_char(state, '0'); emit_char(state, 'x');
    emit_padding(state, zeros, '0');
    emit_string(state, tmp, len_val);

    if (flags->width > total_len && flags->left_align) {
        emit_padding(state, flags->width - total_len, ' ');
    }
}

static void format_string(struct format_state *state, struct format_flags *flags, const char *val) {
    if (!val) val = "(null)";

    int s_len = 0;
    const char *p = val;
    while (*p && (flags->precision == -1 || s_len < flags->precision)) {
        s_len++;
        p++;
    }

    if (!flags->left_align && flags->width > s_len) {
        emit_padding(state, flags->width - s_len, ' ');
    }

    emit_string(state, val, s_len);

    if (flags->left_align && flags->width > s_len) {
        emit_padding(state, flags->width - s_len, ' ');
    }
}

static void format_char(struct format_state *state, struct format_flags *flags, char c) {
    (void)flags;
    emit_char(state, c);
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    struct format_state state = {
        .str = str,
        .remaining = size,
        .len = 0,
        .total_size = size
    };

    const char *f = format;

    while (*f) {
        if (*f == '%') {
            f++;
            
            struct format_flags flags = {
                .left_align = 0,
                .force_sign = 0,
                .space_prefix = 0,
                .alternate_form = 0,
                .pad_zero = 0,
                .width = 0,
                .precision = -1,
                .length = LEN_NONE
            };
            
            // Parse flags
            while (1) {
                if (*f == '-') {
                    flags.left_align = 1;
                    flags.pad_zero = 0;
                    f++;
                } else if (*f == '+') {
                    flags.force_sign = 1;
                    flags.space_prefix = 0;
                    f++;
                } else if (*f == ' ') {
                    if (!flags.force_sign) flags.space_prefix = 1;
                    f++;
                } else if (*f == '#') {
                    flags.alternate_form = 1;
                    f++;
                } else if (*f == '0') {
                    if (!flags.left_align) flags.pad_zero = 1;
                    f++;
                } else {
                    break;
                }
            }

            // Parse width
            if (*f == '*') {
                flags.width = va_arg(ap, int);
                if (flags.width < 0) {
                    flags.left_align = 1;
                    flags.width = -flags.width;
                }
                f++;
            } else {
                while (*f >= '0' && *f <= '9') {
                    flags.width = flags.width * 10 + (*f - '0');
                    f++;
                }
            }

            if (flags.left_align) flags.pad_zero = 0;

            // Parse precision
            if (*f == '.') {
                f++;
                if (*f == '*') {
                    flags.precision = va_arg(ap, int);
                    if (flags.precision < 0) flags.precision = -1;
                    f++;
                } else {
                    flags.precision = 0;
                    while (*f >= '0' && *f <= '9') {
                        flags.precision = flags.precision * 10 + (*f - '0');
                        f++;
                    }
                }
            }

            // Parse length modifier
            if (*f == 'h') {
                f++;
                if (*f == 'h') {
                    flags.length = LEN_HH;
                    f++;
                } else {
                    flags.length = LEN_H;
                }
            } else if (*f == 'l') {
                f++;
                if (*f == 'l') {
                    flags.length = LEN_LL;
                    f++;
                } else {
                    flags.length = LEN_L;
                }
            } else if (*f == 'j') {
                flags.length = LEN_J;
                f++;
            } else if (*f == 'z') {
                flags.length = LEN_Z;
                f++;
            } else if (*f == 't') {
                flags.length = LEN_T;
                f++;
            } else if (*f == 'L') {
                flags.length = LEN_LONG_DOUBLE;
                f++;
            }
            
            switch (*f) {
                case 'd':
                case 'i': {
                    int64_t val;
                    if (flags.length == LEN_LL || flags.length == LEN_J) val = va_arg(ap, long long);
                    else if (flags.length == LEN_L) val = va_arg(ap, long);
                    else if (flags.length == LEN_H) val = (short)va_arg(ap, int);
                    else if (flags.length == LEN_HH) val = (signed char)va_arg(ap, int);
                    else if (flags.length == LEN_Z) val = va_arg(ap, ssize_t);
                    else if (flags.length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, int);
                    format_int(&state, &flags, val);
                    break;
                }
                case 'u': {
                    uint64_t val;
                    if (flags.length == LEN_LL || flags.length == LEN_J) val = va_arg(ap, unsigned long long);
                    else if (flags.length == LEN_L) val = va_arg(ap, unsigned long);
                    else if (flags.length == LEN_H) val = (unsigned short)va_arg(ap, unsigned int);
                    else if (flags.length == LEN_HH) val = (unsigned char)va_arg(ap, unsigned int);
                    else if (flags.length == LEN_Z) val = va_arg(ap, size_t);
                    else if (flags.length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, unsigned int);
                    format_uint(&state, &flags, val);
                    break;
                }
                case 'o': {
                    uint64_t val;
                    if (flags.length == LEN_LL || flags.length == LEN_J) val = va_arg(ap, unsigned long long);
                    else if (flags.length == LEN_L) val = va_arg(ap, unsigned long);
                    else if (flags.length == LEN_H) val = (unsigned short)va_arg(ap, unsigned int);
                    else if (flags.length == LEN_HH) val = (unsigned char)va_arg(ap, unsigned int);
                    else if (flags.length == LEN_Z) val = va_arg(ap, size_t);
                    else if (flags.length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, unsigned int);
                    format_oct(&state, &flags, val);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t val;
                    if (flags.length == LEN_LL || flags.length == LEN_J) val = va_arg(ap, unsigned long long);
                    else if (flags.length == LEN_L) val = va_arg(ap, unsigned long);
                    else if (flags.length == LEN_H) val = (unsigned short)va_arg(ap, unsigned int);
                    else if (flags.length == LEN_HH) val = (unsigned char)va_arg(ap, unsigned int);
                    else if (flags.length == LEN_Z) val = va_arg(ap, size_t);
                    else if (flags.length == LEN_T) val = va_arg(ap, ptrdiff_t);
                    else val = va_arg(ap, unsigned int);
                    format_hex(&state, &flags, val, (*f == 'X'));
                    break;
                }
                case 'f':
                case 'F':
                case 'e':
                case 'E':
                case 'g':
                case 'G':
                case 'a':
                case 'A': {
                    double val;
                    if (flags.length == LEN_LONG_DOUBLE) val = (double)va_arg(ap, long double);
                    else val = va_arg(ap, double);
                    format_float(&state, &flags, val, *f);
                    break;
                }
                case 'p': {
                    unsigned int val = (unsigned int)(uintptr_t)va_arg(ap, void*);
                    format_ptr(&state, &flags, val);
                    break;
                }
                case 's': {
                    const char *val;
                    if (flags.length == LEN_L) val = (const char *)va_arg(ap, void *);
                    else val = va_arg(ap, const char *);
                    format_string(&state, &flags, val);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    format_char(&state, &flags, c);
                    break;
                }
                case 'n': {
                    void *ptr = va_arg(ap, void*);
                    if (ptr) {
                        if (flags.length == LEN_LL) *(long long *)ptr = (long long)state.len;
                        else if (flags.length == LEN_L) *(long *)ptr = (long)state.len;
                        else if (flags.length == LEN_HH) *(signed char *)ptr = (signed char)state.len;
                        else if (flags.length == LEN_H) *(short *)ptr = (short)state.len;
                        else if (flags.length == LEN_J) *(intmax_t *)ptr = (intmax_t)state.len;
                        else if (flags.length == LEN_Z) *(ssize_t *)ptr = (ssize_t)state.len;
                        else if (flags.length == LEN_T) *(ptrdiff_t *)ptr = (ptrdiff_t)state.len;
                        else *(int *)ptr = (int)state.len;
                    }
                    break;
                }
                case '%':
                    emit_char(&state, '%');
                    break;
                default:
                    emit_char(&state, '%');
                    emit_char(&state, *f);
                    break;
            }
            f++;
        } else {
            emit_char(&state, *f);
            f++;
        }
    }

    if (size > 0) {
        if (state.remaining > 0) {
            *state.str = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    return state.len;
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
