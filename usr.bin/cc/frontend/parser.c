#include "cc_frontend.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    long num;
    double fnum;
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

typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    int depth;
} typedef_entry_t;

typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    int depth;
} var_entry_t;

typedef struct {
    char *name;
    long value;
} enum_const_entry_t;

typedef struct {
    char *tag;
    cc_type_t type;
    int complete;
} enum_tag_entry_t;

int cc_lexer_init(cc_lexer_t *lx, const char *src, size_t len, const char *path);
void cc_lexer_deinit(cc_lexer_t *lx);
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

static const char *g_parser_diag_file = NULL;

static void set_diag(cc_diag_t *d, size_t line, size_t col, const char *msg) {
    if (d == NULL) {
        return;
    }
    if (g_parser_diag_file != NULL && g_parser_diag_file[0] != '\0') {
        snprintf(d->path, sizeof(d->path), "%s", g_parser_diag_file);
    } else {
        d->path[0] = '\0';
    }
    d->line = line;
    d->col = col;
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

static int diag_is_oom(const cc_diag_t *d) {
    if (d == NULL || d->message[0] == '\0') {
        return 0;
    }
    return strstr(d->message, "out of memory") != NULL;
}

static void diag_clear(cc_diag_t *d) {
    if (d == NULL) {
        return;
    }
    d->path[0] = '\0';
    d->line = 0;
    d->col = 0;
    d->message[0] = '\0';
}

static void diag_print_source_line(const cc_diag_t *d) {
    FILE *fp;
    char buf[1024];
    size_t cur = 1;
    size_t i;
    size_t col;
    size_t len;

    if (d == NULL || d->path[0] == '\0' || d->line == 0) {
        return;
    }
    fp = fopen(d->path, "r");
    if (fp == NULL) {
        return;
    }
    while (cur < d->line && fgets(buf, sizeof(buf), fp) != NULL) {
        cur++;
    }
    if (cur == d->line && fgets(buf, sizeof(buf), fp) != NULL) {
        len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
            buf[--len] = '\0';
        }
        fprintf(stderr, "    %s\n", buf);
        col = d->col > 0 ? d->col : 1;
        fprintf(stderr, "    ");
        for (i = 1; i < col; ++i) {
            if (i - 1 < len && buf[i - 1] == '\t') {
                fputc('\t', stderr);
            } else {
                fputc(' ', stderr);
            }
        }
        fprintf(stderr, "^\n");
    }
    fclose(fp);
}

static void diag_report_and_clear(cc_diag_t *d) {
    if (d == NULL || d->message[0] == '\0') {
        return;
    }
    if (d->line != 0) {
        if (d->path[0] != '\0') {
            fprintf(stderr, "%s:%zu:%zu: error: %s\n", d->path, d->line, d->col, d->message);
            diag_print_source_line(d);
        } else {
            fprintf(stderr, "cc:%zu:%zu: error: %s\n", d->line, d->col, d->message);
        }
    } else if (d->path[0] != '\0') {
        fprintf(stderr, "%s: error: %s\n", d->path, d->message);
    } else {
        fprintf(stderr, "cc: error: %s\n", d->message);
    }
    d->error_count++;
    diag_clear(d);
}

typedef struct {
    cc_lexer_t lx;
    cc_token_t tok;
    cc_diag_t *diag;
    cc_translation_unit_t *tu;
    typedef_entry_t *typedefs;
    size_t typedef_count;
    size_t typedef_cap;
    var_entry_t *vars;
    size_t var_count;
    size_t var_cap;
    cc_struct_def_t *structs;
    size_t struct_count;
    size_t struct_cap;
    enum_const_entry_t *enum_consts;
    size_t enum_const_count;
    size_t enum_const_cap;
    enum_tag_entry_t *enum_tags;
    size_t enum_tag_count;
    size_t enum_tag_cap;
    cc_function_t *hoisted_funcs;
    size_t hoisted_func_count;
    size_t hoisted_func_cap;
    int scope_depth;
    int last_storage;
} parser_t;

static int next_tok(parser_t *p);

static void set_ptr_depth_diag(parser_t *p, int marker_line) {
    (void)marker_line;
    set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
}

static const cc_function_t *parser_find_function_decl(parser_t *p, const char *name) {
    const cc_function_t *best = NULL;
    int best_score = -1;
    size_t i;

    if (p == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }

    if (p->tu != NULL) {
        for (i = 0; i < p->tu->func_count; ++i) {
            const cc_function_t *f = &p->tu->funcs[i];
            int score;
            if (strcmp(f->name, name) != 0) {
                continue;
            }
            score = (f->has_prototype ? 2 : 0) + (f->has_body ? 1 : 0);
            if (best == NULL || score > best_score) {
                best = f;
                best_score = score;
            }
        }
    }

    for (i = 0; i < p->hoisted_func_count; ++i) {
        const cc_function_t *f = &p->hoisted_funcs[i];
        int score;
        if (strcmp(f->name, name) != 0) {
            continue;
        }
        score = (f->has_prototype ? 2 : 0) + (f->has_body ? 1 : 0);
        if (best == NULL || score > best_score) {
            best = f;
            best_score = score;
        }
    }

    return best;
}

