#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

/* Upper bound on a single read_stream_all() result (script / stdin size). */
#define READ_STREAM_MAX (64u * 1024 * 1024)

char *sh_strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *res = malloc(len + 1);
    if (res) {
        memcpy(res, s, len);
        res[len] = 0;
    }
    return res;
}

/*
 * Match one character c against the bracket expression starting at *pp
 * (which points at '['), advancing *pp past the closing ']'.
 *   returns  1 : valid bracket expression, c matches
 *   returns  0 : valid bracket expression, c does not match
 *   returns -1 : no closing ']' -- not a bracket expression (*pp unchanged),
 *                the caller should treat the '[' as a literal character.
 * Range endpoints are compared as unsigned char so bytes >= 0x80 order
 * correctly.
 */
static int match_bracket(const char **pp, char c) {
    const char *p = *pp + 1;   /* skip '[' */
    int invert = 0;
    if (*p == '!' || *p == '^') { invert = 1; p++; }

    int match = 0, first = 1;
    const char *scan = p;
    while (*scan && (*scan != ']' || first)) {
        if (scan[1] == '-' && scan[2] && scan[2] != ']') {
            unsigned char lo = (unsigned char)scan[0];
            unsigned char hi = (unsigned char)scan[2];
            unsigned char uc = (unsigned char)c;
            if (uc >= lo && uc <= hi) match = 1;
            scan += 3;
        } else {
            if (*scan == c) match = 1;
            scan++;
        }
        first = 0;
    }
    if (*scan != ']') return -1;   /* unterminated: literal '[' */
    *pp = scan + 1;                /* past ']' */
    return invert ? !match : match;
}

/*
 * Glob matcher for '*', '?' and '[...]'. Iterative two-pointer algorithm
 * with a single backtrack point (star_p/star_s): O(len(pattern)*len(str))
 * worst case, no recursion and no exponential backtracking -- the previous
 * recursive version overflowed the stack on long inputs and blew up
 * exponentially on patterns like "a*a*a*...b".
 */
int match_pattern(const char *pattern, const char *str) {
    if (!pattern || !str) return 0;

    const char *p = pattern;
    const char *s = str;
    const char *star_p = NULL;   /* pattern just after the last '*' */
    const char *star_s = NULL;   /* str position that '*' is matching from */

    while (*s) {
        int advanced = 0;

        if (*p == '*') {
            while (*p == '*') p++;          /* collapse consecutive '*' */
            if (*p == '\0') return 1;       /* trailing '*' matches the rest */
            star_p = p;
            star_s = s;
            continue;
        } else if (*p == '?') {
            p++; s++; advanced = 1;
        } else if (*p == '[') {
            const char *pp = p;
            int r = match_bracket(&pp, *s);
            if (r == 1) { p = pp; s++; advanced = 1; }
            else if (r < 0 && *p == *s) { p++; s++; advanced = 1; }
            /* r == 0 (no match) or unterminated non-literal: not advanced */
        } else if (*p == *s) {
            p++; s++; advanced = 1;
        }

        if (!advanced) {
            if (star_p) {
                /* '*' absorbs one more character and we retry. */
                p = star_p;
                s = ++star_s;
            } else {
                return 0;
            }
        }
    }

    while (*p == '*') p++;   /* trailing '*' can match the empty tail */
    return *p == '\0';
}

void unquote_word(char *word) {
    if (!word) return;
    char *src = word;
    char *dest = word;
    int in_sq = 0;
    int in_dq = 0;
    
    while (*src) {
        if (*src == '\'' && !in_dq) {
            in_sq = !in_sq;
            src++;
        } else if (*src == '"' && !in_sq) {
            in_dq = !in_dq;
            src++;
        } else if (*src == '\\' && !in_sq) {
            src++;
            if (*src) *dest++ = *src++;
        } else {
            *dest++ = *src++;
        }
    }
    *dest = 0;
}

void buffer_append(char **buf, size_t *cap, size_t *len, char c) {
    #define BUFFER_MAX_SIZE (1024 * 1024)
    if (*len >= BUFFER_MAX_SIZE) {
        static int warned;
        if (!warned) {
            const char *msg = "sh: warning: expansion truncated at 1MB\n";
            if (write(2, msg, strlen(msg)) < 0) {}
            warned = 1;
        }
        return;
    }
    
    if (*len + 1 >= *cap) {
        size_t new_cap = *cap * 2;
        if (new_cap == 0) new_cap = 16;
        if (new_cap > BUFFER_MAX_SIZE) new_cap = BUFFER_MAX_SIZE + 1;
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) {
            // Simplified OOM handling: exit safety
            // In a more complex shell we might return error, but for now prevent segfault.
            // Using write/exit to avoid malloc in error handler.
            const char *msg = "sh: Out of memory\n";
            if (write(2, msg, strlen(msg)) < 0) {}
            exit(1);
        }
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[*len] = c;
    (*len)++;
    (*buf)[*len] = 0;
}

void buffer_append_str(char **buf, size_t *cap, size_t *len, const char *str) {
    if (!str) return;
    while (*str) {
        buffer_append(buf, cap, len, *str++);
    }
}

char *read_stream_all(FILE *stream) {
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    char chunk[256];

    if (!buf) {
        return NULL;
    }
    buf[0] = '\0';

    while (!feof(stream)) {
        size_t nread = fread(chunk, 1, sizeof(chunk), stream);

        if (nread == 0) {
            if (ferror(stream)) {
                free(buf);
                return NULL;
            }
            break;
        }

        if (len + nread + 1 > cap) {
            /* Bound total size (guards a 32-bit size_t against cap*2 wrapping
             * to a tiny value, and an endless stream against OOM). */
            if (len + nread + 1 > READ_STREAM_MAX) {
                fprintf(stderr, "sh: input exceeds %u bytes\n",
                    (unsigned)READ_STREAM_MAX);
                free(buf);
                return NULL;
            }
            size_t ncap = cap;
            while (len + nread + 1 > ncap) {
                ncap = (ncap > READ_STREAM_MAX / 2) ? READ_STREAM_MAX : ncap * 2;
            }
            char *nb = realloc(buf, ncap);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
            cap = ncap;
        }

        memcpy(buf + len, chunk, nread);
        len += nread;
        buf[len] = '\0';
    }

    return buf;
}
