/*
 * IEEE 754 binary128 constants: see cc_binary128.h.  A value reaches the
 * encoder as an exact product q x 2^b of a big integer q and a power of
 * two, with a sticky bit for what an inexact division left; the encoder
 * rounds it once, to nearest with ties to even, to 113 significant bits,
 * or fewer for a subnormal, and packs it.
 */
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "cc_binary128.h"
#include "cc_decimal.h"

#define B128_BIAS 16383
#define B128_EMAX 16383
#define B128_PREC 113
/** The exponent of the least subnormal's one bit: 2^(1 - bias - 112). */
#define B128_LSB_MIN (-16494)

/** A non-negative big integer: 32-bit limbs, least significant first. */
typedef struct {
    uint32_t *w; /**< the limbs */
    size_t n;    /**< limbs in use; no zero limb above the top one */
    size_t cap;  /**< limbs allocated */
} big_t;

/**
 * Make room for @p n limbs in @p b.
 *
 * @return 0, or -1 when out of memory
 */
static int big_reserve(big_t *b, size_t n) {
    uint32_t *w;

    if (n <= b->cap) {
        return 0;
    }
    w = (uint32_t *)realloc(b->w, n * sizeof(*w));
    if (w == NULL) {
        return -1;
    }
    memset(w + b->cap, 0, (n - b->cap) * sizeof(*w));
    b->w = w;
    b->cap = n;
    return 0;
}

/** Drop zero limbs from the top of @p b. */
static void big_trim(big_t *b) {
    while (b->n > 0 && b->w[b->n - 1] == 0) {
        --b->n;
    }
}

/**
 * Set @p b to a 64-bit value.
 *
 * @return 0, or -1 when out of memory
 */
static int big_set_u64(big_t *b, uint64_t v) {
    if (big_reserve(b, 2) != 0) {
        return -1;
    }
    b->w[0] = (uint32_t)v;
    b->w[1] = (uint32_t)(v >> 32);
    b->n = 2;
    big_trim(b);
    return 0;
}

/**
 * @p b = @p b x @p m + @p a.
 *
 * @return 0, or -1 when out of memory
 */
