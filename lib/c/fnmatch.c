#include <fnmatch.h>
#include <stddef.h>

#define FNM_NOT_LEADING 0x80000000

// Simple glob-style pattern matching implementation
// Supports: * (any chars), ? (single char), [abc] (character class)
int fnmatch(const char *pattern, const char *string, int flags) {
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;

            // Check trailing * behavior
            if (!*pattern) {
                // If FNM_PATHNAME, * cannot match /
                if (flags & FNM_PATHNAME) {
                    const char *s = string;
                    while (*s) {
                        if (*s == '/') return FNM_NOMATCH;
                        s++;
                    }
                }
                // If FNM_PERIOD, * cannot match leading period
                if ((flags & FNM_PERIOD) && !(flags & FNM_NOT_LEADING) && *string == '.') {
                    return FNM_NOMATCH;
                }
                return 0;
            }

            // Loop to consume string
            int first = 1;
            while (1) {
                // Determine recursive flags
                // If we advanced string, we are NOT leading.
                // If we didn't advance (first), we keep leading status.
                int rec_flags = flags;
                if (!first) rec_flags |= FNM_NOT_LEADING;

                if (fnmatch(pattern, string, rec_flags) == 0) return 0;

                if (!*string) break;

                // Can we consume *string?
                // FNM_PATHNAME: cannot cross /
                if ((flags & FNM_PATHNAME) && *string == '/') break;

                // FNM_PERIOD: cannot cross leading period
                // Leading period constraint applies only to the first character matched by *
                // If first iteration, and is_leading, and char is '.', we cannot consume it.
                if (first && (flags & FNM_PERIOD) && !(flags & FNM_NOT_LEADING) && *string == '.') break;

                string++;
                first = 0;
            }
            return FNM_NOMATCH;

        } else if (*pattern == '?') {
            if (!*string) return FNM_NOMATCH;

            // FNM_PATHNAME: ? cannot match /
            if ((flags & FNM_PATHNAME) && *string == '/') return FNM_NOMATCH;

            // FNM_PERIOD: ? cannot match leading .
            if ((flags & FNM_PERIOD) && !(flags & FNM_NOT_LEADING) && *string == '.') return FNM_NOMATCH;

            pattern++;

            // Update flags for next char
            if (*string == '/' && (flags & FNM_PATHNAME))
                flags &= ~FNM_NOT_LEADING; // Next is leading
            else
                flags |= FNM_NOT_LEADING;  // Next is not leading

            string++;

        } else if (*pattern == '[') {
            // FNM_PATHNAME: cannot match /
            if ((flags & FNM_PATHNAME) && *string == '/') return FNM_NOMATCH;

            // FNM_PERIOD: cannot match leading .
            if ((flags & FNM_PERIOD) && !(flags & FNM_NOT_LEADING) && *string == '.') return FNM_NOMATCH;

            pattern++; // Move past [
            int match = 0;
            int invert = 0;
            if (*pattern == '!' || *pattern == '^') {
                invert = 1;
                pattern++;
            }

            while (*pattern && *pattern != ']') {
                char c = *pattern;
                pattern++;
                // Handle escapes in bracket unless FNM_NOESCAPE
                if (c == '\\' && !(flags & FNM_NOESCAPE)) {
                    if (*pattern) c = *pattern++;
                }

                char end = 0;
                // Check range
                if (*pattern == '-' && pattern[1] && pattern[1] != ']') {
                    pattern++; // consume -
                    end = *pattern++;
                    if (end == '\\' && !(flags & FNM_NOESCAPE)) {
                         if (*pattern) end = *pattern++;
                    }
                    if (*string >= c && *string <= end) match = 1;
                } else {
                    if (*string == c) match = 1;
                }
            }

            if (*pattern == ']') pattern++;
            if (invert) match = !match;
            if (!match) return FNM_NOMATCH;

            // Update flags for next char
            flags |= FNM_NOT_LEADING;

            string++;

        } else {
            // Literal match
            char c = *pattern;
            pattern++;

            // Handle escape
            if (c == '\\' && !(flags & FNM_NOESCAPE)) {
                if (*pattern) c = *pattern++;
            }

            if (c != *string) return FNM_NOMATCH;

            // Update flags
            if (c == '/' && (flags & FNM_PATHNAME))
                flags &= ~FNM_NOT_LEADING;
            else
                flags |= FNM_NOT_LEADING;

            string++;
        }
    }
    return (*string == '\0') ? 0 : FNM_NOMATCH;
}
