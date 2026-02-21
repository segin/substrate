#include "cc_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
} cc_lexer_t;

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUM,
    TOK_KW_AUTO,
    TOK_KW_BOOL,
    TOK_KW_CHAR,
    TOK_KW_CONST,
    TOK_KW_INT,
    TOK_KW_EXTERN,
    TOK_KW_FLOAT,
    TOK_KW_INLINE,
    TOK_KW_LONG,
    TOK_KW_REGISTER,
    TOK_KW_RESTRICT,
    TOK_KW_SHORT,
    TOK_KW_SIGNED,
    TOK_KW_STATIC,
    TOK_KW_UNSIGNED,
    TOK_KW_DOUBLE,
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
    TOK_COMMA,
    TOK_QUESTION,
    TOK_COLON,
    TOK_SEMI,
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
    long num;
    double fnum;
    int is_float;
    int int_is_unsigned;
    int int_is_longlong;
    size_t line;
    size_t col;
} cc_token_t;

int cc_lexer_init(cc_lexer_t *lx, const char *src, size_t len);
int cc_lexer_next(cc_lexer_t *lx, cc_token_t *out);

static char *xstrdup_n(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static void set_diag(cc_diag_t *d, size_t line, size_t col, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    d->line = line;
    d->col = col;
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

typedef struct {
    cc_lexer_t lx;
    cc_token_t tok;
    cc_diag_t *diag;
} parser_t;

static int next_tok(parser_t *p) {
    if (cc_lexer_next(&p->lx, &p->tok) != 0) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid token");
        return -1;
    }
    return 0;
}

static int expect(parser_t *p, cc_tok_kind_t k, const char *what) {
    if (p->tok.kind != k) {
        set_diag(p->diag, p->tok.line, p->tok.col, what);
        return -1;
    }
    return next_tok(p);
}

static cc_tok_kind_t peek_kind(parser_t *p) {
    cc_lexer_t lx = p->lx;
    cc_token_t t;
    if (cc_lexer_next(&lx, &t) != 0) {
        return TOK_EOF;
    }
    return t.kind;
}

static int is_declspec_tok(cc_tok_kind_t k) {
    switch (k) {
    case TOK_KW_AUTO:
    case TOK_KW_BOOL:
    case TOK_KW_CHAR:
    case TOK_KW_CONST:
    case TOK_KW_INT:
    case TOK_KW_EXTERN:
    case TOK_KW_FLOAT:
    case TOK_KW_INLINE:
    case TOK_KW_LONG:
    case TOK_KW_REGISTER:
    case TOK_KW_RESTRICT:
    case TOK_KW_SHORT:
    case TOK_KW_SIGNED:
    case TOK_KW_STATIC:
    case TOK_KW_UNSIGNED:
    case TOK_KW_DOUBLE:
    case TOK_KW_VOLATILE:
    case TOK_KW_VOID:
        return 1;
    default:
        return 0;
    }
}

static int parse_declspec(parser_t *p, cc_type_t *out_type, int allow_void, const char *what) {
    int seen = 0;
    int seen_type = 0;
    int seen_void = 0;
    int seen_bool = 0;
    int seen_char = 0;
    int seen_int = 0;
    int seen_float = 0;
    int seen_double = 0;
    int seen_long = 0;
    int seen_short = 0;
    int seen_signed = 0;
    int seen_unsigned = 0;

    while (is_declspec_tok(p->tok.kind)) {
        seen = 1;
        switch (p->tok.kind) {
        case TOK_KW_VOID:
            seen_void = 1;
            seen_type = 1;
            break;
        case TOK_KW_BOOL:
            seen_bool = 1;
            seen_type = 1;
            break;
        case TOK_KW_CHAR:
            seen_char = 1;
            seen_type = 1;
            break;
        case TOK_KW_INT:
            seen_int = 1;
            seen_type = 1;
            break;
        case TOK_KW_FLOAT:
            seen_float = 1;
            seen_type = 1;
            break;
        case TOK_KW_DOUBLE:
            seen_double = 1;
            seen_type = 1;
            break;
        case TOK_KW_LONG:
            seen_long++;
            seen_type = 1;
            break;
        case TOK_KW_SHORT:
            seen_short = 1;
            seen_type = 1;
            break;
        case TOK_KW_SIGNED:
            seen_signed = 1;
            seen_type = 1;
            break;
        case TOK_KW_UNSIGNED:
            seen_unsigned = 1;
            seen_type = 1;
            break;
        default:
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }

    if (!seen) {
        set_diag(p->diag, p->tok.line, p->tok.col, what);
        return -1;
    }
    if (!seen_type) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected type specifier in declaration");
        return -1;
    }

    if (seen_void) {
        if (!allow_void || seen_bool || seen_char || seen_int || seen_float || seen_double || seen_long || seen_short) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid use of void in declaration specifiers");
            return -1;
        }
        *out_type = CC_TYPE_VOID;
        return 0;
    }

    if (seen_signed && seen_unsigned) {
        set_diag(p->diag, p->tok.line, p->tok.col, "conflicting signed/unsigned in declaration specifiers");
        return -1;
    }

    if ((seen_signed || seen_unsigned) && (seen_float || seen_double || seen_bool || seen_void)) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid signed/unsigned type combination");
        return -1;
    }

    if (seen_double) {
        *out_type = CC_TYPE_DOUBLE;
        return 0;
    }
    if (seen_float) {
        *out_type = CC_TYPE_FLOAT;
        return 0;
    }
    if (seen_bool) {
        *out_type = CC_TYPE_BOOL;
        return 0;
    }
    if (seen_char) {
        *out_type = seen_unsigned ? CC_TYPE_UCHAR : CC_TYPE_CHAR;
        return 0;
    }
    if (seen_long > 0) {
        *out_type = seen_unsigned ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG;
        return 0;
    }
    if (seen_short || seen_int) {
        *out_type = seen_unsigned ? CC_TYPE_UINT : CC_TYPE_INT;
        return 0;
    }

    /* e.g. signed/unsigned without explicit base type => int */
    if (seen_signed || seen_unsigned) {
        *out_type = seen_unsigned ? CC_TYPE_UINT : CC_TYPE_INT;
        return 0;
    }

    *out_type = CC_TYPE_INT;
    return 0;
}

