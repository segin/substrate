#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

char *sh_strndup(const char *s, size_t n);
int match_pattern(const char *pattern, const char *str);
void unquote_word(char *word);
void buffer_append(char **buf, size_t *cap, size_t *len, char c);
void buffer_append_str(char **buf, size_t *cap, size_t *len, const char *str);

#endif
