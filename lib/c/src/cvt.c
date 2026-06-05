/*
 * cvt.c — reentrant ecvt(3) / fcvt(3) and their long-double q-forms.
 *
 * These obsolete SVID conversions turn a floating value into a bare digit
 * string plus a decimal-point position and a sign flag.  The reentrant *_r
 * forms write the digits into a caller-supplied buffer instead of a static
 * one.  Both are implemented over snprintf():
 *
 *   ecvt_r  ndigit = total significant digits        ("%.*e")
 *   fcvt_r  ndigit = digits after the decimal point  ("%.*f")
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Pull the digit run and exponent out of a "d.ddde±xx" string produced by
 * "%.*e", writing the significant digits into buf and the decimal exponent
 * into *decpt (point sits after the first digit -> exponent + 1). */
static int
ecvt_common(long double value, int ndigit, int *decpt, int *sign,
            char *buf, size_t len)
{
    char  tmp[512];
    char *p, *out;
    int   exp;

    if (buf == NULL || len == 0)
        return -1;
    if (ndigit < 1)
        ndigit = 1;
    if ((size_t)ndigit > len - 1)
        ndigit = (int)len - 1;

    *sign = signbit(value) ? 1 : 0;
    value = fabsl(value);

    snprintf(tmp, sizeof tmp, "%.*Le", ndigit - 1, value);

    out = buf;
    p   = tmp;
    if (*p) *out++ = *p++;          /* leading digit */
    if (*p == '.') p++;             /* skip the point */
    while (*p && *p != 'e' && *p != 'E')
        *out++ = *p++;
    *out = '\0';

    exp = 0;
    if (*p == 'e' || *p == 'E')
        exp = (int)strtol(p + 1, NULL, 10);
    *decpt = exp + 1;
    return 0;
}

static int
fcvt_common(long double value, int ndigit, int *decpt, int *sign,
            char *buf, size_t len)
{
    char  tmp[640];
    char *dot, *out;
    int   intlen;

    if (buf == NULL || len == 0)
        return -1;
    if (ndigit < 0)
        ndigit = 0;

    *sign = signbit(value) ? 1 : 0;
    value = fabsl(value);

    snprintf(tmp, sizeof tmp, "%.*Lf", ndigit, value);

    dot = strchr(tmp, '.');
    intlen = dot ? (int)(dot - tmp) : (int)strlen(tmp);

    if (value == 0) {
        /* glibc keeps every digit for an exact zero: "0" + ndigit zeros,
         * decimal point after the integer "0" (decpt 1). */
        memcpy(buf, tmp, (size_t)intlen);
        out = buf + intlen;
        if (dot)
            for (char *p = dot + 1; *p; p++)
                *out++ = *p;
        *out = '\0';
        *decpt = intlen;
        if ((size_t)strlen(buf) > len - 1)
            buf[len - 1] = '\0';
        return 0;
    }

    out = buf;
    if (intlen == 1 && tmp[0] == '0') {
        /* 0 < value < 1: the integer "0" is not a significant digit. */
        *decpt = 0;
    } else {
        memcpy(out, tmp, (size_t)intlen);
        out += intlen;
        *decpt = intlen;
    }
    if (dot)
        for (char *p = dot + 1; *p; p++)
            *out++ = *p;
    *out = '\0';

    /* For 0 < value < 1, strip leading zeros, pulling the decimal point left
     * so e.g. 0.05 -> digits "5", decpt -1.  (value >= 1 has no leading zero.) */
    if (*decpt == 0) {
        char *d = buf;
        while (d[0] == '0' && d[1] != '\0') {
            memmove(d, d + 1, strlen(d));
            (*decpt)--;
        }
    }
    if (buf[0] == '\0') {
        buf[0] = '0';
        buf[1] = '\0';
        *decpt = 0;
    }
    if ((size_t)strlen(buf) > len - 1)
        buf[len - 1] = '\0';
    return 0;
}

int
ecvt_r(double value, int ndigit, int *decpt, int *sign, char *buf, size_t len)
{
    return ecvt_common(value, ndigit, decpt, sign, buf, len);
}

int
fcvt_r(double value, int ndigit, int *decpt, int *sign, char *buf, size_t len)
{
    return fcvt_common(value, ndigit, decpt, sign, buf, len);
}

int
qecvt_r(long double value, int ndigit, int *decpt, int *sign, char *buf, size_t len)
{
    return ecvt_common(value, ndigit, decpt, sign, buf, len);
}

int
qfcvt_r(long double value, int ndigit, int *decpt, int *sign, char *buf, size_t len)
{
    return fcvt_common(value, ndigit, decpt, sign, buf, len);
}
