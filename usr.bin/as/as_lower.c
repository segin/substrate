#include "as_lower.h"

#include "as_arm.h"
#include "as_arm_int.h"
#include "as_data.h"
#include "as_expr.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The most copies one .dcb or .ds may be written out as. */
#define LOWER_REPEAT_MAX (1u << 20)

/* A number of 128 bits, in two's complement. */
typedef struct {
    uint64_t lo;
    uint64_t hi;
} u128_t;

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} arg_vec_t;

static char *lower_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);

    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static void vec_free(arg_vec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i]);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

/* Takes s, which may be NULL for a failed allocation. */
static int vec_push(arg_vec_t *v, char *s) {
    if (s == NULL) {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        char **items = (char **)realloc(v->items, ncap * sizeof(*items));

        if (items == NULL) {
            free(s);
            return -1;
        }
        v->items = items;
        v->cap = ncap;
    }
    v->items[v->count++] = s;
    return 0;
}

static int vec_push_text(arg_vec_t *v, const char *s) {
    return vec_push(v, lower_strdup(s));
}

static int vec_push_num(arg_vec_t *v, unsigned long long n) {
    char text[32];

    snprintf(text, sizeof(text), "0x%llx", n);
    return vec_push_text(v, text);
}

/* The directive's arguments become those of v, which is emptied. */
static void set_args(as_directive_t *d, arg_vec_t *v) {
    size_t i;

    for (i = 0; i < d->arg_count; ++i) {
        free(d->args[i]);
    }
    free(d->args);
    d->args = v->items;
    d->arg_count = v->count;
    d->arg_cap = v->cap;
    memset(v, 0, sizeof(*v));
}

static int set_name(as_directive_t *d, const char *name) {
    char *copy = lower_strdup(name);

    if (copy == NULL) {
        return -1;
    }
    free(d->name);
    d->name = copy;
    return 0;
}

static const char *arg_text(const as_directive_t *d, size_t i) {
    return i < d->arg_count && d->args[i] != NULL ? d->args[i] : "";
}

static int arg_is_empty(const as_directive_t *d, size_t i) {
    const char *s = arg_text(d, i);

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return *s == '\0';
}

static void u128_muladd(u128_t *v, unsigned mul, unsigned add) {
    uint32_t limb[4];
    uint64_t carry = add;
    int i;

    limb[0] = (uint32_t)v->lo;
    limb[1] = (uint32_t)(v->lo >> 32);
    limb[2] = (uint32_t)v->hi;
    limb[3] = (uint32_t)(v->hi >> 32);
    for (i = 0; i < 4; ++i) {
        uint64_t t = (uint64_t)limb[i] * mul + carry;

        limb[i] = (uint32_t)t;
        carry = t >> 32;
    }
    v->lo = (uint64_t)limb[0] | ((uint64_t)limb[1] << 32);
    v->hi = (uint64_t)limb[2] | ((uint64_t)limb[3] << 32);
}

static void u128_negate(u128_t *v) {
    v->lo = ~v->lo + 1;
    v->hi = ~v->hi + (v->lo == 0 ? 1 : 0);
}

/*
 * A number written out and nothing else -- decimal, 0x, 0b or octal, with
 * a sign -- of up to 128 bits; more are cut off, as GNU as cuts a bignum.
 * 0, or -1 if the text is anything else.
 */
static int parse_literal_u128(const char *s, u128_t *out, int *negative) {
    unsigned base = 10;
    int digits = 0;

    out->lo = 0;
    out->hi = 0;
    *negative = 0;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '-' || *s == '+') {
        *negative = *s == '-';
        s++;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B') && (s[2] == '0' || s[2] == '1')) {
        base = 2;
        s += 2;
    } else if (s[0] == '0') {
        base = 8;
    }
    for (; *s != '\0' && !isspace((unsigned char)*s); ++s) {
        unsigned digit;

        if (*s >= '0' && *s <= '9') {
            digit = (unsigned)(*s - '0');
        } else if (*s >= 'a' && *s <= 'f') {
            digit = (unsigned)(*s - 'a') + 10;
        } else if (*s >= 'A' && *s <= 'F') {
            digit = (unsigned)(*s - 'A') + 10;
        } else {
            return -1;
        }
        if (digit >= base) {
            return -1;
        }
        u128_muladd(out, base, digit);
        digits++;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s != '\0' || digits == 0) {
        return -1;
    }
    if (*negative) {
        u128_negate(out);
    }
    return 0;
}

/* The value of an argument that is a number or an expression of numbers. */
static int arg_value(const char *arg, u128_t *out, int *negative) {
    long long v;

    if (parse_literal_u128(arg, out, negative) == 0) {
        return 0;
    }
    if (as_expr_eval_string(arg, NULL, NULL, &v) != AS_EXPR_EVAL_OK) {
        return -1;
    }
    out->lo = (uint64_t)v;
    out->hi = v < 0 ? ~(uint64_t)0 : 0;
    *negative = v < 0;
    return 0;
}

/* The LEB128 of 128 bits, into `.byte` arguments. */
static int push_leb128(arg_vec_t *v, u128_t value, int is_signed) {
    for (;;) {
        unsigned b = (unsigned)(value.lo & 0x7f);
        int sign = (value.hi >> 63) != 0;
        int last;

        value.lo = (value.lo >> 7) | (value.hi << 57);
        value.hi = is_signed && sign ? (value.hi >> 7) | ((uint64_t)0x7f << 57) : value.hi >> 7;
        if (is_signed) {
            last = (value.lo == 0 && value.hi == 0 && (b & 0x40) == 0) ||
                   (value.lo == ~(uint64_t)0 && value.hi == ~(uint64_t)0 && (b & 0x40) != 0);
        } else {
            last = value.lo == 0 && value.hi == 0;
        }
        if (vec_push_num(v, last ? b : (b | 0x80)) != 0) {
            return -1;
        }
        if (last) {
            return 0;
        }
    }
}

/*
 * The x87 extended format, for .tfloat: ten bytes, a 64-bit significand
 * with its leading bit written and a 15-bit exponent.  The decimal number
 * is worked out exactly as an integer times a power of two -- its digits
 * multiplied by ten, or divided by it after a shift that leaves bits to
 * spare -- and rounded once, to nearest and to even.
 */
#define BIG_LIMBS 800u

typedef struct {
    uint32_t *limb;
    size_t n;
} big_t;

