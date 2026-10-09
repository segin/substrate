#include "as_expr.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    EXPR_TOK_EOF = 0,
    EXPR_TOK_NUMBER,
    EXPR_TOK_SYMBOL,
    EXPR_TOK_LOCAL,
    EXPR_TOK_LPAREN,
    EXPR_TOK_RPAREN,
    EXPR_TOK_OP,
} expr_tok_kind_t;

typedef struct {
    expr_tok_kind_t kind;
    as_expr_op_t op;
    long long number;
    char *symbol;
    int local_digit;
    int local_forward;
} expr_tok_t;

typedef struct {
    const char *s;
    size_t i;
    expr_tok_t cur;
    const char *file;
    unsigned line;
    size_t nodes;       /* made so far: see AS_EXPR_MAX_NODES */
    /* Parentheses and unary operators nest the parser itself, whatever the
     * tree comes to; this bounds that. */
    int depth;
} expr_lex_t;

#define EXPR_MAX_DEPTH 256

static char *expr_strdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1U;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

void as_expr_free(as_expr_t *e) {
    if (e == NULL) {
        return;
    }
    as_expr_free(e->lhs);
    as_expr_free(e->rhs);
    free(e->symbol);
    free(e->src_file);
    free(e);
}

static as_expr_t *new_expr(expr_lex_t *lx, as_expr_kind_t kind) {
    as_expr_t *e;

    if (lx->nodes >= AS_EXPR_MAX_NODES) {
        return NULL;
    }
    e = (as_expr_t *)calloc(1, sizeof(*e));
    if (e == NULL) {
        return NULL;
    }
    lx->nodes++;
    e->kind = kind;
    e->src_line = lx->line;
    if (lx->file != NULL) {
        e->src_file = expr_strdup(lx->file);
        if (e->src_file == NULL) {
            free(e);
            return NULL;
        }
    }
    return e;
}

static void expr_lex_free_cur(expr_lex_t *lx) {
    free(lx->cur.symbol);
    lx->cur.symbol = NULL;
}

/*
 * An unsigned integer literal at s[*inout_i]: decimal, 0x, 0b, or octal
 * for a leading 0.  The value is its 64 bits; one that needs more is
 * refused.
 */
static int expr_parse_number(const char *s, size_t *inout_i, long long *out) {
    size_t i = *inout_i;
    int base = 10;
    uint64_t v = 0;
    int saw = 0;

    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    } else if (s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B') &&
               (s[i + 2] == '0' || s[i + 2] == '1')) {
        base = 2;
        i += 2;
    } else if (s[i] == '0' && isdigit((unsigned char)s[i + 1])) {
        base = 8;
        i += 1;
    }

    while (s[i] != '\0') {
        int d = -1;
        if (s[i] >= '0' && s[i] <= '9') {
            d = s[i] - '0';
        } else if (base == 16 && s[i] >= 'a' && s[i] <= 'f') {
            d = 10 + s[i] - 'a';
        } else if (base == 16 && s[i] >= 'A' && s[i] <= 'F') {
            d = 10 + s[i] - 'A';
        }
        if (d < 0 || d >= base) {
            break;
        }
        saw = 1;
        if (v > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) {
            return -1;
        }
        v = v * (uint64_t)base + (uint64_t)d;
        i++;
    }
    if (!saw) {
        return -1;
    }
    *out = (long long)(int64_t)v;
    *inout_i = i;
    return 0;
}

int as_expr_parse_number(const char *s, long long *out) {
    size_t i = 0;
    int neg = 0;
    long long v;

    if (s == NULL || out == NULL) {
        return -1;
    }
    while (isspace((unsigned char)s[i])) {
        i++;
    }
    if (s[i] == '+' || s[i] == '-') {
        neg = s[i] == '-';
        i++;
    }
    if (!isdigit((unsigned char)s[i]) || expr_parse_number(s, &i, &v) != 0) {
        return -1;
    }
    while (isspace((unsigned char)s[i])) {
        i++;
    }
    if (s[i] != '\0') {
        return -1;
    }
    *out = neg ? (long long)(0ULL - (unsigned long long)v) : v;
    return 0;
}

