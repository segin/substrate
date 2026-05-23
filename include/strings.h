#ifndef _STRINGS_H
#define _STRINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
int ffs(int i);
int ffsl(long i);
int ffsll(long long i);

/* BSD legacy: map to string.h equivalents */
void bcopy(const void *src, void *dst, size_t n);
#ifndef bzero   /* X11's <X11/Xfuncs.h> redefines bzero as a function-like macro */
void bzero(void *s, size_t n);
#endif
int  bcmp(const void *s1, const void *s2, size_t n);
/* xorgproto's <X11/Xos.h> defines index / rindex as function-like
 * macros pointing to strchr / strrchr.  If <X11/Xos.h> was included
 * before us, declaring `char *index(...)` here parses as garbage
 * because the macro expands inside the prototype.  Undef first so
 * either order works. */
#ifdef index
#undef index
#endif
char *index(const char *s, int c);
#ifdef rindex
#undef rindex
#endif
char *rindex(const char *s, int c);

#ifdef __cplusplus
}
#endif
#endif /* _STRINGS_H */
