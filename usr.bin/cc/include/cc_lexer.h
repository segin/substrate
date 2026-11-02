#ifndef CC_LEXER_H
#define CC_LEXER_H

/*
 * The C lexer: tokens and the lexer state.  frontend/lexer.c
 * implements it; the parser is its only user.
 */

#include <stddef.h>

/** The most `#pragma GCC visibility push` levels the lexer keeps. */
#define CC_LEX_VIS_DEPTH 16

/** A default visibility a `#pragma GCC visibility push(...)` sets (cc_token_t.default_vis). */
enum {
    CC_LEX_VIS_DEFAULT = 0, /**< none set: a declaration's own attributes decide */
    CC_LEX_VIS_HIDDEN,
    CC_LEX_VIS_PROTECTED,
    CC_LEX_VIS_INTERNAL,
    CC_LEX_VIS_PUSHED_DEFAULT /**< push(default): default, which -fvisibility does not change */
};

/**
 * A file the source came from, as the preprocessor's line markers say: its
 * name (gcc's: as it was included), and the file that included it and the
 * line of that #include, which diagnostics name as gcc's do ("In file
 * included from hs.c:1:").
 */
typedef struct {
    char *name;         /**< owned */
    int parent;         /**< the including file's index; -1 for none */
    size_t included_at; /**< the line of the #include in it */
} cc_src_file_t;

/** The files of a translation unit, as the lexer met them (append-only: an index is never invalidated). */
typedef struct cc_src_files {
    cc_src_file_t *items;
    size_t count;
    size_t cap;
} cc_src_files_t;

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
    char *logical_file;
    /** the files met so far, shared by the copies of the lexer the parser's lookahead makes; NULL for none */
    cc_src_files_t *files;
    int file_id; /**< the file the lexer is in, an index in files; -1 for none */
    /** the default visibility `#pragma GCC visibility push(...)`/`pop` have set, innermost last; copied by value
     *  with the lexer, as the parser's lookahead copies it */
    unsigned char vis_stack[CC_LEX_VIS_DEPTH];
    int vis_depth;
    /** `#pragma pack`: the alignment structure members are held to now (1, 2, 4, 8, 16), or 0 for none; and the
     *  values `pack(push)` saved, innermost last */
    unsigned char pack_cur;
    unsigned char pack_stack[CC_LEX_VIS_DEPTH];
    int pack_depth;
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
    int float_is_half; /**< the `f16` suffix: a _Float16 constant */
    int float_variant; /**< the `f32`, `f64`, `f32x` or `f64x` suffix: its CC_FLOATN_* type; else 0 */
    int float_is_decimal; /**< a decimal floating constant: 32, 64 or 128 (df, dd, dl); else 0 */
    int is_imaginary; /**< GNU `i`/`j` suffix: the literal is an imaginary constant */
    int int_is_bitprecise; /**< C23 `wb`/`WB` suffix: a _BitInt constant (its value may need more than 64 bits) */
    int int_is_unsigned;
    int int_is_long;
    int int_is_longlong;
    char char_prefix; /**< a character constant's prefix, 'L', 'u' or 'U', which gives its type; else 0 */
    const char *file;
    int file_id; /**< the file it is in, an index in the lexer's files; -1 for none */
    size_t line;
    size_t col;
    unsigned char default_vis; /**< the default visibility where the token is (CC_LEX_VIS_*) */
    unsigned char pack;        /**< the `#pragma pack` alignment where the token is, or 0 for none */
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
 * Have the lexer record the files the line markers name in @p files, the
 * file it is in now the first.
 *
 * @param lx     the lexer
 * @param files  the table, owned by the caller; NULL for none
 * @return       0, or -1 when out of memory
 */
int cc_lexer_track_files(cc_lexer_t *lx, cc_src_files_t *files);

/**
 * Free a file table's entries (not the table itself).
 *
 * @param files  the table; NULL is ignored
 */
void cc_src_files_free(cc_src_files_t *files);

/**
 * Write gcc's account of how file @p id of @p files was included, a line
 * each, outermost last: "In file included from a.h:3,\n                 from
 * hs.c:1:\n"; nothing for a file not included.
 *
 * @param files  the table, or NULL
 * @param id     the file, or -1
 * @param out    receives the text, NUL-terminated (truncated to fit)
 * @param cap    its size
 */
void cc_src_files_include_chain(const cc_src_files_t *files, int id, char *out, size_t cap);

#endif