/* The character after a backslash in a character constant. */
static int expr_char_escape(const char *s, size_t *inout_i) {
    size_t i = *inout_i;
    int c = (unsigned char)s[i];

    if (c >= '0' && c <= '7') {
        int v = 0, n = 0;

        while (n < 3 && s[i] >= '0' && s[i] <= '7') {
            v = v * 8 + (s[i] - '0');
            i++;
            n++;
        }
        *inout_i = i;
        return v & 0xff;
    }
    *inout_i = i + 1;
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'b': return '\b';
    case 'f': return '\f';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    default: return c;
    }
}

static int expr_op_tok(expr_lex_t *lx, as_expr_op_t op, size_t next) {
    lx->cur.kind = EXPR_TOK_OP;
    lx->cur.op = op;
    lx->i = next;
    return 0;
}

static int expr_lex_next(expr_lex_t *lx) {
    const char *s = lx->s;
    size_t i;

    expr_lex_free_cur(lx);
    memset(&lx->cur, 0, sizeof(lx->cur));

    i = lx->i;
    while (s[i] != '\0' && isspace((unsigned char)s[i])) {
        i++;
    }

    if (s[i] == '\0') {
        lx->cur.kind = EXPR_TOK_EOF;
        lx->i = i;
        return 0;
    }

    if (s[i] == '(') {
        lx->cur.kind = EXPR_TOK_LPAREN;
        lx->i = i + 1;
        return 0;
    }
    if (s[i] == ')') {
        lx->cur.kind = EXPR_TOK_RPAREN;
        lx->i = i + 1;
        return 0;
    }

    /* Two characters before one. */
    if (s[i] == '<' && s[i + 1] == '<') return expr_op_tok(lx, AS_EXPR_OP_SHL, i + 2);
    if (s[i] == '>' && s[i + 1] == '>') return expr_op_tok(lx, AS_EXPR_OP_SHR, i + 2);
    if (s[i] == '=' && s[i + 1] == '=') return expr_op_tok(lx, AS_EXPR_OP_EQ, i + 2);
    if (s[i] == '!' && s[i + 1] == '=') return expr_op_tok(lx, AS_EXPR_OP_NE, i + 2);
    if (s[i] == '<' && s[i + 1] == '>') return expr_op_tok(lx, AS_EXPR_OP_NE, i + 2);
    if (s[i] == '<' && s[i + 1] == '=') return expr_op_tok(lx, AS_EXPR_OP_LE, i + 2);
    if (s[i] == '>' && s[i + 1] == '=') return expr_op_tok(lx, AS_EXPR_OP_GE, i + 2);
    if (s[i] == '&' && s[i + 1] == '&') return expr_op_tok(lx, AS_EXPR_OP_LAND, i + 2);
    if (s[i] == '|' && s[i + 1] == '|') return expr_op_tok(lx, AS_EXPR_OP_LOR, i + 2);

    switch (s[i]) {
    case '<': return expr_op_tok(lx, AS_EXPR_OP_LT, i + 1);
    case '>': return expr_op_tok(lx, AS_EXPR_OP_GT, i + 1);
    case '+': return expr_op_tok(lx, AS_EXPR_OP_ADD, i + 1);
    case '-': return expr_op_tok(lx, AS_EXPR_OP_SUB, i + 1);
    case '*': return expr_op_tok(lx, AS_EXPR_OP_MUL, i + 1);
    case '/': return expr_op_tok(lx, AS_EXPR_OP_DIV, i + 1);
    case '%': return expr_op_tok(lx, AS_EXPR_OP_MOD, i + 1);
    case '|': return expr_op_tok(lx, AS_EXPR_OP_OR, i + 1);
    case '&': return expr_op_tok(lx, AS_EXPR_OP_AND, i + 1);
    case '^': return expr_op_tok(lx, AS_EXPR_OP_XOR, i + 1);
    case '~': return expr_op_tok(lx, AS_EXPR_OP_BNOT, i + 1);
    /* Infix it is or-not; where a value is wanted, logical not. */
    case '!': return expr_op_tok(lx, AS_EXPR_OP_ORNOT, i + 1);
    default: break;
    }

    /* 'c, with or without the closing quote. */
    if (s[i] == '\'' && s[i + 1] != '\0') {
        int c;

        i++;
        if (s[i] == '\\' && s[i + 1] != '\0') {
            i++;
            c = expr_char_escape(s, &i);
        } else {
            c = (unsigned char)s[i];
            i++;
        }
        if (s[i] == '\'') {
            i++;
        }
        lx->cur.kind = EXPR_TOK_NUMBER;
        lx->cur.number = c;
        lx->i = i;
        return 0;
    }

    if (isdigit((unsigned char)s[i])) {
        long long v = 0;
        size_t begin = i;
        size_t end = i;

        while (isdigit((unsigned char)s[end])) {
            end++;
        }
        /* A local label: digits, then f or b, then nothing of a name. */
        if (end > begin && (s[end] == 'f' || s[end] == 'b') &&
            !isalnum((unsigned char)s[end + 1]) && s[end + 1] != '_') {
            long long local_id = 0;
            size_t j;

            /* But 0b is a prefix where binary digits follow, and that
             * was settled above by the digit scan stopping at the b. */
            for (j = begin; j < end; ++j) {
                local_id = local_id * 10 + (long long)(s[j] - '0');
                if (local_id > INT_MAX) {
                    return -1;
                }
            }
            lx->cur.kind = EXPR_TOK_LOCAL;
            lx->cur.local_digit = (int)local_id;
            lx->cur.local_forward = (s[end] == 'f');
            lx->i = end + 1;
            return 0;
        }

        if (expr_parse_number(s, &i, &v) != 0) {
            return -1;
        }
        /* Digits that run on into a name are not a number: `12abc` is
         * nothing at all. */
        if (isalnum((unsigned char)s[i]) || s[i] == '_') {
            return -1;
        }
        lx->cur.kind = EXPR_TOK_NUMBER;
        lx->cur.number = v;
        lx->i = i;
        return 0;
    }

    if (isalpha((unsigned char)s[i]) || s[i] == '_' || s[i] == '.' || s[i] == '$') {
        size_t begin = i;
        size_t len;

        i++;
        while (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.' || s[i] == '$' || s[i] == '@') {
            i++;
        }
        len = i - begin;
        lx->cur.symbol = (char *)malloc(len + 1);
        if (lx->cur.symbol == NULL) {
            return -1;
        }
        memcpy(lx->cur.symbol, s + begin, len);
        lx->cur.symbol[len] = '\0';
        lx->cur.kind = EXPR_TOK_SYMBOL;
        lx->i = i;
        return 0;
    }

    return -1;
}

