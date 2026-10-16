/*
 * Decimal floating constants in BID, the encoding gcc uses on x86: see
 * cc_decimal.h.  Everything here is exact or rounds once, half to even,
 * as IEEE 754-2008 converts to a decimal format.
 */
#include <string.h>

#include "cc_decimal.h"

/** A decimal format's parameters: its coefficient digits, exponent range and exponent bias. */
typedef struct {
    int precision; /**< coefficient digits */
    long qmin;     /**< the least exponent (of the coefficient read as an integer) */
    long qmax;     /**< the greatest */
    long bias;     /**< what the encoded exponent field adds to the exponent */
} dec_format_t;

/** The parameters of the decimal format @p bits wide (32, 64 or 128). */
static dec_format_t dec_format(int bits) {
    dec_format_t f;

    if (bits == 32) {
        f.precision = 7;
        f.qmin = -101;
        f.qmax = 90;
        f.bias = 101;
    } else if (bits == 64) {
        f.precision = 16;
        f.qmin = -398;
        f.qmax = 369;
        f.bias = 398;
    } else {
        f.precision = 34;
        f.qmin = -6176;
        f.qmax = 6111;
        f.bias = 6176;
    }
    return f;
}

/** Drop the leading zeros of @p d's coefficient; a zero coefficient has no digits. */
static void dec_strip_leading(cc_dec_t *d) {
    int lead = 0;

    while (lead < d->ndigits && d->digits[lead] == 0) {
        ++lead;
    }
    if (lead > 0) {
        memmove(d->digits, d->digits + lead, (size_t)(d->ndigits - lead));
        d->ndigits -= lead;
    }
}

/**
 * Read the decimal floating constant spelled by @p s (@p len bytes, its
 * suffix may follow): digits with an optional point and exponent.  The
 * coefficient is the digits as written and the exponent places the point,
 * so `1.50` is 150 x 10^-2, as the constant is in C23 (and gcc).
 *
 * @return 0, or -1 if it is malformed or has too many digits
 */
int cc_dec_from_string(const char *s, size_t len, cc_dec_t *out) {
    size_t i = 0;
    long frac = 0;
    int saw_point = 0;
    long exp = 0;

    memset(out, 0, sizeof(*out));
    for (; i < len; ++i) {
        char c = s[i];

        if (c == '\'') {
            continue;
        }
        if (c == '.') {
            saw_point = 1;
            continue;
        }
        if (c < '0' || c > '9') {
            break;
        }
        if (out->ndigits >= CC_DEC_MAX_DIGITS) {
            return -1;
        }
        out->digits[out->ndigits++] = (char)(c - '0');
        if (saw_point) {
            ++frac;
        }
    }
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        int neg = 0;

        ++i;
        if (i < len && (s[i] == '+' || s[i] == '-')) {
            neg = s[i] == '-';
            ++i;
        }
        for (; i < len && s[i] >= '0' && s[i] <= '9'; ++i) {
            if (exp < 100000000L) {
                exp = exp * 10 + (s[i] - '0');
            }
        }
        if (neg) {
            exp = -exp;
        }
    }
    out->exponent = exp - frac;
    dec_strip_leading(out);
    return 0;
}

/** The decimal number @p magnitude, negated if @p negative, exactly (exponent 0). */
int cc_dec_from_int(unsigned long long magnitude, int negative, cc_dec_t *out) {
    char tmp[24];
    int n = 0;

    memset(out, 0, sizeof(*out));
    out->sign = negative != 0;
    while (magnitude > 0) {
        tmp[n++] = (char)(magnitude % 10);
        magnitude /= 10;
    }
    while (n > 0) {
        out->digits[out->ndigits++] = tmp[--n];
    }
    return 0;
}

/** A nonnegative integer in base 10^9, least significant limb first, for exact expansions. */
typedef struct {
    uint32_t limb[140];
    int n;
} dec_big_t;

