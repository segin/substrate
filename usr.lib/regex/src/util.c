#include <stdlib.h>
#include <string.h>

#ifdef REGEX_USE_ICU
#include <unicode/uchar.h>
#endif

#include "regex_internal.h"

int regex_is_newline(uint32_t cp) {
    return cp == '\n' || cp == '\r';
}

uint32_t regex_ascii_tolower(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') {
        return (uint32_t)(cp - 'A' + 'a');
    }
    return cp;
}

uint32_t regex_ascii_toupper(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') {
        return (uint32_t)(cp - 'a' + 'A');
    }
    return cp;
}

#ifdef REGEX_USE_ICU
uint32_t regex_unicode_tolower(uint32_t cp) {
    return (uint32_t)u_tolower((UChar32)cp);
}

uint32_t regex_unicode_toupper(uint32_t cp) {
    return (uint32_t)u_toupper((UChar32)cp);
}
#else
uint32_t regex_unicode_tolower(uint32_t cp) {
    return regex_ascii_tolower(cp);
}

uint32_t regex_unicode_toupper(uint32_t cp) {
    return regex_ascii_toupper(cp);
}
#endif

int regex_utf8_decode(const char *s, size_t len, size_t *index, uint32_t *out_cp) {
    size_t i;
    uint8_t c;
    uint32_t cp;

    if (!s || !index || !out_cp) {
        return 0;
    }

    i = *index;
    if (i >= len) {
        return 0;
    }

    c = (uint8_t)s[i++];
    if (c < 0x80) {
        *out_cp = c;
        *index = i;
        return 1;
    }

    if ((c & 0xE0) == 0xC0) {
        if (i >= len) {
            return 0;
        }
        cp = (uint32_t)(c & 0x1F) << 6;
        c = (uint8_t)s[i++];
        if ((c & 0xC0) != 0x80) {
            return 0;
        }
        cp |= (uint32_t)(c & 0x3F);
        if (cp < 0x80) {
            return 0;
        }
        *out_cp = cp;
        *index = i;
        return 1;
    }

    if ((c & 0xF0) == 0xE0) {
        if (i + 1 >= len) {
            return 0;
        }
        cp = (uint32_t)(c & 0x0F) << 12;
        c = (uint8_t)s[i++];
        if ((c & 0xC0) != 0x80) {
            return 0;
        }
        cp |= (uint32_t)(c & 0x3F) << 6;
        c = (uint8_t)s[i++];
        if ((c & 0xC0) != 0x80) {
            return 0;
        }
        cp |= (uint32_t)(c & 0x3F);
        if (cp < 0x800) {
            return 0;
        }
        *out_cp = cp;
        *index = i;
        return 1;
    }

    if ((c & 0xF8) == 0xF0) {
        if (i + 2 >= len) {
            return 0;
        }
        cp = (uint32_t)(c & 0x07) << 18;
        c = (uint8_t)s[i++];
        if ((c & 0xC0) != 0x80) {
            return 0;
        }
        cp |= (uint32_t)(c & 0x3F) << 12;
        c = (uint8_t)s[i++];
        if ((c & 0xC0) != 0x80) {
            return 0;
        }
        cp |= (uint32_t)(c & 0x3F) << 6;
        c = (uint8_t)s[i++];
        if ((c & 0xC0) != 0x80) {
            return 0;
        }
        cp |= (uint32_t)(c & 0x3F);
        if (cp < 0x10000 || cp > REGEX_MAX_CODEPOINT) {
            return 0;
        }
        *out_cp = cp;
        *index = i;
        return 1;
    }

    return 0;
}

size_t regex_utf8_encode(uint32_t cp, char out[4]) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

char *regex_escape_literal(const char *s, size_t len) {
    size_t i;
    size_t out_len = 0;
    char *out;
    char *p;

    if (!s) {
        return NULL;
    }

    for (i = 0; i < len; ++i) {
        switch (s[i]) {
        case '.' : case '*' : case '+' : case '?' : case '(' : case ')' :
        case '[' : case ']' : case '{' : case '}' : case '|' : case '^' :
        case '$' : case '\\':
            out_len += 2;
            break;
        default:
            out_len += 1;
            break;
        }
    }

    out = (char *)malloc(out_len + 1);
    if (!out) {
        return NULL;
    }

    p = out;
    for (i = 0; i < len; ++i) {
        switch (s[i]) {
        case '.' : case '*' : case '+' : case '?' : case '(' : case ')' :
        case '[' : case ']' : case '{' : case '}' : case '|' : case '^' :
        case '$' : case '\\':
            *p++ = '\\';
            *p++ = s[i];
            break;
        default:
            *p++ = s[i];
            break;
        }
    }
    *p = '\0';
    return out;
}