/* GNU as's four ranks of infix operator; -1 for what is not one. */
static int expr_precedence(as_expr_op_t op) {
    switch (op) {
    case AS_EXPR_OP_LOR:
        return 1;
    case AS_EXPR_OP_LAND:
        return 2;
    case AS_EXPR_OP_ADD:
    case AS_EXPR_OP_SUB:
    case AS_EXPR_OP_EQ:
    case AS_EXPR_OP_NE:
    case AS_EXPR_OP_LT:
    case AS_EXPR_OP_LE:
    case AS_EXPR_OP_GT:
    case AS_EXPR_OP_GE:
        return 3;
    case AS_EXPR_OP_OR:
    case AS_EXPR_OP_AND:
    case AS_EXPR_OP_XOR:
    case AS_EXPR_OP_ORNOT:
        return 4;
    case AS_EXPR_OP_MUL:
    case AS_EXPR_OP_DIV:
    case AS_EXPR_OP_MOD:
    case AS_EXPR_OP_SHL:
    case AS_EXPR_OP_SHR:
        return 5;
    default:
        return -1;
    }
}

static as_expr_t *parse_expr_bp(expr_lex_t *lx, int min_bp);

static as_expr_t *parse_expr_primary_inner(expr_lex_t *lx);

static as_expr_t *parse_expr_primary(expr_lex_t *lx) {
    as_expr_t *e;

    if (lx->depth >= EXPR_MAX_DEPTH) {
        return NULL;
    }
    lx->depth++;
    e = parse_expr_primary_inner(lx);
    lx->depth--;
    return e;
}

