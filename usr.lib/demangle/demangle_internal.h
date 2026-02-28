#ifndef DEMANGLE_INTERNAL_H
#define DEMANGLE_INTERNAL_H

#include <stddef.h>

typedef struct demangle_buf {
    char *data;
    size_t len;
    size_t cap;
} demangle_buf_t;

int demangle_buf_reserve(demangle_buf_t *buf, size_t extra);
int demangle_buf_append(demangle_buf_t *buf, const char *s, size_t n);
int demangle_buf_appendc(demangle_buf_t *buf, char ch);
int demangle_buf_printf(demangle_buf_t *buf, const char *fmt, ...);
char *demangle_buf_take(demangle_buf_t *buf);
void demangle_buf_destroy(demangle_buf_t *buf);

char *demangle_itanium(const char *mangled, int options);
char *demangle_rust(const char *mangled, int options);
char *demangle_dlang(const char *mangled, int options);

#endif /* DEMANGLE_INTERNAL_H */