static int big_muladd(big_t *b, uint32_t mul, uint32_t add) {
    uint64_t carry = add;
    size_t i;

    for (i = 0; i < b->n; ++i) {
        uint64_t t = (uint64_t)b->limb[i] * mul + carry;

        b->limb[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry != 0) {
        if (b->n == BIG_LIMBS) {
            return -1;
        }
        b->limb[b->n++] = (uint32_t)carry;
    }
    return 0;
}

static uint32_t big_div(big_t *b, uint32_t div) {
    uint64_t rem = 0;
    size_t i;

    for (i = b->n; i > 0; --i) {
        uint64_t t = (rem << 32) | b->limb[i - 1];

        b->limb[i - 1] = (uint32_t)(t / div);
        rem = t % div;
    }
    while (b->n > 0 && b->limb[b->n - 1] == 0) {
        b->n--;
    }
    return (uint32_t)rem;
}

static int big_shl(big_t *b, size_t bits) {
    size_t limbs = bits / 32;
    unsigned shift = (unsigned)(bits % 32);
    size_t i;

    if (b->n == 0) {
        return 0;
    }
    if (b->n + limbs + 1 > BIG_LIMBS) {
        return -1;
    }
    b->limb[b->n + limbs] = 0;
    for (i = b->n; i > 0; --i) {
        uint32_t w = b->limb[i - 1];

        if (shift != 0) {
            b->limb[i + limbs] |= w >> (32 - shift);
        }
        b->limb[i - 1 + limbs] = w << shift;
    }
    for (i = 0; i < limbs; ++i) {
        b->limb[i] = 0;
    }
    b->n += limbs + 1;
    while (b->n > 0 && b->limb[b->n - 1] == 0) {
        b->n--;
    }
    return 0;
}

static size_t big_bits(const big_t *b) {
    size_t bits;
    uint32_t top;

    if (b->n == 0) {
        return 0;
    }
    bits = (b->n - 1) * 32;
    for (top = b->limb[b->n - 1]; top != 0; top >>= 1) {
        bits++;
    }
    return bits;
}

static int big_bit(const big_t *b, size_t i) {
    return i / 32 < b->n && ((b->limb[i / 32] >> (i % 32)) & 1u) != 0;
}

/* Whether any of the bits below i is set. */
static int big_any_below(const big_t *b, size_t i) {
    size_t k;

    for (k = 0; k < i / 32 && k < b->n; ++k) {
        if (b->limb[k] != 0) {
            return 1;
        }
    }
    return i % 32 != 0 && i / 32 < b->n && (b->limb[i / 32] & ((1u << (i % 32)) - 1u)) != 0;
}

static int tfloat_bytes(const char *s, unsigned char out[10]) {
    big_t b;
    int negative = 0;
    long exp10 = 0;
    long written_exp = 0;
    int digits = 0;
    int seen_point = 0;
    int sticky = 0;
    size_t shift = 0;
    size_t bits;
    uint64_t mant = 0;
    long bexp;
    size_t i;
    int rc = -1;

    memset(out, 0, 10);
    b.n = 0;
    b.limb = (uint32_t *)calloc(BIG_LIMBS, sizeof(uint32_t));
    if (b.limb == NULL) {
        return -1;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '-' || *s == '+') {
        negative = *s == '-';
        s++;
    }
    for (; (*s >= '0' && *s <= '9') || (*s == '.' && !seen_point); ++s) {
        if (*s == '.') {
            seen_point = 1;
            continue;
        }
        digits++;
        /* Digits past what the format can hold only say that the rest is
         * not all zeros. */
        if (b.n < 12) {
            if (big_muladd(&b, 10, (uint32_t)(*s - '0')) != 0) {
                goto done;
            }
            if (seen_point) {
                exp10--;
            }
        } else {
            if (*s != '0') {
                sticky = 1;
            }
            if (!seen_point) {
                exp10++;
            }
        }
    }
    if (digits == 0) {
        goto done;
    }
    if (*s == 'e' || *s == 'E') {
        int exp_negative = 0;
        int exp_digits = 0;

        s++;
        if (*s == '-' || *s == '+') {
            exp_negative = *s == '-';
            s++;
        }
        for (; *s >= '0' && *s <= '9'; ++s) {
            if (written_exp < 100000) {
                written_exp = written_exp * 10 + (*s - '0');
            }
            exp_digits++;
        }
        if (exp_digits == 0) {
            goto done;
        }
        exp10 += exp_negative ? -written_exp : written_exp;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s != '\0') {
        goto done;
    }
    rc = 0;
    out[9] = negative ? 0x80 : 0;
    if (b.n == 0 || exp10 < -5100) {
        goto done;
    }
    if (exp10 > 5000) {
        goto infinity;
    }
    if (exp10 >= 0) {
        for (; exp10 > 0; --exp10) {
            if (big_muladd(&b, 10, 0) != 0) {
                goto infinity;
            }
        }
    } else {
        shift = (size_t)(-exp10) * 4 + 68;
        if (big_shl(&b, shift) != 0) {
            rc = -1;
            goto done;
        }
        for (; exp10 < 0; ++exp10) {
            if (big_div(&b, 10) != 0) {
                sticky = 1;
            }
        }
    }
    bits = big_bits(&b);
    for (i = 0; i < 64; ++i) {
        size_t at = bits + i;

        if (at >= 64 && big_bit(&b, at - 64)) {
            mant |= (uint64_t)1 << i;
        }
    }
    if (bits > 64) {
        int guard = big_bit(&b, bits - 65);

        if (big_any_below(&b, bits - 65)) {
            sticky = 1;
        }
        if (guard && (sticky || (mant & 1) != 0)) {
            mant++;
            if (mant == 0) {
                mant = (uint64_t)1 << 63;
                bits++;
            }
        }
    }
    bexp = (long)bits - 1 - (long)shift + 16383;
    if (bexp >= 0x7fff) {
        goto infinity;
    }
    if (bexp <= 0) {
        /* A denormal: the exponent is 0 and the significand has no
         * leading bit. */
        long down = 1 - bexp;

        mant = down >= 64 ? 0 : mant >> down;
        bexp = 0;
    }
    for (i = 0; i < 8; ++i) {
        out[i] = (unsigned char)(mant >> (8 * i));
    }
    out[8] = (unsigned char)bexp;
    out[9] |= (unsigned char)(bexp >> 8);
    goto done;

infinity:
    out[7] = 0x80;
    out[8] = 0xff;
    out[9] |= 0x7f;
done:
    free(b.limb);
    return rc;
}

static int push_tfloat(arg_vec_t *v, const char *arg) {
    unsigned char bytes[10];
    size_t i;

    if (tfloat_bytes(arg, bytes) != 0) {
        return 1;
    }
    for (i = 0; i < 10; ++i) {
        if (vec_push_num(v, bytes[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

/* What a .dc, .ds or .dcb is of, from its suffix: the bytes of one, and
 * the directive that stores one. */
typedef struct {
    const char *suffix;
    unsigned bytes;
    const char *store;
} lower_unit_t;

static const lower_unit_t *find_unit(const char *suffix, int for_ds) {
    static const lower_unit_t units[] = {
        {"", 2, ".short"},   {".b", 1, ".byte"},   {".w", 2, ".short"},  {".l", 4, ".long"},
        {".s", 4, ".float"}, {".d", 8, ".double"}, {".x", 10, ".tfloat"}, {".p", 10, NULL},
    };
    size_t i;

    for (i = 0; i < sizeof(units) / sizeof(units[0]); ++i) {
        /* .p is a size to reserve and not a thing to store. */
        if (strcmp(units[i].suffix, suffix) == 0 && (for_ds || units[i].store != NULL)) {
            return &units[i];
        }
    }
    return NULL;
}

/* A count that is written out in copies: a number, and not too many. */
static int repeat_count(const char *arg, unsigned long *out) {
    long long v;

    if (as_expr_eval_string(arg, NULL, NULL, &v) != AS_EXPR_EVAL_OK || v < 0 ||
        (unsigned long long)v > LOWER_REPEAT_MAX) {
        return -1;
    }
    *out = (unsigned long)v;
    return 0;
}

/*
 * 0 if the directive was rewritten or is none of these; -1 for no
 * memory; 1 for a directive of these that cannot be done, with why.
 */
/*
 * The bits of a `.float` or a `.double`, as GNU as 2.46 reads the
 * number:
 *
 *   - a decimal number, with or without a fraction and an exponent, and
 *     an `e` with nothing after it is no exponent;
 *   - `0` and a letter before it are passed over -- `0f1.5`, `0d2.5`,
 *     `0e1.5` are what C would write 1.5 and 2.5, and so is `0x10` ten;
 *   - `0x:` and hexadecimal digits are the bits themselves;
 *   - `inf` and `infinity`, `nan` and `qnan`, `snan`, with a sign;
 *   - a number too large is refused, not made infinity -- but one that
 *     only rounds up to it is infinity; one too small is nothing, or a
 *     denormal.
 *
 * 0, or -1 with *why said.
 */
static int float_bits(const char *text, int is_double, uint64_t *bits, const char **why) {
    static const double float_limit = 340282366920938463463374607431768211456.0; /* 2^128 */
    const char *s = text;
    const char *digits;
    char *end = NULL;
    int negative = 0;
    int signed_before = 0;
    double value;
    size_t n;

    *why = "not a floating-point number";
    while (isspace((unsigned char)*s)) {
        s++;
    }
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        n--;
    }
    if (*s == '+' || *s == '-') {
        negative = *s == '-';
        signed_before = 1;
        s++;
        n--;
    }
    if (n >= 2 && s[0] == '0' && isalpha((unsigned char)s[1])) {
        int hex = s[1] == 'x' || s[1] == 'X';

        if (signed_before) {
            return -1;
        }
        s += 2;
        n -= 2;
        if (hex && n > 1 && s[0] == ':') {
            uint64_t v = 0;
            size_t i;

            if (n - 1 > (is_double ? 16u : 8u)) {
                return -1;
            }
            for (i = 1; i < n; ++i) {
                if (!isxdigit((unsigned char)s[i])) {
                    return -1;
                }
                v = (v << 4) | (uint64_t)(isdigit((unsigned char)s[i]) ? s[i] - '0'
                                                                        : tolower((unsigned char)s[i]) - 'a' + 10);
            }
            *bits = v;
            return 0;
        }
        if (n > 0 && (*s == '+' || *s == '-')) {
            negative = *s == '-';
            s++;
            n--;
        }
    }
    {
        static const struct {
            const char *word;
            uint32_t single;
            uint64_t wide;
        } words[] = {
            {"inf", 0x7f800000u, 0x7ff0000000000000ull},      {"infinity", 0x7f800000u, 0x7ff0000000000000ull},
            {"nan", 0x7fffffffu, 0x7fffffffffffffffull},      {"qnan", 0x7fffffffu, 0x7fffffffffffffffull},
            {"snan", 0x7fbfffffu, 0x7ff7ffffffffffffull},
        };
        size_t i;

        for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
            if (strlen(words[i].word) == n && strncasecmp(s, words[i].word, n) == 0) {
                *bits = is_double ? words[i].wide | ((uint64_t)negative << 63)
                                  : (uint64_t)(words[i].single | ((uint32_t)negative << 31));
                return 0;
            }
        }
    }
    /* Digits, a point and an exponent, and nothing strtod() would
     * take besides: no 0x, no nan(), no infinity spelt out. */
    for (digits = s; digits < s + n; ++digits) {
        if (!isdigit((unsigned char)*digits) && *digits != '.' && *digits != 'e' && *digits != 'E' &&
            *digits != '+' && *digits != '-') {
            return -1;
        }
    }
    {
        char *copy = (char *)malloc(n + 1);

        if (copy == NULL) {
            *why = "out of memory";
            return -1;
        }
        memcpy(copy, s, n);
        copy[n] = '\0';
        if (n > 0 && (copy[n - 1] == 'e' || copy[n - 1] == 'E')) {
            copy[n - 1] = '\0';
        }
        value = copy[0] != '\0' ? strtod(copy, &end) : 0.0;
        if (copy[0] != '\0' && (end == copy || *end != '\0')) {
            free(copy);
            return -1;
        }
        free(copy);
    }
    if (value != value || value > 1.7976931348623157e308 || (!is_double && value >= float_limit)) {
        *why = "cannot create floating-point number";
        return -1;
    }
    if (negative) {
        value = -value;
    }
    if (is_double) {
        memcpy(bits, &value, sizeof(*bits));
    } else {
        float narrow = (float)value;
        uint32_t b;

        memcpy(&b, &narrow, sizeof(b));
        *bits = b;
    }
    return 0;
}

/* `.float` and `.double` become the `.long` and `.quad` of their bits:
 * the number was read in three places, and is read in this one. */
static int lower_floats(as_directive_t *d, const char **why) {
    int is_double = strcmp(d->name, ".double") == 0;
    arg_vec_t v;
    size_t i;

    if (!is_double && strcmp(d->name, ".float") != 0) {
        return 0;
    }
    memset(&v, 0, sizeof(v));
    for (i = 0; i < d->arg_count; ++i) {
        uint64_t bits = 0;

        /* (An argument left out is nothing, as it is of `.long`.) */
        if (!arg_is_empty(d, i) && float_bits(arg_text(d, i), is_double, &bits, why) != 0) {
            vec_free(&v);
            return strcmp(*why, "out of memory") == 0 ? -1 : 1;
        }
        if (vec_push_num(&v, bits) != 0) {
            vec_free(&v);
            return -1;
        }
    }
    set_args(d, &v);
    return set_name(d, is_double ? ".quad" : ".long");
}

/*
 * `.base64 "..."`, of one string or several: the bytes each says in
 * base 64, which a compiler writes its tables of constants in.  They
 * are `.byte` of each.  (It was no directive, and what a current gcc
 * makes of a source with a table in it was not assembled.)
 */
static int lower_base64(as_directive_t *d, const char **why) {
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    arg_vec_t v;
    size_t i;

    if (strcmp(d->name, ".base64") != 0) {
        return 0;
    }
    memset(&v, 0, sizeof(v));
    for (i = 0; i < d->arg_count; ++i) {
        const char *s = arg_text(d, i);
        size_t len = strlen(s);
        unsigned acc = 0;
        unsigned have = 0;
        size_t pad = 0;
        size_t k;

        /* (With its quotes or, as the parser hands a string to some
         * directives, without them.) */
        if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
            s++;
            len -= 2;
        }
        for (k = 0; k < len; ++k) {
            const char *at = s[k] != '\0' ? strchr(digits, s[k]) : NULL;

            if (s[k] == '=') {
                pad++;
                continue;
            }
            if (at == NULL || pad != 0) {
                vec_free(&v);
                *why = "has a character that is no digit of base 64";
                return 1;
            }
            acc = (acc << 6) | (unsigned)(at - digits);
            have += 6;
            if (have >= 8) {
                have -= 8;
                if (vec_push_num(&v, (acc >> have) & 0xffu) != 0) {
                    vec_free(&v);
                    return -1;
                }
            }
        }
        /* Four digits are three bytes; what is short of four is made
         * up with `=`.  (An empty string is refused, as GNU as refuses
         * it.) */
        if (len == 0 || len % 4 != 0 || pad > 2) {
            vec_free(&v);
            *why = "has a string that is not whole groups of four digits";
            return 1;
        }
    }
    set_args(d, &v);
    return set_name(d, ".byte");
}

static int lower_one(as_directive_t *d, unsigned address_bytes, const char **why) {
    arg_vec_t v;
    const lower_unit_t *unit;
    size_t i;
    int rc;

    memset(&v, 0, sizeof(v));
    *why = "an argument is not a number";

    /* With nothing after them: GNU as has a directive of space put
     * none, one of LEB128 put a zero, and one of strings is refused. */
    for (i = 0; i < d->arg_count && arg_is_empty(d, i); ++i) {
    }
    if (i == d->arg_count) {
        static const char *const space[] = {".zero", ".skip", ".space", ".fill"};
        static const char *const strings[] = {".ascii", ".asciz", ".string", ".string8", ".string16", ".string32",
                                              ".string64"};
        size_t k;

        for (k = 0; k < sizeof(space) / sizeof(space[0]); ++k) {
            /* (`.fill ,` is nothing too; `.zero ,` is refused.) */
            if ((d->arg_count == 0 || k == 3) && strcmp(d->name, space[k]) == 0) {
                set_args(d, &v);
                return set_name(d, ".byte");
            }
        }
        if (strncmp(d->name, ".ds", 3) == 0 || strncmp(d->name, ".dcb", 4) == 0) {
            if (find_unit(d->name + (d->name[2] == 's' ? 3 : 4), d->name[2] == 's') != NULL) {
                set_args(d, &v);
                return set_name(d, ".byte");
            }
        }
        if (strcmp(d->name, ".uleb128") == 0 || strcmp(d->name, ".sleb128") == 0) {
            if (vec_push_text(&v, "0") != 0) {
                vec_free(&v);
                return -1;
            }
            set_args(d, &v);
            return set_name(d, ".byte");
        }
        for (k = 0; d->arg_count == 0 && k < sizeof(strings) / sizeof(strings[0]); ++k) {
            if (strcmp(d->name, strings[k]) == 0) {
                *why = "a string is wanted";
                return 1;
            }
        }
    }

    if (strcmp(d->name, ".single") == 0) {
        return set_name(d, ".float");
    }
    /*
     * .incbin "file", skip, count: a skip left empty is 0, and a count
     * of 0, or left empty, is the rest of the file -- as GNU as has it.
     * An empty skip was refused, and a count of 0 gave no bytes.
     */
    if (strcmp(d->name, ".incbin") == 0 && d->arg_count >= 2) {
        long long count = 1;

        if (arg_is_empty(d, 1)) {
            char *zero = lower_strdup("0");

            if (zero == NULL) {
                return -1;
            }
            free(d->args[1]);
            d->args[1] = zero;
        }
        if (d->arg_count == 3 &&
            (arg_is_empty(d, 2) ||
             (as_expr_eval_string(arg_text(d, 2), NULL, NULL, &count) == AS_EXPR_EVAL_OK && count == 0))) {
            free(d->args[2]);
            d->args[2] = NULL;
            d->arg_count = 2;
        }
        return 0;
    }
    /* An alignment that pads with a pattern of two bytes or four. */
    if (strcmp(d->name, ".balignw") == 0 || strcmp(d->name, ".balignl") == 0) {
        d->fill_width = d->name[7] == 'w' ? 2 : 4;
        return set_name(d, ".balign");
    }
    if (strcmp(d->name, ".p2alignw") == 0 || strcmp(d->name, ".p2alignl") == 0) {
        d->fill_width = d->name[8] == 'w' ? 2 : 4;
        return set_name(d, ".p2align");
    }
    if (strcmp(d->name, ".string8") == 0) {
        return set_name(d, ".string");
    }
    if (strcmp(d->name, ".dc.a") == 0) {
        return set_name(d, address_bytes == 8 ? ".quad" : ".long");
    }

    if (strcmp(d->name, ".tfloat") == 0 || strcmp(d->name, ".dc.x") == 0) {
        *why = "an argument is not a decimal number";
        for (i = 0; i < d->arg_count; ++i) {
            rc = push_tfloat(&v, arg_text(d, i));
            if (rc != 0) {
                vec_free(&v);
                return rc;
            }
        }
        set_args(d, &v);
        return set_name(d, ".byte");
    }

    if (strcmp(d->name, ".dc") == 0 || strncmp(d->name, ".dc.", 4) == 0) {
        unit = find_unit(d->name + 3, 0);
        if (unit == NULL) {
            return 0;
        }
        for (i = 0; i < d->arg_count; ++i) {
            if (arg_is_empty(d, i) && unit->bytes <= 4 && unit->store[1] != 'f') {
                char *zero = lower_strdup("0");

                if (zero == NULL) {
                    return -1;
                }
                free(d->args[i]);
                d->args[i] = zero;
            }
        }
        return set_name(d, unit->store);
    }

    if (strcmp(d->name, ".ds") == 0 || strncmp(d->name, ".ds.", 4) == 0) {
        char text[64];

        unit = find_unit(d->name + 3, 1);
        if (unit == NULL) {
            return 0;
        }
        *why = "a count is wanted, and after it at most a value to fill with";
        if (d->arg_count < 1 || d->arg_count > 2 || arg_is_empty(d, 0)) {
            return 1;
        }
        if (unit->bytes == 10) {
            /* Ten bytes each, the value in the first eight. */
            unsigned long count;
            unsigned long k;
            u128_t fill = {0, 0};
            int negative;

            *why = "the count and the value of a ten-byte fill are to be numbers";
            if (repeat_count(arg_text(d, 0), &count) != 0 || count > LOWER_REPEAT_MAX / 10 ||
                (d->arg_count == 2 && arg_value(arg_text(d, 1), &fill, &negative) != 0)) {
                return 1;
            }
            for (k = 0; k < count; ++k) {
                for (i = 0; i < 10; ++i) {
                    if (vec_push_num(&v, i < 8 ? (unsigned)((fill.lo >> (8 * i)) & 0xff) : 0) != 0) {
                        vec_free(&v);
                        return -1;
                    }
                }
            }
            set_args(d, &v);
            return set_name(d, ".byte");
        }
        snprintf(text, sizeof(text), "%u", unit->bytes);
        if (vec_push_text(&v, arg_text(d, 0)) != 0 || vec_push_text(&v, text) != 0 ||
            vec_push_text(&v, d->arg_count == 2 && !arg_is_empty(d, 1) ? arg_text(d, 1) : "0") != 0) {
            vec_free(&v);
            return -1;
        }
        set_args(d, &v);
        return set_name(d, ".fill");
    }

    if (strcmp(d->name, ".dcb") == 0 || strncmp(d->name, ".dcb.", 5) == 0) {
        char text[64];

        unit = find_unit(d->name + 4, 0);
        if (unit == NULL) {
            return 0;
        }
        *why = "a count is wanted, and after it at most a value";
        if (d->arg_count < 1 || d->arg_count > 2 || arg_is_empty(d, 0)) {
            return 1;
        }
        if (unit->store[1] == 'f' || unit->store[1] == 'd' || unit->store[1] == 't') {
            /* Of a floating-point number: that many of it. */
            unsigned long count;
            unsigned long k;
            const char *value = d->arg_count == 2 && !arg_is_empty(d, 1) ? arg_text(d, 1) : "0";

            *why = "the count is to be a number, and the value a decimal one";
            if (repeat_count(arg_text(d, 0), &count) != 0) {
                return 1;
            }
            for (k = 0; k < count; ++k) {
                rc = unit->bytes == 10 ? push_tfloat(&v, value) : vec_push_text(&v, value);
                if (rc != 0) {
                    vec_free(&v);
                    return rc;
                }
            }
            set_args(d, &v);
            return set_name(d, unit->bytes == 10 ? ".byte" : unit->store);
        }
        snprintf(text, sizeof(text), "%u", unit->bytes);
        if (vec_push_text(&v, arg_text(d, 0)) != 0 || vec_push_text(&v, text) != 0 ||
            vec_push_text(&v, d->arg_count == 2 && !arg_is_empty(d, 1) ? arg_text(d, 1) : "0") != 0) {
            vec_free(&v);
            return -1;
        }
        set_args(d, &v);
        return set_name(d, ".fill");
    }

    if (strcmp(d->name, ".octa") == 0) {
        for (i = 0; i < d->arg_count; ++i) {
            u128_t value;
            int negative;

            if (arg_value(arg_text(d, i), &value, &negative) != 0) {
                vec_free(&v);
                return 1;
            }
            if (vec_push_num(&v, value.lo) != 0 || vec_push_num(&v, value.hi) != 0) {
                vec_free(&v);
                return -1;
            }
        }
        set_args(d, &v);
        return set_name(d, ".quad");
    }

    if (strcmp(d->name, ".uleb128") == 0 || strcmp(d->name, ".sleb128") == 0) {
        int is_signed = d->name[1] == 's';

        /* Of numbers.  One that is the distance between two labels is
         * left for where the labels are known. */
        for (i = 0; i < d->arg_count; ++i) {
            u128_t value;
            int negative;

            if (arg_value(arg_text(d, i), &value, &negative) != 0) {
                vec_free(&v);
                return 0;
            }
            /* GNU as takes a negative number given to .uleb128 as the
             * 64 bits of it. */
            if (!is_signed && negative) {
                value.hi = 0;
            }
            if (push_leb128(&v, value, is_signed) != 0) {
                vec_free(&v);
                return -1;
            }
        }
        if (d->arg_count == 0) {
            return 0;
        }
        set_args(d, &v);
        return set_name(d, ".byte");
    }

    if (strcmp(d->name, ".string16") == 0 || strcmp(d->name, ".string32") == 0 ||
        strcmp(d->name, ".string64") == 0) {
        const char *store = d->name[7] == '1' ? ".short" : (d->name[7] == '3' ? ".long" : ".quad");

        *why = "an argument is not a string";
        for (i = 0; i < d->arg_count; ++i) {
            char *bytes = NULL;
            size_t len = 0;
            size_t k;

            if (as_decode_string_literal(arg_text(d, i), &bytes, &len) != 0) {
                vec_free(&v);
                return 1;
            }
            for (k = 0; k <= len; ++k) {
                if (vec_push_num(&v, k < len ? (unsigned char)bytes[k] : 0) != 0) {
                    free(bytes);
                    vec_free(&v);
                    return -1;
                }
            }
            free(bytes);
        }
        set_args(d, &v);
        return set_name(d, store);
    }
    return 0;
}

/*
 * .struct: what follows is laid out from an offset and assembled into
 * nothing.  Its labels are the offsets, absolute symbols, and its data
 * directives only move the offset on -- the way a structure's fields are
 * given names:
 *
 *      .struct 0
 *  next:   .long 0
 *  size:   .long 0
 *  bytes:
 *
 * It was not known and so was passed over, and the fields were assembled
 * into the section the source was in, as data.  Here each label becomes
 * a `.set` of its offset and the directives that only reserve room are
 * taken out; the next directive that names a section ends it.
 */
typedef struct {
    char *name;
    long long value;
} struct_const_t;

typedef struct {
    struct_const_t *items;
    size_t count;
    size_t cap;
} struct_consts_t;

typedef struct {
    int in_struct;
    long long off;
} struct_frame_t;

typedef struct {
    as_stmt_t *items;
    size_t count;
    size_t cap;
} stmt_vec_t;

static int struct_lookup(void *cookie, const char *name, long long *value_out) {
    const struct_consts_t *c = (const struct_consts_t *)cookie;
    size_t i;

    /* The last said: a symbol may be set again. */
    for (i = c->count; i > 0; --i) {
        if (strcmp(c->items[i - 1].name, name) == 0) {
            *value_out = c->items[i - 1].value;
            return 1;
        }
    }
    return 0;
}

static int struct_define(struct_consts_t *c, const char *name, long long value) {
    if (c->count == c->cap) {
        size_t ncap = c->cap == 0 ? 32 : c->cap * 2;
        struct_const_t *items = (struct_const_t *)realloc(c->items, ncap * sizeof(*items));

        if (items == NULL) {
            return -1;
        }
        c->items = items;
        c->cap = ncap;
    }
    c->items[c->count].name = lower_strdup(name);
    if (c->items[c->count].name == NULL) {
        return -1;
    }
    c->items[c->count++].value = value;
    return 0;
}

static int stmt_vec_push(stmt_vec_t *v, const as_stmt_t *st) {
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 64 : v->cap * 2;
        as_stmt_t *items = (as_stmt_t *)realloc(v->items, ncap * sizeof(*items));

        if (items == NULL) {
            return -1;
        }
        v->items = items;
        v->cap = ncap;
    }
    v->items[v->count++] = *st;
    return 0;
}

/* The labels of st, which is at `off` of a .struct: a `.set` of each. */
static int struct_set_labels(stmt_vec_t *out, struct_consts_t *consts, as_stmt_t *st, long long off) {
    size_t i;

    for (i = 0; i < st->label_count; ++i) {
        as_stmt_t made;
        char text[32];

        memset(&made, 0, sizeof(made));
        made.kind = AS_STMT_DIRECTIVE;
        made.line = st->labels[i].line != 0 ? st->labels[i].line : st->line;
        made.file = st->file != NULL ? lower_strdup(st->file) : NULL;
        made.u.directive.name = lower_strdup(".set");
        made.u.directive.args = (char **)calloc(2, sizeof(char *));
        snprintf(text, sizeof(text), "%lld", off);
        if (made.u.directive.args != NULL) {
            made.u.directive.arg_cap = 2;
            made.u.directive.args[0] = lower_strdup(st->labels[i].name);
            made.u.directive.args[1] = lower_strdup(text);
            made.u.directive.arg_count = 2;
        }
        if ((st->file != NULL && made.file == NULL) || made.u.directive.name == NULL ||
            made.u.directive.args == NULL || made.u.directive.args[0] == NULL ||
            made.u.directive.args[1] == NULL || struct_define(consts, st->labels[i].name, off) != 0 ||
            stmt_vec_push(out, &made) != 0) {
            if (made.u.directive.args != NULL) {
                free(made.u.directive.args[0]);
                free(made.u.directive.args[1]);
            }
            free(made.u.directive.args);
            free(made.u.directive.name);
            free(made.file);
            return -1;
        }
        free(st->labels[i].name);
        free(st->labels[i].file);
    }
    free(st->labels);
    st->labels = NULL;
    st->label_count = 0;
    st->label_cap = 0;
    return 0;
}

/* A statement that is taken out, its labels being gone already. */
static void struct_drop(as_stmt_t *st) {
    size_t i;

    free(st->file);
    if (st->kind == AS_STMT_DIRECTIVE) {
        for (i = 0; i < st->u.directive.arg_count; ++i) {
            free(st->u.directive.args[i]);
        }
        free(st->u.directive.args);
        free(st->u.directive.name);
    }
    memset(st, 0, sizeof(*st));
}

static unsigned struct_scalar_bytes(const char *name) {
    static const struct {
        const char *name;
        unsigned bytes;
    } scalars[] = {
        {".byte", 1},  {".short", 2}, {".word", 2},  {".hword", 2}, {".2byte", 2}, {".long", 4},
        {".4byte", 4}, {".quad", 8},  {".8byte", 8}, {".float", 4}, {".double", 8},
    };
    size_t i;

    for (i = 0; i < sizeof(scalars) / sizeof(scalars[0]); ++i) {
        if (strcmp(name, scalars[i].name) == 0) {
            return scalars[i].bytes;
        }
    }
    return 0;
}

/* A directive that says something of a symbol and nothing of a place. */
static int struct_directive_is_placeless(const char *name) {
    static const char *const names[] = {
        ".set",    ".equ",       ".equiv",    ".globl", ".global", ".local", ".weak", ".type",
        ".size",   ".hidden",    ".internal", ".protected", ".file", ".ident", ".code16", ".code32",
        ".code64", ".intel_syntax", ".att_syntax", ".arch",
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Move the offset of a .struct on by what a directive would store or
 * reserve.  NULL, or why it cannot be.
 */
static const char *struct_advance(const as_directive_t *d, struct_consts_t *consts, long long *off) {
    unsigned bytes = struct_scalar_bytes(d->name);
    long long v = 0;
    long long size = 1;

    if (bytes != 0) {
        size_t i;

        /* Room is reserved and nothing stored: a value is refused, as
         * GNU as refuses it. */
        for (i = 0; i < d->arg_count; ++i) {
            if (as_expr_eval_string(arg_text(d, i), struct_lookup, consts, &v) == AS_EXPR_EVAL_OK && v != 0) {
                return "a value cannot be stored in a .struct, which only reserves room";
            }
        }
        *off += (long long)bytes * (long long)(d->arg_count == 0 ? 1 : d->arg_count);
        return NULL;
    }
    if (strcmp(d->name, ".ascii") == 0 || strcmp(d->name, ".asciz") == 0 || strcmp(d->name, ".string") == 0) {
        return "strings must be placed into a section";
    }
    if (d->arg_count < 1 || as_expr_eval_string(arg_text(d, 0), struct_lookup, consts, &v) != AS_EXPR_EVAL_OK ||
        v < 0) {
        return "is not, in a .struct, a directive with a size that is a number";
    }
    if (strcmp(d->name, ".skip") == 0 || strcmp(d->name, ".space") == 0 || strcmp(d->name, ".zero") == 0) {
        *off += v;
        return NULL;
    }
    if (strcmp(d->name, ".fill") == 0) {
        if (d->arg_count >= 2 && !arg_is_empty(d, 1) &&
            (as_expr_eval_string(arg_text(d, 1), struct_lookup, consts, &size) != AS_EXPR_EVAL_OK || size < 0)) {
            return "the size of what is filled with is not a number";
        }
        *off += v * size;
        return NULL;
    }
    if (strcmp(d->name, ".org") == 0) {
        if (v < *off) {
            return "would move backwards";
        }
        *off = v;
        return NULL;
    }
    if (strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 || strcmp(d->name, ".p2align") == 0) {
        if (d->name[1] == 'p') {
            if (v > 30) {
                return "is too large an alignment";
            }
            v = 1ll << v;
        }
        if (v == 0 || (v & (v - 1)) != 0) {
            return "is not to a power of two";
        }
        *off = (*off + v - 1) & ~(v - 1);
        return NULL;
    }
    return "cannot be used in a .struct";
}

static int lower_structs(as_parse_result_t *parsed, char *errbuf, size_t errbuf_sz) {
    struct_consts_t consts;
    stmt_vec_t out;
    struct_frame_t at = {0, 0};
    struct_frame_t *stack = NULL;
    size_t stack_count = 0;
    size_t stack_cap = 0;
    const char *why = NULL;
    int any = 0;
    int rc = -1;
    size_t i;

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL &&
            strcmp(st->u.directive.name, ".struct") == 0) {
            any = 1;
            break;
        }
    }
    if (!any) {
        return 0;
    }
    memset(&consts, 0, sizeof(consts));
    memset(&out, 0, sizeof(out));
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        as_directive_t *d = &st->u.directive;
        const char *name = st->kind == AS_STMT_DIRECTIVE && d->name != NULL ? d->name : "";
        long long v = 0;

        if (strcmp(name, ".struct") == 0) {
            /* `a: .struct a + 4`: the label is of the offset it leaves. */
            if (at.in_struct && struct_set_labels(&out, &consts, st, at.off) != 0) {
                goto oom;
            }
            if (d->arg_count >= 1 && !arg_is_empty(d, 0) &&
                as_expr_eval_string(arg_text(d, 0), struct_lookup, &consts, &v) != AS_EXPR_EVAL_OK) {
                why = "the offset is not a number";
                goto fail_at;
            }
            if (at.in_struct) {
                struct_drop(st);
            } else {
                /* Its labels are of the section it leaves, and stay. */
                size_t k;

                for (k = 0; k < d->arg_count; ++k) {
                    free(d->args[k]);
                }
                free(d->args);
                free(d->name);
                memset(&st->u, 0, sizeof(st->u));
                st->kind = st->label_count != 0 ? AS_STMT_LABEL_ONLY : AS_STMT_EMPTY;
                if (stmt_vec_push(&out, st) != 0) {
                    goto oom;
                }
            }
            at.in_struct = 1;
            at.off = v;
            continue;
        }
        if (strcmp(name, ".text") == 0 || strcmp(name, ".data") == 0 || strcmp(name, ".bss") == 0 ||
            strcmp(name, ".rodata") == 0 || strcmp(name, ".section") == 0 || strcmp(name, ".pushsection") == 0 ||
            strcmp(name, ".popsection") == 0 || strcmp(name, ".previous") == 0) {
            if (at.in_struct && struct_set_labels(&out, &consts, st, at.off) != 0) {
                goto oom;
            }
            if (name[2] == 'u') {
                if (stack_count == stack_cap) {
                    size_t ncap = stack_cap == 0 ? 8 : stack_cap * 2;
                    struct_frame_t *next = (struct_frame_t *)realloc(stack, ncap * sizeof(*next));

                    if (next == NULL) {
                        goto oom;
                    }
                    stack = next;
                    stack_cap = ncap;
                }
                stack[stack_count++] = at;
                at.in_struct = 0;
            } else if (name[2] == 'o') {
                if (stack_count != 0) {
                    at = stack[--stack_count];
                }
            } else if (strcmp(name, ".previous") == 0 && at.in_struct) {
                /* Back to the section the .struct was written in, which
                 * nothing after this pass has left. */
                at.in_struct = 0;
                struct_drop(st);
                continue;
            } else {
                at.in_struct = 0;
            }
            if (stmt_vec_push(&out, st) != 0) {
                goto oom;
            }
            continue;
        }
        if (!at.in_struct) {
            /* A constant set outside may be an offset inside. */
            if ((strcmp(name, ".set") == 0 || strcmp(name, ".equ") == 0) && d->arg_count == 2 &&
                as_expr_eval_string(arg_text(d, 1), struct_lookup, &consts, &v) == AS_EXPR_EVAL_OK &&
                struct_define(&consts, arg_text(d, 0), v) != 0) {
                goto oom;
            }
            if (stmt_vec_push(&out, st) != 0) {
                goto oom;
            }
            continue;
        }
        if (struct_set_labels(&out, &consts, st, at.off) != 0) {
            goto oom;
        }
        if (st->kind == AS_STMT_INSTRUCTION) {
            why = "an instruction cannot be assembled in a .struct";
            goto fail_at;
        }
        if (st->kind != AS_STMT_DIRECTIVE) {
            struct_drop(st);
            continue;
        }
        if (struct_directive_is_placeless(name)) {
            if ((strcmp(name, ".set") == 0 || strcmp(name, ".equ") == 0) && d->arg_count == 2 &&
                as_expr_eval_string(arg_text(d, 1), struct_lookup, &consts, &v) == AS_EXPR_EVAL_OK &&
                struct_define(&consts, arg_text(d, 0), v) != 0) {
                goto oom;
            }
            if (stmt_vec_push(&out, st) != 0) {
                goto oom;
            }
            continue;
        }
        why = struct_advance(d, &consts, &at.off);
        if (why != NULL) {
            goto fail_at;
        }
        struct_drop(st);
        continue;

    fail_at:
        if (errbuf != NULL && errbuf_sz != 0) {
            snprintf(errbuf, errbuf_sz, "%s:%u: %s%s%s", st->file != NULL ? st->file : "<input>", st->line, name,
                     name[0] != '\0' ? ": " : "", why);
        }
        goto fail;
    }
    free(parsed->items);
    parsed->items = out.items;
    parsed->count = out.count;
    parsed->cap = out.cap;
    out.items = NULL;
    rc = 0;
    goto done;

oom:
    if (errbuf != NULL && errbuf_sz != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory");
    }
fail:
    /* Statements are in two places or in neither by now; the assembler
     * is about to exit, and is not to free any of them twice. */
    parsed->count = 0;
    free(out.items);
done:
    for (i = 0; i < consts.count; ++i) {
        free(consts.items[i].name);
    }
    free(consts.items);
    free(stack);
    return rc;
}

/* A directive of up to two arguments, at the place of the statement `at`. */
static int push_directive(stmt_vec_t *out, const as_stmt_t *at, const char *name, const char *a0, const char *a1) {
    as_stmt_t made;
    arg_vec_t args;

    memset(&made, 0, sizeof(made));
    memset(&args, 0, sizeof(args));
    made.kind = AS_STMT_DIRECTIVE;
    made.line = at->line;
    made.file = at->file != NULL ? lower_strdup(at->file) : NULL;
    made.u.directive.name = lower_strdup(name);
    if ((at->file != NULL && made.file == NULL) || made.u.directive.name == NULL ||
        (a0 != NULL && vec_push_text(&args, a0) != 0) || (a1 != NULL && vec_push_text(&args, a1) != 0)) {
        goto fail;
    }
    set_args(&made.u.directive, &args);
    if (stmt_vec_push(out, &made) == 0) {
        return 0;
    }
    args.items = made.u.directive.args;
    args.count = made.u.directive.arg_count;
    args.cap = made.u.directive.arg_cap;
fail:
    vec_free(&args);
    free(made.u.directive.name);
    free(made.file);
    return -1;
}

/* A label alone, at the place of the statement `at`. */
static int push_label(stmt_vec_t *out, const as_stmt_t *at, const char *name) {
    as_stmt_t made;

    memset(&made, 0, sizeof(made));
    made.kind = AS_STMT_LABEL_ONLY;
    made.line = at->line;
    made.file = at->file != NULL ? lower_strdup(at->file) : NULL;
    made.labels = (as_label_def_t *)calloc(1, sizeof(*made.labels));
    if (made.labels != NULL) {
        made.label_cap = 1;
        made.labels[0].name = lower_strdup(name);
        made.labels[0].line = at->line;
        made.labels[0].file = at->file != NULL ? lower_strdup(at->file) : NULL;
        made.label_count = 1;
    }
    if ((at->file != NULL && made.file == NULL) || made.labels == NULL || made.labels[0].name == NULL ||
        (at->file != NULL && made.labels[0].file == NULL) || stmt_vec_push(out, &made) != 0) {
        if (made.labels != NULL) {
            free(made.labels[0].name);
            free(made.labels[0].file);
        }
        free(made.labels);
        free(made.file);
        return -1;
    }
    return 0;
}

static char *local_common_name(const as_directive_t *d) {
    const char *s = arg_text(d, 0);
    size_t n;
    char *name;

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        n--;
    }
    name = (char *)malloc(n + 1);
    if (name != NULL) {
        memcpy(name, s, n);
        name[n] = '\0';
    }
    return name;
}