static int big_mul_add(big_t *b, uint32_t m, uint32_t a) {
    uint64_t carry = a;
    size_t i;

    for (i = 0; i < b->n; ++i) {
        uint64_t t = (uint64_t)b->w[i] * m + carry;

        b->w[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry != 0) {
        if (big_reserve(b, b->n + 1) != 0) {
            return -1;
        }
        b->w[b->n++] = (uint32_t)carry;
    }
    return 0;
}

/** The number of significant bits of @p b; 0 for zero. */
static long big_bits(const big_t *b) {
    uint32_t top;
    long bits;

    if (b->n == 0) {
        return 0;
    }
    top = b->w[b->n - 1];
    bits = (long)(b->n - 1) * 32;
    while (top != 0) {
        ++bits;
        top >>= 1;
    }
    return bits;
}

/** Bit @p k of @p b. */
static int big_bit(const big_t *b, long k) {
    size_t i = (size_t)(k / 32);

    return k >= 0 && i < b->n ? (int)((b->w[i] >> (k % 32)) & 1u) : 0;
}

/** Whether any bit of @p b below bit @p k is set. */
static int big_any_below(const big_t *b, long k) {
    long i;

    for (i = 0; i < k && i < (long)b->n * 32; ++i) {
        if (big_bit(b, i)) {
            return 1;
        }
    }
    return 0;
}

/**
 * @p b <<= @p k.
 *
 * @return 0, or -1 when out of memory
 */
static int big_shl(big_t *b, long k) {
    size_t limbs = (size_t)(k / 32);
    int bits = (int)(k % 32);
    size_t i;

    if (b->n == 0 || k <= 0) {
        return 0;
    }
    if (big_reserve(b, b->n + limbs + 1) != 0) {
        return -1;
    }
    b->w[b->n + limbs] = 0;
    for (i = b->n; i-- > 0;) {
        uint32_t v = b->w[i];

        if (bits != 0) {
            b->w[i + limbs + 1] |= v >> (32 - bits);
        }
        b->w[i + limbs] = bits != 0 ? v << bits : v;
    }
    memset(b->w, 0, limbs * sizeof(*b->w));
    b->n += limbs + 1;
    big_trim(b);
    return 0;
}

/** @p b >>= @p k. */
static void big_shr(big_t *b, long k) {
    size_t limbs = (size_t)(k / 32);
    int bits = (int)(k % 32);
    size_t i;

    if (k <= 0) {
        return;
    }
    if (limbs >= b->n) {
        b->n = 0;
        return;
    }
    for (i = 0; i + limbs < b->n; ++i) {
        uint32_t lo = b->w[i + limbs] >> bits;
        uint32_t hi = bits != 0 && i + limbs + 1 < b->n ? b->w[i + limbs + 1] << (32 - bits) : 0;

        b->w[i] = lo | hi;
    }
    b->n -= limbs;
    big_trim(b);
}

/** Compare @p a with @p b: -1, 0 or 1. */
static int big_cmp(const big_t *a, const big_t *b) {
    size_t i;

    if (a->n != b->n) {
        return a->n < b->n ? -1 : 1;
    }
    for (i = a->n; i-- > 0;) {
        if (a->w[i] != b->w[i]) {
            return a->w[i] < b->w[i] ? -1 : 1;
        }
    }
    return 0;
}

/** @p a -= @p b, where @p a >= @p b. */
static void big_sub(big_t *a, const big_t *b) {
    uint64_t borrow = 0;
    size_t i;

    for (i = 0; i < a->n; ++i) {
        uint64_t s = (uint64_t)(i < b->n ? b->w[i] : 0) + borrow;

        borrow = a->w[i] < s ? 1 : 0;
        a->w[i] = (uint32_t)((uint64_t)a->w[i] - s);
    }
    big_trim(a);
}

/**
 * @p q = floor(@p num / @p den), by binary long division; @p num becomes
 * the remainder.
 *
 * @return 0, or -1 when out of memory
 */
static int big_divmod(big_t *num, const big_t *den, big_t *q) {
    long shift = big_bits(num) - big_bits(den);
    big_t d = {NULL, 0, 0};
    long k;

    q->n = 0;
    if (shift < 0) {
        return 0;
    }
    if (big_reserve(&d, den->n + (size_t)shift / 32 + 2) != 0 ||
        big_reserve(q, (size_t)shift / 32 + 2) != 0) {
        free(d.w);
        return -1;
    }
    memcpy(d.w, den->w, den->n * sizeof(*d.w));
    d.n = den->n;
    if (big_shl(&d, shift) != 0) {
        free(d.w);
        return -1;
    }
    q->n = (size_t)shift / 32 + 1;
    memset(q->w, 0, q->n * sizeof(*q->w));
    for (k = shift; k >= 0; --k) {
        if (big_cmp(num, &d) >= 0) {
            big_sub(num, &d);
            q->w[k / 32] |= 1u << (k % 32);
        }
        big_shr(&d, 1);
    }
    big_trim(q);
    free(d.w);
    return 0;
}

/**
 * Round (-1)^sign x (q + sticky) x 2^b to binary128 and pack it.  @p q is
 * consumed.  A sticky bit stands for a nonzero part below q's last bit.
 *
 * @param q       the integer, overwritten
 * @param b       the power of two it is scaled by
 * @param sticky  nonzero if the value is a little more than q x 2^b
 * @param sign    nonzero for a negative value
 * @param out     receives the encoding
 * @return        0, or -1 when out of memory
 */
static int b128_pack(big_t *q, long b, int sticky, int sign, uint64_t out[2]) {
    long bits = big_bits(q);
    long e2;
    long lsb;
    long sh;
    uint64_t lo;
    uint64_t hi;
    long biased;

    out[0] = 0;
    out[1] = sign ? 0x8000000000000000ULL : 0;
    if (bits == 0) {
        return 0;
    }
    e2 = bits - 1 + b;
    lsb = e2 - (B128_PREC - 1);
    if (lsb < B128_LSB_MIN) {
        lsb = B128_LSB_MIN;
    }
    sh = lsb - b;
    if (sh > 0) {
        int half = big_bit(q, sh - 1);
        int rest = sticky || big_any_below(q, sh - 1);

        big_shr(q, sh);
        if (half && (rest || big_bit(q, 0))) {
            if (big_mul_add(q, 1, 1) != 0) {
                return -1;
            }
        }
    } else if (big_shl(q, -sh) != 0) {
        return -1;
    }
    if (big_bits(q) > B128_PREC) {
        /* the rounding carried into a new bit */
        big_shr(q, 1);
        ++lsb;
    }
    bits = big_bits(q);
    if (bits == 0) {
        return 0;
    }
    lo =(uint64_t)(q->n > 0 ? q->w[0] : 0) | (uint64_t)(q->n > 1 ? q->w[1] : 0) << 32;
    hi = (uint64_t)(q->n > 2 ? q->w[2] : 0) | (uint64_t)(q->n > 3 ? q->w[3] : 0) << 32;
    if (bits == B128_PREC) {
        biased = lsb + (B128_PREC - 1) + B128_BIAS;
        if (biased > 2 * B128_EMAX) {
            /* too large: an infinity */
            out[1] |= 0x7fff000000000000ULL;
            return 0;
        }
        hi &= 0x0000ffffffffffffULL;
    } else {
        /* a subnormal: below the least normal exponent, the exponent field 0 */
        biased = 0;
    }
    out[0] = lo;
    out[1] |= (uint64_t)biased << 48 | hi;
    return 0;
}

void cc_b128_from_int(unsigned long long magnitude, int negative, uint64_t out[2]) {
    big_t q = {NULL, 0, 0};

    if (big_set_u64(&q, magnitude) != 0 || b128_pack(&q, 0, 0, negative != 0, out) != 0) {
        out[0] = 0;
        out[1] = 0;
    }
    free(q.w);
}

void cc_b128_from_double(double v, uint64_t out[2]) {
    uint64_t bits;
    int sign;
    int exp;
    uint64_t frac;
    big_t q = {NULL, 0, 0};

    memcpy(&bits, &v, sizeof(bits));
    sign = (int)(bits >> 63);
    exp = (int)((bits >> 52) & 0x7ff);
    frac = bits & 0x000fffffffffffffULL;
    if (exp == 0x7ff) {
        /* an infinity or a NaN: the fraction's bits at the top of binary128's */
        out[0] = 0;
        out[1] = (uint64_t)sign << 63 | 0x7fff000000000000ULL | frac >> 4;
        out[0] = frac << 60;
        return;
    }
    if (big_set_u64(&q, exp == 0 ? frac : frac | 1ULL << 52) != 0 ||
        b128_pack(&q, (exp == 0 ? 1 : exp) - 1023 - 52, 0, sign, out) != 0) {
        out[0] = 0;
        out[1] = 0;
    }
    free(q.w);
}

void cc_b128_from_ld(long double v, uint64_t out[2]) {
#if LDBL_MANT_DIG == 64
    unsigned char raw[sizeof(long double)];
    uint64_t mant;
    int sexp;
    int exp;
    big_t q = {NULL, 0, 0};

    /* the x87 format: a 64-bit significand with its integer bit, then sign and a 15-bit exponent */
    memcpy(raw, &v, sizeof(raw));
    memcpy(&mant, raw, sizeof(mant));
    sexp = raw[8] | raw[9] << 8;
    exp = sexp & 0x7fff;
    if (exp == 0x7fff) {
        out[1] = (uint64_t)(sexp >> 15) << 63 | 0x7fff000000000000ULL | (mant & 0x7fffffffffffffffULL) >> 15;
        out[0] = mant << 49;
        return;
    }
    if (big_set_u64(&q, mant) != 0 || b128_pack(&q, (exp == 0 ? 1 : exp) - B128_BIAS - 63, 0, sexp >> 15, out) != 0) {
        out[0] = 0;
        out[1] = 0;
    }
    free(q.w);
#else
    cc_b128_from_double((double)v, out);
#endif
}

/** The value of hexadecimal digit @p c, or -1. */
static int b128_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * Encode a hexadecimal floating constant's text (after `0x`): its digits
 * are the value's bits exactly, the binary exponent scaling them.
 *
 * @return 0, or -1 on malformed text or out of memory
 */
static int b128_from_hex(const char *s, size_t len, uint64_t out[2]) {
    big_t q = {NULL, 0, 0};
    long frac_digits = 0;
    int saw_point = 0;
    long exp = 0;
    int neg = 0;
    size_t i = 0;
    int rc;

    for (; i < len; ++i) {
        int d = b128_hex_digit(s[i]);

        if (s[i] == '\'') {
            continue;
        }
        if (s[i] == '.') {
            saw_point = 1;
            continue;
        }
        if (d < 0) {
            break;
        }
        if (big_mul_add(&q, 16, (uint32_t)d) != 0) {
            free(q.w);
            return -1;
        }
        if (saw_point) {
            ++frac_digits;
        }
    }
    if (i < len && (s[i] == 'p' || s[i] == 'P')) {
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
    }
    if (neg) {
        exp = -exp;
    }
    big_trim(&q);
    if (big_bits(&q) > 0 && exp - 4 * frac_digits + big_bits(&q) > B128_EMAX + 2) {
        out[0] = 0;
        out[1] = 0x7fff000000000000ULL;
        free(q.w);
        return 0;
    }
    rc = b128_pack(&q, exp - 4 * frac_digits, 0, 0, out);
    free(q.w);
    return rc;
}

/**
 * @p r = @p a x @p b, schoolbook.
 *
 * @return 0, or -1 when out of memory
 */
static int big_mul(const big_t *a, const big_t *b, big_t *r) {
    size_t i;
    size_t j;

    if (big_reserve(r, a->n + b->n + 1) != 0) {
        return -1;
    }
    memset(r->w, 0, (a->n + b->n + 1) * sizeof(*r->w));
    for (i = 0; i < a->n; ++i) {
        uint64_t carry = 0;

        for (j = 0; j < b->n; ++j) {
            uint64_t t = (uint64_t)a->w[i] * b->w[j] + r->w[i + j] + carry;

            r->w[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        r->w[i + b->n] = (uint32_t)carry;
    }
    r->n = a->n + b->n;
    big_trim(r);
    return 0;
}

/** What a binary128 encoding holds, for arithmetic on it. */
typedef enum { B128_FINITE, B128_INF, B128_NAN } b128_class_t;

/**
 * Decode @p v into its class and sign and, for a finite value, the integer
 * @p q and the power of two @p b that it is q x 2^b.
 *
 * @return its class; B128_NAN when out of memory too
 */
static b128_class_t b128_decode(const uint64_t v[2], int *sign, big_t *q, long *b) {
    int biased = (int)((v[1] >> 48) & 0x7fff);
    uint64_t hi = v[1] & 0x0000ffffffffffffULL;

    *sign = (int)(v[1] >> 63);
    if (biased == 0x7fff) {
        return hi != 0 || v[0] != 0 ? B128_NAN : B128_INF;
    }
    if (big_reserve(q, 4) != 0) {
        return B128_NAN;
    }
    if (biased != 0) {
        hi |= 0x0001000000000000ULL;
    }
    q->w[0] = (uint32_t)v[0];
    q->w[1] = (uint32_t)(v[0] >> 32);
    q->w[2] = (uint32_t)hi;
    q->w[3] = (uint32_t)(hi >> 32);
    q->n = 4;
    big_trim(q);
    *b = (biased == 0 ? 1 : biased) - B128_BIAS - (B128_PREC - 1);
    return B128_FINITE;
}

int cc_b128_arith(int op, const uint64_t a[2], const uint64_t b[2], uint64_t out[2]) {
    static const uint64_t qnan[2] = {0, 0x7fff800000000000ULL};
    big_t qa = {NULL, 0, 0};
    big_t qb = {NULL, 0, 0};
    big_t r = {NULL, 0, 0};
    long ea = 0;
    long eb = 0;
    int sa;
    int sb;
    b128_class_t ca;
    b128_class_t cb;
    int rc = 0;

    if (op != '+' && op != '-' && op != '*' && op != '/') {
        return -1;
    }
    ca = b128_decode(a, &sa, &qa, &ea);
    cb = b128_decode(b, &sb, &qb, &eb);
    if (op == '-') {
        sb = !sb;
    }
    memcpy(out, qnan, sizeof(qnan));
    if (ca == B128_NAN || cb == B128_NAN) {
        /* a NaN operand: the default quiet NaN */
    } else if (op == '+' || op == '-') {
        if (ca == B128_INF || cb == B128_INF) {
            if (!(ca == B128_INF && cb == B128_INF && sa != sb)) {
                out[0] = 0;
                out[1] = (uint64_t)(ca == B128_INF ? sa : sb) << 63 | 0x7fff000000000000ULL;
            }
        } else {
            /* both aligned to the smaller exponent: an exact sum, rounded once */
            long e = ea < eb ? ea : eb;
            int sign;

            if (qa.n == 0) {
                ea = e;
            }
            if (qb.n == 0) {
                eb = e;
            }
            e = ea < eb ? ea : eb;
            rc = big_shl(&qa, ea - e) != 0 || big_shl(&qb, eb - e) != 0 ? -1 : 0;
            if (rc == 0 && sa == sb) {
                size_t i;
                uint64_t carry = 0;

                rc = big_reserve(&qa, (qa.n > qb.n ? qa.n : qb.n) + 1);
                for (i = 0; rc == 0 && i < qb.n; ++i) {
                    uint64_t t = (uint64_t)(i < qa.n ? qa.w[i] : 0) + qb.w[i] + carry;

                    qa.w[i] = (uint32_t)t;
                    carry = t >> 32;
                }
                if (rc == 0) {
                    if (qb.n > qa.n) {
                        qa.n = qb.n;
                    }
                    for (; carry != 0; ++i) {
                        uint64_t t = (uint64_t)(i < qa.n ? qa.w[i] : 0) + carry;

                        qa.w[i] = (uint32_t)t;
                        carry = t >> 32;
                        if (i >= qa.n) {
                            qa.n = i + 1;
                        }
                    }
                }
                sign = sa;
            } else if (rc == 0) {
                int c = big_cmp(&qa, &qb);

                if (c >= 0) {
                    big_sub(&qa, &qb);
                    sign = sa;
                } else {
                    big_t t = qa;

                    big_sub(&qb, &qa);
                    qa = qb;
                    qb = t;
                    sign = sb;
                }
                if (c == 0) {
                    /* an exact zero is +0 when rounding to nearest */
                    sign = 0;
                }
            } else {
                sign = 0;
            }
            if (rc == 0) {
                if (qa.n == 0 && sa == sb) {
                    /* -0 + -0 */
                    sign = sa;
                }
                rc = b128_pack(&qa, e, 0, sign, out);
            }
        }
    } else if (op == '*') {
        int zero_a = ca == B128_FINITE && qa.n == 0;
        int zero_b = cb == B128_FINITE && qb.n == 0;

        if (ca == B128_INF || cb == B128_INF) {
            if (!zero_a && !zero_b) {
                out[0] = 0;
                out[1] = (uint64_t)(sa ^ sb) << 63 | 0x7fff000000000000ULL;
            }
        } else if ((rc = big_mul(&qa, &qb, &r)) == 0) {
            rc = b128_pack(&r, ea + eb, 0, sa ^ sb, out);
        }
    } else {
        int zero_a = ca == B128_FINITE && qa.n == 0;
        int zero_b = cb == B128_FINITE && qb.n == 0;

        if (ca == B128_INF) {
            if (cb != B128_INF) {
                out[0] = 0;
                out[1] = (uint64_t)(sa ^ sb) << 63 | 0x7fff000000000000ULL;
            }
        } else if (cb == B128_INF) {
            out[0] = 0;
            out[1] = (uint64_t)(sa ^ sb) << 63;
        } else if (zero_b) {
            if (!zero_a) {
                out[0] = 0;
                out[1] = (uint64_t)(sa ^ sb) << 63 | 0x7fff000000000000ULL;
            }
        } else {
            /* a quotient of at least 116 bits, its remainder the sticky bit */
            long s = big_bits(&qb) - big_bits(&qa) + B128_PREC + 3;

            if (s < 0) {
                s = 0;
            }
            if ((rc = big_shl(&qa, s)) == 0 && (rc = big_divmod(&qa, &qb, &r)) == 0) {
                rc = b128_pack(&r, ea - eb - s, qa.n != 0, sa ^ sb, out);
            }
        }
    }
    free(qa.w);
    free(qb.w);
    free(r.w);
    return rc;
}

int cc_b128_from_string(const char *s, size_t len, uint64_t out[2]) {
    cc_dec_t *d;
    big_t q = {NULL, 0, 0};
    big_t den = {NULL, 0, 0};
    big_t quo = {NULL, 0, 0};
    long e10;
    long bshift;
    int rc = 0;
    int k;

    if (len >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return b128_from_hex(s + 2, len - 2, out);
    }
    d = (cc_dec_t *)malloc(sizeof(*d));
    if (d == NULL || cc_dec_from_string(s, len, d) != 0) {
        free(d);
        return -1;
    }
    out[0] = 0;
    out[1] = 0;
    e10 = d->exponent;
    if (d->ndigits == 0 || e10 + d->ndigits < -4970) {
        /* zero, or below half the least subnormal (about 6.5e-4966) */
        free(d);
        return 0;
    }
    if (e10 + d->ndigits > 4934) {
        /* above the greatest finite value (about 1.19e4932) */
        out[1] = 0x7fff000000000000ULL;
        free(d);
        return 0;
    }
    for (k = 0; k < d->ndigits && rc == 0; ++k) {
        rc = big_mul_add(&q, 10, (uint32_t)d->digits[k]);
    }
    free(d);
    if (rc == 0 && e10 >= 0) {
        /* an integer: the digits times 10^e10, exactly */
        for (; e10 > 0 && rc == 0; --e10) {
            rc = big_mul_add(&q, 10, 0);
        }
        rc = rc == 0 ? b128_pack(&q, 0, 0, 0, out) : rc;
    } else if (rc == 0) {
        /* digits / 10^-e10: a quotient of at least 116 bits, its remainder the sticky bit */
        rc = big_set_u64(&den, 1);
        for (; e10 < 0 && rc == 0; ++e10) {
            rc = big_mul_add(&den, 10, 0);
        }
        bshift = big_bits(&den) - big_bits(&q) + B128_PREC + 3;
        if (bshift < 0) {
            bshift = 0;
        }
        if (rc == 0 && (rc = big_shl(&q, bshift)) == 0 && (rc = big_divmod(&q, &den, &quo)) == 0) {
            rc = b128_pack(&quo, -bshift, q.n != 0, 0, out);
        }
    }
    free(q.w);
    free(den.w);
    free(quo.w);
    return rc;
}