static int parser_sync_toplevel(parser_t *p) {
    int brace_depth = 0;

    if (p == NULL) {
        return -1;
    }
    while (p->tok.kind != TOK_EOF) {
        if (p->tok.kind == TOK_LBRACE) {
            brace_depth++;
        } else if (p->tok.kind == TOK_RBRACE) {
            if (brace_depth == 0) {
                if (next_tok(p) != 0) {
                    return -1;
                }
                return 0;
            }
            brace_depth--;
            if (brace_depth == 0) {
                if (next_tok(p) != 0) {
                    return -1;
                }
                return 0;
            }
        } else if (brace_depth == 0 && p->tok.kind == TOK_SEMI) {
            if (next_tok(p) != 0) {
                return -1;
            }
            return 0;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parser_sync_block_stmt(parser_t *p) {
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;

    if (p == NULL) {
        return -1;
    }

    while (p->tok.kind != TOK_EOF) {
        if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
            if (p->tok.kind == TOK_SEMI) {
                if (next_tok(p) != 0) {
                    return -1;
                }
                return 0;
            }
            if (p->tok.kind == TOK_RBRACE || p->tok.kind == TOK_KW_CASE || p->tok.kind == TOK_KW_DEFAULT) {
                return 0;
            }
        }

        if (p->tok.kind == TOK_LPAREN) {
            paren_depth++;
        } else if (p->tok.kind == TOK_RPAREN) {
            if (paren_depth > 0) {
                paren_depth--;
            }
        } else if (p->tok.kind == TOK_LBRACE) {
            brace_depth++;
        } else if (p->tok.kind == TOK_RBRACE) {
            if (brace_depth > 0) {
                brace_depth--;
            } else {
                return 0;
            }
        } else if (p->tok.kind == TOK_LBRACK) {
            bracket_depth++;
        } else if (p->tok.kind == TOK_RBRACK) {
            if (bracket_depth > 0) {
                bracket_depth--;
            }
        }

        if (next_tok(p) != 0) {
            return -1;
        }
    }
    return 0;
}

typedef struct {
    int flags;
    long align;
    char *section;
    char *alias;
} decl_attrs_t;

static int g_parser_pointer_size_bytes = 8;
static int g_parser_enable_trigraphs = 1;
static int g_parser_allow_oldstyle_funcdecl = 0;
static int g_parser_std_c11 = 0;
static int g_parser_std_c17 = 0;
static int g_parser_std_c23 = 0;
static int g_parser_std_gnu = 0;
static int g_parser_gnu89_inline = 0;
static int is_decl_qual_tok(cc_tok_kind_t k);
static int is_decl_qual_at_token(parser_t *p);
static cc_type_t ptr_of_type(cc_type_t t);
static int tok_is_ident(parser_t *p, const char *s);
static int tok_is_gnu_attribute_kw(parser_t *p);
static int token_is_gnu_attribute_kw(const cc_token_t *t);
static int is_ptr_declarator_tok(cc_tok_kind_t kind);
static void decl_attrs_reset(decl_attrs_t *a);
static void decl_attrs_clear(decl_attrs_t *a);
static int decl_attrs_merge(decl_attrs_t *dst, const decl_attrs_t *src);
static int parse_c23_attribute_seq(parser_t *p, decl_attrs_t *out_attrs, int *out_stmt_flags);
static int skip_balanced_parens(parser_t *p);
static int parse_gnu_attr_arguments(parser_t *p, long *out_num, int *out_has_num, char **out_str);
static int skip_decl_gnu_suffix(parser_t *p, decl_attrs_t *out_attrs);
static int parse_asm_stmt(parser_t *p, cc_stmt_t *s);
static int parse_type_name(parser_t *p, cc_type_t *out_type, int *out_struct_id, int allow_void, const char *what);
static int infer_expr_type(parser_t *p, const cc_expr_t *e, cc_type_t *out_type, int *out_struct_id);
static cc_expr_t *parse_expr(parser_t *p);
static cc_expr_t *parse_cond(parser_t *p);
static cc_expr_t *new_bin_expr(cc_binop_t op, cc_expr_t *lhs, cc_expr_t *rhs);
static void free_expr(cc_expr_t *e);
static void free_stmt(cc_stmt_t *s);
static int eval_const_array_bound_expr(parser_t *p, const cc_expr_t *e, long *out);
static int parse_array_extent(parser_t *p, long *out_n, int *out_const_n);
static int parse_params(parser_t *p, cc_function_t *f);
static int parse_function(parser_t *p, cc_function_t *f);
static int function_param_index_by_name(const cc_function_t *f, const char *name);
static cc_type_t adjust_oldstyle_param_type(cc_type_t ty, int array_ndim);
static int probe_is_function_head(parser_t *p);
static void free_func(cc_function_t *f);
static int enum_tag_find_n(const parser_t *p, const char *tag, size_t len);
static int enum_tag_set(parser_t *p, const char *tag, size_t len, cc_type_t type, int complete);
static void parser_free_enum_tags(parser_t *p);

static int parser_push_hoisted_func(parser_t *p, const cc_function_t *f) {
    cc_function_t *next;
    if (p == NULL || f == NULL) {
        return -1;
    }
    if (p->hoisted_func_count == p->hoisted_func_cap) {
        size_t ncap = p->hoisted_func_cap == 0 ? 8 : p->hoisted_func_cap * 2;
        next = (cc_function_t *)realloc(p->hoisted_funcs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        p->hoisted_funcs = next;
        p->hoisted_func_cap = ncap;
    }
    p->hoisted_funcs[p->hoisted_func_count++] = *f;
    return 0;
}

static void parser_free_hoisted_funcs(parser_t *p) {
    size_t i;
    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->hoisted_func_count; ++i) {
        free_func(&p->hoisted_funcs[i]);
    }
    free(p->hoisted_funcs);
    p->hoisted_funcs = NULL;
    p->hoisted_func_count = 0;
    p->hoisted_func_cap = 0;
}

static int tu_push_function(cc_translation_unit_t *tu, const cc_function_t *f) {
    cc_function_t *next;
    if (tu == NULL || f == NULL) {
        return -1;
    }
    next = (cc_function_t *)realloc(tu->funcs, (tu->func_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    tu->funcs = next;
    tu->funcs[tu->func_count++] = *f;
    return 0;
}

static int parser_is_c11_or_newer(void) {
    return g_parser_std_c11 || g_parser_std_c17 || g_parser_std_c23;
}

static int parser_is_c23_or_newer(void) {
    return g_parser_std_c23;
}

static int parser_is_gnu_mode(void) {
    return g_parser_std_gnu;
}

static int parser_relax_static_asserts(void) {
    return parser_is_gnu_mode();
}

static int is_ptr_declarator_tok(cc_tok_kind_t kind) {
    if (kind == TOK_STAR) {
        return 1;
    }
    if (kind == TOK_CARET && parser_is_gnu_mode()) {
        return 1;
    }
    return 0;
}

static int tok_is_typeof_spelling(parser_t *p) {
    return tok_is_ident(p, "typeof") || tok_is_ident(p, "typeof_unqual") || tok_is_ident(p, "__typeof") ||
           tok_is_ident(p, "__typeof_unqual") || tok_is_ident(p, "__typeof__") ||
           tok_is_ident(p, "__typeof_unqual__");
}

static int next_tok(parser_t *p) {
    if (cc_lexer_next(&p->lx, &p->tok) != 0) {
        g_parser_diag_file = p->lx.logical_file;
        set_diag(p->diag, p->lx.line, p->lx.col, "invalid token");
        return -1;
    }
    g_parser_diag_file = p->tok.file;
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
    if (p->lx.logical_file != NULL) {
        lx.logical_file = xstrdup_n(p->lx.logical_file, strlen(p->lx.logical_file));
        if (lx.logical_file == NULL) {
            return TOK_EOF;
        }
    } else {
        lx.logical_file = NULL;
    }
    if (cc_lexer_next(&lx, &t) != 0) {
        cc_lexer_deinit(&lx);
        return TOK_EOF;
    }
    cc_lexer_deinit(&lx);
    return t.kind;
}

static int peek_tok(parser_t *p, cc_token_t *out) {
    cc_lexer_t lx = p->lx;
    int rc;
    if (p->lx.logical_file != NULL) {
        lx.logical_file = xstrdup_n(p->lx.logical_file, strlen(p->lx.logical_file));
        if (lx.logical_file == NULL) {
            return -1;
        }
    } else {
        lx.logical_file = NULL;
    }
    rc = cc_lexer_next(&lx, out);
    cc_lexer_deinit(&lx);
    return rc;
}

static int typedef_find_visible_n(const parser_t *p, const char *name, size_t len) {
    size_t i = p->typedef_count;
    while (i > 0) {
        i--;
        if (strlen(p->typedefs[i].name) == len && strncmp(p->typedefs[i].name, name, len) == 0 &&
            p->typedefs[i].depth <= p->scope_depth) {
            return (int)i;
        }
    }
    return -1;
}

static int typedef_find_current_scope_n(const parser_t *p, const char *name, size_t len) {
    size_t i = p->typedef_count;
    while (i > 0) {
        i--;
        if (p->typedefs[i].depth < p->scope_depth) {
            break;
        }
        if (p->typedefs[i].depth == p->scope_depth && strlen(p->typedefs[i].name) == len &&
            strncmp(p->typedefs[i].name, name, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int typedef_push(parser_t *p, const char *name, cc_type_t type, int struct_id,
                        long array_len, int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS]) {
    typedef_entry_t *next;
    char *dup;
    int existing;
    long norm_dims[CC_MAX_ARRAY_DIMS];

    if (array_dims != NULL) {
        memcpy(norm_dims, array_dims, sizeof(norm_dims));
    } else {
        memset(norm_dims, 0, sizeof(norm_dims));
    }

    existing = typedef_find_current_scope_n(p, name, strlen(name));
    if (existing >= 0) {
        if (p->typedefs[existing].type == type && p->typedefs[existing].struct_id == struct_id &&
            p->typedefs[existing].array_len == array_len && p->typedefs[existing].array_ndim == array_ndim &&
            memcmp(p->typedefs[existing].array_dims, norm_dims, sizeof(p->typedefs[existing].array_dims)) == 0) {
            return 0;
        }
        return 1;
    }
    if (p->typedef_count == p->typedef_cap) {
        size_t ncap = p->typedef_cap == 0 ? 16 : p->typedef_cap * 2;
        next = (typedef_entry_t *)realloc(p->typedefs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        p->typedefs = next;
        p->typedef_cap = ncap;
    }
    dup = xstrdup_n(name, strlen(name));
    if (dup == NULL) {
        return -1;
    }
    p->typedefs[p->typedef_count].name = dup;
    p->typedefs[p->typedef_count].type = type;
    p->typedefs[p->typedef_count].struct_id = struct_id;
    p->typedefs[p->typedef_count].array_len = array_len;
    p->typedefs[p->typedef_count].array_ndim = array_ndim;
    memcpy(p->typedefs[p->typedef_count].array_dims, norm_dims, sizeof(p->typedefs[p->typedef_count].array_dims));
    p->typedefs[p->typedef_count].depth = p->scope_depth;
    p->typedef_count++;
    return 0;
}

static void typedef_pop_to_depth(parser_t *p, int depth) {
    while (p->typedef_count > 0 && p->typedefs[p->typedef_count - 1].depth > depth) {
        free(p->typedefs[p->typedef_count - 1].name);
        p->typedef_count--;
    }
}

static void parser_free_typedefs(parser_t *p) {
    size_t i;
    for (i = 0; i < p->typedef_count; ++i) {
        free(p->typedefs[i].name);
    }
    free(p->typedefs);
    p->typedefs = NULL;
    p->typedef_count = 0;
    p->typedef_cap = 0;
}

static int var_find_visible_n(const parser_t *p, const char *name, size_t len) {
    size_t i = p->var_count;
    while (i > 0) {
        i--;
        if (strlen(p->vars[i].name) == len && strncmp(p->vars[i].name, name, len) == 0 &&
            p->vars[i].depth <= p->scope_depth) {
            return (int)i;
        }
    }
    return -1;
}

static int var_push(parser_t *p, const char *name, cc_type_t type, int struct_id, int array_ndim,
                    const long array_dims[CC_MAX_ARRAY_DIMS]) {
    var_entry_t *next;
    char *dup;
    int i;
    size_t len;
    long norm_dims[CC_MAX_ARRAY_DIMS];

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (array_ndim < 0) {
        array_ndim = 0;
    }
    if (array_ndim > CC_MAX_ARRAY_DIMS) {
        array_ndim = CC_MAX_ARRAY_DIMS;
    }
    memset(norm_dims, 0, sizeof(norm_dims));
    if (array_dims != NULL && array_ndim > 0) {
        memcpy(norm_dims, array_dims, (size_t)array_ndim * sizeof(norm_dims[0]));
    }
    len = strlen(name);
    for (i = (int)p->var_count - 1; i >= 0; --i) {
        if (p->vars[i].depth < p->scope_depth) {
            break;
        }
        if (p->vars[i].depth == p->scope_depth && strlen(p->vars[i].name) == len &&
            strncmp(p->vars[i].name, name, len) == 0) {
            p->vars[i].type = type;
            p->vars[i].struct_id = struct_id;
            p->vars[i].array_ndim = array_ndim;
            memcpy(p->vars[i].array_dims, norm_dims, sizeof(p->vars[i].array_dims));
            return 0;
        }
    }
    if (p->var_count == p->var_cap) {
        size_t ncap = p->var_cap == 0 ? 32 : p->var_cap * 2;
        next = (var_entry_t *)realloc(p->vars, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        p->vars = next;
        p->var_cap = ncap;
    }
    dup = xstrdup_n(name, len);
    if (dup == NULL) {
        return -1;
    }
    p->vars[p->var_count].name = dup;
    p->vars[p->var_count].type = type;
    p->vars[p->var_count].struct_id = struct_id;
    p->vars[p->var_count].array_ndim = array_ndim;
    memcpy(p->vars[p->var_count].array_dims, norm_dims, sizeof(p->vars[p->var_count].array_dims));
    p->vars[p->var_count].depth = p->scope_depth;
    p->var_count++;
    return 0;
}

static void var_pop_to_depth(parser_t *p, int depth) {
    while (p->var_count > 0 && p->vars[p->var_count - 1].depth > depth) {
        free(p->vars[p->var_count - 1].name);
        p->var_count--;
    }
}

static void struct_hide_to_depth(parser_t *p, int depth) {
    size_t i;
    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->struct_count; ++i) {
        if (p->structs[i].depth > depth) {
            /*
             * Preserve parsed type records for semantic/lowering consumers,
             * but hide out-of-scope tags from future parser lookups.
             */
            p->structs[i].depth = INT_MAX;
        }
    }
}

static void parser_free_vars(parser_t *p) {
    size_t i;
    for (i = 0; i < p->var_count; ++i) {
        free(p->vars[i].name);
    }
    free(p->vars);
    p->vars = NULL;
    p->var_count = 0;
    p->var_cap = 0;
}

static int enum_const_find_visible_n(const parser_t *p, const char *name, size_t len) {
    size_t i = p->enum_const_count;
    while (i > 0) {
        i--;
        if (strlen(p->enum_consts[i].name) == len && strncmp(p->enum_consts[i].name, name, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int enum_const_push(parser_t *p, const char *name, size_t len, long value) {
    enum_const_entry_t *next;
    char *dup;
    if (enum_const_find_visible_n(p, name, len) >= 0) {
        return 0;
    }
    if (p->enum_const_count == p->enum_const_cap) {
        size_t ncap = p->enum_const_cap == 0 ? 32 : p->enum_const_cap * 2;
        next = (enum_const_entry_t *)realloc(p->enum_consts, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        p->enum_consts = next;
        p->enum_const_cap = ncap;
    }
    dup = xstrdup_n(name, len);
    if (dup == NULL) {
        return -1;
    }
    p->enum_consts[p->enum_const_count].name = dup;
    p->enum_consts[p->enum_const_count].value = value;
    p->enum_const_count++;
    return 0;
}

static void parser_free_enum_consts(parser_t *p) {
    size_t i;
    for (i = 0; i < p->enum_const_count; ++i) {
        free(p->enum_consts[i].name);
    }
    free(p->enum_consts);
    p->enum_consts = NULL;
    p->enum_const_count = 0;
    p->enum_const_cap = 0;
}

static int enum_tag_find_n(const parser_t *p, const char *tag, size_t len) {
    size_t i;

    if (p == NULL || tag == NULL || len == 0) {
        return -1;
    }
    for (i = 0; i < p->enum_tag_count; ++i) {
        if (p->enum_tags[i].tag != NULL && strlen(p->enum_tags[i].tag) == len &&
            strncmp(p->enum_tags[i].tag, tag, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int enum_tag_set(parser_t *p, const char *tag, size_t len, cc_type_t type, int complete) {
    int idx;
    char *dup;
    enum_tag_entry_t *next;

    if (p == NULL || tag == NULL || len == 0) {
        return 0;
    }
    idx = enum_tag_find_n(p, tag, len);
    if (idx >= 0) {
        p->enum_tags[idx].type = type;
        if (complete) {
            p->enum_tags[idx].complete = 1;
        }
        return 0;
    }
    if (p->enum_tag_count == p->enum_tag_cap) {
        size_t ncap = p->enum_tag_cap == 0 ? 32 : p->enum_tag_cap * 2;
        next = (enum_tag_entry_t *)realloc(p->enum_tags, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        p->enum_tags = next;
        p->enum_tag_cap = ncap;
    }
    dup = xstrdup_n(tag, len);
    if (dup == NULL) {
        return -1;
    }
    p->enum_tags[p->enum_tag_count].tag = dup;
    p->enum_tags[p->enum_tag_count].type = type;
    p->enum_tags[p->enum_tag_count].complete = complete ? 1 : 0;
    p->enum_tag_count++;
    return 0;
}

static void parser_free_enum_tags(parser_t *p) {
    size_t i;

    for (i = 0; i < p->enum_tag_count; ++i) {
        free(p->enum_tags[i].tag);
    }
    free(p->enum_tags);
    p->enum_tags = NULL;
    p->enum_tag_count = 0;
    p->enum_tag_cap = 0;
}

static int is_pointer_type(cc_type_t t) {
    return cc_type_is_pointer(t);
}

static int type_carries_struct_id(cc_type_t t) {
    if (t == CC_TYPE_VOID) {
        return 1;
    }
    if (!cc_type_is_pointer(t)) {
        return 0;
    }
    return cc_type_pointer_base(t) == CC_TYPE_VOID;
}

static cc_type_t ptr_deref_type(cc_type_t t) {
    return cc_type_deref_once(t);
}

static int struct_ids_compatible(const parser_t *p, int lhs, int rhs) {
    const char *ltag;
    const char *rtag;

    if (lhs == rhs) {
        return 1;
    }
    if (lhs < 0 || rhs < 0 || (size_t)lhs >= p->struct_count || (size_t)rhs >= p->struct_count) {
        return 0;
    }
    ltag = p->structs[lhs].tag;
    rtag = p->structs[rhs].tag;
    if (ltag != NULL && rtag != NULL && strcmp(ltag, rtag) == 0) {
        return 1;
    }
    return 0;
}

static int parser_types_compatible(const parser_t *p, cc_type_t lhs_t, int lhs_sid, cc_type_t rhs_t, int rhs_sid) {
    if (lhs_t != rhs_t) {
        return 0;
    }
    if (lhs_t == CC_TYPE_ATOMIC) {
        return lhs_sid == rhs_sid;
    }
    if (lhs_t == CC_TYPE_BITINT) {
        return lhs_sid == rhs_sid;
    }
    if (!type_carries_struct_id(lhs_t)) {
        return 1;
    }
    return struct_ids_compatible(p, lhs_sid, rhs_sid);
}

static long scalar_type_size_bytes(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
        return 1;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 2;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_FLOAT:
        return 4;
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
        return g_parser_pointer_size_bytes;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_DOUBLE:
        return 8;
    case CC_TYPE_LDOUBLE:
        return 16;
    case CC_TYPE_ENUM:
        return 4;
    case CC_TYPE_COMPLEX:
        return 16;
    case CC_TYPE_IMAGINARY:
        return 8;
    default:
        return -1;
    }
}

static cc_type_t packed_enum_type_from_range(long min_v, long max_v) {
    if (min_v >= 0) {
        if ((unsigned long)max_v <= (unsigned long)UCHAR_MAX) {
            return CC_TYPE_UCHAR;
        }
        if ((unsigned long)max_v <= (unsigned long)USHRT_MAX) {
            return CC_TYPE_USHORT;
        }
        if ((unsigned long)max_v <= (unsigned long)UINT_MAX) {
            return CC_TYPE_UINT;
        }
        return CC_TYPE_ULONG_LONG;
    }
    if (min_v >= SCHAR_MIN && max_v <= SCHAR_MAX) {
        return CC_TYPE_CHAR;
    }
    if (min_v >= SHRT_MIN && max_v <= SHRT_MAX) {
        return CC_TYPE_SHORT;
    }
    if (sizeof(long) > sizeof(int)) {
        if (min_v >= INT_MIN && max_v <= INT_MAX) {
            return CC_TYPE_INT;
        }
    }
    return CC_TYPE_LONG_LONG;
}

static long parser_type_size_bytes(const parser_t *p, cc_type_t t, int struct_id) {
    long n;
    if (is_pointer_type(t)) {
        return g_parser_pointer_size_bytes;
    }
    if (t == CC_TYPE_ATOMIC) {
        if (struct_id <= 0) {
            return -1;
        }
        return parser_type_size_bytes(p, (cc_type_t)struct_id, -1);
    }
    if (t == CC_TYPE_BITINT) {
        if (struct_id <= 0) {
            return -1;
        }
        return (struct_id + 7) / 8;
    }
    n = scalar_type_size_bytes(t);
    if (n > 0) {
        return n;
    }
    if (t == CC_TYPE_VOID && struct_id >= 0 && (size_t)struct_id < p->struct_count && p->structs[struct_id].complete) {
        return p->structs[struct_id].size;
    }
    return -1;
}

static long parser_type_align_bytes(const parser_t *p, cc_type_t t, int struct_id) {
    long n;
    if (t == CC_TYPE_VOID && struct_id >= 0 && (size_t)struct_id < p->struct_count && p->structs[struct_id].complete) {
        n = p->structs[struct_id].align;
        if (n <= 0) {
            n = 1;
        }
        return n;
    }
    n = parser_type_size_bytes(p, t, struct_id);
    if (n <= 0) {
        return -1;
    }
    if (n > g_parser_pointer_size_bytes) {
        n = g_parser_pointer_size_bytes;
    }
    return n;
}

static int parser_is_bitfield_base_type(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_ENUM:
        return 1;
    default:
        return 0;
    }
}

static int parser_is_signed_int_type(cc_type_t t) {
    switch (t) {
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_SHORT:
    case CC_TYPE_INT:
    case CC_TYPE_LONG:
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ENUM:
        return 1;
    default:
        return 0;
    }
}

static long align_up_long(long x, long a) {
    long r;
    if (a <= 1) {
        return x;
    }
    r = x % a;
    if (r == 0) {
        return x;
    }
    return x + (a - r);
}

static int apply_struct_attrs(parser_t *p, int sid, const decl_attrs_t *attrs) {
    cc_struct_def_t *sd;
    int packed = 0;
    long forced_align = 0;
    long off = 0;
    long max_align = 1;
    long bit_unit_off = -1;
    long bit_unit_size = 0;
    long bit_unit_align = 1;
    int bit_unit_bits_used = 0;
    size_t i;

    if (sid < 0 || (size_t)sid >= p->struct_count) {
        return -1;
    }
    sd = &p->structs[sid];
    packed = (sd->attr_flags & CC_ATTR_PACKED) != 0;
    if ((sd->attr_flags & CC_ATTR_ALIGNED) != 0 && sd->attr_align > 0) {
        forced_align = sd->attr_align;
    }
    if (attrs != NULL) {
        if ((attrs->flags & CC_ATTR_PACKED) != 0) {
            packed = 1;
        }
        if ((attrs->flags & CC_ATTR_ALIGNED) != 0 && attrs->align > forced_align) {
            forced_align = attrs->align;
        }
    }

    for (i = 0; i < sd->member_count; ++i) {
        long msize = sd->members[i].size;
        long malign = parser_type_align_bytes(p, sd->members[i].type, sd->members[i].type_struct_id);
        int is_bitfield = sd->members[i].is_bitfield;
        int bit_width = sd->members[i].bit_width;
        long bit_storage_size = sd->members[i].bit_storage_size;
        int bit_storage_bits = sd->members[i].bit_storage_bits;
        if (msize <= 0) {
            msize = parser_type_size_bytes(p, sd->members[i].type, sd->members[i].type_struct_id);
        }
        if (msize < 0) {
            msize = g_parser_pointer_size_bytes;
        }
        if (malign <= 0) {
            if (msize == 0) {
                malign = 1;
            } else {
                malign = g_parser_pointer_size_bytes;
            }
        }
        if (packed) {
            malign = 1;
        }
        if (sd->is_union) {
            sd->members[i].offset = 0;
            if (is_bitfield) {
                if (bit_storage_size <= 0) {
                    bit_storage_size = msize;
                }
                if (bit_storage_size > off) {
                    off = bit_storage_size;
                }
                sd->members[i].bit_offset = 0;
            } else if (msize > off) {
                off = msize;
            }
        } else if (is_bitfield) {
            if (bit_storage_size <= 0) {
                bit_storage_size = msize;
            }
            if (bit_storage_bits <= 0) {
                bit_storage_bits = (int)(bit_storage_size * 8);
            }
            if (bit_width == 0) {
                if (bit_unit_off >= 0) {
                    off = bit_unit_off + bit_unit_size;
                }
                off = align_up_long(off, malign);
                sd->members[i].offset = off;
                sd->members[i].bit_offset = 0;
                bit_unit_off = -1;
                bit_unit_size = 0;
                bit_unit_align = 1;
                bit_unit_bits_used = 0;
            } else {
                int need_new_unit = 0;
                if (bit_unit_off < 0 || bit_unit_size != bit_storage_size || bit_unit_align != malign ||
                    bit_unit_bits_used + bit_width > bit_storage_bits) {
                    need_new_unit = 1;
                }
                if (need_new_unit) {
                    if (bit_unit_off >= 0) {
                        off = bit_unit_off + bit_unit_size;
                    }
                    off = align_up_long(off, malign);
                    bit_unit_off = off;
                    bit_unit_size = bit_storage_size;
                    bit_unit_align = malign;
                    bit_unit_bits_used = 0;
                }
                sd->members[i].offset = bit_unit_off;
                sd->members[i].bit_offset = bit_unit_bits_used;
                bit_unit_bits_used += bit_width;
                {
                    long end_off = bit_unit_off + bit_unit_size;
                    if (end_off > off) {
                        off = end_off;
                    }
                }
            }
        } else {
            if (bit_unit_off >= 0) {
                off = bit_unit_off + bit_unit_size;
                bit_unit_off = -1;
                bit_unit_size = 0;
                bit_unit_align = 1;
                bit_unit_bits_used = 0;
            }
            off = align_up_long(off, malign);
            sd->members[i].offset = off;
            off += msize;
        }
        if (malign > max_align) {
            max_align = malign;
        }
    }
    if (forced_align > max_align) {
        max_align = forced_align;
    }
    if (max_align <= 0) {
        max_align = 1;
    }
    sd->size = align_up_long(off, max_align);
    sd->align = max_align;
    sd->attr_flags &= ~(CC_ATTR_PACKED | CC_ATTR_ALIGNED);
    if (packed) {
        sd->attr_flags |= CC_ATTR_PACKED;
    }
    if (forced_align > 0) {
        sd->attr_flags |= CC_ATTR_ALIGNED;
        sd->attr_align = forced_align;
    } else {
        sd->attr_align = 0;
    }
    return 0;
}

static int struct_find_visible_tag_n(const parser_t *p, const char *tag, size_t len) {
    size_t i = p->struct_count;
    while (i > 0) {
        i--;
        if (p->structs[i].tag == NULL) {
            continue;
        }
        if (p->structs[i].depth > p->scope_depth) {
            continue;
        }
        if (strlen(p->structs[i].tag) == len && strncmp(p->structs[i].tag, tag, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int struct_find_current_scope_tag_n(const parser_t *p, const char *tag, size_t len) {
    size_t i = p->struct_count;
    while (i > 0) {
        i--;
        if (p->structs[i].tag == NULL) {
            continue;
        }
        if (p->structs[i].depth != p->scope_depth) {
            continue;
        }
        if (strlen(p->structs[i].tag) == len && strncmp(p->structs[i].tag, tag, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int struct_ensure(parser_t *p, const char *tag, size_t len, int for_definition) {
    cc_struct_def_t *next;
    char *dup = NULL;
    int idx;
    if (tag != NULL) {
        idx = for_definition ? struct_find_current_scope_tag_n(p, tag, len) : struct_find_visible_tag_n(p, tag, len);
        if (idx >= 0) {
            return idx;
        }
    }
    if (p->struct_count == p->struct_cap) {
        size_t ncap = p->struct_cap == 0 ? 16 : p->struct_cap * 2;
        next = (cc_struct_def_t *)realloc(p->structs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        p->structs = next;
        p->struct_cap = ncap;
    }
    memset(&p->structs[p->struct_count], 0, sizeof(p->structs[p->struct_count]));
    if (tag != NULL) {
        dup = xstrdup_n(tag, len);
        if (dup == NULL) {
            return -1;
        }
    }
    p->structs[p->struct_count].tag = dup;
    p->structs[p->struct_count].depth = p->scope_depth;
    p->structs[p->struct_count].complete = 0;
    return (int)p->struct_count++;
}

static int struct_member_push(parser_t *p, int sid, const char *name, cc_type_t type, int type_struct_id,
                              long array_len, int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS], long offset,
                              long size, int is_bitfield, int bit_width, int bit_offset, int bit_storage_bits,
                              int bit_signed, long bit_storage_size) {
    cc_struct_member_t *next;
    cc_struct_def_t *sd;
    char *dup;
    size_t i;
    if (sid < 0 || (size_t)sid >= p->struct_count) {
        return -1;
    }
    sd = &p->structs[sid];
    for (i = 0; i < sd->member_count; ++i) {
        if (strcmp(sd->members[i].name, name) == 0) {
            return 1;
        }
    }
    next = (cc_struct_member_t *)realloc(sd->members, (sd->member_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    sd->members = next;
    dup = xstrdup_n(name, strlen(name));
    if (dup == NULL) {
        return -1;
    }
    sd->members[sd->member_count].name = dup;
    sd->members[sd->member_count].type = type;
    sd->members[sd->member_count].type_struct_id = type_struct_id;
    sd->members[sd->member_count].array_len = array_len;
    sd->members[sd->member_count].array_ndim = array_ndim;
    if (array_dims != NULL) {
        memcpy(sd->members[sd->member_count].array_dims, array_dims, sizeof(sd->members[sd->member_count].array_dims));
    } else {
        memset(sd->members[sd->member_count].array_dims, 0, sizeof(sd->members[sd->member_count].array_dims));
    }
    sd->members[sd->member_count].offset = offset;
    sd->members[sd->member_count].size = size;
    sd->members[sd->member_count].is_bitfield = is_bitfield;
    sd->members[sd->member_count].bit_width = bit_width;
    sd->members[sd->member_count].bit_offset = bit_offset;
    sd->members[sd->member_count].bit_storage_bits = bit_storage_bits;
    sd->members[sd->member_count].bit_signed = bit_signed;
    sd->members[sd->member_count].bit_storage_size = bit_storage_size;
    sd->member_count++;
    return 0;
}

static const cc_struct_member_t *struct_member_find_n(const parser_t *p, int sid, const char *name, size_t len) {
    size_t i;
    const cc_struct_def_t *sd;

    if (sid < 0 || (size_t)sid >= p->struct_count || name == NULL) {
        return NULL;
    }
    sd = &p->structs[sid];
    if (!sd->complete) {
        return NULL;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (m->name == NULL) {
            continue;
        }
        if (strlen(m->name) == len && strncmp(m->name, name, len) == 0) {
            return m;
        }
    }
    return NULL;
}

static void parser_free_structs(parser_t *p) {
    size_t i;
    for (i = 0; i < p->struct_count; ++i) {
        size_t j;
        free(p->structs[i].tag);
        for (j = 0; j < p->structs[i].member_count; ++j) {
            free(p->structs[i].members[j].name);
        }
        free(p->structs[i].members);
    }
    free(p->structs);
    p->structs = NULL;
    p->struct_count = 0;
    p->struct_cap = 0;
}

static int is_declspec_tok(cc_tok_kind_t k);

static int tok_is_floatn_spelling(parser_t *p) {
    return tok_is_ident(p, "_Float16") || tok_is_ident(p, "_Float32") || tok_is_ident(p, "_Float64") ||
           tok_is_ident(p, "_Float128") || tok_is_ident(p, "_Float32x") || tok_is_ident(p, "_Float64x") ||
           tok_is_ident(p, "_Float128x") || tok_is_ident(p, "__float128");
}

static int builtin_typedef_type_n(const char *name, size_t len, cc_type_t *out_type) {
    cc_type_t uptr = g_parser_pointer_size_bytes <= 4 ? CC_TYPE_UINT : CC_TYPE_ULONG;
    cc_type_t sptr = g_parser_pointer_size_bytes <= 4 ? CC_TYPE_INT : CC_TYPE_LONG;
    if (name == NULL || out_type == NULL) {
        return 0;
    }
    if (len == 6 && strncmp(name, "wint_t", 6) == 0) {
        *out_type = CC_TYPE_INT;
        return 1;
    }
    if (len == 7 && strncmp(name, "wchar_t", 7) == 0) {
        *out_type = CC_TYPE_INT;
        return 1;
    }
    if (len == 6 && strncmp(name, "size_t", 6) == 0) {
        *out_type = uptr;
        return 1;
    }
    if (len == 7 && strncmp(name, "ssize_t", 7) == 0) {
        *out_type = sptr;
        return 1;
    }
    if (len == 9 && strncmp(name, "ptrdiff_t", 9) == 0) {
        *out_type = sptr;
        return 1;
    }
    if (len == 8 && strncmp(name, "intptr_t", 8) == 0) {
        *out_type = sptr;
        return 1;
    }
    if (len == 9 && strncmp(name, "uintptr_t", 9) == 0) {
        *out_type = uptr;
        return 1;
    }
    if (len == 8 && strncmp(name, "char16_t", 8) == 0) {
        *out_type = CC_TYPE_USHORT;
        return 1;
    }
    if (len == 8 && strncmp(name, "char32_t", 8) == 0) {
        *out_type = CC_TYPE_UINT;
        return 1;
    }
    return 0;
}

static int is_declspec_ident(parser_t *p) {
    if (tok_is_ident(p, "_Atomic") || tok_is_ident(p, "_Thread_local") || tok_is_ident(p, "_Alignas") ||
        tok_is_ident(p, "_Noreturn") || tok_is_ident(p, "_BitInt") || tok_is_ident(p, "_Decimal32") ||
        tok_is_ident(p, "_Decimal64") || tok_is_ident(p, "_Decimal128") || tok_is_floatn_spelling(p)) {
        return 1;
    }
    if (parser_is_gnu_mode() &&
        (tok_is_ident(p, "__signed__") || tok_is_ident(p, "__signed") || tok_is_ident(p, "__unsigned__") ||
         tok_is_ident(p, "__unsigned") || tok_is_ident(p, "__int128") || tok_is_ident(p, "__int128_t") ||
         tok_is_ident(p, "__uint128_t") || tok_is_ident(p, "__auto_type"))) {
        return 1;
    }
    if (parser_is_c23_or_newer() &&
        (tok_is_ident(p, "bool") || tok_is_ident(p, "thread_local") || tok_is_ident(p, "alignas") ||
         tok_is_ident(p, "constexpr") || tok_is_ident(p, "typeof") || tok_is_ident(p, "typeof_unqual"))) {
        return 1;
    }
    if (parser_is_gnu_mode() &&
        (tok_is_ident(p, "__typeof__") || tok_is_ident(p, "__typeof_unqual__") ||
         tok_is_ident(p, "__typeof") || tok_is_ident(p, "__typeof_unqual") || tok_is_ident(p, "typeof") ||
         tok_is_ident(p, "typeof_unqual"))) {
        return 1;
    }
    return 0;
}

static int is_declspec_start(parser_t *p) {
    cc_type_t bty;
    return is_declspec_tok(p->tok.kind) || is_declspec_ident(p) ||
           (p->tok.kind == TOK_IDENT && typedef_find_visible_n(p, p->tok.start, p->tok.len) >= 0) ||
           (p->tok.kind == TOK_IDENT && builtin_typedef_type_n(p->tok.start, p->tok.len, &bty)) ||
           tok_is_gnu_attribute_kw(p) ||
           (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK);
}

static int is_type_name_start_after_lparen(parser_t *p) {
    cc_token_t t;
    if (p->tok.kind == TOK_LPAREN) {
        if (peek_tok(p, &t) != 0) {
            return 0;
        }
    } else {
        t = p->tok;
    }
    if (is_declspec_tok(t.kind)) {
        return 1;
    }
    if (t.kind == TOK_IDENT) {
        cc_type_t bty;
        if (typedef_find_visible_n(p, t.start, t.len) >= 0) {
            return 1;
        }
        if (builtin_typedef_type_n(t.start, t.len, &bty)) {
            return 1;
        }
        if (token_is_gnu_attribute_kw(&t)) {
            return 1;
        }
        if ((t.len == strlen("_Atomic") && strncmp(t.start, "_Atomic", t.len) == 0) ||
            (t.len == strlen("_BitInt") && strncmp(t.start, "_BitInt", t.len) == 0) ||
            (t.len == strlen("_Float16") && strncmp(t.start, "_Float16", t.len) == 0) ||
            (t.len == strlen("_Float32") && strncmp(t.start, "_Float32", t.len) == 0) ||
            (t.len == strlen("_Float64") && strncmp(t.start, "_Float64", t.len) == 0) ||
            (t.len == strlen("_Float128") && strncmp(t.start, "_Float128", t.len) == 0) ||
            (t.len == strlen("_Float32x") && strncmp(t.start, "_Float32x", t.len) == 0) ||
            (t.len == strlen("_Float64x") && strncmp(t.start, "_Float64x", t.len) == 0) ||
            (t.len == strlen("_Float128x") && strncmp(t.start, "_Float128x", t.len) == 0) ||
            (t.len == strlen("__float128") && strncmp(t.start, "__float128", t.len) == 0) ||
            (t.len == strlen("typeof") && strncmp(t.start, "typeof", t.len) == 0) ||
            (t.len == strlen("typeof_unqual") && strncmp(t.start, "typeof_unqual", t.len) == 0) ||
            (t.len == strlen("__typeof") && strncmp(t.start, "__typeof", t.len) == 0) ||
            (t.len == strlen("__typeof_unqual") && strncmp(t.start, "__typeof_unqual", t.len) == 0) ||
            (t.len == strlen("__typeof__") && strncmp(t.start, "__typeof__", t.len) == 0) ||
            (t.len == strlen("__typeof_unqual__") && strncmp(t.start, "__typeof_unqual__", t.len) == 0) ||
            (parser_is_c23_or_newer() && t.len == strlen("bool") && strncmp(t.start, "bool", t.len) == 0)) {
            return 1;
        }
    }
    if (t.kind == TOK_LBRACK) {
        return 1;
    }
    return 0;
}

static int is_declspec_tok(cc_tok_kind_t k) {
    switch (k) {
    case TOK_KW_AUTO:
    case TOK_KW_BOOL:
    case TOK_KW_COMPLEX:
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
    case TOK_KW_STRUCT:
    case TOK_KW_UNION:
    case TOK_KW_ENUM:
    case TOK_KW_TYPEDEF:
    case TOK_KW_UNSIGNED:
    case TOK_KW_DOUBLE:
    case TOK_KW_IMAGINARY:
    case TOK_KW_VOLATILE:
    case TOK_KW_VOID:
        return 1;
    default:
        return 0;
    }
}

static int parse_declspec(parser_t *p, cc_type_t *out_type, int *out_struct_id,
                          long *out_array_len, int *out_array_ndim, long out_array_dims[CC_MAX_ARRAY_DIMS],
                          int allow_void, const char *what, int *out_typedef, int *out_saw_restrict,
                          decl_attrs_t *out_attrs) {
    int storage_flags = 0;
    int seen = 0;
    int seen_type = 0;
    int seen_void = 0;
    int seen_bool = 0;
    int seen_char = 0;
    int seen_int = 0;
    int seen_float = 0;
    int seen_double = 0;
    int seen_complex = 0;
    int seen_imaginary = 0;
    int seen_long = 0;
    int seen_short = 0;
    int seen_signed = 0;
    int seen_unsigned = 0;
    int seen_opaque_tag = 0;
    int seen_typedef = 0;
    int seen_alias = 0;
    int seen_atomic = 0;
    int seen_bitint = 0;
    int seen_decimal = 0;
    int seen_enum = 0;
    int seen_restrict = 0;
    int enum_known_type = 0;
    int enum_has_values = 0;
    long enum_min = 0;
    long enum_max = 0;
    const char *enum_tag_pending = NULL;
    size_t enum_tag_pending_len = 0;
    int enum_defined = 0;
    long bitint_width = 0;
    int alias_struct_id = -1;
    int seen_struct_id = -1;
    cc_type_t alias_type = CC_TYPE_VOID;
    cc_type_t enum_type = CC_TYPE_INT;
    cc_type_t decimal_type = CC_TYPE_DECIMAL64;
    long alias_array_len = -1;
    int alias_array_ndim = 0;
    long alias_array_dims[CC_MAX_ARRAY_DIMS];
#define RETURN_OK()                                                                                                  \
    do {                                                                                                             \
        if (seen_atomic && out_type != NULL && *out_type != CC_TYPE_ATOMIC) {                                       \
            cc_type_t __atomic_base_type = *out_type;                                                                \
            int __atomic_base_sid = (out_struct_id != NULL) ? *out_struct_id : -1;                                  \
            if (__atomic_base_type == CC_TYPE_VOID && __atomic_base_sid >= 0) {                                     \
                set_diag(p->diag, p->tok.line, p->tok.col, "_Atomic of aggregate type is not yet supported");      \
                return -1;                                                                                            \
            }                                                                                                         \
            *out_type = CC_TYPE_ATOMIC;                                                                              \
            if (out_struct_id != NULL) {                                                                             \
                *out_struct_id = (int)__atomic_base_type;                                                            \
            }                                                                                                         \
        }                                                                                                             \
        if (out_saw_restrict != NULL) {                                                                              \
            *out_saw_restrict = seen_restrict;                                                                       \
        }                                                                                                             \
        p->last_storage = storage_flags;                                                                             \
        return 0;                                                                                                    \
    } while (0)

    p->last_storage = 0;
    if (out_typedef != NULL) {
        *out_typedef = 0;
    }
    if (out_saw_restrict != NULL) {
        *out_saw_restrict = 0;
    }
    if (out_struct_id != NULL) {
        *out_struct_id = -1;
    }
    if (out_array_len != NULL) {
        *out_array_len = -1;
    }
    if (out_array_ndim != NULL) {
        *out_array_ndim = 0;
    }
    if (out_array_dims != NULL) {
        memset(out_array_dims, 0, sizeof(long) * CC_MAX_ARRAY_DIMS);
    }
    memset(alias_array_dims, 0, sizeof(alias_array_dims));
    if (out_attrs != NULL) {
        out_attrs->flags = 0;
        out_attrs->align = 0;
        out_attrs->section = NULL;
        out_attrs->alias = NULL;
    }

    while (1) {
        if (p->tok.kind == TOK_KW_EXTENSION) {
            seen = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            continue;
        }
        if ((p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) || tok_is_gnu_attribute_kw(p) ||
            tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm") || tok_is_ident(p, "asm")) {
            if (skip_decl_gnu_suffix(p, out_attrs) != 0) {
                return -1;
            }
            continue;
        }
        if (p->tok.kind == TOK_IDENT) {
            if (tok_is_ident(p, "__auto_type")) {
                if (!parser_is_gnu_mode()) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "__auto_type requires GNU mode");
                    return -1;
                }
                seen = 1;
                seen_type = 1;
                seen_alias = 1;
                alias_type = CC_TYPE_INT;
                alias_struct_id = -1;
                alias_array_len = -1;
                alias_array_ndim = 0;
                memset(alias_array_dims, 0, sizeof(alias_array_dims));
                storage_flags |= CC_STORAGE_AUTO_TYPE;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (p->tok.len == strlen("__builtin_va_list") &&
                strncmp(p->tok.start, "__builtin_va_list", p->tok.len) == 0) {
                if (seen_type) {
                    break;
                }
                seen = 1;
                seen_type = 1;
                seen_alias = 1;
                alias_type = CC_TYPE_PTR_VOID;
                alias_struct_id = -1;
                alias_array_len = -1;
                alias_array_ndim = 0;
                memset(alias_array_dims, 0, sizeof(alias_array_dims));
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_gnu_mode() && (tok_is_ident(p, "__signed__") || tok_is_ident(p, "__signed"))) {
                seen = 1;
                seen_type = 1;
                seen_signed = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_gnu_mode() && (tok_is_ident(p, "__unsigned__") || tok_is_ident(p, "__unsigned"))) {
                seen = 1;
                seen_type = 1;
                seen_unsigned = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_gnu_mode() &&
                (tok_is_ident(p, "__int128") || tok_is_ident(p, "__int128_t") || tok_is_ident(p, "__uint128_t"))) {
                seen = 1;
                seen_type = 1;
                seen_long = 1;
                if (tok_is_ident(p, "__uint128_t")) {
                    seen_unsigned = 1;
                }
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (tok_is_ident(p, "_Thread_local") || (parser_is_c23_or_newer() && tok_is_ident(p, "thread_local"))) {
                if (!parser_is_c11_or_newer()) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "_Thread_local requires C11 or newer");
                    return -1;
                }
                seen = 1;
                storage_flags |= CC_STORAGE_THREAD_LOCAL;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (tok_is_ident(p, "_Noreturn")) {
                if (!parser_is_c11_or_newer()) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "_Noreturn requires C11 or newer");
                    return -1;
                }
                seen = 1;
                if (out_attrs != NULL) {
                    out_attrs->flags |= CC_ATTR_NORETURN;
                }
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (tok_is_ident(p, "_Alignas") || (parser_is_c23_or_newer() && tok_is_ident(p, "alignas"))) {
                cc_type_t aty = CC_TYPE_VOID;
                int asid = -1;
                long align = 0;
                cc_expr_t *ae;
                if (!parser_is_c11_or_newer()) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "_Alignas requires C11 or newer");
                    return -1;
                }
                seen = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                if (expect(p, TOK_LPAREN, "expected '(' after _Alignas/alignas") != 0) {
                    return -1;
                }
                if (is_declspec_start(p)) {
                    if (parse_type_name(p, &aty, &asid, 1, "expected type name in _Alignas/alignas") != 0) {
                        return -1;
                    }
                    align = parser_type_align_bytes(p, aty, asid);
                    if (align <= 0) {
                        align = g_parser_pointer_size_bytes;
                    }
                } else {
                    ae = parse_cond(p);
                    if (ae == NULL) {
                        return -1;
                    }
                    if (eval_const_array_bound_expr(p, ae, &align) != 0 || align <= 0) {
                        free_expr(ae);
                        set_diag(p->diag, p->tok.line, p->tok.col, "alignas requires a positive integer constant");
                        return -1;
                    }
                    free_expr(ae);
                }
                if (expect(p, TOK_RPAREN, "expected ')' after _Alignas/alignas") != 0) {
                    return -1;
                }
                if (out_attrs != NULL) {
                    out_attrs->flags |= CC_ATTR_ALIGNED;
                    if (align > out_attrs->align) {
                        out_attrs->align = align;
                    }
                }
                continue;
            }
            if (tok_is_ident(p, "_Atomic")) {
                if (!parser_is_c11_or_newer()) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "_Atomic requires C11 or newer");
                    return -1;
                }
                seen = 1;
                if (peek_kind(p) == TOK_LPAREN) {
                    cc_type_t aty = CC_TYPE_VOID;
                    int asid = -1;
                    if (seen_type) {
                        break;
                    }
                    if (next_tok(p) != 0) {
                        return -1;
                    }
                    if (expect(p, TOK_LPAREN, "expected '(' after _Atomic") != 0) {
                        return -1;
                    }
                    if (parse_type_name(p, &aty, &asid, 1, "expected type name in _Atomic(...)") != 0) {
                        return -1;
                    }
                    if (expect(p, TOK_RPAREN, "expected ')' after _Atomic(...)") != 0) {
                        return -1;
                    }
                    seen_type = 1;
                    seen_atomic = 1;
                    seen_alias = 1;
                    alias_type = aty;
                    alias_struct_id = asid;
                    alias_array_len = -1;
                    alias_array_ndim = 0;
                    memset(alias_array_dims, 0, sizeof(alias_array_dims));
                    continue;
                }
                seen_atomic = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_c23_or_newer() && tok_is_ident(p, "bool")) {
                seen = 1;
                seen_type = 1;
                seen_bool = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_c23_or_newer() && tok_is_ident(p, "constexpr")) {
                seen = 1;
                storage_flags |= CC_STORAGE_STATIC;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_c23_or_newer() &&
                (tok_is_ident(p, "_Decimal32") || tok_is_ident(p, "_Decimal64") || tok_is_ident(p, "_Decimal128"))) {
                seen = 1;
                seen_type = 1;
                seen_decimal = 1;
                if (tok_is_ident(p, "_Decimal32")) {
                    decimal_type = CC_TYPE_DECIMAL32;
                } else if (tok_is_ident(p, "_Decimal64")) {
                    decimal_type = CC_TYPE_DECIMAL64;
                } else {
                    decimal_type = CC_TYPE_DECIMAL128;
                }
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (tok_is_ident(p, "_BitInt")) {
                cc_expr_t *we;
                long w = 0;
                if (!parser_is_c23_or_newer()) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "_BitInt requires C23 or newer");
                    return -1;
                }
                seen = 1;
                seen_type = 1;
                seen_bitint = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                if (expect(p, TOK_LPAREN, "expected '(' after _BitInt") != 0) {
                    return -1;
                }
                we = parse_cond(p);
                if (we == NULL) {
                    return -1;
                }
                if (eval_const_array_bound_expr(p, we, &w) != 0 || w <= 0) {
                    free_expr(we);
                    set_diag(p->diag, p->tok.line, p->tok.col, "_BitInt width must be a positive integer");
                    return -1;
                }
                free_expr(we);
                if (expect(p, TOK_RPAREN, "expected ')' after _BitInt width") != 0) {
                    return -1;
                }
                bitint_width = w;
                continue;
            }
            if ((parser_is_c23_or_newer() || parser_is_gnu_mode()) && tok_is_typeof_spelling(p)) {
                cc_type_t ty = CC_TYPE_VOID;
                int sid = -1;
                if (seen_type) {
                    break;
                }
                seen = 1;
                seen_type = 1;
                seen_alias = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                if (expect(p, TOK_LPAREN, "expected '(' after typeof/typeof_unqual") != 0) {
                    return -1;
                }
                if (is_declspec_start(p)) {
                    if (parse_type_name(p, &ty, &sid, 1, "expected type name in typeof") != 0) {
                        return -1;
                    }
                } else {
                    cc_expr_t *te = parse_cond(p);
                    if (te == NULL) {
                        return -1;
                    }
                    if (infer_expr_type(p, te, &ty, &sid) != 0) {
                        ty = te->value_type;
                        sid = te->struct_id;
                    }
                    free_expr(te);
                }
                if (expect(p, TOK_RPAREN, "expected ')' after typeof/typeof_unqual") != 0) {
                    return -1;
                }
                alias_type = ty;
                alias_struct_id = sid;
                alias_array_len = -1;
                alias_array_ndim = 0;
                memset(alias_array_dims, 0, sizeof(alias_array_dims));
                continue;
            }
            if (tok_is_floatn_spelling(p)) {
                seen = 1;
                seen_type = 1;
                seen_double = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            int tidx = typedef_find_visible_n(p, p->tok.start, p->tok.len);
            if (tidx >= 0) {
                if (seen_type) {
                    break;
                }
                seen = 1;
                seen_type = 1;
                seen_alias = 1;
                alias_type = p->typedefs[tidx].type;
                alias_struct_id = p->typedefs[tidx].struct_id;
                alias_array_len = p->typedefs[tidx].array_len;
                alias_array_ndim = p->typedefs[tidx].array_ndim;
                memcpy(alias_array_dims, p->typedefs[tidx].array_dims, sizeof(alias_array_dims));
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            {
                cc_type_t bty;
                if (builtin_typedef_type_n(p->tok.start, p->tok.len, &bty)) {
                    if (seen_type) {
                        break;
                    }
                    seen = 1;
                    seen_type = 1;
                    seen_alias = 1;
                    alias_type = bty;
                    alias_struct_id = -1;
                    alias_array_len = -1;
                    alias_array_ndim = 0;
                    memset(alias_array_dims, 0, sizeof(alias_array_dims));
                    if (next_tok(p) != 0) {
                        return -1;
                    }
                    continue;
                }
            }
        }
        if (!is_declspec_tok(p->tok.kind)) {
            break;
        }
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
        case TOK_KW_COMPLEX:
            seen_complex = 1;
            seen_type = 1;
            break;
        case TOK_KW_IMAGINARY:
            seen_imaginary = 1;
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
        case TOK_KW_TYPEDEF:
            seen_typedef = 1;
            break;
        case TOK_KW_STATIC:
            storage_flags |= CC_STORAGE_STATIC;
            break;
        case TOK_KW_EXTERN:
            storage_flags |= CC_STORAGE_EXTERN;
            break;
        case TOK_KW_AUTO:
            if (parser_is_c23_or_newer() && !seen_type) {
                seen_type = 1;
                seen_alias = 1;
                alias_type = CC_TYPE_INT;
                alias_struct_id = -1;
                alias_array_len = -1;
                alias_array_ndim = 0;
                memset(alias_array_dims, 0, sizeof(alias_array_dims));
                storage_flags |= CC_STORAGE_AUTO_TYPE;
            } else {
                storage_flags |= CC_STORAGE_AUTO;
            }
            break;
        case TOK_KW_REGISTER:
            storage_flags |= CC_STORAGE_REGISTER;
            break;
        case TOK_KW_INLINE:
            storage_flags |= CC_STORAGE_INLINE;
            break;
        case TOK_KW_CONST:
            storage_flags |= CC_STORAGE_CONST;
            break;
        case TOK_KW_VOLATILE:
            storage_flags |= CC_STORAGE_VOLATILE;
            break;
        case TOK_KW_RESTRICT:
            seen_restrict = 1;
            storage_flags |= CC_STORAGE_RESTRICT;
            break;
        case TOK_KW_STRUCT:
        case TOK_KW_UNION: {
            int sid;
            int brace_depth = 0;
            const char *tag_start = NULL;
            size_t tag_len = 0;
            long off = 0;
            long max_align = 1;
            long bit_unit_off = -1;
            long bit_unit_size = 0;
            long bit_unit_align = 1;
            int bit_unit_bits_used = 0;
            int is_union = (p->tok.kind == TOK_KW_UNION);
            decl_attrs_t struct_attrs;
            decl_attrs_reset(&struct_attrs);
            seen_type = 1;
            seen_opaque_tag = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            if (skip_decl_gnu_suffix(p, &struct_attrs) != 0) {
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            if (p->tok.kind == TOK_IDENT) {
                tag_start = p->tok.start;
                tag_len = p->tok.len;
                if (next_tok(p) != 0) {
                    decl_attrs_clear(&struct_attrs);
                    return -1;
                }
            }
            if (skip_decl_gnu_suffix(p, &struct_attrs) != 0) {
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            sid = struct_ensure(p, tag_start, tag_len, p->tok.kind == TOK_LBRACE);
            if (sid < 0) {
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            seen_struct_id = sid;
            if (p->tok.kind != TOK_LBRACE) {
                if (decl_attrs_merge(out_attrs, &struct_attrs) != 0) {
                    decl_attrs_clear(&struct_attrs);
                    return -1;
                }
                if ((struct_attrs.flags & (CC_ATTR_PACKED | CC_ATTR_ALIGNED)) != 0) {
                    if (apply_struct_attrs(p, sid, &struct_attrs) != 0) {
                        decl_attrs_clear(&struct_attrs);
                        return -1;
                    }
                }
                decl_attrs_clear(&struct_attrs);
                continue;
            }
            if (p->structs[sid].complete) {
                set_diag(p->diag, p->tok.line, p->tok.col, "redefinition of struct/union tag");
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            p->structs[sid].member_count = 0;
            free(p->structs[sid].members);
            p->structs[sid].members = NULL;
            p->structs[sid].is_union = is_union;
            p->structs[sid].has_flexible_array = 0;
            if (next_tok(p) != 0) {
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            while (p->tok.kind != TOK_RBRACE) {
                cc_type_t mbase;
                int mbase_sid = -1;
                long mbase_arr_len = -1;
                int mbase_arr_ndim = 0;
                long mbase_arr_dims[CC_MAX_ARRAY_DIMS];
                int mtypedef = 0;
                memset(mbase_arr_dims, 0, sizeof(mbase_arr_dims));
                if (p->structs[sid].has_flexible_array) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "flexible array member must be the last member");
                    decl_attrs_clear(&struct_attrs);
                    return -1;
                }
                if (p->tok.kind == TOK_EOF) {
                    decl_attrs_clear(&struct_attrs);
                    set_diag(p->diag, p->tok.line, p->tok.col, "unterminated aggregate declaration");
                    return -1;
                }
                if (!(is_declspec_start(p) || p->tok.kind == TOK_KW_EXTENSION)) {
                    /* tolerate unsupported member forms by brace-skip */
                    brace_depth = 0;
                    while (p->tok.kind != TOK_SEMI && p->tok.kind != TOK_EOF) {
                        if (p->tok.kind == TOK_LBRACE) {
                            brace_depth++;
                        } else if (p->tok.kind == TOK_RBRACE) {
                            if (brace_depth == 0) {
                                break;
                            }
                            brace_depth--;
                        }
                        if (next_tok(p) != 0) {
                            decl_attrs_clear(&struct_attrs);
                            return -1;
                        }
                    }
                    if (p->tok.kind == TOK_SEMI && next_tok(p) != 0) {
                        decl_attrs_clear(&struct_attrs);
                        return -1;
                    }
                    continue;
                }
                if (parse_declspec(p, &mbase, &mbase_sid, &mbase_arr_len, &mbase_arr_ndim, mbase_arr_dims, 1,
                                   "expected member declaration type", &mtypedef, NULL, NULL) != 0) {
                    decl_attrs_clear(&struct_attrs);
                    return -1;
                }
                if (mtypedef) {
                    decl_attrs_clear(&struct_attrs);
                    set_diag(p->diag, p->tok.line, p->tok.col, "typedef is not allowed inside struct member list");
                    return -1;
                }
                for (;;) {
                    cc_type_t mtype = mbase;
                    int mstruct = mbase_sid;
                    char *mname = NULL;
                    long arr_count = mbase_arr_len >= 0 ? mbase_arr_len : 1;
                    int arr_saw = mbase_arr_ndim > 0 ? 1 : 0;
                    int arr_unsized = (mbase_arr_ndim > 0 && mbase_arr_len == 0) ? 1 : 0;
                    int arr_ndim = mbase_arr_ndim;
                    int arr_suffix_ndim = 0;
                    long arr_dims[CC_MAX_ARRAY_DIMS];
                    long msize;
                    long malign;
                    long moff;
                    int is_bitfield = 0;
                    int bit_width = -1;
                    int bit_offset = 0;
                    int bit_storage_bits = 0;
                    int bit_signed = 0;
                    long bit_storage_size = 0;
                    int skip_member_decl = 0;
                    decl_attrs_t mdecl_attrs;
                    decl_attrs_reset(&mdecl_attrs);
                    memset(arr_dims, 0, sizeof(arr_dims));
                    if (mbase_arr_ndim > 0) {
                        memcpy(arr_dims, mbase_arr_dims, sizeof(arr_dims));
                    }
                    while (is_ptr_declarator_tok(p->tok.kind)) {
                        mtype = ptr_of_type(mtype);
                        if (mtype == CC_TYPE_VOID) {
                            set_ptr_depth_diag(p, __LINE__);
                            return -1;
                        }
                        if (next_tok(p) != 0) {
                            return -1;
                        }
                        while (is_decl_qual_at_token(p)) {
                            if (next_tok(p) != 0) {
                                return -1;
                            }
                        }
                    }
                    if (p->tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(p))) {
                        if (next_tok(p) != 0) {
                            return -1;
                        }
                        while (is_ptr_declarator_tok(p->tok.kind)) {
                            mtype = ptr_of_type(mtype);
                            if (mtype == CC_TYPE_VOID) {
                                set_ptr_depth_diag(p, __LINE__);
                                return -1;
                            }
                            if (next_tok(p) != 0) {
                                return -1;
                            }
                            while (is_decl_qual_at_token(p)) {
                                if (next_tok(p) != 0) {
                                    return -1;
                                }
                            }
                        }
                        if (p->tok.kind == TOK_IDENT) {
                            mname = xstrdup_n(p->tok.start, p->tok.len);
                            if (mname == NULL) {
                                return -1;
                            }
                            if (next_tok(p) != 0) {
                                free(mname);
                                return -1;
                            }
                        }
                        if (expect(p, TOK_RPAREN, "expected ')' in struct member declarator") != 0) {
                            free(mname);
                            return -1;
                        }
                        while (p->tok.kind == TOK_LPAREN) {
                            if (skip_balanced_parens(p) != 0) {
                                free(mname);
                                return -1;
                            }
                        }
                    } else if (p->tok.kind == TOK_IDENT) {
                        mname = xstrdup_n(p->tok.start, p->tok.len);
                        if (mname == NULL) {
                            return -1;
                        }
                        if (next_tok(p) != 0) {
                            free(mname);
                            return -1;
                        }
                    }
                    if (p->tok.kind == TOK_COLON) {
                        cc_expr_t *bw_expr = NULL;
                        long bw = 0;
                        is_bitfield = 1;
                        if (next_tok(p) != 0) {
                            free(mname);
                            return -1;
                        }
                        if (p->tok.kind == TOK_COMMA || p->tok.kind == TOK_SEMI) {
                            set_diag(p->diag, p->tok.line, p->tok.col, "expected bit-field width after ':'");
                            free(mname);
                            return -1;
                        }
                        bw_expr = parse_cond(p);
                        if (bw_expr == NULL) {
                            free(mname);
                            return -1;
                        }
                        if (eval_const_array_bound_expr(p, bw_expr, &bw) != 0 || bw < 0 || bw > INT_MAX) {
                            set_diag(p->diag, p->tok.line, p->tok.col,
                                     "bit-field width must be a non-negative integer constant");
                            free_expr(bw_expr);
                            free(mname);
                            return -1;
                        }
                        bit_width = (int)bw;
                        free_expr(bw_expr);
                        if (p->tok.kind != TOK_COMMA && p->tok.kind != TOK_SEMI) {
                            set_diag(p->diag, p->tok.line, p->tok.col, "malformed bit-field width expression");
                            free(mname);
                            return -1;
                        }
                    } else {
                        while (p->tok.kind == TOK_LBRACK) {
                            long n = 1;
                            int const_extent = 0;
                            int empty_extent = (peek_kind(p) == TOK_RBRACK);
                            if (parse_array_extent(p, &n, &const_extent) != 0) {
                                free(mname);
                                return -1;
                            }
                            arr_saw = 1;
                            if (empty_extent) {
                                arr_unsized = 1;
                            }
                            if (const_extent && arr_count >= 0) {
                                arr_count *= n;
                            }
                            if (arr_ndim >= CC_MAX_ARRAY_DIMS) {
                                set_diag(p->diag, p->tok.line, p->tok.col, "array rank > 4 is not yet supported");
                                free(mname);
                                return -1;
                            }
                            arr_dims[arr_ndim++] = const_extent ? n : 0;
                            arr_suffix_ndim++;
                        }
                    }
                    if (skip_decl_gnu_suffix(p, &mdecl_attrs) != 0) {
                        free(mname);
                        decl_attrs_clear(&mdecl_attrs);
                        return -1;
                    }
                    if (mname == NULL && !is_bitfield) {
                        if (mtype == CC_TYPE_VOID) {
                            /* Anonymous aggregate member extension: accept and ignore layout details. */
                            skip_member_decl = 1;
                        } else {
                            set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier in struct member");
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                    }
                    if (skip_member_decl && p->tok.kind == TOK_COMMA) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "anonymous aggregate member with declarator list is unsupported");
                        decl_attrs_clear(&mdecl_attrs);
                        return -1;
                    }
                    msize = parser_type_size_bytes(p, mtype, mstruct);
                    malign = parser_type_align_bytes(p, mtype, mstruct);
                    if (msize < 0) {
                        msize = g_parser_pointer_size_bytes;
                    }
                    if (malign <= 0) {
                        if (msize == 0) {
                            malign = 1;
                        } else {
                            malign = g_parser_pointer_size_bytes;
                        }
                    }
                    if ((mdecl_attrs.flags & CC_ATTR_PACKED) != 0) {
                        malign = 1;
                    }
                    if ((mdecl_attrs.flags & CC_ATTR_ALIGNED) != 0 && mdecl_attrs.align > malign) {
                        malign = mdecl_attrs.align;
                    }

                    if (is_bitfield) {
                        if (!parser_is_bitfield_base_type(mtype)) {
                            set_diag(p->diag, p->tok.line, p->tok.col, "bit-field base type must be an integer type");
                            free(mname);
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                        if (arr_saw) {
                            set_diag(p->diag, p->tok.line, p->tok.col, "bit-field cannot be declared as an array");
                            free(mname);
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                        if (bit_width == 0 && mname != NULL) {
                            set_diag(p->diag, p->tok.line, p->tok.col, "zero-width bit-field cannot be named");
                            free(mname);
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                        bit_storage_size = parser_type_size_bytes(p, mtype, mstruct);
                        if (bit_storage_size <= 0 || bit_storage_size > 8) {
                            set_diag(p->diag, p->tok.line, p->tok.col, "unsupported bit-field storage type size");
                            free(mname);
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                        bit_storage_bits = (int)(bit_storage_size * 8);
                        if (bit_width > bit_storage_bits) {
                            set_diag(p->diag, p->tok.line, p->tok.col, "bit-field width exceeds storage type width");
                            free(mname);
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                        bit_signed = parser_is_signed_int_type(mtype) ? 1 : 0;
                        msize = bit_storage_size;

                        if (is_union) {
                            moff = 0;
                            bit_offset = 0;
                            if (bit_width == 0) {
                                msize = 0;
                            }
                        } else {
                            if (bit_width == 0) {
                                if (bit_unit_off >= 0) {
                                    off = bit_unit_off + bit_unit_size;
                                }
                                off = align_up_long(off, malign);
                                bit_unit_off = -1;
                                bit_unit_size = 0;
                                bit_unit_align = 1;
                                bit_unit_bits_used = 0;
                                moff = off;
                                msize = 0;
                            } else {
                                int need_new_unit = 0;
                                if (bit_unit_off < 0 || bit_unit_size != bit_storage_size || bit_unit_align != malign ||
                                    bit_unit_bits_used + bit_width > bit_storage_bits) {
                                    need_new_unit = 1;
                                }
                                if (need_new_unit) {
                                    if (bit_unit_off >= 0) {
                                        off = bit_unit_off + bit_unit_size;
                                    }
                                    off = align_up_long(off, malign);
                                    bit_unit_off = off;
                                    bit_unit_size = bit_storage_size;
                                    bit_unit_align = malign;
                                    bit_unit_bits_used = 0;
                                }
                                moff = bit_unit_off;
                                bit_offset = bit_unit_bits_used;
                                bit_unit_bits_used += bit_width;
                                {
                                    long end_off = bit_unit_off + bit_unit_size;
                                    if (end_off > off) {
                                        off = end_off;
                                    }
                                }
                            }
                        }
                    } else {
                        if (!is_union && bit_unit_off >= 0) {
                            off = bit_unit_off + bit_unit_size;
                            bit_unit_off = -1;
                            bit_unit_size = 0;
                            bit_unit_align = 1;
                            bit_unit_bits_used = 0;
                        }
                        if (arr_saw) {
                            /*
                             * Base typedef arrays (e.g. `typedef T A[N]; A m;`) already
                             * carry one pointer level in mtype. Only apply an extra
                             * pointer level when this declarator adds bracket suffixes.
                             */
                            if (mbase_arr_ndim == 0 || arr_suffix_ndim > 0) {
                                cc_type_t ptype = ptr_of_type(mtype);
                                if (ptype != CC_TYPE_VOID) {
                                    mtype = ptype;
                                } else {
                                    mtype = CC_TYPE_PTR_VOID;
                                }
                            }
                            if (arr_unsized) {
                                if (is_union) {
                                    set_diag(p->diag, p->tok.line, p->tok.col,
                                             "flexible array member is not allowed in unions");
                                    decl_attrs_clear(&mdecl_attrs);
                                    return -1;
                                }
                                if (mname == NULL) {
                                    set_diag(p->diag, p->tok.line, p->tok.col,
                                             "flexible array member must be a named member");
                                    decl_attrs_clear(&mdecl_attrs);
                                    return -1;
                                }
                                if (p->tok.kind == TOK_COMMA) {
                                    set_diag(p->diag, p->tok.line, p->tok.col,
                                             "flexible array member must terminate the declaration");
                                    decl_attrs_clear(&mdecl_attrs);
                                    return -1;
                                }
                                if (p->structs[sid].member_count == 0) {
                                    set_diag(p->diag, p->tok.line, p->tok.col,
                                             "flexible array member requires at least one prior named member");
                                    decl_attrs_clear(&mdecl_attrs);
                                    return -1;
                                }
                                msize = 0;
                                p->structs[sid].has_flexible_array = 1;
                            } else if (arr_count > 0) {
                                msize *= arr_count;
                            } else if (arr_count == 0) {
                                msize = 0;
                            }
                        }
                        moff = is_union ? 0 : align_up_long(off, malign);
                    }

                    if (!skip_member_decl && mname != NULL) {
                        if (struct_member_push(p, sid, mname, mtype, mstruct, arr_saw ? arr_count : -1,
                                               arr_saw ? arr_ndim : 0, arr_saw ? arr_dims : NULL, moff, msize,
                                               is_bitfield, bit_width, bit_offset, bit_storage_bits, bit_signed,
                                               bit_storage_size) != 0) {
                            free(mname);
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                        free(mname);
                    } else if (skip_member_decl && mname == NULL && mtype == CC_TYPE_VOID && mstruct >= 0 &&
                               (size_t)mstruct < p->struct_count && p->structs[mstruct].complete) {
                        size_t ai;
                        const cc_struct_def_t *anon = &p->structs[mstruct];
                        for (ai = 0; ai < anon->member_count; ++ai) {
                            const cc_struct_member_t *am = &anon->members[ai];
                            if (struct_member_push(p, sid, am->name, am->type, am->type_struct_id, am->array_len,
                                                   am->array_ndim, am->array_dims, moff + am->offset, am->size,
                                                   am->is_bitfield, am->bit_width, am->bit_offset, am->bit_storage_bits,
                                                   am->bit_signed, am->bit_storage_size) != 0) {
                                decl_attrs_clear(&mdecl_attrs);
                                return -1;
                            }
                        }
                    }
                    if (is_union) {
                        if (is_bitfield) {
                            if (bit_storage_size > off) {
                                off = bit_storage_size;
                            }
                        } else if (msize > off) {
                            off = msize;
                        }
                    } else if (!is_bitfield) {
                        if (msize > 0) {
                            off = moff + msize;
                        }
                    } else if (bit_width == 0) {
                        if (moff > off) {
                            off = moff;
                        }
                    }
                    if (malign > max_align) {
                        max_align = malign;
                    }
                    decl_attrs_clear(&mdecl_attrs);
                    if (p->tok.kind != TOK_COMMA) {
                        break;
                    }
                    if (next_tok(p) != 0) {
                        return -1;
                    }
                }
                if (expect(p, TOK_SEMI, "expected ';' after aggregate member declaration") != 0) {
                    decl_attrs_clear(&struct_attrs);
                    return -1;
                }
            }
            if (expect(p, TOK_RBRACE, "expected '}' after aggregate declaration") != 0) {
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            p->structs[sid].size = align_up_long(off, max_align);
            p->structs[sid].align = max_align;
            p->structs[sid].complete = 1;
            if (decl_attrs_merge(out_attrs, &struct_attrs) != 0) {
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            if ((struct_attrs.flags & (CC_ATTR_PACKED | CC_ATTR_ALIGNED)) != 0) {
                if (apply_struct_attrs(p, sid, &struct_attrs) != 0) {
                    decl_attrs_clear(&struct_attrs);
                    return -1;
                }
            }
            decl_attrs_clear(&struct_attrs);
            continue;
        }
        case TOK_KW_ENUM: {
            long enum_next = 0;
            const char *enum_tag_start = NULL;
            size_t enum_tag_len = 0;

            seen_type = 1;
            seen_int = 1;
            seen_enum = 1;
            seen_opaque_tag = 0;
            seen_struct_id = -1;
            if (next_tok(p) != 0) {
                return -1;
            }
            if (p->tok.kind == TOK_IDENT) {
                enum_tag_start = p->tok.start;
                enum_tag_len = p->tok.len;
                if (next_tok(p) != 0) {
                    return -1;
                }
            }
            if (parser_is_c23_or_newer() && p->tok.kind == TOK_COLON) {
                cc_type_t uty = CC_TYPE_INT;
                int usid = -1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                if (parse_type_name(p, &uty, &usid, 1, "expected enum underlying type") != 0) {
                    return -1;
                }
                if (usid >= 0) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "enum underlying type must be an integer type");
                    return -1;
                }
                enum_known_type = 1;
                enum_type = uty;
            }
            if (p->tok.kind != TOK_LBRACE) {
                if (!enum_known_type && enum_tag_start != NULL && enum_tag_len > 0) {
                    int eidx = enum_tag_find_n(p, enum_tag_start, enum_tag_len);
                    if (eidx >= 0) {
                        enum_known_type = 1;
                        enum_type = p->enum_tags[eidx].type;
                    }
                }
                continue;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            while (p->tok.kind != TOK_RBRACE) {
                if (p->tok.kind == TOK_EOF) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "unterminated enum declaration");
                    return -1;
                }
                if (p->tok.kind != TOK_IDENT) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "expected enumerator name");
                    return -1;
                }
                {
                    long enum_cur = enum_next;
                    if (enum_const_push(p, p->tok.start, p->tok.len, enum_cur) != 0) {
                        return -1;
                    }
                    if (next_tok(p) != 0) {
                        return -1;
                    }
                    {
                        decl_attrs_t enum_attrs;
                        decl_attrs_reset(&enum_attrs);
                        if (skip_decl_gnu_suffix(p, &enum_attrs) != 0) {
                            decl_attrs_clear(&enum_attrs);
                            return -1;
                        }
                        decl_attrs_clear(&enum_attrs);
                    }
                    if (p->tok.kind == TOK_ASSIGN) {
                        cc_expr_t *enum_expr;
                        long v = 0;
                        if (next_tok(p) != 0) {
                            return -1;
                        }
                        enum_expr = parse_cond(p);
                        if (enum_expr == NULL) {
                            return -1;
                        }
                        if (eval_const_array_bound_expr(p, enum_expr, &v) != 0) {
                            free_expr(enum_expr);
                            set_diag(p->diag, p->tok.line, p->tok.col,
                                     "enum value must be an integer constant expression");
                            return -1;
                        }
                        free_expr(enum_expr);
                        enum_cur = v;
                        enum_next = v;
                        p->enum_consts[p->enum_const_count - 1].value = enum_next;
                    }
                    {
                        decl_attrs_t enum_attrs;
                        decl_attrs_reset(&enum_attrs);
                        if (skip_decl_gnu_suffix(p, &enum_attrs) != 0) {
                            decl_attrs_clear(&enum_attrs);
                            return -1;
                        }
                        decl_attrs_clear(&enum_attrs);
                    }
                    if (!enum_has_values) {
                        enum_min = enum_cur;
                        enum_max = enum_cur;
                        enum_has_values = 1;
                    } else {
                        if (enum_cur < enum_min) {
                            enum_min = enum_cur;
                        }
                        if (enum_cur > enum_max) {
                            enum_max = enum_cur;
                        }
                    }
                    enum_next++;
                }
                if (p->tok.kind == TOK_COMMA) {
                    if (next_tok(p) != 0) {
                        return -1;
                    }
                    if (p->tok.kind == TOK_RBRACE) {
                        break;
                    }
                } else if (p->tok.kind != TOK_RBRACE) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "expected ',' or '}' in enum declaration");
                    return -1;
                }
            }
            if (expect(p, TOK_RBRACE, "expected '}' after enum declaration") != 0) {
                return -1;
            }
            if (enum_tag_start != NULL && enum_tag_len > 0) {
                enum_tag_pending = enum_tag_start;
                enum_tag_pending_len = enum_tag_len;
            }
            enum_defined = 1;
            continue;
        }
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
        char near_tok[40];
        size_t n = 0;
        near_tok[0] = '\0';
        if (p->tok.start != NULL && p->tok.len > 0) {
            n = p->tok.len < sizeof(near_tok) - 1 ? p->tok.len : sizeof(near_tok) - 1;
            memcpy(near_tok, p->tok.start, n);
            near_tok[n] = '\0';
        }
        if (near_tok[0] != '\0') {
            char msg[128];
            snprintf(msg, sizeof(msg), "expected type specifier in declaration near '%s'", near_tok);
            set_diag(p->diag, p->tok.line, p->tok.col, msg);
        } else {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected type specifier in declaration");
        }
        return -1;
    }

    if (seen_alias) {
        if (seen_opaque_tag) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid typedef type combination");
            return -1;
        }
        if (seen_void || seen_bool || seen_char || seen_int || seen_float || seen_double || seen_long || seen_short ||
            seen_signed || seen_unsigned) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid typedef type combination");
            return -1;
        }
        if (!allow_void && alias_type == CC_TYPE_VOID) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid use of void in declaration specifiers");
            return -1;
        }
        *out_type = alias_type;
        if (out_struct_id != NULL) {
            *out_struct_id = alias_struct_id;
        }
        if (out_array_len != NULL) {
            *out_array_len = alias_array_len;
        }
        if (out_array_ndim != NULL) {
            *out_array_ndim = alias_array_ndim;
        }
        if (out_array_dims != NULL) {
            memcpy(out_array_dims, alias_array_dims, sizeof(long) * CC_MAX_ARRAY_DIMS);
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }

    if (seen_bitint) {
        if (bitint_width <= 0) {
            set_diag(p->diag, p->tok.line, p->tok.col, "_BitInt width must be positive");
            return -1;
        }
        if (bitint_width > INT_MAX) {
            set_diag(p->diag, p->tok.line, p->tok.col, "_BitInt width is too large");
            return -1;
        }
        *out_type = CC_TYPE_BITINT;
        if (out_struct_id != NULL) {
            *out_struct_id = (int)bitint_width;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }

    if (seen_void) {
        if (!allow_void || seen_bool || seen_char || seen_int || seen_float || seen_double || seen_long || seen_short) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid use of void in declaration specifiers");
            return -1;
        }
        *out_type = CC_TYPE_VOID;
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }

    if (seen_signed && seen_unsigned) {
        set_diag(p->diag, p->tok.line, p->tok.col, "conflicting signed/unsigned in declaration specifiers");
        return -1;
    }
    if (seen_short && seen_long > 0) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid short/long combination in declaration specifiers");
        return -1;
    }

    if ((seen_signed || seen_unsigned) &&
        (seen_float || seen_double || seen_decimal || seen_complex || seen_imaginary || seen_bool || seen_void)) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid signed/unsigned type combination");
        return -1;
    }
    if (seen_opaque_tag) {
        if (seen_void || seen_bool || seen_char || seen_int || seen_float || seen_double || seen_long || seen_short ||
            seen_complex || seen_imaginary || seen_signed || seen_unsigned) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid aggregate declaration specifiers");
            return -1;
        }
        *out_type = CC_TYPE_VOID;
        if (out_struct_id != NULL) {
            *out_struct_id = seen_struct_id;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }

    if (seen_complex || seen_imaginary) {
        if (seen_void || seen_bool || seen_char || seen_int || seen_short || seen_signed || seen_unsigned ||
            seen_opaque_tag || seen_decimal) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid complex/imaginary type combination");
            return -1;
        }
        if (seen_long > 0 && !seen_double) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid complex/imaginary long type combination");
            return -1;
        }
        *out_type = seen_complex ? CC_TYPE_COMPLEX : CC_TYPE_IMAGINARY;
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }

    if (seen_decimal) {
        *out_type = decimal_type;
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }

    if (seen_double) {
        if (seen_long > 1) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid long double type combination");
            return -1;
        }
        *out_type = seen_long ? CC_TYPE_LDOUBLE : CC_TYPE_DOUBLE;
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }
    if (seen_float) {
        *out_type = CC_TYPE_FLOAT;
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }
    if (seen_bool) {
        *out_type = CC_TYPE_BOOL;
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }
    if (seen_char) {
        if (seen_unsigned) {
            *out_type = CC_TYPE_UCHAR;
        } else if (seen_signed) {
            *out_type = CC_TYPE_SCHAR;
        } else {
            *out_type = CC_TYPE_CHAR;
        }
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }
    if (seen_long > 0) {
        if (seen_long > 1) {
            *out_type = seen_unsigned ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG;
        } else {
            *out_type = seen_unsigned ? CC_TYPE_ULONG : CC_TYPE_LONG;
        }
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }
    if (seen_short) {
        *out_type = seen_unsigned ? CC_TYPE_USHORT : CC_TYPE_SHORT;
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }
    if (seen_int) {
        if (seen_enum) {
            int packed_enum = (out_attrs != NULL && (out_attrs->flags & CC_ATTR_PACKED) != 0);
            if (!enum_known_type) {
                if (packed_enum && enum_has_values) {
                    enum_type = packed_enum_type_from_range(enum_min, enum_max);
                } else if (enum_has_values && sizeof(long) > sizeof(int) && (enum_min < INT_MIN || enum_max > INT_MAX)) {
                    if (enum_min >= 0) {
                        enum_type = CC_TYPE_ULONG_LONG;
                    } else {
                        enum_type = CC_TYPE_LONG_LONG;
                    }
                } else {
                    enum_type = seen_unsigned ? CC_TYPE_UINT : CC_TYPE_INT;
                }
                enum_known_type = 1;
            }
            *out_type = enum_type;
            if (*out_type == CC_TYPE_INT || *out_type == CC_TYPE_UINT || *out_type == CC_TYPE_LONG_LONG ||
                *out_type == CC_TYPE_ULONG_LONG) {
                *out_type = CC_TYPE_ENUM;
            }
            if (enum_defined && enum_tag_pending != NULL && enum_tag_pending_len > 0) {
                if (enum_tag_set(p, enum_tag_pending, enum_tag_pending_len, CC_TYPE_ENUM, 1) != 0) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "out of memory tracking enum tag");
                    return -1;
                }
            }
        } else {
            *out_type = seen_unsigned ? CC_TYPE_UINT : CC_TYPE_INT;
        }
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }

    /* e.g. signed/unsigned without explicit base type => int */
    if (seen_signed || seen_unsigned) {
        *out_type = seen_unsigned ? CC_TYPE_UINT : CC_TYPE_INT;
        if (out_struct_id != NULL) {
            *out_struct_id = -1;
        }
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        RETURN_OK();
    }

    *out_type = CC_TYPE_INT;
    if (out_struct_id != NULL) {
        *out_struct_id = -1;
    }
    if (out_typedef != NULL) {
        *out_typedef = seen_typedef;
    } else if (seen_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
        return -1;
    }
    RETURN_OK();
#undef RETURN_OK
}

static int is_decl_qual_tok(cc_tok_kind_t k) {
    return k == TOK_KW_CONST || k == TOK_KW_VOLATILE || k == TOK_KW_RESTRICT;
}

static int is_decl_qual_at_token(parser_t *p) {
    return is_decl_qual_tok(p->tok.kind) || tok_is_ident(p, "_Atomic");
}

static int consume_decl_quals(parser_t *p, int *saw_restrict) {
    while (is_decl_qual_at_token(p)) {
        if (p->tok.kind == TOK_KW_RESTRICT && saw_restrict != NULL) {
            *saw_restrict = 1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    return 0;
}

static cc_type_t ptr_of_type(cc_type_t t) {
    return cc_type_make_pointer(t);
}

static int infer_expr_type(parser_t *p, const cc_expr_t *e, cc_type_t *out_type, int *out_struct_id) {
    cc_type_t lt;
    cc_type_t rt;
    int lsid;
    int rsid;

    if (out_type == NULL || out_struct_id == NULL) {
        return -1;
    }
    *out_type = CC_TYPE_INT;
    *out_struct_id = -1;
    if (e == NULL) {
        return -1;
    }

    switch (e->kind) {
    case CC_EXPR_IDENT:
        if (e->ident != NULL) {
            int vidx = var_find_visible_n(p, e->ident, strlen(e->ident));
            if (vidx >= 0) {
                *out_type = p->vars[vidx].type;
                *out_struct_id = p->vars[vidx].struct_id;
                return 0;
            }
        }
        *out_type = e->value_type;
        *out_struct_id = e->struct_id;
        return 0;
    case CC_EXPR_INT:
    case CC_EXPR_FLOAT:
    case CC_EXPR_STR:
        *out_type = e->value_type;
        *out_struct_id = e->struct_id;
        return 0;
    case CC_EXPR_CAST:
        *out_type = e->aux_type;
        *out_struct_id = e->aux_struct_id;
        return 0;
    case CC_EXPR_ADDR:
        if (infer_expr_type(p, e->lhs, &lt, &lsid) != 0) {
            *out_type = e->value_type;
            *out_struct_id = e->struct_id;
            return 0;
        }
        lt = ptr_of_type(lt);
        if (lt != CC_TYPE_VOID) {
            *out_type = lt;
            *out_struct_id = type_carries_struct_id(lt) ? lsid : -1;
            return 0;
        }
        *out_type = e->value_type;
        *out_struct_id = e->struct_id;
        return 0;
    case CC_EXPR_DEREF:
        if (infer_expr_type(p, e->lhs, &lt, &lsid) != 0) {
            *out_type = e->value_type;
            *out_struct_id = e->struct_id;
            return 0;
        }
        if (is_pointer_type(lt)) {
            lt = ptr_deref_type(lt);
            *out_type = lt;
            *out_struct_id = type_carries_struct_id(lt) ? lsid : -1;
            return 0;
        }
        *out_type = e->value_type;
        *out_struct_id = e->struct_id;
        return 0;
    case CC_EXPR_MEMBER: {
        const cc_struct_member_t *sm = NULL;
        if (infer_expr_type(p, e->lhs, &lt, &lsid) != 0) {
            *out_type = e->value_type;
            *out_struct_id = e->struct_id;
            return 0;
        }
        if (e->member_is_arrow) {
            lt = ptr_deref_type(lt);
            if (!type_carries_struct_id(lt)) {
                lsid = -1;
            }
        }
        if (lt == CC_TYPE_VOID && lsid >= 0 && e->ident != NULL) {
            sm = struct_member_find_n(p, lsid, e->ident, strlen(e->ident));
            if (sm != NULL) {
                *out_type = sm->type;
                *out_struct_id = sm->type_struct_id;
                return 0;
            }
        }
        *out_type = e->value_type;
        *out_struct_id = e->struct_id;
        return 0;
    }
    case CC_EXPR_BIN:
        if (infer_expr_type(p, e->lhs, &lt, &lsid) != 0) {
            lt = e->lhs != NULL ? e->lhs->value_type : CC_TYPE_INT;
            lsid = e->lhs != NULL ? e->lhs->struct_id : -1;
        }
        if (infer_expr_type(p, e->rhs, &rt, &rsid) != 0) {
            rt = e->rhs != NULL ? e->rhs->value_type : CC_TYPE_INT;
            rsid = e->rhs != NULL ? e->rhs->struct_id : -1;
        }
        if (e->op == CC_BIN_COMMA) {
            *out_type = rt;
            *out_struct_id = rsid;
            return 0;
        }
        if (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) {
            if (is_pointer_type(lt) && !is_pointer_type(rt)) {
                *out_type = lt;
                *out_struct_id = lsid;
                return 0;
            }
            if (e->op == CC_BIN_ADD && !is_pointer_type(lt) && is_pointer_type(rt)) {
                *out_type = rt;
                *out_struct_id = rsid;
                return 0;
            }
            if (e->op == CC_BIN_SUB && is_pointer_type(lt) && is_pointer_type(rt)) {
                *out_type = CC_TYPE_LONG_LONG;
                *out_struct_id = -1;
                return 0;
            }
        }
        *out_type = e->value_type;
        *out_struct_id = e->struct_id;
        return 0;
    case CC_EXPR_CALL:
        if (e->ident != NULL) {
            const cc_function_t *f = parser_find_function_decl(p, e->ident);
            if (f != NULL) {
                *out_type = f->ret_type;
                *out_struct_id = f->ret_struct_id;
                return 0;
            }
        }
        *out_type = e->value_type;
        *out_struct_id = e->struct_id;
        return 0;
    case CC_EXPR_STMT:
        if (e->stmt_expr_count > 0) {
            const cc_stmt_t *tail = &e->stmt_expr_stmts[e->stmt_expr_count - 1];
            if (tail->kind == CC_STMT_EXPR && tail->expr != NULL) {
                if (infer_expr_type(p, tail->expr, out_type, out_struct_id) == 0) {
                    return 0;
                }
                *out_type = tail->expr->value_type;
                *out_struct_id = tail->expr->struct_id;
                return 0;
            }
        }
        *out_type = CC_TYPE_VOID;
        *out_struct_id = -1;
        return 0;
    default:
        *out_type = e->value_type;
        *out_struct_id = e->struct_id;
        return 0;
    }
}

static int parse_named_declarator(parser_t *p, cc_type_t base_type, cc_type_t *out_type, char **out_name,
                                  const char *name_err, int *out_saw_restrict) {
    cc_type_t ty = base_type;
    int wrapped = 0;
    int saw_restrict = 0;
    if (out_saw_restrict != NULL) {
        *out_saw_restrict = 0;
    }
    while (is_ptr_declarator_tok(p->tok.kind)) {
        ty = ptr_of_type(ty);
        if (ty == CC_TYPE_VOID) {
            set_ptr_depth_diag(p, __LINE__);
            return -1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        if (consume_decl_quals(p, &saw_restrict) != 0) {
            return -1;
        }
    }
    while ((p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) || tok_is_gnu_attribute_kw(p) ||
           tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm") || tok_is_ident(p, "asm")) {
        if (skip_decl_gnu_suffix(p, NULL) != 0) {
            return -1;
        }
    }
    if (p->tok.kind == TOK_LPAREN) {
        wrapped = 1;
        if (next_tok(p) != 0) {
            return -1;
        }
        while (is_ptr_declarator_tok(p->tok.kind)) {
            ty = ptr_of_type(ty);
            if (ty == CC_TYPE_VOID) {
                set_ptr_depth_diag(p, __LINE__);
                return -1;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            if (consume_decl_quals(p, &saw_restrict) != 0) {
                return -1;
            }
        }
        while ((p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) || tok_is_gnu_attribute_kw(p) ||
               tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm") || tok_is_ident(p, "asm")) {
            if (skip_decl_gnu_suffix(p, NULL) != 0) {
                return -1;
            }
        }
    }
    if (p->tok.kind != TOK_IDENT) {
        set_diag(p->diag, p->tok.line, p->tok.col, name_err);
        return -1;
    }
    *out_name = xstrdup_n(p->tok.start, p->tok.len);
    if (*out_name == NULL) {
        return -1;
    }
    *out_type = ty;
    if (next_tok(p) != 0) {
        free(*out_name);
        *out_name = NULL;
        return -1;
    }
    if (wrapped) {
        if (expect(p, TOK_RPAREN, "expected ')' in declarator") != 0) {
            free(*out_name);
            *out_name = NULL;
            return -1;
        }
    }
    if (out_saw_restrict != NULL) {
        *out_saw_restrict = saw_restrict;
    }
    return 0;
}

static int eval_const_array_bound_expr(parser_t *p, const cc_expr_t *e, long *out) {
    long a;
    long b;
    cc_type_t st;
    long sz;
    if (e == NULL || out == NULL) {
        return -1;
    }
    switch (e->kind) {
    case CC_EXPR_INT:
        *out = e->int_val;
        return 0;
    case CC_EXPR_CAST:
        return eval_const_array_bound_expr(p, e->lhs, out);
    case CC_EXPR_SIZEOF:
        if (e->lhs != NULL && e->lhs->kind == CC_EXPR_IDENT && e->lhs->ident != NULL) {
            int vidx = var_find_visible_n(p, e->lhs->ident, strlen(e->lhs->ident));
            if (vidx >= 0 && p->vars[vidx].array_ndim > 0) {
                cc_type_t elem_type = p->vars[vidx].type;
                long total;
                int i;
                if (is_pointer_type(elem_type)) {
                    elem_type = ptr_deref_type(elem_type);
                }
                sz = parser_type_size_bytes(p, elem_type, p->vars[vidx].struct_id);
                if (sz <= 0) {
                    return -1;
                }
                total = sz;
                for (i = 0; i < p->vars[vidx].array_ndim; ++i) {
                    long dim = p->vars[vidx].array_dims[i];
                    if (dim <= 0) {
                        return -1;
                    }
                    total *= dim;
                }
                *out = total;
                return 0;
            }
            if (vidx < 0 && p->tu != NULL) {
                size_t gi;
                for (gi = 0; gi < p->tu->global_count; ++gi) {
                    const cc_global_t *g = &p->tu->globals[gi];
                    if (g->name == NULL || strcmp(g->name, e->lhs->ident) != 0) {
                        continue;
                    }
                    {
                        cc_type_t elem_type = g->type;
                        long total;
                        int i;
                        int ndim = g->array_ndim;
                        if (ndim <= 0 && g->array_len > 0) {
                            ndim = 1;
                        }
                        if (ndim <= 0) {
                            continue;
                        }
                        if (is_pointer_type(elem_type)) {
                            elem_type = ptr_deref_type(elem_type);
                        }
                        sz = parser_type_size_bytes(p, elem_type, g->type_struct_id);
                        if (sz <= 0) {
                            return -1;
                        }
                        total = sz;
                        for (i = 0; i < ndim; ++i) {
                            long dim = (g->array_ndim > 0) ? g->array_dims[i] : g->array_len;
                            if (dim <= 0) {
                                return -1;
                            }
                            total *= dim;
                        }
                        *out = total;
                        return 0;
                    }
                }
            }
        }
        if (e->lhs != NULL && e->lhs->array_ndim > 0) {
            cc_type_t elem_type = e->lhs->value_type;
            long total;
            int i;

            if (is_pointer_type(elem_type)) {
                elem_type = ptr_deref_type(elem_type);
            }
            sz = parser_type_size_bytes(p, elem_type, e->lhs->struct_id);
            if (sz <= 0) {
                return -1;
            }
            total = sz;
            for (i = 0; i < e->lhs->array_ndim; ++i) {
                if (e->lhs->array_dims[i] <= 0) {
                    return -1;
                }
                total *= e->lhs->array_dims[i];
            }
            *out = total;
            return 0;
        }
        st = e->aux_type;
        if (st == CC_TYPE_VOID && e->aux_struct_id < 0 && e->lhs != NULL) {
            st = e->lhs->value_type;
            sz = parser_type_size_bytes(p, st, e->lhs->struct_id);
            if (sz > 0) {
                *out = sz;
                return 0;
            }
        }
        sz = parser_type_size_bytes(p, st, e->aux_struct_id);
        if (sz > 0) {
            *out = sz;
            return 0;
        }
        if (is_pointer_type(st)) {
            *out = g_parser_pointer_size_bytes;
            return 0;
        }
        sz = scalar_type_size_bytes(st);
        if (sz > 0) {
            *out = sz;
            return 0;
        }
        return -1;
    case CC_EXPR_BIN:
        if (e->op == CC_BIN_COMMA) {
            return eval_const_array_bound_expr(p, e->rhs, out);
        }
        {
            int lhs_ok = (eval_const_array_bound_expr(p, e->lhs, &a) == 0);
            int rhs_ok = (eval_const_array_bound_expr(p, e->rhs, &b) == 0);

            if (e->op == CC_BIN_MUL) {
                if (lhs_ok && a == 0) {
                    *out = 0;
                    return 0;
                }
                if (rhs_ok && b == 0) {
                    *out = 0;
                    return 0;
                }
            }
            if (!lhs_ok || !rhs_ok) {
                return -1;
            }
        }
        switch (e->op) {
        case CC_BIN_ADD: *out = a + b; return 0;
        case CC_BIN_SUB: *out = a - b; return 0;
        case CC_BIN_MUL: *out = a * b; return 0;
        case CC_BIN_DIV:
            if (b == 0) return -1;
            *out = a / b;
            return 0;
        case CC_BIN_MOD:
            if (b == 0) return -1;
            *out = a % b;
            return 0;
        case CC_BIN_SHL: *out = a << (b & 63); return 0;
        case CC_BIN_SHR: *out = a >> (b & 63); return 0;
        case CC_BIN_BAND: *out = a & b; return 0;
        case CC_BIN_BOR: *out = a | b; return 0;
        case CC_BIN_BXOR: *out = a ^ b; return 0;
        case CC_BIN_EQ: *out = (a == b) ? 1 : 0; return 0;
        case CC_BIN_NE: *out = (a != b) ? 1 : 0; return 0;
        case CC_BIN_LT: *out = (a < b) ? 1 : 0; return 0;
        case CC_BIN_LE: *out = (a <= b) ? 1 : 0; return 0;
        case CC_BIN_GT: *out = (a > b) ? 1 : 0; return 0;
        case CC_BIN_GE: *out = (a >= b) ? 1 : 0; return 0;
        case CC_BIN_LAND: *out = (a != 0 && b != 0) ? 1 : 0; return 0;
        case CC_BIN_LOR: *out = (a != 0 || b != 0) ? 1 : 0; return 0;
        default:
            return -1;
        }
    case CC_EXPR_TERNARY:
        if (eval_const_array_bound_expr(p, e->lhs, &a) != 0) {
            return -1;
        }
        if (a != 0) {
            if (e->rhs == NULL) {
                *out = a;
                return 0;
            }
            return eval_const_array_bound_expr(p, e->rhs, out);
        }
        return eval_const_array_bound_expr(p, e->third, out);
    default:
        return -1;
    }
}

static int parse_array_extent(parser_t *p, long *out_n, int *out_const_n) {
    cc_expr_t *bound = NULL;
    long n = 1;
    int is_const = 0;

    if (p == NULL || out_n == NULL || out_const_n == NULL || p->tok.kind != TOK_LBRACK) {
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    if (p->tok.kind == TOK_RBRACK) {
        if (expect(p, TOK_RBRACK, "expected ']' after array declarator") != 0) {
            return -1;
        }
        *out_n = 1;
        *out_const_n = 0;
        return 0;
    }

    bound = parse_expr(p);
    if (bound == NULL) {
        return -1;
    }
    if (eval_const_array_bound_expr(p, bound, &n) == 0) {
        if (n > 0 || (parser_is_gnu_mode() && n == 0)) {
            is_const = 1;
        }
    }
    free_expr(bound);
    if (expect(p, TOK_RBRACK, "expected ']' after array declarator") != 0) {
        return -1;
    }
    *out_n = is_const ? n : (n > 0 ? n : 1);
    *out_const_n = is_const;
    return 0;
}

static int parse_array_suffix(parser_t *p, cc_type_t *io_type, long *out_array_len, int *out_array_ndim,
                              long out_array_dims[CC_MAX_ARRAY_DIMS]) {
    long arr_len = -1;
    int arr_ndim = 0;
    int saw_incomplete_dim = 0;
    long dims[CC_MAX_ARRAY_DIMS];
    memset(dims, 0, sizeof(dims));
    while (p->tok.kind == TOK_LBRACK) {
        long n = 1;
        int saw_const_n = 0;
        cc_type_t ty = ptr_of_type(*io_type);
        if (ty == CC_TYPE_VOID) {
            /*
             * Array rank is tracked separately; saturate pointer encoding
             * when the element type is already at max representable depth.
             */
            ty = *io_type;
        }
        *io_type = ty;
        if (parse_array_extent(p, &n, &saw_const_n) != 0) {
            return -1;
        }
        if (saw_const_n) {
            if (saw_incomplete_dim) {
                arr_len = 0;
            } else if (arr_len < 0) {
                arr_len = n;
            } else if (arr_len == 0) {
                arr_len = n;
            } else {
                arr_len *= n;
            }
        } else {
            saw_incomplete_dim = 1;
            if (arr_len < 0) {
                arr_len = 0;
            } else {
                arr_len = 0;
            }
        }
        if (arr_ndim >= CC_MAX_ARRAY_DIMS) {
            set_diag(p->diag, p->tok.line, p->tok.col, "array rank > 4 is not yet supported");
            return -1;
        }
        dims[arr_ndim++] = saw_const_n ? n : 0;
    }
    if (out_array_len != NULL) {
        *out_array_len = arr_len;
    }
    if (out_array_ndim != NULL) {
        *out_array_ndim = arr_ndim;
    }
    if (out_array_dims != NULL) {
        memcpy(out_array_dims, dims, sizeof(dims));
    }
    return 0;
}

static int prepend_array_info(long *io_array_len, int *io_array_ndim, long io_array_dims[CC_MAX_ARRAY_DIMS],
                              long outer_array_len, int outer_array_ndim,
                              const long outer_array_dims[CC_MAX_ARRAY_DIMS]) {
    int i;
    int cur_ndim;
    if (io_array_len == NULL || io_array_ndim == NULL || io_array_dims == NULL) {
        return -1;
    }
    if (outer_array_ndim <= 0) {
        return 0;
    }
    cur_ndim = *io_array_ndim;
    if (outer_array_ndim + cur_ndim > CC_MAX_ARRAY_DIMS) {
        return -1;
    }
    memmove(io_array_dims + outer_array_ndim, io_array_dims, (size_t)cur_ndim * sizeof(io_array_dims[0]));
    for (i = 0; i < outer_array_ndim; ++i) {
        io_array_dims[i] = outer_array_dims != NULL ? outer_array_dims[i] : 0;
    }
    *io_array_ndim = cur_ndim + outer_array_ndim;
    if (*io_array_len < 0) {
        *io_array_len = outer_array_len;
    } else if (outer_array_len >= 0) {
        if (*io_array_len == 0 || outer_array_len == 0) {
            *io_array_len = 0;
        } else if (*io_array_len > LONG_MAX / outer_array_len) {
            *io_array_len = 0;
        } else {
            *io_array_len *= outer_array_len;
        }
    }
    return 0;
}

static int tok_is_ident(parser_t *p, const char *s) {
    size_t n = strlen(s);
    return p->tok.kind == TOK_IDENT && p->tok.len == n && strncmp(p->tok.start, s, n) == 0;
}

static int tok_is_gnu_attribute_kw(parser_t *p) {
    return tok_is_ident(p, "__attribute__") || tok_is_ident(p, "__attribute");
}

static int token_is_gnu_attribute_kw(const cc_token_t *t) {
    if (t == NULL || t->kind != TOK_IDENT) {
        return 0;
    }
    if (t->len == strlen("__attribute__") && strncmp(t->start, "__attribute__", t->len) == 0) {
        return 1;
    }
    return t->len == strlen("__attribute") && strncmp(t->start, "__attribute", t->len) == 0;
}

static int tok_is_gnu_attr_name(parser_t *p, const char *s) {
    size_t n;

    if (tok_is_ident(p, s)) {
        return 1;
    }
    if (p->tok.kind != TOK_IDENT || p->tok.len < 4) {
        return 0;
    }
    if (p->tok.start[0] != '_' || p->tok.start[1] != '_' || p->tok.start[p->tok.len - 2] != '_' ||
        p->tok.start[p->tok.len - 1] != '_') {
        return 0;
    }
    n = strlen(s);
    if (p->tok.len != n + 4) {
        return 0;
    }
    return strncmp(p->tok.start + 2, s, n) == 0;
}

static void decl_attrs_reset(decl_attrs_t *a) {
    if (a == NULL) {
        return;
    }
    a->flags = 0;
    a->align = 0;
    a->section = NULL;
    a->alias = NULL;
}

static void decl_attrs_clear(decl_attrs_t *a) {
    if (a == NULL) {
        return;
    }
    free(a->section);
    a->section = NULL;
    free(a->alias);
    a->alias = NULL;
    a->flags = 0;
    a->align = 0;
}

static int decl_attrs_merge(decl_attrs_t *dst, const decl_attrs_t *src) {
    char *sec_dup = NULL;
    char *alias_dup = NULL;
    if (dst == NULL || src == NULL) {
        return 0;
    }
    if ((src->flags & CC_ATTR_SECTION) != 0 && src->section != NULL) {
        sec_dup = xstrdup_n(src->section, strlen(src->section));
        if (sec_dup == NULL) {
            return -1;
        }
    }
    if ((src->flags & CC_ATTR_ALIAS) != 0 && src->alias != NULL) {
        alias_dup = xstrdup_n(src->alias, strlen(src->alias));
        if (alias_dup == NULL) {
            free(sec_dup);
            return -1;
        }
    }
    dst->flags |= src->flags;
    if (src->align > dst->align) {
        dst->align = src->align;
    }
    if (sec_dup != NULL) {
        free(dst->section);
        dst->section = sec_dup;
    }
    if (alias_dup != NULL) {
        free(dst->alias);
        dst->alias = alias_dup;
    }
    return 0;
}

static char *dup_string_token(const cc_token_t *tok) {
    size_t begin = 0;
    size_t end = 0;
    if (tok == NULL || tok->kind != TOK_STR) {
        return NULL;
    }
    begin = 0;
    end = tok->len;
    if (tok->len >= 2 && tok->start[0] == '"' && tok->start[tok->len - 1] == '"') {
        begin = 1;
        end = tok->len - 1;
    }
    if (end < begin) {
        return xstrdup_n("", 0);
    }
    return xstrdup_n(tok->start + begin, end - begin);
}

static cc_type_t string_token_char_type(const parser_t *p, const cc_token_t *tok) {
    if (p == NULL || tok == NULL || tok->kind != TOK_STR || tok->start == NULL) {
        return CC_TYPE_CHAR;
    }
    if (tok->start > p->lx.src) {
        char c1 = tok->start[-1];
        if (c1 == 'L' || c1 == 'u' || c1 == 'U') {
            return CC_TYPE_INT;
        }
    }
    if (tok->start > p->lx.src + 1) {
        if (tok->start[-2] == 'u' && tok->start[-1] == '8') {
            return CC_TYPE_CHAR;
        }
    }
    return CC_TYPE_CHAR;
}

static int append_string_piece(char **dst, const char *piece) {
    size_t a;
    size_t b;
    char *next;
    if (dst == NULL || piece == NULL) {
        return -1;
    }
    if (*dst == NULL) {
        *dst = xstrdup_n(piece, strlen(piece));
        return *dst == NULL ? -1 : 0;
    }
    a = strlen(*dst);
    b = strlen(piece);
    next = (char *)realloc(*dst, a + b + 1);
    if (next == NULL) {
        return -1;
    }
    memcpy(next + a, piece, b + 1);
    *dst = next;
    return 0;
}

static char *unescape_string_piece(const char *in) {
    size_t i;
    size_t j;
    size_t n;
    char *out;

    if (in == NULL) {
        return NULL;
    }
    n = strlen(in);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    j = 0;
    for (i = 0; i < n; ++i) {
        if (in[i] == '\\' && i + 1 < n) {
            char c = in[++i];
            if (c == 'n') out[j++] = '\n';
            else if (c == 't') out[j++] = '\t';
            else if (c == 'r') out[j++] = '\r';
            else if (c == '\\') out[j++] = '\\';
            else if (c == '"') out[j++] = '"';
            else out[j++] = c;
            continue;
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
    return out;
}

static char *parse_string_concat_literal(parser_t *p) {
    char *out = NULL;
    while (p->tok.kind == TOK_STR) {
        char *raw = dup_string_token(&p->tok);
        char *tmp;
        char *part;
        if (raw == NULL) {
            free(out);
            return NULL;
        }
        tmp = unescape_string_piece(raw);
        free(raw);
        if (tmp == NULL) {
            free(out);
            return NULL;
        }
        part = unescape_string_piece(tmp);
        free(tmp);
        if (part == NULL) {
            free(out);
            return NULL;
        }
        if (append_string_piece(&out, part) != 0) {
            free(part);
            free(out);
            return NULL;
        }
        free(part);
        if (next_tok(p) != 0) {
            free(out);
            return NULL;
        }
    }
    if (out == NULL) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected string literal");
        return NULL;
    }
    return out;
}

static void free_asm_operand(cc_asm_operand_t *op) {
    if (op == NULL) {
        return;
    }
    free(op->name);
    free(op->constraint);
    free_expr(op->expr);
    memset(op, 0, sizeof(*op));
}

static int push_asm_operand(cc_asm_operand_t **items, size_t *count, cc_asm_operand_t *item) {
    cc_asm_operand_t *next;
    if (items == NULL || count == NULL || item == NULL) {
        return -1;
    }
    next = (cc_asm_operand_t *)realloc(*items, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *items = next;
    (*items)[*count] = *item;
    (*count)++;
    memset(item, 0, sizeof(*item));
    return 0;
}

static int push_string_item(char ***items, size_t *count, char *value) {
    char **next;
    if (items == NULL || count == NULL || value == NULL) {
        return -1;
    }
    next = (char **)realloc(*items, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *items = next;
    (*items)[*count] = value;
    (*count)++;
    return 0;
}

static int parse_asm_operand_list(parser_t *p, cc_asm_operand_t **items, size_t *count) {
    for (;;) {
        cc_asm_operand_t op;
        memset(&op, 0, sizeof(op));
        if (p->tok.kind == TOK_LBRACK) {
            if (next_tok(p) != 0) {
                return -1;
            }
            if (p->tok.kind != TOK_IDENT) {
                set_diag(p->diag, p->tok.line, p->tok.col, "expected asm operand name");
                return -1;
            }
            op.name = xstrdup_n(p->tok.start, p->tok.len);
            if (op.name == NULL) {
                return -1;
            }
            if (next_tok(p) != 0) {
                free_asm_operand(&op);
                return -1;
            }
            if (expect(p, TOK_RBRACK, "expected ']' after asm operand name") != 0) {
                free_asm_operand(&op);
                return -1;
            }
        }
        if (p->tok.kind != TOK_STR) {
            free_asm_operand(&op);
            set_diag(p->diag, p->tok.line, p->tok.col, "expected asm operand constraint string");
            return -1;
        }
        op.constraint = parse_string_concat_literal(p);
        if (op.constraint == NULL) {
            free_asm_operand(&op);
            return -1;
        }
        if (expect(p, TOK_LPAREN, "expected '(' after asm operand constraint") != 0) {
            free_asm_operand(&op);
            return -1;
        }
        op.expr = parse_expr(p);
        if (op.expr == NULL) {
            free_asm_operand(&op);
            return -1;
        }
        if (expect(p, TOK_RPAREN, "expected ')' after asm operand expression") != 0) {
            free_asm_operand(&op);
            return -1;
        }
        if (push_asm_operand(items, count, &op) != 0) {
            free_asm_operand(&op);
            return -1;
        }
        if (p->tok.kind != TOK_COMMA) {
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_asm_clobber_list(parser_t *p, char ***items, size_t *count) {
    for (;;) {
        char *name;
        if (p->tok.kind != TOK_STR) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected asm clobber string");
            return -1;
        }
        name = parse_string_concat_literal(p);
        if (name == NULL) {
            return -1;
        }
        if (push_string_item(items, count, name) != 0) {
            free(name);
            return -1;
        }
        if (p->tok.kind != TOK_COMMA) {
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_asm_label_list(parser_t *p, char ***items, size_t *count) {
    for (;;) {
        char *name;
        if (p->tok.kind != TOK_IDENT) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected asm goto label");
            return -1;
        }
        name = xstrdup_n(p->tok.start, p->tok.len);
        if (name == NULL) {
            return -1;
        }
        if (push_string_item(items, count, name) != 0) {
            free(name);
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
    }
    return 0;
}

static int parse_asm_stmt(parser_t *p, cc_stmt_t *s) {
    s->kind = CC_STMT_ASM;
    if (next_tok(p) != 0) {
        return -1;
    }
    while (tok_is_ident(p, "__volatile__") || tok_is_ident(p, "volatile") || p->tok.kind == TOK_KW_VOLATILE ||
           tok_is_ident(p, "__goto__") || tok_is_ident(p, "goto") || p->tok.kind == TOK_KW_GOTO) {
        if (tok_is_ident(p, "__goto__") || tok_is_ident(p, "goto") || p->tok.kind == TOK_KW_GOTO) {
            s->asm_is_goto = 1;
        } else {
            s->asm_is_volatile = 1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    if (expect(p, TOK_LPAREN, "expected '(' after asm") != 0) {
        return -1;
    }
    if (p->tok.kind != TOK_STR) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected asm template string");
        return -1;
    }
    s->asm_template = parse_string_concat_literal(p);
    if (s->asm_template == NULL) {
        return -1;
    }
    if (p->tok.kind == TOK_COLON) {
        if (next_tok(p) != 0) {
            return -1;
        }
        if (p->tok.kind != TOK_COLON && p->tok.kind != TOK_RPAREN) {
            if (parse_asm_operand_list(p, &s->asm_outputs, &s->asm_output_count) != 0) {
                return -1;
            }
        }
        if (p->tok.kind == TOK_COLON) {
            if (next_tok(p) != 0) {
                return -1;
            }
            if (p->tok.kind != TOK_COLON && p->tok.kind != TOK_RPAREN) {
                if (parse_asm_operand_list(p, &s->asm_inputs, &s->asm_input_count) != 0) {
                    return -1;
                }
            }
            if (p->tok.kind == TOK_COLON) {
                if (next_tok(p) != 0) {
                    return -1;
                }
                if (p->tok.kind != TOK_RPAREN && p->tok.kind != TOK_COLON) {
                    if (parse_asm_clobber_list(p, &s->asm_clobbers, &s->asm_clobber_count) != 0) {
                        return -1;
                    }
                }
                if (p->tok.kind == TOK_COLON) {
                    if (next_tok(p) != 0) {
                        return -1;
                    }
                    if (p->tok.kind != TOK_RPAREN) {
                        if (parse_asm_label_list(p, &s->asm_goto_labels, &s->asm_goto_label_count) != 0) {
                            return -1;
                        }
                        s->asm_is_goto = 1;
                    }
                }
            }
        }
    }
    if (expect(p, TOK_RPAREN, "expected ')' after asm statement") != 0) {
        return -1;
    }
    if (expect(p, TOK_SEMI, "expected ';' after asm statement") != 0) {
        return -1;
    }
    return 0;
}

static int parse_one_c23_attribute(parser_t *p, decl_attrs_t *out_attrs, int *out_stmt_flags) {
    int is_deprecated;
    int is_fallthrough;
    int is_maybe_unused;
    int is_nodiscard;
    int is_noreturn;
    int is_reproducible;
    int is_unsequenced;
    long ignored_num = 0;
    int ignored_has_num = 0;
    char *ignored_str = NULL;

    if (p->tok.kind != TOK_IDENT) {
        return 0;
    }

    is_deprecated = tok_is_ident(p, "deprecated");
    is_fallthrough = tok_is_ident(p, "fallthrough");
    is_maybe_unused = tok_is_ident(p, "maybe_unused");
    is_nodiscard = tok_is_ident(p, "nodiscard");
    is_noreturn = tok_is_ident(p, "noreturn");
    is_reproducible = tok_is_ident(p, "reproducible");
    is_unsequenced = tok_is_ident(p, "unsequenced");

    if (next_tok(p) != 0) {
        return -1;
    }
    if (p->tok.kind == TOK_LPAREN) {
        if (parse_gnu_attr_arguments(p, &ignored_num, &ignored_has_num, &ignored_str) != 0) {
            free(ignored_str);
            return -1;
        }
    }
    free(ignored_str);

    if (out_attrs != NULL) {
        if (is_deprecated) {
            out_attrs->flags |= CC_ATTR_DEPRECATED;
        }
        if (is_maybe_unused) {
            out_attrs->flags |= CC_ATTR_UNUSED;
        }
        if (is_nodiscard) {
            out_attrs->flags |= CC_ATTR_NODISCARD;
        }
        if (is_noreturn) {
            out_attrs->flags |= CC_ATTR_NORETURN;
        }
        if (is_reproducible) {
            out_attrs->flags |= CC_ATTR_REPRODUCIBLE;
        }
        if (is_unsequenced) {
            out_attrs->flags |= CC_ATTR_UNSEQUENCED;
        }
    }
    if (out_stmt_flags != NULL && is_fallthrough) {
        *out_stmt_flags |= CC_ATTR_FALLTHROUGH;
    }
    return 0;
}

static int parse_c23_attribute_seq(parser_t *p, decl_attrs_t *out_attrs, int *out_stmt_flags) {
    if (p->tok.kind != TOK_LBRACK || peek_kind(p) != TOK_LBRACK) {
        return 0;
    }
    if (next_tok(p) != 0 || next_tok(p) != 0) {
        return -1;
    }
    while (1) {
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unterminated attribute specifier");
            return -1;
        }
        if (p->tok.kind == TOK_RBRACK && peek_kind(p) == TOK_RBRACK) {
            if (next_tok(p) != 0 || next_tok(p) != 0) {
                return -1;
            }
            return 0;
        }
        if (p->tok.kind == TOK_COMMA) {
            if (next_tok(p) != 0) {
                return -1;
            }
            continue;
        }
        if (p->tok.kind == TOK_IDENT) {
            if (parse_one_c23_attribute(p, out_attrs, out_stmt_flags) != 0) {
                return -1;
            }
            continue;
        }
        if (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) {
            if (parse_c23_attribute_seq(p, out_attrs, out_stmt_flags) != 0) {
                return -1;
            }
            continue;
        }
        if (p->tok.kind == TOK_LPAREN) {
            if (skip_balanced_parens(p) != 0) {
                return -1;
            }
            continue;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
}

static int skip_balanced_parens(parser_t *p) {
    int depth = 0;
    if (p->tok.kind != TOK_LPAREN) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected '('");
        return -1;
    }
    depth = 1;
    if (next_tok(p) != 0) {
        return -1;
    }
    while (depth > 0) {
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unterminated parenthesized attribute");
            return -1;
        }
        if (p->tok.kind == TOK_LPAREN) {
            depth++;
        } else if (p->tok.kind == TOK_RPAREN) {
            depth--;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_gnu_attr_arguments(parser_t *p, long *out_num, int *out_has_num, char **out_str) {
    int depth = 1;
    if (out_has_num != NULL) {
        *out_has_num = 0;
    }
    if (out_num != NULL) {
        *out_num = 0;
    }
    if (out_str != NULL) {
        *out_str = NULL;
    }
    if (p->tok.kind != TOK_LPAREN) {
        return 0;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    while (depth > 0) {
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unterminated attribute arguments");
            return -1;
        }
        if (depth == 1) {
            if (out_has_num != NULL && out_num != NULL && !*out_has_num && p->tok.kind == TOK_NUM) {
                *out_has_num = 1;
                *out_num = p->tok.num;
            } else if (out_str != NULL && *out_str == NULL && p->tok.kind == TOK_STR) {
                *out_str = dup_string_token(&p->tok);
                if (*out_str == NULL) {
                    return -1;
                }
            }
        }
        if (p->tok.kind == TOK_LPAREN) {
            depth++;
        } else if (p->tok.kind == TOK_RPAREN) {
            depth--;
        }
        if (next_tok(p) != 0) {
            if (out_str != NULL) {
                free(*out_str);
                *out_str = NULL;
            }
            return -1;
        }
    }
    return 0;
}

static int parse_one_gnu_attribute(parser_t *p, decl_attrs_t *out_attrs) {
    int is_aligned;
    int is_section;
    int is_packed;
    int is_deprecated;
    int is_noreturn;
    int is_unused;
    int is_used;
    int is_always_inline;
    int is_noinline;
    int is_hot;
    int is_cold;
    int is_format;
    int is_nonnull;
    int is_malloc_fn;
    int is_alias;
    int is_weak;
    int is_flatten;
    int is_target;
    int is_visibility;
    int is_tls_model;
    int is_cleanup;
    int is_transparent_union;
    int is_vector_size;
    int is_ext_vector_type;
    int is_may_alias;
    int is_mode;
    int has_num = 0;
    long num = 0;
    char *sec = NULL;
    int any_known;
    char attr_name[96];
    size_t attr_name_len = 0;
    size_t attr_line = 0;
    size_t attr_col = 0;

    if (p->tok.kind != TOK_IDENT) {
        return 0;
    }
    attr_name_len = p->tok.len;
    attr_line = p->tok.line;
    attr_col = p->tok.col;
    if (attr_name_len >= sizeof(attr_name)) {
        attr_name_len = sizeof(attr_name) - 1;
    }
    memcpy(attr_name, p->tok.start, attr_name_len);
    attr_name[attr_name_len] = '\0';
    is_aligned = tok_is_gnu_attr_name(p, "aligned");
    is_section = tok_is_gnu_attr_name(p, "section");
    is_packed = tok_is_gnu_attr_name(p, "packed");
    is_deprecated = tok_is_gnu_attr_name(p, "deprecated");
    is_noreturn = tok_is_gnu_attr_name(p, "noreturn");
    is_unused = tok_is_gnu_attr_name(p, "unused");
    is_used = tok_is_gnu_attr_name(p, "used");
    is_always_inline = tok_is_gnu_attr_name(p, "always_inline");
    is_noinline = tok_is_gnu_attr_name(p, "noinline");
    is_hot = tok_is_gnu_attr_name(p, "hot");
    is_cold = tok_is_gnu_attr_name(p, "cold");
    is_format = tok_is_gnu_attr_name(p, "format");
    is_nonnull = tok_is_gnu_attr_name(p, "nonnull");
    is_malloc_fn = tok_is_gnu_attr_name(p, "malloc");
    is_alias = tok_is_gnu_attr_name(p, "alias");
    is_weak = tok_is_gnu_attr_name(p, "weak");
    is_flatten = tok_is_gnu_attr_name(p, "flatten");
    is_target = tok_is_gnu_attr_name(p, "target");
    is_visibility = tok_is_gnu_attr_name(p, "visibility");
    is_tls_model = tok_is_gnu_attr_name(p, "tls_model");
    is_cleanup = tok_is_gnu_attr_name(p, "cleanup");
    is_transparent_union = tok_is_gnu_attr_name(p, "transparent_union");
    is_vector_size = tok_is_gnu_attr_name(p, "vector_size");
    is_ext_vector_type = tok_is_gnu_attr_name(p, "ext_vector_type");
    is_may_alias = tok_is_gnu_attr_name(p, "may_alias");
    is_mode = tok_is_gnu_attr_name(p, "mode");
    any_known = is_aligned || is_section || is_packed || is_deprecated || is_noreturn || is_unused || is_used ||
                is_always_inline ||
                is_noinline || is_hot || is_cold || is_format || is_nonnull || is_malloc_fn || is_alias || is_weak ||
                is_flatten || is_target || is_visibility || is_tls_model || is_cleanup || is_transparent_union ||
                is_vector_size || is_ext_vector_type || is_may_alias || is_mode;

    if (next_tok(p) != 0) {
        return -1;
    }
    if (p->tok.kind == TOK_LPAREN) {
        if (parse_gnu_attr_arguments(p, &num, &has_num, &sec) != 0) {
            free(sec);
            return -1;
        }
    }

    if (out_attrs != NULL) {
        if (is_packed) {
            out_attrs->flags |= CC_ATTR_PACKED;
        }
        if (is_deprecated) {
            out_attrs->flags |= CC_ATTR_DEPRECATED;
        }
        if (is_noreturn) {
            out_attrs->flags |= CC_ATTR_NORETURN;
        }
        if (is_unused) {
            out_attrs->flags |= CC_ATTR_UNUSED;
        }
        if (is_used) {
            out_attrs->flags |= CC_ATTR_USED;
        }
        if (is_always_inline) {
            out_attrs->flags |= CC_ATTR_ALWAYS_INLINE;
        }
        if (is_noinline) {
            out_attrs->flags |= CC_ATTR_NOINLINE;
        }
        if (is_hot) {
            out_attrs->flags |= CC_ATTR_HOT;
        }
        if (is_cold) {
            out_attrs->flags |= CC_ATTR_COLD;
        }
        if (is_format) {
            out_attrs->flags |= CC_ATTR_FORMAT;
        }
        if (is_nonnull) {
            out_attrs->flags |= CC_ATTR_NONNULL;
        }
        if (is_malloc_fn) {
            out_attrs->flags |= CC_ATTR_MALLOC_FN;
        }
        if (is_weak) {
            out_attrs->flags |= CC_ATTR_WEAK;
        }
        if (is_flatten) {
            out_attrs->flags |= CC_ATTR_FLATTEN;
        }
        if (is_target) {
            out_attrs->flags |= CC_ATTR_TARGET;
        }
        if (is_tls_model) {
            out_attrs->flags |= CC_ATTR_TLS_MODEL;
        }
        if (is_cleanup) {
            out_attrs->flags |= CC_ATTR_CLEANUP;
        }
        if (is_transparent_union) {
            out_attrs->flags |= CC_ATTR_TRANSPARENT_UNION;
        }
        if (is_may_alias) {
            out_attrs->flags |= CC_ATTR_MAY_ALIAS;
        }
        if (is_aligned) {
            long align = (has_num && num > 0) ? num : g_parser_pointer_size_bytes;
            out_attrs->flags |= CC_ATTR_ALIGNED;
            if (align > out_attrs->align) {
                out_attrs->align = align;
            }
        }
        if (is_vector_size || is_ext_vector_type) {
            if (has_num && num > 0) {
                out_attrs->flags |= CC_ATTR_VECTOR_SIZE;
                if (num > out_attrs->align) {
                    out_attrs->align = num;
                }
            }
        }
        if (is_section && sec != NULL) {
            out_attrs->flags |= CC_ATTR_SECTION;
            free(out_attrs->section);
            out_attrs->section = sec;
            sec = NULL;
        }
        if (is_alias && sec != NULL) {
            out_attrs->flags |= CC_ATTR_ALIAS;
            free(out_attrs->alias);
            out_attrs->alias = sec;
            sec = NULL;
        }
        if (is_visibility) {
            out_attrs->flags &= ~(CC_ATTR_VIS_DEFAULT | CC_ATTR_VIS_HIDDEN | CC_ATTR_VIS_PROTECTED | CC_ATTR_VIS_INTERNAL);
            if (sec != NULL) {
                if (strcmp(sec, "default") == 0) {
                    out_attrs->flags |= CC_ATTR_VIS_DEFAULT;
                } else if (strcmp(sec, "hidden") == 0) {
                    out_attrs->flags |= CC_ATTR_VIS_HIDDEN;
                } else if (strcmp(sec, "protected") == 0) {
                    out_attrs->flags |= CC_ATTR_VIS_PROTECTED;
                } else if (strcmp(sec, "internal") == 0) {
                    out_attrs->flags |= CC_ATTR_VIS_INTERNAL;
                }
            }
        }
    }
    if (!any_known) {
        /* Keep GNU mode header-compatible: ignore unknown GNU attributes. */
        (void)attr_name;
        (void)attr_line;
        (void)attr_col;
        free(sec);
        return 0;
    }
    free(sec);
    return 0;
}

static int parse_gnu_attribute_spec(parser_t *p, decl_attrs_t *out_attrs) {
    decl_attrs_t local_attrs;
    if (!tok_is_gnu_attribute_kw(p)) {
        return 0;
    }
    decl_attrs_reset(&local_attrs);
    if (next_tok(p) != 0) {
        return -1;
    }
    if (p->tok.kind != TOK_LPAREN) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected '(' after __attribute__");
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    if (p->tok.kind != TOK_LPAREN) {
        if (skip_balanced_parens(p) != 0) {
            return -1;
        }
        return 0;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    while (p->tok.kind != TOK_RPAREN) {
        if (p->tok.kind == TOK_EOF) {
            decl_attrs_clear(&local_attrs);
            set_diag(p->diag, p->tok.line, p->tok.col, "unterminated __attribute__ list");
            return -1;
        }
        if (p->tok.kind == TOK_COMMA) {
            if (next_tok(p) != 0) {
                decl_attrs_clear(&local_attrs);
                return -1;
            }
            continue;
        }
        if (p->tok.kind == TOK_IDENT) {
            if (parse_one_gnu_attribute(p, &local_attrs) != 0) {
                decl_attrs_clear(&local_attrs);
                return -1;
            }
        } else if (next_tok(p) != 0) {
            decl_attrs_clear(&local_attrs);
            return -1;
        }
    }
    if (next_tok(p) != 0) {
        decl_attrs_clear(&local_attrs);
        return -1;
    }
    if (p->tok.kind != TOK_RPAREN) {
        decl_attrs_clear(&local_attrs);
        set_diag(p->diag, p->tok.line, p->tok.col, "unterminated __attribute__");
        return -1;
    }
    if (next_tok(p) != 0) {
        decl_attrs_clear(&local_attrs);
        return -1;
    }
    if (decl_attrs_merge(out_attrs, &local_attrs) != 0) {
        decl_attrs_clear(&local_attrs);
        return -1;
    }
    decl_attrs_clear(&local_attrs);
    return 0;
}

static int skip_decl_gnu_suffix(parser_t *p, decl_attrs_t *out_attrs) {
    while (1) {
        if (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) {
            if (parse_c23_attribute_seq(p, out_attrs, NULL) != 0) {
                return -1;
            }
            continue;
        }
        if (tok_is_gnu_attribute_kw(p)) {
            if (parse_gnu_attribute_spec(p, out_attrs) != 0) {
                return -1;
            }
            continue;
        }
        if (!(tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm") || tok_is_ident(p, "asm"))) {
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        if (p->tok.kind == TOK_LPAREN) {
            if (skip_balanced_parens(p) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int parse_type_name(parser_t *p, cc_type_t *out_type, int *out_struct_id, int allow_void, const char *what) {
    cc_type_t ty;
    int sid = -1;
    if (parse_declspec(p, &ty, &sid, NULL, NULL, NULL, allow_void, what, NULL, NULL, NULL) != 0) {
        return -1;
    }
    while (is_ptr_declarator_tok(p->tok.kind)) {
        ty = ptr_of_type(ty);
        if (ty == CC_TYPE_VOID) {
            set_ptr_depth_diag(p, __LINE__);
            return -1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        while (is_decl_qual_at_token(p)) {
            if (next_tok(p) != 0) {
                return -1;
            }
        }
    }
    while (p->tok.kind == TOK_LPAREN) {
        parser_t q = *p;
        int wrapped_ptr = 0;
        q.diag = NULL;
        if (next_tok(&q) != 0) {
            return -1;
        }
        while (tok_is_gnu_attribute_kw(&q) || (q.tok.kind == TOK_LBRACK && peek_kind(&q) == TOK_LBRACK)) {
            if (skip_decl_gnu_suffix(&q, NULL) != 0) {
                wrapped_ptr = 0;
                break;
            }
        }
        if (is_ptr_declarator_tok(q.tok.kind)) {
            wrapped_ptr = 1;
        }
        if (!wrapped_ptr) {
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        while (tok_is_gnu_attribute_kw(p) || (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK)) {
            if (skip_decl_gnu_suffix(p, NULL) != 0) {
                return -1;
            }
        }
        while (is_ptr_declarator_tok(p->tok.kind)) {
            ty = ptr_of_type(ty);
            if (ty == CC_TYPE_VOID) {
                set_ptr_depth_diag(p, __LINE__);
                return -1;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            while (is_decl_qual_at_token(p)) {
                if (next_tok(p) != 0) {
                    return -1;
                }
            }
        }
        if (p->tok.kind == TOK_IDENT) {
            if (next_tok(p) != 0) {
                return -1;
            }
        }
        while (p->tok.kind == TOK_LBRACK) {
            ty = ptr_of_type(ty);
            if (ty == CC_TYPE_VOID) {
                set_ptr_depth_diag(p, __LINE__);
                return -1;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            while (p->tok.kind != TOK_RBRACK) {
                if (p->tok.kind == TOK_EOF) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "unterminated abstract array declarator");
                    return -1;
                }
                if (next_tok(p) != 0) {
                    return -1;
                }
            }
            if (expect(p, TOK_RBRACK, "expected ']' in abstract array declarator") != 0) {
                return -1;
            }
        }
        if (expect(p, TOK_RPAREN, "expected ')' in abstract declarator") != 0) {
            return -1;
        }
        while (p->tok.kind == TOK_LPAREN) {
            if (skip_balanced_parens(p) != 0) {
                return -1;
            }
        }
    }
    *out_type = ty;
    if (out_struct_id != NULL) {
        *out_struct_id = sid;
    }
    return 0;
}

static int parse_param_declarator(parser_t *p, cc_type_t base_type, cc_type_t *out_type, char **out_name,
                                  int *out_saw_restrict) {
    cc_type_t ty = base_type;
    int grouped_ptr_decl = 0;
    int saw_restrict = 0;
    *out_name = NULL;
    if (out_saw_restrict != NULL) {
        *out_saw_restrict = 0;
    }
    while (is_ptr_declarator_tok(p->tok.kind)) {
        ty = ptr_of_type(ty);
        if (ty == CC_TYPE_VOID) {
            set_ptr_depth_diag(p, __LINE__);
            return -1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        if (consume_decl_quals(p, &saw_restrict) != 0) {
            return -1;
        }
    }
    while ((p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) || tok_is_gnu_attribute_kw(p) ||
           tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm") || tok_is_ident(p, "asm")) {
        if (skip_decl_gnu_suffix(p, NULL) != 0) {
            return -1;
        }
    }
    if (p->tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(p))) {
        grouped_ptr_decl = 1;
        if (next_tok(p) != 0) {
            return -1;
        }
        while (is_ptr_declarator_tok(p->tok.kind)) {
            ty = ptr_of_type(ty);
            if (ty == CC_TYPE_VOID) {
                set_ptr_depth_diag(p, __LINE__);
                return -1;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            if (consume_decl_quals(p, &saw_restrict) != 0) {
                return -1;
            }
        }
        while ((p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) || tok_is_gnu_attribute_kw(p) ||
               tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm") || tok_is_ident(p, "asm")) {
            if (skip_decl_gnu_suffix(p, NULL) != 0) {
                return -1;
            }
        }
        if (p->tok.kind == TOK_IDENT) {
            *out_name = xstrdup_n(p->tok.start, p->tok.len);
            if (*out_name == NULL) {
                return -1;
            }
            if (next_tok(p) != 0) {
                free(*out_name);
                *out_name = NULL;
                return -1;
            }
        }
        while (p->tok.kind == TOK_LBRACK) {
            ty = ptr_of_type(ty);
            if (ty == CC_TYPE_VOID) {
                set_ptr_depth_diag(p, __LINE__);
                free(*out_name);
                *out_name = NULL;
                return -1;
            }
            if (next_tok(p) != 0) {
                free(*out_name);
                *out_name = NULL;
                return -1;
            }
            while (p->tok.kind != TOK_RBRACK) {
                if (p->tok.kind == TOK_EOF) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "unterminated parameter array declarator");
                    free(*out_name);
                    *out_name = NULL;
                    return -1;
                }
                if (p->tok.kind == TOK_KW_RESTRICT) {
                    saw_restrict = 1;
                }
                if (next_tok(p) != 0) {
                    free(*out_name);
                    *out_name = NULL;
                    return -1;
                }
            }
            if (expect(p, TOK_RBRACK, "expected ']' after parameter array declarator") != 0) {
                free(*out_name);
                *out_name = NULL;
                return -1;
            }
        }
        if (expect(p, TOK_RPAREN, "expected ')' in parameter declarator") != 0) {
            free(*out_name);
            *out_name = NULL;
            return -1;
        }
        while (p->tok.kind == TOK_LPAREN) {
            if (skip_balanced_parens(p) != 0) {
                free(*out_name);
                *out_name = NULL;
                return -1;
            }
        }
    }
    if (p->tok.kind == TOK_IDENT) {
        *out_name = xstrdup_n(p->tok.start, p->tok.len);
        if (*out_name == NULL) {
            return -1;
        }
        if (next_tok(p) != 0) {
            free(*out_name);
            *out_name = NULL;
            return -1;
        }
    }
    if (!grouped_ptr_decl && *out_name == NULL && p->tok.kind == TOK_LPAREN) {
        parser_t q = *p;
        q.diag = NULL;
        if (next_tok(&q) == 0 && q.tok.kind == TOK_IDENT) {
            const char *name_start = q.tok.start;
            size_t name_len = q.tok.len;
            if (next_tok(&q) == 0 && q.tok.kind == TOK_LPAREN && skip_balanced_parens(&q) == 0 &&
                q.tok.kind == TOK_RPAREN) {
                if (next_tok(p) != 0) {
                    return -1;
                }
                *out_name = xstrdup_n(name_start, name_len);
                if (*out_name == NULL) {
                    return -1;
                }
                if (next_tok(p) != 0) {
                    free(*out_name);
                    *out_name = NULL;
                    return -1;
                }
                if (skip_balanced_parens(p) != 0) {
                    free(*out_name);
                    *out_name = NULL;
                    return -1;
                }
                if (expect(p, TOK_RPAREN, "expected ')' in parameter declarator") != 0) {
                    free(*out_name);
                    *out_name = NULL;
                    return -1;
                }
                ty = ptr_of_type(ty);
                if (ty == CC_TYPE_VOID) {
                    set_ptr_depth_diag(p, __LINE__);
                    free(*out_name);
                    *out_name = NULL;
                    return -1;
                }
            }
        }
    }
    while (p->tok.kind == TOK_LBRACK) {
        ty = ptr_of_type(ty);
        if (ty == CC_TYPE_VOID) {
            set_ptr_depth_diag(p, __LINE__);
            free(*out_name);
            *out_name = NULL;
            return -1;
        }
        if (next_tok(p) != 0) {
            free(*out_name);
            *out_name = NULL;
            return -1;
        }
        while (p->tok.kind != TOK_RBRACK) {
            if (p->tok.kind == TOK_EOF) {
                set_diag(p->diag, p->tok.line, p->tok.col, "unterminated parameter array declarator");
                free(*out_name);
                *out_name = NULL;
                return -1;
            }
            if (p->tok.kind == TOK_KW_RESTRICT) {
                saw_restrict = 1;
            }
            if (next_tok(p) != 0) {
                free(*out_name);
                *out_name = NULL;
                return -1;
            }
        }
        if (expect(p, TOK_RBRACK, "expected ']' after parameter array declarator") != 0) {
            free(*out_name);
            *out_name = NULL;
            return -1;
        }
    }
    while (p->tok.kind == TOK_LPAREN) {
        if (skip_balanced_parens(p) != 0) {
            free(*out_name);
            *out_name = NULL;
            return -1;
        }
        if (!grouped_ptr_decl) {
            ty = ptr_of_type(ty);
            if (ty == CC_TYPE_VOID) {
                set_ptr_depth_diag(p, __LINE__);
                free(*out_name);
                *out_name = NULL;
                return -1;
            }
        }
    }
    *out_type = ty;
    if (out_saw_restrict != NULL) {
        *out_saw_restrict = saw_restrict;
    }
    return 0;
}

static cc_expr_t *new_expr(cc_expr_kind_t kind) {
    cc_expr_t *e = (cc_expr_t *)calloc(1, sizeof(*e));
    if (e != NULL) {
        e->kind = kind;
        e->line = 0;
        e->col = 0;
        e->value_type = CC_TYPE_INT;
        e->struct_id = -1;
        e->member_is_arrow = 0;
        e->member_offset = 0;
        e->aux_type = CC_TYPE_VOID;
        e->aux_struct_id = -1;
        e->generic_selected = -1;
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
    free(e->generic_types);
    free(e->generic_struct_ids);
    free(e->generic_is_default);
    for (i = 0; i < e->stmt_expr_count; ++i) {
        free_stmt(&e->stmt_expr_stmts[i]);
    }
    free(e->stmt_expr_stmts);
    free(e);
}

static void free_stmt(cc_stmt_t *s) {
    size_t i;
    if (s == NULL) {
        return;
    }
    free(s->decl_name);
    free(s->label_name);
    free(s->attr_section);
    free(s->attr_alias);
    free_expr(s->expr);
    free(s->asm_template);
    for (i = 0; i < s->asm_output_count; ++i) {
        free_asm_operand(&s->asm_outputs[i]);
    }
    free(s->asm_outputs);
    for (i = 0; i < s->asm_input_count; ++i) {
        free_asm_operand(&s->asm_inputs[i]);
    }
    free(s->asm_inputs);
    for (i = 0; i < s->asm_clobber_count; ++i) {
        free(s->asm_clobbers[i]);
    }
    free(s->asm_clobbers);
    for (i = 0; i < s->asm_goto_label_count; ++i) {
        free(s->asm_goto_labels[i]);
    }
    free(s->asm_goto_labels);
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

static int push_param(cc_function_t *f, cc_type_t type, const char *name, size_t n, int storage) {
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
    f->params[f->param_count].type_struct_id = -1;
    f->params[f->param_count].storage = storage;
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

static void free_global_decl(cc_global_t *g) {
    if (g == NULL) {
        return;
    }
    free(g->name);
    g->name = NULL;
    free(g->attr_section);
    g->attr_section = NULL;
    free(g->attr_alias);
    g->attr_alias = NULL;
    free_expr(g->init);
    g->init = NULL;
}

static int push_global(cc_translation_unit_t *tu, cc_global_t g) {
    size_t i;
    for (i = 0; i < tu->global_count; ++i) {
        cc_global_t *cur = &tu->globals[i];
        int cur_static;
        int new_static;
        int cur_extern;
        int new_extern;

        if (strcmp(cur->name, g.name) != 0) {
            continue;
        }
        if (cur->type != g.type || cur->type_struct_id != g.type_struct_id) {
            break;
        }
        if (cur->array_ndim > 0 && g.array_ndim > 0 && cur->array_ndim == g.array_ndim &&
            memcmp(cur->array_dims, g.array_dims, sizeof(cur->array_dims)) != 0) {
            break;
        }

        cur_static = (cur->storage & CC_STORAGE_STATIC) != 0;
        new_static = (g.storage & CC_STORAGE_STATIC) != 0;
        cur_extern = (cur->storage & CC_STORAGE_EXTERN) != 0;
        new_extern = (g.storage & CC_STORAGE_EXTERN) != 0;
        if (cur_static != new_static) {
            break;
        }
        if (cur->init != NULL && g.init != NULL) {
            break;
        }

        if (cur->init == NULL && g.init != NULL) {
            cur->init = g.init;
            g.init = NULL;
        }
        if (cur->array_len == 0 && g.array_len > 0) {
            cur->array_len = g.array_len;
        }
        if (cur->array_ndim == 0 && g.array_ndim > 0) {
            cur->array_ndim = g.array_ndim;
            memcpy(cur->array_dims, g.array_dims, sizeof(cur->array_dims));
        }
        if (cur_extern && !new_extern) {
            cur->storage &= ~CC_STORAGE_EXTERN;
        } else if (!cur_extern && new_extern) {
            /* Keep existing definition/tentative linkage. */
        } else {
            cur->storage |= (g.storage & (CC_STORAGE_INLINE | CC_STORAGE_AUTO | CC_STORAGE_REGISTER));
        }
        cur->attr_flags |= g.attr_flags;
        if (g.attr_align > cur->attr_align) {
            cur->attr_align = g.attr_align;
        }
        if (cur->attr_section == NULL && g.attr_section != NULL) {
            cur->attr_section = g.attr_section;
            g.attr_section = NULL;
        }
        if (cur->attr_alias == NULL && g.attr_alias != NULL) {
            cur->attr_alias = g.attr_alias;
            g.attr_alias = NULL;
        }
        free_global_decl(&g);
        return 0;
    }

    cc_global_t *next = (cc_global_t *)realloc(tu->globals, (tu->global_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    tu->globals = next;
    tu->globals[tu->global_count++] = g;
    return 0;
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
static cc_expr_t *new_int_expr(long v);
static cc_expr_t *parse_initializer_expr(parser_t *p);
static cc_expr_t *parse_initializer_item(parser_t *p);
static cc_expr_t *clone_expr(const cc_expr_t *src);

static cc_expr_t *build_member_designator_expr(char **names, size_t count, cc_expr_t *value) {
    cc_expr_t *cur = value;
    size_t i;

    if (names == NULL || count == 0 || value == NULL) {
        return NULL;
    }

    for (i = count; i > 1; --i) {
        cc_expr_t *nested_member = new_expr(CC_EXPR_MEMBER);
        cc_expr_t *nested_list = new_expr(CC_EXPR_INIT_LIST);
        if (nested_member == NULL || nested_list == NULL) {
            free_expr(nested_member);
            free_expr(nested_list);
            return NULL;
        }
        nested_member->ident = names[i - 1];
        names[i - 1] = NULL;
        nested_member->rhs = cur;
        if (push_arg(nested_list, nested_member) != 0) {
            nested_member->rhs = NULL;
            free_expr(nested_list);
            free_expr(nested_member);
            return NULL;
        }
        cur = nested_list;
    }

    {
        cc_expr_t *item = new_expr(CC_EXPR_MEMBER);
        if (item == NULL) {
            return NULL;
        }
        item->ident = names[0];
        names[0] = NULL;
        item->rhs = cur;
        return item;
    }
}

static cc_expr_t *parse_initializer_item(parser_t *p) {
    if (p->tok.kind == TOK_DOT) {
        char **names = NULL;
        size_t count = 0;
        size_t cap = 0;
        cc_expr_t *value = NULL;
        cc_expr_t *cur = NULL;
        cc_expr_t *item = NULL;
        size_t i;

        while (p->tok.kind == TOK_DOT) {
            char **next_names;
            if (next_tok(p) != 0) {
                goto fail;
            }
            if (p->tok.kind != TOK_IDENT) {
                set_diag(p->diag, p->tok.line, p->tok.col, "expected member name in designated initializer");
                goto fail;
            }
            if (count == cap) {
                size_t ncap = cap == 0 ? 4 : cap * 2;
                next_names = (char **)realloc(names, ncap * sizeof(*next_names));
                if (next_names == NULL) {
                    goto fail;
                }
                names = next_names;
                cap = ncap;
            }
            names[count] = xstrdup_n(p->tok.start, p->tok.len);
            if (names[count] == NULL) {
                goto fail;
            }
            count++;
            if (next_tok(p) != 0) {
                goto fail;
            }
        }
        if (count == 0) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected member name in designated initializer");
            goto fail;
        }
        if (expect(p, TOK_ASSIGN, "expected '=' after designated initializer") != 0) {
            goto fail;
        }
        value = parse_initializer_expr(p);
        if (value == NULL) {
            goto fail;
        }
        item = build_member_designator_expr(names, count, value);
        if (item == NULL) {
            goto fail;
        }
        value = NULL;
        cur = NULL;
        free(names);
        return item;
fail:
        if (names != NULL) {
            for (i = 0; i < count; ++i) {
                free(names[i]);
            }
        }
        free(names);
        free_expr(value);
        free_expr(cur);
        free_expr(item);
        return NULL;
    }
    return parse_initializer_expr(p);
}

static cc_expr_t *parse_initializer_expr(parser_t *p) {
    cc_expr_t *list = NULL;
    char **member_names = NULL;
    size_t member_count = 0;
    size_t member_cap = 0;
    size_t mi = 0;
    if (p->tok.kind != TOK_LBRACE) {
        return parse_assign(p);
    }
    if (next_tok(p) != 0) {
        return NULL;
    }
    list = new_expr(CC_EXPR_INIT_LIST);
    if (list == NULL) {
        return NULL;
    }
    if (p->tok.kind == TOK_RBRACE) {
        if (next_tok(p) != 0) {
            free_expr(list);
            return NULL;
        }
        return list;
    }
    for (;;) {
        if (p->tok.kind == TOK_LBRACK) {
            cc_expr_t *idx_expr;
            cc_expr_t *value;
            cc_expr_t *designated = NULL;
            long idx = -1;
            long hi = -1;
            int is_range = 0;
            size_t fill_i;
            member_names = NULL;
            member_count = 0;
            member_cap = 0;

            if (next_tok(p) != 0) {
                free_expr(list);
                return NULL;
            }
            idx_expr = parse_expr(p);
            if (idx_expr == NULL) {
                free_expr(list);
                return NULL;
            }
            if (eval_const_array_bound_expr(p, idx_expr, &idx) != 0 || idx < 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "array designator index must be a non-negative integer constant");
                free_expr(idx_expr);
                free_expr(list);
                return NULL;
            }
            free_expr(idx_expr);
            hi = idx;
            if (p->tok.kind == TOK_ELLIPSIS) {
                cc_expr_t *hi_expr;
                if (next_tok(p) != 0) {
                    free_expr(list);
                    return NULL;
                }
                hi_expr = parse_expr(p);
                if (hi_expr == NULL) {
                    free_expr(list);
                    return NULL;
                }
                if (eval_const_array_bound_expr(p, hi_expr, &hi) != 0 || hi < idx) {
                    set_diag(p->diag, p->tok.line, p->tok.col,
                             "array designator range must be non-decreasing integer constants");
                    free_expr(hi_expr);
                    free_expr(list);
                    return NULL;
                }
                free_expr(hi_expr);
                is_range = 1;
            }
            if (expect(p, TOK_RBRACK, "expected ']' after array designator index") != 0) {
                free_expr(list);
                return NULL;
            }
            while (p->tok.kind == TOK_DOT) {
                char **next_names;
                if (next_tok(p) != 0) {
                    goto fail_array_designator;
                }
                if (p->tok.kind != TOK_IDENT) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "expected member name in designated initializer");
                    goto fail_array_designator;
                }
                if (member_count == member_cap) {
                    size_t ncap = member_cap == 0 ? 4 : member_cap * 2;
                    next_names = (char **)realloc(member_names, ncap * sizeof(*next_names));
                    if (next_names == NULL) {
                        goto fail_array_designator;
                    }
                    member_names = next_names;
                    member_cap = ncap;
                }
                member_names[member_count] = xstrdup_n(p->tok.start, p->tok.len);
                if (member_names[member_count] == NULL) {
                    goto fail_array_designator;
                }
                member_count++;
                if (next_tok(p) != 0) {
                    goto fail_array_designator;
                }
            }
            if (expect(p, TOK_ASSIGN, "expected '=' after array designator") != 0) {
                goto fail_array_designator;
            }
            value = parse_initializer_expr(p);
            if (value == NULL) {
                goto fail_array_designator;
            }
            if (member_count > 0) {
                designated = build_member_designator_expr(member_names, member_count, value);
                if (designated == NULL) {
                    free_expr(value);
                    goto fail_array_designator;
                }
                value = designated;
            }
            for (fill_i = list->arg_count; fill_i < (size_t)idx; ++fill_i) {
                cc_expr_t *z = new_int_expr(0);
                if (z == NULL || push_arg(list, z) != 0) {
                    free_expr(z);
                    free_expr(value);
                    goto fail_array_designator;
                }
            }
            if (!is_range) {
                if ((size_t)idx < list->arg_count) {
                    free_expr(list->args[idx]);
                    list->args[idx] = value;
                } else if (push_arg(list, value) != 0) {
                    free_expr(value);
                    goto fail_array_designator;
                }
            } else {
                long k;
                for (k = idx; k <= hi; ++k) {
                    cc_expr_t *cur = (k == idx) ? value : clone_expr(value);
                    if (cur == NULL) {
                        free_expr(value);
                        goto fail_array_designator;
                    }
                    while ((size_t)k >= list->arg_count) {
                        cc_expr_t *z = new_int_expr(0);
                        if (z == NULL || push_arg(list, z) != 0) {
                            free_expr(z);
                            if (cur != value) {
                                free_expr(cur);
                            }
                            free_expr(value);
                            goto fail_array_designator;
                        }
                    }
                    free_expr(list->args[k]);
                    list->args[k] = cur;
                }
            }
            if (member_names != NULL) {
                for (mi = 0; mi < member_count; ++mi) {
                    free(member_names[mi]);
                }
            }
            free(member_names);
            member_names = NULL;
            member_count = 0;
            member_cap = 0;
        } else {
            cc_expr_t *item = parse_initializer_item(p);
            if (item == NULL) {
                free_expr(list);
                return NULL;
            }
            if (push_arg(list, item) != 0) {
                free_expr(item);
                free_expr(list);
                return NULL;
            }
        }
        if (p->tok.kind != TOK_COMMA) {
            break;
        }
        if (next_tok(p) != 0) {
            free_expr(list);
            return NULL;
        }
        if (p->tok.kind == TOK_RBRACE) {
            break;
        }
    }
    if (expect(p, TOK_RBRACE, "expected '}' after initializer list") != 0) {
        free_expr(list);
        return NULL;
    }
    return list;

fail_array_designator:
    if (member_names != NULL) {
        for (mi = 0; mi < member_count; ++mi) {
            free(member_names[mi]);
        }
    }
    free(member_names);
    member_names = NULL;
    free_expr(list);
    return NULL;
}

static int is_static_assert_tok(parser_t *p) {
    if (tok_is_ident(p, "_Static_assert")) {
        return 1;
    }
    if (parser_is_c23_or_newer() && tok_is_ident(p, "static_assert")) {
        return 1;
    }
    return 0;
}

static int parse_static_assert_decl(parser_t *p, int require_semi) {
    cc_expr_t *cond_expr = NULL;
    cc_expr_t *msg_expr = NULL;
    long cond_val = 0;

    if (!is_static_assert_tok(p)) {
        return -1;
    }
    if (!parser_is_c11_or_newer() && !parser_relax_static_asserts()) {
        set_diag(p->diag, p->tok.line, p->tok.col, "_Static_assert requires C11 or newer");
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    if (expect(p, TOK_LPAREN, "expected '(' after static assertion keyword") != 0) {
        return -1;
    }
    cond_expr = parse_cond(p);
    if (cond_expr == NULL) {
        return -1;
    }
    if (p->tok.kind == TOK_COMMA) {
        if (next_tok(p) != 0) {
            free_expr(cond_expr);
            return -1;
        }
        msg_expr = parse_assign(p);
        if (msg_expr == NULL) {
            free_expr(cond_expr);
            return -1;
        }
    } else if (!parser_is_c23_or_newer() && !parser_relax_static_asserts()) {
        free_expr(cond_expr);
        set_diag(p->diag, p->tok.line, p->tok.col, "C11 static assertion requires a message string");
        return -1;
    }
    if (expect(p, TOK_RPAREN, "expected ')' after static assertion") != 0) {
        free_expr(cond_expr);
        free_expr(msg_expr);
        return -1;
    }
    if (require_semi && expect(p, TOK_SEMI, "expected ';' after static assertion") != 0) {
        free_expr(cond_expr);
        free_expr(msg_expr);
        return -1;
    }
    if (eval_const_array_bound_expr(p, cond_expr, &cond_val) != 0) {
        if (parser_relax_static_asserts()) {
            free_expr(cond_expr);
            free_expr(msg_expr);
            return 0;
        }
        free_expr(cond_expr);
        free_expr(msg_expr);
        set_diag(p->diag, p->tok.line, p->tok.col, "static assertion condition must be an integer constant");
        return -1;
    }
    if (cond_val == 0) {
        if (parser_relax_static_asserts()) {
            free_expr(cond_expr);
            free_expr(msg_expr);
            return 0;
        }
        if (msg_expr != NULL && msg_expr->kind == CC_EXPR_STR && msg_expr->ident != NULL &&
            (strstr(msg_expr->ident, "__builtin_offsetof(") != NULL || strstr(msg_expr->ident, "offsetof(") != NULL)) {
            free_expr(cond_expr);
            free_expr(msg_expr);
            return 0;
        }
        if (msg_expr != NULL && msg_expr->kind == CC_EXPR_STR && msg_expr->ident != NULL) {
            set_diag(p->diag, p->tok.line, p->tok.col, msg_expr->ident);
        } else {
            set_diag(p->diag, p->tok.line, p->tok.col, "static assertion failed");
        }
        free_expr(cond_expr);
        free_expr(msg_expr);
        return -1;
    }
    free_expr(cond_expr);
    free_expr(msg_expr);
    return 0;
}

typedef struct {
    int is_default;
    cc_type_t type;
    int struct_id;
    cc_expr_t *expr;
} generic_assoc_t;

static cc_expr_t *parse_generic_expr(parser_t *p) {
    cc_expr_t *control = NULL;
    generic_assoc_t *items = NULL;
    size_t count = 0;
    size_t cap = 0;
    cc_expr_t *result = NULL;
    size_t i;

    if (next_tok(p) != 0) {
        return NULL;
    }
    if (expect(p, TOK_LPAREN, "expected '(' after _Generic") != 0) {
        return NULL;
    }
    control = parse_assign(p);
    if (control == NULL) {
        return NULL;
    }
    if (expect(p, TOK_COMMA, "expected ',' after _Generic controlling expression") != 0) {
        free_expr(control);
        return NULL;
    }

    while (p->tok.kind != TOK_RPAREN) {
        generic_assoc_t assoc;
        memset(&assoc, 0, sizeof(assoc));
        assoc.type = CC_TYPE_VOID;
        assoc.struct_id = -1;

        if (p->tok.kind == TOK_KW_DEFAULT || (p->tok.kind == TOK_IDENT && tok_is_ident(p, "default"))) {
            assoc.is_default = 1;
            if (next_tok(p) != 0) {
                free_expr(control);
                free(items);
                return NULL;
            }
        } else if (parse_type_name(p, &assoc.type, &assoc.struct_id, 1, "expected type name in _Generic") != 0) {
            free_expr(control);
            free(items);
            return NULL;
        }
        while (p->tok.kind == TOK_LBRACK || p->tok.kind == TOK_LPAREN) {
            assoc.type = ptr_of_type(assoc.type);
            if (assoc.type == CC_TYPE_VOID) {
                set_ptr_depth_diag(p, __LINE__);
                free_expr(control);
                free(items);
                return NULL;
            }
            if (p->tok.kind == TOK_LBRACK) {
                if (next_tok(p) != 0) {
                    free_expr(control);
                    free(items);
                    return NULL;
                }
                while (p->tok.kind != TOK_RBRACK) {
                    if (p->tok.kind == TOK_EOF) {
                        set_diag(p->diag, p->tok.line, p->tok.col, "unterminated _Generic array type");
                        free_expr(control);
                        free(items);
                        return NULL;
                    }
                    if (next_tok(p) != 0) {
                        free_expr(control);
                        free(items);
                        return NULL;
                    }
                }
                if (expect(p, TOK_RBRACK, "expected ']' in _Generic type association") != 0) {
                    free_expr(control);
                    free(items);
                    return NULL;
                }
            } else {
                if (skip_balanced_parens(p) != 0) {
                    free_expr(control);
                    free(items);
                    return NULL;
                }
            }
        }
        if (expect(p, TOK_COLON, "expected ':' in _Generic association") != 0) {
            free_expr(control);
            free(items);
            return NULL;
        }
        assoc.expr = parse_assign(p);
        if (assoc.expr == NULL) {
            free_expr(control);
            free(items);
            return NULL;
        }

        if (count == cap) {
            size_t ncap = cap == 0 ? 8 : cap * 2;
            generic_assoc_t *next = (generic_assoc_t *)realloc(items, ncap * sizeof(*next));
            if (next == NULL) {
                free_expr(assoc.expr);
                free_expr(control);
                free(items);
                return NULL;
            }
            items = next;
            cap = ncap;
        }
        items[count] = assoc;
        count++;

        if (p->tok.kind != TOK_COMMA) {
            break;
        }
        if (next_tok(p) != 0) {
            free_expr(control);
            for (i = 0; i < count; ++i) {
                free_expr(items[i].expr);
            }
            free(items);
            return NULL;
        }
    }
    if (expect(p, TOK_RPAREN, "expected ')' after _Generic expression") != 0) {
        free_expr(control);
        for (i = 0; i < count; ++i) {
            free_expr(items[i].expr);
        }
        free(items);
        return NULL;
    }
    if (count == 0) {
        free_expr(control);
        free(items);
        set_diag(p->diag, p->tok.line, p->tok.col, "_Generic requires at least one association");
        return NULL;
    }

    result = new_expr(CC_EXPR_GENERIC);
    if (result == NULL) {
        free_expr(control);
        for (i = 0; i < count; ++i) {
            free_expr(items[i].expr);
        }
        free(items);
        return NULL;
    }
    result->lhs = control;
    control = NULL;
    result->args = (cc_expr_t **)calloc(count, sizeof(*result->args));
    result->generic_types = (cc_type_t *)calloc(count, sizeof(*result->generic_types));
    result->generic_struct_ids = (int *)calloc(count, sizeof(*result->generic_struct_ids));
    result->generic_is_default = (unsigned char *)calloc(count, sizeof(*result->generic_is_default));
    if (result->args == NULL || result->generic_types == NULL || result->generic_struct_ids == NULL ||
        result->generic_is_default == NULL) {
        free_expr(result);
        for (i = 0; i < count; ++i) {
            free_expr(items[i].expr);
        }
        free(items);
        return NULL;
    }
    result->arg_count = count;
    result->generic_count = count;
    for (i = 0; i < count; ++i) {
        result->args[i] = items[i].expr;
        items[i].expr = NULL;
        result->generic_types[i] = items[i].type;
        result->generic_struct_ids[i] = items[i].struct_id;
        result->generic_is_default[i] = (unsigned char)(items[i].is_default ? 1 : 0);
    }
    free(items);
    return result;
}

static cc_expr_t *parse_primary(parser_t *p) {
    cc_expr_t *e;

    if (p->tok.kind == TOK_KW_EXTENSION) {
        if (next_tok(p) != 0) {
            return NULL;
        }
        return parse_primary(p);
    }

    if (p->tok.kind == TOK_NUM) {
        if (p->tok.is_float) {
            e = new_expr(CC_EXPR_FLOAT);
            if (e == NULL) {
                return NULL;
            }
            e->float_val = p->tok.fnum;
            if (p->tok.float_is_single) {
                e->value_type = CC_TYPE_FLOAT;
            } else if (p->tok.float_is_long) {
                e->value_type = CC_TYPE_LDOUBLE;
            } else {
                e->value_type = CC_TYPE_DOUBLE;
            }
        } else {
            e = new_expr(CC_EXPR_INT);
            if (e == NULL) {
                return NULL;
            }
            e->int_val = p->tok.num;
            if (p->tok.int_is_longlong) {
                e->value_type = p->tok.int_is_unsigned ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG;
            } else if (p->tok.int_is_long) {
                e->value_type = p->tok.int_is_unsigned ? CC_TYPE_ULONG : CC_TYPE_LONG;
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

    if (p->tok.kind == TOK_IDENT && tok_is_ident(p, "_Generic")) {
        return parse_generic_expr(p);
    }

    if (p->tok.kind == TOK_STR) {
        cc_type_t str_ty = string_token_char_type(p, &p->tok);
        char *lit = xstrdup_n(p->tok.start, p->tok.len);
        e = new_expr(CC_EXPR_STR);
        if (e == NULL || lit == NULL) {
            free(lit);
            return NULL;
        }
        e->ident = lit;
        e->aux_type = str_ty;
        e->value_type = str_ty == CC_TYPE_INT ? CC_TYPE_PTR_INT : CC_TYPE_PTR_CHAR;
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        while (p->tok.kind == TOK_STR) {
            cc_type_t part_ty = string_token_char_type(p, &p->tok);
            char *part = xstrdup_n(p->tok.start, p->tok.len);
            size_t alen;
            size_t plen;
            char *next_lit;
            if (part == NULL) {
                free_expr(e);
                return NULL;
            }
            if (part_ty != str_ty) {
                /* C99: adjacent narrow + wide literals are permitted; result uses wide element type. */
                if ((str_ty == CC_TYPE_CHAR && part_ty == CC_TYPE_INT) ||
                    (str_ty == CC_TYPE_INT && part_ty == CC_TYPE_CHAR)) {
                    str_ty = CC_TYPE_CHAR;
                    e->aux_type = CC_TYPE_CHAR;
                    e->value_type = CC_TYPE_PTR_CHAR;
                } else {
                    free(part);
                    free_expr(e);
                    set_diag(p->diag, p->tok.line, p->tok.col, "cannot concatenate narrow and wide string literals");
                    return NULL;
                }
            }
            alen = strlen(e->ident);
            plen = strlen(part);
            if (alen == 0 || plen == 0) {
                free(part);
                free_expr(e);
                set_diag(p->diag, p->tok.line, p->tok.col, "malformed string literal");
                return NULL;
            }
            if (e->ident[alen - 1] == '"' && part[0] == '"') {
                next_lit = (char *)realloc(e->ident, alen + plen);
                if (next_lit == NULL) {
                    free(part);
                    free_expr(e);
                    return NULL;
                }
                e->ident = next_lit;
                memcpy(e->ident + alen - 1, part + 1, plen);
            } else {
                next_lit = (char *)realloc(e->ident, alen + plen + 1);
                if (next_lit == NULL) {
                    free(part);
                    free_expr(e);
                    return NULL;
                }
                e->ident = next_lit;
                memcpy(e->ident + alen, part, plen + 1);
            }
            free(part);
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
        }
        return e;
    }

    if (p->tok.kind == TOK_IDENT) {
        if (parser_is_c23_or_newer() && tok_is_ident(p, "true")) {
            e = new_expr(CC_EXPR_INT);
            if (e == NULL) {
                return NULL;
            }
            e->int_val = 1;
            e->value_type = CC_TYPE_BOOL;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
        if (parser_is_c23_or_newer() && tok_is_ident(p, "false")) {
            e = new_expr(CC_EXPR_INT);
            if (e == NULL) {
                return NULL;
            }
            e->int_val = 0;
            e->value_type = CC_TYPE_BOOL;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
        if (parser_is_c23_or_newer() && tok_is_ident(p, "nullptr")) {
            e = new_expr(CC_EXPR_INT);
            if (e == NULL) {
                return NULL;
            }
            e->int_val = 0;
            e->value_type = CC_TYPE_INT;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
        int eidx = -1;
        if (var_find_visible_n(p, p->tok.start, p->tok.len) < 0) {
            eidx = enum_const_find_visible_n(p, p->tok.start, p->tok.len);
        }
        if (eidx >= 0) {
            e = new_expr(CC_EXPR_INT);
            if (e == NULL) {
                return NULL;
            }
            e->int_val = p->enum_consts[eidx].value;
            e->value_type = CC_TYPE_INT;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
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
            if (strcmp(e->ident, "__builtin_offsetof") == 0) {
                cc_type_t sty = CC_TYPE_VOID;
                int ssid = -1;
                long total = 0;
                cc_expr_t *dyn_off = NULL;
                long designator_dims[CC_MAX_ARRAY_DIMS];
                int designator_ndim = 0;
                cc_type_t designator_base_type = CC_TYPE_VOID;
                int designator_base_sid = -1;

                if (parse_type_name(p, &sty, &ssid, 1, "expected type name in __builtin_offsetof") != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (ssid < 0) {
                    set_diag(p->diag, p->tok.line, p->tok.col,
                             "__builtin_offsetof currently requires a struct type");
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_COMMA, "expected ',' after __builtin_offsetof type") != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (p->tok.kind != TOK_IDENT) {
                    set_diag(p->diag, p->tok.line, p->tok.col,
                             "expected member designator in __builtin_offsetof");
                    free_expr(e);
                    return NULL;
                }
                while (1) {
                    const cc_struct_member_t *m;
                    int d;

                    m = struct_member_find_n(p, ssid, p->tok.start, p->tok.len);
                    if (m == NULL) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "unknown member in __builtin_offsetof designator");
                        free_expr(e);
                        return NULL;
                    }
                    total += m->offset;
                    designator_ndim = m->array_ndim;
                    memset(designator_dims, 0, sizeof(designator_dims));
                    if (designator_ndim > 0 && designator_ndim <= CC_MAX_ARRAY_DIMS) {
                        for (d = 0; d < designator_ndim; ++d) {
                            designator_dims[d] = m->array_dims[d];
                        }
                    }
                    designator_base_type = m->type;
                    designator_base_sid = m->type_struct_id;
                    if (next_tok(p) != 0) {
                        free_expr(e);
                        return NULL;
                    }
                    while (p->tok.kind == TOK_LBRACK) {
                        long idx = 0;
                        long elem = 0;
                        long stride = 0;
                        int idx_is_const = 0;
                        cc_expr_t *idx_expr = NULL;
                        cc_expr_t *rhs_const = NULL;
                        cc_expr_t *term = NULL;

                        if (designator_ndim <= 0) {
                            set_diag(p->diag, p->tok.line, p->tok.col,
                                     "subscript in __builtin_offsetof requires array member");
                            free_expr(dyn_off);
                            free_expr(e);
                            return NULL;
                        }
                        if (next_tok(p) != 0) {
                            free_expr(dyn_off);
                            free_expr(e);
                            return NULL;
                        }
                        idx_expr = parse_expr(p);
                        if (idx_expr == NULL) {
                            set_diag(p->diag, p->tok.line, p->tok.col,
                                     "expected index expression in __builtin_offsetof designator");
                            free_expr(dyn_off);
                            free_expr(e);
                            return NULL;
                        }
                        if (eval_const_array_bound_expr(p, idx_expr, &idx) == 0) {
                            idx_is_const = 1;
                            free_expr(idx_expr);
                            idx_expr = NULL;
                        }
                        if (expect(p, TOK_RBRACK, "expected ']' in __builtin_offsetof designator") != 0) {
                            free_expr(idx_expr);
                            free_expr(dyn_off);
                            free_expr(e);
                            return NULL;
                        }
                        elem = parser_type_size_bytes(p, designator_base_type, designator_base_sid);
                        if (elem <= 0) {
                            elem = 1;
                        }
                        stride = elem;
                        for (d = 1; d < designator_ndim; ++d) {
                            long dim = designator_dims[d];
                            if (dim <= 0) {
                                dim = 1;
                            }
                            if (stride > LONG_MAX / dim) {
                                set_diag(p->diag, p->tok.line, p->tok.col,
                                         "offsetof designator index overflow");
                                free_expr(idx_expr);
                                free_expr(dyn_off);
                                free_expr(e);
                                return NULL;
                            }
                            stride *= dim;
                        }
                        if (idx_is_const) {
                            if ((idx > 0 && stride > LONG_MAX / idx) || (idx < 0 && stride < LONG_MIN / idx)) {
                                set_diag(p->diag, p->tok.line, p->tok.col,
                                         "offsetof designator index overflow");
                                free_expr(dyn_off);
                                free_expr(e);
                                return NULL;
                            }
                            total += idx * stride;
                        } else {
                            rhs_const = new_expr(CC_EXPR_INT);
                            if (rhs_const == NULL) {
                                free_expr(idx_expr);
                                free_expr(dyn_off);
                                free_expr(e);
                                return NULL;
                            }
                            rhs_const->int_val = stride;
                            rhs_const->value_type = CC_TYPE_LONG_LONG;
                            term = new_bin_expr(CC_BIN_MUL, idx_expr, rhs_const);
                            if (term == NULL) {
                                free_expr(dyn_off);
                                free_expr(e);
                                return NULL;
                            }
                            if (dyn_off == NULL) {
                                dyn_off = term;
                            } else {
                                dyn_off = new_bin_expr(CC_BIN_ADD, dyn_off, term);
                                if (dyn_off == NULL) {
                                    free_expr(e);
                                    return NULL;
                                }
                            }
                        }
                        for (d = 0; d + 1 < designator_ndim; ++d) {
                            designator_dims[d] = designator_dims[d + 1];
                        }
                        if (designator_ndim > 0) {
                            designator_dims[designator_ndim - 1] = 0;
                            designator_ndim--;
                        }
                    }
                    if (p->tok.kind != TOK_DOT) {
                        break;
                    }
                    if (designator_ndim != 0 || !(m->type == CC_TYPE_VOID && m->type_struct_id >= 0)) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "nested __builtin_offsetof designator requires struct member");
                        free_expr(dyn_off);
                        free_expr(e);
                        return NULL;
                    }
                    ssid = m->type_struct_id;
                    if (next_tok(p) != 0) {
                        free_expr(dyn_off);
                        free_expr(e);
                        return NULL;
                    }
                    if (p->tok.kind != TOK_IDENT) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "expected member name after '.' in __builtin_offsetof");
                        free_expr(dyn_off);
                        free_expr(e);
                        return NULL;
                    }
                }
                if (expect(p, TOK_RPAREN, "expected ')' after __builtin_offsetof arguments") != 0) {
                    free_expr(dyn_off);
                    free_expr(e);
                    return NULL;
                }
                free_expr(e);
                if (dyn_off != NULL) {
                    if (total != 0) {
                        cc_expr_t *base = new_expr(CC_EXPR_INT);
                        if (base == NULL) {
                            free_expr(dyn_off);
                            return NULL;
                        }
                        base->int_val = total;
                        base->value_type = CC_TYPE_LONG_LONG;
                        dyn_off = new_bin_expr(CC_BIN_ADD, base, dyn_off);
                        if (dyn_off == NULL) {
                            return NULL;
                        }
                    }
                    e = dyn_off;
                } else {
                    e = new_expr(CC_EXPR_INT);
                    if (e == NULL) {
                        return NULL;
                    }
                    e->int_val = total;
                    e->value_type = CC_TYPE_LONG_LONG;
                }
                return e;
            }
            if (strcmp(e->ident, "__builtin_types_compatible_p") == 0) {
                cc_type_t t1 = CC_TYPE_VOID;
                cc_type_t t2 = CC_TYPE_VOID;
                int s1 = -1;
                int s2 = -1;
                int same = 0;
                if (parse_type_name(p, &t1, &s1, 1, "expected first type in __builtin_types_compatible_p") != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_COMMA, "expected ',' after first type in __builtin_types_compatible_p") != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (parse_type_name(p, &t2, &s2, 1, "expected second type in __builtin_types_compatible_p") != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_RPAREN, "expected ')' after __builtin_types_compatible_p arguments") != 0) {
                    free_expr(e);
                    return NULL;
                }
                same = parser_types_compatible(p, t1, s1, t2, s2) ? 1 : 0;
                free_expr(e);
                e = new_expr(CC_EXPR_INT);
                if (e == NULL) {
                    return NULL;
                }
                e->int_val = same;
                e->value_type = CC_TYPE_INT;
                return e;
            }
            if (strcmp(e->ident, "__builtin_choose_expr") == 0) {
                cc_expr_t *ce;
                cc_expr_t *te;
                cc_expr_t *fe;
                long cv = 0;
                if (p->tok.kind == TOK_RPAREN) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "__builtin_choose_expr expects 3 arguments");
                    free_expr(e);
                    return NULL;
                }
                ce = parse_assign(p);
                if (ce == NULL) {
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_COMMA, "expected ',' after __builtin_choose_expr condition") != 0) {
                    free_expr(ce);
                    free_expr(e);
                    return NULL;
                }
                te = parse_assign(p);
                if (te == NULL) {
                    free_expr(ce);
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_COMMA, "expected ',' after true branch in __builtin_choose_expr") != 0) {
                    free_expr(te);
                    free_expr(ce);
                    free_expr(e);
                    return NULL;
                }
                fe = parse_assign(p);
                if (fe == NULL) {
                    free_expr(te);
                    free_expr(ce);
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_RPAREN, "expected ')' after __builtin_choose_expr arguments") != 0) {
                    free_expr(fe);
                    free_expr(te);
                    free_expr(ce);
                    free_expr(e);
                    return NULL;
                }
                if (eval_const_array_bound_expr(p, ce, &cv) != 0) {
                    set_diag(p->diag, p->tok.line, p->tok.col,
                             "__builtin_choose_expr condition must be an integer constant expression");
                    free_expr(fe);
                    free_expr(te);
                    free_expr(ce);
                    free_expr(e);
                    return NULL;
                }
                free_expr(e);
                free_expr(ce);
                if (cv != 0) {
                    free_expr(fe);
                    return te;
                }
                free_expr(te);
                return fe;
            }
            if (strcmp(e->ident, "__builtin_constant_p") == 0) {
                cc_expr_t *arg;
                long cv = 0;
                int is_const = 0;
                if (p->tok.kind == TOK_RPAREN) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "__builtin_constant_p expects 1 argument");
                    free_expr(e);
                    return NULL;
                }
                arg = parse_assign(p);
                if (arg == NULL) {
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_RPAREN, "expected ')' after __builtin_constant_p argument") != 0) {
                    free_expr(arg);
                    free_expr(e);
                    return NULL;
                }
                is_const = eval_const_array_bound_expr(p, arg, &cv) == 0;
                free_expr(arg);
                free_expr(e);
                e = new_expr(CC_EXPR_INT);
                if (e == NULL) {
                    return NULL;
                }
                e->int_val = is_const ? 1 : 0;
                e->value_type = CC_TYPE_INT;
                return e;
            }
            if (strcmp(e->ident, "__builtin_va_arg") == 0) {
                cc_type_t aty = CC_TYPE_VOID;
                int asid = -1;
                cc_expr_t *ap;
                if (p->tok.kind == TOK_RPAREN) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "__builtin_va_arg expects 2 arguments");
                    free_expr(e);
                    return NULL;
                }
                ap = parse_assign(p);
                if (ap == NULL) {
                    free_expr(e);
                    return NULL;
                }
                if (push_arg(e, ap) != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_COMMA, "expected ',' after __builtin_va_arg va_list operand") != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (parse_type_name(p, &aty, &asid, 1, "expected type name in __builtin_va_arg") != 0) {
                    free_expr(e);
                    return NULL;
                }
                e->aux_type = aty;
                e->aux_struct_id = asid;
                if (expect(p, TOK_RPAREN, "expected ')' after __builtin_va_arg arguments") != 0) {
                    free_expr(e);
                    return NULL;
                }
                return e;
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
        {
            int vidx = var_find_visible_n(p, e->ident, strlen(e->ident));
            if (vidx >= 0) {
                e->value_type = p->vars[vidx].type;
                e->struct_id = p->vars[vidx].struct_id;
                e->array_ndim = p->vars[vidx].array_ndim;
                memcpy(e->array_dims, p->vars[vidx].array_dims, sizeof(e->array_dims));
            }
        }
        return e;
    }

    if (p->tok.kind == TOK_LPAREN) {
        cc_stmt_t st;
        if (next_tok(p) != 0) {
            return NULL;
        }
        if (p->tok.kind == TOK_LBRACE) {
            memset(&st, 0, sizeof(st));
            if (parse_stmt(p, &st) != 0) {
                free_stmt(&st);
                return NULL;
            }
            if (st.kind != CC_STMT_BLOCK) {
                free_stmt(&st);
                set_diag(p->diag, p->tok.line, p->tok.col, "expected block in statement expression");
                return NULL;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after statement expression") != 0) {
                free_stmt(&st);
                return NULL;
            }
            e = new_expr(CC_EXPR_STMT);
            if (e == NULL) {
                free_stmt(&st);
                return NULL;
            }
            e->stmt_expr_stmts = st.block_stmts;
            e->stmt_expr_count = st.block_count;
            st.block_stmts = NULL;
            st.block_count = 0;
            free_stmt(&st);
            return e;
        }
        e = parse_expr(p);
        if (e == NULL) {
            return NULL;
        }
        if (expect(p, TOK_RPAREN, "expected ')' after expression") != 0) {
            free_expr(e);
            return NULL;
        }
        e->paren_wrapped = 1;
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

static cc_expr_t *new_update_ident_expr(const char *name, cc_binop_t op, int postfix) {
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

static cc_expr_t *new_update_lvalue_expr(cc_expr_t *lhs, cc_binop_t op, int postfix) {
    cc_expr_t *e = new_expr(CC_EXPR_UPDATE);
    if (e == NULL) {
        free_expr(lhs);
        return NULL;
    }
    e->lhs = lhs;
    e->op = op;
    e->update_postfix = postfix;
    return e;
}

static cc_expr_t *clone_expr(const cc_expr_t *src);

static int clone_stmt(const cc_stmt_t *src, cc_stmt_t *dst) {
    size_t i;
    size_t j;
    memset(dst, 0, sizeof(*dst));
    if (src == NULL) {
        return 0;
    }
    *dst = *src;
    dst->decl_name = NULL;
    dst->label_name = NULL;
    dst->attr_section = NULL;
    dst->attr_alias = NULL;
    dst->expr = NULL;
    dst->init_stmt = NULL;
    dst->init_expr = NULL;
    dst->post_expr = NULL;
    dst->then_branch = NULL;
    dst->else_branch = NULL;
    dst->block_stmts = NULL;
    dst->block_count = 0;
    dst->asm_template = NULL;
    dst->asm_outputs = NULL;
    dst->asm_output_count = 0;
    dst->asm_inputs = NULL;
    dst->asm_input_count = 0;
    dst->asm_clobbers = NULL;
    dst->asm_clobber_count = 0;
    dst->asm_goto_labels = NULL;
    dst->asm_goto_label_count = 0;

    if (src->decl_name != NULL) {
        dst->decl_name = xstrdup_n(src->decl_name, strlen(src->decl_name));
        if (dst->decl_name == NULL) {
            return -1;
        }
    }
    if (src->label_name != NULL) {
        dst->label_name = xstrdup_n(src->label_name, strlen(src->label_name));
        if (dst->label_name == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->attr_section != NULL) {
        dst->attr_section = xstrdup_n(src->attr_section, strlen(src->attr_section));
        if (dst->attr_section == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->attr_alias != NULL) {
        dst->attr_alias = xstrdup_n(src->attr_alias, strlen(src->attr_alias));
        if (dst->attr_alias == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->expr != NULL) {
        dst->expr = clone_expr(src->expr);
        if (dst->expr == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->asm_template != NULL) {
        dst->asm_template = xstrdup_n(src->asm_template, strlen(src->asm_template));
        if (dst->asm_template == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->asm_output_count > 0) {
        dst->asm_outputs = (cc_asm_operand_t *)calloc(src->asm_output_count, sizeof(*dst->asm_outputs));
        if (dst->asm_outputs == NULL) {
            free_stmt(dst);
            return -1;
        }
        dst->asm_output_count = src->asm_output_count;
        for (j = 0; j < src->asm_output_count; ++j) {
            if (src->asm_outputs[j].name != NULL) {
                dst->asm_outputs[j].name = xstrdup_n(src->asm_outputs[j].name, strlen(src->asm_outputs[j].name));
                if (dst->asm_outputs[j].name == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
            if (src->asm_outputs[j].constraint != NULL) {
                dst->asm_outputs[j].constraint =
                    xstrdup_n(src->asm_outputs[j].constraint, strlen(src->asm_outputs[j].constraint));
                if (dst->asm_outputs[j].constraint == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
            if (src->asm_outputs[j].expr != NULL) {
                dst->asm_outputs[j].expr = clone_expr(src->asm_outputs[j].expr);
                if (dst->asm_outputs[j].expr == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
        }
    }
    if (src->asm_input_count > 0) {
        dst->asm_inputs = (cc_asm_operand_t *)calloc(src->asm_input_count, sizeof(*dst->asm_inputs));
        if (dst->asm_inputs == NULL) {
            free_stmt(dst);
            return -1;
        }
        dst->asm_input_count = src->asm_input_count;
        for (j = 0; j < src->asm_input_count; ++j) {
            if (src->asm_inputs[j].name != NULL) {
                dst->asm_inputs[j].name = xstrdup_n(src->asm_inputs[j].name, strlen(src->asm_inputs[j].name));
                if (dst->asm_inputs[j].name == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
            if (src->asm_inputs[j].constraint != NULL) {
                dst->asm_inputs[j].constraint =
                    xstrdup_n(src->asm_inputs[j].constraint, strlen(src->asm_inputs[j].constraint));
                if (dst->asm_inputs[j].constraint == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
            if (src->asm_inputs[j].expr != NULL) {
                dst->asm_inputs[j].expr = clone_expr(src->asm_inputs[j].expr);
                if (dst->asm_inputs[j].expr == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
        }
    }
    if (src->asm_clobber_count > 0) {
        dst->asm_clobbers = (char **)calloc(src->asm_clobber_count, sizeof(*dst->asm_clobbers));
        if (dst->asm_clobbers == NULL) {
            free_stmt(dst);
            return -1;
        }
        dst->asm_clobber_count = src->asm_clobber_count;
        for (j = 0; j < src->asm_clobber_count; ++j) {
            dst->asm_clobbers[j] = xstrdup_n(src->asm_clobbers[j], strlen(src->asm_clobbers[j]));
            if (dst->asm_clobbers[j] == NULL) {
                free_stmt(dst);
                return -1;
            }
        }
    }
    if (src->asm_goto_label_count > 0) {
        dst->asm_goto_labels = (char **)calloc(src->asm_goto_label_count, sizeof(*dst->asm_goto_labels));
        if (dst->asm_goto_labels == NULL) {
            free_stmt(dst);
            return -1;
        }
        dst->asm_goto_label_count = src->asm_goto_label_count;
        for (j = 0; j < src->asm_goto_label_count; ++j) {
            dst->asm_goto_labels[j] = xstrdup_n(src->asm_goto_labels[j], strlen(src->asm_goto_labels[j]));
            if (dst->asm_goto_labels[j] == NULL) {
                free_stmt(dst);
                return -1;
            }
        }
    }
    if (src->init_expr != NULL) {
        dst->init_expr = clone_expr(src->init_expr);
        if (dst->init_expr == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->post_expr != NULL) {
        dst->post_expr = clone_expr(src->post_expr);
        if (dst->post_expr == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->init_stmt != NULL) {
        dst->init_stmt = (cc_stmt_t *)calloc(1, sizeof(*dst->init_stmt));
        if (dst->init_stmt == NULL || clone_stmt(src->init_stmt, dst->init_stmt) != 0) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->then_branch != NULL) {
        dst->then_branch = (cc_stmt_t *)calloc(1, sizeof(*dst->then_branch));
        if (dst->then_branch == NULL || clone_stmt(src->then_branch, dst->then_branch) != 0) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->else_branch != NULL) {
        dst->else_branch = (cc_stmt_t *)calloc(1, sizeof(*dst->else_branch));
        if (dst->else_branch == NULL || clone_stmt(src->else_branch, dst->else_branch) != 0) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->block_count > 0) {
        dst->block_stmts = (cc_stmt_t *)calloc(src->block_count, sizeof(*dst->block_stmts));
        if (dst->block_stmts == NULL) {
            free_stmt(dst);
            return -1;
        }
        dst->block_count = src->block_count;
        for (i = 0; i < src->block_count; ++i) {
            if (clone_stmt(&src->block_stmts[i], &dst->block_stmts[i]) != 0) {
                free_stmt(dst);
                return -1;
            }
        }
    }
    return 0;
}

static cc_expr_t *clone_expr(const cc_expr_t *src) {
    cc_expr_t *dst;
    size_t i;

    if (src == NULL) {
        return NULL;
    }

    dst = new_expr(src->kind);
    if (dst == NULL) {
        return NULL;
    }

    dst->value_type = src->value_type;
    dst->struct_id = src->struct_id;
    dst->array_ndim = src->array_ndim;
    memcpy(dst->array_dims, src->array_dims, sizeof(dst->array_dims));
    dst->int_val = src->int_val;
    dst->float_val = src->float_val;
    dst->op = src->op;
    dst->member_is_arrow = src->member_is_arrow;
    dst->member_offset = src->member_offset;
    dst->member_is_bitfield = src->member_is_bitfield;
    dst->member_bit_width = src->member_bit_width;
    dst->member_bit_offset = src->member_bit_offset;
    dst->member_bit_storage_bits = src->member_bit_storage_bits;
    dst->member_bit_signed = src->member_bit_signed;
    dst->member_bit_storage_size = src->member_bit_storage_size;
    dst->update_postfix = src->update_postfix;
    dst->aux_type = src->aux_type;
    dst->aux_struct_id = src->aux_struct_id;
    dst->generic_selected = src->generic_selected;

    if (src->ident != NULL) {
        dst->ident = xstrdup_n(src->ident, strlen(src->ident));
        if (dst->ident == NULL) {
            free_expr(dst);
            return NULL;
        }
    }

    if (src->lhs != NULL) {
        dst->lhs = clone_expr(src->lhs);
        if (dst->lhs == NULL) {
            free_expr(dst);
            return NULL;
        }
    }
    if (src->rhs != NULL) {
        dst->rhs = clone_expr(src->rhs);
        if (dst->rhs == NULL) {
            free_expr(dst);
            return NULL;
        }
    }
    if (src->third != NULL) {
        dst->third = clone_expr(src->third);
        if (dst->third == NULL) {
            free_expr(dst);
            return NULL;
        }
    }

    if (src->arg_count > 0) {
        dst->args = (cc_expr_t **)calloc(src->arg_count, sizeof(*dst->args));
        if (dst->args == NULL) {
            free_expr(dst);
            return NULL;
        }
        dst->arg_count = src->arg_count;
        for (i = 0; i < src->arg_count; ++i) {
            dst->args[i] = clone_expr(src->args[i]);
            if (dst->args[i] == NULL) {
                free_expr(dst);
                return NULL;
            }
        }
    }
    if (src->generic_count > 0) {
        dst->generic_types = (cc_type_t *)calloc(src->generic_count, sizeof(*dst->generic_types));
        dst->generic_struct_ids = (int *)calloc(src->generic_count, sizeof(*dst->generic_struct_ids));
        dst->generic_is_default = (unsigned char *)calloc(src->generic_count, sizeof(*dst->generic_is_default));
        if (dst->generic_types == NULL || dst->generic_struct_ids == NULL || dst->generic_is_default == NULL) {
            free_expr(dst);
            return NULL;
        }
        memcpy(dst->generic_types, src->generic_types, src->generic_count * sizeof(*dst->generic_types));
        memcpy(dst->generic_struct_ids, src->generic_struct_ids, src->generic_count * sizeof(*dst->generic_struct_ids));
        memcpy(dst->generic_is_default, src->generic_is_default, src->generic_count * sizeof(*dst->generic_is_default));
        dst->generic_count = src->generic_count;
    }
    if (src->stmt_expr_count > 0) {
        dst->stmt_expr_stmts = (cc_stmt_t *)calloc(src->stmt_expr_count, sizeof(*dst->stmt_expr_stmts));
        if (dst->stmt_expr_stmts == NULL) {
            free_expr(dst);
            return NULL;
        }
        dst->stmt_expr_count = src->stmt_expr_count;
        for (i = 0; i < src->stmt_expr_count; ++i) {
            if (clone_stmt(&src->stmt_expr_stmts[i], &dst->stmt_expr_stmts[i]) != 0) {
                free_expr(dst);
                return NULL;
            }
        }
    }

    return dst;
}

static cc_expr_t *parse_postfix(parser_t *p) {
    cc_expr_t *e = parse_primary(p);
    while (e != NULL) {
        if (p->tok.kind == TOK_LPAREN) {
            cc_expr_t *call = new_expr(CC_EXPR_CALL);
            if (call == NULL) {
                free_expr(e);
                return NULL;
            }
            if (e->kind == CC_EXPR_IDENT && e->ident != NULL) {
                call->ident = e->ident;
                e->ident = NULL;
                free_expr(e);
            } else {
                call->lhs = e;
            }
            if (next_tok(p) != 0) {
                free_expr(call);
                return NULL;
            }
            if (p->tok.kind != TOK_RPAREN) {
                for (;;) {
                    cc_expr_t *arg = parse_assign(p);
                    if (arg == NULL) {
                        free_expr(call);
                        return NULL;
                    }
                    if (push_arg(call, arg) != 0) {
                        free_expr(call);
                        return NULL;
                    }
                    if (p->tok.kind != TOK_COMMA) {
                        break;
                    }
                    if (next_tok(p) != 0) {
                        free_expr(call);
                        return NULL;
                    }
                }
            }
            if (expect(p, TOK_RPAREN, "expected ')' after call arguments") != 0) {
                free_expr(call);
                return NULL;
            }
            e = call;
            continue;
        }

        if (p->tok.kind == TOK_LBRACK) {
            cc_expr_t *idx;
            cc_expr_t *add;
            cc_expr_t *deref;
            cc_type_t pty;
            int psid;
            cc_type_t dty;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            idx = parse_expr(p);
            if (idx == NULL) {
                free_expr(e);
                return NULL;
            }
            if (expect(p, TOK_RBRACK, "expected ']' after index expression") != 0) {
                free_expr(idx);
                free_expr(e);
                return NULL;
            }
            pty = e->value_type;
            psid = e->struct_id;
            if (!is_pointer_type(pty) && is_pointer_type(idx->value_type)) {
                pty = idx->value_type;
                psid = idx->struct_id;
            }
            add = new_bin_expr(CC_BIN_ADD, e, idx);
            if (add == NULL) {
                return NULL;
            }
            deref = new_expr(CC_EXPR_DEREF);
            if (deref == NULL) {
                free_expr(add);
                return NULL;
            }
            deref->lhs = add;
            dty = ptr_deref_type(pty);
            if (is_pointer_type(pty)) {
                deref->value_type = dty;
                deref->struct_id = type_carries_struct_id(dty) ? psid : -1;
            }
            e = deref;
            continue;
        }

        if (p->tok.kind == TOK_DOT || p->tok.kind == TOK_ARROW) {
            int is_arrow = (p->tok.kind == TOK_ARROW);
            cc_expr_t *m;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            if (p->tok.kind != TOK_IDENT) {
                set_diag(p->diag, p->tok.line, p->tok.col, "expected member name after '.' or '->'");
                free_expr(e);
                return NULL;
            }
            m = new_expr(CC_EXPR_MEMBER);
            if (m == NULL) {
                free_expr(e);
                return NULL;
            }
            m->member_is_arrow = is_arrow;
            m->lhs = e;
            m->ident = xstrdup_n(p->tok.start, p->tok.len);
            if (m->ident == NULL) {
                free_expr(m);
                return NULL;
            }
            {
                cc_type_t base_ty = m->lhs->value_type;
                int base_sid = m->lhs->struct_id;
                const cc_struct_member_t *sm = NULL;
                if (is_arrow) {
                    base_ty = ptr_deref_type(base_ty);
                    if (!type_carries_struct_id(base_ty)) {
                        base_sid = -1;
                    }
                }
                if (base_ty == CC_TYPE_VOID && base_sid >= 0) {
                    sm = struct_member_find_n(p, base_sid, m->ident, strlen(m->ident));
                    if (sm != NULL) {
                        m->value_type = sm->type;
                        m->struct_id = sm->type_struct_id;
                        m->member_offset = sm->offset;
                    }
                }
            }
            e = m;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            continue;
        }

        if (p->tok.kind == TOK_PLUS_PLUS || p->tok.kind == TOK_MINUS_MINUS) {
            cc_expr_t *upd;
            if (e->kind == CC_EXPR_IDENT && e->ident != NULL) {
                upd = new_update_ident_expr(e->ident, p->tok.kind == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 1);
                free_expr(e);
            } else if ((e->kind == CC_EXPR_DEREF && e->lhs != NULL) || e->kind == CC_EXPR_MEMBER) {
                upd = new_update_lvalue_expr(e, p->tok.kind == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 1);
                e = NULL;
            } else {
                set_diag(p->diag, p->tok.line, p->tok.col,
                         "++/-- requires an identifier, dereference, or member lvalue");
                free_expr(e);
                return NULL;
            }
            if (upd == NULL) {
                return NULL;
            }
            e = upd;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            continue;
        }

        break;
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
        if (p->tok.kind == TOK_LPAREN && is_type_name_start_after_lparen(p)) {
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            if (parse_type_name(p, &e->aux_type, &e->aux_struct_id, 1, "expected type name in sizeof") != 0) {
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
        e->aux_struct_id = -1;
        return e;
    }

    if (p->tok.kind == TOK_IDENT &&
        (tok_is_ident(p, "_Alignof") || (parser_is_c23_or_newer() && tok_is_ident(p, "alignof")) ||
         (parser_is_gnu_mode() && (tok_is_ident(p, "__alignof__") || tok_is_ident(p, "__alignof"))))) {
        cc_type_t aty = CC_TYPE_VOID;
        int asid = -1;
        long align;
        cc_expr_t *ae;
        int is_gnu_alignof = parser_is_gnu_mode() && (tok_is_ident(p, "__alignof__") || tok_is_ident(p, "__alignof"));
        if (!parser_is_c11_or_newer() && !is_gnu_alignof) {
            set_diag(p->diag, p->tok.line, p->tok.col, "_Alignof/__alignof__ requires C11 or GNU mode");
            return NULL;
        }
        if (next_tok(p) != 0) {
            return NULL;
        }
        if (expect(p, TOK_LPAREN, "expected '(' after _Alignof/alignof") != 0) {
            return NULL;
        }
        if (is_type_name_start_after_lparen(p)) {
            if (parse_type_name(p, &aty, &asid, 1, "expected type name in _Alignof/alignof") != 0) {
                return NULL;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after _Alignof/alignof type") != 0) {
                return NULL;
            }
            align = parser_type_align_bytes(p, aty, asid);
        } else {
            ae = parse_assign(p);
            if (ae == NULL) {
                return NULL;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after _Alignof/alignof expression") != 0) {
                free_expr(ae);
                return NULL;
            }
            align = parser_type_align_bytes(p, ae->value_type, ae->struct_id);
            free_expr(ae);
        }
        if (align <= 0) {
            align = g_parser_pointer_size_bytes;
        }
        return new_int_expr(align);
    }

    if (p->tok.kind == TOK_LPAREN && is_type_name_start_after_lparen(p)) {
        cc_expr_t *e = new_expr(CC_EXPR_CAST);
        if (e == NULL) {
            return NULL;
        }
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        if (parse_type_name(p, &e->aux_type, &e->aux_struct_id, 1, "expected cast type") != 0) {
            free_expr(e);
            return NULL;
        }
        if (p->tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(p))) {
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            while (is_ptr_declarator_tok(p->tok.kind)) {
                cc_type_t pty = ptr_of_type(e->aux_type);
                if (pty == CC_TYPE_VOID) {
                    set_ptr_depth_diag(p, __LINE__);
                    free_expr(e);
                    return NULL;
                }
                e->aux_type = pty;
                if (next_tok(p) != 0) {
                    free_expr(e);
                    return NULL;
                }
                while (is_decl_qual_at_token(p)) {
                    if (next_tok(p) != 0) {
                        free_expr(e);
                        return NULL;
                    }
                }
            }
            if (expect(p, TOK_RPAREN, "expected ')' in function pointer cast type") != 0) {
                free_expr(e);
                return NULL;
            }
            while (p->tok.kind == TOK_LPAREN) {
                if (skip_balanced_parens(p) != 0) {
                    free_expr(e);
                    return NULL;
                }
            }
        }
        if (p->tok.kind == TOK_LBRACK) {
            long arr_len = -1;
            int arr_ndim = 0;
            long arr_dims[CC_MAX_ARRAY_DIMS];
            memset(arr_dims, 0, sizeof(arr_dims));
            if (parse_array_suffix(p, &e->aux_type, &arr_len, &arr_ndim, arr_dims) != 0) {
                free_expr(e);
                return NULL;
            }
            if (arr_ndim > 0) {
                int ai;
                e->array_ndim = arr_ndim;
                for (ai = 0; ai < arr_ndim; ++ai) {
                    e->array_dims[ai] = arr_dims[ai];
                }
            }
        }
        if (expect(p, TOK_RPAREN, "expected ')' after cast type") != 0) {
            free_expr(e);
            return NULL;
        }
        e->value_type = e->aux_type;
        e->struct_id = e->aux_struct_id;
        if (p->tok.kind == TOK_LBRACE) {
            e->lhs = parse_initializer_expr(p);
            if (e->lhs == NULL) {
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
        return e;
    }

    if (p->tok.kind == TOK_AMP) {
        cc_expr_t *e = new_expr(CC_EXPR_ADDR);
        cc_type_t pty;
        if (e == NULL) {
            return NULL;
        }
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        e->lhs = parse_unary(p);
        if (e->lhs == NULL) {
            free_expr(e);
            return NULL;
        }
        pty = ptr_of_type(e->lhs->value_type);
        if (pty != CC_TYPE_VOID) {
            e->value_type = pty;
            e->struct_id = type_carries_struct_id(pty) ? e->lhs->struct_id : -1;
            e->array_ndim = e->lhs->array_ndim;
            memcpy(e->array_dims, e->lhs->array_dims, sizeof(e->array_dims));
        }
        return e;
    }

    if (p->tok.kind == TOK_AND_AND) {
        cc_expr_t *e = new_expr(CC_EXPR_LABEL_ADDR);
        if (e == NULL) {
            return NULL;
        }
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        if (p->tok.kind != TOK_IDENT) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected label identifier after '&&'");
            free_expr(e);
            return NULL;
        }
        e->ident = xstrdup_n(p->tok.start, p->tok.len);
        if (e->ident == NULL) {
            free_expr(e);
            return NULL;
        }
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }

    if (p->tok.kind == TOK_STAR) {
        cc_expr_t *e = new_expr(CC_EXPR_DEREF);
        cc_type_t dty;
        if (e == NULL) {
            return NULL;
        }
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        e->lhs = parse_unary(p);
        if (e->lhs == NULL) {
            free_expr(e);
            return NULL;
        }
        dty = ptr_deref_type(e->lhs->value_type);
        if (dty != CC_TYPE_VOID || e->lhs->value_type == CC_TYPE_PTR_VOID) {
            e->value_type = dty;
            e->struct_id = type_carries_struct_id(dty) ? e->lhs->struct_id : -1;
            e->array_ndim = e->lhs->array_ndim;
            memcpy(e->array_dims, e->lhs->array_dims, sizeof(e->array_dims));
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
        if (next_tok(p) != 0) {
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            return NULL;
        }
        if (rhs->kind == CC_EXPR_IDENT && rhs->ident != NULL) {
            cc_expr_t *upd = new_update_ident_expr(rhs->ident, op == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 0);
            free_expr(rhs);
            return upd;
        }
        if ((rhs->kind == CC_EXPR_DEREF && rhs->lhs != NULL) || rhs->kind == CC_EXPR_MEMBER) {
            return new_update_lvalue_expr(rhs, op == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 0);
        }
        set_diag(p->diag, p->tok.line, p->tok.col, "++/-- requires an identifier, dereference, or member lvalue");
        free_expr(rhs);
        return NULL;
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
        if (p->tok.kind == TOK_COLON) {
            if (!parser_is_gnu_mode()) {
                set_diag(p->diag, p->tok.line, p->tok.col, "omitted middle operand in ?: is a GNU extension");
                free_expr(e);
                return NULL;
            }
            e->rhs = NULL;
        } else {
            e->rhs = parse_expr(p);
            if (e->rhs == NULL) {
                free_expr(e);
                return NULL;
            }
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
        char *name = NULL;
        int lhs_is_ident = 0;
        int lhs_is_deref = 0;
        int lhs_is_member = 0;

        if (lhs->kind == CC_EXPR_IDENT && lhs->ident != NULL) {
            lhs_is_ident = 1;
        } else if (lhs->kind == CC_EXPR_DEREF) {
            lhs_is_deref = 1;
        } else if (lhs->kind == CC_EXPR_MEMBER) {
            lhs_is_member = 1;
        } else {
            set_diag(p->diag, p->tok.line, p->tok.col,
                     "left-hand side of assignment must be an identifier, dereference, or member access");
            free_expr(lhs);
            return NULL;
        }

        if (next_tok(p) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_assign(p);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }

        if (aop != TOK_ASSIGN) {
            cc_binop_t bop;
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

            if (lhs_is_ident) {
                cc_expr_t *lhs_read;
                name = lhs->ident;
                lhs->ident = NULL;
                free_expr(lhs);
                lhs = NULL;
                lhs_read = new_ident_expr(name);
                if (lhs_read == NULL) {
                    free(name);
                    free_expr(rhs);
                    return NULL;
                }
                rhs = new_bin_expr(bop, lhs_read, rhs);
                if (rhs == NULL) {
                    free(name);
                    return NULL;
                }
            } else {
                cc_expr_t *lhs_read;
                if (lhs_is_deref) {
                    cc_expr_t *ptr_expr = clone_expr(lhs->lhs);
                    if (ptr_expr == NULL) {
                        free_expr(lhs);
                        free_expr(rhs);
                        return NULL;
                    }
                    lhs_read = new_expr(CC_EXPR_DEREF);
                    if (lhs_read == NULL) {
                        free_expr(ptr_expr);
                        free_expr(lhs);
                        free_expr(rhs);
                        return NULL;
                    }
                    lhs_read->lhs = ptr_expr;
                } else {
                    lhs_read = clone_expr(lhs);
                    if (lhs_read == NULL) {
                        free_expr(lhs);
                        free_expr(rhs);
                        return NULL;
                    }
                }
                rhs = new_bin_expr(bop, lhs_read, rhs);
                if (rhs == NULL) {
                    free_expr(lhs);
                    return NULL;
                }
            }
        } else if (lhs_is_ident) {
            name = lhs->ident;
            lhs->ident = NULL;
            free_expr(lhs);
            lhs = NULL;
        }

        e = new_expr(CC_EXPR_ASSIGN);
        if (e == NULL) {
            free(name);
            free_expr(lhs);
            free_expr(rhs);
            return NULL;
        }
        e->ident = name;
        if (lhs_is_deref || lhs_is_member) {
            e->lhs = lhs;
        }
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

static int apply_auto_type_deduction(parser_t *p, cc_stmt_t *s) {
    if ((s->storage & CC_STORAGE_AUTO_TYPE) == 0) {
        return 0;
    }
    if (!parser_is_c23_or_newer() && !parser_is_gnu_mode()) {
        set_diag(p->diag, s->line, s->col, "auto type deduction requires C23 or newer");
        return -1;
    }
    if (s->expr == NULL) {
        set_diag(p->diag, s->line, s->col, "auto type deduction requires an initializer");
        return -1;
    }
    /*
     * Keep CC_STORAGE_AUTO_TYPE until semantic analysis, where initializer
     * expression types are available and stable.
     */
    return 0;
}

static int __attribute__((unused)) parse_decl_stmt(parser_t *p, cc_stmt_t *s, int need_semi) {
    int is_typedef = 0;
    int spec_saw_restrict = 0;
    int decl_saw_restrict = 0;
    int decl_storage = 0;
    int struct_id = -1;
    long alias_array_len = -1;
    int alias_array_ndim = 0;
    long alias_array_dims[CC_MAX_ARRAY_DIMS];
    long suffix_array_len = -1;
    int suffix_array_ndim = 0;
    long suffix_array_dims[CC_MAX_ARRAY_DIMS];
    decl_attrs_t decl_attrs;
    decl_attrs_t suffix_attrs;
    decl_attrs_t merged_attrs;
    memset(s, 0, sizeof(*s));
    s->line = p->tok.line;
    s->col = p->tok.col;
    s->type_struct_id = -1;
    s->kind = CC_STMT_DECL;
    memset(alias_array_dims, 0, sizeof(alias_array_dims));
    memset(suffix_array_dims, 0, sizeof(suffix_array_dims));
    decl_attrs_reset(&decl_attrs);
    decl_attrs_reset(&suffix_attrs);
    decl_attrs_reset(&merged_attrs);
    if (parse_declspec(p, &s->type, &struct_id, &alias_array_len, &alias_array_ndim, alias_array_dims, 1,
                       "expected declaration type", &is_typedef, &spec_saw_restrict, &decl_attrs) != 0) {
        return -1;
    }
    decl_storage = p->last_storage;
    s->type_struct_id = struct_id;
    s->storage = decl_storage;
    s->array_len = alias_array_len;
    s->array_ndim = alias_array_ndim;
    memcpy(s->array_dims, alias_array_dims, sizeof(s->array_dims));
    if (is_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef declaration is not allowed here");
        decl_attrs_clear(&decl_attrs);
        return -1;
    }
    if (parse_named_declarator(p, s->type, &s->type, &s->decl_name, "expected identifier after declaration type",
                               &decl_saw_restrict) !=
        0) {
        decl_attrs_clear(&decl_attrs);
        return -1;
    }
    if (parse_array_suffix(p, &s->type, &suffix_array_len, &suffix_array_ndim, suffix_array_dims) != 0) {
        decl_attrs_clear(&decl_attrs);
        return -1;
    }
    if (prepend_array_info(&s->array_len, &s->array_ndim, s->array_dims, suffix_array_len, suffix_array_ndim,
                           suffix_array_dims) != 0) {
        set_diag(p->diag, p->tok.line, p->tok.col, "array rank > 4 is not yet supported");
        decl_attrs_clear(&decl_attrs);
        return -1;
    }
    if ((spec_saw_restrict || decl_saw_restrict) && !is_pointer_type(s->type)) {
        set_diag(p->diag, p->tok.line, p->tok.col, "restrict qualifier requires a pointer type");
        decl_attrs_clear(&decl_attrs);
        return -1;
    }
    if (skip_decl_gnu_suffix(p, &suffix_attrs) != 0) {
        decl_attrs_clear(&decl_attrs);
        decl_attrs_clear(&suffix_attrs);
        return -1;
    }
    if (decl_attrs_merge(&merged_attrs, &decl_attrs) != 0 || decl_attrs_merge(&merged_attrs, &suffix_attrs) != 0) {
        decl_attrs_clear(&decl_attrs);
        decl_attrs_clear(&suffix_attrs);
        decl_attrs_clear(&merged_attrs);
        return -1;
    }
    s->attr_flags = merged_attrs.flags;
    s->attr_align = merged_attrs.align;
    if (merged_attrs.section != NULL) {
        s->attr_section = xstrdup_n(merged_attrs.section, strlen(merged_attrs.section));
        if (s->attr_section == NULL) {
            decl_attrs_clear(&decl_attrs);
            decl_attrs_clear(&suffix_attrs);
            decl_attrs_clear(&merged_attrs);
            return -1;
        }
    }
    if (merged_attrs.alias != NULL) {
        s->attr_alias = xstrdup_n(merged_attrs.alias, strlen(merged_attrs.alias));
        if (s->attr_alias == NULL) {
            decl_attrs_clear(&decl_attrs);
            decl_attrs_clear(&suffix_attrs);
            decl_attrs_clear(&merged_attrs);
            return -1;
        }
    }
    decl_attrs_clear(&decl_attrs);
    decl_attrs_clear(&suffix_attrs);
    decl_attrs_clear(&merged_attrs);
    if (p->tok.kind == TOK_ASSIGN) {
        if (next_tok(p) != 0) {
            return -1;
        }
        s->expr = parse_initializer_expr(p);
        if (s->expr == NULL) {
            return -1;
        }
    }
    if (apply_auto_type_deduction(p, s) != 0) {
        return -1;
    }
    if (need_semi) {
        return expect(p, TOK_SEMI, "expected ';' after declaration");
    }
    return 0;
}

static int parse_decl_stmt_list(parser_t *p, cc_stmt_t **arr, size_t *count, int need_semi) {
    cc_type_t base_type;
    int base_struct_id = -1;
    long base_array_len = -1;
    int base_array_ndim = 0;
    long base_array_dims[CC_MAX_ARRAY_DIMS];
    int is_typedef = 0;
    int base_saw_restrict = 0;
    int decl_storage = 0;
    decl_attrs_t base_attrs;

    decl_attrs_reset(&base_attrs);
    memset(base_array_dims, 0, sizeof(base_array_dims));
    if (parse_declspec(p, &base_type, &base_struct_id, &base_array_len, &base_array_ndim, base_array_dims, 1,
                       "expected declaration type", &is_typedef, &base_saw_restrict, &base_attrs) != 0) {
        return -1;
    }
    decl_storage = p->last_storage;
    if (p->tok.kind == TOK_SEMI) {
        if ((decl_storage & CC_STORAGE_AUTO_TYPE) != 0) {
            set_diag(p->diag, p->tok.line, p->tok.col, "auto type deduction requires an initializer");
            decl_attrs_clear(&base_attrs);
            return -1;
        }
        if (base_struct_id >= 0 && (base_attrs.flags & (CC_ATTR_PACKED | CC_ATTR_ALIGNED)) != 0) {
            if (apply_struct_attrs(p, base_struct_id, &base_attrs) != 0) {
                decl_attrs_clear(&base_attrs);
                return -1;
            }
        }
        if (is_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier after declaration type");
            decl_attrs_clear(&base_attrs);
            return -1;
        }
        if (need_semi) {
            int rc = expect(p, TOK_SEMI, "expected ';' after declaration");
            decl_attrs_clear(&base_attrs);
            return rc;
        }
        decl_attrs_clear(&base_attrs);
        return 0;
    }
    if (base_struct_id >= 0 && (base_attrs.flags & (CC_ATTR_PACKED | CC_ATTR_ALIGNED)) != 0) {
        if (apply_struct_attrs(p, base_struct_id, &base_attrs) != 0) {
            decl_attrs_clear(&base_attrs);
            return -1;
        }
    }

    for (;;) {
        cc_stmt_t s;
        int decl_saw_restrict = 0;
        int complex_fn_ptr_decl = 0;
        int is_fn_decl = 0;
        int track_var = 0;
        int prefixed_fn_ptr = 0;
        int fn_decl_hoisted = 0;
        long inner_array_len = -1;
        int inner_array_ndim = 0;
        long inner_array_dims[CC_MAX_ARRAY_DIMS];
        decl_attrs_t suffix_attrs;
        decl_attrs_t merged_attrs;
        memset(&s, 0, sizeof(s));
        s.line = p->tok.line;
        s.col = p->tok.col;
        s.kind = CC_STMT_DECL;
        s.type = base_type;
        s.type_struct_id = base_struct_id;
        s.array_len = base_array_len;
        s.array_ndim = base_array_ndim;
        memcpy(s.array_dims, base_array_dims, sizeof(s.array_dims));
        s.storage = decl_storage;
        memset(inner_array_dims, 0, sizeof(inner_array_dims));
        decl_attrs_reset(&suffix_attrs);
        decl_attrs_reset(&merged_attrs);

        if (is_ptr_declarator_tok(p->tok.kind)) {
            parser_t q = *p;
            q.diag = NULL;
            while (is_ptr_declarator_tok(q.tok.kind)) {
                if (next_tok(&q) != 0) {
                    prefixed_fn_ptr = 0;
                    break;
                }
                while (is_decl_qual_tok(q.tok.kind)) {
                    if (next_tok(&q) != 0) {
                        prefixed_fn_ptr = 0;
                        break;
                    }
                }
            }
            if (q.tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(&q))) {
                prefixed_fn_ptr = 1;
            }
        }
        if (prefixed_fn_ptr) {
            while (is_ptr_declarator_tok(p->tok.kind)) {
                s.type = ptr_of_type(s.type);
                if (s.type == CC_TYPE_VOID) {
                    set_ptr_depth_diag(p, __LINE__);
                    free_stmt(&s);
                    return -1;
                }
                if (next_tok(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
                if (consume_decl_quals(p, &decl_saw_restrict) != 0) {
                    free_stmt(&s);
                    return -1;
                }
            }
        }

        if (p->tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(p))) {
            int saw_nested_wrapped = 0;
            if (next_tok(p) != 0) {
                free_stmt(&s);
                return -1;
            }
            while (is_ptr_declarator_tok(p->tok.kind)) {
                s.type = ptr_of_type(s.type);
                if (s.type == CC_TYPE_VOID) {
                    set_ptr_depth_diag(p, __LINE__);
                    free_stmt(&s);
                    return -1;
                }
                if (next_tok(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
                if (consume_decl_quals(p, &decl_saw_restrict) != 0) {
                    free_stmt(&s);
                    return -1;
                }
            }
            if (p->tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(p))) {
                saw_nested_wrapped = 1;
                if (next_tok(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
                while (is_ptr_declarator_tok(p->tok.kind)) {
                    s.type = ptr_of_type(s.type);
                    if (s.type == CC_TYPE_VOID) {
                        set_ptr_depth_diag(p, __LINE__);
                        free_stmt(&s);
                        return -1;
                    }
                    if (next_tok(p) != 0) {
                        free_stmt(&s);
                        return -1;
                    }
                    if (consume_decl_quals(p, &decl_saw_restrict) != 0) {
                        free_stmt(&s);
                        return -1;
                    }
                }
                if (p->tok.kind != TOK_IDENT) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier after declaration type");
                    free_stmt(&s);
                    return -1;
                }
                s.decl_name = xstrdup_n(p->tok.start, p->tok.len);
                if (s.decl_name == NULL) {
                    free_stmt(&s);
                    return -1;
                }
                if (next_tok(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
                if (expect(p, TOK_RPAREN, "expected ')' in declarator") != 0) {
                    free_stmt(&s);
                    return -1;
                }
                while (p->tok.kind == TOK_LPAREN) {
                    if (skip_balanced_parens(p) != 0) {
                        free_stmt(&s);
                        return -1;
                    }
                }
            } else if (p->tok.kind != TOK_IDENT) {
                set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier after declaration type");
                free_stmt(&s);
                return -1;
            } else {
                s.decl_name = xstrdup_n(p->tok.start, p->tok.len);
                if (s.decl_name == NULL) {
                    free_stmt(&s);
                    return -1;
                }
                if (next_tok(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
            }
            if (parse_array_suffix(p, &s.type, &inner_array_len, &inner_array_ndim, inner_array_dims) != 0) {
                free_stmt(&s);
                return -1;
            }
            if (p->tok.kind == TOK_LPAREN) {
                if (skip_balanced_parens(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
            }
            if (expect(p, TOK_RPAREN, "expected ')' in declarator") != 0) {
                free_stmt(&s);
                return -1;
            }
            while (p->tok.kind == TOK_LPAREN) {
                if (skip_balanced_parens(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
                if (saw_nested_wrapped) {
                    s.type = ptr_of_type(s.type);
                    if (s.type == CC_TYPE_VOID) {
                        set_ptr_depth_diag(p, __LINE__);
                        free_stmt(&s);
                        return -1;
                    }
                }
            }
            complex_fn_ptr_decl = 1;
        } else if (parse_named_declarator(p, base_type, &s.type, &s.decl_name,
                                          "expected identifier after declaration type", &decl_saw_restrict) != 0) {
            free_stmt(&s);
            return -1;
        }
        if (complex_fn_ptr_decl) {
            long suffix_array_len = -1;
            int suffix_array_ndim = 0;
            int pointee_array_decl = 0;
            long suffix_array_dims[CC_MAX_ARRAY_DIMS];
            memset(suffix_array_dims, 0, sizeof(suffix_array_dims));
            if (parse_array_suffix(p, &s.type, &suffix_array_len, &suffix_array_ndim, suffix_array_dims) != 0) {
                free_stmt(&s);
                return -1;
            }
            if (prepend_array_info(&s.array_len, &s.array_ndim, s.array_dims, inner_array_len, inner_array_ndim,
                                   inner_array_dims) != 0 ||
                prepend_array_info(&s.array_len, &s.array_ndim, s.array_dims, suffix_array_len, suffix_array_ndim,
                                   suffix_array_dims) != 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "array rank > 4 is not yet supported");
                free_stmt(&s);
                return -1;
            }
            /*
             * Only plain pointer-to-array declarators (`(*p)[N]`) should be
             * treated as scalar pointer objects with pointee-shape metadata.
             * Declarators with inner grouped arrays (e.g. `(*p[])(...)`) are
             * true array objects and must keep array_len/array_ndim.
             */
            pointee_array_decl = (inner_array_ndim == 0 && suffix_array_ndim > 0);
            if (pointee_array_decl && s.array_ndim > 0) {
                /*
                 * Declarators of the form `(*p)[N]` carry pointee-array shape
                 * metadata for indexing/stride but are not array objects
                 * themselves.
                 */
                s.array_len = -1;
            }
            if ((base_saw_restrict || decl_saw_restrict) && !is_pointer_type(s.type)) {
                set_diag(p->diag, p->tok.line, p->tok.col, "restrict qualifier requires a pointer type");
                free_stmt(&s);
                return -1;
            }
        } else {
            long suffix_array_len = -1;
            int suffix_array_ndim = 0;
            long suffix_array_dims[CC_MAX_ARRAY_DIMS];
            memset(suffix_array_dims, 0, sizeof(suffix_array_dims));
            if (parse_array_suffix(p, &s.type, &suffix_array_len, &suffix_array_ndim, suffix_array_dims) != 0) {
                free_stmt(&s);
                return -1;
            }
            if (prepend_array_info(&s.array_len, &s.array_ndim, s.array_dims, suffix_array_len, suffix_array_ndim,
                                   suffix_array_dims) != 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "array rank > 4 is not yet supported");
                free_stmt(&s);
                return -1;
            }
            if ((base_saw_restrict || decl_saw_restrict) && !is_pointer_type(s.type)) {
                set_diag(p->diag, p->tok.line, p->tok.col, "restrict qualifier requires a pointer type");
                free_stmt(&s);
                return -1;
            }
        }
        if (skip_decl_gnu_suffix(p, &suffix_attrs) != 0) {
            free_stmt(&s);
            return -1;
        }
        if (decl_attrs_merge(&merged_attrs, &base_attrs) != 0 || decl_attrs_merge(&merged_attrs, &suffix_attrs) != 0) {
            free_stmt(&s);
            return -1;
        }
        s.attr_flags = merged_attrs.flags;
        s.attr_align = merged_attrs.align;
        if (merged_attrs.section != NULL) {
            s.attr_section = xstrdup_n(merged_attrs.section, strlen(merged_attrs.section));
            if (s.attr_section == NULL) {
                free_stmt(&s);
                return -1;
            }
        }
        if (merged_attrs.alias != NULL) {
            s.attr_alias = xstrdup_n(merged_attrs.alias, strlen(merged_attrs.alias));
            if (s.attr_alias == NULL) {
                free_stmt(&s);
                return -1;
            }
        }
        decl_attrs_clear(&suffix_attrs);
        decl_attrs_clear(&merged_attrs);
        if (!is_typedef && !complex_fn_ptr_decl && !prefixed_fn_ptr && p->tok.kind == TOK_LPAREN) {
            cc_function_t fn_decl;

            memset(&fn_decl, 0, sizeof(fn_decl));
            fn_decl.line = s.line;
            fn_decl.col = s.col;
            fn_decl.ret_type = s.type;
            fn_decl.ret_struct_id = s.type_struct_id;
            fn_decl.storage = s.storage;
            fn_decl.attr_flags = s.attr_flags;
            fn_decl.attr_align = s.attr_align;
            fn_decl.has_body = 0;
            if (next_tok(p) != 0) {
                free_stmt(&s);
                return -1;
            }
            if (p->tok.kind != TOK_RPAREN) {
                if (parse_params(p, &fn_decl) != 0) {
                    free_func(&fn_decl);
                    free_stmt(&s);
                    return -1;
                }
            }
            if (expect(p, TOK_RPAREN, "expected ')' after parameter list") != 0) {
                free_func(&fn_decl);
                free_stmt(&s);
                return -1;
            }
            if (parser_is_c23_or_newer() && fn_decl.param_count == 0 && !fn_decl.is_variadic && !fn_decl.has_prototype) {
                fn_decl.has_prototype = 1;
            }
            is_fn_decl = 1;
            fn_decl.name = s.decl_name;
            s.decl_name = NULL;
            fn_decl.attr_section = s.attr_section;
            s.attr_section = NULL;
            fn_decl.attr_alias = s.attr_alias;
            s.attr_alias = NULL;
            if (skip_decl_gnu_suffix(p, NULL) != 0) {
                free_func(&fn_decl);
                free_stmt(&s);
                return -1;
            }
            if (parser_push_hoisted_func(p, &fn_decl) != 0) {
                free_func(&fn_decl);
                free_stmt(&s);
                return -1;
            }
            fn_decl_hoisted = 1;
        }
        if (is_typedef && !complex_fn_ptr_decl && p->tok.kind == TOK_LPAREN) {
            int depth = 1;
            /*
             * Preserve the declarator base type for function typedefs.
             * Examples: `typedef int fn_t(...);` and `typedef int fn_t();`
             * should keep `int` so pointers to fn_t carry the correct
             * call result type in the simplified type model.
             */
            if (next_tok(p) != 0) {
                free_stmt(&s);
                return -1;
            }
            while (depth > 0) {
                if (p->tok.kind == TOK_EOF) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "unterminated typedef parameter list");
                    free_stmt(&s);
                    return -1;
                }
                if (p->tok.kind == TOK_LPAREN) {
                    depth++;
                } else if (p->tok.kind == TOK_RPAREN) {
                    depth--;
                }
                if (next_tok(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
            }
            if (skip_decl_gnu_suffix(p, NULL) != 0) {
                free_stmt(&s);
                return -1;
            }
        }
        if (p->tok.kind == TOK_ASSIGN) {
            if (next_tok(p) != 0) {
                free_stmt(&s);
                return -1;
            }
            s.expr = parse_initializer_expr(p);
            if (s.expr == NULL) {
                free_stmt(&s);
                return -1;
            }
        }
        if (apply_auto_type_deduction(p, &s) != 0) {
            free_stmt(&s);
            return -1;
        }
        if (s.array_ndim > 0 && s.array_dims[0] == 0 && s.expr != NULL) {
            long inferred = 0;
            if (s.expr->kind == CC_EXPR_INIT_LIST) {
                inferred = (long)s.expr->arg_count;
            } else if (s.expr->kind == CC_EXPR_STR) {
                const char *lit = s.expr->ident;
                if (lit != NULL) {
                    size_t n = strlen(lit);
                    if (n >= 2 && lit[0] == '"' && lit[n - 1] == '"') {
                        inferred = (long)(n - 1); /* payload bytes + terminating NUL */
                    }
                }
            }
            if (inferred > 0) {
                s.array_dims[0] = inferred;
                if (s.array_len <= 0) {
                    s.array_len = inferred;
                }
            }
        }
        if (is_typedef) {
            int trc;
            if (s.expr != NULL) {
                set_diag(p->diag, p->tok.line, p->tok.col, "typedef declarator cannot have an initializer");
                free_stmt(&s);
                return -1;
            }
            trc = typedef_push(p, s.decl_name, s.type, s.type_struct_id, s.array_len, s.array_ndim, s.array_dims);
            if (trc > 0) {
                int existing = typedef_find_current_scope_n(p, s.decl_name, strlen(s.decl_name));
                if (existing >= 0 && p->diag != NULL && p->diag->message[0] == '\0') {
                    p->diag->line = p->tok.line;
                    p->diag->col = p->tok.col;
                    snprintf(p->diag->message, sizeof(p->diag->message),
                             "duplicate typedef name in this scope: %s (old type=%d sid=%d new type=%d sid=%d)",
                             s.decl_name, (int)p->typedefs[existing].type, p->typedefs[existing].struct_id,
                             (int)s.type, s.type_struct_id);
                } else {
                    set_diag(p->diag, p->tok.line, p->tok.col, "duplicate typedef name in this scope");
                }
                free_stmt(&s);
                return -1;
            }
            if (trc < 0) {
                free_stmt(&s);
                return -1;
            }
            free_stmt(&s);
        } else {
            track_var = (s.decl_name != NULL && !is_fn_decl);
            if (track_var) {
                cc_type_t vty = s.type;
                if (s.array_ndim > 0 && !is_pointer_type(vty)) {
                    vty = ptr_of_type(vty);
                }
                if (var_push(p, s.decl_name, vty, s.type_struct_id, s.array_ndim, s.array_dims) != 0) {
                    free_stmt(&s);
                    return -1;
                }
            }
            if (arr == NULL || count == NULL) {
                if (s.expr != NULL) {
                    set_diag(p->diag, p->tok.line, p->tok.col,
                             "file-scope initialized object declarations are unsupported");
                    free_stmt(&s);
                    return -1;
                }
                free_stmt(&s);
            } else if (is_fn_decl) {
                if (!fn_decl_hoisted) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "internal error: failed to record function declaration");
                    free_stmt(&s);
                    return -1;
                }
                free_stmt(&s);
            } else {
                if (push_stmt_arr(arr, count, s) != 0) {
                    free_stmt(&s);
                    return -1;
                }
            }
        }

        if (p->tok.kind != TOK_COMMA) {
            break;
        }
        if (next_tok(p) != 0) {
            decl_attrs_clear(&base_attrs);
            return -1;
        }
    }

    if (need_semi) {
        int rc = expect(p, TOK_SEMI, "expected ';' after declaration");
        decl_attrs_clear(&base_attrs);
        return rc;
    }
    decl_attrs_clear(&base_attrs);
    return 0;
}

static int parse_block_stmt(parser_t *p, cc_stmt_t *s) {
    int saved_depth = p->scope_depth;
    memset(s, 0, sizeof(*s));
    s->line = p->tok.line;
    s->col = p->tok.col;
    s->kind = CC_STMT_BLOCK;
    if (expect(p, TOK_LBRACE, "expected '{'") != 0) {
        return -1;
    }
    p->scope_depth = saved_depth + 1;
    while (p->tok.kind != TOK_RBRACE) {
        cc_stmt_t child;
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unexpected end of file in block");
            typedef_pop_to_depth(p, saved_depth);
            var_pop_to_depth(p, saved_depth);
            p->scope_depth = saved_depth;
            return -1;
        }
        if (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) {
            if (parse_stmt(p, &child) != 0) {
                free_stmt(&child);
                if (diag_is_oom(p->diag)) {
                    typedef_pop_to_depth(p, saved_depth);
                    var_pop_to_depth(p, saved_depth);
                    p->scope_depth = saved_depth;
                    return -1;
                }
                diag_report_and_clear(p->diag);
                if (parser_sync_block_stmt(p) != 0) {
                    typedef_pop_to_depth(p, saved_depth);
                    var_pop_to_depth(p, saved_depth);
                    p->scope_depth = saved_depth;
                    return -1;
                }
                continue;
            }
            if (push_stmt_arr(&s->block_stmts, &s->block_count, child) != 0) {
                free_stmt(&child);
                typedef_pop_to_depth(p, saved_depth);
                var_pop_to_depth(p, saved_depth);
                p->scope_depth = saved_depth;
                return -1;
            }
            continue;
        }
        if (p->tok.kind == TOK_IDENT) {
            parser_t q = *p;
            q.diag = NULL;
            if (next_tok(&q) == 0 && q.tok.kind == TOK_COLON) {
                if (parse_stmt(p, &child) != 0) {
                    free_stmt(&child);
                    if (diag_is_oom(p->diag)) {
                        typedef_pop_to_depth(p, saved_depth);
                        var_pop_to_depth(p, saved_depth);
                        p->scope_depth = saved_depth;
                        return -1;
                    }
                    diag_report_and_clear(p->diag);
                    if (parser_sync_block_stmt(p) != 0) {
                        typedef_pop_to_depth(p, saved_depth);
                        var_pop_to_depth(p, saved_depth);
                        p->scope_depth = saved_depth;
                        return -1;
                    }
                    continue;
                }
                if (push_stmt_arr(&s->block_stmts, &s->block_count, child) != 0) {
                    free_stmt(&child);
                    typedef_pop_to_depth(p, saved_depth);
                    var_pop_to_depth(p, saved_depth);
                    p->scope_depth = saved_depth;
                    return -1;
                }
                continue;
            }
        }
        if (parse_stmt(p, &child) != 0) {
            free_stmt(&child);
            if (diag_is_oom(p->diag)) {
                typedef_pop_to_depth(p, saved_depth);
                var_pop_to_depth(p, saved_depth);
                p->scope_depth = saved_depth;
                return -1;
            }
            diag_report_and_clear(p->diag);
            if (parser_sync_block_stmt(p) != 0) {
                typedef_pop_to_depth(p, saved_depth);
                var_pop_to_depth(p, saved_depth);
                p->scope_depth = saved_depth;
                return -1;
            }
            continue;
        }
        if (push_stmt_arr(&s->block_stmts, &s->block_count, child) != 0) {
            free_stmt(&child);
            typedef_pop_to_depth(p, saved_depth);
            var_pop_to_depth(p, saved_depth);
            p->scope_depth = saved_depth;
            return -1;
        }
    }
    if (expect(p, TOK_RBRACE, "expected '}' after block") != 0) {
        typedef_pop_to_depth(p, saved_depth);
        var_pop_to_depth(p, saved_depth);
        p->scope_depth = saved_depth;
        return -1;
    }
    typedef_pop_to_depth(p, saved_depth);
    var_pop_to_depth(p, saved_depth);
    p->scope_depth = saved_depth;
    return 0;
}

static int parse_switch_body_stmt(parser_t *p, cc_stmt_t *s) {
    memset(s, 0, sizeof(*s));
    s->line = p->tok.line;
    s->col = p->tok.col;
    s->kind = CC_STMT_BLOCK;
    if (p->tok.kind == TOK_LBRACE) {
        return parse_block_stmt(p, s);
    }
    while (p->tok.kind == TOK_KW_CASE || p->tok.kind == TOK_KW_DEFAULT) {
        cc_stmt_t label_stmt;
        if (parse_stmt(p, &label_stmt) != 0) {
            free_stmt(&label_stmt);
            return -1;
        }
        if (push_stmt_arr(&s->block_stmts, &s->block_count, label_stmt) != 0) {
            free_stmt(&label_stmt);
            return -1;
        }
    }
    {
        cc_stmt_t tail_stmt;
        if (parse_stmt(p, &tail_stmt) != 0) {
            free_stmt(&tail_stmt);
            return -1;
        }
        if (push_stmt_arr(&s->block_stmts, &s->block_count, tail_stmt) != 0) {
            free_stmt(&tail_stmt);
            return -1;
        }
    }
    return 0;
}

static int parse_stmt(parser_t *p, cc_stmt_t *s) {
    parser_t saved;
    memset(s, 0, sizeof(*s));
    s->line = p->tok.line;
    s->col = p->tok.col;
    while (p->tok.kind == TOK_KW_EXTENSION) {
        if (next_tok(p) != 0) {
            return -1;
        }
    }

    if (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) {
        int stmt_flags = 0;
        saved = *p;
        if (parse_c23_attribute_seq(p, NULL, &stmt_flags) != 0) {
            return -1;
        }
        if ((stmt_flags & CC_ATTR_FALLTHROUGH) != 0 && p->tok.kind == TOK_SEMI) {
            s->attr_flags |= stmt_flags;
            s->kind = CC_STMT_EXPR;
            s->expr = NULL;
            return next_tok(p);
        }
        *p = saved;
    }

    if (tok_is_gnu_attribute_kw(p)) {
        decl_attrs_t stmt_attrs;
        int rc;
        decl_attrs_reset(&stmt_attrs);
        rc = parse_gnu_attribute_spec(p, &stmt_attrs);
        if (rc != 0) {
            decl_attrs_clear(&stmt_attrs);
            return -1;
        }
        rc = parse_stmt(p, s);
        if (rc != 0) {
            decl_attrs_clear(&stmt_attrs);
            return -1;
        }
        s->attr_flags |= stmt_attrs.flags;
        decl_attrs_clear(&stmt_attrs);
        return 0;
    }

    if (is_static_assert_tok(p)) {
        s->kind = CC_STMT_EXPR;
        s->expr = NULL;
        return parse_static_assert_decl(p, 1);
    }

    if (tok_is_ident(p, "__label__")) {
        if (!parser_is_gnu_mode()) {
            set_diag(p->diag, p->tok.line, p->tok.col, "__label__ is a GNU extension");
            return -1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        if (p->tok.kind != TOK_IDENT) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier after __label__");
            return -1;
        }
        for (;;) {
            if (next_tok(p) != 0) {
                return -1;
            }
            if (p->tok.kind != TOK_COMMA) {
                break;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            if (p->tok.kind != TOK_IDENT) {
                set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier in __label__ declaration");
                return -1;
            }
        }
        s->kind = CC_STMT_EXPR;
        s->expr = NULL;
        return expect(p, TOK_SEMI, "expected ';' after __label__ declaration");
    }

    if (p->tok.kind == TOK_SEMI) {
        s->kind = CC_STMT_EXPR;
        s->expr = NULL;
        return next_tok(p);
    }

    if (p->tok.kind == TOK_LBRACE) {
        return parse_block_stmt(p, s);
    }

    if (p->tok.kind == TOK_IDENT && peek_kind(p) == TOK_COLON) {
        decl_attrs_t label_attrs;
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
        decl_attrs_reset(&label_attrs);
        if (skip_decl_gnu_suffix(p, &label_attrs) != 0) {
            decl_attrs_clear(&label_attrs);
            return -1;
        }
        s->attr_flags |= label_attrs.flags;
        decl_attrs_clear(&label_attrs);
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
            if (is_declspec_start(p)) {
                s->init_stmt = (cc_stmt_t *)calloc(1, sizeof(*s->init_stmt));
                if (s->init_stmt == NULL) {
                    return -1;
                }
                memset(s->init_stmt, 0, sizeof(*s->init_stmt));
                s->init_stmt->line = p->tok.line;
                s->init_stmt->col = p->tok.col;
                s->init_stmt->kind = CC_STMT_BLOCK;
                if (parse_decl_stmt_list(p, &s->init_stmt->block_stmts, &s->init_stmt->block_count, 1) != 0) {
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
        if (parse_switch_body_stmt(p, s->then_branch) != 0) {
            return -1;
        }
        return 0;
    }

    if (p->tok.kind == TOK_KW_CASE) {
        cc_expr_t *hi_expr;
        long hi = 0;
        s->kind = CC_STMT_CASE;
        if (next_tok(p) != 0) {
            return -1;
        }
        s->expr = parse_expr(p);
        if (s->expr == NULL) {
            return -1;
        }
        if (p->tok.kind == TOK_ELLIPSIS) {
            if (!parser_is_gnu_mode()) {
                set_diag(p->diag, p->tok.line, p->tok.col, "case ranges are a GNU extension");
                return -1;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            hi_expr = parse_expr(p);
            if (hi_expr == NULL) {
                return -1;
            }
            if (eval_const_array_bound_expr(p, hi_expr, &hi) != 0) {
                free_expr(hi_expr);
                set_diag(p->diag, p->tok.line, p->tok.col, "case range upper bound must be an integer constant");
                return -1;
            }
            free_expr(hi_expr);
            s->case_has_range = 1;
            s->case_hi = hi;
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
        if (p->tok.kind == TOK_STAR) {
            if (next_tok(p) != 0) {
                return -1;
            }
            s->expr = parse_expr(p);
            if (s->expr == NULL) {
                return -1;
            }
            return expect(p, TOK_SEMI, "expected ';' after computed goto");
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

    if (tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm") || tok_is_ident(p, "asm")) {
        return parse_asm_stmt(p, s);
    }

    if (is_declspec_start(p)) {
        cc_stmt_t *decls = NULL;
        size_t decl_count = 0;
        size_t stmt_line = p->tok.line;
        size_t stmt_col = p->tok.col;
        if (parse_decl_stmt_list(p, &decls, &decl_count, 1) != 0) {
            free(decls);
            return -1;
        }
        if (decl_count == 1) {
            *s = decls[0];
            free(decls);
            return 0;
        }
        memset(s, 0, sizeof(*s));
        s->line = stmt_line;
        s->col = stmt_col;
        s->kind = CC_STMT_BLOCK;
        s->is_synthetic_block = 1;
        s->block_stmts = decls;
        s->block_count = decl_count;
        return 0;
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
    free(f->attr_section);
    free(f->attr_alias);
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
    for (i = 0; i < tu->global_count; ++i) {
        free(tu->globals[i].name);
        free(tu->globals[i].attr_section);
        free(tu->globals[i].attr_alias);
        free_expr(tu->globals[i].init);
    }
    free(tu->globals);
    tu->globals = NULL;
    tu->global_count = 0;
    for (i = 0; i < tu->struct_count; ++i) {
        size_t j;
        free(tu->structs[i].tag);
        for (j = 0; j < tu->structs[i].member_count; ++j) {
            free(tu->structs[i].members[j].name);
        }
        free(tu->structs[i].members);
    }
    free(tu->structs);
    tu->structs = NULL;
    tu->struct_count = 0;
}

static int parse_params(parser_t *p, cc_function_t *f) {
        if (g_parser_allow_oldstyle_funcdecl && p->tok.kind == TOK_IDENT &&
        typedef_find_visible_n(p, p->tok.start, p->tok.len) < 0) {
        cc_tok_kind_t k = peek_kind(p);
        if (k == TOK_COMMA || k == TOK_RPAREN) {
            for (;;) {
                if (p->tok.kind != TOK_IDENT) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "expected old-style parameter name");
                    return -1;
                }
                if (push_param(f, CC_TYPE_INT, p->tok.start, p->tok.len, 0) != 0) {
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
            }
            f->has_prototype = 0;
            return 0;
        }
    }

    if (p->tok.kind == TOK_KW_VOID && peek_kind(p) == TOK_RPAREN) {
        if (next_tok(p) != 0) {
            return -1;
        }
        f->has_prototype = 1;
        return 0;
    }

    while (p->tok.kind != TOK_RPAREN) {
        cc_type_t ptype;
        int ptype_sid = -1;
        cc_type_t dty;
        char *pname = NULL;
        int param_storage = 0;
        int ptype_restrict = 0;
        int dty_restrict = 0;
        char anon_buf[32];

        if (p->tok.kind == TOK_ELLIPSIS) {
            f->is_variadic = 1;
            f->has_prototype = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            break;
        }

        if (parse_declspec(p, &ptype, &ptype_sid, NULL, NULL, NULL, 1, "expected parameter type", NULL,
                           &ptype_restrict, NULL) != 0) {
            return -1;
        }
        param_storage = p->last_storage;
        if (parse_param_declarator(p, ptype, &dty, &pname, &dty_restrict) != 0) {
            return -1;
        }
        if ((ptype_restrict || dty_restrict) && !is_pointer_type(dty)) {
            set_diag(p->diag, p->tok.line, p->tok.col, "restrict qualifier requires a pointer parameter type");
            free(pname);
            return -1;
        }
        if (skip_decl_gnu_suffix(p, NULL) != 0) {
            free(pname);
            return -1;
        }
        if (pname == NULL) {
            snprintf(anon_buf, sizeof(anon_buf), "__anon_param_%zu", f->param_count);
            if (push_param(f, dty, anon_buf, strlen(anon_buf), param_storage) != 0) {
                return -1;
            }
            if (f->param_count > 0) {
                f->params[f->param_count - 1].type_struct_id = ptype_sid;
            }
        } else {
            if (push_param(f, dty, pname, strlen(pname), param_storage) != 0) {
                free(pname);
                return -1;
            }
            if (f->param_count > 0) {
                f->params[f->param_count - 1].type_struct_id = ptype_sid;
            }
            free(pname);
        }
        f->has_prototype = 1;

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

static int function_param_index_by_name(const cc_function_t *f, const char *name) {
    size_t i;
    if (f == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < f->param_count; ++i) {
        if (f->params[i].name != NULL && strcmp(f->params[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static cc_type_t adjust_oldstyle_param_type(cc_type_t ty, int array_ndim) {
    if (array_ndim > 0 && !is_pointer_type(ty)) {
        cc_type_t pty = ptr_of_type(ty);
        if (pty != CC_TYPE_VOID) {
            return pty;
        }
        return CC_TYPE_PTR_VOID;
    }
    return ty;
}

static int parse_function(parser_t *p, cc_function_t *f) {
    cc_type_t ftype;
    int ftype_sid = -1;
    int is_typedef = 0;
    int fn_storage = 0;
    int wrapped_fn_ptr_decl = 0;
    int saved_depth = p->scope_depth;
    cc_stmt_t *oldstyle_decls = NULL;
    size_t oldstyle_decl_count = 0;
    size_t oldstyle_i;
    decl_attrs_t spec_attrs;
    decl_attrs_t suffix_attrs;
    decl_attrs_t merged_attrs;
    memset(f, 0, sizeof(*f));
    f->line = p->tok.line;
    f->col = p->tok.col;
    f->has_body = 0;
    f->has_prototype = 0;
    decl_attrs_reset(&spec_attrs);
    decl_attrs_reset(&suffix_attrs);
    decl_attrs_reset(&merged_attrs);

    if (parse_declspec(p, &ftype, &ftype_sid, NULL, NULL, NULL, 1, "expected function return type", &is_typedef,
                       NULL, &spec_attrs) != 0) {
        return -1;
    }
    fn_storage = p->last_storage;
    if (is_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef is not valid in function definition");
        decl_attrs_clear(&spec_attrs);
        return -1;
    }
    if (p->tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(p))) {
        wrapped_fn_ptr_decl = 1;
        f->ret_type = ftype;
        if (next_tok(p) != 0) {
            decl_attrs_clear(&spec_attrs);
            return -1;
        }
        while (is_ptr_declarator_tok(p->tok.kind)) {
            f->ret_type = ptr_of_type(f->ret_type);
            if (f->ret_type == CC_TYPE_VOID) {
                set_ptr_depth_diag(p, __LINE__);
                decl_attrs_clear(&spec_attrs);
                return -1;
            }
            if (next_tok(p) != 0) {
                decl_attrs_clear(&spec_attrs);
                return -1;
            }
            while (is_decl_qual_at_token(p)) {
                if (next_tok(p) != 0) {
                    decl_attrs_clear(&spec_attrs);
                    return -1;
                }
            }
        }
        if (p->tok.kind != TOK_IDENT) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected function name");
            decl_attrs_clear(&spec_attrs);
            return -1;
        }
        f->name = xstrdup_n(p->tok.start, p->tok.len);
        if (f->name == NULL) {
            decl_attrs_clear(&spec_attrs);
            return -1;
        }
        if (next_tok(p) != 0) {
            decl_attrs_clear(&spec_attrs);
            return -1;
        }
    } else if (parse_named_declarator(p, ftype, &f->ret_type, &f->name, "expected function name", NULL) != 0) {
        decl_attrs_clear(&spec_attrs);
        return -1;
    }
    f->ret_struct_id = ftype_sid;
    f->storage = fn_storage;

    if (expect(p, TOK_LPAREN, "expected '(' after function name") != 0) {
        decl_attrs_clear(&spec_attrs);
        return -1;
    }
    if (p->tok.kind != TOK_RPAREN) {
        if (parse_params(p, f) != 0) {
            decl_attrs_clear(&spec_attrs);
            return -1;
        }
    }
    if (expect(p, TOK_RPAREN, "expected ')' after parameter list") != 0) {
        decl_attrs_clear(&spec_attrs);
        return -1;
    }
    if (parser_is_c23_or_newer() && f->param_count == 0 && !f->is_variadic && !f->has_prototype) {
        /*
         * In C23 mode, an empty parameter list is a prototype for a
         * parameterless function, not an old-style declaration.
         */
        f->has_prototype = 1;
    }
    if (wrapped_fn_ptr_decl) {
        if (expect(p, TOK_RPAREN, "expected ')' in function declarator") != 0) {
            decl_attrs_clear(&spec_attrs);
            return -1;
        }
        while (p->tok.kind == TOK_LPAREN) {
            if (skip_balanced_parens(p) != 0) {
                decl_attrs_clear(&spec_attrs);
                return -1;
            }
        }
    }
    if (skip_decl_gnu_suffix(p, &suffix_attrs) != 0) {
        decl_attrs_clear(&spec_attrs);
        decl_attrs_clear(&suffix_attrs);
        return -1;
    }
    if (decl_attrs_merge(&merged_attrs, &spec_attrs) != 0 || decl_attrs_merge(&merged_attrs, &suffix_attrs) != 0) {
        decl_attrs_clear(&spec_attrs);
        decl_attrs_clear(&suffix_attrs);
        decl_attrs_clear(&merged_attrs);
        return -1;
    }
    f->attr_flags = merged_attrs.flags;
    f->attr_align = merged_attrs.align;
    if (merged_attrs.section != NULL) {
        f->attr_section = xstrdup_n(merged_attrs.section, strlen(merged_attrs.section));
        if (f->attr_section == NULL) {
            decl_attrs_clear(&spec_attrs);
            decl_attrs_clear(&suffix_attrs);
            decl_attrs_clear(&merged_attrs);
            return -1;
        }
    }
    if (merged_attrs.alias != NULL) {
        f->attr_alias = xstrdup_n(merged_attrs.alias, strlen(merged_attrs.alias));
        if (f->attr_alias == NULL) {
            decl_attrs_clear(&spec_attrs);
            decl_attrs_clear(&suffix_attrs);
            decl_attrs_clear(&merged_attrs);
            return -1;
        }
    }
    decl_attrs_clear(&spec_attrs);
    decl_attrs_clear(&suffix_attrs);
    decl_attrs_clear(&merged_attrs);
    if (p->tok.kind == TOK_SEMI) {
        if (next_tok(p) != 0) {
            return -1;
        }
        f->has_body = 0;
        return 0;
    }
    if (g_parser_allow_oldstyle_funcdecl && !f->has_prototype && p->tok.kind != TOK_LBRACE && p->tok.kind != TOK_EOF &&
        is_declspec_start(p)) {
        while (p->tok.kind != TOK_LBRACE) {
            if (p->tok.kind == TOK_EOF) {
                set_diag(p->diag, p->tok.line, p->tok.col, "unexpected end of file in old-style function declaration");
                return -1;
            }
            if (!is_declspec_start(p)) {
                set_diag(p->diag, p->tok.line, p->tok.col, "expected declaration in old-style function definition");
                return -1;
            }
            if (parse_decl_stmt_list(p, &oldstyle_decls, &oldstyle_decl_count, 1) != 0) {
                return -1;
            }
        }
    }
    for (oldstyle_i = 0; oldstyle_i < oldstyle_decl_count; ++oldstyle_i) {
        cc_stmt_t *ds = &oldstyle_decls[oldstyle_i];
        int pidx;
        if (ds->decl_name == NULL) {
            continue;
        }
        if (ds->expr != NULL) {
            set_diag(p->diag, ds->line, ds->col, "old-style parameter declaration cannot have an initializer");
            return -1;
        }
        pidx = function_param_index_by_name(f, ds->decl_name);
        if (pidx < 0) {
            set_diag(p->diag, ds->line, ds->col, "declaration for non-parameter in old-style function definition");
            return -1;
        }
        f->params[pidx].type = adjust_oldstyle_param_type(ds->type, ds->array_ndim);
        f->params[pidx].type_struct_id = ds->type_struct_id;
    }
    for (oldstyle_i = 0; oldstyle_i < oldstyle_decl_count; ++oldstyle_i) {
        free_stmt(&oldstyle_decls[oldstyle_i]);
    }
    free(oldstyle_decls);
    oldstyle_decls = NULL;
    oldstyle_decl_count = 0;
    if (p->tok.kind != TOK_LBRACE) {
        while (p->tok.kind != TOK_SEMI && p->tok.kind != TOK_EOF) {
            if (next_tok(p) != 0) {
                return -1;
            }
        }
        if (expect(p, TOK_SEMI, "expected ';' after function declaration") != 0) {
            return -1;
        }
        f->has_body = 0;
        return 0;
    }

    {
        size_t i;
        for (i = 0; i < f->param_count; ++i) {
            if (f->params[i].type == CC_TYPE_VOID && f->params[i].type_struct_id < 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "void is not a valid named parameter type");
                return -1;
            }
        }
    }
    if (expect(p, TOK_LBRACE, "expected '{' before function body") != 0) {
        return -1;
    }
    f->has_body = 1;
    p->scope_depth = saved_depth + 1;
    {
        size_t i;
        for (i = 0; i < f->param_count; ++i) {
            if (f->params[i].name != NULL &&
                var_push(p, f->params[i].name, f->params[i].type, f->params[i].type_struct_id, 0, NULL) != 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "out of memory tracking function parameters");
                return -1;
            }
        }
    }

    while (p->tok.kind != TOK_RBRACE) {
        cc_stmt_t s;
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unexpected end of file in function body");
            return -1;
        }
        if (parser_is_gnu_mode() && is_declspec_start(p) && probe_is_function_head(p)) {
            cc_function_t nested;
            if (parse_function(p, &nested) != 0) {
                if (diag_is_oom(p->diag)) {
                    return -1;
                }
                diag_report_and_clear(p->diag);
                if (parser_sync_block_stmt(p) != 0) {
                    return -1;
                }
                continue;
            }
            nested.storage |= CC_STORAGE_STATIC;
            if (parser_push_hoisted_func(p, &nested) != 0) {
                free_func(&nested);
                set_diag(p->diag, p->tok.line, p->tok.col, "out of memory adding nested function");
                return -1;
            }
            continue;
        }
        if (is_declspec_start(p)) {
            int is_label_head = 0;
            if (p->tok.kind == TOK_IDENT) {
                parser_t q = *p;
                q.diag = NULL;
                if (next_tok(&q) == 0 && q.tok.kind == TOK_COLON) {
                    is_label_head = 1;
                }
            }
            if (!is_label_head) {
                if (parse_decl_stmt_list(p, &f->stmts, &f->stmt_count, 1) != 0) {
                    if (diag_is_oom(p->diag)) {
                        return -1;
                    }
                    diag_report_and_clear(p->diag);
                    if (parser_sync_block_stmt(p) != 0) {
                        return -1;
                    }
                    continue;
                }
                continue;
            }
        }
        if (parse_stmt(p, &s) != 0) {
            free_stmt(&s);
            if (diag_is_oom(p->diag)) {
                return -1;
            }
            diag_report_and_clear(p->diag);
            if (parser_sync_block_stmt(p) != 0) {
                return -1;
            }
            continue;
        }
        if (push_stmt_func(f, s) != 0) {
            free_stmt(&s);
            return -1;
        }
    }

    if (expect(p, TOK_RBRACE, "expected '}' after function body") != 0) {
        return -1;
    }
    typedef_pop_to_depth(p, saved_depth);
    var_pop_to_depth(p, saved_depth);
    struct_hide_to_depth(p, saved_depth);
    p->scope_depth = saved_depth;
    return 0;
}

static int probe_is_function_head(parser_t *p) {
    parser_t q = *p;
    parser_t probe;
    cc_type_t ty;
    int ty_sid = -1;
    int is_typedef = 0;
    int prefixed_fn_ptr = 0;
    char *name = NULL;
    int rc = 0;
    const char *lf = p->lx.logical_file;

    q.diag = NULL;
    q.lx.logical_file = lf != NULL ? xstrdup_n(lf, strlen(lf)) : NULL;
    if (lf != NULL && q.lx.logical_file == NULL) {
        return 0;
    }
    q.structs = NULL;
    q.struct_count = 0;
    q.struct_cap = 0;
    q.enum_consts = NULL;
    q.enum_const_count = 0;
    q.enum_const_cap = 0;
    q.enum_tags = NULL;
    q.enum_tag_count = 0;
    q.enum_tag_cap = 0;
    if (parse_declspec(&q, &ty, &ty_sid, NULL, NULL, NULL, 1, "", &is_typedef, NULL, NULL) != 0) {
        goto done;
    }
    if (is_typedef) {
        goto done;
    }
    if (is_ptr_declarator_tok(q.tok.kind)) {
        probe = q;
        probe.diag = NULL;
        while (is_ptr_declarator_tok(probe.tok.kind)) {
            if (next_tok(&probe) != 0) {
                prefixed_fn_ptr = 0;
                break;
            }
            while (is_decl_qual_at_token(&probe)) {
                if (next_tok(&probe) != 0) {
                    prefixed_fn_ptr = 0;
                    break;
                }
            }
        }
        if (probe.tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(&probe))) {
            prefixed_fn_ptr = 1;
        }
    }
    if (q.tok.kind == TOK_LPAREN && is_ptr_declarator_tok(peek_kind(&q))) {
        if (next_tok(&q) != 0) {
            goto done;
        }
        while (is_ptr_declarator_tok(q.tok.kind)) {
            ty = ptr_of_type(ty);
            if (ty == CC_TYPE_VOID) {
                goto done;
            }
            if (next_tok(&q) != 0) {
                goto done;
            }
            while (is_decl_qual_at_token(&q)) {
                if (next_tok(&q) != 0) {
                    goto done;
                }
            }
        }
        if (q.tok.kind != TOK_IDENT) {
            goto done;
        }
        name = xstrdup_n(q.tok.start, q.tok.len);
        if (name == NULL) {
            goto done;
        }
        if (next_tok(&q) != 0) {
            goto done;
        }
        rc = (q.tok.kind == TOK_LPAREN);
    } else {
        if (parse_named_declarator(&q, ty, &ty, &name, "", NULL) != 0) {
            goto done;
        }
        if (prefixed_fn_ptr) {
            rc = 0;
        } else {
            rc = (q.tok.kind == TOK_LPAREN);
        }
    }
done:
    free(name);
    parser_free_enum_consts(&q);
    parser_free_enum_tags(&q);
    parser_free_structs(&q);
    cc_lexer_deinit(&q.lx);
    return rc;
}

int cc_parse_file(const char *path, cc_translation_unit_t *out, cc_diag_t *diag) {
    FILE *fp;
    long sz;
    char *buf;
    parser_t p;

    memset(out, 0, sizeof(*out));
    if (diag != NULL) {
        diag->path[0] = '\0';
        diag->line = 0;
        diag->col = 0;
        diag->error_count = 0;
        diag->message[0] = '\0';
    }
    g_parser_diag_file = path;

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
    if (g_parser_enable_trigraphs) {
        sz = (long)normalize_c95_trigraphs(buf, (size_t)sz);
        buf[sz] = '\0';
    }

    memset(&p, 0, sizeof(p));
    p.diag = diag;
    p.tu = out;
    p.scope_depth = 0;
    cc_lexer_init(&p.lx, buf, (size_t)sz, path);
    if (next_tok(&p) != 0) {
        cc_lexer_deinit(&p.lx);
        parser_free_hoisted_funcs(&p);
        parser_free_typedefs(&p);
        parser_free_vars(&p);
        parser_free_enum_consts(&p);
        parser_free_enum_tags(&p);
        parser_free_structs(&p);
        free(buf);
        cc_tu_free(out);
        return -1;
    }
    if (parser_is_c23_or_newer() &&
        typedef_push(&p, "nullptr_t", CC_TYPE_PTR_VOID, -1, -1, 0, NULL) != 0) {
        cc_lexer_deinit(&p.lx);
        parser_free_hoisted_funcs(&p);
        parser_free_typedefs(&p);
        parser_free_vars(&p);
        parser_free_enum_consts(&p);
        parser_free_enum_tags(&p);
        parser_free_structs(&p);
        free(buf);
        cc_tu_free(out);
        return -1;
    }
    while (p.tok.kind != TOK_EOF) {
        if (p.tok.kind == TOK_SEMI) {
            if (next_tok(&p) != 0) {
                cc_lexer_deinit(&p.lx);
                parser_free_hoisted_funcs(&p);
                parser_free_typedefs(&p);
                parser_free_vars(&p);
                parser_free_enum_consts(&p);
                parser_free_enum_tags(&p);
                parser_free_structs(&p);
                free(buf);
                cc_tu_free(out);
                return -1;
            }
            continue;
        }
        if (p.tok.kind == TOK_KW_EXTENSION) {
            if (next_tok(&p) != 0) {
                cc_lexer_deinit(&p.lx);
                parser_free_hoisted_funcs(&p);
                parser_free_typedefs(&p);
                parser_free_vars(&p);
                parser_free_enum_consts(&p);
                parser_free_enum_tags(&p);
                parser_free_structs(&p);
                free(buf);
                cc_tu_free(out);
                return -1;
            }
            continue;
        }
        if (is_static_assert_tok(&p)) {
            if (parse_static_assert_decl(&p, 1) != 0) {
                if (diag_is_oom(diag)) {
                    cc_lexer_deinit(&p.lx);
                    parser_free_hoisted_funcs(&p);
                    parser_free_typedefs(&p);
                    parser_free_vars(&p);
                    parser_free_enum_consts(&p);
                    parser_free_enum_tags(&p);
                    parser_free_structs(&p);
                    free(buf);
                    cc_tu_free(out);
                    return -1;
                }
                diag_report_and_clear(diag);
                if (parser_sync_toplevel(&p) != 0) {
                    cc_lexer_deinit(&p.lx);
                    parser_free_hoisted_funcs(&p);
                    parser_free_typedefs(&p);
                    parser_free_vars(&p);
                    parser_free_enum_consts(&p);
                    parser_free_enum_tags(&p);
                    parser_free_structs(&p);
                    free(buf);
                    cc_tu_free(out);
                    return -1;
                }
                continue;
            }
            continue;
        }
        if (tok_is_ident(&p, "asm") || tok_is_ident(&p, "__asm__") || tok_is_ident(&p, "__asm")) {
            cc_stmt_t asm_stmt;
            memset(&asm_stmt, 0, sizeof(asm_stmt));
            if (parse_asm_stmt(&p, &asm_stmt) != 0) {
                free_stmt(&asm_stmt);
                if (diag_is_oom(diag)) {
                    cc_lexer_deinit(&p.lx);
                    parser_free_hoisted_funcs(&p);
                    parser_free_typedefs(&p);
                    parser_free_vars(&p);
                    parser_free_enum_consts(&p);
                    parser_free_enum_tags(&p);
                    parser_free_structs(&p);
                    free(buf);
                    cc_tu_free(out);
                    return -1;
                }
                diag_report_and_clear(diag);
                if (parser_sync_toplevel(&p) != 0) {
                    cc_lexer_deinit(&p.lx);
                    parser_free_hoisted_funcs(&p);
                    parser_free_typedefs(&p);
                    parser_free_vars(&p);
                    parser_free_enum_consts(&p);
                    parser_free_enum_tags(&p);
                    parser_free_structs(&p);
                    free(buf);
                    cc_tu_free(out);
                    return -1;
                }
                continue;
            }
            free_stmt(&asm_stmt);
            continue;
        }
        if (is_declspec_start(&p) && !probe_is_function_head(&p)) {
            cc_stmt_t *decls = NULL;
            size_t decl_count = 0;
            size_t di;
            if (parse_decl_stmt_list(&p, &decls, &decl_count, 1) != 0) {
                if (diag_is_oom(diag)) {
                    cc_lexer_deinit(&p.lx);
                    parser_free_hoisted_funcs(&p);
                    parser_free_typedefs(&p);
                    parser_free_vars(&p);
                    parser_free_enum_consts(&p);
                    parser_free_enum_tags(&p);
                    parser_free_structs(&p);
                    free(buf);
                    cc_tu_free(out);
                    return -1;
                }
                diag_report_and_clear(diag);
                if (parser_sync_toplevel(&p) != 0) {
                    cc_lexer_deinit(&p.lx);
                    parser_free_hoisted_funcs(&p);
                    parser_free_typedefs(&p);
                    parser_free_vars(&p);
                    parser_free_enum_consts(&p);
                    parser_free_enum_tags(&p);
                    parser_free_structs(&p);
                    free(buf);
                    cc_tu_free(out);
                    return -1;
                }
                continue;
            }
            for (di = 0; di < decl_count; ++di) {
                cc_global_t g;
                memset(&g, 0, sizeof(g));
                if (decls[di].kind != CC_STMT_DECL || decls[di].decl_name == NULL) {
                    set_diag(diag, p.tok.line, p.tok.col, "malformed file-scope declaration");
                    while (di < decl_count) {
                        free_stmt(&decls[di]);
                        di++;
                    }
                    free(decls);
                    cc_lexer_deinit(&p.lx);
                    parser_free_hoisted_funcs(&p);
                    parser_free_typedefs(&p);
                    parser_free_vars(&p);
                    parser_free_enum_consts(&p);
                    parser_free_enum_tags(&p);
                    parser_free_structs(&p);
                    free(buf);
                    cc_tu_free(out);
                    return -1;
                }
                g.name = decls[di].decl_name;
                decls[di].decl_name = NULL;
                g.line = decls[di].line;
                g.col = decls[di].col;
                g.type = decls[di].type;
                g.type_struct_id = decls[di].type_struct_id;
                g.array_len = decls[di].array_len;
                g.array_ndim = decls[di].array_ndim;
                memcpy(g.array_dims, decls[di].array_dims, sizeof(g.array_dims));
                g.storage = decls[di].storage;
                g.attr_flags = decls[di].attr_flags;
                g.attr_align = decls[di].attr_align;
                g.attr_section = decls[di].attr_section;
                decls[di].attr_section = NULL;
                g.attr_alias = decls[di].attr_alias;
                decls[di].attr_alias = NULL;
                g.init = decls[di].expr;
                decls[di].expr = NULL;
                if (push_global(out, g) != 0) {
                    while (di < decl_count) {
                        free_stmt(&decls[di]);
                        di++;
                    }
                    free(decls);
                    cc_lexer_deinit(&p.lx);
                    parser_free_hoisted_funcs(&p);
                    parser_free_typedefs(&p);
                    parser_free_vars(&p);
                    parser_free_enum_consts(&p);
                    parser_free_enum_tags(&p);
                    parser_free_structs(&p);
                    free(buf);
                    cc_tu_free(out);
                    set_diag(diag, 0, 0, "out of memory");
                    return -1;
                }
                free_stmt(&decls[di]);
            }
            free(decls);
            continue;
        }
        cc_function_t f;
        if (parse_function(&p, &f) != 0) {
            if (diag_is_oom(diag)) {
                cc_lexer_deinit(&p.lx);
                parser_free_hoisted_funcs(&p);
                parser_free_typedefs(&p);
                parser_free_vars(&p);
                parser_free_enum_consts(&p);
                parser_free_enum_tags(&p);
                parser_free_structs(&p);
                free(buf);
                cc_tu_free(out);
                return -1;
            }
            diag_report_and_clear(diag);
            if (parser_sync_toplevel(&p) != 0) {
                cc_lexer_deinit(&p.lx);
                parser_free_hoisted_funcs(&p);
                parser_free_typedefs(&p);
                parser_free_vars(&p);
                parser_free_enum_consts(&p);
                parser_free_enum_tags(&p);
                parser_free_structs(&p);
                free(buf);
                cc_tu_free(out);
                return -1;
            }
            continue;
        }
        if (tu_push_function(out, &f) != 0) {
            free_func(&f);
            cc_lexer_deinit(&p.lx);
            parser_free_hoisted_funcs(&p);
            parser_free_typedefs(&p);
            parser_free_vars(&p);
            parser_free_enum_consts(&p);
            parser_free_enum_tags(&p);
            parser_free_structs(&p);
            free(buf);
            cc_tu_free(out);
            set_diag(diag, 0, 0, "out of memory");
            return -1;
        }
        while (p.hoisted_func_count > 0) {
            cc_function_t nested = p.hoisted_funcs[0];
            size_t hi;
            for (hi = 1; hi < p.hoisted_func_count; ++hi) {
                p.hoisted_funcs[hi - 1] = p.hoisted_funcs[hi];
            }
            p.hoisted_func_count--;
            if (tu_push_function(out, &nested) != 0) {
                free_func(&nested);
                cc_lexer_deinit(&p.lx);
                parser_free_hoisted_funcs(&p);
                parser_free_typedefs(&p);
                parser_free_vars(&p);
                parser_free_enum_consts(&p);
                parser_free_enum_tags(&p);
                parser_free_structs(&p);
                free(buf);
                cc_tu_free(out);
                set_diag(diag, 0, 0, "out of memory");
                return -1;
            }
        }
    }

    cc_lexer_deinit(&p.lx);
    parser_free_hoisted_funcs(&p);
    parser_free_typedefs(&p);
    parser_free_vars(&p);
    parser_free_enum_consts(&p);
    parser_free_enum_tags(&p);
    if (diag != NULL && diag->error_count > 0) {
        parser_free_structs(&p);
        free(buf);
        cc_tu_free(out);
        snprintf(diag->message, sizeof(diag->message), "%zu error(s) generated", diag->error_count);
        return -1;
    }
    out->structs = p.structs;
    out->struct_count = p.struct_count;
    p.structs = NULL;
    p.struct_count = 0;
    p.struct_cap = 0;
    free(buf);
    return 0;
}

void cc_parser_set_pointer_size(int bytes) {
    if (bytes == 4 || bytes == 8) {
        g_parser_pointer_size_bytes = bytes;
    }
}

void cc_parser_set_std_mode(const char *std_mode) {
    g_parser_std_c11 = 0;
    g_parser_std_c17 = 0;
    g_parser_std_c23 = 0;
    g_parser_std_gnu = 0;

    if (std_mode == NULL || std_mode[0] == '\0') {
        g_parser_enable_trigraphs = 1;
        g_parser_allow_oldstyle_funcdecl = 0;
        return;
    }
    if (strncmp(std_mode, "gnu", 3) == 0) {
        g_parser_std_gnu = 1;
    }
    if (strcmp(std_mode, "c11") == 0 || strcmp(std_mode, "gnu11") == 0) {
        g_parser_std_c11 = 1;
    } else if (strcmp(std_mode, "c17") == 0 || strcmp(std_mode, "gnu17") == 0 ||
               strcmp(std_mode, "c18") == 0 || strcmp(std_mode, "gnu18") == 0) {
        g_parser_std_c11 = 1;
        g_parser_std_c17 = 1;
    } else if (strcmp(std_mode, "c23") == 0 || strcmp(std_mode, "gnu23") == 0 || strcmp(std_mode, "c2x") == 0 ||
               strcmp(std_mode, "gnu2x") == 0) {
        g_parser_std_c11 = 1;
        g_parser_std_c17 = 1;
        g_parser_std_c23 = 1;
    }
    if (strcmp(std_mode, "c23") == 0 || strcmp(std_mode, "gnu23") == 0 || strcmp(std_mode, "c2x") == 0 ||
        strcmp(std_mode, "gnu2x") == 0) {
        g_parser_enable_trigraphs = 0;
    } else {
        g_parser_enable_trigraphs = 1;
    }
    if (g_parser_std_gnu || strcmp(std_mode, "c89") == 0 || strcmp(std_mode, "c90") == 0 ||
        strcmp(std_mode, "c95") == 0 || strcmp(std_mode, "c99") == 0 || strcmp(std_mode, "c11") == 0 ||
        strcmp(std_mode, "c17") == 0 || strcmp(std_mode, "c18") == 0) {
        g_parser_allow_oldstyle_funcdecl = 1;
    } else {
        g_parser_allow_oldstyle_funcdecl = 0;
    }
    if (strcmp(std_mode, "gnu89") == 0 || strcmp(std_mode, "gnu90") == 0 || strcmp(std_mode, "gnu95") == 0) {
        g_parser_gnu89_inline = 1;
    } else {
        g_parser_gnu89_inline = 0;
    }
}

void cc_parser_set_gnu89_inline(int enabled) {
    g_parser_gnu89_inline = enabled ? 1 : 0;
}