/*
 * A common symbol that is local has no need of the linker to find it a
 * place, and is given one here: `.lcomm name, size`, and `.comm name,
 * size[, alignment]` of a name that a `.local` above it names, are
 * storage in subsection 1 of .bss -- behind what the source puts in
 * .bss itself -- at a label, with the type of an object and the size.
 * So two files that each have a `static int x;` have an `x` each.
 */
static int lower_local_commons(as_parse_result_t *parsed, char *errbuf, size_t errbuf_sz) {
    struct_consts_t consts;
    struct_consts_t locals;
    stmt_vec_t out;
    stmt_vec_t tail;
    const char *why = NULL;
    int any = 0;
    int rc = -1;
    size_t i;

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL &&
            (strcmp(st->u.directive.name, ".lcomm") == 0 || strcmp(st->u.directive.name, ".comm") == 0)) {
            any = 1;
            break;
        }
    }
    if (!any) {
        return 0;
    }
    memset(&consts, 0, sizeof(consts));
    memset(&locals, 0, sizeof(locals));
    memset(&out, 0, sizeof(out));
    memset(&tail, 0, sizeof(tail));
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        as_directive_t *d = &st->u.directive;
        char *name;
        long long size = 0;
        long long align = 1;
        char text[32];
        int is_lcomm;
        int failed;
        size_t k;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL) {
            if (stmt_vec_push(&out, st) != 0) {
                goto oom;
            }
            continue;
        }
        if ((strcmp(d->name, ".set") == 0 || strcmp(d->name, ".equ") == 0 || strcmp(d->name, ".equiv") == 0) &&
            d->arg_count == 2) {
            name = local_common_name(d);
            if (name == NULL) {
                goto oom;
            }
            if (as_expr_eval_string(d->args[1], struct_lookup, &consts, &size) == AS_EXPR_EVAL_OK &&
                struct_define(&consts, name, size) != 0) {
                free(name);
                goto oom;
            }
            free(name);
        }
        if (strcmp(d->name, ".local") == 0) {
            for (k = 0; k < d->arg_count; ++k) {
                as_directive_t one = *d;

                one.args = &d->args[k];
                one.arg_count = 1;
                name = local_common_name(&one);
                if (name == NULL || struct_define(&locals, name, 0) != 0) {
                    free(name);
                    goto oom;
                }
                free(name);
            }
        }
        is_lcomm = strcmp(d->name, ".lcomm") == 0;
        if (!is_lcomm && strcmp(d->name, ".comm") != 0) {
            if (stmt_vec_push(&out, st) != 0) {
                goto oom;
            }
            continue;
        }
        name = local_common_name(d);
        if (name == NULL) {
            goto oom;
        }
        if (!is_lcomm && !struct_lookup(&locals, name, &size)) {
            free(name);
            if (stmt_vec_push(&out, st) != 0) {
                goto oom;
            }
            continue;
        }
        if (name[0] == '\0') {
            why = "expected symbol name";
        } else if (d->arg_count < 2 || arg_is_empty(d, 1)) {
            why = "missing size expression";
        } else if (as_expr_eval_string(d->args[1], struct_lookup, &consts, &size) != AS_EXPR_EVAL_OK) {
            why = "the size is not a number";
        } else if (is_lcomm && d->arg_count > 2) {
            why = "takes a name and a size";
        } else if (d->arg_count > 3) {
            why = "takes a name, a size and an alignment";
        } else if (d->arg_count == 3 &&
                   (arg_is_empty(d, 2) ||
                    as_expr_eval_string(d->args[2], struct_lookup, &consts, &align) != AS_EXPR_EVAL_OK)) {
            why = "expected alignment after size";
        } else if (align < 0 || (align & (align - 1)) != 0) {
            why = "alignment not a power of 2";
        }
        if (why != NULL) {
            if (errbuf != NULL && errbuf_sz != 0) {
                snprintf(errbuf, errbuf_sz, "%s:%u: %s: %s", st->file != NULL ? st->file : "<input>", st->line,
                         d->name, why);
            }
            free(name);
            goto fail;
        }
        if (size < 0) {
            /* As GNU as: said, and the directive is no more than that. */
            char said[80];

            snprintf(said, sizeof(said), "size (%lld) out of range, ignored", size);
            failed = push_directive(&out, st, ".warning", said, NULL);
            free(name);
            if (failed) {
                goto oom;
            }
            struct_drop(st);
            continue;
        }
        if (is_lcomm) {
            /* An alignment is implied by the size, up to 8. */
            align = size >= 8 ? 8 : (size >= 4 ? 4 : (size >= 2 ? 2 : 1));
        }
        snprintf(text, sizeof(text), "%lld", size);
        failed = push_directive(&out, st, ".type", name, "@object") != 0 ||
                 push_directive(&out, st, ".size", name, text) != 0;
        if (!failed && tail.count == 0) {
            failed = push_directive(&tail, st, ".bss", "1", NULL) != 0;
        }
        if (!failed && align > 1) {
            char a[32];

            snprintf(a, sizeof(a), "%lld", align);
            failed = push_directive(&tail, st, ".balign", a, NULL) != 0;
        }
        failed = failed || push_label(&tail, st, name) != 0 || push_directive(&tail, st, ".zero", text, NULL) != 0;
        free(name);
        if (failed) {
            goto oom;
        }
        /* The labels of the directive's line stay where they are. */
        if (st->label_count != 0) {
            as_stmt_t kept = *st;

            kept.kind = AS_STMT_LABEL_ONLY;
            memset(&kept.u, 0, sizeof(kept.u));
            kept.file = st->file != NULL ? lower_strdup(st->file) : NULL;
            if ((st->file != NULL && kept.file == NULL) || stmt_vec_push(&out, &kept) != 0) {
                free(kept.file);
                goto oom;
            }
            st->labels = NULL;
            st->label_count = 0;
            st->label_cap = 0;
        }
        struct_drop(st);
    }
    for (i = 0; i < tail.count; ++i) {
        if (stmt_vec_push(&out, &tail.items[i]) != 0) {
            goto oom;
        }
    }
    free(parsed->items);
    parsed->items = out.items;
    parsed->count = out.count;
    parsed->cap = out.cap;
    out.items = NULL;
    rc = 0;
    goto done;

