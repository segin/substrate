#include "as_lower.h"

#include "as_data.h"
#include "as_expr.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static int lower_one(as_directive_t *d, unsigned address_bytes, const char **why) {
    arg_vec_t v;
    const lower_unit_t *unit;
    size_t i;
    int rc;

    memset(&v, 0, sizeof(v));
    *why = "an argument is not a number";

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

            snprintf(said, sizeof(said), "\"size (%lld) out of range, ignored\"", size);
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

int as_directives_lower(as_parse_result_t *parsed, unsigned address_bytes, char *errbuf, size_t errbuf_sz) {
    size_t i;

    if (parsed == NULL) {
        return -1;
    }
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        const char *why = "";
        int rc;

        if (st->kind != AS_STMT_DIRECTIVE || st->u.directive.name == NULL) {
            continue;
        }
        rc = lower_one(&st->u.directive, address_bytes, &why);
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
    return lower_local_commons(parsed, errbuf, errbuf_sz);
}
