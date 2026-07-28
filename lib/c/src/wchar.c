#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
wint_t btowc(int c)            { return (c >= 0 && c < 128) ? (wint_t)c : WEOF; }
int    wctob(wint_t c)         { return (c < 128) ? (int)c : EOF; }
wint_t fgetwc(FILE *s)         { (void)s; return WEOF; }
wchar_t *fgetws(wchar_t *w, int n, FILE *s) { (void)w; (void)n; (void)s; return NULL; }
wint_t fputwc(wchar_t c, FILE *s) { (void)c; (void)s; return WEOF; }
int    fputws(const wchar_t *w, FILE *s) { (void)w; (void)s; return -1; }
int    fwide(FILE *s, int m)   { (void)s; (void)m; return 0; }
wint_t getwc(FILE *s)          { return fgetwc(s); }
wint_t getwchar(void)          { return fgetwc(stdin); }
wint_t putwc(wchar_t c, FILE *s) { return fputwc(c, s); }
wint_t putwchar(wchar_t c)     { return fputwc(c, stdout); }
wint_t ungetwc(wint_t c, FILE *s) { (void)c; (void)s; return WEOF; }
int    fwprintf(FILE *s, const wchar_t *f, ...) { (void)s; (void)f; return -1; }
int    fwscanf(FILE *s, const wchar_t *f, ...)  { (void)s; (void)f; return EOF; }
int    swprintf(wchar_t *s, size_t n, const wchar_t *f, ...) { (void)s; (void)n; (void)f; return -1; }
int    swscanf(const wchar_t *s, const wchar_t *f, ...) { (void)s; (void)f; return EOF; }
int    wprintf(const wchar_t *f, ...) { (void)f; return -1; }
int    wscanf(const wchar_t *f, ...)  { (void)f; return EOF; }
int    vfwprintf(FILE *s, const wchar_t *f, va_list a) { (void)s; (void)f; (void)a; return -1; }
int    vfwscanf(FILE *s, const wchar_t *f, va_list a)  { (void)s; (void)f; (void)a; return EOF; }
int    vswprintf(wchar_t *s, size_t n, const wchar_t *f, va_list a) { (void)s; (void)n; (void)f; (void)a; return -1; }
int    vswscanf(const wchar_t *s, const wchar_t *f, va_list a)  { (void)s; (void)f; (void)a; return EOF; }
int    vwprintf(const wchar_t *f, va_list a) { (void)f; (void)a; return -1; }
int    vwscanf(const wchar_t *f, va_list a)  { (void)f; (void)a; return EOF; }
int    wcscoll(const wchar_t *s1, const wchar_t *s2) { return wcscmp(s1, s2); }
size_t wcsxfrm(wchar_t *dst, const wchar_t *src, size_t n) {
    size_t l = wcslen(src);
    if (dst && l < n) wcscpy(dst, src);
    return l;
}
size_t wcsftime(wchar_t *s, size_t m, const wchar_t *f, const struct tm *t) { (void)s; (void)m; (void)f; (void)t; return 0; }

int mbsinit(const mbstate_t *ps) { (void)ps; return 1; }
size_t wcscspn(const wchar_t *s, const wchar_t *reject) {
    size_t n = 0;
    while (s[n] && !wcschr(reject, s[n])) n++;
    return n;
}
size_t wcsspn(const wchar_t *s, const wchar_t *accept) {
    size_t n = 0;
    while (s[n] && wcschr(accept, s[n])) n++;
    return n;
}
wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n) {
    /* Append at most n cells from src, then always terminate WITHIN
     * the reserved space.  Mirror strncat: stop on the source NUL or
     * after n cells, then write a single terminating 0.  The previous
     * version wrote the terminator one cell PAST the n-cell budget on
     * full truncation (off-by-one heap overflow). */
    wchar_t *d = dst; while (*d) d++;
    size_t i = 0;
    for (; i < n && src[i]; i++) d[i] = src[i];
    d[i] = 0;
    return dst;
}
wchar_t *wcstok(wchar_t *s, const wchar_t *delim, wchar_t **ptr) {
    if (!s) s = *ptr;
    if (!s) return NULL;
    while (*s && wcschr(delim, *s)) s++;
    if (!*s) { *ptr = NULL; return NULL; }
    wchar_t *tok = s;
    while (*s && !wcschr(delim, *s)) s++;
    if (*s) { *s = 0; *ptr = s + 1; } else { *ptr = NULL; }
    return tok;
}
wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept) {
    for (; *s; s++) if (wcschr(accept, *s)) return (wchar_t *)s;
    return NULL;
}
