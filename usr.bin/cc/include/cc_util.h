#ifndef CC_UTIL_H
#define CC_UTIL_H

/*
 * Small helpers every part of the compiler uses: string copies and a
 * growable vector of owned strings.
 */

#include <stddef.h>

/** A growable array of strings the vector owns. */
typedef struct {
    char **items; /**< the strings, each malloc'd */
    size_t count; /**< how many there are */
    size_t cap;   /**< how many fit before the array must grow */
} cc_strvec_t;

/**
 * Copy a string.
 *
 * @param s  the string, or NULL
 * @return   a malloc'd copy, or NULL if @p s is NULL or memory runs out
 */
char *cc_strdup(const char *s);

/**
 * Copy the first @p n bytes of a string, which need not be terminated.
 *
 * @param s  the bytes
 * @param n  how many
 * @return   a malloc'd, terminated copy, or NULL if memory runs out
 */
char *cc_strndup(const char *s, size_t n);

/**
 * Append a copy of a string.
 *
 * @param v  the vector
 * @param s  the string; the vector keeps its own copy
 * @return   0, or -1 if memory runs out (the vector is unchanged)
 */
int cc_strvec_push(cc_strvec_t *v, const char *s);

/**
 * Whether the vector holds a string equal to @p s.
 *
 * @param v  the vector, or NULL
 * @param s  the string, or NULL
 * @return   1 if it does, 0 if not or if either is NULL
 */
int cc_strvec_contains(const cc_strvec_t *v, const char *s);

/**
 * Free every string and the array, leaving an empty vector.
 *
 * @param v  the vector
 */
void cc_strvec_free(cc_strvec_t *v);

/**
 * The IEEE 754 binary16 encoding of @p value, as `_Float16` holds it:
 * rounded to nearest, ties to even; overflow to infinity; values below
 * 2^-14 as subnormals; infinities and NaNs (quiet) kept.
 *
 * @param value  the value
 * @return       its 16 bits
 */
unsigned short cc_float16_bits(double value);

#endif
