#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

// Mock functions
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

static void gtoa_orig(char *buf, double val, int precision, int uppercase, int alternate_form) {
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

static void gtoa_opt(char *buf, double val, int precision, int uppercase, int alternate_form) {
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

int main() {
    double vals[] = { 123.456000, 123000.0, 0.000123000, 1.234567e-10, 100.0, 1.0, 0.0, 1.23456e5 };
    char b1[128], b2[128];
    for (int i = 0; i < sizeof(vals)/sizeof(double); i++) {
        gtoa_orig(b1, vals[i], 6, 0, 0);
        gtoa_opt(b2, vals[i], 6, 0, 0);
        if (strcmp(b1, b2) != 0) {
            printf("Mismatch for %g: orig='%s', opt='%s'\n", vals[i], b1, b2);
        }
    }
    printf("Verification complete.\n");

    clock_t start = clock();
    for (int i = 0; i < 2000000; i++) {
        gtoa_orig(b1, 123.456000, 6, 0, 0);
        gtoa_orig(b1, 123000.0, 6, 0, 0);
        gtoa_orig(b1, 0.000123000, 6, 0, 0);
        gtoa_orig(b1, 1.234567e-10, 6, 0, 0);
    }
    clock_t end = clock();
    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Time orig: %f\n", cpu_time_used);

    start = clock();
    for (int i = 0; i < 2000000; i++) {
        gtoa_opt(b2, 123.456000, 6, 0, 0);
        gtoa_opt(b2, 123000.0, 6, 0, 0);
        gtoa_opt(b2, 0.000123000, 6, 0, 0);
        gtoa_opt(b2, 1.234567e-10, 6, 0, 0);
    }
    end = clock();
    double cpu_time_used2 = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Time opt: %f\n", cpu_time_used2);

    return 0;
}