static as_expr_t *parse_expr_primary_inner(expr_lex_t *lx) {
    as_expr_t *e;

    if (lx->cur.kind == EXPR_TOK_NUMBER) {
        e = new_expr(lx, AS_EXPR_CONST);
        if (e == NULL) {
            return NULL;
        }
        e->value = lx->cur.number;
        if (expr_lex_next(lx) != 0) {
            as_expr_free(e);
            return NULL;
        }
        return e;
    }

    if (lx->cur.kind == EXPR_TOK_SYMBOL) {
        e = new_expr(lx, AS_EXPR_SYMBOL);
        if (e == NULL) {
            return NULL;
        }
        e->symbol = expr_strdup(lx->cur.symbol);
        if (e->symbol == NULL) {
            as_expr_free(e);
            return NULL;
        }
        if (expr_lex_next(lx) != 0) {
            as_expr_free(e);
            return NULL;
        }
        return e;
    }

    if (lx->cur.kind == EXPR_TOK_LOCAL) {
        e = new_expr(lx, AS_EXPR_LOCAL_REF);
        if (e == NULL) {
            return NULL;
        }
        e->local_digit = lx->cur.local_digit;
        e->local_forward = lx->cur.local_forward;
        if (expr_lex_next(lx) != 0) {
            as_expr_free(e);
            return NULL;
        }
        return e;
    }

    if (lx->cur.kind == EXPR_TOK_OP &&
        (lx->cur.op == AS_EXPR_OP_SUB || lx->cur.op == AS_EXPR_OP_BNOT ||
         lx->cur.op == AS_EXPR_OP_ADD || lx->cur.op == AS_EXPR_OP_ORNOT)) {
        as_expr_op_t uop = lx->cur.op;
        as_expr_t *rhs;

        if (expr_lex_next(lx) != 0) {
            return NULL;
        }
        rhs = parse_expr_primary(lx);
        if (rhs == NULL) {
            return NULL;
        }
        if (uop == AS_EXPR_OP_ADD) {
            return rhs;
        }

        e = new_expr(lx, AS_EXPR_UNARY);
        if (e == NULL) {
            as_expr_free(rhs);
            return NULL;
        }
        e->op = (uop == AS_EXPR_OP_SUB) ? AS_EXPR_OP_NEG
              : (uop == AS_EXPR_OP_BNOT) ? AS_EXPR_OP_BNOT : AS_EXPR_OP_LNOT;
        e->lhs = rhs;
        return e;
    }

    if (lx->cur.kind == EXPR_TOK_LPAREN) {
        if (expr_lex_next(lx) != 0) {
            return NULL;
        }
        e = parse_expr_bp(lx, 0);
        if (e == NULL) {
            return NULL;
        }
        if (lx->cur.kind != EXPR_TOK_RPAREN) {
            as_expr_free(e);
            return NULL;
        }
        if (expr_lex_next(lx) != 0) {
            as_expr_free(e);
            return NULL;
        }
        return e;
    }

    return NULL;
}

static as_expr_t *parse_expr_bp(expr_lex_t *lx, int min_bp) {
    as_expr_t *lhs;

    lhs = parse_expr_primary(lx);
    if (lhs == NULL) {
        return NULL;
    }

    while (lx->cur.kind == EXPR_TOK_OP) {
        as_expr_op_t op = lx->cur.op;
        int prec = expr_precedence(op);
        as_expr_t *rhs;
        as_expr_t *node;

        if (prec < 0 || prec < min_bp) {
            break;
        }
        if (expr_lex_next(lx) != 0) {
            as_expr_free(lhs);
            return NULL;
        }
        rhs = parse_expr_bp(lx, prec + 1);
        if (rhs == NULL) {
            as_expr_free(lhs);
            return NULL;
        }

        node = new_expr(lx, AS_EXPR_BINARY);
        if (node == NULL) {
            as_expr_free(lhs);
            as_expr_free(rhs);
            return NULL;
        }
        node->op = op;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }

    return lhs;
}

as_expr_t *as_parse_expr_string(const char *s, const char *file, unsigned line) {
    expr_lex_t lx;
    as_expr_t *e;

    if (s == NULL) {
        return NULL;
    }
    memset(&lx, 0, sizeof(lx));
    lx.s = s;
    lx.file = file;
    lx.line = line;
    if (expr_lex_next(&lx) != 0) {
        return NULL;
    }
    e = parse_expr_bp(&lx, 0);
    if (e == NULL || lx.cur.kind != EXPR_TOK_EOF) {
        as_expr_free(e);
        expr_lex_free_cur(&lx);
        return NULL;
    }
    expr_lex_free_cur(&lx);
    return e;
}

