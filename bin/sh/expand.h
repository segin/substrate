#ifndef EXPAND_H
#define EXPAND_H

#include <stddef.h>

/**
 * expand_word - Performs shell expansions on a single word.
 * @word: The input word string.
 *
 * Returns a newly allocated string with expansions performed, 
 * or NULL on error. The caller must free the result.
 * 
 * Expansions performed:
 * 1. Tilde expansion (~ -> $HOME)
 * 2. Parameter expansion ($VAR, ${VAR})
 * 3. Quote removal (strip outer "" and '')
 */
char *expand_word(const char *word);

/**
 * expand_list - Expands a list of words.
 * @words: NULL-terminated array of strings.
 *
 * Returns a newly allocated NULL-terminated array of strings.
 * The caller must free the array and its contents.
 */
char **expand_list(char **words);

/**
 * expand_heredoc - Performs expansions on here-document content.
 * @content: The raw here-doc content.
 * @quoted: If 1, no expansions are performed (only backslash-newline removal).
 *
 * Returns a newly allocated expanded string.
 */
char *expand_heredoc(const char *content, int quoted);

#endif
