#ifndef _WCHAR_H
#define _WCHAR_H

#ifdef __cplusplus
extern "C" {
#endif

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
size_t mbrtowc(wchar_t *__restrict pwc, const char *__restrict s, size_t n, mbstate_t *__restrict ps);
size_t mbrlen(const char *__restrict s, size_t n, mbstate_t *__restrict ps);
size_t wcrtomb(char *__restrict s, wchar_t wc, mbstate_t *__restrict ps);
size_t mbsrtowcs(wchar_t *__restrict dst, const char **__restrict src, size_t len, mbstate_t *__restrict ps);
size_t wcsrtombs(char *__restrict dst, const wchar_t **__restrict src, size_t len, mbstate_t *__restrict ps);

#ifdef __cplusplus
}
#endif
#endif

/* wide-char string functions — UTF-32 codepoint based, since wchar_t
 * on substrate is 32-bit Unicode codepoint.  Substrate's wchar.h had
 * only the multibyte stuff; libstdc++ <cwchar> requires the full
 * suite. */
size_t wcslen(const wchar_t *s);
size_t wcsnlen(const wchar_t *s, size_t maxlen);
int    wcscmp(const wchar_t *s1, const wchar_t *s2);
int    wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle);
wchar_t *wcscpy(wchar_t *__restrict dst, const wchar_t *__restrict src);
wchar_t *wcsncpy(wchar_t *__restrict dst, const wchar_t *__restrict src, size_t n);
wchar_t *wcscat(wchar_t *__restrict dst, const wchar_t *__restrict src);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);
int      wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemcpy(wchar_t *__restrict dst, const wchar_t *__restrict src, size_t n);
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
long     wcstol(const wchar_t *__restrict nptr, wchar_t **__restrict endptr, int base);
long long wcstoll(const wchar_t *__restrict nptr, wchar_t **__restrict endptr, int base);
unsigned long      wcstoul(const wchar_t *__restrict nptr, wchar_t **__restrict endptr, int base);
unsigned long long wcstoull(const wchar_t *__restrict nptr, wchar_t **__restrict endptr, int base);
double             wcstod(const wchar_t *__restrict nptr, wchar_t **__restrict endptr);
float              wcstof(const wchar_t *__restrict nptr, wchar_t **__restrict endptr);
long double        wcstold(const wchar_t *__restrict nptr, wchar_t **__restrict endptr);

/* wide-char stdio.  Most operations are stubs returning WEOF — substrate
 * doesn't yet have wide-char I/O modes on FILE streams.  These exist so
 * libstdc++'s <cwchar> can `using ::name;` cleanly. */
#include <stdio.h>
struct tm;
typedef unsigned int wint_t;
#define WEOF ((wint_t)-1)

wint_t btowc(int c);
int    wctob(wint_t c);
wint_t fgetwc(FILE *stream);
wchar_t *fgetws(wchar_t *ws, int n, FILE *stream);
wint_t fputwc(wchar_t wc, FILE *stream);
int    fputws(const wchar_t *ws, FILE *stream);
int    fwide(FILE *stream, int mode);
int    fwprintf(FILE *stream, const wchar_t *format, ...);
int    fwscanf(FILE *stream, const wchar_t *format, ...);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t putwc(wchar_t wc, FILE *stream);
wint_t putwchar(wchar_t wc);
int    swprintf(wchar_t *s, size_t n, const wchar_t *format, ...);
int    swscanf(const wchar_t *s, const wchar_t *format, ...);
wint_t ungetwc(wint_t c, FILE *stream);
int    vfwprintf(FILE *stream, const wchar_t *format, __builtin_va_list args);
int    vfwscanf(FILE *stream, const wchar_t *format, __builtin_va_list args);
int    vswprintf(wchar_t *s, size_t n, const wchar_t *format, __builtin_va_list args);
int    vswscanf(const wchar_t *s, const wchar_t *format, __builtin_va_list args);
int    vwprintf(const wchar_t *format, __builtin_va_list args);
int    vwscanf(const wchar_t *format, __builtin_va_list args);
int    wcscoll(const wchar_t *s1, const wchar_t *s2);
size_t wcsftime(wchar_t *s, size_t maxsize, const wchar_t *format, const struct tm *timeptr);
size_t wcsxfrm(wchar_t *dst, const wchar_t *src, size_t n);
int    wprintf(const wchar_t *format, ...);
int    wscanf(const wchar_t *format, ...);

int      mbsinit(const mbstate_t *ps);
size_t   wcscspn(const wchar_t *s, const wchar_t *reject);
size_t   wcsspn(const wchar_t *s, const wchar_t *accept);
wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wcstok(wchar_t *__restrict s, const wchar_t *__restrict delim, wchar_t **__restrict ptr);
wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept);