oom:
    if (errbuf != NULL && errbuf_sz != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory");
    }
fail:
    /* Statements are in two places or in neither by now; the assembler
     * is about to exit, and is not to free any of them twice. */
    parsed->count = 0;
    free(out.items);
done:
    free(tail.items);
    for (i = 0; i < consts.count; ++i) {
        free(consts.items[i].name);
    }
    free(consts.items);
    for (i = 0; i < locals.count; ++i) {
        free(locals.items[i].name);
    }
    free(locals.items);
    return rc;
}

/*
 * The symbols a source sets to numbers: as they stand at the statement
 * being read, and -- for a name not yet set there -- as they end, but
 * for the names that are somewhere set to what is no number.
 */
typedef struct {
    struct_consts_t now;
    struct_consts_t last;
    struct_consts_t never;
} set_consts_t;

static int set_lookup(void *cookie, const char *name, long long *value_out) {
    const set_consts_t *c = (const set_consts_t *)cookie;
    long long unused;

    if (struct_lookup((void *)&c->now, name, value_out)) {
        return 1;
    }
    return !struct_lookup((void *)&c->never, name, &unused) && struct_lookup((void *)&c->last, name, value_out);
}

static int directive_is_set(const as_directive_t *d) {
    return (strcmp(d->name, ".set") == 0 || strcmp(d->name, ".equ") == 0 || strcmp(d->name, ".equiv") == 0) &&
           d->arg_count == 2;
}

/* Every name the source sets, with the number it ends as. */
static int set_consts_collect(const as_parse_result_t *parsed, set_consts_t *c) {
    size_t i;

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        long long v = 0;
        char *name;
        int is_number;

        if (st->kind != AS_STMT_DIRECTIVE || st->u.directive.name == NULL || !directive_is_set(&st->u.directive)) {
            continue;
        }
        name = local_common_name(&st->u.directive);
        if (name == NULL) {
            return -1;
        }
        /* A name that is ever set to what is no number is not known
         * ahead of where it is set. */
        is_number = as_expr_eval_string(st->u.directive.args[1], struct_lookup, &c->last, &v) == AS_EXPR_EVAL_OK;
        if (struct_define(is_number ? &c->last : &c->never, name, v) != 0) {
            free(name);
            return -1;
        }
        free(name);
    }
    return 0;
}

/* An argument that is a number once the symbols set to numbers are
 * known is written as the number. */
static int arg_make_number(as_directive_t *d, size_t i, set_consts_t *c, int ahead) {
    long long v;
    char text[32];
    char *copy;

    /* An alignment is of what is known where it stands; a count may
     * be of a symbol set further down. */
    if (i >= d->arg_count || d->args[i] == NULL || arg_is_empty(d, i) ||
        as_expr_eval_string(d->args[i], NULL, NULL, &v) == AS_EXPR_EVAL_OK ||
        (ahead ? as_expr_eval_string(d->args[i], set_lookup, c, &v)
               : as_expr_eval_string(d->args[i], struct_lookup, &c->now, &v)) != AS_EXPR_EVAL_OK) {
        return 0;
    }
    snprintf(text, sizeof(text), "%lld", v);
    copy = lower_strdup(text);
    if (copy == NULL) {
        return -1;
    }
    free(d->args[i]);
    d->args[i] = copy;
    return 0;
}

