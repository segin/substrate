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
    TOK_KW_INT,
    TOK_KW_DOUBLE,
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
    TOK_ELLIPSIS,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_COLON,
    TOK_SEMI,
    TOK_ASSIGN,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH
} cc_tok_kind_t;

typedef struct {
    cc_tok_kind_t kind;
    const char *start;
    size_t len;
    long num;
    double fnum;
    int is_float;
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

static int parse_type_tok(parser_t *p, cc_type_t *out_type, const char *what) {
    if (p->tok.kind == TOK_KW_INT) {
        *out_type = CC_TYPE_INT;
    } else if (p->tok.kind == TOK_KW_DOUBLE) {
        *out_type = CC_TYPE_DOUBLE;
    } else if (p->tok.kind == TOK_KW_VOID) {
        *out_type = CC_TYPE_VOID;
    } else {
        set_diag(p->diag, p->tok.line, p->tok.col, what);
        return -1;
    }
    return next_tok(p);
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
    free_expr(s->expr);
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
            e->value_type = CC_TYPE_INT;
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
                    cc_expr_t *arg = parse_expr(p);
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

static cc_expr_t *parse_unary(parser_t *p) {
    if (p->tok.kind == TOK_MINUS) {
        cc_expr_t *z;
        cc_expr_t *rhs;
        cc_expr_t *e;

        if (next_tok(p) != 0) {
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            return NULL;
        }

        z = new_expr(CC_EXPR_INT);
        e = new_expr(CC_EXPR_BIN);
        if (z == NULL || e == NULL) {
            free_expr(z);
            free_expr(rhs);
            free_expr(e);
            return NULL;
        }
        z->int_val = 0;
        e->op = CC_BIN_SUB;
        e->lhs = z;
        e->rhs = rhs;
        return e;
    }
    return parse_primary(p);
}

static cc_expr_t *parse_mul(parser_t *p) {
    cc_expr_t *lhs = parse_unary(p);
    while (lhs != NULL && (p->tok.kind == TOK_STAR || p->tok.kind == TOK_SLASH)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        cc_expr_t *e;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        e = new_expr(CC_EXPR_BIN);
        if (e == NULL) {
            free_expr(lhs);
            free_expr(rhs);
            return NULL;
        }
        e->op = op == TOK_STAR ? CC_BIN_MUL : CC_BIN_DIV;
        e->lhs = lhs;
        e->rhs = rhs;
        lhs = e;
    }
    return lhs;
}

static cc_expr_t *parse_add(parser_t *p) {
    cc_expr_t *lhs = parse_mul(p);
    while (lhs != NULL && (p->tok.kind == TOK_PLUS || p->tok.kind == TOK_MINUS)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        cc_expr_t *e;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_mul(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        e = new_expr(CC_EXPR_BIN);
        if (e == NULL) {
            free_expr(lhs);
            free_expr(rhs);
            return NULL;
        }
        e->op = op == TOK_PLUS ? CC_BIN_ADD : CC_BIN_SUB;
        e->lhs = lhs;
        e->rhs = rhs;
        lhs = e;
    }
    return lhs;
}

static cc_expr_t *parse_rel(parser_t *p) {
    cc_expr_t *lhs = parse_add(p);
    while (lhs != NULL &&
           (p->tok.kind == TOK_LT || p->tok.kind == TOK_LE || p->tok.kind == TOK_GT || p->tok.kind == TOK_GE)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        cc_expr_t *e;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_add(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        e = new_expr(CC_EXPR_BIN);
        if (e == NULL) {
            free_expr(lhs);
            free_expr(rhs);
            return NULL;
        }
        if (op == TOK_LT) {
            e->op = CC_BIN_LT;
        } else if (op == TOK_LE) {
            e->op = CC_BIN_LE;
        } else if (op == TOK_GT) {
            e->op = CC_BIN_GT;
        } else {
            e->op = CC_BIN_GE;
        }
        e->lhs = lhs;
        e->rhs = rhs;
        lhs = e;
    }
    return lhs;
}

static cc_expr_t *parse_eq(parser_t *p) {
    cc_expr_t *lhs = parse_rel(p);
    while (lhs != NULL && (p->tok.kind == TOK_EQ || p->tok.kind == TOK_NE)) {
        cc_tok_kind_t op = p->tok.kind;
        cc_expr_t *rhs;
        cc_expr_t *e;
        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_rel(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }
        e = new_expr(CC_EXPR_BIN);
        if (e == NULL) {
            free_expr(lhs);
            free_expr(rhs);
            return NULL;
        }
        e->op = op == TOK_EQ ? CC_BIN_EQ : CC_BIN_NE;
        e->lhs = lhs;
        e->rhs = rhs;
        lhs = e;
    }
    return lhs;
}

static cc_expr_t *parse_assign(parser_t *p) {
    cc_expr_t *lhs = parse_eq(p);

    if (lhs != NULL && p->tok.kind == TOK_ASSIGN) {
        cc_expr_t *rhs;
        cc_expr_t *e;
        char *name;

        if (lhs->kind != CC_EXPR_IDENT) {
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

static cc_expr_t *parse_expr(parser_t *p) {
    return parse_assign(p);
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
            s->init_expr = parse_expr(p);
            if (s->init_expr == NULL) {
                return -1;
            }
        }
        if (expect(p, TOK_SEMI, "expected ';' after for-init") != 0) {
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

    if (p->tok.kind == TOK_KW_INT || p->tok.kind == TOK_KW_DOUBLE) {
        s->kind = CC_STMT_DECL;
        if (parse_type_tok(p, &s->type, "expected declaration type") != 0) {
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
        return expect(p, TOK_SEMI, "expected ';' after declaration");
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
        return 0;
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

        if (parse_type_tok(p, &ptype, "expected parameter type") != 0) {
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

    if (parse_type_tok(p, &f->ret_type, "expected function return type") != 0) {
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
    if (expect(p, TOK_LBRACE, "expected '{' before function body") != 0) {
        return -1;
    }

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
    if (out->func_count == 0) {
        set_diag(diag, 0, 0, "no function definitions found");
        return -1;
    }
    return 0;
}