/* ------------------------------------------------------------------ */
/* Evaluation                                                          */
/* ------------------------------------------------------------------ */

/* One operator applied, in 64-bit two's-complement arithmetic. */
static int expr_apply_binary(as_expr_op_t op, long long l, long long r, long long *out) {
    unsigned long long ul = (unsigned long long)l;
    unsigned long long ur = (unsigned long long)r;

    switch (op) {
    case AS_EXPR_OP_ADD: *out = (long long)(ul + ur); return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_SUB: *out = (long long)(ul - ur); return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_MUL: *out = (long long)(ul * ur); return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_DIV:
        if (r == 0) return AS_EXPR_EVAL_ERROR;
        /* The one quotient that does not fit: it is its dividend. */
        *out = (l == LLONG_MIN && r == -1) ? l : l / r;
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_MOD:
        if (r == 0) return AS_EXPR_EVAL_ERROR;
        *out = (l == LLONG_MIN && r == -1) ? 0 : l % r;
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_OR: *out = l | r; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_AND: *out = l & r; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_XOR: *out = l ^ r; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_ORNOT: *out = l | ~r; return AS_EXPR_EVAL_OK;
    /* A count the word does not have shifts everything out; the right
     * shift is of the bits, not of the sign. */
    case AS_EXPR_OP_SHL:
        *out = (r < 0 || r > 63) ? 0 : (long long)(ul << r);
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_SHR:
        *out = (r < 0 || r > 63) ? 0 : (long long)(ul >> r);
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_EQ: *out = (l == r) ? -1 : 0; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_NE: *out = (l != r) ? -1 : 0; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_LT: *out = (l < r) ? -1 : 0; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_LE: *out = (l <= r) ? -1 : 0; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_GT: *out = (l > r) ? -1 : 0; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_GE: *out = (l >= r) ? -1 : 0; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_LAND: *out = (l != 0 && r != 0) ? 1 : 0; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_LOR: *out = (l != 0 || r != 0) ? 1 : 0; return AS_EXPR_EVAL_OK;
    default: return AS_EXPR_EVAL_NOT_CONST;
    }
}

static int expr_apply_unary(as_expr_op_t op, long long v, long long *out) {
    switch (op) {
    case AS_EXPR_OP_NEG: *out = (long long)(0ULL - (unsigned long long)v); return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_BNOT: *out = ~v; return AS_EXPR_EVAL_OK;
    case AS_EXPR_OP_LNOT: *out = (v == 0) ? 1 : 0; return AS_EXPR_EVAL_OK;
    default: return AS_EXPR_EVAL_NOT_CONST;
    }
}

int as_expr_eval(const as_expr_t *e, as_expr_lookup_fn lookup, void *cookie, long long *out) {
    long long l;
    long long r;
    int rc;

    if (e == NULL || out == NULL) {
        return AS_EXPR_EVAL_NOT_CONST;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        *out = e->value;
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_SYMBOL:
        if (lookup != NULL && e->symbol != NULL && lookup(cookie, e->symbol, out) == 1) {
            return AS_EXPR_EVAL_OK;
        }
        return AS_EXPR_EVAL_NOT_CONST;
    case AS_EXPR_UNARY:
        rc = as_expr_eval(e->lhs, lookup, cookie, &l);
        if (rc != AS_EXPR_EVAL_OK) {
            return rc;
        }
        return expr_apply_unary(e->op, l, out);
    case AS_EXPR_BINARY:
        rc = as_expr_eval(e->lhs, lookup, cookie, &l);
        if (rc != AS_EXPR_EVAL_OK) {
            return rc;
        }
        rc = as_expr_eval(e->rhs, lookup, cookie, &r);
        if (rc != AS_EXPR_EVAL_OK) {
            return rc;
        }
        return expr_apply_binary(e->op, l, r, out);
    default:
        return AS_EXPR_EVAL_NOT_CONST;
    }
}

