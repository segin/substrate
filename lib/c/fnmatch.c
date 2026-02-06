#include <fnmatch.h>
#include <stddef.h>

// Simple glob-style pattern matching implementation
// Supports: * (any chars), ? (single char), [abc] (character class)
int fnmatch(const char *pattern, const char *string, int flags) {
    (void)flags; // TODO: implement FNM_PATHNAME, FNM_PERIOD, FNM_NOESCAPE
    
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 0; // trailing * matches everything
            while (*string) {
                if (fnmatch(pattern, string, flags) == 0) return 0;
                string++;
            }
            return FNM_NOMATCH;
        } else if (*pattern == '?') {
            if (!*string) return FNM_NOMATCH;
            pattern++;
            string++;
        } else if (*pattern == '[') {
            pattern++;
            int match = 0;
            int invert = 0;
            if (*pattern == '!' || *pattern == '^') {
                invert = 1;
                pattern++;
            }
            while (*pattern && *pattern != ']') {
                if (pattern[1] == '-' && pattern[2] && pattern[2] != ']') {
                    // Range: [a-z]
                    if (*string >= pattern[0] && *string <= pattern[2])
                        match = 1;
                    pattern += 3;
                } else {
                    if (*string == *pattern) match = 1;
                    pattern++;
                }
            }
            if (*pattern == ']') pattern++;
            if (invert) match = !match;
            if (!match) return FNM_NOMATCH;
            string++;
        } else {
            if (*pattern != *string) return FNM_NOMATCH;
            pattern++;
            string++;
        }
    }
    return (*string == '\0') ? 0 : FNM_NOMATCH;
}
