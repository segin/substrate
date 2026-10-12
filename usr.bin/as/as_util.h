#ifndef AS_UTIL_H
#define AS_UTIL_H

#include <stddef.h>

/* A copy of `s` in memory of its own, or NULL: for a NULL, or for want of memory. */
char *xstrdup(const char *s);

/* Whether two names are one but for the case of their ASCII letters. */
int streq_ci(const char *a, const char *b);

/* A copy of `s` without the white space at either end of it, or NULL as xstrdup. */
char *trim_copy(const char *s);

/*
 * Write a message where a caller left room for one, and return -1, which
 * is what the caller is about to.  A NULL buffer, or one of no size, is a
 * caller that did not ask.
 */
int as_errf(char *errbuf, size_t errbuf_sz, const char *fmt, ...);

/*
 * The same for a context that carries the buffer as `errbuf` and
 * `errbuf_sz`, whichever context it is; a NULL one did not ask either.
 */
#define set_err(ctx, ...)                                                                                              \
    ((ctx) != NULL ? as_errf((ctx)->errbuf, (ctx)->errbuf_sz, __VA_ARGS__) : as_errf(NULL, 0, __VA_ARGS__))

#endif