/* The value of an argument that is there and is a number by now. */
static int arg_number(const as_directive_t *d, size_t i, long long *out) {
    return i < d->arg_count && d->args[i] != NULL && !arg_is_empty(d, i) &&
           as_expr_eval_string(d->args[i], NULL, NULL, out) == AS_EXPR_EVAL_OK;
}

static int arg_set_number(as_directive_t *d, size_t i, long long v) {
    char text[32];
    char *copy;

    snprintf(text, sizeof(text), "%lld", v);
    copy = lower_strdup(text);
    if (copy == NULL) {
        return -1;
    }
    if (i < d->arg_count) {
        free(d->args[i]);
        d->args[i] = copy;
        return 0;
    }
    if (i != d->arg_count || d->arg_count != 0) {
        free(copy);
        return -1;
    }
    {
        arg_vec_t v1;

        memset(&v1, 0, sizeof(v1));
        if (vec_push(&v1, copy) != 0) {
            return -1;
        }
        set_args(d, &v1);
    }
    return 0;
}

static int section_type_is_known(const char *raw) {
    static const char *const known[] = {"progbits", "nobits", "note", "init_array", "fini_array",
                                        "preinit_array", "unwind", "rela", "rel"};
    char word[32];
    size_t n = 0;
    size_t i;

    while (*raw != '\0' && (isspace((unsigned char)*raw) || *raw == '@' || *raw == '%' || *raw == '"')) {
        raw++;
    }
    if (isdigit((unsigned char)*raw)) {
        return 1;
    }
    while (raw[n] != '\0' && raw[n] != '"' && !isspace((unsigned char)raw[n]) && n + 1 < sizeof(word)) {
        word[n] = (char)tolower((unsigned char)raw[n]);
        n++;
    }
    word[n] = '\0';
    for (i = 0; i < sizeof(known) / sizeof(known[0]); ++i) {
        if (strcmp(word, known[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Take a letter out of a string of section flags. */
static void section_flag_drop(char *flags, char letter) {
    char *w = flags;
    const char *r;

    for (r = flags; *r != '\0'; ++r) {
        if (*r != letter) {
            *w++ = *r;
        }
    }
    *w = '\0';
}

/*
 * The arguments of the directives that take numbers, checked once and
 * left as numbers for everything that reads them after.
 *
 * - A symbol set to a number is its number: `.align A`, `.skip K*2`,
 *   `.fill K, 1, 0x66`, `.org K`, and where the `.set` is further down.
 * - An alignment is validated here and nowhere else: none, or 0, is 1;
 *   it is a power of two; it is no more than the object library will
 *   write (AS_ALIGN_P2_MAX).
 * - A count below nothing is said and the directive passed over.
 * - `.zero n, fill` is `.skip`; `.fill` of more than 8 bytes is of 8.
 * - A section of entries to merge with no size for one, or of a group
 *   with no name for it, or of a type that is none, is said, and is
 *   without that.
 *
 * What is said is a `.warning` put before the directive.
 */
/* Say of each statement in the vector from `*count` to `made` that it
 * goes before the statement at `at`. */
static int lower_note_before(size_t **before, size_t *cap, size_t *count, size_t made, size_t at) {
    while (*count < made) {
        if (*count == *cap) {
            size_t ncap = *cap == 0 ? 8 : *cap * 2;
            size_t *next = (size_t *)realloc(*before, ncap * sizeof(*next));

            if (next == NULL) {
                return -1;
            }
            *before = next;
            *cap = ncap;
        }
        (*before)[(*count)++] = at;
    }
    return 0;
}

static int lower_arguments(as_parse_result_t *parsed, char *errbuf, size_t errbuf_sz) {
    set_consts_t consts;
    stmt_vec_t out;
    size_t *before = NULL;
    size_t before_cap = 0;
    size_t before_count = 0;
    const char *why = NULL;
    int rc = -1;
    size_t i;

    memset(&consts, 0, sizeof(consts));
    memset(&out, 0, sizeof(out));
    if (set_consts_collect(parsed, &consts) != 0) {
        goto oom;
    }
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        as_directive_t *d = &st->u.directive;
        const char *said = NULL;
        int is_align;
        int is_space;
        long long v = 0;
        size_t k;

        /*
         * Each statement is changed where it is.  (A second vector of
         * them all was made here, 144 bytes and more for each, though
         * none is dropped and the few that are added are warnings: `out`
         * has those alone, and `before` the statement each goes before.)
         */
        if (lower_note_before(&before, &before_cap, &before_count, out.count, i - 1) != 0) {
            goto oom;
        }
        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL) {
            continue;
        }
        if (directive_is_set(d)) {
            char *name = local_common_name(d);

            if (name == NULL) {
                goto oom;
            }
            if (as_expr_eval_string(d->args[1], struct_lookup, &consts.now, &v) == AS_EXPR_EVAL_OK &&
                struct_define(&consts.now, name, v) != 0) {
                free(name);
                goto oom;
            }
            free(name);
        }
        is_align = strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 ||
                   strcmp(d->name, ".p2align") == 0;
        is_space = strcmp(d->name, ".zero") == 0 || strcmp(d->name, ".skip") == 0 || strcmp(d->name, ".space") == 0;
        if (is_align || is_space || strcmp(d->name, ".fill") == 0 || strcmp(d->name, ".org") == 0) {
            for (k = 0; k < d->arg_count; ++k) {
                if (arg_make_number(d, k, &consts, !is_align) != 0) {
                    goto oom;
                }
            }
        }
        if (is_align) {
            int p2 = strcmp(d->name, ".p2align") == 0;

            if (d->arg_count == 0 || arg_is_empty(d, 0)) {
                /* None is asked for: the least there is. */
                if (arg_set_number(d, 0, p2 ? 0 : 1) != 0) {
                    goto oom;
                }
            } else if (arg_number(d, 0, &v)) {
                if (!p2 && v == 0) {
                    if (arg_set_number(d, 0, 1) != 0) {
                        goto oom;
                    }
                } else if (!p2 && (v < 0 || (v & (v - 1)) != 0)) {
                    why = "alignment not a power of 2";
                } else if (p2 ? (v < 0 || v > AS_ALIGN_P2_MAX) : v > (1ll << AS_ALIGN_P2_MAX)) {
                    why = "alignment too large: the most is 2 to the power 20";
                }
            }
        } else if (is_space && arg_number(d, 0, &v) && v < 0) {
            said = ".space repeat count is negative, ignored";
        } else if (strcmp(d->name, ".zero") == 0 && d->arg_count > 2) {
            why = "takes a count and a fill";
        } else if (strcmp(d->name, ".zero") == 0 && d->arg_count == 2) {
            if (set_name(d, ".skip") != 0) {
                goto oom;
            }
        } else if (strcmp(d->name, ".fill") == 0) {
            if (arg_number(d, 0, &v) && v < 0) {
                said = "repeat < 0; .fill ignored";
            } else if (arg_number(d, 1, &v) && v < 0) {
                said = "size negative; .fill ignored";
            } else if (arg_number(d, 1, &v) && v == 0) {
                if (arg_set_number(d, 0, 0) != 0 || arg_set_number(d, 1, 1) != 0) {
                    goto oom;
                }
            } else if (arg_number(d, 1, &v) && v > 8) {
                if (push_directive(&out, st, ".warning", ".fill size clamped to 8", NULL) != 0 ||
                    arg_set_number(d, 1, 8) != 0) {
                    goto oom;
                }
            }
        } else if ((strcmp(d->name, ".section") == 0 || strcmp(d->name, ".pushsection") == 0) && d->arg_count >= 2 &&
                   d->args[1] != NULL && !(d->arg_count == 2 && arg_number(d, 1, &v))) {
            char *flags = d->args[1];
            size_t named = 3;

            if (d->arg_count >= 3 && !arg_is_empty(d, 2) && !section_type_is_known(d->args[2])) {
                if (push_directive(&out, st, ".warning", "unrecognized section type", NULL) != 0) {
                    goto oom;
                }
                d->args[2][0] = '\0';
            }
            if (strchr(flags, 'M') != NULL) {
                if (d->arg_count <= named || arg_is_empty(d, named)) {
                    if (push_directive(&out, st, ".warning", "entity size for SHF_MERGE not specified", NULL) != 0) {
                        goto oom;
                    }
                    section_flag_drop(flags, 'M');
                } else {
                    named++;
                }
            }
            if (strchr(flags, 'o') != NULL) {
                named++;
            }
            if (strchr(flags, 'G') != NULL && (d->arg_count <= named || arg_is_empty(d, named))) {
                if (push_directive(&out, st, ".warning", "group name for SHF_GROUP not specified", NULL) != 0) {
                    goto oom;
                }
                section_flag_drop(flags, 'G');
            }
        }
        if (why != NULL) {
            if (errbuf != NULL && errbuf_sz != 0) {
                snprintf(errbuf, errbuf_sz, "%s:%u: %s: %s", st->file != NULL ? st->file : "<input>", st->line,
                         d->name, why);
            }
            goto fail;
        }
        if (said != NULL) {
            /* The labels of the line stay; the directive is the saying. */
            arg_vec_t text;

            memset(&text, 0, sizeof(text));
            if (vec_push_text(&text, said) != 0 || set_name(d, ".warning") != 0) {
                vec_free(&text);
                goto oom;
            }
            set_args(d, &text);
        }
    }
    if (out.count != 0) {
        as_stmt_t *merged;
        size_t w = 0;
        size_t k = 0;

        if (lower_note_before(&before, &before_cap, &before_count, out.count, parsed->count - 1) != 0) {
            goto oom;
        }
        merged = (as_stmt_t *)malloc((parsed->count + out.count) * sizeof(*merged));
        if (merged == NULL) {
            goto oom;
        }
        for (i = 0; i < parsed->count; ++i) {
            while (k < out.count && before[k] == i) {
                merged[w++] = out.items[k++];
            }
            merged[w++] = parsed->items[i];
        }
        free(parsed->items);
        parsed->items = merged;
        parsed->count = w;
        parsed->cap = w;
        out.count = 0;
    }
    rc = 0;
    goto done;

oom:
    if (errbuf != NULL && errbuf_sz != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory");
    }
fail:
    /* Statements are in two places or in neither by now; the assembler
     * is about to exit, and is not to free any of them twice. */
    parsed->count = 0;
done:
    free(out.items);
    free(before);
    for (i = 0; i < consts.now.count; ++i) {
        free(consts.now.items[i].name);
    }
    free(consts.now.items);
    for (i = 0; i < consts.last.count; ++i) {
        free(consts.last.items[i].name);
    }
    free(consts.last.items);
    for (i = 0; i < consts.never.count; ++i) {
        free(consts.never.items[i].name);
    }
    free(consts.never.items);
    return rc;
}

/*
 * Symbols that are other names for something.
 *
 * `.set a, expr`, where expr is no number, makes `a` a name for what
 * expr is: another symbol and so many bytes on, a place, the distance
 * between two.  Each part of the assembler that met such a name made
 * what it could of it, and they did not agree: a name for a name was
 * undefined if the second was set further down; a name for another
 * file's symbol was a symbol of its own that no file defined; a
 * distance was refused unless it was written `. - label`.
 *
 * So the name is taken out before any of them sees it.  Wherever it is
 * used -- in an operand, or in the argument of a directive that takes a
 * value -- what it stands for is put in its place, names within names
 * followed to the end, and a name that comes back to itself said to.
 * `.` in what a name stands for is where the `.set` is, which a label
 * put there says.  What is left of the `.set` is what the symbol table
 * needs: the name, and one symbol with a number, or two labels.
 *
 * And a symbol set to a number further down than where it is used is
 * its number there, in an operand or a value: it had been a relocation
 * against an absolute symbol.
 */
typedef struct {
    char *name;
    size_t stmt;
    as_expr_t *expr;
    int busy;
    int resolved;
    int kept; /* a symbol of its own: see lower_aliases() */
    /* What a branch to the name goes to, if the name is this file's
     * own for a label of it: see alias_branch_target(). */
    int branch_known;
    char *branch_label;
} alias_def_t;

typedef struct {
    alias_def_t *items;
    size_t count;
    size_t cap;
    set_consts_t consts;
    const char *loop; /* a name that stands for itself */
    int failed;       /* no memory */
} alias_ctx_t;

/* What a name stands for where statement `use` is: as it was last set
 * before there, or -- set only further down -- as it is set last. */
static alias_def_t *alias_find(alias_ctx_t *a, const char *name, size_t use) {
    alias_def_t *last = NULL;
    alias_def_t *before = NULL;
    size_t i;

    for (i = 0; i < a->count; ++i) {
        if (strcmp(a->items[i].name, name) != 0) {
            continue;
        }
        last = &a->items[i];
        if (a->items[i].stmt < use) {
            before = last;
        }
    }
    return before != NULL ? before : last;
}

static void alias_substitute(alias_ctx_t *a, as_expr_t *e, size_t use, int *changed);

static void alias_resolve(alias_ctx_t *a, alias_def_t *def) {
    int changed = 0;

    if (def->resolved) {
        return;
    }
    if (def->busy) {
        a->loop = def->name;
        return;
    }
    def->busy = 1;
    alias_substitute(a, def->expr, def->stmt, &changed);
    def->busy = 0;
    def->resolved = 1;
}

static void alias_substitute(alias_ctx_t *a, as_expr_t *e, size_t use, int *changed) {
    alias_def_t *def;
    as_expr_t *copy;
    long long value;

    if (e == NULL || a->loop != NULL || a->failed) {
        return;
    }
    alias_substitute(a, e->lhs, use, changed);
    alias_substitute(a, e->rhs, use, changed);
    /* (A symbol with a relocation operator on it is left a symbol.) */
    if (e->kind != AS_EXPR_SYMBOL || e->symbol == NULL || e->reloc_op != NULL) {
        return;
    }
    def = alias_find(a, e->symbol, use);
    if (def == NULL) {
        /* A number it is not yet set to here, and will be. */
        if (!struct_lookup(&a->consts.now, e->symbol, &value) && set_lookup(&a->consts, e->symbol, &value)) {
            free(e->symbol);
            e->symbol = NULL;
            e->kind = AS_EXPR_CONST;
            e->value = value;
            *changed = 1;
        }
        return;
    }
    alias_resolve(a, def);
    if (a->loop != NULL || a->failed || def->kept) {
        return;
    }
    copy = as_expr_clone(def->expr);
    if (copy == NULL) {
        a->failed = 1;
        return;
    }
    free(e->symbol);
    free(e->src_file);
    *e = *copy;
    free(copy);
    *changed = 1;
}

static int expr_names_dot(const as_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && strcmp(e->symbol, ".") == 0) {
        return 1;
    }
    return expr_names_dot(e->lhs) || expr_names_dot(e->rhs);
}

static int expr_rename_dot(as_expr_t *e, const char *label) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && strcmp(e->symbol, ".") == 0) {
        char *copy = lower_strdup(label);

        if (copy == NULL) {
            return -1;
        }
        free(e->symbol);
        e->symbol = copy;
    }
    return expr_rename_dot(e->lhs, label) != 0 || expr_rename_dot(e->rhs, label) != 0 ? -1 : 0;
}

