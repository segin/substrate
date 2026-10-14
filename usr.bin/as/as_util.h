#ifndef AS_UTIL_H
#define AS_UTIL_H

#include <stddef.h>
#include <string.h>

/*
 * strcmp(), with the first characters compared where it is called.  A
 * mnemonic is looked for by going down lists of names, a thousand
 * comparisons and more for one statement, and nearly every one of them
 * is settled by its first character: the call was most of what they
 * cost.  And the second: a directive's name is looked for so too, and
 * every one begins with its dot.  What it returns is strcmp()'s in its
 * sign, which is all that is said of strcmp()'s.
 */
static inline int as_strcmp(const char *a, const char *b) {
    unsigned char ca = (unsigned char)a[0];
    unsigned char cb = (unsigned char)b[0];

    if (ca != cb) {
        return ca < cb ? -1 : 1;
    }
    if (ca == '\0') {
        return 0;
    }
    ca = (unsigned char)a[1];
    cb = (unsigned char)b[1];
    return ca != cb ? (ca < cb ? -1 : 1) : strcmp(a, b);
}

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
 * A message whose format begins "%s:%u:" is about a statement, and its
 * first argument is that statement's `file`: the pointer itself, which
 * every statement has its own of.  `fn` is called with it for each such
 * message, so that what a statement carries beyond its file and line --
 * which macro's expansion it is of -- can be found for the message.
 * The pointer is to be compared and not read.  NULL for no one.
 */
typedef void (*as_err_about_fn)(const char *file, void *arg);
void as_err_about(as_err_about_fn fn, void *arg);

/*
 * The same for a context that carries the buffer as `errbuf` and
 * `errbuf_sz`, whichever context it is; a NULL one did not ask either.
 */
#define set_err(ctx, ...)                                                                                              \
    ((ctx) != NULL ? as_errf((ctx)->errbuf, (ctx)->errbuf_sz, __VA_ARGS__) : as_errf(NULL, 0, __VA_ARGS__))

#endif
