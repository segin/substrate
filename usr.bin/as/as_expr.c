#include "as_expr.h"
#include "as_util.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
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

/*
 * Where the parser is among the tokens it was given.  `cur` is the next
 * of them as an expression reads it: a number has its value, a local
 * label its digit and direction, an operator which one it is.
 */
typedef struct {
    const as_token_t *tokv;
    size_t n;
    size_t i;
    /* A `%name` met where an operator is wanted is the remainder and a
     * name -- `a %b`, which the tokenizer cannot tell from a register --
     * and this is set between the two halves of it. */
    int half;
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
    free(e->reloc_op);
    free(e);
}

const char *as_expr_reloc_op(const as_expr_t *e) {
    const char *op;

    if (e == NULL) {
        return NULL;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->reloc_op != NULL) {
        return e->reloc_op;
    }
    op = as_expr_reloc_op(e->lhs);
    return op != NULL ? op : as_expr_reloc_op(e->rhs);
}

as_expr_t *as_expr_clone(const as_expr_t *e) {
    as_expr_t *copy;

    if (e == NULL) {
        return NULL;
    }
    copy = (as_expr_t *)calloc(1, sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }
    *copy = *e;
    copy->symbol = NULL;
    copy->reloc_op = NULL;
    copy->lhs = NULL;
    copy->rhs = NULL;
    if ((e->symbol != NULL && (copy->symbol = expr_strdup(e->symbol)) == NULL) ||
        (e->reloc_op != NULL && (copy->reloc_op = expr_strdup(e->reloc_op)) == NULL) ||
        (e->lhs != NULL && (copy->lhs = as_expr_clone(e->lhs)) == NULL) ||
        (e->rhs != NULL && (copy->rhs = as_expr_clone(e->rhs)) == NULL)) {
        as_expr_free(copy);
        return NULL;
    }
    return copy;
}

typedef struct {
    char *text;
    size_t len;
    size_t cap;
    int failed;
} expr_text_t;

static void expr_text_put(expr_text_t *t, const char *s) {
    size_t n = strlen(s);

    if (t->failed) {
        return;
    }
    if (t->len + n + 1 > t->cap) {
        size_t cap = t->cap != 0 ? t->cap * 2 : 64;
        char *grown;

        while (cap < t->len + n + 1) {
            cap *= 2;
        }
        grown = (char *)realloc(t->text, cap);
        if (grown == NULL) {
            t->failed = 1;
            return;
        }
        t->text = grown;
        t->cap = cap;
    }
    memcpy(t->text + t->len, s, n + 1);
    t->len += n;
}

static const char *expr_op_text(as_expr_op_t op) {
    switch (op) {
    case AS_EXPR_OP_ADD: return "+";
    case AS_EXPR_OP_SUB: return "-";
    case AS_EXPR_OP_MUL: return "*";
    case AS_EXPR_OP_DIV: return "/";
    case AS_EXPR_OP_MOD: return "%";
    case AS_EXPR_OP_OR: return "|";
    case AS_EXPR_OP_AND: return "&";
    case AS_EXPR_OP_XOR: return "^";
    case AS_EXPR_OP_SHL: return "<<";
    case AS_EXPR_OP_SHR: return ">>";
    case AS_EXPR_OP_EQ: return "==";
    case AS_EXPR_OP_NE: return "!=";
    case AS_EXPR_OP_LT: return "<";
    case AS_EXPR_OP_LE: return "<=";
    case AS_EXPR_OP_GT: return ">";
    case AS_EXPR_OP_GE: return ">=";
    case AS_EXPR_OP_NEG: return "-";
    case AS_EXPR_OP_BNOT: return "~";
    case AS_EXPR_OP_LNOT: return "!";
    case AS_EXPR_OP_ORNOT: return "!";
    case AS_EXPR_OP_LAND: return "&&";
    case AS_EXPR_OP_LOR: return "||";
    default: return NULL;
    }
}

