#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

// Mock functions
size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t src_len = strlen(src);
    if (size > 0) {
        size_t copy_len = (src_len >= size) ? size - 1 : src_len;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

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
    if (is_negative) { if (size > 0 && j < size - 1) buf[j++] = '-'; }
    else if (force_sign) { if (size > 0 && j < size - 1) buf[j++] = '+'; }
    else if (space_prefix) { if (size > 0 && j < size - 1) buf[j++] = ' '; }

    for (int k = 0; k < i; k++) {
        if (size > 0 && j < size - 1) buf[j++] = tmp[i - k - 1];
    }
    if (size > 0) buf[j < size ? j : size - 1] = '\0';
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
    size_t len = size > 0 ? strlen(buf) : 0;
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
        char *dot = strchr(buf, '.');
        if (dot) {
            char *e = strchr(dot, 'e');
            if (!e) e = strchr(dot, 'E');

            char *end = e ? e : dot + strlen(dot);
            char *p = end - 1;
            while (p > dot && *p == '0') {
                p--;
            }
            if (*p == '.') {
                p--;
            }

            p++; // points to the first character to replace

            if (e) {
                // e might be something like "e-10"
                // p is where we want to place it
                if (p != e) {
                    while (*e) {
                        *p++ = *e++;
                    }
                    *p = '\0';
                }
            } else {
                *p = '\0';
            }
        }
    }
}

int main() {
    char buf[128];
    clock_t start = clock();
    for (int i = 0; i < 1000000; i++) {
        gtoa(buf, sizeof(buf), 123.456000, 6, 0, 0);
        gtoa(buf, sizeof(buf), 123000.0, 6, 0, 0);
        gtoa(buf, sizeof(buf), 0.000123000, 6, 0, 0);
        gtoa(buf, sizeof(buf), 1.234567e-10, 6, 0, 0);
    }
    clock_t end = clock();
    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Time opt: %f\n", cpu_time_used);
    return 0;
}
