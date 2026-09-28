#ifndef SLEX_UTIL_H
#define SLEX_UTIL_H

#include <stddef.h>

/* Allocation wrappers: on failure they print the reason and exit(1). */
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);

/* A growable NUL-terminated byte buffer. */
struct strbuf {
    char *buf;
    size_t len;
    size_t cap;
};

void strbuf_putc(struct strbuf *sb, int c);

#endif
