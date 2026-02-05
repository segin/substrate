#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

char *sh_strndup(const char *s, size_t n);
int match_pattern(const char *pattern, const char *str);
void unquote_word(char *word);

#endif
