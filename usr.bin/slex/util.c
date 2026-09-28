/*
 * util.c - checked allocation and growable buffers for slex.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void *xmalloc(size_t size) {
    void *ptr = malloc(size);

    if (!ptr && size != 0) {
        perror("malloc");
        exit(1);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);

    if (!new_ptr && size != 0) {
        perror("realloc");
        exit(1);
    }
    return new_ptr;
}

char *xstrdup(const char *s) {
    char *new_s = strdup(s);

    if (!new_s) {
        perror("strdup");
        exit(1);
    }
    return new_s;
}

void strbuf_putc(struct strbuf *sb, int c) {
    if (sb->len + 2 > sb->cap) {
        sb->cap = sb->cap == 0 ? 1024 : sb->cap * 2;
        sb->buf = xrealloc(sb->buf, sb->cap);
    }
    sb->buf[sb->len++] = (char)c;
    sb->buf[sb->len] = '\0';
}