static int stmt_add_label(as_stmt_t *st, const char *name) {
    as_label_def_t *labels = (as_label_def_t *)realloc(st->labels, (st->label_count + 1) * sizeof(*labels));

    if (labels == NULL) {
        return -1;
    }
    st->labels = labels;
    st->label_cap = st->label_count + 1;
    memset(&labels[st->label_count], 0, sizeof(labels[0]));
    labels[st->label_count].name = lower_strdup(name);
    labels[st->label_count].line = st->line;
    labels[st->label_count].file = st->file != NULL ? lower_strdup(st->file) : NULL;
    if (labels[st->label_count].name == NULL || (st->file != NULL && labels[st->label_count].file == NULL)) {
        free(labels[st->label_count].name);
        free(labels[st->label_count].file);
        return -1;
    }
    st->label_count++;
    return 0;
}

/* The directives whose arguments from `first` on are values. */
static int directive_takes_values(const char *name, size_t *first) {
    static const char *const all[] = {".byte",  ".word",    ".short",   ".hword",  ".2byte", ".long",
                                      ".int",   ".4byte",   ".quad",    ".8byte",  ".skip",  ".space",
                                      ".zero",  ".fill",    ".org",     ".uleb128", ".sleb128"};
    /* (Not an alignment: it is of what is known where it stands.) */
    size_t i;

    *first = 0;
    for (i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        if (strcmp(name, all[i]) == 0) {
            return 1;
        }
    }
    if (strcmp(name, ".size") == 0) {
        *first = 1;
        return 1;
    }
    return 0;
}

static int source_has_label(const as_parse_result_t *parsed, const char *name) {
    size_t i;
    size_t j;

    for (i = 0; i < parsed->count; ++i) {
        for (j = 0; j < parsed->items[i].label_count; ++j) {
            if (parsed->items[i].labels[j].name != NULL && strcmp(parsed->items[i].labels[j].name, name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * A call or a jump to a name that this file has set to one of its own
 * labels, and has not exported: `call f.localalias`, which a compiler
 * writes so that the call is to this file's `f` and to no other's.
 *
 * Put `f` in its place, as the name is elsewhere, and the call is one
 * to a symbol another object may take over: it was assembled with a
 * relocation through the procedure linkage table, and a shared object
 * made of it had a table entry and a jump for a call GNU as resolves
 * where it stands.  So such a branch goes to a label of this file's
 * that is put where `f` is, `.L.alias.f`, which nothing else can name.
 *
 * The label's name, or NULL where the name is not one of these (and is
 * to be put in its place as any other).
 */
static const char *alias_branch_target(alias_ctx_t *a, as_parse_result_t *parsed, const as_stmt_t *st,
                                       const as_operand_t *op, size_t use) {
    const char *m = st->u.instr.mnemonic;
    const as_expr_t *e = op->u.expr;
    alias_def_t *def;
    const char *target;
    size_t i;
    size_t j;

    /* (And an address taken of it, or a load through it: `leaq
     * f.localalias(%rip), %rax` named `f`, which a shared object may
     * not be made with.) */
    if (op->kind == AS_OPERAND_MEMORY) {
        e = op->u.mem.disp;
    }
    if (m == NULL || e == NULL || e->kind != AS_EXPR_SYMBOL || e->symbol == NULL || e->reloc_op != NULL ||
        e->lhs != NULL || e->rhs != NULL) {
        return NULL;
    }
    def = alias_find(a, e->symbol, use);
    if (def == NULL) {
        return NULL;
    }
    if (def->branch_known) {
        return def->branch_label;
    }
    def->branch_known = 1;
    alias_resolve(a, def);
    if (a->loop != NULL || a->failed || def->kept || def->expr == NULL || def->expr->kind != AS_EXPR_SYMBOL ||
        def->expr->symbol == NULL || def->expr->reloc_op != NULL || def->expr->lhs != NULL || def->expr->rhs != NULL) {
        return NULL;
    }
    target = def->expr->symbol;
    /* (A name that is exported is another object's to bind.) */
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *s = &parsed->items[i];

        if (s->kind != AS_STMT_DIRECTIVE || s->u.directive.name == NULL ||
            (strcmp(s->u.directive.name, ".globl") != 0 && strcmp(s->u.directive.name, ".global") != 0 &&
             strcmp(s->u.directive.name, ".weak") != 0)) {
            continue;
        }
        for (j = 0; j < s->u.directive.arg_count; ++j) {
            if (s->u.directive.args[j] != NULL && strcmp(s->u.directive.args[j], def->name) == 0) {
                return NULL;
            }
        }
    }
    for (i = 0; i < parsed->count; ++i) {
        for (j = 0; j < parsed->items[i].label_count; ++j) {
            const char *have = parsed->items[i].labels[j].name;

            if (have != NULL && strcmp(have, target) == 0) {
                size_t len = strlen(target) + sizeof(".L.alias.");
                char *label = (char *)malloc(len);

                if (label == NULL) {
                    a->failed = 1;
                    return NULL;
                }
                snprintf(label, len, ".L.alias.%s", target);
                if (!source_has_label(parsed, label) && stmt_add_label(&parsed->items[i], label) != 0) {
                    free(label);
                    a->failed = 1;
                    return NULL;
                }
                def->branch_label = label;
                return label;
            }
        }
    }
    return NULL;
}

static int lower_aliases(as_parse_result_t *parsed, char *errbuf, size_t errbuf_sz) {
    alias_ctx_t a;
    int rc = -1;
    size_t i;
    size_t k;

    memset(&a, 0, sizeof(a));
    if (set_consts_collect(parsed, &a.consts) != 0) {
        goto oom;
    }
    /* What each name that is no number stands for. */
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        as_directive_t *d = &st->u.directive;
        as_expr_t *e;
        long long value;
        char *name;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL || !directive_is_set(d)) {
            continue;
        }
        if (as_expr_eval_string(d->args[1], set_lookup, &a.consts, &value) == AS_EXPR_EVAL_OK) {
            continue;
        }
        e = as_parse_expr_string(d->args[1], st->file, st->line);
        if (e == NULL) {
            /* Not an expression: said where it is read. */
            continue;
        }
        if (expr_names_dot(e)) {
            char label[48];

            snprintf(label, sizeof(label), ".Lset.%lu", (unsigned long)i);
            if (stmt_add_label(st, label) != 0 || expr_rename_dot(e, label) != 0) {
                as_expr_free(e);
                goto oom;
            }
        }
        name = local_common_name(d);
        if (name == NULL) {
            as_expr_free(e);
            goto oom;
        }
        if (a.count == a.cap) {
            size_t cap = a.cap != 0 ? a.cap * 2 : 16;
            alias_def_t *items = (alias_def_t *)realloc(a.items, cap * sizeof(*items));

            if (items == NULL) {
                free(name);
                as_expr_free(e);
                goto oom;
            }
            a.items = items;
            a.cap = cap;
        }
        memset(&a.items[a.count], 0, sizeof(a.items[0]));
        a.items[a.count].name = name;
        a.items[a.count].stmt = i;
        a.items[a.count].expr = e;
        a.count++;
    }
    for (i = 0; i < a.count; ++i) {
        alias_resolve(&a, &a.items[i]);
    }
    /*
     * A name that other files see, for a place in this one, is a symbol
     * in its own right: another file may say what it is instead, and
     * what refers to it must go by the name.  It is not put in.
     */
    for (i = 0; i < parsed->count && a.loop == NULL && !a.failed; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        const as_directive_t *d = &st->u.directive;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL ||
            (strcmp(d->name, ".globl") != 0 && strcmp(d->name, ".global") != 0 && strcmp(d->name, ".weak") != 0)) {
            continue;
        }
        for (k = 0; k < d->arg_count; ++k) {
            as_directive_t one = *d;
            char *name;
            size_t j;

            one.args = &d->args[k];
            one.arg_count = 1;
            name = local_common_name(&one);
            if (name == NULL) {
                goto oom;
            }
            for (j = 0; j < a.count; ++j) {
                as_expr_linear_t lin;

                if (strcmp(a.items[j].name, name) == 0 &&
                    as_expr_eval_linear(a.items[j].expr, NULL, NULL, &lin) == AS_EXPR_EVAL_OK &&
                    lin.add_symbol != NULL && lin.sub_symbol == NULL && source_has_label(parsed, lin.add_symbol)) {
                    a.items[j].kept = 1;
                }
            }
            free(name);
        }
    }
    /* And each put where it is named. */
    for (i = 0; i < parsed->count && a.loop == NULL && !a.failed; ++i) {
        as_stmt_t *st = &parsed->items[i];
        int changed = 0;

        if (st->kind == AS_STMT_INSTRUCTION) {
            for (k = 0; k < st->u.instr.operand_count; ++k) {
                as_operand_t *op = &st->u.instr.operands[k];

                if (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF ||
                    op->kind == AS_OPERAND_MEMORY) {
                    const char *local = alias_branch_target(&a, parsed, st, op, i);

                    if (local != NULL) {
                        as_expr_t *e = op->kind == AS_OPERAND_MEMORY ? op->u.mem.disp : op->u.expr;
                        char *copy = lower_strdup(local);

                        if (copy == NULL) {
                            goto oom;
                        }
                        free(e->symbol);
                        e->symbol = copy;
                        continue;
                    }
                }
                if (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) {
                    alias_substitute(&a, op->u.expr, i, &changed);
                } else if (op->kind == AS_OPERAND_MEMORY) {
                    alias_substitute(&a, op->u.mem.disp, i, &changed);
                }
            }
        } else if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL) {
            as_directive_t *d = &st->u.directive;
            size_t first = 0;
            int is_set = directive_is_set(d);

            if (is_set || directive_takes_values(d->name, &first)) {
                for (k = is_set ? 1 : first; k < d->arg_count; ++k) {
                    as_expr_t *e;
                    char *text;

                    if (d->args[k] == NULL || arg_is_empty(d, k)) {
                        continue;
                    }
                    e = as_parse_expr_string(d->args[k], st->file, st->line);
                    if (e == NULL) {
                        continue;
                    }
                    changed = 0;
                    if (is_set) {
                        char *name = local_common_name(d);
                        alias_def_t *def = name != NULL ? alias_find(&a, name, i + 1) : NULL;

                        free(name);
                        if (def != NULL && def->stmt == i) {
                            as_expr_free(e);
                            e = as_expr_clone(def->expr);
                            if (e == NULL) {
                                a.failed = 1;
                                break;
                            }
                            changed = 1;
                        }
                    } else {
                        alias_substitute(&a, e, i, &changed);
                    }
                    if (changed && a.loop == NULL && !a.failed) {
                        text = as_expr_to_text(e);
                        if (text == NULL) {
                            a.failed = 1;
                        } else {
                            free(d->args[k]);
                            d->args[k] = text;
                        }
                    }
                    as_expr_free(e);
                }
            }
            /* The numbers as they stand, for what is further down. */
            if (is_set) {
                long long value;
                char *name = local_common_name(d);

                if (name == NULL) {
                    goto oom;
                }
                if (as_expr_eval_string(d->args[1], struct_lookup, &a.consts.now, &value) == AS_EXPR_EVAL_OK) {
                    long long plain;

                    if (struct_define(&a.consts.now, name, value) != 0) {
                        free(name);
                        goto oom;
                    }
                    /* A number made of other symbols' numbers is
                     * written as the number it is: the symbol table
                     * reads the directive with no table of them. */
                    if (as_expr_eval_string(d->args[1], NULL, NULL, &plain) != AS_EXPR_EVAL_OK &&
                        arg_set_number(d, 1, value) != 0) {
                        free(name);
                        goto oom;
                    }
                }
                free(name);
            }
        }
    }
    if (a.failed) {
        goto oom;
    }
    if (a.loop != NULL) {
        alias_def_t *def = alias_find(&a, a.loop, 0);
        const as_stmt_t *st = &parsed->items[def != NULL ? def->stmt : 0];

        if (errbuf != NULL && errbuf_sz != 0) {
            snprintf(errbuf, errbuf_sz, "%s:%u: symbol definition loop encountered at `%s'",
                     st->file != NULL ? st->file : "<input>", st->line, a.loop);
        }
        goto done;
    }
    rc = 0;
    goto done;

oom:
    if (errbuf != NULL && errbuf_sz != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory");
    }
done:
    for (i = 0; i < a.count; ++i) {
        free(a.items[i].name);
        free(a.items[i].branch_label);
        as_expr_free(a.items[i].expr);
    }
    free(a.items);
    for (i = 0; i < a.consts.now.count; ++i) {
        free(a.consts.now.items[i].name);
    }
    free(a.consts.now.items);
    for (i = 0; i < a.consts.last.count; ++i) {
        free(a.consts.last.items[i].name);
    }
    free(a.consts.last.items);
    for (i = 0; i < a.consts.never.count; ++i) {
        free(a.consts.never.items[i].name);
    }
    free(a.consts.never.items);
    return rc;
}

