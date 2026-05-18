#ifndef LEXER_H
#define LEXER_H

#include "token.h"
#include <stddef.h>

typedef struct lexer {
    const char *input;
    size_t pos;
    size_t len;
    int error;
    struct token *lookahead[4];
    int lookahead_count;
} lexer_t;

void lexer_init(lexer_t *l, const char *input);
token_t *lexer_next(lexer_t *l);
token_t *lexer_peek(lexer_t *l);
token_t *lexer_peek2(lexer_t *l);
token_t *lexer_peek_n(lexer_t *l, int n);
void lexer_push_back(lexer_t *l, token_t *t);
void lexer_clear_lookahead(lexer_t *l);

#endif
