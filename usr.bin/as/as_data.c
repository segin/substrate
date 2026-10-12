/*
 * What data directives are written in: string literals, and LEB128.
 *
 * (There was more here: a list of every data directive of a source, each
 * with its values parsed, that was made before the object was written
 * and handed to the emitter -- which did not read it, and emits from the
 * statements as it does everything else.  The list refused what GNU as
 * takes, `.byte` and `.zero` with nothing after them, and did not know
 * `.word`.  It is gone.)
 */
#include "as_data.h"

#include <stdlib.h>
#include <string.h>

size_t as_leb128_encode(long long value, int is_signed, unsigned char *out) {
    size_t n = 0;

    if (is_signed) {
        int more = 1;

        while (more) {
            unsigned char b = (unsigned char)(value & 0x7f);

            value >>= 7;
            if ((value == 0 && (b & 0x40) == 0) || (value == -1 && (b & 0x40) != 0)) {
                more = 0;
            } else {
                b |= 0x80;
            }
            out[n++] = b;
        }
    } else {
        unsigned long long u = (unsigned long long)value;

        do {
            unsigned char b = (unsigned char)(u & 0x7f);

            u >>= 7;
            if (u != 0) {
                b |= 0x80;
            }
            out[n++] = b;
        } while (u != 0);
    }
    return n;
}

int as_decode_string_literal(const char *src, char **bytes_out, size_t *len_out) {
    size_t i;
    size_t j;
    size_t n;
    char *out;

    if (bytes_out == NULL || len_out == NULL) {
        return -1;
    }
    *bytes_out = NULL;
    *len_out = 0;
    if (src == NULL) {
        src = "";
    }
    n = strlen(src);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return -1;
    }

    i = 0;
    j = 0;
    while (i < n) {
        unsigned char ch = (unsigned char)src[i++];

        if (ch != '\\') {
            out[j++] = (char)ch;
            continue;
        }
        if (i >= n) {
            free(out);
            return -1;
        }

        ch = (unsigned char)src[i++];
        /* The escapes are GNU as's: there is none for a bell (`\a` is
         * an a), and `\X` is `\x`. */
        if (ch == 'X') {
            ch = 'x';
        }
        switch (ch) {
        case 'b':
            out[j++] = '\b';
            break;
        case 'f':
            out[j++] = '\f';
            break;
        case 'n':
            out[j++] = '\n';
            break;
        case 'r':
            out[j++] = '\r';
            break;
        case 't':
            out[j++] = '\t';
            break;
        case 'v':
            out[j++] = '\v';
            break;
        case '\\':
            out[j++] = '\\';
            break;
        case '\'':
            out[j++] = '\'';
            break;
        case '"':
            out[j++] = '"';
            break;
        case '?':
            out[j++] = '?';
            break;
        case 'x': {
            unsigned value = 0;

            while (i < n) {
                unsigned char hex = (unsigned char)src[i];
                if (hex >= '0' && hex <= '9') {
                    value = (value << 4) | (unsigned)(hex - '0');
                } else if (hex >= 'a' && hex <= 'f') {
                    value = (value << 4) | (unsigned)(10 + (hex - 'a'));
                } else if (hex >= 'A' && hex <= 'F') {
                    value = (value << 4) | (unsigned)(10 + (hex - 'A'));
                } else {
                    break;
                }
                ++i;
            }
            /* (With no digit it is the byte of nothing.) */
            out[j++] = (char)(value & 0xffu);
            break;
        }
        default:
            /* (Up to three digits, each times eight -- and 8 and 9
             * are digits here, as GNU as has it: `\18` is 16.) */
            if (ch >= '0' && ch <= '9') {
                unsigned value = (unsigned)(ch - '0');
                int digits = 1;

                while (digits < 3 && i < n && src[i] >= '0' && src[i] <= '9') {
                    value = value * 8u + (unsigned)(src[i] - '0');
                    ++i;
                    ++digits;
                }
                out[j++] = (char)(value & 0xffu);
            } else {
                out[j++] = (char)ch;
            }
            break;
        }
    }

    out[j] = '\0';
    *bytes_out = out;
    *len_out = j;
    return 0;
}
