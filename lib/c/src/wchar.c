#include <wchar.h>
#include <time.h>
#include <stdlib.h>
#include <stddef.h>
#include <stddef.h>

size_t mbrtowc(wchar_t *restrict pwc, const char *restrict s, size_t n, mbstate_t *restrict ps) {
    (void)ps;

    if (s == NULL)
        return 0;
    if (n == 0)
        return (size_t)-2;

    unsigned char c0 = (unsigned char)s[0];
    if (c0 == '\0') {
        if (pwc)
            *pwc = 0;
        return 0;
    }

    if (c0 < 0x80) {
        if (pwc)
            *pwc = (wchar_t)c0;
        return 1;
    }

    if ((c0 & 0xE0) == 0xC0) {
        if (n < 2)
            return (size_t)-2;
        unsigned char c1 = (unsigned char)s[1];
        if ((c1 & 0xC0) != 0x80)
            return (size_t)-1;
        wchar_t cp = (wchar_t)(((c0 & 0x1F) << 6) | (c1 & 0x3F));
        if (cp < 0x80)
            return (size_t)-1; /* reject overlong */
        if (pwc)
            *pwc = cp;
        return 2;
    }

    if ((c0 & 0xF0) == 0xE0) {
        if (n < 3)
            return (size_t)-2;
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
            return (size_t)-1;
        wchar_t cp = (wchar_t)(((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F));
        if (cp < 0x800)
            return (size_t)-1; /* reject overlong */
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return (size_t)-1; /* reject surrogates */
        if (pwc)
            *pwc = cp;
        return 3;
    }

    if ((c0 & 0xF8) == 0xF0) {
        if (n < 4)
            return (size_t)-2;
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        unsigned char c3 = (unsigned char)s[3];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
            return (size_t)-1;
        wchar_t cp = (wchar_t)(((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) |
                             ((c2 & 0x3F) << 6) | (c3 & 0x3F));
        if (cp < 0x10000)
            return (size_t)-1; /* reject overlong */
        if (cp > 0x10FFFF)
            return (size_t)-1; /* reject beyond Unicode range */
        if (pwc)
            *pwc = cp;
        return 4;
    }

    return (size_t)-1;
}

/*
 * C89 stateless multibyte conversion wrappers around mbrtowc.
 * Substrate's encoding is UTF-8 unconditionally, so there is no
 * shift state and the (s == NULL) state-reset query always returns
 * zero.  mbtowc returns the byte count, 0 for NUL, or -1 on error
 * (matching C89's int-valued contract).
 */
int mbtowc(wchar_t *pwc, const char *s, size_t n) {
    if (s == NULL)
        return 0;
    if (n == 0)
        return -1;
    size_t r = mbrtowc(pwc, s, n, NULL);
    if (r == (size_t)-1 || r == (size_t)-2)
        return -1;
    return (int)r;
}

int wctomb(char *s, wchar_t wc) {
    if (s == NULL)
        return 0;
    if (wc < 0)
        return -1;
    if (wc < 0x80) {
        s[0] = (char)wc;
        return 1;
    }
    if (wc < 0x800) {
        s[0] = (char)(0xC0 | (wc >> 6));
        s[1] = (char)(0x80 | (wc & 0x3F));
        return 2;
    }
    if (wc >= 0xD800 && wc <= 0xDFFF)
        return -1;                              /* surrogate */
    if (wc < 0x10000) {
        s[0] = (char)(0xE0 | (wc >> 12));
        s[1] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[2] = (char)(0x80 | (wc & 0x3F));
        return 3;
    }
    if (wc < 0x110000) {
        s[0] = (char)(0xF0 | (wc >> 18));
        s[1] = (char)(0x80 | ((wc >> 12) & 0x3F));
        s[2] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[3] = (char)(0x80 | (wc & 0x3F));
        return 4;
    }
    return -1;
}

int mblen(const char *s, size_t n) {
    return mbtowc(NULL, s, n);
}

/*
 * mbstowcs / wcstombs — bulk multibyte-string ↔ wchar-array conversion.
 * C89 require these; C++ libstdc++ exposes them as ::mbstowcs etc.
 * Built on top of mbtowc / wctomb so they share the UTF-8 logic.
 */
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n) {
    size_t produced = 0;
    while (n == 0 || produced < n) {
        wchar_t wc;
        int r = mbtowc(&wc, s, 4);
        if (r < 0) return (size_t)-1;
        if (pwcs) pwcs[produced] = wc;
        if (wc == 0) return produced;
        produced++;
        s += r;
    }
    return produced;
}

size_t wcstombs(char *s, const wchar_t *pwcs, size_t n) {
    size_t produced = 0;
    char buf[4];
    while (*pwcs != 0) {
        int r = wctomb(buf, *pwcs);
        if (r < 0) return (size_t)-1;
        if (s) {
            if (produced + (size_t)r > n) return produced;
            for (int i = 0; i < r; i++) s[produced + i] = buf[i];
        }
        produced += (size_t)r;
        pwcs++;
    }
    if (s && produced < n) s[produced] = '\0';
    return produced;
}

int wcwidth(wchar_t c) {
    if (c == 0)
        return 0;
    if (c < 32 || (c >= 0x7F && c < 0xA0))
        return -1;
    // Simple implementation: assume mostly 1, except for some ranges?
    // For now, let's assume 1 for all printable characters.
    // Real implementation needs a table.
    // POSIX says wcwidth returns -1 if c is not printable.

    // Minimal UTF-8 support:
    // This is very naive.
    return 1;
}

/* mbrlen: how many bytes does the next char take?  Substrate is
 * UTF-8 so we just dispatch through mbrtowc. */
size_t mbrlen(const char *s, size_t n, mbstate_t *ps) {
    (void)ps;
    return mbrtowc(NULL, s, n, NULL);
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps) {
    (void)ps;
    if (!s) return 1;
    int r = wctomb(s, wc);
    if (r < 0) return (size_t)-1;
    return (size_t)r;
}

size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps) {
    (void)ps;
    size_t produced = 0;
    while (len == 0 || produced < len) {
        wchar_t wc;
        size_t r = mbrtowc(&wc, *src, 4, NULL);
        if (r == (size_t)-1 || r == (size_t)-2) return (size_t)-1;
        if (dst) dst[produced] = wc;
        if (wc == 0) {
            if (dst) *src = NULL;
            return produced;
        }
        produced++;
        *src += r;
    }
    return produced;
}

