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
    char **files;
    size_t file_count;
    size_t file_cap;
} as_token_vec_t;

typedef struct {
    const char **include_dirs;
    size_t include_dir_count;
    int intel_syntax;
    unsigned max_include_depth;
    /* Whether sp, lr, pc, r0, x1 and their like are registers: they are
     * ARM's, and in x86 source they are names like any other. */
    int arm_registers;
} as_lexer_cfg_t;

void as_token_vec_init(as_token_vec_t *v);
void as_token_vec_free(as_token_vec_t *v);

/*
 * What a character of a source line is: text, part of a quoted string
 * (its quotes included), or comment.  One scanner says, for everything
 * that has to tell them apart -- what splits a line at `;`, what splits
 * a macro's arguments, what takes comments out, what reads a string.
 * There were six, each with its own idea: two did not know that a
 * backslash escapes a quote, so `"a\"; b"` was cut in two, and only one
 * knew what a comment was, so a `;` in one began a statement.
 *
 * A string is between two `"` or two `'`, and in it a backslash takes
 * the next character with it.  A comment is from `#` or `//` to the end
 * of the line, or between slash-star and star-slash, which may be on
 * another line: the state is carried from line to line by whoever keeps
 * it, and begun as zeros.
 */
typedef struct {
    int quote;
    int escaped;
    int block_comment;
} as_scan_t;

enum {
    AS_SCAN_TEXT = 0,
    AS_SCAN_QUOTED,
    AS_SCAN_COMMENT,       /* in a block comment */
    AS_SCAN_COMMENT_TO_EOL /* this character and the rest of the line */
};

/*
 * Classify s[i], which is not the end of s.  *more is how many further
 * characters are of it -- the second of the two that open or close a
 * block comment.
 */
int as_scan_step(as_scan_t *st, const char *s, size_t i, size_t *more);

/*
 * The first byte of a line that is not source but says where the next
 * line is from: the mark, the line's number in decimal, a space and the
 * file's name.  What expands the source before it is tokenized writes
 * these, so that a statement has the file and line its author wrote it
 * at and not its place in what the expansion made.
 */
#define AS_LEX_MARK '\001'

/*
 * The file name of a token of a marked text begins with this: its line
 * is a line of the text and not of a file, and as_source_position()
 * says which line of which file it was written at.
 */
#define AS_SOURCE_INTERNAL_PREFIX "\002"

/*
 * Tokenize a source held in memory, whose first line is line 1 of
 * `name`.  If `marked`, the text has been expanded and holds marks: its
 * tokens have an internal file name and the text's own line numbers, one
 * to a line -- several lines of it may all be of one line of the user's,
 * and the assembler tells statements apart by line.
 */
int as_lex_text(const char *name, const char *text, size_t len, int marked, const as_lexer_cfg_t *cfg,
                as_token_vec_t *out, char *errbuf, size_t errbuf_sz);

/*
 * Where the user wrote what an internal file name and line stand for:
 * 1, with the file and line, or 0 if `file` is not internal.
 */
int as_source_position(const char *file, unsigned line, const char **file_out, unsigned *line_out);

/* A message, with every internal `file:line` in it made the user's. */
void as_source_positions_in_text(const char *in, char *out, size_t out_sz);

int as_lex_file(const char *path, const as_lexer_cfg_t *cfg, as_token_vec_t *out,
                char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
