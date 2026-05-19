#ifndef _STRING_H
#define _STRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

/*
 * When _SUBSTRATE_FORTIFY is defined, strcpy/strcat are marked deprecated
 * to steer callers toward the bounds-checked strlcpy/strlcat alternatives.
 */
#if defined(__GNUC__) && defined(_SUBSTRATE_FORTIFY)
#define _SUBSTRATE_DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#define _SUBSTRATE_DEPRECATED(msg)
#endif

_SUBSTRATE_DEPRECATED("use strlcpy()")
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
_SUBSTRATE_DEPRECATED("use strlcat()")
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcoll(const char *s1, const char *s2);
size_t strxfrm(char *dest, const char *src, size_t n);

/* BSD/GNU extension: case-insensitive compare also visible via
 * <string.h>.  Canonical home is <strings.h> per POSIX but most
 * userland (binutils gas/as.c, autoconf, many old packages) just
 * #include <string.h> and expect these to be declared. */
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strcasestr(const char *haystack, const char *needle);
char *strsep(char **stringp, const char *delim);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);
char *strpbrk(const char *s1, const char *s2);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
char *strerror(int errnum);
char *geterror(int errnum);
int   strerror_r(int errnum, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
#endif