/*
 * `.code16gcc`: 16-bit code as a compiler for 32-bit code writes it.
 * The instructions of the stack and the calls are of 32 bits where the
 * source does not say: a `call` pushes four bytes, and `ret` pops
 * them.  Each such instruction is given the `l` it was written
 * without -- unless a register says its size -- and the rest is
 * `.code16`.
 */
static int code16gcc_widen(as_instruction_t *in) {
    static const char *const names[] = {"call", "ret", "lret", "push", "pop", "enter",
                                        "leave", "pushf", "popf", "pusha", "popa"};
    size_t i;
    size_t n;
    char *wide;

    if (in->mnemonic == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcasecmp(in->mnemonic, names[i]) == 0) {
            break;
        }
    }
    if (i == sizeof(names) / sizeof(names[0])) {
        return 0;
    }
    for (i = 0; i < in->operand_count; ++i) {
        if (in->operands[i].kind == AS_OPERAND_REGISTER) {
            return 0;
        }
    }
    n = strlen(in->mnemonic);
    wide = (char *)realloc(in->mnemonic, n + 2);
    if (wide == NULL) {
        return -1;
    }
    wide[n] = 'l';
    wide[n + 1] = '\0';
    in->mnemonic = wide;
    return 0;
}

int as_directives_lower(as_parse_result_t *parsed, unsigned address_bytes, char *errbuf, size_t errbuf_sz) {
    size_t pushed = 0;
    int gcc16 = 0;
    size_t i;

    if (parsed == NULL) {
        return -1;
    }
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        const char *why = "";
        int rc;

        if (st->kind == AS_STMT_INSTRUCTION && gcc16 && code16gcc_widen(&st->u.instr) != 0) {
            if (errbuf != NULL && errbuf_sz != 0) {
                snprintf(errbuf, errbuf_sz, "out of memory");
            }
            return -1;
        }
        if (st->kind != AS_STMT_DIRECTIVE || st->u.directive.name == NULL) {
            continue;
        }
        if (strcmp(st->u.directive.name, ".code16gcc") == 0) {
            gcc16 = 1;
            if (set_name(&st->u.directive, ".code16") != 0) {
                if (errbuf != NULL && errbuf_sz != 0) {
                    snprintf(errbuf, errbuf_sz, "out of memory");
                }
                return -1;
            }
        } else if (strcmp(st->u.directive.name, ".code16") == 0 || strcmp(st->u.directive.name, ".code32") == 0 ||
                   strcmp(st->u.directive.name, ".code64") == 0) {
            gcc16 = 0;
        }
        /* A .popsection with nothing pushed is said and passed over,
         * as GNU as has it. */
        if (strcmp(st->u.directive.name, ".pushsection") == 0) {
            pushed++;
        } else if (strcmp(st->u.directive.name, ".popsection") == 0) {
            if (pushed != 0) {
                pushed--;
            } else {
                arg_vec_t said;

                memset(&said, 0, sizeof(said));
                if (vec_push_text(&said, ".popsection without corresponding .pushsection; ignored") != 0 ||
                    set_name(&st->u.directive, ".warning") != 0) {
                    vec_free(&said);
                    if (errbuf != NULL && errbuf_sz != 0) {
                        snprintf(errbuf, errbuf_sz, "out of memory");
                    }
                    return -1;
                }
                set_args(&st->u.directive, &said);
                continue;
            }
        }
        rc = lower_one(&st->u.directive, address_bytes, &why);
        if (rc == 0) {
            rc = lower_floats(&st->u.directive, &why);
        }
        if (rc == 0) {
            rc = lower_base64(&st->u.directive, &why);
        }
        if (rc == 0) {
            continue;
        }
        if (errbuf != NULL && errbuf_sz != 0) {
            if (rc < 0) {
                snprintf(errbuf, errbuf_sz, "out of memory");
            } else {
                snprintf(errbuf, errbuf_sz, "%s:%u: %s: %s", st->file != NULL ? st->file : "<input>", st->line,
                         st->u.directive.name, why);
            }
        }
        return -1;
    }
    if (lower_structs(parsed, errbuf, errbuf_sz) != 0) {
        return -1;
    }
    if (lower_local_commons(parsed, errbuf, errbuf_sz) != 0 || lower_aliases(parsed, errbuf, errbuf_sz) != 0) {
        return -1;
    }
    return lower_arguments(parsed, errbuf, errbuf_sz);
}

/*
 * ARM and AArch64.
 */

/* A constant waiting for its literal pool. */
typedef struct {
    char *section;
    char *label;
    char *expr;
    unsigned size;
} arm_literal_t;

typedef struct {
    int a64;
    int thumb;
    unsigned it_left;
    char *section;
    char *previous;
    char *stack[32];
    size_t depth;
    arm_literal_t *lits;
    size_t lit_count;
    size_t lit_cap;
    unsigned serial;
    /* .eabi_attribute: the bytes of the attributes, in order. */
    unsigned char *attrs;
    size_t attr_len;
    size_t attr_cap;
    /* Thumb functions: the labels defined in Thumb code, the names
     * `.type` calls functions, and the labels `.thumb_func` is before. */
    arg_vec_t thumb_labels;
    arg_vec_t functions;
    arg_vec_t thumb_funcs;
    int thumb_func_next;
    stmt_vec_t out;
    const char *why;
} arm_lower_t;

static int arm_set_section(arm_lower_t *a, const char *name) {
    char *copy = lower_strdup(name);

    if (copy == NULL) {
        return -1;
    }
    free(a->previous);
    a->previous = a->section;
    a->section = copy;
    return 0;
}

static int arm_attr_byte(arm_lower_t *a, unsigned b) {
    if (a->attr_len == a->attr_cap) {
        size_t ncap = a->attr_cap == 0 ? 64 : a->attr_cap * 2;
        unsigned char *next = (unsigned char *)realloc(a->attrs, ncap);

        if (next == NULL) {
            return -1;
        }
        a->attrs = next;
        a->attr_cap = ncap;
    }
    a->attrs[a->attr_len++] = (unsigned char)b;
    return 0;
}

static int arm_attr_uleb(arm_lower_t *a, unsigned long long v) {
    do {
        unsigned b = (unsigned)(v & 0x7f);

        v >>= 7;
        if (arm_attr_byte(a, b | (v != 0 ? 0x80u : 0u)) != 0) {
            return -1;
        }
    } while (v != 0);
    return 0;
}

/* The statement is gone, but for its labels. */
static int arm_drop(arm_lower_t *a, as_stmt_t *st) {
    size_t i;

    if (st->kind == AS_STMT_DIRECTIVE) {
        for (i = 0; i < st->u.directive.arg_count; ++i) {
            free(st->u.directive.args[i]);
        }
        free(st->u.directive.args);
        free(st->u.directive.name);
        memset(&st->u.directive, 0, sizeof(st->u.directive));
    }
    if (st->label_count != 0) {
        st->kind = AS_STMT_LABEL_ONLY;
        return stmt_vec_push(&a->out, st);
    }
    free(st->file);
    free(st->labels);
    memset(st, 0, sizeof(*st));
    return 0;
}

/* The constants waiting in `section` are put here; `wrap` if here is
 * not in that section. */
static int arm_flush(arm_lower_t *a, const as_stmt_t *at, const char *section, int wrap) {
    unsigned widest = 0;
    size_t i;
    size_t kept = 0;

    for (i = 0; i < a->lit_count; ++i) {
        if (strcmp(a->lits[i].section, section) == 0 && a->lits[i].size > widest) {
            widest = a->lits[i].size;
        }
    }
    if (widest == 0) {
        return 0;
    }
    if (wrap && push_directive(&a->out, at, ".pushsection", section, NULL) != 0) {
        return -1;
    }
    for (i = 0; i < a->lit_count; ++i) {
        arm_literal_t *l = &a->lits[i];

        if (strcmp(l->section, section) != 0) {
            a->lits[kept++] = *l;
            continue;
        }
        /* Each where a word of its size may stand, with nothing in the
         * gap: it is data, among instructions or not. */
        if (push_directive(&a->out, at, ".p2align", l->size == 8 ? "3" : "2", "0") != 0 ||
            push_label(&a->out, at, l->label) != 0 ||
            push_directive(&a->out, at, l->size == 8 ? ".quad" : ".long", l->expr, NULL) != 0) {
            return -1;
        }
        free(l->section);
        free(l->label);
        free(l->expr);
    }
    a->lit_count = kept;
    return wrap ? push_directive(&a->out, at, ".popsection", NULL, NULL) : 0;
}

/* `ldr reg, =value`: a move where one instruction can move the value,
 * and otherwise a load of it from the literal pool. */
static int arm_load_constant(arm_lower_t *a, as_stmt_t *st) {
    as_instruction_t *in = &st->u.instr;
    const char *value = in->operands[1].raw + 1;
    const char *ops[2];
    char imm[160];
    char mov[16];
    char label[40];
    as_arm_req_t rq;
    as_arm_out_t out;
    unsigned size = 4;
    char *text;
    size_t i;

    if (strlen(value) + 2 > sizeof(imm) || strlen(in->mnemonic) + 1 > sizeof(mov)) {
        a->why = "a constant too long";
        return 1;
    }
    snprintf(imm, sizeof(imm), "#%s", value);
    /* (`ldreq` is `moveq`.) */
    snprintf(mov, sizeof(mov), "mov%s", in->mnemonic + 3);
    ops[0] = in->operands[0].raw;
    ops[1] = imm;
    memset(&rq, 0, sizeof(rq));
    rq.mnemonic = mov;
    rq.ops = ops;
    rq.op_count = 2;
    rq.thumb = a->thumb;
    rq.in_it = a->it_left != 0;
    if (isdigit((unsigned char)value[0]) || value[0] == '-' || value[0] == '~') {
        if ((a->a64 ? as_a64_encode(&rq, &out, NULL, 0) : as_arm_encode(&rq, &out, NULL, 0)) == 0 &&
            out.fixup_count == 0) {
            char *m = lower_strdup(mov);
            char *o = lower_strdup(imm);

            if (m == NULL || o == NULL) {
                free(m);
                free(o);
                return -1;
            }
            free(in->mnemonic);
            in->mnemonic = m;
            free(in->operands[1].raw);
            in->operands[1].raw = o;
            return 0;
        }
    }
    if (a->a64) {
        unsigned n, bits;
        int sp;

        if (as_a64_register(in->operands[0].raw, &n, &bits, &sp) != 0 || (bits != 32 && bits != 64)) {
            a->why = "a constant is loaded to a general register";
            return 1;
        }
        size = bits / 8;
    }
    for (i = 0; i < a->lit_count; ++i) {
        if (a->lits[i].size == size && strcmp(a->lits[i].section, a->section) == 0 &&
            strcmp(a->lits[i].expr, value) == 0) {
            break;
        }
    }
    if (i == a->lit_count) {
        arm_literal_t l;

        if (a->lit_count == a->lit_cap) {
            size_t ncap = a->lit_cap == 0 ? 16 : a->lit_cap * 2;
            arm_literal_t *next = (arm_literal_t *)realloc(a->lits, ncap * sizeof(*next));

            if (next == NULL) {
                return -1;
            }
            a->lits = next;
            a->lit_cap = ncap;
        }
        snprintf(label, sizeof(label), ".Lpool.%u", a->serial++);
        l.section = lower_strdup(a->section);
        l.label = lower_strdup(label);
        l.expr = lower_strdup(value);
        l.size = size;
        if (l.section == NULL || l.label == NULL || l.expr == NULL) {
            free(l.section);
            free(l.label);
            free(l.expr);
            return -1;
        }
        a->lits[a->lit_count++] = l;
    }
    text = lower_strdup(a->lits[i].label);
    if (text == NULL) {
        return -1;
    }
    free(in->operands[1].raw);
    in->operands[1].raw = text;
    return 0;
}

static int arm_names(const arg_vec_t *v, const char *name) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* One directive: 0 if it stays (as it is or as it was made), 2 if it
 * is dropped, 1 with a->why, -1 for want of memory. */