/** @p b *= @p k (k < 2^32). */
static void dec_big_mul(dec_big_t *b, uint32_t k) {
    uint64_t carry = 0;
    int i;

    for (i = 0; i < b->n; ++i) {
        uint64_t v = (uint64_t)b->limb[i] * k + carry;

        b->limb[i] = (uint32_t)(v % 1000000000u);
        carry = v / 1000000000u;
    }
    while (carry > 0 && b->n < (int)(sizeof(b->limb) / sizeof(b->limb[0]))) {
        b->limb[b->n++] = (uint32_t)(carry % 1000000000u);
        carry /= 1000000000u;
    }
}

/** Write @p b's decimal digits to @p out, most significant first (no leading zeros). */
static void dec_big_digits(const dec_big_t *b, cc_dec_t *out) {
    int i;

    out->ndigits = 0;
    for (i = b->n - 1; i >= 0; --i) {
        uint32_t v = b->limb[i];
        char tmp[9];
        int k;

        for (k = 8; k >= 0; --k) {
            tmp[k] = (char)(v % 10);
            v /= 10;
        }
        for (k = 0; k < 9; ++k) {
            if (out->ndigits < CC_DEC_MAX_DIGITS) {
                out->digits[out->ndigits++] = tmp[k];
            }
        }
    }
    dec_strip_leading(out);
}

/**
 * The binary floating constant @p v as gcc converts one to a decimal type:
 * its exact decimal value with the trailing zeros dropped -- but keeping
 * a digit after the point, as gcc's printed form d.d...e+N has one -- so
 * 2.0 becomes 20 x 10^-1 and 0.1 its 55 exact digits, to be rounded to
 * the format's precision when it is encoded.
 *
 * @return 0 (an infinity for an infinite @p v)
 */
int cc_dec_from_binary(double v, cc_dec_t *out) {
    dec_big_t b;
    unsigned long long m;
    long e = 0;

    memset(out, 0, sizeof(*out));
    if (v < 0 || (v == 0 && 1 / v < 0)) {
        out->sign = 1;
        v = -v;
    }
    if (v == 0) {
        out->exponent = -1;
        return 0;
    }
    if (v > 1.7976931348623157e308) {
        out->is_inf = 1;
        return 0;
    }
    /* v = m x 2^e with m an integer below 2^53: halving and doubling are exact */
    while (v >= 9007199254740992.0) {
        v /= 2;
        ++e;
    }
    while (v != (double)(unsigned long long)v) {
        v *= 2;
        --e;
    }
    m = (unsigned long long)v;
    b.n = 0;
    while (m > 0) {
        b.limb[b.n++] = (uint32_t)(m % 1000000000u);
        m /= 1000000000u;
    }
    if (e >= 0) {
        for (; e > 0; --e) {
            dec_big_mul(&b, 2);
        }
        out->exponent = 0;
    } else {
        /* m x 2^e = m x 5^-e x 10^e */
        long k;

        for (k = 0; k < -e; ++k) {
            dec_big_mul(&b, 5);
        }
        out->exponent = e;
    }
    dec_big_digits(&b, out);
    while (out->ndigits > 1 && out->digits[out->ndigits - 1] == 0) {
        --out->ndigits;
        ++out->exponent;
    }
    if (out->ndigits == 1) {
        out->digits[out->ndigits++] = 0;
        --out->exponent;
    }
    return 0;
}

/**
 * Round @p d's coefficient to its first @p keep digits (0 or more), half
 * to even, raising the exponent to match; a carry out of the top digit
 * (9.99 to 10.0) adds a digit, which the caller deals with.
 */
