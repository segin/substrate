#ifndef _WCHAR_H
#define _WCHAR_H

#include <stddef.h>
#include <stdint.h>

/* wchar_t is provided by stddef.h */
typedef uint32_t wint_t;
typedef struct {
	unsigned int __count;
	unsigned int __value;
} mbstate_t;

#define WEOF ((wint_t)-1)

int wcwidth(wchar_t c);
size_t mbrtowc(wchar_t *restrict pwc, const char *restrict s, size_t n, mbstate_t *restrict ps);

#endif
