/*
 * util.c: string copies and string vectors, used throughout cc.
 */

#include "cc_util.h"

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
