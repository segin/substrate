#include <wchar.h>
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
        if (pwc)
            *pwc = (wchar_t)(((c0 & 0x1F) << 6) | (c1 & 0x3F));
        return 2;
    }

    if ((c0 & 0xF0) == 0xE0) {
        if (n < 3)
            return (size_t)-2;
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
            return (size_t)-1;
        if (pwc)
            *pwc = (wchar_t)(((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F));
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
        if (pwc)
            *pwc = (wchar_t)(((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) |
                             ((c2 & 0x3F) << 6) | (c3 & 0x3F));
        return 4;
    }

    return (size_t)-1;
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