size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps) {
    (void)ps;
    size_t produced = 0;
    char buf[4];
    while (**src) {
        int r = wctomb(buf, **src);
        if (r < 0) return (size_t)-1;
        if (dst) {
            if (produced + (size_t)r > len) return produced;
            for (int i = 0; i < r; i++) dst[produced + i] = buf[i];
        }
        produced += (size_t)r;
        (*src)++;
    }
    if (dst && produced < len) {
        dst[produced] = '\0';
        *src = NULL;
    }
    return produced;
}

size_t wcslen(const wchar_t *s) {
    const wchar_t *p = s;
    while (*p) p++;
    return p - s;
}
size_t wcsnlen(const wchar_t *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}
int wcscmp(const wchar_t *s1, const wchar_t *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    /* Compare as unsigned code points and return -1/0/1.  Subtracting
     * two 32-bit wchar_t as int can overflow and flip the sign (e.g.
     * 0x10FFFF vs a small value), so don't return the difference. */
    unsigned u1 = (unsigned)*s1, u2 = (unsigned)*s2;
    return (u1 < u2) ? -1 : (u1 > u2) ? 1 : 0;
}
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n) {
    if (n == 0) return 0;
    while (--n && *s1 && *s1 == *s2) { s1++; s2++; }
    unsigned u1 = (unsigned)*s1, u2 = (unsigned)*s2;
    return (u1 < u2) ? -1 : (u1 > u2) ? 1 : 0;
}
wchar_t *wcschr(const wchar_t *s, wchar_t c) {
    do { if (*s == c) return (wchar_t *)s; } while (*s++);
    return NULL;
}
wchar_t *wcsrchr(const wchar_t *s, wchar_t c) {
    const wchar_t *last = NULL;
    do { if (*s == c) last = s; } while (*s++);
    return (wchar_t *)last;
}
wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle) {
    if (!*needle) return (wchar_t *)haystack;
    for (; *haystack; haystack++) {
        const wchar_t *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (wchar_t *)haystack;
    }
    return NULL;
}
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src) {
    wchar_t *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}
wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n) {
    /* Mirror strncpy: copy at most n cells, stopping at the source
     * NUL, then zero-fill the remainder of the n cells.  On pure
     * truncation (src longer than n) the result is NOT NUL-terminated,
     * which is correct.  The previous version mis-decremented n in the
     * loop guard and used a wrong d[-1]==0 pad condition, both
     * over- and under-filling the destination. */
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}
wchar_t *wcscat(wchar_t *dst, const wchar_t *src) {
    wchar_t *d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dst;
}
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n) {
    while (n--) { if (*s == c) return (wchar_t *)s; s++; }
    return NULL;
}
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n) {
    while (n--) { if (*s1 != *s2) return (int)(*s1) - (int)(*s2); s1++; s2++; }
    return 0;
}
wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n) {
    wchar_t *d = dst;
    while (n--) *d++ = *src++;
    return dst;
}
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n) {
    if (dst < src) {
        wchar_t *d = dst; while (n--) *d++ = *src++;
    } else if (dst > src) {
        wchar_t *d = dst + n; src += n; while (n--) *--d = *--src;
    }
    return dst;
}
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n) {
    wchar_t *p = s; while (n--) *p++ = c;
    return s;
}
/* wcstol etc — stubs: parse only ASCII digits via the integer-strtol. */
long wcstol(const wchar_t *nptr, wchar_t **endptr, int base) {
    char buf[64]; size_t i = 0;
    while (i < sizeof(buf)-1 && nptr[i] && nptr[i] < 128) { buf[i] = (char)nptr[i]; i++; }
    buf[i] = 0;
    char *cep;
    long v = strtol(buf, &cep, base);
    if (endptr) *endptr = (wchar_t *)(nptr + (cep - buf));
    return v;
}
long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base) {
    char buf[64]; size_t i = 0;
    while (i < sizeof(buf)-1 && nptr[i] && nptr[i] < 128) { buf[i] = (char)nptr[i]; i++; }
    buf[i] = 0;
    char *cep;
    long long v = strtoll(buf, &cep, base);
    if (endptr) *endptr = (wchar_t *)(nptr + (cep - buf));
    return v;
}
unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base) {
    char buf[64]; size_t i = 0;
    while (i < sizeof(buf)-1 && nptr[i] && nptr[i] < 128) { buf[i] = (char)nptr[i]; i++; }
    buf[i] = 0;
    char *cep;
    unsigned long v = strtoul(buf, &cep, base);
    if (endptr) *endptr = (wchar_t *)(nptr + (cep - buf));
    return v;
}
unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base) {
    char buf[64]; size_t i = 0;
    while (i < sizeof(buf)-1 && nptr[i] && nptr[i] < 128) { buf[i] = (char)nptr[i]; i++; }
    buf[i] = 0;
    char *cep;
    unsigned long long v = strtoull(buf, &cep, base);
    if (endptr) *endptr = (wchar_t *)(nptr + (cep - buf));
    return v;
}
double wcstod(const wchar_t *nptr, wchar_t **endptr) {
    char buf[64]; size_t i = 0;
    while (i < sizeof(buf)-1 && nptr[i] && nptr[i] < 128) { buf[i] = (char)nptr[i]; i++; }
    buf[i] = 0;
    char *cep;
    double v = strtod(buf, &cep);
    if (endptr) *endptr = (wchar_t *)(nptr + (cep - buf));
    return v;
}
float wcstof(const wchar_t *nptr, wchar_t **endptr) {
    return (float)wcstod(nptr, endptr);
}
long double wcstold(const wchar_t *nptr, wchar_t **endptr) {
    return (long double)wcstod(nptr, endptr);
}

#include <stdarg.h>
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
