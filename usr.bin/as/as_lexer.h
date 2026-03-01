#ifndef SUBSTRATE_AS_LEXER_H
#define SUBSTRATE_AS_LEXER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_TOK_INVALID = 0,
    AS_TOK_MNEMONIC,
    AS_TOK_REGISTER,
    AS_TOK_IMMEDIATE,
    AS_TOK_LABEL,
    AS_TOK_DIRECTIVE,
    AS_TOK_STRING,
    AS_TOK_IDENTIFIER,
    AS_TOK_OPERATOR,
    AS_TOK_PUNCT,
} as_token_kind_t;

typedef struct {
    as_token_kind_t kind;
    char *text;
    char *file;
    unsigned line;
    unsigned col;
} as_token_t;

typedef struct {
    as_token_t *items;
    size_t count;
    size_t cap;
} as_token_vec_t;

typedef struct {
    const char **include_dirs;
    size_t include_dir_count;
    int intel_syntax;
    unsigned max_include_depth;
} as_lexer_cfg_t;

void as_token_vec_init(as_token_vec_t *v);
void as_token_vec_free(as_token_vec_t *v);

int as_lex_file(const char *path, const as_lexer_cfg_t *cfg, as_token_vec_t *out,
                char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