int as_expr_eval_string(const char *s, as_expr_lookup_fn lookup, void *cookie, long long *out) {
    as_expr_t *e = as_parse_expr_string(s, NULL, 0);
    int rc;

    if (e == NULL) {
        return AS_EXPR_EVAL_NOT_CONST;
    }
    rc = as_expr_eval(e, lookup, cookie, out);
    as_expr_free(e);
    return rc;
}

static int linear_is_const(const as_expr_linear_t *v) {
    return v->add_symbol == NULL && v->sub_symbol == NULL &&
           v->add_local == NULL && v->sub_local == NULL;
}

static int linear_has_add(const as_expr_linear_t *v) {
    return v->add_symbol != NULL || v->add_local != NULL;
}

static int linear_has_sub(const as_expr_linear_t *v) {
    return v->sub_symbol != NULL || v->sub_local != NULL;
}

int as_expr_eval_linear(const as_expr_t *e, as_expr_lookup_fn lookup, void *cookie,
                        as_expr_linear_t *out) {
    as_expr_linear_t l;
    as_expr_linear_t r;
    int rc;

    if (e == NULL || out == NULL) {
        return AS_EXPR_EVAL_NOT_CONST;
    }
    memset(out, 0, sizeof(*out));
    switch (e->kind) {
    case AS_EXPR_CONST:
        out->value = e->value;
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_SYMBOL:
        if (e->symbol == NULL) {
            return AS_EXPR_EVAL_NOT_CONST;
        }
        if (lookup != NULL && lookup(cookie, e->symbol, &out->value) == 1) {
            return AS_EXPR_EVAL_OK;
        }
        out->add_symbol = e->symbol;
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_LOCAL_REF:
        out->add_local = e;
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_UNARY:
        rc = as_expr_eval_linear(e->lhs, lookup, cookie, &l);
        if (rc != AS_EXPR_EVAL_OK) {
            return rc;
        }
        if (linear_is_const(&l)) {
            return expr_apply_unary(e->op, l.value, &out->value);
        }
        /* -sym is the one thing to be done to a symbol alone, and only
         * to one that is not itself subtracted from. */
        if (e->op != AS_EXPR_OP_NEG || linear_has_sub(&l)) {
            return AS_EXPR_EVAL_NOT_CONST;
        }
        out->value = (long long)(0ULL - (unsigned long long)l.value);
        out->sub_symbol = l.add_symbol;
        out->sub_local = l.add_local;
        return AS_EXPR_EVAL_OK;
    case AS_EXPR_BINARY:
        rc = as_expr_eval_linear(e->lhs, lookup, cookie, &l);
        if (rc != AS_EXPR_EVAL_OK) {
            return rc;
        }
        rc = as_expr_eval_linear(e->rhs, lookup, cookie, &r);
        if (rc != AS_EXPR_EVAL_OK) {
            return rc;
        }
        if (linear_is_const(&l) && linear_is_const(&r)) {
            return expr_apply_binary(e->op, l.value, r.value, &out->value);
        }
        if (e->op == AS_EXPR_OP_SUB) {
            /* a - b is a + (-b): exchange b's two symbols. */
            as_expr_linear_t n = r;

            r.value = (long long)(0ULL - (unsigned long long)n.value);
            r.add_symbol = n.sub_symbol;
            r.add_local = n.sub_local;
            r.sub_symbol = n.add_symbol;
            r.sub_local = n.add_local;
        } else if (e->op != AS_EXPR_OP_ADD) {
            return AS_EXPR_EVAL_NOT_CONST;
        }
        if ((linear_has_add(&l) && linear_has_add(&r)) ||
            (linear_has_sub(&l) && linear_has_sub(&r))) {
            return AS_EXPR_EVAL_NOT_CONST;
        }
        *out = l;
        out->value = (long long)((unsigned long long)l.value + (unsigned long long)r.value);
        if (linear_has_add(&r)) {
            out->add_symbol = r.add_symbol;
            out->add_local = r.add_local;
        }
        if (linear_has_sub(&r)) {
            out->sub_symbol = r.sub_symbol;
            out->sub_local = r.sub_local;
        }
        return AS_EXPR_EVAL_OK;
    default:
        return AS_EXPR_EVAL_NOT_CONST;
    }
}
