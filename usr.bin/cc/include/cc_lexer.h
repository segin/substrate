#ifndef CC_LEXER_H
#define CC_LEXER_H

/*
 * The C lexer: tokens and the lexer state.  frontend/lexer.c
 * implements it; the parser is its only user.
 */

#include <stddef.h>

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
    char *logical_file;
} cc_lexer_t;

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUM,
    TOK_STR,
    TOK_KW_AUTO,
    TOK_KW_BOOL,
    TOK_KW_COMPLEX,
    TOK_KW_CHAR,
    TOK_KW_CONST,
    TOK_KW_INT,
    TOK_KW_EXTERN,
    TOK_KW_EXTENSION,
    TOK_KW_FLOAT,
    TOK_KW_INLINE,
    TOK_KW_LONG,
    TOK_KW_REGISTER,
    TOK_KW_RESTRICT,
    TOK_KW_SHORT,
    TOK_KW_SIGNED,
    TOK_KW_STATIC,
    TOK_KW_STRUCT,
    TOK_KW_UNION,
    TOK_KW_ENUM,
    TOK_KW_TYPEDEF,
    TOK_KW_UNSIGNED,
    TOK_KW_DOUBLE,
    TOK_KW_IMAGINARY,
    TOK_KW_VOLATILE,
    TOK_KW_VOID,
    TOK_KW_RETURN,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_DO,
    TOK_KW_FOR,
    TOK_KW_SWITCH,
    TOK_KW_CASE,
    TOK_KW_DEFAULT,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_GOTO,
    TOK_KW_SIZEOF,
    TOK_ELLIPSIS,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACK,
    TOK_RBRACK,
    TOK_COMMA,
    TOK_QUESTION,
    TOK_COLON,
    TOK_SEMI,
    TOK_DOT,
    TOK_ARROW,
    TOK_ASSIGN,
    TOK_PLUS_EQ,
    TOK_MINUS_EQ,
    TOK_STAR_EQ,
    TOK_SLASH_EQ,
    TOK_PERCENT_EQ,
    TOK_LSHIFT_EQ,
    TOK_RSHIFT_EQ,
    TOK_AND_EQ,
    TOK_XOR_EQ,
    TOK_OR_EQ,
    TOK_PLUS_PLUS,
    TOK_MINUS_MINUS,
    TOK_AND_AND,
    TOK_OR_OR,
    TOK_BANG,
    TOK_LSHIFT,
    TOK_RSHIFT,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_AMP,
    TOK_PIPE,
    TOK_CARET,
    TOK_TILDE
} cc_tok_kind_t;

typedef struct {
    cc_tok_kind_t kind;
    const char *start;
    size_t len;
    long long num;
    double fnum;
    long double fnum_ld;
    int is_float;
    int float_is_single;
    int float_is_long;
    int int_is_unsigned;
    int int_is_long;
    int int_is_longlong;
    const char *file;
    size_t line;
    size_t col;
} cc_token_t;

/**
 * Start lexing a buffer.
 *
 * @param lx    the lexer to initialize
 * @param src   the (preprocessed) source text; it must outlive the lexer
 * @param len   its length in bytes
 * @param path  the file name tokens report until a line marker changes it
 * @return      0
 */
int cc_lexer_init(cc_lexer_t *lx, const char *src, size_t len, const char *path);

/**
 * Free what the lexer allocated (file names from line markers).
 *
 * @param lx  the lexer
 */
void cc_lexer_deinit(cc_lexer_t *lx);

/**
 * Read the next token.
 *
 * @param lx   the lexer
 * @param out  receives the token; at the end of input, a TOK_EOF token
 * @return     0, or -1 when the input at this point is not a valid token
 *             (an unterminated literal, a stray character, ...)
 */
int cc_lexer_next(cc_lexer_t *lx, cc_token_t *out);

/**
 * Tell the lexer the target's pointer size, which decides the type of
 * integer constants with an `l` suffix.
 *
 * @param bytes  4 or 8; other values are ignored
 */
void cc_lexer_set_pointer_size(int bytes);

#endif
