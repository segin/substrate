#ifndef EXPAND_H
#define EXPAND_H

#include <stddef.h>

typedef struct expand_state {
    int fatal;
    int fatal_status;
    int cmdsub_status;
    int saw_cmdsub;
} expand_state_t;

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
int expand_word_ex(const char *word, char **out, expand_state_t *state);

/**
 * expand_list - Expands a list of words.
 * @words: NULL-terminated array of strings.
 *
 * Returns a newly allocated NULL-terminated array of strings.
 * The caller must free the array and its contents.
 */
char **expand_list(char **words);
int expand_list_ex(char **words, char ***out, expand_state_t *state);

/**
 * expand_heredoc - Performs expansions on here-document content.
 * @content: The raw here-doc content.
 * @quoted: If 1, no expansions are performed (only backslash-newline removal).
 *
 * Returns a newly allocated expanded string.
 */
char *expand_heredoc(const char *content, int quoted);
int expand_heredoc_ex(const char *content, int quoted, char **out,
    expand_state_t *state);

#endif
