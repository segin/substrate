#ifndef _WCHAR_H
#define _WCHAR_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t wint_t;

#define WEOF ((wint_t)-1)

int wcwidth(wchar_t c);

#endif
