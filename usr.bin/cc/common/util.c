/*
 * util.c: string copies and string vectors, used throughout cc; the
 * binary16 encoding lowering and the emitters write `_Float16` data in.
 */

#include "cc_util.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * Copy the first @p n bytes of a string, which need not be terminated.
 *
 * @param s  the bytes
 * @param n  how many
 * @return   a malloc'd, terminated copy, or NULL if memory runs out
 */
char *cc_strndup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);

    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/**
 * Copy a string.
 *
 * @param s  the string, or NULL
 * @return   a malloc'd copy, or NULL if @p s is NULL or memory runs out
 */
char *cc_strdup(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    return cc_strndup(s, strlen(s));
}

/**
 * Append a copy of a string.
 *
 * @param v  the vector
 * @param s  the string; the vector keeps its own copy
 * @return   0, or -1 if memory runs out (the vector is unchanged)
 */
int cc_strvec_push(cc_strvec_t *v, const char *s) {
    char *copy;

    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        char **next = (char **)realloc(v->items, ncap * sizeof(*next));

        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    copy = cc_strdup(s);
    if (copy == NULL) {
        return -1;
    }
    v->items[v->count++] = copy;
    return 0;
}

/**
 * Whether the vector holds a string equal to @p s.
 *
 * @param v  the vector, or NULL
 * @param s  the string, or NULL
 * @return   1 if it does, 0 if not or if either is NULL
 */
int cc_strvec_contains(const cc_strvec_t *v, const char *s) {
    size_t i;

    if (v == NULL || s == NULL) {
        return 0;
    }
    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i], s) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Free every string and the array, leaving an empty vector.
 *
 * @param v  the vector
 */
void cc_strvec_free(cc_strvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

unsigned short cc_float16_bits(double value) {
    union {
        double d;
        uint64_t u;
    } cvt;
    unsigned sign;
    int exp;
    uint64_t mant;
    uint64_t q;
    uint64_t rem;
    uint64_t half;
    int shift;

    cvt.d = value;
    sign = (unsigned)(cvt.u >> 63) << 15;
    exp = (int)((cvt.u >> 52) & 0x7ff);
    mant = cvt.u & ((1ULL << 52) - 1);
    if (exp == 0x7ff) {
        /* infinity, or a NaN: its top payload bits, never all zero */
        unsigned payload = (unsigned)(mant >> 42);

        return (unsigned short)(sign | 0x7c00 | (mant != 0 && payload == 0 ? 1 : payload));
    }
    if (exp == 0) {
        /* zero, or a double subnormal: far below the smallest half */
        return (unsigned short)sign;
    }
    mant |= 1ULL << 52;      /* the 53-bit significand */
    exp -= 1023 - 15;        /* the half's biased exponent */
    /* a normal half keeps 11 significant bits; a subnormal fewer, one less per step below 1 */
    shift = 42 + (exp < 1 ? 1 - exp : 0);
    if (shift > 63) {
        return (unsigned short)sign;
    }
    q = mant >> shift;
    rem = mant & ((1ULL << shift) - 1);
    half = 1ULL << (shift - 1);
    if (rem > half || (rem == half && (q & 1) != 0)) {
        q++;
    }
    if (exp < 1) {
        /* a subnormal, or the smallest normal (0x400) when it rounds up */
        return (unsigned short)(sign | (unsigned)q);
    }
    if (q == 0x800) {
        /* the rounding carried out of the significand */
        q = 0x400;
        exp++;
    }
    if (exp >= 0x1f) {
        return (unsigned short)(sign | 0x7c00);
    }
    return (unsigned short)(sign | ((unsigned)exp << 10) | (unsigned)(q & 0x3ff));
}
