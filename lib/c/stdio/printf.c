#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stddef.h>
#include <errno.h>

// Helpers from kernel printf.c adapted for libc

static void itoa(char *buf, size_t size, int64_t val, int force_sign, int space_prefix) {
    char tmp[32];
    int i = 0;
    int is_negative = (val < 0);

    if (val == 0) {
        if (force_sign) {
            strlcpy(buf, "+0", size);
        } else if (space_prefix) {
            strlcpy(buf, " 0", size);
        } else {
            strlcpy(buf, "0", size);
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

    size_t j = 0;
    if (is_negative) { if (j < size - 1) buf[j++] = '-'; }
    else if (force_sign) { if (j < size - 1) buf[j++] = '+'; }
    else if (space_prefix) { if (j < size - 1) buf[j++] = ' '; }

    for (int k = 0; k < i; k++) {
        if (j < size - 1) buf[j++] = tmp[i - k - 1];
    }
    if (size > 0) buf[j < size ? j : size - 1] = '\0';
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

static void ftoa(char *buf, size_t size, double val, int precision, int uppercase) {
    if (precision < 0) precision = 6;
    if (val != val) { strlcpy(buf, uppercase ? "NAN" : "nan", size); return; }
    if (val > 1e308 || val < -1e308) { strlcpy(buf, uppercase ? "INF" : "inf", size); return; }
    if (val < 0) {
        if (size > 1) {
            *buf++ = '-';
            size--;
        }
        val = -val;
    }

    double rounding = 0.5;
    for (int i = 0; i < precision; i++) rounding /= 10.0;
    val += rounding;

    int64_t integral = (int64_t)val;
    double fractional = val - (double)integral;

    char tmp[64];
    itoa(tmp, sizeof(tmp), integral, 0, 0);
    size_t len = strlcpy(buf, tmp, size);
    if (len >= size) len = size > 0 ? size - 1 : 0;
    buf += len;
    size -= len;

    if (precision > 0 && size > 1) {
        *buf++ = '.';
        size--;
        for (int i = 0; i < precision && size > 1; i++) {
            fractional *= 10.0;
            int digit = (int)fractional;
            *buf++ = digit + '0';
            size--;
            fractional -= digit;
        }
    }
    if (size > 0) *buf = '\0';
}

static void etoa(char *buf, size_t size, double val, int precision, int uppercase) {
    if (precision < 0) precision = 6;
    if (val != val) { strlcpy(buf, uppercase ? "NAN" : "nan", size); return; }
    if (val > 1e308 || val < -1e308) { strlcpy(buf, uppercase ? "INF" : "inf", size); return; }
    if (val < 0) {
        if (size > 1) {
            *buf++ = '-';
            size--;
        }
        val = -val;
    }

    int exponent = 0;
    if (val > 0) {
        if (val >= 10.0) {
            while (val >= 10.0) { val /= 10.0; exponent++; }
        } else if (val < 1.0) {
            while (val < 1.0) { val *= 10.0; exponent--; }
        }
    }

    ftoa(buf, size, val, precision, uppercase);
    size_t len = strlen(buf);
    buf += len;
    size -= len;

    if (size > 1) { *buf++ = uppercase ? 'E' : 'e'; size--; }
    if (size > 1) { *buf++ = (exponent >= 0) ? '+' : '-'; size--; }
    if (exponent < 0) exponent = -exponent;
    if (exponent < 10 && size > 1) { *buf++ = '0'; size--; }
    char exp_buf[16];
    itoa(exp_buf, sizeof(exp_buf), exponent, 0, 0);
    strlcpy(buf, exp_buf, size);
}

static void gtoa(char *buf, size_t size, double val, int precision, int uppercase, int alternate_form) {
    if (precision < 0) precision = 6;
    if (precision == 0) precision = 1;
    if (val != val || val > 1e308 || val < -1e308) { ftoa(buf, size, val, precision, uppercase); return; }

    double abs_val = (val < 0) ? -val : val;
    int exponent = 0;
    if (abs_val > 0) {
        double t = abs_val;
        if (t >= 10.0) { while (t >= 10.0) { t /= 10.0; exponent++; } }
        else if (t < 1.0) { while (t < 1.0) { t *= 10.0; exponent--; } }
    }

    if (exponent < -4 || exponent >= precision) etoa(buf, size, val, precision - 1, uppercase);
    else ftoa(buf, size, val, precision - 1 - exponent, uppercase);

    if (!alternate_form) {
        char *dot = buf;
        while (*dot && *dot != '.') dot++;

        if (*dot == '.') {
            char *e = dot + 1;
            while (*e && *e != 'e' && *e != 'E') e++;

            char *p = e - 1;
            while (p > dot && *p == '0') p--;
            if (*p == '.') p--;

            p++;

            if (*e) {
                while (*e) *p++ = *e++;
            }
            *p = '\0';
        }
    }
}

/*
 * %a / %A — hexadecimal floating-point.  Emits [sign][0x]h[.hhh]p[+-]ddd
 * where h is one hex digit (1 for normals, 0 for subnormals and zero),
 * the fractional nibbles come from the IEEE-754 binary64 mantissa, and
 * the binary exponent is decimal (unbiased: exp_field - 1023, or -1022
 * for subnormals).  Precision selects the count of fractional hex
 * digits with round-to-nearest-even; -1 means "shortest exact".
 */
static void atoa(char *buf, size_t size, double val, int precision,
                 int uppercase, int alternate_form, int force_sign,
                 int space_prefix) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char p_char = uppercase ? 'P' : 'p';
    char x_char = uppercase ? 'X' : 'x';

    union { double d; uint64_t u; } u;
    u.d = val;
    int sign = (int)((u.u >> 63) & 1);
    uint64_t exp_field = (u.u >> 52) & 0x7FF;
    uint64_t mantissa = u.u & (((uint64_t)1 << 52) - 1);

    char *o = buf;
    size_t rem = size;
#define APUT(c) do { if (rem > 1) { *o++ = (char)(c); rem--; } } while (0)

    if (sign) APUT('-');
    else if (force_sign) APUT('+');
    else if (space_prefix) APUT(' ');

    if (exp_field == 0x7FF) {
        const char *s = (mantissa == 0)
            ? (uppercase ? "INF" : "inf")
            : (uppercase ? "NAN" : "nan");
        while (*s) APUT(*s++);
        if (rem > 0) *o = '\0';
        return;
    }

    int exponent;
    int leading_digit;
    if (exp_field == 0) {
        leading_digit = 0;
        exponent = (mantissa == 0) ? 0 : -1022;
    } else {
        leading_digit = 1;
        exponent = (int)exp_field - 1023;
    }

    int eff_precision;
    if (precision < 0) {
        /* Shortest exact: trim trailing zero nibbles. */
        eff_precision = 13;
        uint64_t tmp = mantissa;
        while (eff_precision > 0 && (tmp & 0xF) == 0) {
            tmp >>= 4;
            eff_precision--;
        }
    } else if (precision < 13) {
        int drop_bits = 52 - precision * 4;
        uint64_t round_bit = (uint64_t)1 << (drop_bits - 1);
        uint64_t drop_mask = ((uint64_t)1 << drop_bits) - 1;
        uint64_t low = mantissa & drop_mask;
        uint64_t kept = mantissa >> drop_bits;

        int round_up = 0;
        /* round-to-nearest-even: LSB of the would-be output value.  When
         * precision == 0 there are no kept fractional bits, so the LSB is
         * the leading hex digit itself. */
        int lsb = (precision == 0) ? (leading_digit & 1) : (int)(kept & 1);
        if (low > round_bit) round_up = 1;
        else if (low == round_bit && lsb) round_up = 1;

        if (round_up) {
            kept++;
            int field_width = precision * 4;
            if (field_width == 0 || (kept >> field_width) != 0) {
                leading_digit++;
                kept = 0;
            }
        }
        mantissa = kept << drop_bits;
        eff_precision = precision;
    } else {
        /* precision >= 13: emit all 13 mantissa digits, pad with zeros. */
        eff_precision = precision;
    }

    APUT('0');
    APUT(x_char);
    APUT(digits[leading_digit]);

    if (eff_precision > 0 || alternate_form) {
        APUT('.');
        for (int i = 0; i < eff_precision; i++) {
            int shift = 48 - i * 4;
            int d = (shift >= 0) ? (int)((mantissa >> shift) & 0xF) : 0;
            APUT(digits[d]);
        }
    }

    APUT(p_char);
    int e = exponent;
    if (e >= 0) APUT('+');
    else { APUT('-'); e = -e; }

    char ebuf[16];
    int ei = 0;
    if (e == 0) ebuf[ei++] = '0';
    else while (e > 0) { ebuf[ei++] = '0' + (e % 10); e /= 10; }
    while (ei > 0) APUT(ebuf[--ei]);

    if (rem > 0) *o = '\0';
#undef APUT
}

/* ===================================================================== *
 * Comprehensive conversion engine.
 *
 * Implements C99 + POSIX.1 printf: flags ('-', '+', ' ', '#', '0', and the
 * locale no-ops '\'' and 'I'); field width and precision, each a literal or
 * '*' (taken from an int argument); length modifiers hh h l ll j z t L;
 * conversions d i o u x X e E f F g G a A c s p n %; the glibc %m
 * (strerror(errno), no argument); and POSIX positional arguments — %n$ for
 * the conversion argument and *m$ for a width/precision argument.
 *
 * Positional and ordinary (sequential) directives must not be mixed in one
 * format (per POSIX it is undefined); when any '$' is present we switch to
 * the two-pass positional path, otherwise we consume va_arg sequentially.
 * ===================================================================== */

enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T,
       LEN_LONG_DOUBLE };

/* va_arg width class for one argument slot. */
enum vcat { VC_NONE, VC_INT, VC_LONG, VC_LLONG, VC_SIZE, VC_PTRDIFF,
            VC_INTMAX, VC_DBL, VC_LDBL, VC_PTR };

union argval { long long ll; long double ld; void *p; };

struct outbuf { char *s; size_t remaining; size_t len; };

static void ob_put(struct outbuf *o, char c) {
    if (o->remaining > 1 && o->s) { *o->s++ = c; o->remaining--; }
    o->len++;
}

struct spec {
    int left_align, force_sign, space_prefix, alternate_form, pad_zero;
    int width;        /* resolved, >= 0 */
    int precision;    /* resolved, -1 = none */
    int length;       /* LEN_* */
    char conv;
};

/* Which va_arg width does a (conversion, length) pair consume? */
static int conv_vcat(char conv, int length) {
    switch (conv) {
    case 'd': case 'i': case 'u': case 'o': case 'x': case 'X':
        switch (length) {
        case LEN_LL: return VC_LLONG;
        case LEN_L:  return VC_LONG;
        case LEN_J:  return VC_INTMAX;
        case LEN_Z:  return VC_SIZE;
        case LEN_T:  return VC_PTRDIFF;
        default:     return VC_INT;     /* int, short(h), char(hh) all promote to int */
        }
    case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
    case 'a': case 'A':
        return (length == LEN_LONG_DOUBLE) ? VC_LDBL : VC_DBL;
    case 'c': case 'C':                  /* int / wint_t (%c, %lc, %C) — int-sized */
        return VC_INT;
    case 's': case 'S': case 'p': case 'n':  /* %S == %ls: a (wide) string pointer */
        return VC_PTR;
    default:                             /* %m, %%, unknown: no argument */
        return VC_NONE;
    }
}

/* Read one argument of class vc from ap into a value union.  Integers are
 * widened keeping their signedness; the conversion later reinterprets the
 * bits per its own signedness and length. */
static union argval read_vcat(va_list *ap, int vc) {
    union argval a;
    a.ll = 0;
    switch (vc) {
    case VC_INT:     a.ll = (long long)va_arg(*ap, int);       break;
    case VC_LONG:    a.ll = (long long)va_arg(*ap, long);      break;
    case VC_LLONG:   a.ll = va_arg(*ap, long long);            break;
    case VC_SIZE:    a.ll = (long long)va_arg(*ap, long);      break;  /* signed read; %zu re-casts */
    case VC_PTRDIFF: a.ll = (long long)va_arg(*ap, ptrdiff_t); break;
    case VC_INTMAX:  a.ll = (long long)va_arg(*ap, intmax_t);  break;
    case VC_DBL:     a.ld = (long double)va_arg(*ap, double);  break;
    case VC_LDBL:    a.ld = va_arg(*ap, long double);          break;
    case VC_PTR:     a.p  = va_arg(*ap, void *);               break;
    default: break;
    }
    return a;
}

/* Emit one fully-parsed conversion with its argument already fetched. */
static void fmt_conv(struct outbuf *o, const struct spec *sp, union argval a) {
    int width = sp->width, precision = sp->precision;
    int left_align = sp->left_align, pad_zero = sp->pad_zero;
    int force_sign = sp->force_sign, space_prefix = sp->space_prefix;
    int alternate_form = sp->alternate_form;
    char conv = sp->conv;

    switch (conv) {
    case 'd':
    case 'i': {
        int64_t val;
        if (sp->length == LEN_H)       val = (short)a.ll;
        else if (sp->length == LEN_HH) val = (signed char)a.ll;
        else                           val = a.ll;

        if (precision != -1) pad_zero = 0;

        char digits[72];
        int is_negative = (val < 0);
        uint64_t uval = is_negative ? (uint64_t)-val : (uint64_t)val;
        if (uval == 0 && precision == 0) {
            digits[0] = '\0';
        } else {
            char b[72];
            int i = 0;
            if (uval == 0) b[i++] = '0';
            else while (uval > 0) { b[i++] = (uval % 10) + '0'; uval /= 10; }
            for (int j = 0; j < i; j++) digits[j] = b[i - j - 1];
            digits[i] = '\0';
        }

        int digits_len = strlen(digits);
        int precision_fill = (precision > digits_len) ? (precision - digits_len) : 0;
        const char *prefix = is_negative ? "-" : (force_sign ? "+" : (space_prefix ? " " : ""));
        int prefix_len = strlen(prefix);
        int total_len = prefix_len + precision_fill + digits_len;

        if (!left_align && pad_zero) {
            for (int i = 0; prefix[i]; i++) ob_put(o, prefix[i]);
            for (int i = 0; i < width - total_len; i++) ob_put(o, '0');
            for (int i = 0; i < precision_fill; i++) ob_put(o, '0');
            for (int i = 0; i < digits_len; i++) ob_put(o, digits[i]);
        } else if (!left_align) {
            for (int i = 0; i < width - total_len; i++) ob_put(o, ' ');
            for (int i = 0; prefix[i]; i++) ob_put(o, prefix[i]);
            for (int i = 0; i < precision_fill; i++) ob_put(o, '0');
            for (int i = 0; i < digits_len; i++) ob_put(o, digits[i]);
        } else {
            for (int i = 0; prefix[i]; i++) ob_put(o, prefix[i]);
            for (int i = 0; i < precision_fill; i++) ob_put(o, '0');
            for (int i = 0; i < digits_len; i++) ob_put(o, digits[i]);
            for (int i = 0; i < width - total_len; i++) ob_put(o, ' ');
        }
        break;
    }
    case 'u':
    case 'o':
    case 'x':
    case 'X': {
        uint64_t val;
        switch (sp->length) {
        case LEN_HH: val = (unsigned char)a.ll;  break;
        case LEN_H:  val = (unsigned short)a.ll; break;
        case LEN_L: case LEN_Z: case LEN_T: val = (uint64_t)(unsigned long)a.ll; break;
        case LEN_LL: case LEN_J: val = (uint64_t)a.ll; break;
        default:     val = (uint64_t)(unsigned int)a.ll; break;
        }

        if (precision != -1) pad_zero = 0;

        char digits[72];
        if (val == 0 && precision == 0) {
            digits[0] = '\0';
        } else {
            char b[72];
            int i = 0;
            int base = (conv == 'o') ? 8 : ((conv == 'u') ? 10 : 16);
            const char *symbols = (conv == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            uint64_t uval = val;
            if (uval == 0) b[i++] = '0';
            else while (uval > 0) { b[i++] = symbols[uval % base]; uval /= base; }
            for (int j = 0; j < i; j++) digits[j] = b[i - j - 1];
            digits[i] = '\0';
        }

        int digits_len = strlen(digits);
        int precision_fill = (precision > digits_len) ? (precision - digits_len) : 0;
        const char *prefix = "";
        if (conv == 'o' && alternate_form && (digits[0] != '0' || precision_fill == 0)) {
            /* '#o' guarantees a leading zero. */
            if (digits[0] != '0') prefix = "0";
        }
        else if ((conv == 'x' || conv == 'X') && alternate_form && val != 0)
            prefix = (conv == 'X') ? "0X" : "0x";
        int prefix_len = strlen(prefix);
        int total_len = prefix_len + precision_fill + digits_len;

        if (!left_align && pad_zero) {
            for (int i = 0; prefix[i]; i++) ob_put(o, prefix[i]);
            for (int i = 0; i < width - total_len; i++) ob_put(o, '0');
            for (int i = 0; i < precision_fill; i++) ob_put(o, '0');
            for (int i = 0; i < digits_len; i++) ob_put(o, digits[i]);
        } else if (left_align) {
            for (int i = 0; prefix[i]; i++) ob_put(o, prefix[i]);
            for (int i = 0; i < precision_fill; i++) ob_put(o, '0');
            for (int i = 0; i < digits_len; i++) ob_put(o, digits[i]);
            for (int i = 0; i < width - total_len; i++) ob_put(o, ' ');
        } else {
            for (int i = 0; i < width - total_len; i++) ob_put(o, ' ');
            for (int i = 0; prefix[i]; i++) ob_put(o, prefix[i]);
            for (int i = 0; i < precision_fill; i++) ob_put(o, '0');
            for (int i = 0; i < digits_len; i++) ob_put(o, digits[i]);
        }
        break;
    }
    case 'f': case 'F':
    case 'e': case 'E':
    case 'g': case 'G': {
        double val = (double)a.ld;
        char tmp[512];
        if (conv == 'f' || conv == 'F') ftoa(tmp, sizeof(tmp), val, precision, (conv == 'F'));
        else if (conv == 'e' || conv == 'E') etoa(tmp, sizeof(tmp), val, precision, (conv == 'E'));
        else gtoa(tmp, sizeof(tmp), val, precision, (conv == 'G'), alternate_form);
        /* The float helpers emit only a leading '-'.  Pull it out as the
         * sign and apply '+'/' ' for non-negatives, so the sign sits before
         * any '0' fill (e.g. "%+08.1f" of 3.5 -> "+00003.5"). */
        char signch = 0;
        char *body = tmp;
        if (tmp[0] == '-') { signch = '-'; body = tmp + 1; }
        else if (force_sign) signch = '+';
        else if (space_prefix) signch = ' ';
        int body_len = strlen(body);
        int total = body_len + (signch ? 1 : 0);

        if (!left_align && pad_zero && width > total) {
            if (signch) ob_put(o, signch);
            for (int i = 0; i < width - total; i++) ob_put(o, '0');
            for (int i = 0; i < body_len; i++) ob_put(o, body[i]);
        } else if (!left_align) {
            for (int i = 0; i < width - total; i++) ob_put(o, ' ');
            if (signch) ob_put(o, signch);
            for (int i = 0; i < body_len; i++) ob_put(o, body[i]);
        } else {
            if (signch) ob_put(o, signch);
            for (int i = 0; i < body_len; i++) ob_put(o, body[i]);
            for (int i = 0; i < width - total; i++) ob_put(o, ' ');
        }
        break;
    }
    case 'a': case 'A': {
        double val = (double)a.ld;
        char tmp[64];
        atoa(tmp, sizeof(tmp), val, precision, (conv == 'A'),
             alternate_form, force_sign, space_prefix);
        int tmp_len = strlen(tmp);
        if (!left_align) for (int i = 0; i < width - tmp_len; i++) ob_put(o, ' ');
        for (int i = 0; i < tmp_len; i++) ob_put(o, tmp[i]);
        if (left_align) for (int i = 0; i < width - tmp_len; i++) ob_put(o, ' ');
        break;
    }
    case 'p': {
        uintptr_t val = (uintptr_t)a.p;
        char tmp[32];
        utoa_hex(tmp, val, 0);
        int len_val = strlen(tmp);
        int total_len = 2 + (len_val < 8 ? 8 : len_val);
        if (width > total_len && !left_align) for (int i = 0; i < width - total_len; i++) ob_put(o, ' ');
        ob_put(o, '0'); ob_put(o, 'x');
        for (int i = 0; i < 8 - len_val; i++) ob_put(o, '0');
        for (int i = 0; i < len_val; i++) ob_put(o, tmp[i]);
        if (width > total_len && left_align) for (int i = 0; i < width - total_len; i++) ob_put(o, ' ');
        break;
    }
    case 's': {
        const char *val = (const char *)a.p;
        if (!val) val = "(null)";
        int s_len = 0;
        const char *p = val;
        while (*p && (precision == -1 || s_len < precision)) { s_len++; p++; }
        if (!left_align && width > s_len) for (int i = 0; i < width - s_len; i++) ob_put(o, ' ');
        for (int i = 0; i < s_len; i++) ob_put(o, val[i]);
        if (left_align && width > s_len) for (int i = 0; i < width - s_len; i++) ob_put(o, ' ');
        break;
    }
    case 'S': {
        /* %S == %ls: a wide string.  Substrate is a single-byte locale, so
         * emit the low byte of each wchar_t (4 bytes on i386) until NUL,
         * honouring precision (max chars) and width. */
        const unsigned int *ws = (const unsigned int *)a.p;
        if (!ws) { const char *n = "(null)"; while (*n) ob_put(o, *n++); break; }
        int s_len = 0;
        while (ws[s_len] && (precision == -1 || s_len < precision)) s_len++;
        if (!left_align && width > s_len) for (int i = 0; i < width - s_len; i++) ob_put(o, ' ');
        for (int i = 0; i < s_len; i++) ob_put(o, (char)ws[i]);
        if (left_align && width > s_len) for (int i = 0; i < width - s_len; i++) ob_put(o, ' ');
        break;
    }
    case 'm': {
        /* glibc extension: strerror(errno), no argument. */
        const char *val = strerror(errno);
        if (!val) val = "";
        int s_len = 0;
        const char *p = val;
        while (*p && (precision == -1 || s_len < precision)) { s_len++; p++; }
        if (!left_align && width > s_len) for (int i = 0; i < width - s_len; i++) ob_put(o, ' ');
        for (int i = 0; i < s_len; i++) ob_put(o, val[i]);
        if (left_align && width > s_len) for (int i = 0; i < width - s_len; i++) ob_put(o, ' ');
        break;
    }
    case 'c': case 'C': {       /* %C == %lc: low byte of the (wide) char */
        char c = (char)a.ll;
        if (!left_align && width > 1) for (int i = 0; i < width - 1; i++) ob_put(o, ' ');
        ob_put(o, c);
        if (left_align && width > 1) for (int i = 0; i < width - 1; i++) ob_put(o, ' ');
        break;
    }
    case 'n': {
        void *ptr = a.p;
        if (ptr) {
            if (sp->length == LEN_LL) *(long long *)ptr = o->len;
            else if (sp->length == LEN_L) *(long *)ptr = o->len;
            else if (sp->length == LEN_H) *(short *)ptr = (short)o->len;
            else if (sp->length == LEN_HH) *(signed char *)ptr = (signed char)o->len;
            else *(int *)ptr = o->len;
        }
        break;
    }
    case '%':
        ob_put(o, '%');
        break;
    default:
        ob_put(o, '%');
        if (conv) ob_put(o, conv);
        break;
    }
}

/* Parse "<digits>$" at *pp.  On success advance *pp past the '$' and return
 * the (1-based) index; otherwise leave *pp untouched and return 0. */
static int parse_dollar(const char **pp) {
    const char *p = *pp;
    if (*p < '1' || *p > '9') return 0;
    int n = 0;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
    if (*p == '$') { *pp = p + 1; return n; }
    return 0;
}

#define PA_MAX 64   /* positional argument slots (indices 1..PA_MAX) */

/*
 * Parse the body of one conversion directive (the bytes after '%').  Fills
 * *sp with everything except width/precision values that come from '*'
 * arguments; for those, *width_argpos / *prec_argpos report the source:
 *   0  -> not a '*' field (sp->width / sp->precision already final)
 *  -1  -> '*' with sequential argument
 *  >0  -> '*m$' positional argument index
 * *main_argpos gets the positional index of the conversion argument (0 if
 * sequential).  Returns a pointer just past the conversion character.
 */
static const char *parse_spec(const char *f, struct spec *sp,
                              int *main_argpos, int *width_argpos,
                              int *prec_argpos) {
    memset(sp, 0, sizeof(*sp));
    sp->precision = -1;
    *main_argpos = *width_argpos = *prec_argpos = 0;

    /* positional conversion index: %n$... */
    *main_argpos = parse_dollar(&f);

    /* flags (any order, including '0'; '\'' and 'I' are accepted no-ops) */
    for (;;) {
        switch (*f) {
        case '-': sp->left_align = 1; f++; continue;
        case '+': sp->force_sign = 1; f++; continue;
        case ' ': sp->space_prefix = 1; f++; continue;
        case '#': sp->alternate_form = 1; f++; continue;
        case '0': sp->pad_zero = 1; f++; continue;
        case '\'': case 'I': f++; continue;   /* locale grouping: no-op */
        }
        break;
    }
    if (sp->force_sign) sp->space_prefix = 0;

    /* width */
    if (*f == '*') {
        f++;
        int p = parse_dollar(&f);
        *width_argpos = p ? p : -1;
    } else {
        while (*f >= '0' && *f <= '9') {
            if (sp->width < 1000000) sp->width = sp->width * 10 + (*f - '0');
            f++;
        }
    }

    /* precision */
    if (*f == '.') {
        f++;
        if (*f == '*') {
            f++;
            int p = parse_dollar(&f);
            *prec_argpos = p ? p : -1;
            sp->precision = 0;   /* placeholder until resolved */
        } else {
            sp->precision = 0;
            while (*f >= '0' && *f <= '9') {
                if (sp->precision < 1000000) sp->precision = sp->precision * 10 + (*f - '0');
                f++;
            }
        }
    }

    /* length modifier */
    if (*f == 'h') { f++; if (*f == 'h') { sp->length = LEN_HH; f++; } else sp->length = LEN_H; }
    else if (*f == 'l') { f++; if (*f == 'l') { sp->length = LEN_LL; f++; } else sp->length = LEN_L; }
    else if (*f == 'j') { sp->length = LEN_J; f++; }
    else if (*f == 'z') { sp->length = LEN_Z; f++; }
    else if (*f == 't') { sp->length = LEN_T; f++; }
    else if (*f == 'L') { sp->length = LEN_LONG_DOUBLE; f++; }
    else if (*f == 'q') { sp->length = LEN_LL; f++; }   /* BSD quad */

    sp->conv = *f;
    if (*f) f++;
    return f;
}

/* Resolve a width/precision that came from a '*' argument. */
static int star_value(int argpos, const union argval *vals, int maxidx,
                      va_list *seq_ap) {
    if (argpos > 0) {                 /* positional *m$ */
        if (argpos <= maxidx) return (int)vals[argpos].ll;
        return 0;
    }
    return va_arg(*seq_ap, int);      /* sequential * */
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    struct outbuf o = { str, size, 0 };

    /* Detect positional usage anywhere in the format. */
    int positional = 0;
    for (const char *f = format; *f; ) {
        if (*f != '%') { f++; continue; }
        f++;
        if (*f == '%') { f++; continue; }
        const char *g = f;
        if (parse_dollar(&g)) { positional = 1; break; }
        /* also catch a bare *m$ width before a positional run */
        struct spec tsp; int ma, wa, pa;
        const char *e = parse_spec(f, &tsp, &ma, &wa, &pa);
        if (ma > 0 || wa > 0 || pa > 0) { positional = 1; break; }
        f = e;
    }

    /* Positional path: collect argument classes, read them by index, format. */
    union argval vals[PA_MAX + 1];
    int maxidx = 0;
    if (positional) {
        int cats[PA_MAX + 1];
        for (int i = 0; i <= PA_MAX; i++) cats[i] = VC_NONE;

        for (const char *f = format; *f; ) {
            if (*f != '%') { f++; continue; }
            f++;
            if (*f == '%') { f++; continue; }
            struct spec sp; int ma, wa, pa;
            f = parse_spec(f, &sp, &ma, &wa, &pa);
            if (wa > 0 && wa <= PA_MAX) { cats[wa] = VC_INT; if (wa > maxidx) maxidx = wa; }
            if (pa > 0 && pa <= PA_MAX) { cats[pa] = VC_INT; if (pa > maxidx) maxidx = pa; }
            if (ma > 0 && ma <= PA_MAX) {
                cats[ma] = conv_vcat(sp.conv, sp.length);
                if (ma > maxidx) maxidx = ma;
            }
        }
        if (maxidx > PA_MAX) maxidx = PA_MAX;
        for (int i = 1; i <= maxidx; i++) vals[i] = read_vcat(&ap, cats[i]);
    }

    for (const char *f = format; *f; ) {
        if (*f != '%') { ob_put(&o, *f); f++; continue; }
        f++;
        if (*f == '%') { ob_put(&o, '%'); f++; continue; }

        struct spec sp; int ma, wa, pa;
        f = parse_spec(f, &sp, &ma, &wa, &pa);

        /* Resolve '*' width and precision (order matters for sequential). */
        if (wa != 0) {
            int w = star_value(wa, vals, maxidx, &ap);
            if (w < 0) { sp.left_align = 1; w = -w; }
            sp.width = w;
        }
        if (pa != 0) {
            int p = star_value(pa, vals, maxidx, &ap);
            sp.precision = (p < 0) ? -1 : p;
        }
        if (sp.left_align) sp.pad_zero = 0;
        if (sp.precision != -1 && (sp.conv == 'd' || sp.conv == 'i' ||
            sp.conv == 'u' || sp.conv == 'o' || sp.conv == 'x' || sp.conv == 'X'))
            sp.pad_zero = 0;

        /* Fetch the conversion argument. */
        union argval a;
        a.ll = 0;
        int vc = conv_vcat(sp.conv, sp.length);
        if (vc != VC_NONE) {
            if (positional) {
                a = (ma > 0 && ma <= maxidx) ? vals[ma] : a;
            } else {
                a = read_vcat(&ap, vc);
            }
        }

        fmt_conv(&o, &sp, a);
    }

    if (size > 0 && str) {
        if (o.remaining > 0) *o.s = '\0';
        else str[size - 1] = '\0';
    }
    return (int)o.len;
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

/*
 * Internal helper: format to FILE with stack buffer + heap fallback.
 * This avoids a buffer overread when vsnprintf returns a length exceeding
 * the stack buffer size.
 */
static int __vfprintf_impl(FILE *stream, const char *format, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    if (ret >= 0) {
        if ((size_t)ret < sizeof(buf)) {
            fwrite(buf, 1, ret, stream);
        } else {
            char *big = malloc((size_t)ret + 1);
            if (big) {
                vsnprintf(big, (size_t)ret + 1, format, ap2);
                fwrite(big, 1, ret, stream);
                free(big);
            } else {
                fwrite(buf, 1, sizeof(buf) - 1, stream);
            }
        }
    }
    va_end(ap2);
    return ret;
}

static int __vdprintf_impl(int fd, const char *format, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    if (ret > 0) {
        if ((size_t)ret < sizeof(buf)) {
            write(fd, buf, ret);
        } else {
            char *big = malloc((size_t)ret + 1);
            if (big) {
                vsnprintf(big, (size_t)ret + 1, format, ap2);
                write(fd, big, ret);
                free(big);
            } else {
                write(fd, buf, sizeof(buf) - 1);
            }
        }
    }
    va_end(ap2);
    return ret;
}

int printf(const char *format, ...) {
    va_list ap; va_start(ap, format);
    int ret = __vfprintf_impl(stdout, format, ap);
    va_end(ap);
    return ret;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int ret = __vfprintf_impl(stream, format, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *format, va_list ap) {
    return __vfprintf_impl(stdout, format, ap);
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    return __vfprintf_impl(stream, format, ap);
}

int vdprintf(int fd, const char *format, va_list ap) {
    return __vdprintf_impl(fd, format, ap);
}

int dprintf(int fd, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int ret = __vdprintf_impl(fd, format, ap);
    va_end(ap);
    return ret;
}