static void expr_text_node(expr_text_t *t, const as_expr_t *e, int nested) {
    char num[32];
    const char *op;

    if (e == NULL) {
        t->failed = 1;
        return;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        snprintf(num, sizeof(num), e->value < 0 ? "(%lld)" : "%lld", e->value);
        expr_text_put(t, num);
        return;
    case AS_EXPR_SYMBOL:
        expr_text_put(t, e->symbol != NULL ? e->symbol : "");
        /* (As it was written, for whoever reads the text again.) */
        if (e->reloc_op != NULL) {
            expr_text_put(t, "@");
            expr_text_put(t, e->reloc_op);
        }
        return;
    case AS_EXPR_LOCAL_REF:
        snprintf(num, sizeof(num), "%d%c", e->local_digit, e->local_forward ? 'f' : 'b');
        expr_text_put(t, num);
        return;
    case AS_EXPR_UNARY:
        op = expr_op_text(e->op);
        if (op == NULL) {
            t->failed = 1;
            return;
        }
        expr_text_put(t, "(");
        expr_text_put(t, op);
        expr_text_node(t, e->lhs != NULL ? e->lhs : e->rhs, 1);
        expr_text_put(t, ")");
        return;
    case AS_EXPR_BINARY:
        op = expr_op_text(e->op);
        if (op == NULL) {
            t->failed = 1;
            return;
        }
        if (nested) {
            expr_text_put(t, "(");
        }
        expr_text_node(t, e->lhs, 1);
        expr_text_put(t, " ");
        expr_text_put(t, op);
        expr_text_put(t, " ");
        expr_text_node(t, e->rhs, 1);
        if (nested) {
            expr_text_put(t, ")");
        }
        return;
    default:
        t->failed = 1;
    }
}

char *as_expr_to_text(const as_expr_t *e) {
    expr_text_t t;

    memset(&t, 0, sizeof(t));
    expr_text_node(&t, e, 0);
    if (t.failed) {
        free(t.text);
        return NULL;
    }
    return t.text;
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
        e->src_file = as_source_unit(lx->file);
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

/* Which operator an operator token is; AS_EXPR_OP_NONE for one that is
 * none of an expression's. */
static as_expr_op_t expr_op_of(const char *s) {
    static const struct {
        const char *text;
        as_expr_op_t op;
    } ops[] = {
        {"<<", AS_EXPR_OP_SHL}, {">>", AS_EXPR_OP_SHR}, {"==", AS_EXPR_OP_EQ},   {"!=", AS_EXPR_OP_NE},
        {"<>", AS_EXPR_OP_NE},  {"<=", AS_EXPR_OP_LE},  {">=", AS_EXPR_OP_GE},   {"&&", AS_EXPR_OP_LAND},
        {"||", AS_EXPR_OP_LOR}, {"<", AS_EXPR_OP_LT},   {">", AS_EXPR_OP_GT},    {"+", AS_EXPR_OP_ADD},
        {"-", AS_EXPR_OP_SUB},  {"*", AS_EXPR_OP_MUL},  {"/", AS_EXPR_OP_DIV},   {"%", AS_EXPR_OP_MOD},
        {"|", AS_EXPR_OP_OR},   {"&", AS_EXPR_OP_AND},  {"^", AS_EXPR_OP_XOR},   {"~", AS_EXPR_OP_BNOT},
        /* Infix it is or-not; where a value is wanted, logical not. */
        {"!", AS_EXPR_OP_ORNOT},
    };
    size_t i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (as_strcmp(s, ops[i].text) == 0) {
            return ops[i].op;
        }
    }
    return AS_EXPR_OP_NONE;
}

static int expr_symbol_tok(expr_lex_t *lx, const char *prefix, const char *name, size_t take) {
    size_t plen = strlen(prefix);
    size_t nlen = strlen(name);

    lx->cur.symbol = (char *)malloc(plen + nlen + 1);
    if (lx->cur.symbol == NULL) {
        return -1;
    }
    memcpy(lx->cur.symbol, prefix, plen);
    memcpy(lx->cur.symbol + plen, name, nlen + 1);
    lx->cur.kind = EXPR_TOK_SYMBOL;
    lx->i += take;
    return 0;
}

/*
 * Whether a relocation operator stands after the name at hand as a
 * token of its own.  `free@PLT` is one token; `"free"@PLT` is two, the
 * name having ended at its quote -- and that is how a compiler writes
 * every name in Intel syntax.  The two were no expression, and no
 * instruction with one was assembled.
 */
static int expr_operator_follows(const expr_lex_t *lx) {
    const as_token_t *next;

    if (lx->i + 1 >= lx->n) {
        return 0;
    }
    next = &lx->tokv[lx->i + 1];
    return next->kind == AS_TOK_IDENTIFIER && next->text != NULL && next->text[0] == '@' && next->text[1] != '\0' &&
           next->text[1] != '@';
}