static cc_expr_t *new_expr(cc_expr_kind_t kind) {
    cc_expr_t *e = (cc_expr_t *)calloc(1, sizeof(*e));
    if (e != NULL) {
        e->kind = kind;
        e->value_type = CC_TYPE_INT;
    }
    return e;
}

static void free_expr(cc_expr_t *e) {
    size_t i;
    if (e == NULL) {
        return;
    }
    free(e->ident);
    free_expr(e->lhs);
    free_expr(e->rhs);
    free_expr(e->third);
    for (i = 0; i < e->arg_count; ++i) {
        free_expr(e->args[i]);
    }
    free(e->args);
    free(e);
}

static void free_stmt(cc_stmt_t *s) {
    size_t i;
    if (s == NULL) {
        return;
    }
    free(s->decl_name);
    free(s->label_name);
    free_expr(s->expr);
    if (s->init_stmt != NULL) {
        free_stmt(s->init_stmt);
        free(s->init_stmt);
    }
    free_expr(s->init_expr);
    free_expr(s->post_expr);
    if (s->then_branch != NULL) {
        free_stmt(s->then_branch);
        free(s->then_branch);
    }
    if (s->else_branch != NULL) {
        free_stmt(s->else_branch);
        free(s->else_branch);
    }
    for (i = 0; i < s->block_count; ++i) {
        free_stmt(&s->block_stmts[i]);
    }
    free(s->block_stmts);
}

