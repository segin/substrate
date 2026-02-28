#include "demangle_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEMANGLE_BUF_INITIAL_CAP 256u

int
demangle_buf_reserve(demangle_buf_t *buf, size_t extra)
{
    size_t need;
    size_t cap;
    char *next;

    if (buf == NULL) {
        return -1;
    }

    if (extra > (size_t)-1 - buf->len - 1u) {
        return -1;
    }

    need = buf->len + extra + 1u;
    if (need <= buf->cap) {
        return 0;
    }

    cap = (buf->cap == 0u) ? DEMANGLE_BUF_INITIAL_CAP : buf->cap;
    while (cap < need) {
        size_t doubled = cap << 1;
        if (doubled < cap) {
            return -1;
        }
        cap = doubled;
    }

    next = (char *)realloc(buf->data, cap);
    if (next == NULL) {
        return -1;
    }

    buf->data = next;
    buf->cap = cap;
    return 0;
}

int
demangle_buf_append(demangle_buf_t *buf, const char *s, size_t n)
{
    if (buf == NULL || s == NULL) {
        return -1;
    }

    if (demangle_buf_reserve(buf, n) != 0) {
        return -1;
    }

    if (n > 0u) {
        memcpy(buf->data + buf->len, s, n);
        buf->len += n;
    }

    buf->data[buf->len] = '\0';
    return 0;
}

int
demangle_buf_appendc(demangle_buf_t *buf, char ch)
{
    if (buf == NULL) {
        return -1;
    }

    if (demangle_buf_reserve(buf, 1u) != 0) {
        return -1;
    }

    buf->data[buf->len++] = ch;
    buf->data[buf->len] = '\0';
    return 0;
}

int
demangle_buf_printf(demangle_buf_t *buf, const char *fmt, ...)
{
    va_list ap;
    va_list ap_copy;
    int need;

    if (buf == NULL || fmt == NULL) {
        return -1;
    }

    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) {
        va_end(ap_copy);
        return -1;
    }

    if (demangle_buf_reserve(buf, (size_t)need) != 0) {
        va_end(ap_copy);
        return -1;
    }

    if (vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, ap_copy) != need) {
        va_end(ap_copy);
        return -1;
    }
    va_end(ap_copy);

    buf->len += (size_t)need;
    return 0;
}

char *
demangle_buf_take(demangle_buf_t *buf)
{
    char *ret;

    if (buf == NULL || buf->data == NULL) {
        return NULL;
    }

    buf->data[buf->len] = '\0';
    ret = buf->data;
    buf->data = NULL;
    buf->len = 0u;
    buf->cap = 0u;
    return ret;
}

void
demangle_buf_destroy(demangle_buf_t *buf)
{
    if (buf == NULL) {
        return;
    }

    free(buf->data);
    buf->data = NULL;
    buf->len = 0u;
    buf->cap = 0u;
}