static int arm_directive(arm_lower_t *a, as_stmt_t *st) {
    as_directive_t *d = &st->u.directive;
    const char *n = d->name;
    const char *a0 = d->arg_count > 0 && d->args[0] != NULL ? d->args[0] : "";
    long long v;

    if (strcmp(n, ".text") == 0 || strcmp(n, ".data") == 0 || strcmp(n, ".bss") == 0) {
        return arm_set_section(a, n);
    }
    if (strcmp(n, ".section") == 0 && a0[0] != '\0') {
        return arm_set_section(a, a0);
    }
    if (strcmp(n, ".pushsection") == 0 && a0[0] != '\0') {
        if (a->depth == sizeof(a->stack) / sizeof(a->stack[0])) {
            a->why = "sections pushed too deep";
            return 1;
        }
        a->stack[a->depth] = lower_strdup(a->section);
        if (a->stack[a->depth] == NULL) {
            return -1;
        }
        a->depth++;
        return arm_set_section(a, a0);
    }
    if (strcmp(n, ".popsection") == 0) {
        if (a->depth != 0) {
            free(a->previous);
            a->previous = a->section;
            a->section = a->stack[--a->depth];
        }
        return 0;
    }
    if (strcmp(n, ".previous") == 0) {
        if (a->previous != NULL) {
            char *t = a->section;

            a->section = a->previous;
            a->previous = t;
        }
        return 0;
    }
    if (strcmp(n, ".ltorg") == 0 || strcmp(n, ".pool") == 0) {
        /* Its labels are before the pool.  (With labels the statement
         * lives on as them, and its file's name with it.) */
        if (st->label_count != 0) {
            as_stmt_t at = *st;

            if (arm_drop(a, st) != 0 || arm_flush(a, &at, a->section, 0) != 0) {
                return -1;
            }
        } else if (arm_flush(a, st, a->section, 0) != 0 || arm_drop(a, st) != 0) {
            return -1;
        }
        return 3;
    }
    if (strcmp(n, ".word") == 0) {
        return set_name(d, ".long");
    }
    if (strcmp(n, ".align") == 0) {
        /* A power of two; none written is four bytes. */
        if (d->arg_count == 0) {
            arg_vec_t args;

            memset(&args, 0, sizeof(args));
            if (vec_push_text(&args, "2") != 0) {
                return -1;
            }
            set_args(d, &args);
        }
        return set_name(d, ".p2align");
    }
    if (strcmp(n, ".inst") == 0 || strcmp(n, ".inst.n") == 0 || strcmp(n, ".inst.w") == 0) {
        arg_vec_t args;
        size_t i;

        if (d->arg_count == 0) {
            a->why = "an instruction's number";
            return 1;
        }
        if (!a->thumb) {
            if (n[5] != '\0') {
                a->why = "a width is of a Thumb instruction";
                return 1;
            }
            return set_name(d, ".long");
        }
        /* Thumb: a half word, or two with the high one first. */
        memset(&args, 0, sizeof(args));
        for (i = 0; i < d->arg_count; ++i) {
            int wide;

            if (as_arm_number(d->args[i], &v) != 0 || v < 0 || v > 0xffffffffll) {
                vec_free(&args);
                a->why = "a Thumb instruction is a number";
                return 1;
            }
            wide = n[5] == '\0' ? v > 0xffff : n[6] == 'w';
            if (!wide && v > 0xffff) {
                vec_free(&args);
                a->why = "a narrow instruction is 16 bits";
                return 1;
            }
            if ((wide && vec_push_num(&args, (unsigned long long)v >> 16) != 0) ||
                vec_push_num(&args, (unsigned long long)v & 0xffff) != 0) {
                vec_free(&args);
                return -1;
            }
        }
        set_args(d, &args);
        return set_name(d, ".short");
    }
    if (a->a64) {
        if (strcmp(n, ".arch") == 0 || strcmp(n, ".cpu") == 0 || strcmp(n, ".arch_extension") == 0) {
            if (a0[0] == '\0') {
                a->why = "which one";
                return 1;
            }
            return 2;
        }
        if (strcmp(n, ".arm") == 0 || strcmp(n, ".thumb") == 0 || strcmp(n, ".thumb_func") == 0 ||
            strcmp(n, ".syntax") == 0 || strcmp(n, ".fpu") == 0 || strcmp(n, ".eabi_attribute") == 0 ||
            strcmp(n, ".fnstart") == 0 || strcmp(n, ".fnend") == 0 || strcmp(n, ".code") == 0) {
            a->why = "a directive of ARM, and this is AArch64";
            return 1;
        }
        return 0;
    }
    if (strcmp(n, ".arm") == 0 || strcmp(n, ".thumb") == 0 || strcmp(n, ".code") == 0) {
        arg_vec_t none;

        if (n[1] == 'c') {
            if (strcmp(a0, "16") != 0 && strcmp(a0, "32") != 0) {
                a->why = "16 for Thumb or 32 for ARM";
                return 1;
            }
            a->thumb = a0[0] == '1';
        } else {
            if (d->arg_count != 0) {
                a->why = "it takes no argument";
                return 1;
            }
            a->thumb = n[1] == 't';
        }
        a->it_left = 0;
        memset(&none, 0, sizeof(none));
        set_args(d, &none);
        /* What follows stands where an instruction of its kind may:
         * at a multiple of four bytes, or of two. */
        if (push_directive(&a->out, st, ".p2align", a->thumb ? "1" : "2", "0") != 0) {
            return -1;
        }
        return set_name(d, a->thumb ? ".code16" : ".code32");
    }
    if (strcmp(n, ".thumb_func") == 0) {
        if (d->arg_count != 0) {
            a->why = "it takes no argument: the function is the next label";
            return 1;
        }
        a->thumb_func_next = 1;
        return 2;
    }
    if (strcmp(n, ".syntax") == 0) {
        /* Both orders of a mnemonic's parts are read whichever is
         * said, and an immediate has its `#` or has not. */
        if (strcasecmp(a0, "unified") != 0 && strcasecmp(a0, "divided") != 0) {
            a->why = "unified or divided";
            return 1;
        }
        return 2;
    }
    if (strcmp(n, ".arch") == 0 || strcmp(n, ".cpu") == 0 || strcmp(n, ".fpu") == 0 ||
        strcmp(n, ".arch_extension") == 0 || strcmp(n, ".object_arch") == 0) {
        /* Every instruction this assembler has is allowed already. */
        if (a0[0] == '\0' || d->arg_count != 1) {
            a->why = "which one";
            return 1;
        }
        return 2;
    }
    if (strcmp(n, ".eabi_attribute") == 0) {
        long long tag;
        const char *val = d->arg_count > 1 && d->args[1] != NULL ? d->args[1] : "";
        int is_string;

        if (d->arg_count != 2 || as_arm_number(a0, &tag) != 0 || tag <= 0) {
            a->why = "a tag's number and its value";
            return 1;
        }
        is_string = tag == 4 || tag == 5 || tag == 67 || (tag > 32 && (tag & 1) != 0);
        if (arm_attr_uleb(a, (unsigned long long)tag) != 0) {
            return -1;
        }
        if (is_string) {
            size_t len = strlen(val);
            size_t i;

            /* (With its quotes or without them, as the parser has it.) */
            if (len >= 2 && val[0] == '"' && val[len - 1] == '"') {
                val++;
                len -= 2;
            }
            if (len == 0) {
                a->why = "this tag's value is a string";
                return 1;
            }
            for (i = 0; i < len; ++i) {
                if (arm_attr_byte(a, (unsigned char)val[i]) != 0) {
                    return -1;
                }
            }
            return arm_attr_byte(a, 0) != 0 ? -1 : 2;
        }
        if (as_arm_number(val, &v) != 0 || v < 0) {
            a->why = "this tag's value is a number";
            return 1;
        }
        return arm_attr_uleb(a, (unsigned long long)v) != 0 ? -1 : 2;
    }
    if (strcmp(n, ".fnstart") == 0 || strcmp(n, ".fnend") == 0 || strcmp(n, ".cantunwind") == 0 ||
        strcmp(n, ".save") == 0 || strcmp(n, ".vsave") == 0 || strcmp(n, ".pad") == 0 || strcmp(n, ".setfp") == 0 ||
        strcmp(n, ".personality") == 0 || strcmp(n, ".personalityindex") == 0 || strcmp(n, ".handlerdata") == 0 ||
        strcmp(n, ".movsp") == 0 || strcmp(n, ".unwind_raw") == 0) {
        a->why = "the tables for unwinding ARM's stack are not made by this assembler";
        return 1;
    }
    if (strcmp(n, ".type") == 0 && d->arg_count >= 2 && d->args[1] != NULL &&
        (strcasecmp(d->args[1], "%function") == 0 || strcasecmp(d->args[1], "#function") == 0 ||
         strcasecmp(d->args[1], "function") == 0 || strcasecmp(d->args[1], "STT_FUNC") == 0)) {
        return vec_push_text(&a->functions, a0);
    }
    return 0;
}

static void arm_lower_free(arm_lower_t *a) {
    size_t i;

    free(a->section);
    free(a->previous);
    for (i = 0; i < a->depth; ++i) {
        free(a->stack[i]);
    }
    for (i = 0; i < a->lit_count; ++i) {
        free(a->lits[i].section);
        free(a->lits[i].label);
        free(a->lits[i].expr);
    }
    free(a->lits);
    free(a->attrs);
    vec_free(&a->thumb_labels);
    vec_free(&a->functions);
    vec_free(&a->thumb_funcs);
}

int as_arm_lower(as_parse_result_t *parsed, int a64, int thumb, char ***thumb_funcs, size_t *thumb_func_count,
                 char *errbuf, size_t errbuf_sz) {
    arm_lower_t a;
    as_stmt_t last;
    int rc = -1;
    size_t i;
    size_t j;

    memset(&a, 0, sizeof(a));
    memset(&last, 0, sizeof(last));
    a.a64 = a64;
    a.section = lower_strdup(".text");
    if (a.section == NULL) {
        goto oom;
    }
    if (thumb && !a64) {
        a.thumb = 1;
        if (push_directive(&a.out, &last, ".code16", NULL, NULL) != 0) {
            goto oom;
        }
    }
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        int r = 0;

        if (st->file != NULL && (last.file == NULL || strcmp(last.file, st->file) != 0)) {
            char *copy = lower_strdup(st->file);

            if (copy == NULL) {
                goto oom;
            }
            free(last.file);
            last.file = copy;
        }
        last.line = st->line;
        for (j = 0; j < st->label_count; ++j) {
            const char *name = st->labels[j].name;

            if (name == NULL) {
                continue;
            }
            if (a.thumb && vec_push_text(&a.thumb_labels, name) != 0) {
                goto oom;
            }
            if (a.thumb_func_next) {
                a.thumb_func_next = 0;
                if (vec_push_text(&a.thumb_funcs, name) != 0) {
                    goto oom;
                }
            }
        }
        if (st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL) {
            as_instruction_t *in = &st->u.instr;
            const char *m = in->mnemonic;
            size_t len = strlen(m);

            if (!a64 && len >= 2 && len <= 4 && strncasecmp(m, "it", 2) == 0 && strspn(m + 2, "teTE") == len - 2 &&
                in->operand_count == 1) {
                /* In ARM code it says what the conditions of the next
                 * instructions say themselves, and is no instruction. */
                if (!a.thumb) {
                    size_t k;

                    for (k = 0; k < in->operand_count; ++k) {
                        free(in->operands[k].raw);
                    }
                    free(in->operands);
                    free(in->mnemonic);
                    free(in->segment_override);
                    memset(in, 0, sizeof(*in));
                    st->kind = AS_STMT_LABEL_ONLY;
                    if (st->label_count == 0) {
                        free(st->file);
                        free(st->labels);
                        memset(st, 0, sizeof(*st));
                        continue;
                    }
                } else {
                    a.it_left = (unsigned)len;
                }
            } else if (a.it_left != 0) {
                in->arm_in_it = 1;
            }
            if (st->kind == AS_STMT_INSTRUCTION && strncasecmp(m, "ldr", 3) == 0 && in->operand_count == 2 &&
                in->operands[1].raw != NULL && in->operands[1].raw[0] == '=') {
                r = arm_load_constant(&a, st);
            }
            if (a.it_left != 0) {
                a.it_left--;
            }
        } else if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL) {
            r = arm_directive(&a, st);
            if (r == 3) {
                continue; /* (gone, and what it made put in its place) */
            }
            if (r == 2) {
                if (arm_drop(&a, st) != 0) {
                    goto oom;
                }
                continue;
            }
        }
        if (r < 0) {
            goto oom;
        }
        if (r > 0) {
            if (errbuf != NULL && errbuf_sz != 0) {
                snprintf(errbuf, errbuf_sz, "%s:%u: %s: %s", st->file != NULL ? st->file : "<input>", st->line,
                         st->kind == AS_STMT_DIRECTIVE ? st->u.directive.name : st->u.instr.mnemonic, a.why);
            }
            /* What was moved to the new list is the list's again. */
            goto restore;
        }
        if (stmt_vec_push(&a.out, st) != 0) {
            goto oom;
        }
    }
    /* What no .ltorg took is at the end of its section. */
    while (a.lit_count != 0) {
        char *section = lower_strdup(a.lits[0].section);

        if (section == NULL || arm_flush(&a, &last, section, 1) != 0) {
            free(section);
            goto oom;
        }
        free(section);
    }
    if (a.attr_len != 0) {
        char num[24];
        static const char aeabi[] = "aeabi";

        if (push_directive(&a.out, &last, ".pushsection", ".ARM.attributes", "\"\"") != 0) {
            goto oom;
        }
        /* The section's type is its own. */
        {
            as_directive_t *d = &a.out.items[a.out.count - 1].u.directive;
            arg_vec_t args;

            args.items = d->args;
            args.count = d->arg_count;
            args.cap = d->arg_cap;
            d->args = NULL;
            d->arg_count = 0;
            d->arg_cap = 0;
            if (vec_push_text(&args, "%0x70000003") != 0) {
                vec_free(&args);
                goto oom;
            }
            set_args(d, &args);
        }
        snprintf(num, sizeof(num), "%lu", (unsigned long)(4 + sizeof(aeabi) + 5 + a.attr_len));
        if (push_directive(&a.out, &last, ".byte", "0x41", NULL) != 0 ||
            push_directive(&a.out, &last, ".long", num, NULL) != 0) {
            goto oom;
        }
        for (j = 0; j < sizeof(aeabi); ++j) {
            snprintf(num, sizeof(num), "%u", (unsigned)aeabi[j]);
            if (push_directive(&a.out, &last, ".byte", num, NULL) != 0) {
                goto oom;
            }
        }
        snprintf(num, sizeof(num), "%lu", (unsigned long)(5 + a.attr_len));
        if (push_directive(&a.out, &last, ".byte", "1", NULL) != 0 ||
            push_directive(&a.out, &last, ".long", num, NULL) != 0) {
            goto oom;
        }
        for (j = 0; j < a.attr_len; ++j) {
            snprintf(num, sizeof(num), "%u", (unsigned)a.attrs[j]);
            if (push_directive(&a.out, &last, ".byte", num, NULL) != 0) {
                goto oom;
            }
        }
        if (push_directive(&a.out, &last, ".popsection", NULL, NULL) != 0) {
            goto oom;
        }
    }
    /* A label of Thumb code that is called a function is one of Thumb. */
    for (j = 0; j < a.thumb_labels.count; ++j) {
        if (arm_names(&a.functions, a.thumb_labels.items[j]) && !arm_names(&a.thumb_funcs, a.thumb_labels.items[j]) &&
            vec_push_text(&a.thumb_funcs, a.thumb_labels.items[j]) != 0) {
            goto oom;
        }
    }
    free(parsed->items);
    parsed->items = a.out.items;
    parsed->count = a.out.count;
    parsed->cap = a.out.cap;
    memset(&a.out, 0, sizeof(a.out));
    if (thumb_funcs != NULL && thumb_func_count != NULL) {
        *thumb_funcs = a.thumb_funcs.items;
        *thumb_func_count = a.thumb_funcs.count;
        memset(&a.thumb_funcs, 0, sizeof(a.thumb_funcs));
    }
    rc = 0;
    goto done;
oom:
    if (errbuf != NULL && errbuf_sz != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory");
    }
restore:
    /*
     * The statements were moved one by one to the new list; the old
     * list has those not reached yet.  The assembler is about to exit:
     * the old list is emptied so that nothing is freed twice.
     */
    parsed->count = 0;
    free(a.out.items);
done:
    free(last.file);
    arm_lower_free(&a);
    return rc;
}