/* The next token, as an expression reads it. */
static int expr_lex_next(expr_lex_t *lx) {
    const as_token_t *t;
    const char *s;

    expr_lex_free_cur(lx);
    memset(&lx->cur, 0, sizeof(lx->cur));

    if (lx->i >= lx->n) {
        lx->cur.kind = EXPR_TOK_EOF;
        return 0;
    }
    t = &lx->tokv[lx->i];
    s = t->text;

    switch (t->kind) {
    case AS_TOK_NUMBER: {
        size_t digits = strspn(s, "0123456789");
        size_t at = 0;
        long long v = 0;

        /* A local label: digits, then f or b, then nothing.  (0b1 is a
         * binary number, and has more after the b.) */
        if (digits != 0 && (s[digits] == 'f' || s[digits] == 'b') && s[digits + 1] == '\0') {
            long long local_id = 0;
            size_t j;

            for (j = 0; j < digits; ++j) {
                local_id = local_id * 10 + (long long)(s[j] - '0');
                if (local_id > INT_MAX) {
                    return -1;
                }
            }
            lx->cur.kind = EXPR_TOK_LOCAL;
            lx->cur.local_digit = (int)local_id;
            lx->cur.local_forward = (s[digits] == 'f');
            lx->i++;
            return 0;
        }
        /* Digits that run on into a name are not a number, nor is one
         * with a fraction: `12abc` and `1.5` are nothing at all here. */
        if (expr_parse_number(s, &at, &v) != 0 || s[at] != '\0') {
            return -1;
        }
        lx->cur.kind = EXPR_TOK_NUMBER;
        lx->cur.number = v;
        lx->i++;
        return 0;
    }
    case AS_TOK_STRING:
        /* A name in quotes is a name. */
        if (expr_operator_follows(lx)) {
            return expr_symbol_tok(lx, s, lx->tokv[lx->i + 1].text, 2);
        }
        return expr_symbol_tok(lx, "", s, 1);
    case AS_TOK_IDENTIFIER:
    case AS_TOK_REGISTER:
    case AS_TOK_MNEMONIC:
    case AS_TOK_DIRECTIVE:
    case AS_TOK_LABEL:
        if (s[0] == '%') {
            if (!lx->half) {
                lx->half = 1;
                lx->cur.kind = EXPR_TOK_OP;
                lx->cur.op = AS_EXPR_OP_MOD;
                return 0;
            }
            lx->half = 0;
            return expr_symbol_tok(lx, "", s + 1, 1);
        }
        if (t->kind == AS_TOK_IDENTIFIER && strchr(s, '@') == NULL && expr_operator_follows(lx)) {
            return expr_symbol_tok(lx, s, lx->tokv[lx->i + 1].text, 2);
        }
        return expr_symbol_tok(lx, "", s, 1);
    case AS_TOK_OPERATOR:
        lx->cur.op = expr_op_of(s);
        if (lx->cur.op == AS_EXPR_OP_NONE) {
            return -1;
        }
        lx->cur.kind = EXPR_TOK_OP;
        lx->i++;
        return 0;
    case AS_TOK_PUNCT:
        /* Square brackets group as parentheses do, as in GNU as: the
         * `+[.-.L1]` of position-independent i386 code. */
        if (s[0] == '(' || s[0] == ')' || s[0] == '[' || s[0] == ']') {
            lx->cur.kind = (s[0] == '(' || s[0] == '[') ? EXPR_TOK_LPAREN : EXPR_TOK_RPAREN;
            lx->i++;
            return 0;
        }
        /* A name may begin with a dollar sign, which is otherwise a
         * token of its own. */
        if (s[0] == '$') {
            const as_token_t *next = lx->i + 1 < lx->n ? &lx->tokv[lx->i + 1] : NULL;

            if (next != NULL && !next->space_before &&
                (next->kind == AS_TOK_IDENTIFIER || next->kind == AS_TOK_NUMBER) && next->text[0] != '%') {
                return expr_symbol_tok(lx, "$", next->text, 2);
            }
            return expr_symbol_tok(lx, "$", "", 1);
        }
        return -1;
    default:
        return -1;
    }
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
        /*
         * `sym@OPERATOR`: the operator is taken off here, where the
         * symbol is read, and is the node's -- it was left in the name,
         * and everything that was handed the name looked for an `@` in
         * it.  A name that begins with one, or has two together (a
         * version, `f@@V1`, which no expression holds but a directive's
         * argument may be read as one), is left as it is.
         */
        {
            char *at = strchr(e->symbol, '@');

            if (at != NULL && at != e->symbol && at[1] != '\0' && at[1] != '@') {
                e->reloc_op = expr_strdup(at + 1);
                if (e->reloc_op == NULL) {
                    as_expr_free(e);
                    return NULL;
                }
                *at = '\0';
            }
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

as_expr_t *as_expr_parse_tokens(const as_token_t *tokv, size_t n, const char *file, unsigned line) {
    expr_lex_t lx;
    as_expr_t *e;

    if (tokv == NULL || n == 0) {
        return NULL;
    }
    memset(&lx, 0, sizeof(lx));
    lx.tokv = tokv;
    lx.n = n;
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

/* Text is made tokens by the tokenizer there is, and then read as
 * tokens: an expression in a string has no grammar of its own. */
static as_expr_t *parse_expr_text(const char *s, const char *file, unsigned line) {
    /* (Its room is kept from one text to the next: what the expression
     * keeps of a token, it copies.) */
    static as_token_vec_t toks;
    as_expr_t *e = NULL;

    as_token_vec_clear(&toks);
    if (as_lex_text_tokens(s, file, line, &toks) == 0) {
        e = as_expr_parse_tokens(toks.items, toks.count, file, line);
    }
    as_token_vec_clear(&toks);
    return e;
}

/*
 * The expressions read from text of late, each kept as it was read.
 *
 * The argument of a directive is text until it is wanted, and it is
 * wanted often: by each pass over the statements, and in each by every
 * way there is of taking it for a number.  `.long a - b` was made
 * tokens and read as an expression eighteen times, which was half the
 * time of assembling it.  What an expression is depends on its text and
 * on where it stands -- a `1b` in it is found from there -- and on
 * nothing else, so one read is as good as another.
 *
 * A slot for each of EXPR_KEPT hashes; what is kept in a slot gives way
 * to the next text that hashes there.  An entry whose expression is
 * NULL is a text that is no expression.
 */
#define EXPR_KEPT (1u << 20)

typedef struct {
    char *text;
    as_expr_t *expr;
} expr_kept_t;

static expr_kept_t *expr_kept;

/* (Kept as read from nowhere: where it stands is put in the copy that
 * is handed out, which is all that where it stands is to an
 * expression -- every node of one carries the same file and line.) */
static const as_expr_t *expr_of_text(const char *s, int *known) {
    unsigned long h = 5381ul;
    const char *p;
    expr_kept_t *k;
    char *text;

    *known = 0;
    if (expr_kept == NULL) {
        expr_kept = (expr_kept_t *)calloc(EXPR_KEPT, sizeof(*expr_kept));
        if (expr_kept == NULL) {
            return NULL;
        }
    }
    for (p = s; *p != '\0'; ++p) {
        h = h * 33ul + (unsigned char)*p;
    }
    k = &expr_kept[h % EXPR_KEPT];
    if (k->text != NULL && strcmp(k->text, s) == 0) {
        *known = 1;
        return k->expr;
    }
    text = expr_strdup(s);
    if (text == NULL) {
        return NULL;
    }
    free(k->text);
    as_expr_free(k->expr);
    k->text = text;
    k->expr = parse_expr_text(s, NULL, 0);
    *known = 1;
    return k->expr;
}

/* A copy of `e` that stands at `line` of `file`. */
static as_expr_t *expr_clone_at(const as_expr_t *e, const char *file, unsigned line) {
    as_expr_t *copy;

    if (e == NULL) {
        return NULL;
    }
    copy = (as_expr_t *)calloc(1, sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }
    *copy = *e;
    copy->symbol = NULL;
    copy->reloc_op = NULL;
    copy->src_file = file;
    copy->src_line = line;
    copy->lhs = NULL;
    copy->rhs = NULL;
    if ((e->symbol != NULL && (copy->symbol = expr_strdup(e->symbol)) == NULL) ||
        (e->reloc_op != NULL && (copy->reloc_op = expr_strdup(e->reloc_op)) == NULL) ||
        (e->lhs != NULL && (copy->lhs = expr_clone_at(e->lhs, file, line)) == NULL) ||
        (e->rhs != NULL && (copy->rhs = expr_clone_at(e->rhs, file, line)) == NULL)) {
        as_expr_free(copy);
        return NULL;
    }
    return copy;
}

as_expr_t *as_parse_expr_string(const char *s, const char *file, unsigned line) {
    const as_expr_t *kept;
    int known;

    if (s == NULL) {
        return NULL;
    }
    kept = expr_of_text(s, &known);
    if (!known) {
        return parse_expr_text(s, file, line);
    }
    /* The caller's own, to change and to free. */
    if (file != NULL && (file = as_source_unit(file)) == NULL) {
        return NULL;
    }
    return expr_clone_at(kept, file, line);
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
        /* (A symbol with a relocation operator on it is the linker's
         * to work out, whatever the symbol is set to here.) */
        if (lookup != NULL && e->symbol != NULL && e->reloc_op == NULL && lookup(cookie, e->symbol, out) == 1) {
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
    const as_expr_t *kept;
    as_expr_t *e;
    int known;
    int rc;

    if (s == NULL) {
        return AS_EXPR_EVAL_NOT_CONST;
    }
    /* (Read only, so the one that is kept will do.) */
    kept = expr_of_text(s, &known);
    if (known) {
        return kept != NULL ? as_expr_eval(kept, lookup, cookie, out) : AS_EXPR_EVAL_NOT_CONST;
    }
    e = parse_expr_text(s, NULL, 0);
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
        if (lookup != NULL && e->reloc_op == NULL && lookup(cookie, e->symbol, &out->value) == 1) {
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