static void dec_round_to(cc_dec_t *d, int keep) {
    int drop = d->ndigits - keep;
    int up = 0;
    int i;

    if (drop <= 0) {
        return;
    }
    if (keep < 0) {
        /* everything is dropped and it is below half a unit of the kept place */
        d->ndigits = 0;
        d->exponent += drop;
        return;
    }
    {
        int first = d->digits[keep];
        int rest = 0;

        for (i = keep + 1; i < d->ndigits; ++i) {
            rest |= d->digits[i];
        }
        if (first > 5 || (first == 5 && rest != 0)) {
            up = 1;
        } else if (first == 5) {
            up = keep > 0 ? (d->digits[keep - 1] & 1) : 0;
        }
    }
    d->ndigits = keep;
    d->exponent += drop;
    if (up) {
        for (i = keep - 1; i >= 0; --i) {
            if (d->digits[i] < 9) {
                d->digits[i]++;
                break;
            }
            d->digits[i] = 0;
        }
        if (i < 0) {
            memmove(d->digits + 1, d->digits, (size_t)d->ndigits);
            d->digits[0] = 1;
            d->ndigits++;
        }
    }
    dec_strip_leading(d);
}

/**
 * Decode @p bid, a BID encoding @p bits wide (as cc_dec_encode_bid() lays
 * it out), into @p out: its sign, coefficient and exponent.  A NaN reads
 * as an infinity; a non-canonical coefficient as zero.
 *
 * @return 0
 */
int cc_dec_from_bid(int bits, const uint64_t bid[2], cc_dec_t *out) {
    dec_format_t f = dec_format(bits);
    uint64_t hi = 0;
    uint64_t lo;
    long e;
    char tmp[40];
    int n = 0;

    memset(out, 0, sizeof(*out));
    if (bits == 32) {
        uint64_t w = bid[0] & 0xffffffffULL;

        out->sign = (int)(w >> 31);
        if ((w & 0x78000000ULL) == 0x78000000ULL) {
            out->is_inf = 1;
            return 0;
        }
        if ((w & 0x60000000ULL) == 0x60000000ULL) {
            e = (long)((w >> 21) & 0xff);
            lo = (w & 0x1fffffULL) | 0x800000ULL;
        } else {
            e = (long)((w >> 23) & 0xff);
            lo = w & 0x7fffffULL;
        }
        if (lo > 9999999ULL) {
            lo = 0;
        }
    } else if (bits == 64) {
        uint64_t w = bid[0];

        out->sign = (int)(w >> 63);
        if ((w & 0x7800000000000000ULL) == 0x7800000000000000ULL) {
            out->is_inf = 1;
            return 0;
        }
        if ((w & 0x6000000000000000ULL) == 0x6000000000000000ULL) {
            e = (long)((w >> 51) & 0x3ff);
            lo = (w & ((1ULL << 51) - 1)) | (1ULL << 53);
        } else {
            e = (long)((w >> 53) & 0x3ff);
            lo = w & ((1ULL << 53) - 1);
        }
        if (lo > 9999999999999999ULL) {
            lo = 0;
        }
    } else {
        out->sign = (int)(bid[1] >> 63);
        if ((bid[1] & 0x7800000000000000ULL) == 0x7800000000000000ULL) {
            out->is_inf = 1;
            return 0;
        }
        e = (long)((bid[1] >> 49) & 0x3fff);
        hi = bid[1] & ((1ULL << 49) - 1);
        lo = bid[0];
    }
    out->exponent = e - f.bias;
    /* the coefficient's digits, least significant first: hi:lo divided by 10 a 32-bit chunk at a time */
    while (hi != 0 || lo != 0) {
        uint64_t parts[4] = {hi >> 32, hi & 0xffffffffULL, lo >> 32, lo & 0xffffffffULL};
        uint64_t rem = 0;
        int k;

        for (k = 0; k < 4; ++k) {
            uint64_t cur = (rem << 32) | parts[k];

            parts[k] = cur / 10;
            rem = cur % 10;
        }
        hi = (parts[0] << 32) | parts[1];
        lo = (parts[2] << 32) | parts[3];
        tmp[n++] = (char)rem;
    }
    while (n > 0) {
        out->digits[out->ndigits++] = tmp[--n];
    }
    return 0;
}

