#ifndef SUBSTRATE_AS_EXPR_H
#define SUBSTRATE_AS_EXPR_H

/*
 * Expressions: the one parser and evaluator of them.  Their tokens are
 * the lexer's (as_lexer.h), as every other token is.
 *
 * Everything in the assembler that reads a number or an expression --
 * an operand, the argument of a data directive, a repeat count, an
 * alignment, the right-hand side of .set, the condition of .if -- reads
 * it here, so that an expression means one thing wherever it is written.
 * There used to be an evaluator in each of as.c, as_data.c, as_sections.c
 * and as_symtab.c and three in as_elf_emit.c, with as many grammars.
 *
 * The grammar is GNU as's.  Highest precedence first:
 *
 *      unary   - ~ ! +
 *      * / % << >>
 *      | & ^ !          (infix !: or-not)
 *      + - == <> != < > <= >=
 *      && ||
 *
 * A comparison is -1 when true, as in GNU as, and 0 when false; && and ||
 * give 1 and 0.  Arithmetic is that of a 64-bit two's-complement machine:
 * it wraps and does not trap.
 */

#include "as_lexer.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_EXPR_CONST = 0,
    AS_EXPR_SYMBOL,
    AS_EXPR_LOCAL_REF,
    AS_EXPR_UNARY,
    AS_EXPR_BINARY,
} as_expr_kind_t;

typedef enum {
    AS_EXPR_OP_NONE = 0,
    AS_EXPR_OP_ADD,
    AS_EXPR_OP_SUB,
    AS_EXPR_OP_MUL,
    AS_EXPR_OP_DIV,
    AS_EXPR_OP_MOD,
    AS_EXPR_OP_OR,
    AS_EXPR_OP_AND,
    AS_EXPR_OP_XOR,
    AS_EXPR_OP_SHL,
    AS_EXPR_OP_SHR,
    AS_EXPR_OP_EQ,
    AS_EXPR_OP_NE,
    AS_EXPR_OP_LT,
    AS_EXPR_OP_LE,
    AS_EXPR_OP_GT,
    AS_EXPR_OP_GE,
    AS_EXPR_OP_NEG,
    AS_EXPR_OP_BNOT,
    AS_EXPR_OP_LNOT,    /* unary !  */
    AS_EXPR_OP_ORNOT,   /* infix !  */
    AS_EXPR_OP_LAND,    /* &&       */
    AS_EXPR_OP_LOR,     /* ||       */
} as_expr_op_t;

typedef struct as_expr as_expr_t;
struct as_expr {
    as_expr_kind_t kind;
    as_expr_op_t op;
    long long value;
    char *symbol;
    /*
     * The relocation operator written on the symbol -- the GOTOFF of
     * `sym@GOTOFF` -- as it was written, or NULL.  It is no part of the
     * symbol's name: `symbol` is `sym`.  Which relocation it names, and
     * whether the target has it, is for as_reloc_op.h to say.
     */
    char *reloc_op;
    int local_digit;
    int local_forward;
    int local_resolved;
    unsigned local_target_line;
    unsigned src_line;
    char *src_file;
    as_expr_t *lhs;
    as_expr_t *rhs;
};

/*
 * An expression may have this many nodes and no more.  The tree is walked
 * recursively -- to resolve local labels, to evaluate, to free -- and a
 * chain of `1+1+1+...` leans all to one side, so its length is the depth
 * of those walks.  A source with a line of a hundred thousand terms used
 * to end the assembler with a stack overflow.
 */
#define AS_EXPR_MAX_NODES 4096

/*
 * Parse the whole of a row of tokens; NULL if they are not one
 * expression, or it is too large.  `file` and `line` are recorded in the
 * nodes for diagnostics.  A string token is a name, the name in quotes.
 */
as_expr_t *as_expr_parse_tokens(const as_token_t *tokv, size_t n, const char *file, unsigned line);

/* The same of text, which is made tokens first (as_lex_text_tokens). */
as_expr_t *as_parse_expr_string(const char *s, const char *file, unsigned line);
void as_expr_free(as_expr_t *e);

/* A copy of the whole tree, or NULL for no memory. */
as_expr_t *as_expr_clone(const as_expr_t *e);

/* The expression written out, so that parsing it gives the same tree:
 * every operation within another is in parentheses.  The caller frees
 * it; NULL for no memory. */
char *as_expr_to_text(const as_expr_t *e);

/* The whole of `s` as one integer literal with an optional sign: decimal,
 * 0x hexadecimal, 0b binary or a leading 0 for octal, up to 64 bits (a
 * value of 2^63 or more is its two's-complement self, not a saturated
 * one).  0, or -1 if `s` is anything else. */
int as_expr_parse_number(const char *s, long long *out);

/*
 * How the evaluator learns the value of a symbol.  Returns 1 and stores
 * the value if `name` is a constant the caller knows, 0 if it is not.
 * The location counter `.` is asked for like any other name.
 */
typedef int (*as_expr_lookup_fn)(void *cookie, const char *name, long long *value_out);

#define AS_EXPR_EVAL_OK         0
#define AS_EXPR_EVAL_NOT_CONST  (-1)    /* a symbol the lookup did not know */
#define AS_EXPR_EVAL_ERROR      (-2)    /* division by zero */

/* The value of `e`, if it has one.  `lookup` may be NULL, for an
 * expression that must be made of numbers alone. */
int as_expr_eval(const as_expr_t *e, as_expr_lookup_fn lookup, void *cookie, long long *out);

/* Parse and evaluate in one step.  AS_EXPR_EVAL_NOT_CONST also for text
 * that is not an expression. */
int as_expr_eval_string(const char *s, as_expr_lookup_fn lookup, void *cookie, long long *out);

/*
 * An expression reduced to what an object file can hold: a number, plus
 * at most one symbol, minus at most one other.
 *
 *      value + add_symbol - sub_symbol
 *
 * add_symbol and sub_symbol point into the expression and live as long
 * as it does.  A local label reference (`1f`) counts as a symbol and is
 * given back as its node, in add_local or sub_local, with the matching
 * name pointer NULL.
 */
typedef struct {
    long long value;
    const char *add_symbol;
    const char *sub_symbol;
    const as_expr_t *add_local;
    const as_expr_t *sub_local;
} as_expr_linear_t;

/* AS_EXPR_EVAL_OK if `e` has that form; AS_EXPR_EVAL_NOT_CONST if it
 * does not (two symbols added, a symbol multiplied, ...). */
int as_expr_eval_linear(const as_expr_t *e, as_expr_lookup_fn lookup, void *cookie,
                        as_expr_linear_t *out);

/* The relocation operator on a symbol of the expression, the first
 * there is, or NULL: see as_expr_t.reloc_op. */
const char *as_expr_reloc_op(const as_expr_t *e);

#ifdef __cplusplus
}
#endif

#endif
