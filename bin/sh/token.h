#ifndef TOKEN_H
#define TOKEN_H

#include <stdlib.h>

typedef enum {
    TOKEN_EOF,
    TOKEN_NEWLINE,
    TOKEN_WORD,
    TOKEN_OPERATOR,
    TOKEN_IO_NUMBER,
    TOKEN_ASSIGNMENT_WORD, // Optional for future
    TOKEN_ERROR
} token_type_t;

typedef struct token {
    token_type_t type;
    char *value;
    int quoted;
} token_t;

static inline void token_free(token_t *t) {
    if (t) {
        if (t->value) free(t->value);
        free(t);
    }
}

static inline const char *token_type_str(token_type_t t) {
    switch (t) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_NEWLINE: return "NEWLINE";
        case TOKEN_WORD: return "WORD";
        case TOKEN_OPERATOR: return "OPERATOR";
        case TOKEN_IO_NUMBER: return "IO_NUMBER";
        case TOKEN_ASSIGNMENT_WORD: return "ASSIGNMENT_WORD";
        case TOKEN_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

#endif