/** @p hi:@p lo = @p hi:@p lo x 10 + @p digit, in 128 bits. */
static void dec_mul10_add(uint64_t *hi, uint64_t *lo, int digit) {
    uint64_t l = *lo;
    uint64_t h = *hi;
    /* x10 as x8 + x2 */
    uint64_t l8 = l << 3;
    uint64_t h8 = (h << 3) | (l >> 61);
    uint64_t l2 = l << 1;
    uint64_t h2 = (h << 1) | (l >> 63);
    uint64_t nl = l8 + l2;
    uint64_t nh = h8 + h2 + (nl < l8);
    uint64_t sl = nl + (uint64_t)digit;

    nh += sl < nl;
    *lo = sl;
    *hi = nh;
}

/**
 * Encode @p d in the BID format @p bits wide (32, 64 or 128) into @p out
 * (out[0] the low 64 bits, out[1] the high ones for 128; for 32 and 64 the
 * value is in out[0]).  The coefficient is rounded to the format's
 * precision, an exponent too large is absorbed by trailing zeros where the
 * precision allows (clamping) or gives an infinity, one too small rounds
 * the coefficient away digit by digit.
 */
void cc_dec_encode_bid(const cc_dec_t *in, int bits, uint64_t out[2]) {
    dec_format_t f = dec_format(bits);
    cc_dec_t d = *in;
    uint64_t chi = 0;
    uint64_t clo = 0;
    uint64_t e;
    int i;

    out[0] = 0;
    out[1] = 0;
    if (!d.is_inf) {
        if (d.ndigits > f.precision) {
            dec_round_to(&d, f.precision);
            if (d.ndigits > f.precision) {
                dec_round_to(&d, f.precision);
            }
        }
        if (d.exponent < f.qmin) {
            dec_round_to(&d, d.ndigits - (int)(f.qmin - d.exponent));
            if (d.exponent < f.qmin) {
                d.exponent = f.qmin;
            }
        }
        if (d.ndigits == 0 && d.exponent > f.qmax) {
            d.exponent = f.qmax;
        }
        if (d.exponent > f.qmax) {
            long pad = d.exponent - f.qmax;

            if (d.ndigits + pad <= f.precision) {
                for (; pad > 0; --pad) {
                    d.digits[d.ndigits++] = 0;
                    d.exponent--;
                }
            } else {
                d.is_inf = 1;
            }
        }
    }
    if (d.is_inf) {
        uint64_t inf = 0x7800000000000000ULL;

        if (bits == 32) {
            out[0] = 0x78000000ULL | (d.sign ? 0x80000000ULL : 0);
        } else if (bits == 64) {
            out[0] = inf | (d.sign ? 0x8000000000000000ULL : 0);
        } else {
            out[1] = inf | (d.sign ? 0x8000000000000000ULL : 0);
        }
        return;
    }
    for (i = 0; i < d.ndigits; ++i) {
        dec_mul10_add(&chi, &clo, d.digits[i]);
    }
    e = (uint64_t)(d.exponent + f.bias);
    if (bits == 32) {
        if (clo < (1ULL << 23)) {
            out[0] = (e << 23) | clo;
        } else {
            out[0] = (3ULL << 29) | (e << 21) | (clo & ((1ULL << 21) - 1));
        }
        out[0] |= d.sign ? 0x80000000ULL : 0;
    } else if (bits == 64) {
        if (clo < (1ULL << 53)) {
            out[0] = (e << 53) | clo;
        } else {
            out[0] = (3ULL << 61) | (e << 51) | (clo & ((1ULL << 51) - 1));
        }
        out[0] |= d.sign ? 0x8000000000000000ULL : 0;
    } else {
        /* 34 digits fit 113 bits: always the first form */
        out[0] = clo;
        out[1] = (e << 49) | chi | (d.sign ? 0x8000000000000000ULL : 0);
    }
}
