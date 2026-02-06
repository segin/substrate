#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int match_pattern(const char *pattern, const char *str) {
    if (!pattern || !str) return 0;
    
    // Simple recursive glob matcher supporting * and ?
    if (*pattern == '\0') {
        return *str == '\0';
    }
    
    if (*pattern == '*') {
        while (*(pattern + 1) == '*') pattern++; // Skip multiple *
        
        if (*(pattern + 1) == '\0') return 1; // * at end matches everything
        
        while (*str) {
            if (match_pattern(pattern + 1, str)) return 1;
            str++;
        }
        return match_pattern(pattern + 1, str); // Try matching empty string at end
    }
    
    if (*pattern == '?' || *pattern == *str) {
        if (*str == '\0') return 0; // ? cannot match empty
        return match_pattern(pattern + 1, str + 1);
    }

    if (*pattern == '[') {
        if (*str == '\0') return 0;
        pattern++;
        int invert = 0;
        if (*pattern == '!') {
            invert = 1;
            pattern++;
        }
        
        int match = 0;
        int first = 1;
        while (*pattern && (*pattern != ']' || first)) {
             // Handle ranges like a-z
             if (*(pattern + 1) == '-' && *(pattern + 2) && *(pattern + 2) != ']') {
                 char start = *pattern;
                 char end = *(pattern + 2);
                 if (*str >= start && *str <= end) match = 1;
                 pattern += 3;
             } else {
                 if (*pattern == *str) match = 1;
                 pattern++;
             }
             first = 0;
        }
        
        if (*pattern != ']') return 0; // Unmatched [
        pattern++; // Skip ]
        
        if (invert) match = !match;
        if (!match) return 0;
        
        return match_pattern(pattern, str + 1);
    }
    
    return 0;
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
    // Guard against exponential expansion - max 64KB
    #define BUFFER_MAX_SIZE (64 * 1024)
    if (*len >= BUFFER_MAX_SIZE) {
        return; // Silently stop appending beyond limit
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
            write(2, msg, strlen(msg));
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