static int push_arg(cc_expr_t *call, cc_expr_t *arg) {
    cc_expr_t **next = (cc_expr_t **)realloc(call->args, (call->arg_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    call->args = next;
    call->args[call->arg_count++] = arg;
    return 0;
}

static int push_param(cc_function_t *f, cc_type_t type, const char *name, size_t n) {
    cc_param_t *next = (cc_param_t *)realloc(f->params, (f->param_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    f->params = next;
    f->params[f->param_count].name = xstrdup_n(name, n);
    if (f->params[f->param_count].name == NULL) {
        return -1;
    }
    f->params[f->param_count].type = type;
    f->param_count++;
    return 0;
}

static int push_stmt_arr(cc_stmt_t **arr, size_t *count, cc_stmt_t s) {
    cc_stmt_t *next = (cc_stmt_t *)realloc(*arr, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *arr = next;
    (*arr)[(*count)++] = s;
    return 0;
}

static int push_stmt_func(cc_function_t *f, cc_stmt_t s) {
    return push_stmt_arr(&f->stmts, &f->stmt_count, s);
}

static int trigraph_replacement(int c) {
    switch (c) {
    case '=':
        return '#';
    case '/':
        return '\\';
    case '\'':
        return '^';
    case '(':
        return '[';
    case ')':
        return ']';
    case '!':
        return '|';
    case '<':
        return '{';
    case '>':
        return '}';
    case '-':
        return '~';
    default:
        return -1;
    }
}

static size_t normalize_c95_trigraphs(char *buf, size_t len) {
    size_t r = 0;
    size_t w = 0;

    while (r < len) {
        if (r + 2 < len && buf[r] == '?' && buf[r + 1] == '?') {
            int repl = trigraph_replacement((unsigned char)buf[r + 2]);
            if (repl >= 0) {
                buf[w++] = (char)repl;
                r += 3;
                continue;
            }
        }
        buf[w++] = buf[r++];
    }
    return w;
}

static cc_expr_t *parse_expr(parser_t *p);
static cc_expr_t *parse_assign(parser_t *p);
static int parse_stmt(parser_t *p, cc_stmt_t *s);

static cc_expr_t *parse_primary(parser_t *p) {
    cc_expr_t *e;

    if (p->tok.kind == TOK_NUM) {
        if (p->tok.is_float) {
            e = new_expr(CC_EXPR_FLOAT);
            if (e == NULL) {
                return NULL;
            }
            e->float_val = p->tok.fnum;
            e->value_type = CC_TYPE_DOUBLE;
        } else {
            e = new_expr(CC_EXPR_INT);
            if (e == NULL) {
                return NULL;
            }
            e->int_val = p->tok.num;
            if (p->tok.int_is_longlong) {
                e->value_type = p->tok.int_is_unsigned ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG;
            } else {
                e->value_type = p->tok.int_is_unsigned ? CC_TYPE_UINT : CC_TYPE_INT;
            }
        }
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }

    if (p->tok.kind == TOK_IDENT) {
        char *name = xstrdup_n(p->tok.start, p->tok.len);
        if (name == NULL) {
            return NULL;
        }
        if (next_tok(p) != 0) {
            free(name);
            return NULL;
        }

        if (p->tok.kind == TOK_LPAREN) {
            e = new_expr(CC_EXPR_CALL);
            if (e == NULL) {
                free(name);
                return NULL;
            }
            e->ident = name;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            if (p->tok.kind != TOK_RPAREN) {
                for (;;) {
                    cc_expr_t *arg = parse_assign(p);
                    if (arg == NULL) {
                        free_expr(e);
                        return NULL;
                    }
                    if (push_arg(e, arg) != 0) {
                        free_expr(e);
                        return NULL;
                    }
                    if (p->tok.kind != TOK_COMMA) {
                        break;
                    }
                    if (next_tok(p) != 0) {
                        free_expr(e);
                        return NULL;
                    }
                }
            }
            if (expect(p, TOK_RPAREN, "expected ')' after call arguments") != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }

        e = new_expr(CC_EXPR_IDENT);
        if (e == NULL) {
            free(name);
            return NULL;
        }
        e->ident = name;
        return e;
    }

    if (p->tok.kind == TOK_LPAREN) {
        if (next_tok(p) != 0) {
            return NULL;
        }
        e = parse_expr(p);
        if (e == NULL) {
            return NULL;
        }
        if (expect(p, TOK_RPAREN, "expected ')' after expression") != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }

    set_diag(p->diag, p->tok.line, p->tok.col, "expected primary expression");
    return NULL;
}

static cc_expr_t *new_int_expr(long v) {
    cc_expr_t *e = new_expr(CC_EXPR_INT);
    if (e != NULL) {
        e->int_val = v;
        e->value_type = CC_TYPE_INT;
    }
    return e;
}

static cc_expr_t *new_ident_expr(const char *name) {
    cc_expr_t *e = new_expr(CC_EXPR_IDENT);
    if (e == NULL) {
        return NULL;
    }
    e->ident = xstrdup_n(name, strlen(name));
    if (e->ident == NULL) {
        free(e);
        return NULL;
    }
    return e;
}

static cc_expr_t *new_bin_expr(cc_binop_t op, cc_expr_t *lhs, cc_expr_t *rhs) {
    cc_expr_t *e = new_expr(CC_EXPR_BIN);
    if (e == NULL) {
        free_expr(lhs);
        free_expr(rhs);
        return NULL;
    }
    e->op = op;
    e->lhs = lhs;
    e->rhs = rhs;
    return e;
}

static cc_expr_t *new_update_expr(const char *name, cc_binop_t op, int postfix) {
    cc_expr_t *e = new_expr(CC_EXPR_UPDATE);
    if (e == NULL) {
        return NULL;
    }
    e->ident = xstrdup_n(name, strlen(name));
    if (e->ident == NULL) {
        free_expr(e);
        return NULL;
    }
    e->op = op;
    e->update_postfix = postfix;
    return e;
}

static cc_expr_t *parse_postfix(parser_t *p) {
    cc_expr_t *e = parse_primary(p);
    while (e != NULL && (p->tok.kind == TOK_PLUS_PLUS || p->tok.kind == TOK_MINUS_MINUS)) {
        cc_expr_t *upd;
        if (e->kind != CC_EXPR_IDENT || e->ident == NULL) {
            set_diag(p->diag, p->tok.line, p->tok.col, "++/-- requires an identifier lvalue");
            free_expr(e);
            return NULL;
        }
        upd = new_update_expr(e->ident, p->tok.kind == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 1);
        free_expr(e);
        if (upd == NULL) {
            return NULL;
        }
        e = upd;
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
    }
    return e;
}

static cc_expr_t *parse_unary(parser_t *p) {
    if (p->tok.kind == TOK_KW_SIZEOF) {
        cc_expr_t *e;
        if (next_tok(p) != 0) {
            return NULL;
        }
        e = new_expr(CC_EXPR_SIZEOF);
        if (e == NULL) {
            return NULL;
        }
        if (p->tok.kind == TOK_LPAREN && is_declspec_tok(peek_kind(p))) {
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            if (parse_declspec(p, &e->aux_type, 1, "expected type name in sizeof") != 0) {
                free_expr(e);
                return NULL;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after sizeof type") != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
        e->lhs = parse_unary(p);
        if (e->lhs == NULL) {
            free_expr(e);
            return NULL;
        }
        e->aux_type = CC_TYPE_VOID;
        return e;
    }

    if (p->tok.kind == TOK_LPAREN && is_declspec_tok(peek_kind(p))) {
        cc_expr_t *e = new_expr(CC_EXPR_CAST);
        if (e == NULL) {
            return NULL;
        }
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        if (parse_declspec(p, &e->aux_type, 1, "expected cast type") != 0) {
            free_expr(e);
            return NULL;
        }
        if (expect(p, TOK_RPAREN, "expected ')' after cast type") != 0) {
            free_expr(e);
            return NULL;
        }
        e->lhs = parse_unary(p);
        if (e->lhs == NULL) {
            free_expr(e);
            return NULL;
        }
        return e;
    }

    if (p->tok.kind == TOK_MINUS) {
        cc_expr_t *z;
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            return NULL;
        }
        z = new_int_expr(0);
        if (z == NULL) {
            free_expr(rhs);
            return NULL;
        }
        return new_bin_expr(CC_BIN_SUB, z, rhs);
    }
    if (p->tok.kind == TOK_PLUS) {
        if (next_tok(p) != 0) {
            return NULL;
        }
        return parse_unary(p);
    }
    if (p->tok.kind == TOK_BANG) {
        cc_expr_t *rhs;
        cc_expr_t *z;
        if (next_tok(p) != 0) {
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            return NULL;
        }
        z = new_int_expr(0);
        if (z == NULL) {
            free_expr(rhs);
            return NULL;
        }
        return new_bin_expr(CC_BIN_EQ, rhs, z);
    }
    if (p->tok.kind == TOK_TILDE) {
        cc_expr_t *rhs;
        cc_expr_t *mask;
        if (next_tok(p) != 0) {
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            return NULL;
        }
        mask = new_int_expr(-1);
        if (mask == NULL) {
            free_expr(rhs);
            return NULL;
        }
        return new_bin_expr(CC_BIN_BXOR, rhs, mask);
    }
    if (p->tok.kind == TOK_PLUS_PLUS || p->tok.kind == TOK_MINUS_MINUS) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        cc_expr_t *upd;
        if (next_tok(p) != 0) {
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            return NULL;
        }
        if (rhs->kind != CC_EXPR_IDENT || rhs->ident == NULL) {
            set_diag(p->diag, p->tok.line, p->tok.col, "++/-- requires an identifier lvalue");
            free_expr(rhs);
            return NULL;
        }
        upd = new_update_expr(rhs->ident, op == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 0);
        free_expr(rhs);
        return upd;
    }
    return parse_postfix(p);
}

static cc_expr_t *parse_mul(parser_t *p) {
    cc_expr_t *lhs = parse_unary(p);
    while (lhs != NULL &&
           (p->tok.kind == TOK_STAR || p->tok.kind == TOK_SLASH || p->tok.kind == TOK_PERCENT)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        if (op == TOK_STAR) {
            lhs = new_bin_expr(CC_BIN_MUL, lhs, rhs);
        } else if (op == TOK_SLASH) {
            lhs = new_bin_expr(CC_BIN_DIV, lhs, rhs);
        } else {
            lhs = new_bin_expr(CC_BIN_MOD, lhs, rhs);
        }
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_add(parser_t *p) {
    cc_expr_t *lhs = parse_mul(p);
    while (lhs != NULL && (p->tok.kind == TOK_PLUS || p->tok.kind == TOK_MINUS)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_mul(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(op == TOK_PLUS ? CC_BIN_ADD : CC_BIN_SUB, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_shift(parser_t *p) {
    cc_expr_t *lhs = parse_add(p);
    while (lhs != NULL && (p->tok.kind == TOK_LSHIFT || p->tok.kind == TOK_RSHIFT)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_add(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(op == TOK_LSHIFT ? CC_BIN_SHL : CC_BIN_SHR, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_rel(parser_t *p) {
    cc_expr_t *lhs = parse_shift(p);
    while (lhs != NULL &&
           (p->tok.kind == TOK_LT || p->tok.kind == TOK_LE || p->tok.kind == TOK_GT || p->tok.kind == TOK_GE)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        cc_binop_t bop;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_shift(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        if (op == TOK_LT) {
            bop = CC_BIN_LT;
        } else if (op == TOK_LE) {
            bop = CC_BIN_LE;
        } else if (op == TOK_GT) {
            bop = CC_BIN_GT;
        } else {
            bop = CC_BIN_GE;
        }
        lhs = new_bin_expr(bop, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_eq(parser_t *p) {
    cc_expr_t *lhs = parse_rel(p);
    while (lhs != NULL && (p->tok.kind == TOK_EQ || p->tok.kind == TOK_NE)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_rel(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(op == TOK_EQ ? CC_BIN_EQ : CC_BIN_NE, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_band(parser_t *p) {
    cc_expr_t *lhs = parse_eq(p);
    while (lhs != NULL && p->tok.kind == TOK_AMP) {
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_eq(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(CC_BIN_BAND, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_bxor(parser_t *p) {
    cc_expr_t *lhs = parse_band(p);
    while (lhs != NULL && p->tok.kind == TOK_CARET) {
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_band(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(CC_BIN_BXOR, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_bor(parser_t *p) {
    cc_expr_t *lhs = parse_bxor(p);
    while (lhs != NULL && p->tok.kind == TOK_PIPE) {
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_bxor(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(CC_BIN_BOR, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_land(parser_t *p) {
    cc_expr_t *lhs = parse_bor(p);
    while (lhs != NULL && p->tok.kind == TOK_AND_AND) {
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_bor(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(CC_BIN_LAND, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_lor(parser_t *p) {
    cc_expr_t *lhs = parse_land(p);
    while (lhs != NULL && p->tok.kind == TOK_OR_OR) {
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_land(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(CC_BIN_LOR, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_cond(parser_t *p) {
    cc_expr_t *cond = parse_lor(p);
    if (cond == NULL) {
        return NULL;
    }
    if (p->tok.kind == TOK_QUESTION) {
        cc_expr_t *e;
        if (next_tok(p) != 0) {
            free_expr(cond);
            return NULL;
        }
        e = new_expr(CC_EXPR_TERNARY);
        if (e == NULL) {
            free_expr(cond);
            return NULL;
        }
        e->lhs = cond;
        e->rhs = parse_expr(p);
        if (e->rhs == NULL) {
            free_expr(e);
            return NULL;
        }
        if (expect(p, TOK_COLON, "expected ':' in conditional expression") != 0) {
            free_expr(e);
            return NULL;
        }
        e->third = parse_cond(p);
        if (e->third == NULL) {
            free_expr(e);
            return NULL;
        }
        return e;
    }
    return cond;
}

static cc_expr_t *parse_assign(parser_t *p) {
    cc_expr_t *lhs = parse_cond(p);

    if (lhs != NULL && (p->tok.kind == TOK_ASSIGN || p->tok.kind == TOK_PLUS_EQ || p->tok.kind == TOK_MINUS_EQ ||
                        p->tok.kind == TOK_STAR_EQ || p->tok.kind == TOK_SLASH_EQ || p->tok.kind == TOK_PERCENT_EQ ||
                        p->tok.kind == TOK_LSHIFT_EQ || p->tok.kind == TOK_RSHIFT_EQ || p->tok.kind == TOK_AND_EQ ||
                        p->tok.kind == TOK_XOR_EQ || p->tok.kind == TOK_OR_EQ)) {
        cc_tok_kind_t aop = p->tok.kind;
        cc_expr_t *rhs;
        cc_expr_t *e;
        char *name;

        if (lhs->kind != CC_EXPR_IDENT || lhs->ident == NULL) {
            set_diag(p->diag, p->tok.line, p->tok.col, "left-hand side of assignment must be an identifier");
            free_expr(lhs);
            return NULL;
        }

        name = lhs->ident;
        lhs->ident = NULL;
        free_expr(lhs);

        if (next_tok(p) != 0) {
            free(name);
            return NULL;
        }
        rhs = parse_assign(p);
        if (rhs == NULL) {
            free(name);
            return NULL;
        }

        if (aop != TOK_ASSIGN) {
            cc_expr_t *lhs_read = new_ident_expr(name);
            cc_binop_t bop;
            if (lhs_read == NULL) {
                free(name);
                free_expr(rhs);
                return NULL;
            }
            if (aop == TOK_PLUS_EQ) {
                bop = CC_BIN_ADD;
            } else if (aop == TOK_MINUS_EQ) {
                bop = CC_BIN_SUB;
            } else if (aop == TOK_STAR_EQ) {
                bop = CC_BIN_MUL;
            } else if (aop == TOK_SLASH_EQ) {
                bop = CC_BIN_DIV;
            } else if (aop == TOK_PERCENT_EQ) {
                bop = CC_BIN_MOD;
            } else if (aop == TOK_LSHIFT_EQ) {
                bop = CC_BIN_SHL;
            } else if (aop == TOK_RSHIFT_EQ) {
                bop = CC_BIN_SHR;
            } else if (aop == TOK_AND_EQ) {
                bop = CC_BIN_BAND;
            } else if (aop == TOK_XOR_EQ) {
                bop = CC_BIN_BXOR;
            } else {
                bop = CC_BIN_BOR;
            }
            rhs = new_bin_expr(bop, lhs_read, rhs);
            if (rhs == NULL) {
                free(name);
                return NULL;
            }
        }

        e = new_expr(CC_EXPR_ASSIGN);
        if (e == NULL) {
            free(name);
            free_expr(rhs);
            return NULL;
        }
        e->ident = name;
        e->rhs = rhs;
        return e;
    }

    return lhs;
}

static cc_expr_t *parse_comma(parser_t *p) {
    cc_expr_t *lhs = parse_assign(p);
    while (lhs != NULL && p->tok.kind == TOK_COMMA) {
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_assign(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        lhs = new_bin_expr(CC_BIN_COMMA, lhs, rhs);
        if (lhs == NULL) {
            return NULL;
        }
    }
    return lhs;
}

static cc_expr_t *parse_expr(parser_t *p) {
    return parse_comma(p);
}

static int parse_decl_stmt(parser_t *p, cc_stmt_t *s, int need_semi) {
    memset(s, 0, sizeof(*s));
    s->kind = CC_STMT_DECL;
    if (parse_declspec(p, &s->type, 0, "expected declaration type") != 0) {
        return -1;
    }
    if (p->tok.kind != TOK_IDENT) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier after declaration type");
        return -1;
    }
    s->decl_name = xstrdup_n(p->tok.start, p->tok.len);
    if (s->decl_name == NULL) {
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    if (p->tok.kind == TOK_ASSIGN) {
        if (next_tok(p) != 0) {
            return -1;
        }
        s->expr = parse_expr(p);
        if (s->expr == NULL) {
            return -1;
        }
    }
    if (need_semi) {
        return expect(p, TOK_SEMI, "expected ';' after declaration");
    }
    return 0;
}

static int parse_block_stmt(parser_t *p, cc_stmt_t *s) {
    memset(s, 0, sizeof(*s));
    s->kind = CC_STMT_BLOCK;
    if (expect(p, TOK_LBRACE, "expected '{'") != 0) {
        return -1;
    }
    while (p->tok.kind != TOK_RBRACE) {
        cc_stmt_t child;
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unexpected end of file in block");
            return -1;
        }
        if (parse_stmt(p, &child) != 0) {
            free_stmt(&child);
            return -1;
        }
        if (push_stmt_arr(&s->block_stmts, &s->block_count, child) != 0) {
            free_stmt(&child);
            return -1;
        }
    }
    return expect(p, TOK_RBRACE, "expected '}' after block");
}

static int parse_stmt(parser_t *p, cc_stmt_t *s) {
    memset(s, 0, sizeof(*s));

    if (p->tok.kind == TOK_LBRACE) {
        return parse_block_stmt(p, s);
    }

    if (p->tok.kind == TOK_IDENT && peek_kind(p) == TOK_COLON) {
        s->kind = CC_STMT_LABEL;
        s->label_name = xstrdup_n(p->tok.start, p->tok.len);
        if (s->label_name == NULL) {
            return -1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        if (expect(p, TOK_COLON, "expected ':' after label") != 0) {
            return -1;
        }
        s->then_branch = (cc_stmt_t *)calloc(1, sizeof(*s->then_branch));
        if (s->then_branch == NULL) {
            return -1;
        }
        return parse_stmt(p, s->then_branch);
    }

    if (p->tok.kind == TOK_KW_IF) {
        s->kind = CC_STMT_IF;
        if (next_tok(p) != 0) {
            return -1;
        }
        if (expect(p, TOK_LPAREN, "expected '(' after if") != 0) {
            return -1;
        }
        s->expr = parse_expr(p);
        if (s->expr == NULL) {
            return -1;
        }
        if (expect(p, TOK_RPAREN, "expected ')' after if condition") != 0) {
            return -1;
        }
        s->then_branch = (cc_stmt_t *)calloc(1, sizeof(*s->then_branch));
        if (s->then_branch == NULL) {
            return -1;
        }
        if (parse_stmt(p, s->then_branch) != 0) {
            return -1;
        }
        if (p->tok.kind == TOK_KW_ELSE) {
            if (next_tok(p) != 0) {
                return -1;
            }
            s->else_branch = (cc_stmt_t *)calloc(1, sizeof(*s->else_branch));
            if (s->else_branch == NULL) {
                return -1;
            }
            if (parse_stmt(p, s->else_branch) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (p->tok.kind == TOK_KW_WHILE) {
        s->kind = CC_STMT_WHILE;
        if (next_tok(p) != 0) {
            return -1;
        }
        if (expect(p, TOK_LPAREN, "expected '(' after while") != 0) {
            return -1;
        }
        s->expr = parse_expr(p);
        if (s->expr == NULL) {
            return -1;
        }
        if (expect(p, TOK_RPAREN, "expected ')' after while condition") != 0) {
            return -1;
        }
        s->then_branch = (cc_stmt_t *)calloc(1, sizeof(*s->then_branch));
        if (s->then_branch == NULL) {
            return -1;
        }
        if (parse_stmt(p, s->then_branch) != 0) {
            return -1;
        }
        return 0;
    }

    if (p->tok.kind == TOK_KW_DO) {
        s->kind = CC_STMT_DO;
        if (next_tok(p) != 0) {
            return -1;
        }
        s->then_branch = (cc_stmt_t *)calloc(1, sizeof(*s->then_branch));
        if (s->then_branch == NULL) {
            return -1;
        }
        if (parse_stmt(p, s->then_branch) != 0) {
            return -1;
        }
        if (expect(p, TOK_KW_WHILE, "expected 'while' after do statement body") != 0) {
            return -1;
        }
        if (expect(p, TOK_LPAREN, "expected '(' after while") != 0) {
            return -1;
        }
        s->expr = parse_expr(p);
        if (s->expr == NULL) {
            return -1;
        }
        if (expect(p, TOK_RPAREN, "expected ')' after do-while condition") != 0) {
            return -1;
        }
        return expect(p, TOK_SEMI, "expected ';' after do-while");
    }

    if (p->tok.kind == TOK_KW_FOR) {
        s->kind = CC_STMT_FOR;
        if (next_tok(p) != 0) {
            return -1;
        }
        if (expect(p, TOK_LPAREN, "expected '(' after for") != 0) {
            return -1;
        }
        if (p->tok.kind != TOK_SEMI) {
            if (is_declspec_tok(p->tok.kind)) {
                s->init_stmt = (cc_stmt_t *)calloc(1, sizeof(*s->init_stmt));
                if (s->init_stmt == NULL) {
                    return -1;
                }
                if (parse_decl_stmt(p, s->init_stmt, 1) != 0) {
                    return -1;
                }
            } else {
                s->init_expr = parse_expr(p);
                if (s->init_expr == NULL) {
                    return -1;
                }
                if (expect(p, TOK_SEMI, "expected ';' after for-init") != 0) {
                    return -1;
                }
            }
        } else if (expect(p, TOK_SEMI, "expected ';' after for-init") != 0) {
            return -1;
        }
        if (p->tok.kind != TOK_SEMI) {
            s->expr = parse_expr(p);
            if (s->expr == NULL) {
                return -1;
            }
        }
        if (expect(p, TOK_SEMI, "expected ';' after for-condition") != 0) {
            return -1;
        }
        if (p->tok.kind != TOK_RPAREN) {
            s->post_expr = parse_expr(p);
            if (s->post_expr == NULL) {
                return -1;
            }
        }
        if (expect(p, TOK_RPAREN, "expected ')' after for clauses") != 0) {
            return -1;
        }
        s->then_branch = (cc_stmt_t *)calloc(1, sizeof(*s->then_branch));
        if (s->then_branch == NULL) {
            return -1;
        }
        if (parse_stmt(p, s->then_branch) != 0) {
            return -1;
        }
        return 0;
    }

    if (p->tok.kind == TOK_KW_SWITCH) {
        s->kind = CC_STMT_SWITCH;
        if (next_tok(p) != 0) {
            return -1;
        }
        if (expect(p, TOK_LPAREN, "expected '(' after switch") != 0) {
            return -1;
        }
        s->expr = parse_expr(p);
        if (s->expr == NULL) {
            return -1;
        }
        if (expect(p, TOK_RPAREN, "expected ')' after switch expression") != 0) {
            return -1;
        }
        s->then_branch = (cc_stmt_t *)calloc(1, sizeof(*s->then_branch));
        if (s->then_branch == NULL) {
            return -1;
        }
        if (parse_stmt(p, s->then_branch) != 0) {
            return -1;
        }
        return 0;
    }

    if (p->tok.kind == TOK_KW_CASE) {
        s->kind = CC_STMT_CASE;
        if (next_tok(p) != 0) {
            return -1;
        }
        s->expr = parse_expr(p);
        if (s->expr == NULL) {
            return -1;
        }
        return expect(p, TOK_COLON, "expected ':' after case expression");
    }

    if (p->tok.kind == TOK_KW_DEFAULT) {
        s->kind = CC_STMT_DEFAULT;
        if (next_tok(p) != 0) {
            return -1;
        }
        return expect(p, TOK_COLON, "expected ':' after default");
    }

    if (p->tok.kind == TOK_KW_BREAK) {
        s->kind = CC_STMT_BREAK;
        if (next_tok(p) != 0) {
            return -1;
        }
        return expect(p, TOK_SEMI, "expected ';' after break");
    }

    if (p->tok.kind == TOK_KW_CONTINUE) {
        s->kind = CC_STMT_CONTINUE;
        if (next_tok(p) != 0) {
            return -1;
        }
        return expect(p, TOK_SEMI, "expected ';' after continue");
    }

    if (p->tok.kind == TOK_KW_GOTO) {
        s->kind = CC_STMT_GOTO;
        if (next_tok(p) != 0) {
            return -1;
        }
        if (p->tok.kind != TOK_IDENT) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected label identifier after goto");
            return -1;
        }
        s->label_name = xstrdup_n(p->tok.start, p->tok.len);
        if (s->label_name == NULL) {
            return -1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        return expect(p, TOK_SEMI, "expected ';' after goto");
    }

    if (is_declspec_tok(p->tok.kind)) {
        return parse_decl_stmt(p, s, 1);
    }

    if (p->tok.kind == TOK_KW_RETURN) {
        s->kind = CC_STMT_RETURN;
        if (next_tok(p) != 0) {
            return -1;
        }
        if (p->tok.kind != TOK_SEMI) {
            s->expr = parse_expr(p);
            if (s->expr == NULL) {
                return -1;
            }
        }
        return expect(p, TOK_SEMI, "expected ';' after return statement");
    }

    s->kind = CC_STMT_EXPR;
    s->expr = parse_expr(p);
    if (s->expr == NULL) {
        return -1;
    }
    return expect(p, TOK_SEMI, "expected ';' after expression");
}

static void free_func(cc_function_t *f) {
    size_t i;
    free(f->name);
    for (i = 0; i < f->param_count; ++i) {
        free(f->params[i].name);
    }
    free(f->params);
    for (i = 0; i < f->stmt_count; ++i) {
        free_stmt(&f->stmts[i]);
    }
    free(f->stmts);
}

void cc_tu_free(cc_translation_unit_t *tu) {
    size_t i;
    if (tu == NULL) {
        return;
    }
    for (i = 0; i < tu->func_count; ++i) {
        free_func(&tu->funcs[i]);
    }
    free(tu->funcs);
    tu->funcs = NULL;
    tu->func_count = 0;
}

static int parse_params(parser_t *p, cc_function_t *f) {
    if (p->tok.kind == TOK_KW_VOID) {
        if (next_tok(p) != 0) {
            return -1;
        }
        if (p->tok.kind == TOK_RPAREN) {
            return 0;
        }
        set_diag(p->diag, p->tok.line, p->tok.col, "void must be the sole token in an empty parameter list");
        return -1;
    }

    while (p->tok.kind != TOK_RPAREN) {
        cc_type_t ptype;

        if (p->tok.kind == TOK_ELLIPSIS) {
            f->is_variadic = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            break;
        }

        if (parse_declspec(p, &ptype, 1, "expected parameter type") != 0) {
            return -1;
        }
        if (ptype == CC_TYPE_VOID) {
            set_diag(p->diag, p->tok.line, p->tok.col, "void is not a valid named parameter type");
            return -1;
        }

        if (p->tok.kind != TOK_IDENT) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected parameter name");
            return -1;
        }

        if (push_param(f, ptype, p->tok.start, p->tok.len) != 0) {
            return -1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }

        if (p->tok.kind != TOK_COMMA) {
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        if (p->tok.kind == TOK_RPAREN) {
            set_diag(p->diag, p->tok.line, p->tok.col, "trailing comma in parameter list");
            return -1;
        }
    }

    return 0;
}

static int parse_function(parser_t *p, cc_function_t *f) {
    memset(f, 0, sizeof(*f));
    f->has_body = 0;

    if (parse_declspec(p, &f->ret_type, 1, "expected function return type") != 0) {
        return -1;
    }
    if (p->tok.kind != TOK_IDENT) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected function name");
        return -1;
    }
    f->name = xstrdup_n(p->tok.start, p->tok.len);
    if (f->name == NULL) {
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }

    if (expect(p, TOK_LPAREN, "expected '(' after function name") != 0) {
        return -1;
    }
    if (p->tok.kind != TOK_RPAREN) {
        if (parse_params(p, f) != 0) {
            return -1;
        }
    }
    if (expect(p, TOK_RPAREN, "expected ')' after parameter list") != 0) {
        return -1;
    }
    if (p->tok.kind == TOK_SEMI) {
        if (next_tok(p) != 0) {
            return -1;
        }
        f->has_body = 0;
        return 0;
    }
    if (expect(p, TOK_LBRACE, "expected '{' before function body") != 0) {
        return -1;
    }
    f->has_body = 1;

    while (p->tok.kind != TOK_RBRACE) {
        cc_stmt_t s;
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unexpected end of file in function body");
            return -1;
        }
        if (parse_stmt(p, &s) != 0) {
            free_stmt(&s);
            return -1;
        }
        if (push_stmt_func(f, s) != 0) {
            free_stmt(&s);
            return -1;
        }
    }

    if (expect(p, TOK_RBRACE, "expected '}' after function body") != 0) {
        return -1;
    }
    return 0;
}

int cc_parse_file(const char *path, cc_translation_unit_t *out, cc_diag_t *diag) {
    FILE *fp;
    long sz;
    char *buf;
    parser_t p;

    memset(out, 0, sizeof(*out));
    if (diag != NULL) {
        diag->line = 0;
        diag->col = 0;
        diag->message[0] = '\0';
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        set_diag(diag, 0, 0, "failed to open source file");
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        set_diag(diag, 0, 0, "failed to seek source file");
        return -1;
    }
    sz = ftell(fp);
    if (sz < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        set_diag(diag, 0, 0, "failed to size source file");
        return -1;
    }

    buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(fp);
        set_diag(diag, 0, 0, "out of memory");
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        set_diag(diag, 0, 0, "failed to read source file");
        return -1;
    }
    buf[sz] = '\0';
    fclose(fp);
    sz = (long)normalize_c95_trigraphs(buf, (size_t)sz);
    buf[sz] = '\0';

    memset(&p, 0, sizeof(p));
    p.diag = diag;
    cc_lexer_init(&p.lx, buf, (size_t)sz);
    if (next_tok(&p) != 0) {
        free(buf);
        cc_tu_free(out);
        return -1;
    }

    while (p.tok.kind != TOK_EOF) {
        cc_function_t f;
        cc_function_t *next = (cc_function_t *)realloc(out->funcs, (out->func_count + 1) * sizeof(*next));
        if (next == NULL) {
            free(buf);
            cc_tu_free(out);
            set_diag(diag, 0, 0, "out of memory");
            return -1;
        }
        out->funcs = next;

        if (parse_function(&p, &f) != 0) {
            free(buf);
            cc_tu_free(out);
            return -1;
        }
        out->funcs[out->func_count++] = f;
    }

    free(buf);
    return 0;
}
