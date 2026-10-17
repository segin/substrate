#include "cc_frontend.h"
#include "cc_consteval.h"
#include "cc_ctype.h"
#include "cc_decimal.h"
#include "cc_diag.h"
#include "cc_lang.h"
#include "cc_lexer.h"
#include "cc_options.h"
#include "cc_parser.h"
#include "cc_types.h"
#include "cc_util.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CC_MAX_PARSE_DEPTH 256

static const char *g_parser_diag_file = NULL;

/**
 * Record a parse error at a position in the file being parsed.  A later
 * error replaces an earlier one: the parser reports the error it stops
 * at.
 *
 * @param d     the diagnostic
 * @param line  the line
 * @param col   the column
 * @param msg   the message
 */
static void set_diag(cc_diag_t *d, size_t line, size_t col, const char *msg) {
    cc_diag_replace(d, g_parser_diag_file != NULL ? g_parser_diag_file : "", line, col, msg);
}

static int diag_is_oom(const cc_diag_t *d) {
    if (d == NULL || d->message[0] == '\0') {
        return 0;
    }
    return strstr(d->message, "out of memory") != NULL;
}


static int next_tok(parser_t *p);
static int parser_hex_value(int c);

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
    cc_attr_flags_t flags;
    long align;
    long format_index;
    long format_first_to_check;
    char *section;
    char *alias;
    long vector_bytes; /**< `vector_size(n)`: the vector's size in bytes; 0 for none */
    long vector_elems; /**< `ext_vector_type(n)`: its element count; 0 for none */
} decl_attrs_t;

static int is_decl_qual_tok(cc_tok_kind_t k);
static int is_decl_qual_at_token(parser_t *p);
static int tok_is_ident(parser_t *p, const char *s);
static int tok_is_gnu_attribute_kw(parser_t *p);
static int is_ptr_declarator_tok(cc_tok_kind_t kind);
static void decl_attrs_reset(decl_attrs_t *a);
static void decl_attrs_clear(decl_attrs_t *a);
static int decl_attrs_merge(decl_attrs_t *dst, const decl_attrs_t *src);
static int parse_c23_attribute_seq(parser_t *p, decl_attrs_t *out_attrs, int *out_stmt_flags);
static int skip_balanced_parens(parser_t *p);
static int parse_gnu_attr_arguments(parser_t *p, long *out_num, int *out_has_num, long *out_num2, int *out_has_num2,
                                    char **out_str);
static int parse_gnu_format_attr_arguments(parser_t *p, long *out_num, int *out_has_num, long *out_num2,
                                           int *out_has_num2);
static int skip_decl_gnu_suffix(parser_t *p, decl_attrs_t *out_attrs);
static int parse_asm_stmt(parser_t *p, cc_stmt_t *s);
static int parse_type_name(parser_t *p, const char *what, const cc_ctype_t **out, cc_bounds_t *out_vla_bounds);
static cc_expr_t *parse_expr(parser_t *p);
static cc_expr_t *parse_cond(parser_t *p);
static cc_expr_t *parse_unary(parser_t *p);
static cc_expr_t *new_bin_expr(cc_binop_t op, cc_expr_t *lhs, cc_expr_t *rhs);
static void free_expr(cc_expr_t *e);
static void free_stmt(cc_stmt_t *s);
static int is_static_assert_tok(parser_t *p);
static int parse_static_assert_decl(parser_t *p, int require_semi);
static int parse_array_extent(parser_t *p, long *out_n, int *out_const_n, cc_expr_t **out_bound_expr);
static int parse_params(parser_t *p, cc_function_t *f, int allow_oldstyle_names);
static int can_parse_oldstyle_param_names(parser_t *p);
static int parse_function(parser_t *p, cc_function_t *f);
static int function_param_index_by_name(const cc_function_t *f, const char *name);
static int probe_is_function_head(parser_t *p);
static void free_func(cc_function_t *f);
static int enum_tag_find_n(const parser_t *p, const char *tag, size_t len);
static int enum_tag_set(parser_t *p, const char *tag, size_t len, const cc_ctype_t *ctype, int complete);
static void parser_free_enum_tags(parser_t *p);
static const char *local_label_lookup_n(const parser_t *p, const char *name, size_t len);
static char *local_label_dup_mapped_n(const parser_t *p, const char *name, size_t len);
static int local_label_push(parser_t *p, const char *name, size_t len);
static void local_label_pop_to_depth(parser_t *p, int depth);
static void parser_free_local_labels(parser_t *p);
static void parser_set_ctype(cc_expr_t *e, const cc_ctype_t *t);
static cc_expr_t *new_const_expr(cc_expr_kind_t kind, const cc_ctype_t *t);
static const cc_ctype_t *parser_size_ctype(void);

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
    return cc_lang_at_least(2011);
}

static int parser_is_c23_or_newer(void) {
    return cc_lang_at_least(2023);
}

static int parser_is_gnu_mode(void) {
    return cc_lang_gnu();
}

static int parser_relax_static_asserts(void) {
    return 0;
}

static int parser_allow_static_assert_extension(void) {
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

/**
 * Find a universal character name in an identifier that may not be
 * there: one for a character of the basic set other than `$`, `@` and
 * `` ` `` (C11 6.4.3p2), or for a surrogate (6.4.3p2 again).
 *
 * @param tok  an identifier token
 * @param out  receives the bad UCN's spelling, NUL-terminated
 * @param cap  the size of @p out (at least 11)
 * @return     1 if one was found, else 0
 */
static int ident_invalid_ucn(const cc_token_t *tok, char *out, size_t cap) {
    size_t i = 0;

    while (i + 1 < tok->len) {
        size_t digits;
        size_t k;
        unsigned long v = 0;
        if (tok->start[i] != '\\' || (tok->start[i + 1] != 'u' && tok->start[i + 1] != 'U')) {
            i++;
            continue;
        }
        digits = tok->start[i + 1] == 'u' ? 4 : 8;
        for (k = 0; k < digits && i + 2 + k < tok->len; ++k) {
            int hv = parser_hex_value((unsigned char)tok->start[i + 2 + k]);
            if (hv < 0) {
                break;
            }
            v = (v << 4) | (unsigned long)hv;
        }
        if ((v < 0xa0 && v != 0x24 && v != 0x40 && v != 0x60) || (v >= 0xd800 && v <= 0xdfff)) {
            size_t n = 2 + k < cap - 1 ? 2 + k : cap - 1;
            memcpy(out, tok->start + i, n);
            out[n] = '\0';
            return 1;
        }
        i += 2 + k;
    }
    return 0;
}

static int next_tok(parser_t *p) {
    char ucn[16];

    if (cc_lexer_next(&p->lx, &p->tok) != 0) {
        g_parser_diag_file = p->lx.logical_file;
        set_diag(p->diag, p->lx.line, p->lx.col, "invalid token");
        return -1;
    }
    g_parser_diag_file = p->tok.file;
    if (p->tok.kind == TOK_IDENT && ident_invalid_ucn(&p->tok, ucn, sizeof(ucn))) {
        char msg[96];
        snprintf(msg, sizeof(msg), "universal character %s is not valid in an identifier", ucn);
        set_diag(p->diag, p->tok.line, p->tok.col, msg);
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
    if (p->lx.logical_file != NULL) {
        lx.logical_file = cc_strndup(p->lx.logical_file, strlen(p->lx.logical_file));
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

/**
 * Declare a typedef name in the current scope.  Redeclaring one with the
 * same type is allowed (C11 6.7p3).
 *
 * @param p      the parser
 * @param name   the name
 * @param ctype  the type it names
 * @return       0, 1 if it redeclares the name with another type, or -1
 *               when out of memory
 */
static int typedef_push(parser_t *p, const char *name, const cc_ctype_t *ctype) {
    typedef_entry_t *next;
    char *dup;
    int existing;

    existing = typedef_find_current_scope_n(p, name, strlen(name));
    if (existing >= 0) {
        return p->typedefs[existing].ctype == ctype ? 0 : 1;
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
    dup = cc_strndup(name, strlen(name));
    if (dup == NULL) {
        return -1;
    }
    p->typedefs[p->typedef_count].name = dup;
    p->typedefs[p->typedef_count].depth = p->scope_depth;
    p->typedefs[p->typedef_count].ctype = ctype;
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
    dup = cc_strndup(name, len);
    if (dup == NULL) {
        return -1;
    }
    p->enum_consts[p->enum_const_count].name = dup;
    p->enum_consts[p->enum_const_count].value = value;
    p->enum_consts[p->enum_const_count].bits = (uint64_t)value;
    p->enum_consts[p->enum_const_count].ctype = cc_ct_basic(CC_CT_INT);
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

/**
 * Record (or update) an enumeration tag and the type `enum tag` names.
 *
 * @return 0, or -1 if out of memory
 */
static int enum_tag_set(parser_t *p, const char *tag, size_t len, const cc_ctype_t *ctype, int complete) {
    int idx;
    char *dup;
    enum_tag_entry_t *next;

    if (p == NULL || tag == NULL || len == 0) {
        return 0;
    }
    idx = enum_tag_find_n(p, tag, len);
    if (idx >= 0) {
        p->enum_tags[idx].ctype = ctype;
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
    dup = cc_strndup(tag, len);
    if (dup == NULL) {
        return -1;
    }
    p->enum_tags[p->enum_tag_count].tag = dup;
    p->enum_tags[p->enum_tag_count].ctype = ctype;
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

static const char *local_label_lookup_n(const parser_t *p, const char *name, size_t len) {
    size_t i;

    if (p == NULL || name == NULL || len == 0) {
        return NULL;
    }
    i = p->local_label_count;
    while (i > 0) {
        i--;
        if (p->local_labels[i].name != NULL &&
            strlen(p->local_labels[i].name) == len &&
            strncmp(p->local_labels[i].name, name, len) == 0) {
            return p->local_labels[i].unique_name;
        }
    }
    return NULL;
}

static char *local_label_dup_mapped_n(const parser_t *p, const char *name, size_t len) {
    const char *mapped = local_label_lookup_n(p, name, len);

    if (mapped != NULL) {
        return cc_strndup(mapped, strlen(mapped));
    }
    return cc_strndup(name, len);
}

static int local_label_push(parser_t *p, const char *name, size_t len) {
    local_label_entry_t *next;
    char unique[256];
    char *dup_name;
    char *dup_unique;
    int n;

    if (p == NULL || name == NULL || len == 0) {
        return -1;
    }
    if (p->local_label_count == p->local_label_cap) {
        size_t ncap = p->local_label_cap == 0 ? 16 : p->local_label_cap * 2;
        next = (local_label_entry_t *)realloc(p->local_labels, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        p->local_labels = next;
        p->local_label_cap = ncap;
    }
    n = snprintf(unique, sizeof(unique), "__cc_local_label_%zu_%.*s",
                 p->local_label_unique++, (int)len, name);
    if (n <= 0 || (size_t)n >= sizeof(unique)) {
        return -1;
    }
    dup_name = cc_strndup(name, len);
    dup_unique = cc_strndup(unique, strlen(unique));
    if (dup_name == NULL || dup_unique == NULL) {
        free(dup_name);
        free(dup_unique);
        return -1;
    }
    p->local_labels[p->local_label_count].name = dup_name;
    p->local_labels[p->local_label_count].unique_name = dup_unique;
    p->local_labels[p->local_label_count].depth = p->scope_depth;
    p->local_label_count++;
    return 0;
}

static void local_label_pop_to_depth(parser_t *p, int depth) {
    while (p->local_label_count > 0 && p->local_labels[p->local_label_count - 1].depth > depth) {
        free(p->local_labels[p->local_label_count - 1].name);
        free(p->local_labels[p->local_label_count - 1].unique_name);
        p->local_label_count--;
    }
}

static void parser_free_local_labels(parser_t *p) {
    local_label_pop_to_depth(p, -1);
    free(p->local_labels);
    p->local_labels = NULL;
    p->local_label_count = 0;
    p->local_label_cap = 0;
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

/**
 * Whether two types are compatible (C11 6.2.7), as
 * `__builtin_types_compatible_p` asks: qualifiers count, an array of
 * unknown bound matches any bound, structures match by tag, and an
 * enumeration matches only itself.
 *
 * @param p      the parser
 * @param a      one type
 * @param b      the other
 * @param depth  recursion depth so far
 * @return       1 or 0
 */
static int parser_ct_compatible(const parser_t *p, const cc_ctype_t *a, const cc_ctype_t *b, int depth) {
    size_t i;

    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL || a->kind != b->kind || a->quals != b->quals || depth > 64) {
        return 0;
    }
    switch (a->kind) {
    case CC_CT_POINTER:
    case CC_CT_COMPLEX:
    case CC_CT_IMAGINARY:
        return parser_ct_compatible(p, a->of, b->of, depth + 1);
    case CC_CT_ARRAY:
        return (a->count < 0 || b->count < 0 || a->count == b->count) &&
               parser_ct_compatible(p, a->of, b->of, depth + 1);
    case CC_CT_RECORD:
        return struct_ids_compatible(p, a->id, b->id);
    case CC_CT_FUNCTION:
        if (!parser_ct_compatible(p, a->of, b->of, depth + 1)) {
            return 0;
        }
        if (!a->prototyped || !b->prototyped) {
            return 1;
        }
        if (a->param_count != b->param_count || a->variadic != b->variadic) {
            return 0;
        }
        for (i = 0; i < a->param_count; ++i) {
            if (!parser_ct_compatible(p, a->params[i]->unqual, b->params[i]->unqual, depth + 1)) {
                return 0;
            }
        }
        return 1;
    default:
        /* the other kinds are interned whole: equal types are one object */
        return 0;
    }
}

static long parser_ct_align(const parser_t *p, const cc_ctype_t *t);

/**
 * The smallest integer kind that holds every enumerator of a packed
 * enumeration whose values run from @p min_v to @p max_v.
 */
static cc_ctype_kind_t packed_enum_kind_from_range(long min_v, long max_v) {
    if (min_v >= 0) {
        if ((unsigned long)max_v <= (unsigned long)UCHAR_MAX) {
            return CC_CT_UCHAR;
        }
        if ((unsigned long)max_v <= (unsigned long)USHRT_MAX) {
            return CC_CT_USHORT;
        }
        if ((unsigned long)max_v <= (unsigned long)UINT_MAX) {
            return CC_CT_UINT;
        }
        return CC_CT_ULLONG;
    }
    if (min_v >= SCHAR_MIN && max_v <= SCHAR_MAX) {
        return CC_CT_CHAR;
    }
    if (min_v >= SHRT_MIN && max_v <= SHRT_MAX) {
        return CC_CT_SHORT;
    }
    if (sizeof(long) > sizeof(int)) {
        if (min_v >= INT_MIN && max_v <= INT_MAX) {
            return CC_CT_INT;
        }
    }
    return CC_CT_LLONG;
}

/** Whether a bit-field may have type @p t: _Bool, a standard integer type or an enumeration. */
static int parser_is_bitfield_base_type(const cc_ctype_t *t) {
    if (t == NULL) {
        return 0;
    }
    switch (t->unqual->kind) {
    case CC_CT_BOOL:
    case CC_CT_CHAR:
    case CC_CT_SCHAR:
    case CC_CT_UCHAR:
    case CC_CT_SHORT:
    case CC_CT_USHORT:
    case CC_CT_INT:
    case CC_CT_UINT:
    case CC_CT_LONG:
    case CC_CT_ULONG:
    case CC_CT_LLONG:
    case CC_CT_ULLONG:
    case CC_CT_ENUM:
        return 1;
    default:
        return 0;
    }
}

/** Whether a bit-field of type @p t holds negative values (an enumeration's do not, here). */
static int parser_bitfield_is_signed(const cc_ctype_t *t) {
    switch (t->unqual->kind) {
    case CC_CT_CHAR:
    case CC_CT_SCHAR:
    case CC_CT_SHORT:
    case CC_CT_INT:
    case CC_CT_LONG:
    case CC_CT_LLONG:
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

/**
 * Record a member declarator of structure @p sid for its layout.
 *
 * @param p           the parser
 * @param sid         the structure
 * @param member      the member it declares (an index into its members), or -1 for an unnamed bit-field
 * @param ctype       a bit-field's declared type, else NULL
 * @param size        an ordinary member's size in bytes
 * @param align       its type's alignment, or 1 where it is `packed`
 * @param user_align  the alignment `aligned` or `_Alignas` asks for, or 0
 * @param bit_width   a bit-field's width, or -1 for an ordinary member
 * @return            0, or -1 when out of memory
 */
static int struct_layout_push(parser_t *p, int sid, int member, const cc_ctype_t *ctype, long size, long align,
                              long user_align, int bit_width) {
    cc_struct_def_t *sd = &p->structs[sid];
    cc_member_layout_t *it;

    if (sd->layout_count == sd->layout_cap) {
        size_t ncap = sd->layout_cap == 0 ? 8 : sd->layout_cap * 2;
        cc_member_layout_t *next = (cc_member_layout_t *)realloc(sd->layout, ncap * sizeof(*next));

        if (next == NULL) {
            return -1;
        }
        sd->layout = next;
        sd->layout_cap = ncap;
    }
    it = &sd->layout[sd->layout_count++];
    it->member = member;
    it->ctype = ctype;
    it->size = size;
    it->align = align;
    it->user_align = user_align;
    it->bit_width = bit_width;
    return 0;
}

/**
 * The storage unit lowering reads and writes a bit-field through: the
 * aligned unit of its type that holds it (its type's alignment where a
 * type is wider than it is aligned, as i386's `long long` is, and the
 * field fits one such word), or for a packed field the smallest of 1, 2,
 * 4 or 8 bytes from the byte it starts in.
 *
 * @param pos         its first bit, from the start of the structure
 * @param width       its width
 * @param type_size   its type's size in bytes
 * @param type_align  its type's alignment in bytes
 * @param packed      it is packed: it may start at any bit
 * @param out_start   receives the unit's offset in bytes
 * @param out_size    receives the unit's size in bytes
 * @return            0, or -1 if no unit of at most 8 bytes holds it
 */
static int bitfield_unit(long pos, int width, long type_size, long type_align, int packed, long *out_start,
                         long *out_size) {
    long start;
    long size;

    if (!packed) {
        start = pos / (type_align * 8) * type_align;
        size = type_size;
        if (type_size > type_align && pos - start * 8 + width <= type_align * 8) {
            size = type_align;
        }
    } else {
        start = pos / 8;
        size = 1;
        while (size < 8 && size * 8 < pos % 8 + width) {
            size *= 2;
        }
    }
    if (pos - start * 8 + width > size * 8) {
        return -1;
    }
    *out_start = start;
    *out_size = size;
    return 0;
}

/**
 * Lay out structure or union @p sid from its member declarators, as gcc
 * and the System V psABI do.  An ordinary member goes at the next offset
 * its alignment allows.  A bit-field goes at the next free bit, unless it
 * would then cross a boundary of its type's alignment that its type's
 * size does not allow, when it goes to that boundary; so it may share the
 * bytes of the members before it.  A zero-width bit-field moves the next
 * one to its type's alignment; it and unnamed bit-fields do not raise the
 * structure's alignment.  Packed bit-fields start at any bit.
 *
 * @param p             the parser
 * @param sid           the structure
 * @param packed        the structure is `packed`
 * @param forced_align  the alignment `aligned` asks of the structure, or 0
 * @return              0, or -1 on error (diagnosed)
 */
static int struct_layout(parser_t *p, int sid, int packed, long forced_align) {
    cc_struct_def_t *sd = &p->structs[sid];
    long bits = 0; /* a structure's next free bit; a union's size in bits */
    long max_align = 1;
    long size;
    size_t i;

    if (sd->layout_count == 0 && sd->member_count > 0) {
        /* a structure the compiler made (complex, wide integer, vector, decimal): laid out already */
        if (forced_align > sd->align) {
            sd->align = forced_align;
            sd->size = align_up_long(sd->size, forced_align);
        }
        return 0;
    }
    for (i = 0; i < sd->layout_count; ++i) {
        const cc_member_layout_t *it = &sd->layout[i];
        cc_struct_member_t *m = it->member >= 0 ? &sd->members[it->member] : NULL;
        long align = packed ? 1 : it->align;

        if (it->user_align > align) {
            align = it->user_align;
        }
        if (it->bit_width >= 0) {
            long type_size = parser_ct_size(p, it->ctype);
            long type_align = parser_ct_align(p, it->ctype);
            int bit_packed = packed || it->align < type_align;
            long pos = 0;
            long start;
            long unit;

            if (type_size <= 0 || type_align <= 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "unsupported bit-field storage type size");
                return -1;
            }
            if (!sd->is_union) {
                if (it->bit_width == 0) {
                    bits = align_up_long(bits, type_align * 8);
                    continue;
                }
                if (it->user_align > 0) {
                    bits = align_up_long(bits, it->user_align * 8);
                }
                if (!bit_packed && (bits % (type_align * 8) + it->bit_width + type_align * 8 - 1) /
                                           (type_align * 8) * (type_align * 8) >
                                       type_size * 8) {
                    bits = align_up_long(bits, type_align * 8);
                }
                pos = bits;
                bits += it->bit_width;
            } else if (it->bit_width > bits) {
                bits = it->bit_width;
            }
            if (m == NULL) {
                continue;
            }
            if (bitfield_unit(pos, it->bit_width, type_size, type_align, bit_packed, &start, &unit) != 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "unsupported packed bit-field layout");
                return -1;
            }
            m->offset = start;
            m->bit_offset = (int)(pos - start * 8);
            m->bit_storage_size = unit;
            m->bit_storage_bits = (int)(unit * 8);
            m->size = unit;
            if (!bit_packed && type_align > align) {
                align = type_align;
            }
        } else if (sd->is_union) {
            if (m != NULL) {
                m->offset = 0;
            }
            if (it->size * 8 > bits) {
                bits = it->size * 8;
            }
        } else {
            bits = align_up_long(bits, align * 8);
            if (m != NULL) {
                m->offset = bits / 8;
            }
            bits += it->size * 8;
        }
        if (align > max_align) {
            max_align = align;
        }
    }
    if (forced_align > max_align) {
        max_align = forced_align;
    }
    size = align_up_long((bits + 7) / 8, max_align);
    /* a unit reaching past the end moves back: lowering must not touch the bytes after the object */
    for (i = 0; i < sd->member_count; ++i) {
        cc_struct_member_t *m = &sd->members[i];

        if (m->is_bitfield && m->bit_width > 0 && m->offset + m->bit_storage_size > size) {
            long back = m->offset + m->bit_storage_size - size;

            if (back > m->offset) {
                set_diag(p->diag, p->tok.line, p->tok.col, "unsupported packed bit-field layout");
                return -1;
            }
            m->offset -= back;
            m->bit_offset += (int)(back * 8);
        }
    }
    sd->size = size;
    sd->align = max_align;
    return 0;
}

/**
 * Lay structure @p sid out again with the `packed` and `aligned`
 * attributes of @p attrs added to those it has.
 *
 * @return 0, or -1 on error
 */
static int apply_struct_attrs(parser_t *p, int sid, const decl_attrs_t *attrs) {
    cc_struct_def_t *sd;
    int packed = 0;
    long forced_align = 0;

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

    if (struct_layout(p, sid, packed, forced_align) != 0) {
        return -1;
    }
    sd = &p->structs[sid];
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
        dup = cc_strndup(tag, len);
        if (dup == NULL) {
            return -1;
        }
    }
    p->structs[p->struct_count].tag = dup;
    p->structs[p->struct_count].depth = p->scope_depth;
    p->structs[p->struct_count].complete = 0;
    return (int)p->struct_count++;
}

static int struct_member_push(parser_t *p, int sid, const char *name, const cc_ctype_t *ctype, long offset,
                              long size, int is_bitfield, int bit_width, int bit_offset, int bit_storage_bits,
                              int bit_signed, long bit_storage_size);

/**
 * The structure that holds a _Complex T in storage:
 * `{ T __re, __im; }`, made once per element type at file scope and
 * recorded for the interned type (cc_ct_set_complex_record()).  Laid out
 * as gcc lays out a complex value: the real part, then the imaginary.
 *
 * @param p     the parser
 * @param kind  CC_CT_FLOAT, CC_CT_DOUBLE or CC_CT_LDOUBLE
 * @return      the structure's index, or -1 when out of memory
 */
static int parser_complex_struct(parser_t *p, cc_ctype_kind_t kind) {
    const cc_ctype_t *ct = cc_ct_basic(kind);
    long es = cc_ct_size(ct, NULL);
    size_t i;
    int sid;
    int saved_depth = p->scope_depth;

    for (i = 0; i < p->struct_count; ++i) {
        if (p->structs[i].complex_elem == ct) {
            return (int)i;
        }
    }
    p->scope_depth = 0;
    sid = struct_ensure(p, NULL, 0, 1);
    p->scope_depth = saved_depth;
    if (sid < 0 || struct_member_push(p, sid, "__re", ct, 0, es, 0, 0, 0, 0, 0, 0) < 0 ||
        struct_member_push(p, sid, "__im", ct, es, es, 0, 0, 0, 0, 0, 0) < 0) {
        return -1;
    }
    p->structs[sid].size = 2 * es;
    p->structs[sid].align = cc_ct_align(ct, NULL);
    p->structs[sid].complete = 1;
    p->structs[sid].complex_elem = ct;
    if (!p->is_probe) {
        cc_ct_set_complex_record(kind, sid);
    }
    return sid;
}

/**
 * The structure that is an integer type wider than 64 bits:
 * `{ limb __limb[n]; }`, the value's limbs least
 * significant first, a limb being the target's word (64 bits on x86-64, 32
 * on i386) -- the layout and the psABI classification gcc gives
 * `_BitInt(N)`.  `__int128` is the same but 16-aligned and a type of its
 * own.  Made once per kind at file scope.
 *
 * @param p            the parser
 * @param bits         the width, more than 64
 * @param is_unsigned  nonzero for an unsigned type
 * @param is_int128    nonzero for `__int128` (bits is 128)
 * @return             the structure's index, or -1 when out of memory
 */
static int parser_wide_struct(parser_t *p, long bits, int is_unsigned, int is_int128) {
    long limb = cc_target_pointer_size();
    long n = (bits + limb * 8 - 1) / (limb * 8);
    size_t i;
    int sid;
    int saved_depth = p->scope_depth;

    for (i = 0; i < p->struct_count; ++i) {
        const cc_struct_def_t *sd = &p->structs[i];

        if (sd->wide_bits == bits && sd->wide_unsigned == (is_unsigned != 0) && sd->wide_is_int128 == (is_int128 != 0)) {
            return (int)i;
        }
    }
    p->scope_depth = 0;
    sid = struct_ensure(p, NULL, 0, 1);
    p->scope_depth = saved_depth;
    if (sid < 0 || struct_member_push(p, sid, "__limb", cc_ct_array(cc_ct_basic(limb == 8 ? CC_CT_ULLONG : CC_CT_UINT), n),
                                      0, n * limb, 0, 0, 0, 0, 0, 0) < 0) {
        return -1;
    }
    p->structs[sid].size = n * limb;
    p->structs[sid].align = is_int128 ? 16 : limb;
    p->structs[sid].complete = 1;
    p->structs[sid].wide_bits = bits;
    p->structs[sid].wide_unsigned = is_unsigned != 0;
    p->structs[sid].wide_is_int128 = is_int128 != 0;
    return sid;
}

static cc_expr_t *new_expr(cc_expr_kind_t kind);
static int push_arg(cc_expr_t *call, cc_expr_t *arg);

/**
 * The structure standing for the decimal floating type @p bits wide (32,
 * 64 or 128): `{ unsigned __bid; }`, `{ unsigned long long __bid; }` or
 * `{ unsigned long long __bid[2]; }` (low half first) holding its BID
 * encoding, of the type's size and alignment.  Made once per width.
 *
 * @return the structure's id, or -1 when out of memory
 */
static int parser_decimal_struct(parser_t *p, int bits) {
    long size = bits / 8;
    size_t i;
    int sid;
    int saved_depth = p->scope_depth;
    int rc;

    for (i = 0; i < p->struct_count; ++i) {
        if (p->structs[i].decimal_bits == bits) {
            return (int)i;
        }
    }
    p->scope_depth = 0;
    sid = struct_ensure(p, NULL, 0, 1);
    p->scope_depth = saved_depth;
    if (sid < 0) {
        return -1;
    }
    if (bits == 128) {
        rc = struct_member_push(p, sid, "__bid", cc_ct_array(cc_ct_basic(CC_CT_ULLONG), 2), 0, 16, 0, 0, 0, 0, 0, 0);
    } else {
        rc = struct_member_push(p, sid, "__bid", cc_ct_basic(bits == 32 ? CC_CT_UINT : CC_CT_ULLONG), 0, size, 0, 0,
                                0, 0, 0, 0);
    }
    if (rc < 0) {
        return -1;
    }
    p->structs[sid].size = size;
    p->structs[sid].align = size;
    p->structs[sid].complete = 1;
    p->structs[sid].decimal_bits = bits;
    return sid;
}

/**
 * The decimal floating constant at the current token (a df, dd or dl
 * suffix): `(decimal type){ bid }`, its BID encoding worked out now --
 * the coefficient and exponent as written, rounded to the type.
 *
 * @return the expression, or NULL on error (diagnosed)
 */
static cc_expr_t *parser_decimal_literal(parser_t *p) {
    int bits = p->tok.float_is_decimal;
    int sid = parser_decimal_struct(p, bits);
    cc_dec_t *d = (cc_dec_t *)malloc(sizeof(*d));
    uint64_t bid[2];
    cc_expr_t *outer = new_expr(CC_EXPR_INIT_LIST);
    cc_expr_t *list = outer;
    cc_expr_t *e = new_expr(CC_EXPR_CAST);
    int k;

    if (sid < 0 || d == NULL || outer == NULL || e == NULL) {
        free(d);
        free_expr(outer);
        free_expr(e);
        set_diag(p->diag, p->tok.line, p->tok.col, "out of memory reading a decimal constant");
        return NULL;
    }
    if (cc_dec_from_string(p->tok.start, p->tok.len, d) != 0) {
        free(d);
        free_expr(outer);
        free_expr(e);
        set_diag(p->diag, p->tok.line, p->tok.col, "malformed decimal floating constant");
        return NULL;
    }
    cc_dec_encode_bid(d, bits, bid);
    free(d);
    if (bits == 128) {
        /* the halves initialize the structure's one member, an array */
        list = new_expr(CC_EXPR_INIT_LIST);
        if (list == NULL || push_arg(outer, list) != 0) {
            free_expr(list);
            free_expr(outer);
            free_expr(e);
            return NULL;
        }
    }
    for (k = 0; k < (bits == 128 ? 2 : 1); ++k) {
        /* not new_int_expr(): its long is 32 bits where cc runs on i386 */
        cc_expr_t *item = new_const_expr(CC_EXPR_INT, cc_ct_basic(bits == 32 ? CC_CT_UINT : CC_CT_ULLONG));

        if (item == NULL || push_arg(list, item) != 0) {
            free_expr(item);
            free_expr(outer);
            free_expr(e);
            return NULL;
        }
        item->int_val = (long long)bid[k];
    }
    e->ctype = cc_ct_record(sid);
    e->lhs = outer;
    return e;
}

/**
 * The element type a vector of @p t elements has: @p t unqualified, or
 * NULL if a vector cannot have it (only the standard integer types but
 * _Bool, float and double can be).
 */
static const cc_ctype_t *parser_vector_elem_ctype(const cc_ctype_t *t) {
    if (t == NULL) {
        return NULL;
    }
    switch (t->unqual->kind) {
    case CC_CT_CHAR:
    case CC_CT_SCHAR:
    case CC_CT_UCHAR:
    case CC_CT_SHORT:
    case CC_CT_USHORT:
    case CC_CT_INT:
    case CC_CT_UINT:
    case CC_CT_LONG:
    case CC_CT_ULONG:
    case CC_CT_LLONG:
    case CC_CT_ULLONG:
    case CC_CT_FLOAT:
    case CC_CT_DOUBLE:
        return t->unqual;
    default:
        return NULL;
    }
}

/**
 * The structure standing for the vector type of @p n elements of type
 * @p elem: `{ elem __vec[n]; }`, aligned to its size up to 16 bytes, as
 * gcc lays a vector out.  Made once per shape and found again after.  It
 * comes with the vector of signed integers of the element's size and the
 * same count, the type its comparisons give (itself for such a vector).
 *
 * @return the structure's id, or -1 when out of memory
 */
static int parser_vector_struct(parser_t *p, const cc_ctype_t *elem, long n) {
    static const cc_ctype_kind_t cmp_by_size[9] = {CC_CT_VOID, CC_CT_CHAR, CC_CT_SHORT, CC_CT_VOID, CC_CT_INT,
                                                   CC_CT_VOID, CC_CT_VOID, CC_CT_VOID,  CC_CT_LLONG};
    long es = parser_ct_size(p, elem);
    const cc_ctype_t *cmp_elem;
    size_t i;
    int sid;
    int saved_depth = p->scope_depth;

    for (i = 0; i < p->struct_count; ++i) {
        if (p->structs[i].vector_count == n && p->structs[i].vector_elem == elem) {
            return (int)i;
        }
    }
    p->scope_depth = 0;
    sid = struct_ensure(p, NULL, 0, 1);
    p->scope_depth = saved_depth;
    if (sid < 0 || struct_member_push(p, sid, "__vec", cc_ct_array(elem, n), 0, n * es, 0, 0, 0, 0, 0, 0) < 0) {
        return -1;
    }
    p->structs[sid].size = n * es;
    p->structs[sid].align = n * es < 16 ? n * es : 16;
    p->structs[sid].complete = 1;
    p->structs[sid].vector_count = n;
    p->structs[sid].vector_elem = elem;
    p->structs[sid].vector_cmp_sid = sid;
    cmp_elem = cc_ct_basic(cmp_by_size[es]);
    if (cmp_elem != elem) {
        int cs = parser_vector_struct(p, cmp_elem, n);

        if (cs < 0) {
            return -1;
        }
        p->structs[sid].vector_cmp_sid = cs;
    }
    return sid;
}

/**
 * Apply a `vector_size(bytes)` or `ext_vector_type(count)` attribute in
 * @p attrs to the type *@p ctype it is written with: the type becomes the
 * vector of that element type.  The size must be a power-of-two multiple
 * of the element's (an ext_vector_type count rounds up to a power of two
 * in size, as clang lays it out) and the element an integer or floating
 * type.
 *
 * @return 0 (also when there is no such attribute), or -1 on error (diagnosed)
 */
static int parser_apply_vector_attr(parser_t *p, const decl_attrs_t *attrs, const cc_ctype_t **ctype) {
    const cc_ctype_t *elem;
    long es;
    long bytes;
    long n;
    int vs;

    if (attrs == NULL || (attrs->vector_bytes <= 0 && attrs->vector_elems <= 0)) {
        return 0;
    }
    elem = parser_vector_elem_ctype(*ctype);
    if (elem == NULL) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid vector type: the element must be an integer or floating type");
        return -1;
    }
    es = parser_ct_size(p, elem);
    if (attrs->vector_bytes > 0) {
        bytes = attrs->vector_bytes;
        n = bytes / es;
    } else {
        n = attrs->vector_elems;
        for (bytes = es; bytes < n * es; bytes *= 2) {
        }
    }
    if (bytes % es != 0 || (bytes & (bytes - 1)) != 0 || n < 1 || bytes > 1024) {
        set_diag(p->diag, p->tok.line, p->tok.col,
                 "invalid vector type: its size must be a power-of-two multiple of the element's");
        return -1;
    }
    vs = parser_vector_struct(p, elem, bytes / es);
    if (vs < 0) {
        set_diag(p->diag, p->tok.line, p->tok.col, "out of memory making a vector type");
        return -1;
    }
    *ctype = cc_ct_record(vs);
    return 0;
}

/** A structure member's declarator, parsed: see parse_member_decl(). */
typedef struct {
    char *name;               /**< the name (owned), or NULL for an unnamed bit-field or aggregate */
    const cc_ctype_t *ctype;  /**< the member's type */
    const cc_ctype_t *elem;   /**< its type, or an array's innermost element type */
    int is_array;             /**< it is an array */
    int unsized;              /**< an array of unknown bound (a flexible array member) */
    long count;               /**< an array's element count (product of its bounds), else -1 */
} member_decl_t;

static int parse_member_decl(parser_t *p, const cc_ctype_t *base, member_decl_t *out);

/**
 * Add a member to structure @p sid.
 *
 * @param p                 the parser
 * @param sid               the structure
 * @param name              its name, or NULL for an anonymous structure or union
 * @param ctype             its type
 * @param offset            its offset, where already laid out
 * @param size              its size, 0 where not yet known
 * @param is_bitfield       it is a bit-field
 * @param bit_width         a bit-field's width
 * @param bit_offset        a bit-field's offset in its storage unit
 * @param bit_storage_bits  the bits of a bit-field's storage unit
 * @param bit_signed        a bit-field is signed
 * @param bit_storage_size  the bytes of a bit-field's storage unit
 * @return                  0, or -1 when out of memory
 */
static int struct_member_push(parser_t *p, int sid, const char *name, const cc_ctype_t *ctype, long offset,
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
    if (name != NULL) {
        for (i = 0; i < sd->member_count; ++i) {
            if (sd->members[i].name != NULL && strcmp(sd->members[i].name, name) == 0) {
                return 1;
            }
        }
    }
    if (sd->member_count == sd->member_cap) {
        size_t ncap = sd->member_cap == 0 ? 8 : sd->member_cap * 2;
        next = (cc_struct_member_t *)realloc(sd->members, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        sd->members = next;
        sd->member_cap = ncap;
    }
    dup = NULL;
    if (name != NULL) {
        dup = cc_strndup(name, strlen(name));
        if (dup == NULL) {
            return -1;
        }
    }
    sd->members[sd->member_count].name = dup;
    sd->members[sd->member_count].offset = offset;
    sd->members[sd->member_count].size = size;
    sd->members[sd->member_count].is_bitfield = is_bitfield;
    sd->members[sd->member_count].bit_width = bit_width;
    sd->members[sd->member_count].bit_offset = bit_offset;
    sd->members[sd->member_count].bit_storage_bits = bit_storage_bits;
    sd->members[sd->member_count].bit_signed = bit_signed;
    sd->members[sd->member_count].bit_storage_size = bit_storage_size;
    sd->members[sd->member_count].ctype = ctype;
    sd->member_count++;
    return 0;
}

static const cc_struct_member_t *struct_member_find_offset_n_depth(const parser_t *p, int sid, const char *name,
                                                                   size_t len, long *out_offset, int depth) {
    size_t i;
    const cc_struct_def_t *sd;

    if (out_offset != NULL) {
        *out_offset = 0;
    }
    if (sid < 0 || (size_t)sid >= p->struct_count || name == NULL || depth > 32) {
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
            if (out_offset != NULL) {
                *out_offset = m->offset;
            }
            return m;
        }
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        const cc_struct_member_t *inner;
        long inner_off = 0;

        if (m->name != NULL || m->ctype == NULL || m->ctype->unqual->kind != CC_CT_RECORD) {
            continue;
        }
        inner = struct_member_find_offset_n_depth(p, m->ctype->unqual->id, name, len, &inner_off, depth + 1);
        if (inner != NULL) {
            if (out_offset != NULL) {
                *out_offset = m->offset + inner_off;
            }
            return inner;
        }
    }
    return NULL;
}

const cc_struct_member_t *struct_member_find_n(const parser_t *p, int sid, const char *name, size_t len) {
    return struct_member_find_offset_n_depth(p, sid, name, len, NULL, 0);
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
        free(p->structs[i].layout);
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

/**
 * The type of a name cc treats as a typedef without a declaration
 * (`size_t` and its relatives, the wide character types).
 *
 * @param name      the name (not NUL-terminated)
 * @param len       its length
 * @param out_type  receives the type; may be NULL
 * @return          1 if it is one, else 0
 */
static int builtin_typedef_type_n(const char *name, size_t len, const cc_ctype_t **out_type) {
    static const struct {
        const char *name;
        int kind; /* a CC_CT_* kind, or -1 for the pointer-sized unsigned, -2 for the signed */
    } names[] = {
        {"wint_t", CC_CT_INT},     {"wchar_t", CC_CT_INT},        {"size_t", -1},
        {"ssize_t", -2},           {"ptrdiff_t", -2},             {"intptr_t", -2},
        {"uintptr_t", -1},         {"char16_t", CC_CT_USHORT},    {"char32_t", CC_CT_UINT},
    };
    size_t i;

    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strlen(names[i].name) == len && strncmp(name, names[i].name, len) == 0) {
            if (out_type != NULL) {
                int kind = names[i].kind;
                if (kind < 0) {
                    int wide = cc_target_pointer_size() > 4;
                    kind = kind == -1 ? (wide ? CC_CT_ULONG : CC_CT_UINT) : (wide ? CC_CT_LONG : CC_CT_INT);
                }
                *out_type = cc_ct_basic((cc_ctype_kind_t)kind);
            }
            return 1;
        }
    }
    return 0;
}

static int is_declspec_ident(parser_t *p) {
    if (tok_is_ident(p, "_Atomic") || tok_is_ident(p, "_Thread_local") || tok_is_ident(p, "_Alignas") ||
        tok_is_ident(p, "_Noreturn") || tok_is_ident(p, "_BitInt") || tok_is_ident(p, "_Decimal32") ||
        tok_is_ident(p, "_Decimal64") || tok_is_ident(p, "_Decimal128") || tok_is_floatn_spelling(p)) {
        return 1;
    }
    /* __int128 is a reserved name, so gcc takes it in the ISO modes too */
    if (tok_is_ident(p, "__int128") || tok_is_ident(p, "__int128_t") || tok_is_ident(p, "__uint128_t")) {
        return 1;
    }
    if (parser_is_gnu_mode() &&
        (tok_is_ident(p, "__signed__") || tok_is_ident(p, "__signed") || tok_is_ident(p, "__unsigned__") ||
         tok_is_ident(p, "__unsigned") || tok_is_ident(p, "__auto_type"))) {
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
    return is_declspec_tok(p->tok.kind) || is_declspec_ident(p) ||
           (p->tok.kind == TOK_IDENT && typedef_find_visible_n(p, p->tok.start, p->tok.len) >= 0) ||
           (p->tok.kind == TOK_IDENT && builtin_typedef_type_n(p->tok.start, p->tok.len, NULL)) ||
           tok_is_gnu_attribute_kw(p) ||
           (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK);
}

static int is_type_name_start_after_lparen(parser_t *p) {
    parser_t q = *p;

    if (q.tok.kind != TOK_LPAREN) {
        return 0;
    }
    if (next_tok(&q) != 0) {
        return 0;
    }
    return is_declspec_start(&q);
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

static int parse_declspec(parser_t *p, const cc_ctype_t **out, int allow_void, const char *what, int *out_typedef,
                          int *out_saw_restrict, decl_attrs_t *out_attrs);

/**
 * What a list of declaration specifiers says, as declspec_accumulate()
 * collects it token by token for declspec_resolve() to make a type of.
 */
typedef struct {
    int storage_flags;
    int seen;
    int seen_type;
    int seen_void;
    int seen_bool;
    int seen_char;
    int seen_int;
    int seen_float;
    int seen_double;
    int seen_complex;
    int seen_imaginary;
    int seen_long;
    int seen_short;
    int seen_signed;
    int seen_unsigned;
    int seen_opaque_tag;
    int seen_typedef;
    int seen_alias;
    int seen_atomic;
    int seen_bitint;
    int seen_decimal;
    int seen_enum;
    int seen_restrict;
    int enum_known_type;
    int enum_has_values;
    long enum_min;
    long enum_max;
    const char *enum_tag_pending;
    size_t enum_tag_pending_len;
    int enum_defined;
    long bitint_width;
    int seen_int128; /* GNU __int128 */
    int seen_struct_id;
    const cc_ctype_t *enum_ctype; /* an enumeration's type, once known */
    int decimal_bits;
    const cc_ctype_t *alias_ctype; /* the type a typedef name, typeof or the like gave */
    const cc_ctype_t *spec;
} declspec_acc_t;

/**
 * Start an empty specifier list.
 *
 * @param acc  the list
 */
static void declspec_acc_init(declspec_acc_t *acc) {
    memset(acc, 0, sizeof(*acc));
    acc->seen_struct_id = -1;
    acc->decimal_bits = 64;
}

/**
 * Read declaration specifiers into @p acc: storage classes, type
 * specifiers and qualifiers, function and alignment specifiers and
 * attributes, up to the first token that is none of them, defining any
 * structure, union or enumeration they declare along the way.
 *
 * @param p          the parser, at the specifiers
 * @param acc        receives what they say
 * @param out_attrs  receives their attributes; may be NULL
 * @return           0, or -1 on error (diagnosed)
 */
static int declspec_accumulate(parser_t *p, declspec_acc_t *acc, decl_attrs_t *out_attrs) {
    while (1) {
        if (p->tok.kind == TOK_KW_EXTENSION) {
            acc->seen = 1;
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
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_alias = 1;
                acc->alias_ctype = cc_ct_basic(CC_CT_INT);
                acc->storage_flags |= CC_STORAGE_AUTO_TYPE;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (p->tok.len == strlen("__builtin_va_list") &&
                strncmp(p->tok.start, "__builtin_va_list", p->tok.len) == 0) {
                if (acc->seen_type) {
                    break;
                }
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_alias = 1;
                acc->alias_ctype = cc_ct_pointer(cc_ct_basic(CC_CT_VOID));
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_gnu_mode() && (tok_is_ident(p, "__signed__") || tok_is_ident(p, "__signed"))) {
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_signed = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_gnu_mode() && (tok_is_ident(p, "__unsigned__") || tok_is_ident(p, "__unsigned"))) {
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_unsigned = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (tok_is_ident(p, "__int128") || tok_is_ident(p, "__int128_t") || tok_is_ident(p, "__uint128_t")) {
                if (cc_target_pointer_size() < 8) {
                    /* as gcc: only 64-bit targets have it */
                    set_diag(p->diag, p->tok.line, p->tok.col, "__int128 is not supported on this target");
                    return -1;
                }
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_int128 = 1;
                if (tok_is_ident(p, "__uint128_t")) {
                    acc->seen_unsigned = 1;
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
                acc->seen = 1;
                acc->storage_flags |= CC_STORAGE_THREAD_LOCAL;
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
                acc->seen = 1;
                if (out_attrs != NULL) {
                    out_attrs->flags |= CC_ATTR_NORETURN;
                }
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (tok_is_ident(p, "_Alignas") || (parser_is_c23_or_newer() && tok_is_ident(p, "alignas"))) {
                const cc_ctype_t *aty = NULL;
                long align = 0;
                cc_expr_t *ae;
                if (!parser_is_c11_or_newer()) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "_Alignas requires C11 or newer");
                    return -1;
                }
                acc->seen = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                if (expect(p, TOK_LPAREN, "expected '(' after _Alignas/alignas") != 0) {
                    return -1;
                }
                if (is_declspec_start(p)) {
                    if (parse_type_name(p, "expected type name in _Alignas/alignas", &aty, NULL) != 0) {
                        return -1;
                    }
                    align = parser_ct_align(p, aty);
                    if (align <= 0) {
                        align = cc_target_pointer_size();
                    }
                } else {
                    ae = parse_cond(p);
                    if (ae == NULL) {
                        return -1;
                    }
                    if (eval_const_array_bound_expr(p, ae, &align) != 0 || align < 0) {
                        free_expr(ae);
                        set_diag(p->diag, p->tok.line, p->tok.col, "alignas requires a non-negative integer constant");
                        return -1;
                    }
                    free_expr(ae);
                }
                if (expect(p, TOK_RPAREN, "expected ')' after _Alignas/alignas") != 0) {
                    return -1;
                }
                if (out_attrs != NULL && align > 0) {
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
                acc->seen = 1;
                if (peek_kind(p) == TOK_LPAREN) {
                    const cc_ctype_t *aty = NULL;
                    if (acc->seen_type) {
                        break;
                    }
                    if (next_tok(p) != 0) {
                        return -1;
                    }
                    if (expect(p, TOK_LPAREN, "expected '(' after _Atomic") != 0) {
                        return -1;
                    }
                    if (parse_type_name(p, "expected type name in _Atomic(...)", &aty, NULL) != 0) {
                        return -1;
                    }
                    if (expect(p, TOK_RPAREN, "expected ')' after _Atomic(...)") != 0) {
                        return -1;
                    }
                    acc->seen_type = 1;
                    acc->seen_atomic = 1;
                    acc->seen_alias = 1;
                    acc->alias_ctype = aty;
                    continue;
                }
                acc->seen_atomic = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_c23_or_newer() && tok_is_ident(p, "bool")) {
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_bool = 1;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (parser_is_c23_or_newer() && tok_is_ident(p, "constexpr")) {
                acc->seen = 1;
                acc->storage_flags |= CC_STORAGE_STATIC;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            /* C23's decimal floating types; gcc takes them in every mode, as reserved identifiers */
            if (tok_is_ident(p, "_Decimal32") || tok_is_ident(p, "_Decimal64") || tok_is_ident(p, "_Decimal128")) {
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_decimal = 1;
                if (tok_is_ident(p, "_Decimal32")) {
                    acc->decimal_bits = 32;
                } else if (tok_is_ident(p, "_Decimal64")) {
                    acc->decimal_bits = 64;
                } else {
                    acc->decimal_bits = 128;
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
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_bitint = 1;
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
                acc->bitint_width = w;
                continue;
            }
            if ((parser_is_c23_or_newer() || parser_is_gnu_mode()) && tok_is_typeof_spelling(p)) {
                const cc_ctype_t *typeof_ctype = NULL;
                int drop_quals;
                if (acc->seen_type) {
                    break;
                }
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_alias = 1;
                drop_quals = tok_is_ident(p, "typeof_unqual") || tok_is_ident(p, "__typeof_unqual") ||
                                tok_is_ident(p, "__typeof_unqual__");
                if (next_tok(p) != 0) {
                    return -1;
                }
                if (expect(p, TOK_LPAREN, "expected '(' after typeof/typeof_unqual") != 0) {
                    return -1;
                }
                if (is_declspec_start(p)) {
                    if (parse_type_name(p, "expected type name in typeof", &typeof_ctype, NULL) != 0) {
                        return -1;
                    }
                } else {
                    cc_expr_t *te = parse_cond(p);
                    if (te == NULL) {
                        return -1;
                    }
                    typeof_ctype = parser_expr_ctype(p, te);
                    if (typeof_ctype == NULL) {
                        set_diag(p->diag, te->line, te->col, "cannot determine the type of typeof's operand");
                        free_expr(te);
                        return -1;
                    }
                    free_expr(te);
                }
                if (expect(p, TOK_RPAREN, "expected ')' after typeof/typeof_unqual") != 0) {
                    return -1;
                }
                acc->alias_ctype = drop_quals ? typeof_ctype->unqual : typeof_ctype;
                continue;
            }
            if (tok_is_floatn_spelling(p)) {
                cc_ctype_kind_t floatn_kind = CC_CT_LDOUBLE;
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_alias = 1;
                if (tok_is_ident(p, "_Float16")) {
                    floatn_kind = CC_CT_FLOAT16;
                } else if (tok_is_ident(p, "_Float32")) {
                    floatn_kind = CC_CT_FLOAT;
                } else if (tok_is_ident(p, "_Float64")) {
                    floatn_kind = CC_CT_DOUBLE;
                }
                acc->alias_ctype = cc_ct_basic(floatn_kind);
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            int tidx = typedef_find_visible_n(p, p->tok.start, p->tok.len);
            if (tidx >= 0) {
                if (acc->seen_type) {
                    break;
                }
                acc->seen = 1;
                acc->seen_type = 1;
                acc->seen_alias = 1;
                acc->alias_ctype = p->typedefs[tidx].ctype;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            {
                const cc_ctype_t *bty;
                if (builtin_typedef_type_n(p->tok.start, p->tok.len, &bty)) {
                    if (acc->seen_type) {
                        break;
                    }
                    acc->seen = 1;
                    acc->seen_type = 1;
                    acc->seen_alias = 1;
                    acc->alias_ctype = bty;
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
        acc->seen = 1;
        switch (p->tok.kind) {
        case TOK_KW_VOID:
            acc->seen_void = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_BOOL:
            acc->seen_bool = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_CHAR:
            acc->seen_char = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_INT:
            acc->seen_int = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_FLOAT:
            acc->seen_float = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_DOUBLE:
            acc->seen_double = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_COMPLEX:
            acc->seen_complex = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_IMAGINARY:
            acc->seen_imaginary = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_LONG:
            acc->seen_long++;
            acc->seen_type = 1;
            break;
        case TOK_KW_SHORT:
            acc->seen_short = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_SIGNED:
            acc->seen_signed = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_UNSIGNED:
            acc->seen_unsigned = 1;
            acc->seen_type = 1;
            break;
        case TOK_KW_TYPEDEF:
            acc->seen_typedef = 1;
            break;
        case TOK_KW_STATIC:
            acc->storage_flags |= CC_STORAGE_STATIC;
            break;
        case TOK_KW_EXTERN:
            acc->storage_flags |= CC_STORAGE_EXTERN;
            break;
        case TOK_KW_AUTO:
            if (parser_is_c23_or_newer() && !acc->seen_type) {
                acc->seen_type = 1;
                acc->seen_alias = 1;
                acc->alias_ctype = cc_ct_basic(CC_CT_INT);
                acc->storage_flags |= CC_STORAGE_AUTO_TYPE;
            } else {
                acc->storage_flags |= CC_STORAGE_AUTO;
            }
            break;
        case TOK_KW_REGISTER:
            acc->storage_flags |= CC_STORAGE_REGISTER;
            break;
        case TOK_KW_INLINE:
            acc->storage_flags |= CC_STORAGE_INLINE;
            break;
        case TOK_KW_CONST:
            acc->storage_flags |= CC_STORAGE_CONST;
            break;
        case TOK_KW_VOLATILE:
            acc->storage_flags |= CC_STORAGE_VOLATILE;
            break;
        case TOK_KW_RESTRICT:
            acc->seen_restrict = 1;
            acc->storage_flags |= CC_STORAGE_RESTRICT;
            break;
        case TOK_KW_STRUCT:
        case TOK_KW_UNION: {
            int sid;
            int brace_depth = 0;
            const char *tag_start = NULL;
            size_t tag_len = 0;
            int is_union = (p->tok.kind == TOK_KW_UNION);
            decl_attrs_t struct_attrs;
            decl_attrs_reset(&struct_attrs);
            acc->seen_type = 1;
            acc->seen_opaque_tag = 1;
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
            acc->seen_struct_id = sid;
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
            p->structs[sid].member_cap = 0;
            free(p->structs[sid].layout);
            p->structs[sid].layout = NULL;
            p->structs[sid].layout_count = 0;
            p->structs[sid].layout_cap = 0;
            p->structs[sid].is_union = is_union;
            p->structs[sid].has_flexible_array = 0;
            if (next_tok(p) != 0) {
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            while (p->tok.kind != TOK_RBRACE) {
                int mtypedef = 0;
                const cc_ctype_t *mspec = NULL;
                decl_attrs_t mspec_attrs;
                long mspec_align = 0;
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
                if (is_static_assert_tok(p)) {
                    if (parse_static_assert_decl(p, 1) != 0) {
                        decl_attrs_clear(&struct_attrs);
                        return -1;
                    }
                    continue;
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
                decl_attrs_reset(&mspec_attrs);
                if (parse_declspec(p, &mspec, 1, "expected member declaration type", &mtypedef, NULL, &mspec_attrs) !=
                    0) {
                    decl_attrs_clear(&mspec_attrs);
                    decl_attrs_clear(&struct_attrs);
                    return -1;
                }
                /* `_Alignas(T) char x;`: the specifiers' alignment applies to each member they declare */
                mspec_align = (mspec_attrs.flags & CC_ATTR_ALIGNED) != 0 ? mspec_attrs.align : 0;
                decl_attrs_clear(&mspec_attrs);
                if (mtypedef) {
                    decl_attrs_clear(&struct_attrs);
                    set_diag(p->diag, p->tok.line, p->tok.col, "typedef is not allowed inside struct member list");
                    return -1;
                }
                for (;;) {
                    const cc_ctype_t *melem; /* the member's type, or an array member's element type */
                    char *mname = NULL;
                    member_decl_t md;
                    long arr_count;
                    int arr_saw;
                    int arr_unsized;
                    long msize;
                    long malign;
                    long muser_align = 0;
                    int mindex;
                    int is_bitfield = 0;
                    int bit_width = -1;
                    int bit_storage_bits = 0;
                    int bit_signed = 0;
                    long bit_storage_size = 0;
                    int skip_member_decl = 0;
                    decl_attrs_t mdecl_attrs;
                    decl_attrs_reset(&mdecl_attrs);
                    if (parse_member_decl(p, mspec, &md) != 0) {
                        return -1;
                    }
                    mname = md.name;
                    melem = md.elem;
                    arr_saw = md.is_array;
                    arr_unsized = md.unsized;
                    arr_count = md.count;
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
                    }
                    if (skip_decl_gnu_suffix(p, &mdecl_attrs) != 0) {
                        free(mname);
                        decl_attrs_clear(&mdecl_attrs);
                        return -1;
                    }
                    if (is_bitfield && p->tok.kind != TOK_COMMA && p->tok.kind != TOK_SEMI) {
                        set_diag(p->diag, p->tok.line, p->tok.col, "malformed bit-field width expression");
                        free(mname);
                        decl_attrs_clear(&mdecl_attrs);
                        return -1;
                    }
                    if (mspec_align > 0) {
                        mdecl_attrs.flags |= CC_ATTR_ALIGNED;
                        if (mspec_align > mdecl_attrs.align) {
                            mdecl_attrs.align = mspec_align;
                        }
                    }
                    if (mname == NULL && !is_bitfield) {
                        if (md.ctype->unqual->kind == CC_CT_RECORD) {
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
                    msize = parser_ct_size(p, melem);
                    malign = parser_ct_align(p, melem);
                    if (msize < 0) {
                        msize = cc_target_pointer_size();
                    }
                    if (malign <= 0) {
                        if (msize == 0) {
                            malign = 1;
                        } else {
                            malign = cc_target_pointer_size();
                        }
                    }
                    if ((mdecl_attrs.flags & CC_ATTR_PACKED) != 0) {
                        malign = 1;
                    }
                    if ((mdecl_attrs.flags & CC_ATTR_ALIGNED) != 0) {
                        muser_align = mdecl_attrs.align;
                    }

                    if (is_bitfield) {
                        if (!parser_is_bitfield_base_type(melem)) {
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
                        bit_storage_size = parser_ct_size(p, melem);
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
                        /*
                         * Enum bit-field signedness is implementation-defined.
                         * Keep enum bit-fields zero-extending by default so
                         * positive-only enumerators with the top value bit set
                         * are not sign-extended on load.
                         */
                        bit_signed = parser_bitfield_is_signed(melem);
                        msize = bit_storage_size;
                    } else {
                        if (arr_saw) {
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
                    }

                    /* placed when the structure is complete (struct_layout) */
                    mindex = -1;
                    if (!skip_member_decl && mname != NULL) {
                        mindex = (int)p->structs[sid].member_count;
                        if (struct_member_push(p, sid, mname, md.ctype, 0, msize, is_bitfield, bit_width, 0,
                                               bit_storage_bits, bit_signed, bit_storage_size) != 0) {
                            free(mname);
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                        free(mname);
                    } else if (skip_member_decl && mname == NULL && md.ctype->unqual->kind == CC_CT_RECORD &&
                               p->structs[md.ctype->unqual->id].complete) {
                        mindex = (int)p->structs[sid].member_count;
                        if (struct_member_push(p, sid, NULL, md.ctype->unqual, 0, msize, is_bitfield, bit_width, 0,
                                               bit_storage_bits, bit_signed, bit_storage_size) != 0) {
                            decl_attrs_clear(&mdecl_attrs);
                            return -1;
                        }
                    }
                    if (struct_layout_push(p, sid, mindex, is_bitfield ? melem : NULL, is_bitfield ? 0 : msize,
                                           malign, muser_align, is_bitfield ? bit_width : -1) != 0) {
                        set_diag(p->diag, p->tok.line, p->tok.col, "out of memory laying out a structure");
                        decl_attrs_clear(&mdecl_attrs);
                        return -1;
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
            } else if (struct_layout(p, sid, 0, 0) != 0) {
                decl_attrs_clear(&struct_attrs);
                return -1;
            }
            decl_attrs_clear(&struct_attrs);
            continue;
        }
        case TOK_KW_ENUM: {
            long enum_next = 0;
            const char *enum_tag_start = NULL;
            size_t enum_tag_len = 0;

            acc->seen_type = 1;
            acc->seen_int = 1;
            acc->seen_enum = 1;
            acc->seen_opaque_tag = 0;
            acc->seen_struct_id = -1;
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
                const cc_ctype_t *uty = NULL;
                if (next_tok(p) != 0) {
                    return -1;
                }
                if (parse_type_name(p, "expected enum underlying type", &uty, NULL) != 0) {
                    return -1;
                }
                if (!cc_ct_is_integer(uty) || uty->unqual->kind == CC_CT_BITINT || uty->unqual->kind == CC_CT_UBITINT) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "enum underlying type must be an integer type");
                    return -1;
                }
                acc->enum_known_type = 1;
                acc->enum_ctype = uty->unqual;
            }
            if (p->tok.kind != TOK_LBRACE) {
                if (!acc->enum_known_type && enum_tag_start != NULL && enum_tag_len > 0) {
                    int eidx = enum_tag_find_n(p, enum_tag_start, enum_tag_len);
                    if (eidx >= 0) {
                        acc->enum_known_type = 1;
                        acc->enum_ctype = p->enum_tags[eidx].ctype;
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
                        parser_const_value_t ev;
                        long v = 0;
                        if (next_tok(p) != 0) {
                            return -1;
                        }
                        enum_expr = parse_cond(p);
                        if (enum_expr == NULL) {
                            return -1;
                        }
                        if (eval_const_expr_value(p, enum_expr, &ev) != 0) {
                            free_expr(enum_expr);
                            set_diag(p->diag, p->tok.line, p->tok.col,
                                     "enum value must be an integer constant expression");
                            return -1;
                        }
                        free_expr(enum_expr);
                        if (cc_const_is_unsigned(&ev)) {
                            uint64_t u = ev.bits;
                            v = u > (uint64_t)LONG_MAX ? LONG_MAX : (long)u;
                        } else {
                            int64_t s = (int64_t)ev.bits;
                            if (s < (int64_t)LONG_MIN) {
                                v = LONG_MIN;
                            } else if (s > (int64_t)LONG_MAX) {
                                v = LONG_MAX;
                            } else {
                                v = (long)s;
                            }
                        }
                        enum_cur = v;
                        enum_next = v;
                        p->enum_consts[p->enum_const_count - 1].value = enum_next;
                        p->enum_consts[p->enum_const_count - 1].bits = ev.bits;
                        p->enum_consts[p->enum_const_count - 1].ctype = ev.type;
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
                    if (!acc->enum_has_values) {
                        acc->enum_min = enum_cur;
                        acc->enum_max = enum_cur;
                        acc->enum_has_values = 1;
                    } else {
                        if (enum_cur < acc->enum_min) {
                            acc->enum_min = enum_cur;
                        }
                        if (enum_cur > acc->enum_max) {
                            acc->enum_max = enum_cur;
                        }
                    }
                    if (enum_next < LONG_MAX) {
                        enum_next++;
                    }
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
                acc->enum_tag_pending = enum_tag_start;
                acc->enum_tag_pending_len = enum_tag_len;
            }
            acc->enum_defined = 1;
            continue;
        }
        default:
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * Make the type a list of declaration specifiers names (C11 6.7.2): check
 * the combination and give the basic, enumerated, record, complex,
 * decimal, _BitInt or wide type it is, or the one a typedef name gave.
 *
 * @param p           the parser, for diagnostics and the types it makes
 * @param acc         the specifiers; acc->spec receives the type
 * @param allow_void  nonzero if `void` may be named
 * @param what        the message when there were no specifiers
 * @param out_attrs   their attributes (`packed` sizes an enumeration); may be NULL
 * @return            0, or -1 on error (diagnosed)
 */
static int declspec_resolve(parser_t *p, declspec_acc_t *acc, int allow_void, const char *what,
                            const decl_attrs_t *out_attrs) {
    if (!acc->seen) {
        char near_tok[40];
        size_t n = 0;
        near_tok[0] = '\0';
        if (p->tok.start != NULL && p->tok.len > 0) {
            n = p->tok.len < sizeof(near_tok) - 1 ? p->tok.len : sizeof(near_tok) - 1;
            memcpy(near_tok, p->tok.start, n);
            near_tok[n] = '\0';
        }
        if (what != NULL && near_tok[0] != '\0') {
            char msg[160];
            snprintf(msg, sizeof(msg), "%s near '%s'", what, near_tok);
            set_diag(p->diag, p->tok.line, p->tok.col, msg);
        } else {
            set_diag(p->diag, p->tok.line, p->tok.col, what);
        }
        return -1;
    }
    if (!acc->seen_type) {
        if (!cc_lang_at_least(1999) && acc->seen) {
            /*
             * Implicit int in declaration-specifier lists such as
             * `extern foo();`: C89 has it, C99 removed it, and gcc (14
             * and later) rejects it in every later mode, GNU ones too.
             */
            acc->seen_int = 1;
            acc->seen_type = 1;
        } else {
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
    }

    if (acc->seen_alias) {
        if (acc->seen_opaque_tag) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid typedef type combination");
            return -1;
        }
        if (acc->seen_void || acc->seen_bool || acc->seen_char || acc->seen_int || acc->seen_float || acc->seen_double || acc->seen_long || acc->seen_short ||
            acc->seen_signed || acc->seen_unsigned) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid typedef type combination");
            return -1;
        }
        if (!allow_void && acc->alias_ctype->kind == CC_CT_VOID) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid use of void in declaration specifiers");
            return -1;
        }
        acc->spec = acc->alias_ctype;
        return 0;
    }

    if (acc->seen_int128) {
        /* GNU __int128, signed unless unsigned is said: a 16-aligned structure of two limbs */
        int wsid = parser_wide_struct(p, 128, acc->seen_unsigned, 1);

        if (wsid < 0) {
            set_diag(p->diag, p->tok.line, p->tok.col, "out of memory making __int128");
            return -1;
        }
        acc->spec = cc_ct_record(wsid);
        return 0;
    }

    if (acc->seen_bitint) {
        if (acc->bitint_width <= 0) {
            set_diag(p->diag, p->tok.line, p->tok.col, "_BitInt width must be positive");
            return -1;
        }
        if (!acc->seen_unsigned && acc->bitint_width < 2) {
            /* the sign bit needs one of its own */
            set_diag(p->diag, p->tok.line, p->tok.col, "signed _BitInt width must be at least 2");
            return -1;
        }
        if (acc->bitint_width > 65535) {
            /* BITINT_MAXWIDTH (__BITINT_MAXWIDTH__), as gcc's */
            set_diag(p->diag, p->tok.line, p->tok.col, "_BitInt width exceeds BITINT_MAXWIDTH (65535)");
            return -1;
        }
        if (acc->bitint_width > 64) {
            /* wider than a register: a structure of limbs (parser_wide_struct()) */
            int wsid = parser_wide_struct(p, acc->bitint_width, acc->seen_unsigned, 0);

            if (wsid < 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "out of memory making a _BitInt type");
                return -1;
            }
            acc->spec = cc_ct_record(wsid);
            return 0;
        }
        acc->spec = cc_ct_bitint(acc->bitint_width, acc->seen_unsigned);
        return 0;
    }

    if (acc->seen_void) {
        if (!allow_void || acc->seen_bool || acc->seen_char || acc->seen_int || acc->seen_float || acc->seen_double || acc->seen_long || acc->seen_short) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid use of void in declaration specifiers");
            return -1;
        }
        acc->spec = cc_ct_basic(CC_CT_VOID);
        return 0;
    }

    if (acc->seen_signed && acc->seen_unsigned) {
        set_diag(p->diag, p->tok.line, p->tok.col, "conflicting signed/unsigned in declaration specifiers");
        return -1;
    }
    if (acc->seen_short && acc->seen_long > 0) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid short/long combination in declaration specifiers");
        return -1;
    }

    if ((acc->seen_signed || acc->seen_unsigned) &&
        (acc->seen_float || acc->seen_double || acc->seen_decimal || acc->seen_complex || acc->seen_imaginary || acc->seen_bool || acc->seen_void)) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid signed/unsigned type combination");
        return -1;
    }
    if (acc->seen_opaque_tag) {
        if (acc->seen_void || acc->seen_bool || acc->seen_char || acc->seen_int || acc->seen_float || acc->seen_double || acc->seen_long || acc->seen_short ||
            acc->seen_complex || acc->seen_imaginary || acc->seen_signed || acc->seen_unsigned) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid aggregate declaration specifiers");
            return -1;
        }
        acc->spec = cc_ct_record(acc->seen_struct_id);
        return 0;
    }

    if (acc->seen_complex || acc->seen_imaginary) {
        if (acc->seen_void || acc->seen_bool || acc->seen_char || acc->seen_int || acc->seen_short || acc->seen_signed || acc->seen_unsigned ||
            acc->seen_opaque_tag || acc->seen_decimal) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid complex/imaginary type combination");
            return -1;
        }
        if (acc->seen_long > 0 && !acc->seen_double) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid complex/imaginary long type combination");
            return -1;
        }
        if (acc->seen_complex) {
            /*
             * A complex value is its two parts, as a structure of them.  All
             * three kinds are made at once: an operation can yield a kind no
             * declaration names (float _Complex times a double).
             */
            int csid = parser_complex_struct(p, acc->seen_float ? CC_CT_FLOAT
                                                           : (acc->seen_long > 0 ? CC_CT_LDOUBLE : CC_CT_DOUBLE));

            if (parser_complex_struct(p, CC_CT_FLOAT) < 0 || parser_complex_struct(p, CC_CT_DOUBLE) < 0 ||
                parser_complex_struct(p, CC_CT_LDOUBLE) < 0) {
                csid = -1;
            }
            if (csid < 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "out of memory making a complex type");
                return -1;
            }
        }
        acc->spec = cc_ct_complex(acc->seen_complex ? CC_CT_COMPLEX : CC_CT_IMAGINARY,
                             cc_ct_basic(acc->seen_float ? CC_CT_FLOAT : (acc->seen_long > 0 ? CC_CT_LDOUBLE : CC_CT_DOUBLE)));
        return 0;
    }

    if (acc->seen_decimal) {
        /* a structure holding the BID encoding (parser_decimal_struct()) */
        int dsid = parser_decimal_struct(p, acc->decimal_bits);

        if (dsid < 0) {
            set_diag(p->diag, p->tok.line, p->tok.col, "out of memory making a decimal floating type");
            return -1;
        }
        acc->spec = cc_ct_record(dsid);
        return 0;
    }

    if (acc->seen_double) {
        if (acc->seen_long > 1) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid long double type combination");
            return -1;
        }
        acc->spec = cc_ct_basic(acc->seen_long ? CC_CT_LDOUBLE : CC_CT_DOUBLE);
    } else if (acc->seen_float) {
        acc->spec = cc_ct_basic(CC_CT_FLOAT);
    } else if (acc->seen_bool) {
        acc->spec = cc_ct_basic(CC_CT_BOOL);
    } else if (acc->seen_char) {
        acc->spec = cc_ct_basic(acc->seen_unsigned ? CC_CT_UCHAR : acc->seen_signed ? CC_CT_SCHAR : CC_CT_CHAR);
    } else if (acc->seen_long > 1) {
        acc->spec = cc_ct_basic(acc->seen_unsigned ? CC_CT_ULLONG : CC_CT_LLONG);
    } else if (acc->seen_long > 0) {
        acc->spec = cc_ct_basic(acc->seen_unsigned ? CC_CT_ULONG : CC_CT_LONG);
    } else if (acc->seen_short) {
        acc->spec = cc_ct_basic(acc->seen_unsigned ? CC_CT_USHORT : CC_CT_SHORT);
    } else if (acc->seen_enum) {
        if (!acc->enum_known_type) {
            int packed_enum = (out_attrs != NULL && (out_attrs->flags & CC_ATTR_PACKED) != 0);
            if (packed_enum && acc->enum_has_values) {
                acc->enum_ctype = cc_ct_basic(packed_enum_kind_from_range(acc->enum_min, acc->enum_max));
            } else {
                /* int-sized and wider enumerations are all the one enumerated type */
                acc->enum_ctype = cc_ct_enum(-1);
            }
        }
        switch (acc->enum_ctype->kind) {
        case CC_CT_INT:
        case CC_CT_UINT:
        case CC_CT_LLONG:
        case CC_CT_ULLONG:
            acc->spec = cc_ct_enum(-1);
            break;
        default:
            acc->spec = acc->enum_ctype;
            break;
        }
        if (acc->enum_defined && acc->enum_tag_pending != NULL && acc->enum_tag_pending_len > 0) {
            if (enum_tag_set(p, acc->enum_tag_pending, acc->enum_tag_pending_len, acc->spec, 1) != 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "out of memory tracking enum tag");
                return -1;
            }
        }
    } else {
        /* `int`, `signed`, `unsigned`, or C89's implicit int */
        acc->spec = cc_ct_basic(acc->seen_unsigned ? CC_CT_UINT : CC_CT_INT);
    }
    return 0;
}

/**
 * Parse declaration specifiers (C11 6.7): storage classes, type
 * specifiers and qualifiers, function specifiers, alignment specifiers
 * and attributes, defining any structure, union or enumeration they
 * declare along the way.
 *
 * The type they give is also left in parser_t.last_spec_ctype, and the
 * storage class bits in parser_t.last_storage.
 *
 * @param p                 the parser, at the specifiers
 * @param out               receives the type; may be NULL
 * @param allow_void        nonzero if `void` may be named
 * @param what              the message when there are no specifiers
 * @param out_typedef       receives whether `typedef` was among them; NULL
 *                          if it may not be
 * @param out_saw_restrict  receives whether `restrict` was; may be NULL
 * @param out_attrs         receives their attributes; may be NULL
 * @return                  0, or -1 on error (diagnosed)
 */
static int parse_declspec(parser_t *p, const cc_ctype_t **out, int allow_void, const char *what, int *out_typedef,
                          int *out_saw_restrict, decl_attrs_t *out_attrs) {
    declspec_acc_t acc;

    p->last_storage = 0;
    p->last_spec_ctype = NULL;
    if (out_typedef != NULL) {
        *out_typedef = 0;
    }
    if (out_saw_restrict != NULL) {
        *out_saw_restrict = 0;
    }
    if (out_attrs != NULL) {
        out_attrs->flags = 0;
        out_attrs->align = 0;
        out_attrs->section = NULL;
        out_attrs->alias = NULL;
    }

    declspec_acc_init(&acc);
    if (declspec_accumulate(p, &acc, out_attrs) != 0 ||
        declspec_resolve(p, &acc, allow_void, what, out_attrs) != 0) {
        return -1;
    }
    if (acc.spec == NULL) {
        set_diag(p->diag, p->tok.line, p->tok.col, "out of memory building a type");
        return -1;
    }
    if (out_typedef != NULL) {
        *out_typedef = acc.seen_typedef;
    } else if (acc.seen_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
        return -1;
    }
    if (acc.seen_atomic && (acc.spec->quals & CC_CQ_ATOMIC) == 0) {
        cc_ctype_kind_t k = acc.spec->unqual->kind;
        if (k == CC_CT_RECORD || k == CC_CT_COMPLEX) {
            set_diag(p->diag, p->tok.line, p->tok.col, "_Atomic of aggregate type is not yet supported");
            return -1;
        }
        acc.spec = cc_ct_qualified(acc.spec, CC_CQ_ATOMIC);
    }
    p->last_spec_ctype = acc.spec;
    if (out != NULL) {
        *out = acc.spec;
    }
    if (out_saw_restrict != NULL) {
        *out_saw_restrict = acc.seen_restrict;
    }
    p->last_storage = acc.storage_flags;
    return 0;
}

static int is_decl_qual_tok(cc_tok_kind_t k) {
    return k == TOK_KW_CONST || k == TOK_KW_VOLATILE || k == TOK_KW_RESTRICT;
}

static int is_decl_qual_at_token(parser_t *p) {
    return is_decl_qual_tok(p->tok.kind) || tok_is_ident(p, "_Atomic");
}

/**
 * The type of function @p f as declared, for the parser.
 *
 * @return the type, or NULL where its declaration did not record it
 */
const cc_ctype_t *parser_function_ctype(const cc_function_t *f) {
    const cc_ctype_t *params[64];
    size_t i;

    if (f->ret_ctype == NULL || f->param_count > sizeof(params) / sizeof(params[0])) {
        return NULL;
    }
    for (i = 0; i < f->param_count; ++i) {
        if (f->params[i].ctype == NULL) {
            return NULL;
        }
        params[i] = f->params[i].ctype;
    }
    return cc_ct_function(f->ret_ctype, params, f->param_count, f->is_variadic, f->has_prototype);
}



/**
 * The size of type @p t as the parser knows it so far: its structures
 * are the parser's, which the translation unit receives only at the end.
 *
 * @return the size in bytes, or -1 if not known
 */
long parser_ct_size(const parser_t *p, const cc_ctype_t *t) {
    cc_translation_unit_t view;

    memset(&view, 0, sizeof(view));
    view.structs = p->structs;
    view.struct_count = p->struct_count;
    return cc_ct_size(t, &view);
}

/**
 * The alignment of type @p t as the parser knows it so far
 * (parser_ct_size()).
 *
 * @return the alignment in bytes, or -1 if not known
 */
static long parser_ct_align(const parser_t *p, const cc_ctype_t *t) {
    cc_translation_unit_t view;

    if (t == NULL) {
        return -1;
    }
    memset(&view, 0, sizeof(view));
    view.structs = p->structs;
    view.struct_count = p->struct_count;
    return cc_ct_align(t, &view);
}

/**
 * Give expression @p e the type @p t.
 */
static void parser_set_ctype(cc_expr_t *e, const cc_ctype_t *t) {
    e->ctype = t;
}

/**
 * A new constant of kind @p kind (CC_EXPR_INT or CC_EXPR_FLOAT) and
 * type @p t, its value zero.
 *
 * @return the node, or NULL when out of memory
 */
static cc_expr_t *new_const_expr(cc_expr_kind_t kind, const cc_ctype_t *t) {
    cc_expr_t *e = new_expr(kind);

    if (e != NULL) {
        parser_set_ctype(e, t);
    }
    return e;
}

/** The type of sizeof and _Alignof: size_t. */
static const cc_ctype_t *parser_size_ctype(void) {
    return cc_ct_basic(cc_target_pointer_size() >= 8 ? CC_CT_ULONG : CC_CT_UINT);
}

static int parse_array_extent(parser_t *p, long *out_n, int *out_const_n, cc_expr_t **out_bound_expr) {
    cc_expr_t *bound = NULL;
    long n = 1;
    int is_const = 0;

    if (p == NULL || out_n == NULL || out_const_n == NULL || p->tok.kind != TOK_LBRACK) {
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    if (out_bound_expr != NULL) {
        *out_bound_expr = NULL;
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
        if (n < 0 || (!parser_is_gnu_mode() && n == 0)) {
            set_diag(p->diag, p->tok.line, p->tok.col, "array bound must be positive");
            free_expr(bound);
            return -1;
        }
        if (n > 0 || (parser_is_gnu_mode() && n == 0)) {
            is_const = 1;
        }
    }
    if (expect(p, TOK_RBRACK, "expected ']' after array declarator") != 0) {
        free_expr(bound);
        return -1;
    }
    if (is_const) {
        free_expr(bound);
        bound = NULL;
    } else if (out_bound_expr == NULL) {
        free_expr(bound);
        bound = NULL;
    } else {
        *out_bound_expr = bound;
    }
    *out_n = is_const ? n : (n > 0 ? n : 1);
    *out_const_n = is_const;
    return 0;
}

/**
 * Parse a parameter's array declarator, `[` to `]`, with the qualifiers and
 * `static` a parameter's bound may carry, or `[*]`.
 *
 * @param p                the parser, at the `[`
 * @param out_n            receives the bound, or 1 if it is not a constant
 * @param out_const_n      receives whether it is a constant
 * @param io_saw_restrict  set if the brackets hold `restrict`
 * @param out_bound_expr   receives a bound that is not a constant (owned),
 *                         or NULL; it gives a pointer to a variable-length
 *                         array its run-time size
 * @return                 0, or -1 on error (diagnosed)
 */
static int parse_param_array_extent(parser_t *p, long *out_n, int *out_const_n, int *io_saw_restrict,
                                    cc_expr_t **out_bound_expr) {
    long n = 1;
    int is_const = 0;
    cc_expr_t *bound;

    if (p == NULL || out_n == NULL || out_const_n == NULL || out_bound_expr == NULL || p->tok.kind != TOK_LBRACK) {
        return -1;
    }
    *out_bound_expr = NULL;
    if (next_tok(p) != 0) {
        return -1;
    }
    while (p->tok.kind == TOK_KW_STATIC || p->tok.kind == TOK_KW_CONST || p->tok.kind == TOK_KW_VOLATILE ||
           p->tok.kind == TOK_KW_RESTRICT) {
        if (p->tok.kind == TOK_KW_RESTRICT && io_saw_restrict != NULL) {
            *io_saw_restrict = 1;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    if (p->tok.kind == TOK_RBRACK) {
        if (expect(p, TOK_RBRACK, "expected ']' after parameter array declarator") != 0) {
            return -1;
        }
        *out_n = 1;
        *out_const_n = 0;
        return 0;
    }
    if (p->tok.kind == TOK_STAR) {
        if (next_tok(p) != 0) {
            return -1;
        }
        if (expect(p, TOK_RBRACK, "expected ']' after parameter array declarator") != 0) {
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
        if (n < 0 || (!parser_is_gnu_mode() && n == 0)) {
            set_diag(p->diag, p->tok.line, p->tok.col, "array bound must be positive");
            free_expr(bound);
            return -1;
        }
        if (n > 0 || (parser_is_gnu_mode() && n == 0)) {
            is_const = 1;
        }
    }
    if (expect(p, TOK_RBRACK, "expected ']' after parameter array declarator") != 0) {
        free_expr(bound);
        return -1;
    }
    if (is_const) {
        free_expr(bound);
    } else {
        *out_bound_expr = bound;
    }
    *out_n = is_const ? n : (n > 0 ? n : 1);
    *out_const_n = is_const;
    return 0;
}

static int tok_is_ident(parser_t *p, const char *s) {
    size_t n = strlen(s);
    return p->tok.kind == TOK_IDENT && p->tok.len == n && strncmp(p->tok.start, s, n) == 0;
}

static int tok_is_gnu_attribute_kw(parser_t *p) {
    return tok_is_ident(p, "__attribute__") || tok_is_ident(p, "__attribute");
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
    a->format_index = 0;
    a->format_first_to_check = 0;
    a->section = NULL;
    a->alias = NULL;
    a->vector_bytes = 0;
    a->vector_elems = 0;
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
    a->format_index = 0;
    a->format_first_to_check = 0;
    a->vector_bytes = 0;
    a->vector_elems = 0;
}

static int decl_attrs_merge(decl_attrs_t *dst, const decl_attrs_t *src) {
    char *sec_dup = NULL;
    char *alias_dup = NULL;
    if (dst == NULL || src == NULL) {
        return 0;
    }
    if ((src->flags & CC_ATTR_SECTION) != 0 && src->section != NULL) {
        sec_dup = cc_strndup(src->section, strlen(src->section));
        if (sec_dup == NULL) {
            return -1;
        }
    }
    if ((src->flags & CC_ATTR_ALIAS) != 0 && src->alias != NULL) {
        alias_dup = cc_strndup(src->alias, strlen(src->alias));
        if (alias_dup == NULL) {
            free(sec_dup);
            return -1;
        }
    }
    dst->flags |= src->flags;
    if (src->align > dst->align) {
        dst->align = src->align;
    }
    if ((src->flags & CC_ATTR_FORMAT) != 0 && src->format_index > 0) {
        dst->format_index = src->format_index;
    }
    if ((src->flags & CC_ATTR_FORMAT) != 0) {
        dst->format_first_to_check = src->format_first_to_check;
    }
    if (src->vector_bytes > 0) {
        dst->vector_bytes = src->vector_bytes;
    }
    if (src->vector_elems > 0) {
        dst->vector_elems = src->vector_elems;
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
        return cc_strndup("", 0);
    }
    return cc_strndup(tok->start + begin, end - begin);
}

static int parser_hex_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static unsigned parser_decode_escape_unit(const unsigned char *s, size_t n, size_t *adv_out) {
    unsigned v = 0;
    size_t i = 0;

    if (adv_out != NULL) {
        *adv_out = 0;
    }
    if (n == 0) {
        return 0;
    }
    if (s[0] == 'x') {
        int hv;
        int seen = 0;
        i = 1;
        while (i < n && (hv = parser_hex_value(s[i])) >= 0) {
            seen = 1;
            v = (v << 4) | (unsigned)hv;
            i++;
        }
        if (!seen) {
            v = 'x';
        }
        if (adv_out != NULL) {
            *adv_out = i;
        }
        return v & 0xffu;
    }
    if (s[0] >= '0' && s[0] <= '7') {
        int digits = 0;
        while (i < n && digits < 3 && s[i] >= '0' && s[i] <= '7') {
            v = (v << 3) | (unsigned)(s[i] - '0');
            i++;
            digits++;
        }
        if (adv_out != NULL) {
            *adv_out = i;
        }
        return v & 0xffu;
    }
    switch (s[0]) {
    case 'a': v = '\a'; break;
    case 'b': v = '\b'; break;
    case 'f': v = '\f'; break;
    case 'n': v = '\n'; break;
    case 'r': v = '\r'; break;
    case 't': v = '\t'; break;
    case 'v': v = '\v'; break;
    case '\\': v = '\\'; break;
    case '\'': v = '\''; break;
    case '"': v = '"'; break;
    case '?': v = '?'; break;
    default: v = s[0]; break;
    }
    if (adv_out != NULL) {
        *adv_out = 1;
    }
    return v & 0xffu;
}

static char *canonicalize_narrow_string_token(const cc_token_t *tok) {
    const unsigned char *s;
    size_t begin = 0;
    size_t end;
    size_t i;
    size_t w = 0;
    size_t cap;
    char *out;

    if (tok == NULL || tok->kind != TOK_STR || tok->start == NULL) {
        return NULL;
    }
    end = tok->len;
    if (tok->len >= 2 && tok->start[0] == '"' && tok->start[tok->len - 1] == '"') {
        begin = 1;
        end = tok->len - 1;
    }
    cap = (end - begin) * 4 + 3;
    out = (char *)malloc(cap);
    if (out == NULL) {
        return NULL;
    }
    s = (const unsigned char *)tok->start;
    out[w++] = '"';
    i = begin;
    while (i < end) {
        unsigned ch;
        size_t adv = 1;
        if (s[i] == '\\' && i + 1 < end && (s[i + 1] == 'u' || s[i + 1] == 'U')) {
            /* a universal character name: the character's UTF-8 bytes, the execution character set's encoding */
            int digits = s[i + 1] == 'u' ? 4 : 8;
            unsigned long cp = 0;
            unsigned char bytes[4];
            int nb;
            int k;

            i += 2;
            for (k = 0; k < digits && i < end && parser_hex_value(s[i]) >= 0; ++k, ++i) {
                cp = (cp << 4) | (unsigned long)parser_hex_value(s[i]);
            }
            if (cp < 0x80) {
                bytes[0] = (unsigned char)cp;
                nb = 1;
            } else if (cp < 0x800) {
                bytes[0] = (unsigned char)(0xC0 | (cp >> 6));
                bytes[1] = (unsigned char)(0x80 | (cp & 0x3F));
                nb = 2;
            } else if (cp < 0x10000) {
                bytes[0] = (unsigned char)(0xE0 | (cp >> 12));
                bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                bytes[2] = (unsigned char)(0x80 | (cp & 0x3F));
                nb = 3;
            } else {
                bytes[0] = (unsigned char)(0xF0 | (cp >> 18));
                bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
                bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                bytes[3] = (unsigned char)(0x80 | (cp & 0x3F));
                nb = 4;
            }
            for (k = 0; k < nb; ++k) {
                snprintf(out + w, cap - w, "\\%03o", bytes[k]);
                w += 4;
            }
            continue;
        }
        if (s[i] == '\\') {
            i++;
            if (i >= end) {
                break;
            }
            ch = parser_decode_escape_unit(s + i, end - i, &adv);
            if (adv == 0) {
                adv = 1;
            }
            i += adv;
        } else {
            ch = s[i++];
        }
        snprintf(out + w, cap - w, "\\%03o", ch & 0xffu);
        w += 4;
    }
    out[w++] = '"';
    out[w] = '\0';
    return out;
}

/**
 * The element type of a string literal token, from its encoding prefix:
 * char for none and `u8`, wchar_t (int) for `L`, char16_t (unsigned
 * short) for `u`, char32_t (unsigned int) for `U`.
 */
static const cc_ctype_t *string_token_char_type(const parser_t *p, const cc_token_t *tok) {
    if (p != NULL && tok != NULL && tok->kind == TOK_STR && tok->start != NULL && tok->start > p->lx.src) {
        char c1 = tok->start[-1]; /* `u8"..."` ends its prefix in '8': a char string */

        if (c1 == 'L') {
            return cc_ct_basic(CC_CT_INT);
        }
        if (c1 == 'u') {
            return cc_ct_basic(CC_CT_USHORT);
        }
        if (c1 == 'U') {
            return cc_ct_basic(CC_CT_UINT);
        }
    }
    return cc_ct_basic(CC_CT_CHAR);
}

long parser_string_literal_array_bound(const cc_expr_t *e) {
    return cc_string_literal_units(e);
}

static long parser_struct_next_init_member_index(const cc_struct_def_t *sd, size_t member_idx) {
    size_t next = member_idx + 1;
    long off;

    if (sd == NULL || member_idx >= sd->member_count) {
        return next;
    }
    if (sd->members[member_idx].size == 0) {
        return next;
    }
    off = sd->members[member_idx].offset;
    /* a union's members share its storage; a structure's bit-fields may share a member's bytes */
    while (sd->is_union && next < sd->member_count && sd->members[next].offset == off) {
        next++;
    }
    return next;
}

static long parser_struct_scalar_slots_for_init(parser_t *p, int struct_id, int depth) {
    const cc_struct_def_t *sd;
    long slots = 0;
    size_t i;

    if (p == NULL || struct_id < 0 || (size_t)struct_id >= p->struct_count || depth > 32) {
        return -1;
    }
    sd = &p->structs[struct_id];
    for (i = 0; i < sd->member_count; ++i) {
        const cc_ctype_t *t = sd->members[i].ctype;
        long cnt = 1;
        long sub = 1;

        if (sd->has_flexible_array && i + 1 == sd->member_count) {
            continue;
        }
        /* an array takes its elements' slots, each level's bound (1 where unknown) times over */
        for (; t != NULL && t->kind == CC_CT_ARRAY; t = t->of) {
            long n = t->count > 0 ? t->count : 1;

            if (cnt > LONG_MAX / n) {
                return -1;
            }
            cnt *= n;
        }
        if (t != NULL && t->unqual->kind == CC_CT_RECORD) {
            sub = parser_struct_scalar_slots_for_init(p, t->unqual->id, depth + 1);
        }
        if (sub < 0 || cnt > LONG_MAX / sub || slots > LONG_MAX - cnt * sub) {
            return -1;
        }
        slots += cnt * sub;
    }
    return slots > 0 ? slots : 1;
}

static long parser_infer_struct_array_len_from_init_list(parser_t *p, int struct_id, const cc_expr_t *init) {
    const cc_struct_def_t *sd;
    long slots;
    size_t cur = 0;
    long elems = 0;
    size_t next_member = 0;

    if (p == NULL || struct_id < 0 || (size_t)struct_id >= p->struct_count || init == NULL || init->kind != CC_EXPR_INIT_LIST) {
        return -1;
    }
    sd = &p->structs[struct_id];
    slots = parser_struct_scalar_slots_for_init(p, struct_id, 0);
    if (slots <= 0) {
        return -1;
    }
    while (cur < init->arg_count) {
        const cc_expr_t *raw = init->args[cur];
        if (raw != NULL && raw->kind == CC_EXPR_INIT_LIST) {
            elems++;
            cur++;
            next_member = 0;
            continue;
        }
        if (raw != NULL && raw->kind == CC_EXPR_CAST && raw->ctype != NULL && raw->ctype->unqual->kind == CC_CT_RECORD &&
            raw->ctype->unqual->id == struct_id && raw->lhs != NULL && raw->lhs->kind == CC_EXPR_INIT_LIST) {
            elems++;
            cur++;
            next_member = 0;
            continue;
        }
        {
            long consumed = 0;
            size_t member_idx = next_member;
            while (cur < init->arg_count && consumed < slots) {
                const cc_expr_t *it = init->args[cur];
                if (consumed > 0 && it != NULL && it->kind == CC_EXPR_INIT_LIST) {
                    break;
                }
                consumed++;
                cur++;
                if (member_idx < sd->member_count && !sd->is_union) {
                    member_idx = parser_struct_next_init_member_index(sd, member_idx);
                }
            }
            if (consumed == 0) {
                cur++;
            }
            elems++;
            next_member = 0;
        }
    }
    return elems > 0 ? elems : 1;
}

/**
 * The bound an initializer list gives an array of unknown bound whose
 * elements have type @p elem: a lone string literal's length for an
 * array of characters, the structures a list of brace-elided members
 * makes up, else the number of items.
 *
 * @param p     the parser
 * @param elem  the element type
 * @param init  the CC_EXPR_INIT_LIST
 * @return      the bound, or 0 if it cannot be told
 */
long parser_init_list_bound_ct(parser_t *p, const cc_ctype_t *elem, const cc_expr_t *init) {
    int sid;

    if (elem == NULL || init == NULL || init->kind != CC_EXPR_INIT_LIST || init->arg_count == 0) {
        return 0;
    }
    if (init->arg_count == 1 && init->args[0] != NULL && init->args[0]->kind == CC_EXPR_STR) {
        int wide = cc_string_literal_is_wide(init->args[0]);
        long n = parser_string_literal_array_bound(init->args[0]);

        if (n > 0 && (wide ? cc_ct_is_integer(elem) : cc_ct_is_character(elem))) {
            return n;
        }
    }
    sid = elem->unqual->kind == CC_CT_RECORD ? elem->unqual->id : -1;
    if (sid >= 0) {
        long n = parser_infer_struct_array_len_from_init_list(p, sid, init);

        return n > 0 ? n : 0;
    }
    return (long)init->arg_count;
}

static int append_string_piece(char **dst, const char *piece) {
    size_t a;
    size_t b;
    char *next;
    if (dst == NULL || piece == NULL) {
        return -1;
    }
    if (*dst == NULL) {
        *dst = cc_strndup(piece, strlen(piece));
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

/**
 * Write a string literal's stored spelling, escapes decoded, as text for
 * a diagnostic: printable ASCII as itself, anything else as `?`.
 *
 * @param stored  the literal as the lexer stores it (quotes included)
 * @param out     receives the text, NUL-terminated
 * @param cap     the size of @p out
 */
static void format_string_for_diag(const char *stored, char *out, size_t cap) {
    size_t n = strlen(stored);
    size_t i = 0;
    size_t j = 0;

    while (i < n && j + 1 < cap) {
        unsigned c;
        if (stored[i] == '\\' && i + 1 < n) {
            size_t adv = 0;
            c = parser_decode_escape_unit((const unsigned char *)stored + i + 1, n - i - 1, &adv);
            i += 1 + (adv > 0 ? adv : 1);
        } else {
            c = (unsigned char)stored[i++];
        }
        out[j++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    out[j] = '\0';
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
            op.name = cc_strndup(p->tok.start, p->tok.len);
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
        name = local_label_dup_mapped_n(p, p->tok.start, p->tok.len);
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
    int is_volatile_tok;
    int is_goto_tok;
    int is_inline_tok;

    s->kind = CC_STMT_ASM;
    if (next_tok(p) != 0) {
        return -1;
    }
    while (1) {
        is_volatile_tok =
            tok_is_ident(p, "__volatile__") || tok_is_ident(p, "__volatile") || tok_is_ident(p, "volatile") ||
            p->tok.kind == TOK_KW_VOLATILE;
        is_goto_tok =
            tok_is_ident(p, "__goto__") || tok_is_ident(p, "__goto") || tok_is_ident(p, "goto") ||
            p->tok.kind == TOK_KW_GOTO;
        is_inline_tok = tok_is_ident(p, "__inline__") || tok_is_ident(p, "__inline") || tok_is_ident(p, "inline") ||
                        p->tok.kind == TOK_KW_INLINE;
        if (!is_volatile_tok && !is_goto_tok && !is_inline_tok) {
            break;
        }
        if (is_goto_tok) {
            s->asm_is_goto = 1;
        } else if (is_volatile_tok) {
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
        if (parse_gnu_attr_arguments(p, &ignored_num, &ignored_has_num, NULL, NULL, &ignored_str) != 0) {
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

static int parse_gnu_attr_arguments(parser_t *p, long *out_num, int *out_has_num, long *out_num2, int *out_has_num2,
                                    char **out_str) {
    int depth = 1;
    if (out_has_num != NULL) {
        *out_has_num = 0;
    }
    if (out_num != NULL) {
        *out_num = 0;
    }
    if (out_has_num2 != NULL) {
        *out_has_num2 = 0;
    }
    if (out_num2 != NULL) {
        *out_num2 = 0;
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
            } else if (out_has_num2 != NULL && out_num2 != NULL && !*out_has_num2 && p->tok.kind == TOK_NUM) {
                *out_has_num2 = 1;
                *out_num2 = p->tok.num;
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

static int parse_gnu_format_attr_arguments(parser_t *p, long *out_num, int *out_has_num, long *out_num2,
                                           int *out_has_num2) {
    int depth = 0;
    cc_expr_t *expr = NULL;

    if (out_has_num != NULL) {
        *out_has_num = 0;
    }
    if (out_has_num2 != NULL) {
        *out_has_num2 = 0;
    }
    if (out_num != NULL) {
        *out_num = 0;
    }
    if (out_num2 != NULL) {
        *out_num2 = 0;
    }
    if (p->tok.kind != TOK_LPAREN) {
        return 0;
    }
    if (next_tok(p) != 0) {
        return -1;
    }

    while (p->tok.kind != TOK_EOF) {
        if (p->tok.kind == TOK_LPAREN) {
            depth++;
        } else if (p->tok.kind == TOK_RPAREN) {
            if (depth == 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "format attribute expects three arguments");
                return -1;
            }
            depth--;
        } else if (p->tok.kind == TOK_COMMA && depth == 0) {
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    if (p->tok.kind != TOK_COMMA) {
        set_diag(p->diag, p->tok.line, p->tok.col, "format attribute expects three arguments");
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }

    expr = parse_cond(p);
    if (expr == NULL) {
        return -1;
    }
    if (out_num != NULL && out_has_num != NULL && eval_const_array_bound_expr(p, expr, out_num) == 0) {
        *out_has_num = 1;
    }
    free_expr(expr);
    expr = NULL;
    if (p->tok.kind != TOK_COMMA) {
        set_diag(p->diag, p->tok.line, p->tok.col, "format attribute expects third argument");
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }

    expr = parse_cond(p);
    if (expr == NULL) {
        return -1;
    }
    if (out_num2 != NULL && out_has_num2 != NULL && eval_const_array_bound_expr(p, expr, out_num2) == 0) {
        *out_has_num2 = 1;
    }
    free_expr(expr);
    expr = NULL;
    if (p->tok.kind != TOK_RPAREN) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected ')' after format attribute arguments");
        return -1;
    }
    return next_tok(p);
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
    int is_constructor;
    int is_destructor;
    int is_transparent_union;
    int is_vector_size;
    int is_ext_vector_type;
    int is_may_alias;
    int is_mode;
    int has_num = 0;
    int has_num2 = 0;
    long num = 0;
    long num2 = 0;
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
    is_constructor = tok_is_gnu_attr_name(p, "constructor");
    is_destructor = tok_is_gnu_attr_name(p, "destructor");
    is_transparent_union = tok_is_gnu_attr_name(p, "transparent_union");
    is_vector_size = tok_is_gnu_attr_name(p, "vector_size");
    is_ext_vector_type = tok_is_gnu_attr_name(p, "ext_vector_type");
    is_may_alias = tok_is_gnu_attr_name(p, "may_alias");
    is_mode = tok_is_gnu_attr_name(p, "mode");
    any_known = is_aligned || is_section || is_packed || is_deprecated || is_noreturn || is_unused || is_used ||
                is_always_inline ||
                is_noinline || is_hot || is_cold || is_format || is_nonnull || is_malloc_fn || is_alias || is_weak ||
                is_flatten || is_target || is_visibility || is_tls_model || is_cleanup || is_constructor ||
                is_destructor || is_transparent_union ||
                is_vector_size || is_ext_vector_type || is_may_alias || is_mode;

    if (next_tok(p) != 0) {
        return -1;
    }
    if (p->tok.kind == TOK_LPAREN) {
        int rc = is_format ? parse_gnu_format_attr_arguments(p, &num, &has_num, &num2, &has_num2)
                           : parse_gnu_attr_arguments(p, &num, &has_num, &num2, &has_num2, &sec);
        if (rc != 0) {
            free(sec);
            return -1;
        }
    }
    if ((is_vector_size || is_ext_vector_type) && (!has_num || num <= 0)) {
        free(sec);
        set_diag(p->diag, attr_line, attr_col, "a vector type's size must be a positive integer constant");
        return -1;
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
            if (has_num) {
                out_attrs->format_index = num;
            }
            if (has_num2) {
                out_attrs->format_first_to_check = num2;
            }
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
        if (is_constructor) {
            out_attrs->flags |= CC_ATTR_CONSTRUCTOR;
        }
        if (is_destructor) {
            out_attrs->flags |= CC_ATTR_DESTRUCTOR;
        }
        if (is_transparent_union) {
            out_attrs->flags |= CC_ATTR_TRANSPARENT_UNION;
        }
        if (is_may_alias) {
            out_attrs->flags |= CC_ATTR_MAY_ALIAS;
        }
        if (is_aligned) {
            long align = (has_num && num > 0) ? num : cc_target_pointer_size();
            out_attrs->flags |= CC_ATTR_ALIGNED;
            if (align > out_attrs->align) {
                out_attrs->align = align;
            }
        }
        if (is_vector_size) {
            out_attrs->flags |= CC_ATTR_VECTOR_SIZE;
            out_attrs->vector_bytes = num;
        }
        if (is_ext_vector_type) {
            out_attrs->flags |= CC_ATTR_VECTOR_SIZE;
            out_attrs->vector_elems = num;
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
        /* Only treat __asm__ as a declarator suffix when it has the form
         * __asm__("string"). Bare __asm__ followed by anything else is the
         * start of an asm statement (e.g. after a label) and must be left
         * for the statement parser. */
        if (peek_kind(p) != TOK_LPAREN) {
            break;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
        if (skip_balanced_parens(p) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ---- declarators ------------------------------------------------------ */

/** One step of a declarator: what it wraps around the type inside it. */
typedef enum {
    DECL_OP_POINTER,  /**< `*`, with its qualifiers */
    DECL_OP_ARRAY,    /**< `[N]` */
    DECL_OP_FUNCTION  /**< `(parameters)` */
} decl_op_kind_t;

/** A declarator step. */
typedef struct {
    decl_op_kind_t kind;
    unsigned quals;                  /**< pointer: CC_CQ_* */
    long count;                      /**< array: the bound, -1 if not a constant */
    cc_expr_t *bound;                /**< array: the bound expression when not a constant (owned) */
    const cc_ctype_t **params;       /**< function: the adjusted parameter types (owned) */
    size_t param_count;              /**< function: how many */
    int variadic;                    /**< function: ends in `...` */
    int prototyped;                  /**< function: has a parameter list */
} decl_op_t;

/**
 * A parsed declarator: its steps, in the order they apply to the base
 * type, and its name if it has one.
 */
typedef struct {
    decl_op_t *ops;
    size_t count;
    size_t cap;
    char *name;        /**< the declared name, or NULL for an abstract declarator */
    int nested;        /**< its direct part is a parenthesized declarator */
    int inner_arrays;  /**< array suffixes inside that parenthesized declarator */
    int outer_arrays;  /**< array suffixes after it (or after the name) */
    int saw_restrict;  /**< a parameter's array bound held `restrict` (`int a[restrict]`) */
    /**
     * A function definition's: called at the parameter list that directly
     * follows the name, wherever the name is nested, to parse it -- `(`
     * through `)` -- into the function being defined.  NULL otherwise.
     */
    int (*name_params)(parser_t *p, void *ctx);
    void *name_params_ctx; /**< passed to name_params */
} declarator_t;

/** How parse_declarator_into() reads a declarator. */
enum {
    DECL_ABSTRACT = 1u,       /**< the name may be left out */
    DECL_STOP_AT_PARAMS = 2u, /**< leave a parameter list right after a plain name unparsed */
    DECL_PARAM = 4u,          /**< a parameter's: `[static N]` bounds, `int (x)` names x */
    DECL_MEMBER = 8u          /**< a member's: any identifier is the name, which a bit-field may leave out */
};

static int parse_declarator_into(parser_t *p, declarator_t *d, unsigned flags);

/**
 * Free a declarator step's owned parts.
 *
 * @param op  the step
 */
static void decl_op_free(decl_op_t *op) {
    free(op->params);
    free_expr(op->bound);
    op->params = NULL;
    op->bound = NULL;
}

/**
 * Release a declarator's storage.
 *
 * @param d  the declarator; may be zeroed
 */
static void declarator_free(declarator_t *d) {
    size_t i;

    for (i = 0; i < d->count; ++i) {
        decl_op_free(&d->ops[i]);
    }
    free(d->ops);
    free(d->name);
    memset(d, 0, sizeof(*d));
}

/**
 * Append a step to a declarator.
 *
 * @param d   the declarator
 * @param op  the step; its params and bound, if any, now belong to @p d
 * @return    0, or -1 when out of memory (the step's parts are freed)
 */
static int declarator_push(declarator_t *d, const decl_op_t *op) {
    if (d->count == d->cap) {
        size_t ncap = d->cap == 0 ? 8 : d->cap * 2;
        decl_op_t *next = (decl_op_t *)realloc(d->ops, ncap * sizeof(*next));
        if (next == NULL) {
            decl_op_t copy = *op;
            decl_op_free(&copy);
            return -1;
        }
        d->ops = next;
        d->cap = ncap;
    }
    d->ops[d->count++] = *op;
    return 0;
}

/**
 * Build the type a declarator gives its base type.
 *
 * @param d     the declarator
 * @param base  the type from the declaration specifiers
 * @return      the type, or NULL when out of memory
 */
static const cc_ctype_t *declarator_type(const declarator_t *d, const cc_ctype_t *base) {
    const cc_ctype_t *t = base;
    size_t i;

    for (i = 0; i < d->count && t != NULL; ++i) {
        const decl_op_t *op = &d->ops[i];
        switch (op->kind) {
        case DECL_OP_POINTER:
            t = cc_ct_qualified(cc_ct_pointer(t), op->quals);
            break;
        case DECL_OP_ARRAY:
            t = cc_ct_array(t, op->count);
            break;
        case DECL_OP_FUNCTION:
            t = cc_ct_function(t, op->params, op->param_count, op->variadic, op->prototyped);
            break;
        }
    }
    return t;
}

/**
 * Read the qualifiers and attributes after a `*`.
 *
 * @param p          the parser, at the token after the `*`
 * @param out_quals  receives the CC_CQ_* bits
 * @return           0, or -1 on a syntax error
 */
static int parse_pointer_quals(parser_t *p, unsigned *out_quals) {
    *out_quals = 0;
    while (1) {
        if (p->tok.kind == TOK_KW_CONST) {
            *out_quals |= CC_CQ_CONST;
        } else if (p->tok.kind == TOK_KW_VOLATILE) {
            *out_quals |= CC_CQ_VOLATILE;
        } else if (p->tok.kind == TOK_KW_RESTRICT) {
            *out_quals |= CC_CQ_RESTRICT;
        } else if (tok_is_ident(p, "_Atomic")) {
            *out_quals |= CC_CQ_ATOMIC;
        } else if (is_decl_qual_at_token(p)) {
            /* another spelling of a qualifier this encoding does not keep */
        } else if (tok_is_gnu_attribute_kw(p) || (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK)) {
            if (skip_decl_gnu_suffix(p, NULL) != 0) {
                return -1;
            }
            continue;
        } else {
            return 0;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
}

/**
 * Parse one parameter declaration and give its type as the function
 * sees it: an array becomes a pointer to its element, a function a
 * pointer to itself, and top-level qualifiers go.
 *
 * @param p    the parser, at the parameter's declaration specifiers
 * @param out  receives the adjusted type
 * @return     0, or -1 on error (diagnosed)
 */
static int parse_param_type(parser_t *p, const cc_ctype_t **out) {
    declarator_t d;
    const cc_ctype_t *spec;
    const cc_ctype_t *t;

    memset(&d, 0, sizeof(d));
    if (parse_declspec(p, &spec, 1, "expected parameter type", NULL, NULL, NULL) != 0) {
        return -1;
    }
    if (parse_declarator_into(p, &d, DECL_ABSTRACT | DECL_PARAM) != 0) {
        declarator_free(&d);
        return -1;
    }
    t = declarator_type(&d, spec);
    declarator_free(&d);
    if (t == NULL) {
        set_diag(p->diag, p->tok.line, p->tok.col, "out of memory building a parameter type");
        return -1;
    }
    *out = cc_ct_decay(t);
    return 0;
}

/**
 * Parse a function declarator's parameter list, after its `(`, through
 * its `)`: a prototype, `(void)`, `()` or an identifier list.
 *
 * @param p   the parser, at the token after `(`
 * @param op  receives the parameter types and flags
 * @return    0, or -1 on error (diagnosed; nothing to free)
 */
static int parse_param_list(parser_t *p, decl_op_t *op) {
    size_t cap = 0;

    memset(op, 0, sizeof(*op));
    op->kind = DECL_OP_FUNCTION;
    if (p->tok.kind == TOK_RPAREN) {
        return next_tok(p);
    }
    if (p->tok.kind == TOK_IDENT && !is_declspec_start(p)) {
        /* an identifier list: names only, so no prototype */
        while (p->tok.kind != TOK_RPAREN && p->tok.kind != TOK_EOF) {
            if (next_tok(p) != 0) {
                return -1;
            }
        }
        return expect(p, TOK_RPAREN, "expected ')' after parameter names");
    }
    op->prototyped = 1;
    while (1) {
        const cc_ctype_t *t;

        if (p->tok.kind == TOK_ELLIPSIS) {
            op->variadic = 1;
            if (next_tok(p) != 0) {
                break;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after '...'") != 0) {
                break;
            }
            return 0;
        }
        if (parse_param_type(p, &t) != 0) {
            break;
        }
        if (t->kind == CC_CT_VOID && op->param_count == 0 && p->tok.kind == TOK_RPAREN) {
            /* (void): a prototype with no parameters */
            return next_tok(p);
        }
        if (op->param_count == cap) {
            size_t ncap = cap == 0 ? 4 : cap * 2;
            const cc_ctype_t **next = (const cc_ctype_t **)realloc(op->params, ncap * sizeof(*next));
            if (next == NULL) {
                set_diag(p->diag, p->tok.line, p->tok.col, "out of memory parsing parameters");
                break;
            }
            op->params = next;
            cap = ncap;
        }
        op->params[op->param_count++] = t;
        if (p->tok.kind == TOK_COMMA) {
            if (next_tok(p) != 0) {
                break;
            }
            continue;
        }
        if (expect(p, TOK_RPAREN, "expected ',' or ')' in parameter list") != 0) {
            break;
        }
        return 0;
    }
    free(op->params);
    op->params = NULL;
    return -1;
}

/**
 * Skip GNU and C23 attributes (and `__asm__` labels) inside a declarator
 * -- the ones followed by more of it: a suffix, or the `)` closing a
 * nested declarator.  Those after its end belong to the declaration,
 * which reads them itself.
 *
 * @param p  the parser
 * @return   0, or -1 on a syntax error
 */
static int skip_attrs_within_declarator(parser_t *p) {
    parser_t q = *p;

    q.diag = NULL;
    if (skip_decl_gnu_suffix(&q, NULL) != 0) {
        return 0;
    }
    if (q.tok.start == p->tok.start) {
        return 0;
    }
    if (q.tok.kind == TOK_LBRACK || q.tok.kind == TOK_LPAREN || q.tok.kind == TOK_RPAREN) {
        return skip_decl_gnu_suffix(p, NULL);
    }
    return 0;
}

/**
 * Whether the `(` at the parser starts a nested declarator, as in
 * `int (*f)(void)`, rather than a parameter list, as in `int (void)`.
 * A parenthesized identifier that is not a typedef name is a nested
 * declarator unless the declarator must be abstract; in a parameter, as
 * `int (x)`, it names the parameter (C11 6.7.6.3p11).  In a member any
 * parenthesized identifier is one, as members may share typedef names.
 *
 * @param p      the parser, at `(`
 * @param flags  DECL_* as parse_declarator_into() was given them
 * @return       1 if it starts a nested declarator, else 0
 */
static int starts_nested_declarator(parser_t *p, unsigned flags) {
    parser_t q = *p;

    q.diag = NULL;
    if (next_tok(&q) != 0) {
        return 0;
    }
    while (tok_is_gnu_attribute_kw(&q) || (q.tok.kind == TOK_LBRACK && peek_kind(&q) == TOK_LBRACK)) {
        if (skip_decl_gnu_suffix(&q, NULL) != 0) {
            return 0;
        }
    }
    if (is_ptr_declarator_tok(q.tok.kind) || q.tok.kind == TOK_LPAREN || q.tok.kind == TOK_LBRACK) {
        return 1;
    }
    if (q.tok.kind != TOK_IDENT || ((flags & DECL_ABSTRACT) != 0 && (flags & (DECL_PARAM | DECL_MEMBER)) == 0)) {
        return 0;
    }
    return (flags & DECL_MEMBER) != 0 || !is_declspec_start(&q);
}

/**
 * Parse a declarator (C11 6.7.6), or an abstract one (6.7.7), into the
 * steps it applies to its base type.  `*`s bind looser than the suffixes
 * after them, and a parenthesized declarator binds looser still, so
 * `int *(*f[2])(void)` is an array of two pointers to functions
 * returning pointers to int.
 *
 * @param p      the parser, at the declarator
 * @param d      receives the steps (appended), the name, and the shape
 *               (nested, inner_arrays, outer_arrays, saw_restrict)
 * @param flags  DECL_ABSTRACT if the name may be left out;
 *               DECL_STOP_AT_PARAMS to leave a parameter list that
 *               directly follows a plain name unparsed, as `f` in
 *               `int f(void)`, for the caller to declare a function;
 *               DECL_PARAM for a parameter's declarator
 * @return       0, or -1 on error (diagnosed)
 */
static int parse_declarator_into(parser_t *p, declarator_t *d, unsigned flags) {
    int abstract = (flags & DECL_ABSTRACT) != 0;
    declarator_t ptrs;   /* the pointer steps before the direct declarator, outermost first */
    declarator_t suffix; /* the array and function suffixes after it, in source order */
    declarator_t inner;
    size_t i;
    int rc = -1;

    memset(&inner, 0, sizeof(inner));
    memset(&ptrs, 0, sizeof(ptrs));
    memset(&suffix, 0, sizeof(suffix));
    while (is_ptr_declarator_tok(p->tok.kind)) {
        decl_op_t op;

        memset(&op, 0, sizeof(op));
        op.kind = DECL_OP_POINTER;
        if (next_tok(p) != 0 || parse_pointer_quals(p, &op.quals) != 0 || declarator_push(&ptrs, &op) != 0) {
            goto out;
        }
    }
    if (skip_decl_gnu_suffix(p, NULL) != 0) {
        goto out;
    }
    /* the direct declarator: a nested declarator, a name, or nothing */
    if (p->tok.kind == TOK_LPAREN && starts_nested_declarator(p, flags)) {
        inner.name_params = d->name_params;
        inner.name_params_ctx = d->name_params_ctx;
        if (next_tok(p) != 0 ||
            parse_declarator_into(p, &inner, flags & (DECL_ABSTRACT | DECL_PARAM | DECL_MEMBER)) != 0 ||
            expect(p, TOK_RPAREN, "expected ')' in declarator") != 0) {
            goto out;
        }
        d->nested = 1;
        d->inner_arrays = inner.inner_arrays + inner.outer_arrays;
        d->saw_restrict |= inner.saw_restrict;
    } else if (p->tok.kind == TOK_IDENT && (!abstract || (flags & DECL_MEMBER) != 0 || !is_declspec_start(p))) {
        /* after the specifiers an identifier is the name, even one that is also a typedef name */
        inner.name = cc_strndup(p->tok.start, p->tok.len);
        if (inner.name == NULL || next_tok(p) != 0) {
            goto out;
        }
    } else if (!abstract) {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier after declaration type");
        goto out;
    }
    if (skip_attrs_within_declarator(p) != 0) {
        goto out;
    }
    /*
     * The suffixes apply before the pointers and the nested declarator do,
     * the last one first: `[2][3]` is an array of 2 arrays of 3.
     */
    {
        int stop = (flags & DECL_STOP_AT_PARAMS) && !d->nested && inner.name != NULL && p->tok.kind == TOK_LPAREN;

        if (d->name_params != NULL && inner.name != NULL && (!d->nested || inner.count == 0) &&
            p->tok.kind == TOK_LPAREN) {
            /*
             * The defined function's own parameter list -- after the name, or
             * after a parenthesized name, `(f)(void)` -- its caller parses it.
             */
            decl_op_t op;

            memset(&op, 0, sizeof(op));
            op.kind = DECL_OP_FUNCTION;
            op.prototyped = 1;
            if (d->name_params(p, d->name_params_ctx) != 0 || declarator_push(&suffix, &op) != 0 ||
                skip_attrs_within_declarator(p) != 0) {
                goto out;
            }
        }
        while (!stop && (p->tok.kind == TOK_LBRACK || p->tok.kind == TOK_LPAREN)) {
            decl_op_t op;

            memset(&op, 0, sizeof(op));
            if (p->tok.kind == TOK_LBRACK) {
                long n = -1;
                int const_n = 0;
                if (flags & DECL_PARAM) {
                    /* `[static N]`, `[const]`, `[restrict N]`: what a parameter's bound may hold */
                    if (parse_param_array_extent(p, &n, &const_n, &d->saw_restrict, &op.bound) != 0) {
                        goto out;
                    }
                } else if (parse_array_extent(p, &n, &const_n, &op.bound) != 0) {
                    goto out;
                }
                if (const_n) {
                    free_expr(op.bound);
                    op.bound = NULL;
                }
                op.kind = DECL_OP_ARRAY;
                op.count = const_n ? n : -1;
                ++d->outer_arrays;
            } else {
                if (next_tok(p) != 0 || parse_param_list(p, &op) != 0) {
                    goto out;
                }
            }
            if (declarator_push(&suffix, &op) != 0 || skip_attrs_within_declarator(p) != 0) {
                goto out;
            }
        }
        /* pointer steps own nothing, so they are copied; each suffix moves to d */
        for (i = 0; i < ptrs.count; ++i) {
            if (declarator_push(d, &ptrs.ops[i]) != 0) {
                goto out;
            }
        }
        while (suffix.count > 0) {
            decl_op_t op = suffix.ops[--suffix.count];

            if (declarator_push(d, &op) != 0) {
                goto out;
            }
        }
    }
    for (i = 0; i < inner.count; ++i) {
        int pushed = declarator_push(d, &inner.ops[i]);
        /* either way the step's parts now belong to d, or are freed */
        inner.ops[i].params = NULL;
        inner.ops[i].bound = NULL;
        if (pushed != 0) {
            goto out;
        }
    }
    if (inner.name != NULL) {
        d->name = inner.name;
        inner.name = NULL;
    }
    rc = 0;
out:
    declarator_free(&inner);
    declarator_free(&ptrs);
    declarator_free(&suffix);
    return rc;
}

/**
 * Take the run-time bounds of the array type @p arr a declarator builds
 * (its levels of unknown bound), out of the declarator and into @p out.
 *
 * The ops apply from the base outward, so the dimensions, outermost
 * first, are the array ops at the end of the list, last first: in
 * `int a[n][m]` the ops are [m] then [n].  @p skip outermost ops are
 * passed over first: the pointer of `int (*p)[m]`, or the outer array of
 * a parameter `int a[n][m]`, which is adjusted to a pointer to `int[m]`
 * (its bound is never part of the type).  The run of array ops ends at
 * any other op.
 *
 * @param d     the declarator; the bounds taken are set to NULL in it
 * @param arr   the array type the dimensions are the levels of
 * @param skip  the number of outermost ops that are not dimensions
 * @param out   receives each dimension's bound; left empty when every
 *              bound is a constant
 * @return      0, or -1 when out of memory
 */
static int take_vla_bounds(declarator_t *d, const cc_ctype_t *arr, size_t skip, cc_bounds_t *out) {
    const cc_ctype_t *t = arr;
    size_t levels = 0;
    size_t k;
    int any = 0;

    memset(out, 0, sizeof(*out));
    /* the run of array levels the declarator spells, and whether one is a run-time bound */
    while (t != NULL && t->kind == CC_CT_ARRAY && levels + skip < d->count &&
           d->ops[d->count - 1 - skip - levels].kind == DECL_OP_ARRAY) {
        any |= d->ops[d->count - 1 - skip - levels].bound != NULL && t->count < 0;
        ++levels;
        t = t->of;
    }
    if (!any) {
        return 0;
    }
    out->at = (cc_expr_t **)calloc(levels, sizeof(out->at[0]));
    if (out->at == NULL) {
        return -1;
    }
    out->count = levels;
    for (k = 0, t = arr; k < levels; ++k, t = t->of) {
        decl_op_t *op = &d->ops[d->count - 1 - skip - k];

        if (op->bound != NULL && t->count < 0) {
            out->at[k] = op->bound;
            op->bound = NULL;
        }
    }
    return 0;
}

/**
 * Parse a type name (C11 6.7.7): declaration specifiers and an abstract
 * declarator.
 *
 * @param p               the parser, at the type name
 * @param what            the message for a missing type
 * @param out             receives the type
 * @param out_vla_bounds  when the type is an array with bounds that are
 *                        not constants, `int[n][m]`, receives them (see
 *                        cc_bounds_t); empty otherwise.  May be NULL.
 * @return                0, or -1 on error (diagnosed)
 */
static int parse_type_name(parser_t *p, const char *what, const cc_ctype_t **out, cc_bounds_t *out_vla_bounds) {
    declarator_t d;
    const cc_ctype_t *spec;
    const cc_ctype_t *t;
    size_t line = p->tok.line;
    size_t col = p->tok.col;

    memset(&d, 0, sizeof(d));
    if (parse_declspec(p, &spec, 1, what, NULL, NULL, NULL) != 0) {
        return -1;
    }
    if (parse_declarator_into(p, &d, DECL_ABSTRACT) != 0) {
        declarator_free(&d);
        return -1;
    }
    if (d.name != NULL) {
        set_diag(p->diag, line, col, "a type name declares no identifier");
        declarator_free(&d);
        return -1;
    }
    t = declarator_type(&d, spec);
    if (t == NULL) {
        declarator_free(&d);
        set_diag(p->diag, line, col, "out of memory building a type");
        return -1;
    }
    if (out_vla_bounds != NULL) {
        memset(out_vla_bounds, 0, sizeof(*out_vla_bounds));
        if (t->kind == CC_CT_ARRAY && take_vla_bounds(&d, t, 0, out_vla_bounds) != 0) {
            declarator_free(&d);
            set_diag(p->diag, line, col, "out of memory recording array bounds");
            return -1;
        }
    }
    declarator_free(&d);
    *out = t;
    return 0;
}

static cc_expr_t *new_expr(cc_expr_kind_t kind) {
    cc_expr_t *e = (cc_expr_t *)calloc(1, sizeof(*e));
    if (e != NULL) {
        e->kind = kind;
        e->line = 0;
        e->col = 0;
        e->member_is_arrow = 0;
        e->member_offset = 0;
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
    free(e->generic_ctypes);
    free(e->generic_is_default);
    for (i = 0; i < e->stmt_expr_count; ++i) {
        free_stmt(&e->stmt_expr_stmts[i]);
    }
    free(e->stmt_expr_stmts);
    free(e);
}

void cc_expr_free(cc_expr_t *e) {
    free_expr(e);
}

cc_expr_t *cc_bounds_at(const cc_bounds_t *b, size_t k) {
    return b != NULL && k < b->count ? b->at[k] : NULL;
}

int cc_bounds_any(const cc_bounds_t *b) {
    size_t k;

    for (k = 0; b != NULL && k < b->count; ++k) {
        if (b->at[k] != NULL) {
            return 1;
        }
    }
    return 0;
}

void cc_bounds_free(cc_bounds_t *b) {
    size_t k;

    if (b == NULL) {
        return;
    }
    for (k = 0; k < b->count; ++k) {
        free_expr(b->at[k]);
    }
    free(b->at);
    b->at = NULL;
    b->count = 0;
}

static cc_expr_t *clone_expr(const cc_expr_t *src);

cc_expr_t *cc_expr_clone(const cc_expr_t *e) {
    return clone_expr(e);
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
    cc_bounds_free(&s->array_bounds);
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

/**
 * Add a parameter to function @p f.
 *
 * @param f        the function
 * @param ctype    its type, as adjusted (an old-style name's is int until
 *                 a declaration says otherwise)
 * @param name     its name
 * @param n        the name's length
 * @param storage  its storage-class flags
 * @return         0, or -1 when out of memory
 */
static int push_param(cc_function_t *f, const cc_ctype_t *ctype, const char *name, size_t n, int storage) {
    cc_param_t *next = (cc_param_t *)realloc(f->params, (f->param_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    f->params = next;
    f->params[f->param_count].name = cc_strndup(name, n);
    if (f->params[f->param_count].name == NULL) {
        return -1;
    }
    f->params[f->param_count].storage = storage;
    f->params[f->param_count].ctype = ctype;
    memset(&f->params[f->param_count].vla_bounds, 0, sizeof(f->params[f->param_count].vla_bounds));
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

/**
 * Whether two declarations of one file-scope object agree on its type,
 * qualifiers aside: the same type, or arrays of agreeing elements whose
 * bounds are equal where both are given.
 */
static int parser_global_types_agree(const cc_ctype_t *a, const cc_ctype_t *b) {
    if (a == NULL || b == NULL) {
        return a == b;
    }
    a = a->unqual;
    b = b->unqual;
    if (a->kind == CC_CT_ARRAY && b->kind == CC_CT_ARRAY) {
        if (a->count >= 0 && b->count >= 0 && a->count != b->count) {
            return 0;
        }
        return parser_global_types_agree(a->of, b->of);
    }
    return a == b;
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
        if (!parser_global_types_agree(cur->ctype, g.ctype)) {
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
        /* the composite type: a bound one declaration gives completes the other's */
        if (cur->ctype != NULL && cur->ctype->kind == CC_CT_ARRAY && cur->ctype->count < 0 && g.ctype != NULL &&
            g.ctype->kind == CC_CT_ARRAY && g.ctype->count >= 0) {
            cur->ctype = g.ctype;
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
            names[count] = cc_strndup(p->tok.start, p->tok.len);
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
    /* where the next undesignated element goes: after the last one written */
    size_t next_pos = 0;
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
                member_names[member_count] = cc_strndup(p->tok.start, p->tok.len);
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
            next_pos = (size_t)hi + 1;
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
            /*
             * After [4] = a, [1] = b, the next element is [2] (C11
             * 6.7.9p17): it can land on one already written.
             */
            if (next_pos < list->arg_count) {
                free_expr(list->args[next_pos]);
                list->args[next_pos] = item;
            } else if (push_arg(list, item) != 0) {
                free_expr(item);
                free_expr(list);
                return NULL;
            }
            next_pos++;
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
    if (!parser_is_c11_or_newer() && !parser_allow_static_assert_extension()) {
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
    } else if (!parser_is_c23_or_newer() && !parser_allow_static_assert_extension()) {
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
            char text[160];
            char msg[200];
            format_string_for_diag(msg_expr->ident, text, sizeof(text));
            snprintf(msg, sizeof(msg), "static assertion failed: %s", text);
            set_diag(p->diag, p->tok.line, p->tok.col, msg);
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

/** One association of a _Generic selection. */
typedef struct {
    int is_default;          /**< `default:` */
    const cc_ctype_t *ctype; /**< the type named, else NULL */
    cc_expr_t *expr;         /**< the expression it selects */
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
        if (p->tok.kind == TOK_KW_DEFAULT || (p->tok.kind == TOK_IDENT && tok_is_ident(p, "default"))) {
            assoc.is_default = 1;
            if (next_tok(p) != 0) {
                free_expr(control);
                free(items);
                return NULL;
            }
        } else if (parse_type_name(p, "expected type name in _Generic", &assoc.ctype, NULL) != 0) {
            free_expr(control);
            free(items);
            return NULL;
        }
        while (p->tok.kind == TOK_LBRACK || p->tok.kind == TOK_LPAREN) {
            /* a suffix the type name left: taken as a pointer, as the type is used as a value */
            assoc.ctype = cc_ct_pointer(assoc.ctype);
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
    result->generic_ctypes = (const cc_ctype_t **)calloc(count, sizeof(*result->generic_ctypes));
    result->generic_is_default = (unsigned char *)calloc(count, sizeof(*result->generic_is_default));
    if (result->args == NULL || result->generic_ctypes == NULL || result->generic_is_default == NULL) {
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
        result->generic_ctypes[i] = items[i].ctype;
        result->generic_is_default[i] = (unsigned char)(items[i].is_default ? 1 : 0);
    }
    free(items);
    return result;
}

static cc_expr_t *parse_unary(parser_t *p);

/**
 * The GNU imaginary constant a number literal with an `i` or `j` suffix
 * is: the complex value (0, @p lit) of the literal's floating type.  An
 * integer literal is taken as a double, the compiler having no complex
 * integer types.
 *
 * @param p    the parser
 * @param lit  the literal, without its suffix's meaning; consumed
 * @return     the CC_EXPR_COMPLEX node, or NULL when out of memory
 */
static cc_expr_t *parser_imaginary_literal(parser_t *p, cc_expr_t *lit) {
    cc_expr_t *zero = new_expr(CC_EXPR_FLOAT); /* typed below as the literal */
    cc_expr_t *c = new_expr(CC_EXPR_COMPLEX);
    int sid;

    if (lit->kind == CC_EXPR_INT) {
        lit->kind = CC_EXPR_FLOAT;
        lit->float_val = (double)lit->int_val;
        lit->float_val_ld = (long double)lit->int_val;
        parser_set_ctype(lit, cc_ct_basic(CC_CT_DOUBLE));
    }
    sid = parser_complex_struct(p, lit->ctype->kind);
    if (zero == NULL || c == NULL || sid < 0 || parser_complex_struct(p, CC_CT_FLOAT) < 0 ||
        parser_complex_struct(p, CC_CT_DOUBLE) < 0 || parser_complex_struct(p, CC_CT_LDOUBLE) < 0) {
        free_expr(zero);
        free_expr(c);
        free_expr(lit);
        return NULL;
    }
    parser_set_ctype(zero, lit->ctype);
    c->line = p->tok.line;
    c->col = p->tok.col;
    c->lhs = zero;
    c->rhs = lit;
    return c;
}

/**
 * A C23 bit-precise constant (`123wb`, `0xffuwb`): of type `_BitInt(N)`
 * with N the bits its value needs plus a sign bit (at least 2), or
 * `unsigned _BitInt(N)` with N the bits it needs (at least 1).  The digits
 * are read again from the token, in any base, since the value may need up
 * to BITINT_MAXWIDTH bits.  Up to 64 bits it is an integer constant node;
 * wider, a compound literal of the wide type's limbs.
 *
 * @param p  the parser, at the number token
 * @return   the expression, or NULL (diagnosed)
 */
static cc_expr_t *parser_bitint_literal(parser_t *p) {
    enum { MAXW = 65535 / 32 + 2 };
    unsigned int acc[MAXW];
    size_t used = 1;
    const char *s = p->tok.start;
    const char *end = s + p->tok.len;
    unsigned base = 10;
    int is_uns = 0;
    long bits = 0;
    long width;
    long i;
    cc_expr_t *e;

    memset(acc, 0, sizeof(acc));
    /* the suffix -- wb or WB, with u or U on either side -- ends the digits (b is a hex digit) */
    for (;;) {
        if (end - s > 1 && (end[-1] == 'u' || end[-1] == 'U')) {
            is_uns = 1;
            --end;
        } else if (end - s > 2 && ((end[-2] == 'w' && end[-1] == 'b') || (end[-2] == 'W' && end[-1] == 'B'))) {
            end -= 2;
        } else {
            break;
        }
    }
    if (end - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (end - s > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        base = 2;
        s += 2;
    } else if (end - s > 1 && s[0] == '0') {
        base = 8;
    }
    for (; s < end; ++s) {
        unsigned d;
        unsigned long long carry;
        size_t k;

        if (*s == '\'') {
            continue;
        }
        if (*s >= '0' && *s <= '9') {
            d = (unsigned)(*s - '0');
        } else if (base == 16 && ((*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F'))) {
            d = (unsigned)((*s | 0x20) - 'a' + 10);
        } else {
            continue;
        }
        carry = d;
        for (k = 0; k < used; ++k) {
            unsigned long long v = (unsigned long long)acc[k] * base + carry;

            acc[k] = (unsigned int)v;
            carry = v >> 32;
        }
        if (carry != 0) {
            if (used == MAXW) {
                set_diag(p->diag, p->tok.line, p->tok.col, "bit-precise constant exceeds BITINT_MAXWIDTH");
                return NULL;
            }
            acc[used++] = (unsigned int)carry;
        }
    }
    for (i = (long)used * 32 - 1; i >= 0; --i) {
        if ((acc[i / 32] >> (i % 32)) & 1u) {
            bits = i + 1;
            break;
        }
    }
    width = is_uns ? (bits < 1 ? 1 : bits) : (bits + 1 < 2 ? 2 : bits + 1);
    if (width > 65535) {
        set_diag(p->diag, p->tok.line, p->tok.col, "bit-precise constant exceeds BITINT_MAXWIDTH");
        return NULL;
    }
    if (width <= 64) {
        e = new_const_expr(CC_EXPR_INT, cc_ct_bitint(width, is_uns));
        if (e == NULL) {
            return NULL;
        }
        e->int_val = (long long)((unsigned long long)acc[0] | ((unsigned long long)acc[1] << 32));
        return e;
    } else {
        /* (wide type){ limbs } */
        int sid = parser_wide_struct(p, width, is_uns, 0);
        long limb = cc_target_pointer_size();
        long n = (width + limb * 8 - 1) / (limb * 8);
        cc_expr_t *outer = new_expr(CC_EXPR_INIT_LIST);
        cc_expr_t *list = new_expr(CC_EXPR_INIT_LIST);

        e = new_expr(CC_EXPR_CAST);
        if (sid < 0 || outer == NULL || list == NULL || e == NULL || push_arg(outer, list) != 0) {
            free_expr(outer);
            free_expr(list);
            free_expr(e);
            return NULL;
        }
        /* the limbs initialize the structure's one member, the limb array */
        list = outer->args[0];
        for (i = 0; i < n; ++i) {
            unsigned long long v = acc[(size_t)(i * limb / 4)];
            cc_expr_t *item;

            if (limb == 8) {
                v |= (unsigned long long)acc[(size_t)(i * 2 + 1)] << 32;
            }
            /* not new_int_expr(): its long is 32 bits where cc runs on i386 */
            item = new_const_expr(CC_EXPR_INT, cc_ct_basic(limb == 8 ? CC_CT_ULLONG : CC_CT_UINT));
            if (item == NULL || push_arg(list, item) != 0) {
                free_expr(item);
                free_expr(outer);
                free_expr(e);
                return NULL;
            }
            item->int_val = (long long)v;
        }
        e->ctype = cc_ct_record(sid);
        e->lhs = outer;
        return e;
    }
}

static cc_expr_t *parse_primary(parser_t *p) {
    cc_expr_t *e;

    if (p->tok.kind == TOK_KW_EXTENSION) {
        if (next_tok(p) != 0) {
            return NULL;
        }
        /*
         * GNU __extension__ can prefix any expression, including casts such as
         * `__extension__ (void)({ ... })`. Re-enter unary parsing so cast
         * recognition still runs instead of dropping straight to primary-only
         * parsing.
         */
        return parse_unary(p);
    }

    if (p->tok.kind == TOK_NUM && p->tok.is_float && p->tok.float_is_decimal != 0) {
        e = parser_decimal_literal(p);
        if (e != NULL && next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }
    if (p->tok.kind == TOK_NUM && p->tok.int_is_bitprecise && !p->tok.is_float) {
        e = parser_bitint_literal(p);
        if (e != NULL && next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }
    if (p->tok.kind == TOK_NUM) {
        if (p->tok.is_float) {
            cc_ctype_kind_t k = p->tok.float_is_single ? CC_CT_FLOAT
                                : p->tok.float_is_long ? CC_CT_LDOUBLE
                                                       : CC_CT_DOUBLE;

            e = new_const_expr(CC_EXPR_FLOAT, cc_ct_basic(k));
            if (e == NULL) {
                return NULL;
            }
            e->float_val = p->tok.fnum;
            e->float_val_ld = p->tok.fnum_ld;
        } else {
            cc_ctype_kind_t k;

            if (p->tok.int_is_longlong) {
                k = p->tok.int_is_unsigned ? CC_CT_ULLONG : CC_CT_LLONG;
            } else if (p->tok.int_is_long) {
                k = p->tok.int_is_unsigned ? CC_CT_ULONG : CC_CT_LONG;
            } else {
                k = p->tok.int_is_unsigned ? CC_CT_UINT : CC_CT_INT;
            }
            e = new_const_expr(CC_EXPR_INT, cc_ct_basic(k));
            if (e == NULL) {
                return NULL;
            }
            e->int_val = p->tok.num;
        }
        if (p->tok.is_imaginary) {
            e = parser_imaginary_literal(p, e);
            if (e == NULL) {
                return NULL;
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
        const cc_ctype_t *char_ty = cc_ct_basic(CC_CT_CHAR);
        const cc_ctype_t *str_ty = string_token_char_type(p, &p->tok);
        char *lit = str_ty == char_ty ? canonicalize_narrow_string_token(&p->tok)
                                      : cc_strndup(p->tok.start, p->tok.len);
        e = new_expr(CC_EXPR_STR);
        if (e == NULL || lit == NULL) {
            free(lit);
            return NULL;
        }
        e->ident = lit;
        e->aux_ctype = str_ty;
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        while (p->tok.kind == TOK_STR) {
            const cc_ctype_t *part_ty = string_token_char_type(p, &p->tok);
            char *part = part_ty == char_ty ? canonicalize_narrow_string_token(&p->tok)
                                            : cc_strndup(p->tok.start, p->tok.len);
            size_t alen;
            size_t plen;
            char *next_lit;
            if (part == NULL) {
                free_expr(e);
                return NULL;
            }
            if (part_ty != str_ty) {
                /* a narrow piece joins a wide literal, which the result is (C11 6.4.5p5) */
                if (str_ty == char_ty || part_ty == char_ty) {
                    str_ty = str_ty == char_ty ? part_ty : str_ty;
                    e->aux_ctype = str_ty;
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
        int var_idx = var_find_visible_n(p, p->tok.start, p->tok.len);
        int eidx = -1;
        int is_typedef_name =
            typedef_find_visible_n(p, p->tok.start, p->tok.len) >= 0 ||
            builtin_typedef_type_n(p->tok.start, p->tok.len, NULL);

        if (parser_is_c23_or_newer() && tok_is_ident(p, "true")) {
            e = new_const_expr(CC_EXPR_INT, cc_ct_basic(CC_CT_BOOL));
            if (e == NULL) {
                return NULL;
            }
            e->int_val = 1;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
        if (parser_is_c23_or_newer() && tok_is_ident(p, "false")) {
            e = new_const_expr(CC_EXPR_INT, cc_ct_basic(CC_CT_BOOL));
            if (e == NULL) {
                return NULL;
            }
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
        if (parser_is_c23_or_newer() && tok_is_ident(p, "nullptr")) {
            e = new_const_expr(CC_EXPR_INT, cc_ct_basic(CC_CT_INT));
            if (e == NULL) {
                return NULL;
            }
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
        if (var_idx < 0) {
            eidx = enum_const_find_visible_n(p, p->tok.start, p->tok.len);
        }
        if (is_typedef_name && var_idx < 0 && eidx < 0) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected expression");
            return NULL;
        }
        if (eidx >= 0) {
            e = new_const_expr(CC_EXPR_INT, p->enum_consts[eidx].ctype);
            if (e == NULL) {
                return NULL;
            }
            e->int_val = (long long)p->enum_consts[eidx].bits;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            return e;
        }
        char *name = cc_strndup(p->tok.start, p->tok.len);
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
                const cc_ctype_t *sct = NULL;
                const cc_ctype_t *cur; /* the type of what the designator names so far */
                int ssid = -1;
                long total = 0;
                cc_expr_t *dyn_off = NULL;

                if (parse_type_name(p, "expected type name in __builtin_offsetof", &sct, NULL) != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (sct != NULL && sct->unqual->kind == CC_CT_RECORD) {
                    ssid = sct->unqual->id;
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
                    long member_off = 0;

                    m = struct_member_find_offset_n_depth(p, ssid, p->tok.start, p->tok.len, &member_off, 0);
                    if (m == NULL) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "unknown member in __builtin_offsetof designator");
                        free_expr(e);
                        return NULL;
                    }
                    total += member_off;
                    cur = m->ctype;
                    if (next_tok(p) != 0) {
                        free_expr(e);
                        return NULL;
                    }
                    while (p->tok.kind == TOK_LBRACK) {
                        long idx = 0;
                        long stride = 0;
                        int idx_is_const = 0;
                        cc_expr_t *idx_expr = NULL;
                        cc_expr_t *rhs_const = NULL;
                        cc_expr_t *term = NULL;

                        if (cur == NULL || cur->kind != CC_CT_ARRAY) {
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
                        /* a step of the array is one of its elements */
                        cur = cur->of;
                        stride = parser_ct_size(p, cur);
                        if (stride <= 0) {
                            stride = 1;
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
                            rhs_const = new_const_expr(CC_EXPR_INT, parser_size_ctype());
                            if (rhs_const == NULL) {
                                free_expr(idx_expr);
                                free_expr(dyn_off);
                                free_expr(e);
                                return NULL;
                            }
                            rhs_const->int_val = stride;
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
                    }
                    if (p->tok.kind != TOK_DOT) {
                        break;
                    }
                    if (cur == NULL || cur->unqual->kind != CC_CT_RECORD) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "nested __builtin_offsetof designator requires struct member");
                        free_expr(dyn_off);
                        free_expr(e);
                        return NULL;
                    }
                    ssid = cur->unqual->id;
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
                        cc_expr_t *base = new_const_expr(CC_EXPR_INT, parser_size_ctype());
                        if (base == NULL) {
                            free_expr(dyn_off);
                            return NULL;
                        }
                        base->int_val = total;
                        dyn_off = new_bin_expr(CC_BIN_ADD, base, dyn_off);
                        if (dyn_off == NULL) {
                            return NULL;
                        }
                    }
                    e = dyn_off;
                } else {
                    e = new_const_expr(CC_EXPR_INT, parser_size_ctype());
                    if (e == NULL) {
                        return NULL;
                    }
                    e->int_val = total;
                }
                return e;
            }
            if (strcmp(e->ident, "__builtin_types_compatible_p") == 0) {
                const cc_ctype_t *t1 = NULL;
                const cc_ctype_t *t2 = NULL;
                int same = 0;
                if (parse_type_name(p, "expected first type in __builtin_types_compatible_p", &t1, NULL) != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_COMMA, "expected ',' after first type in __builtin_types_compatible_p") != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (parse_type_name(p, "expected second type in __builtin_types_compatible_p", &t2, NULL) != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (expect(p, TOK_RPAREN, "expected ')' after __builtin_types_compatible_p arguments") != 0) {
                    free_expr(e);
                    return NULL;
                }
                /* top-level qualifiers are ignored, as gcc ignores them */
                same = parser_ct_compatible(p, t1->unqual, t2->unqual, 0);
                free_expr(e);
                e = new_const_expr(CC_EXPR_INT, cc_ct_basic(CC_CT_INT));
                if (e == NULL) {
                    return NULL;
                }
                e->int_val = same;
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
                    e->args = (cc_expr_t **)calloc(3, sizeof(*e->args));
                    if (e->args == NULL) {
                        free_expr(fe);
                        free_expr(te);
                        free_expr(ce);
                        free_expr(e);
                        return NULL;
                    }
                    e->args[0] = ce;
                    e->args[1] = te;
                    e->args[2] = fe;
                    e->arg_count = 3;
                    return e;
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
                e = new_const_expr(CC_EXPR_INT, cc_ct_basic(CC_CT_INT));
                if (e == NULL) {
                    return NULL;
                }
                e->int_val = is_const ? 1 : 0;
                return e;
            }
            if (strcmp(e->ident, "__builtin_va_arg") == 0) {
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
                if (parse_type_name(p, "expected type name in __builtin_va_arg", &e->ctype, NULL) != 0) {
                    free_expr(e);
                    return NULL;
                }
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
                e->ctype = p->vars[vidx].ctype;
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

/** A new `int` constant of value @p v, or NULL when out of memory. */
static cc_expr_t *new_int_expr(long v) {
    cc_expr_t *e = new_const_expr(CC_EXPR_INT, cc_ct_basic(CC_CT_INT));

    if (e != NULL) {
        e->int_val = v;
    }
    return e;
}

static cc_expr_t *new_ident_expr(const char *name) {
    cc_expr_t *e = new_expr(CC_EXPR_IDENT);
    if (e == NULL) {
        return NULL;
    }
    e->ident = cc_strndup(name, strlen(name));
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

/**
 * Mark a binary expression as the spelling of a unary operator.
 *
 * @param e     the expression, or NULL
 * @param form  CC_UNARY_RHS or CC_UNARY_LHS: which operand is the operator's
 * @return      @p e
 */
static cc_expr_t *mark_unary_form(cc_expr_t *e, unsigned char form) {
    if (e != NULL) {
        e->unary_form = form;
    }
    return e;
}

/**
 * The size of an array type with run-time bounds, `sizeof (T[n][m])`, as
 * an expression of type size_t: the product of each level's bound -- the
 * expression where it is one, else the constant -- and the size of the
 * element.  The bounds are evaluated where the sizeof is (C11 6.5.3.4p2).
 *
 * @param p       the parser
 * @param t       the array type
 * @param bounds  each level's run-time bound; all are consumed
 * @return        the expression, or NULL on error (diagnosed)
 */
static cc_expr_t *vla_type_size_expr(parser_t *p, const cc_ctype_t *t, cc_bounds_t *bounds) {
    cc_expr_t *acc = NULL;
    long elem;
    size_t k;

    for (k = 0; t != NULL && t->kind == CC_CT_ARRAY; ++k, t = t->of) {
        cc_expr_t *factor;

        if (cc_bounds_at(bounds, k) != NULL) {
            factor = new_expr(CC_EXPR_CAST);
            if (factor != NULL) {
                parser_set_ctype(factor, parser_size_ctype());
                factor->lhs = bounds->at[k];
                bounds->at[k] = NULL;
            }
        } else if (t->count >= 0) {
            factor = new_const_expr(CC_EXPR_INT, parser_size_ctype());
            if (factor != NULL) {
                factor->int_val = t->count;
            }
        } else {
            set_diag(p->diag, p->tok.line, p->tok.col, "sizeof of an array type with an unspecified bound");
            factor = NULL;
            free_expr(acc);
            acc = NULL;
            break;
        }
        if (factor == NULL) {
            free_expr(acc);
            acc = NULL;
            break;
        }
        acc = acc == NULL ? factor : new_bin_expr(CC_BIN_MUL, acc, factor);
        if (acc == NULL) {
            break;
        }
    }
    cc_bounds_free(bounds);
    if (acc == NULL) {
        return NULL;
    }
    elem = cc_ct_size(t, p->tu);
    if (elem <= 0) {
        set_diag(p->diag, p->tok.line, p->tok.col, "sizeof of an array of an incomplete type");
        free_expr(acc);
        return NULL;
    }
    {
        cc_expr_t *scale = new_const_expr(CC_EXPR_INT, parser_size_ctype());

        if (scale == NULL) {
            free_expr(acc);
            return NULL;
        }
        scale->int_val = elem;
        acc = new_bin_expr(CC_BIN_MUL, acc, scale);
    }
    return acc;
}

static cc_expr_t *new_update_ident_expr(const char *name, cc_binop_t op, int postfix) {
    cc_expr_t *e = new_expr(CC_EXPR_UPDATE);
    if (e == NULL) {
        return NULL;
    }
    e->ident = cc_strndup(name, strlen(name));
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
    memset(&dst->array_bounds, 0, sizeof(dst->array_bounds));
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
        dst->decl_name = cc_strndup(src->decl_name, strlen(src->decl_name));
        if (dst->decl_name == NULL) {
            return -1;
        }
    }
    if (src->label_name != NULL) {
        dst->label_name = cc_strndup(src->label_name, strlen(src->label_name));
        if (dst->label_name == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->attr_section != NULL) {
        dst->attr_section = cc_strndup(src->attr_section, strlen(src->attr_section));
        if (dst->attr_section == NULL) {
            free_stmt(dst);
            return -1;
        }
    }
    if (src->attr_alias != NULL) {
        dst->attr_alias = cc_strndup(src->attr_alias, strlen(src->attr_alias));
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
    if (src->array_bounds.count > 0) {
        dst->array_bounds.at = (cc_expr_t **)calloc(src->array_bounds.count, sizeof(dst->array_bounds.at[0]));
        if (dst->array_bounds.at == NULL) {
            free_stmt(dst);
            return -1;
        }
        dst->array_bounds.count = src->array_bounds.count;
        for (i = 0; i < src->array_bounds.count; ++i) {
            if (src->array_bounds.at[i] != NULL) {
                dst->array_bounds.at[i] = clone_expr(src->array_bounds.at[i]);
                if (dst->array_bounds.at[i] == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
        }
    }
    if (src->asm_template != NULL) {
        dst->asm_template = cc_strndup(src->asm_template, strlen(src->asm_template));
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
                dst->asm_outputs[j].name = cc_strndup(src->asm_outputs[j].name, strlen(src->asm_outputs[j].name));
                if (dst->asm_outputs[j].name == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
            if (src->asm_outputs[j].constraint != NULL) {
                dst->asm_outputs[j].constraint =
                    cc_strndup(src->asm_outputs[j].constraint, strlen(src->asm_outputs[j].constraint));
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
                dst->asm_inputs[j].name = cc_strndup(src->asm_inputs[j].name, strlen(src->asm_inputs[j].name));
                if (dst->asm_inputs[j].name == NULL) {
                    free_stmt(dst);
                    return -1;
                }
            }
            if (src->asm_inputs[j].constraint != NULL) {
                dst->asm_inputs[j].constraint =
                    cc_strndup(src->asm_inputs[j].constraint, strlen(src->asm_inputs[j].constraint));
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
            dst->asm_clobbers[j] = cc_strndup(src->asm_clobbers[j], strlen(src->asm_clobbers[j]));
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
            dst->asm_goto_labels[j] = cc_strndup(src->asm_goto_labels[j], strlen(src->asm_goto_labels[j]));
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

    dst->int_val = src->int_val;
    dst->float_val = src->float_val;
    dst->float_val_ld = src->float_val_ld;
    dst->op = src->op;
    dst->is_subscript = src->is_subscript;
    dst->unary_form = src->unary_form;
    dst->ctype = src->ctype;
    dst->aux_ctype = src->aux_ctype;
    dst->member_is_arrow = src->member_is_arrow;
    dst->member_offset = src->member_offset;
    dst->member_is_bitfield = src->member_is_bitfield;
    dst->member_bit_width = src->member_bit_width;
    dst->member_bit_offset = src->member_bit_offset;
    dst->member_bit_storage_bits = src->member_bit_storage_bits;
    dst->member_bit_signed = src->member_bit_signed;
    dst->member_bit_storage_size = src->member_bit_storage_size;
    dst->update_postfix = src->update_postfix;
    dst->generic_selected = src->generic_selected;

    if (src->ident != NULL) {
        dst->ident = cc_strndup(src->ident, strlen(src->ident));
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
        dst->generic_ctypes = (const cc_ctype_t **)calloc(src->generic_count, sizeof(*dst->generic_ctypes));
        dst->generic_is_default = (unsigned char *)calloc(src->generic_count, sizeof(*dst->generic_is_default));
        if (dst->generic_ctypes == NULL || dst->generic_is_default == NULL) {
            free_expr(dst);
            return NULL;
        }
        memcpy(dst->generic_ctypes, src->generic_ctypes, src->generic_count * sizeof(*dst->generic_ctypes));
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
            add = new_bin_expr(CC_BIN_ADD, e, idx);
            if (add == NULL) {
                return NULL;
            }
            add->is_subscript = 1;
            deref = new_expr(CC_EXPR_DEREF);
            if (deref == NULL) {
                free_expr(add);
                return NULL;
            }
            deref->lhs = add;
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
            m->ident = cc_strndup(p->tok.start, p->tok.len);
            if (m->ident == NULL) {
                free_expr(m);
                return NULL;
            }
            {
                const cc_ctype_t *base = cc_ct_decay(parser_expr_ctype(p, m->lhs));

                if (base != NULL && is_arrow) {
                    base = base->kind == CC_CT_POINTER ? base->of->unqual : NULL;
                }
                if (base != NULL && base->unqual->kind == CC_CT_RECORD) {
                    const cc_struct_member_t *sm =
                        struct_member_find_n(p, base->unqual->id, m->ident, strlen(m->ident));

                    if (sm != NULL) {
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

/**
 * Fold `sizeof (type-name)` into the integer constant it is, when the
 * type has a size (not a variable-length array, not an incomplete type).
 * The type must be complete where the sizeof is written, so its size is
 * known now and is the same wherever the expression is used.
 *
 * Otherwise the node is left for sema and lowering (a variable-length
 * array's size is computed at run time) -- unless it names a function
 * type, which has no size.
 *
 * @param p  the parser
 * @param e  the CC_EXPR_SIZEOF node, its aux fields set from the type name
 * @param t  the type named
 * @return   0, or -1 on error (diagnosed)
 */
static int fold_sizeof_type(parser_t *p, cc_expr_t *e, const cc_ctype_t *t) {
    long n = cc_ct_size(t, p->tu);

    if (n >= 0) {
        e->kind = CC_EXPR_INT;
        e->int_val = n;
        parser_set_ctype(e, parser_size_ctype());
        e->aux_ctype = NULL;
        return 0;
    }
    if (t->kind != CC_CT_FUNCTION) {
        return 0;
    }
    {
        char spelled[128];
        char msg[200];
        snprintf(msg, sizeof(msg), "sizeof of '%s' is not yet supported", cc_ct_format(t, spelled, sizeof(spelled)));
        set_diag(p->diag, e->line, e->col, msg);
    }
    return -1;
}

/**
 * Fold `sizeof x` into the integer constant it is, when x's type is
 * known from the declarations it names -- a variable, and `*`,
 * subscripts, `&` and pointer + integer applied to one (see
 * cc_ct_of_expr()) -- and that type has a size (not a variable-length
 * array).  The declarations in scope here are the ones the expression
 * names, so the size is the answer wherever the expression is used.
 * Otherwise the node is left as it is.
 *
 * @param p  the parser
 * @param e  the CC_EXPR_SIZEOF node with its operand
 */
static void fold_sizeof_expr(parser_t *p, cc_expr_t *e) {
    const cc_ctype_t *t = cc_ct_of_expr(e->lhs);
    long n;

    if (t == NULL) {
        return;
    }
    n = cc_ct_size(t, p->tu);
    if (n < 0) {
        return;
    }
    free_expr(e->lhs);
    e->lhs = NULL;
    e->kind = CC_EXPR_INT;
    e->int_val = n;
    parser_set_ctype(e, parser_size_ctype());
}

/**
 * Hoist a file-scope compound literal into an anonymous static global
 * initialized by its braces, and turn @p e into a reference to it.  At
 * file scope a compound literal has static storage (C11 6.5.2.5p5), so
 * its address -- an array's decayed value, or `&` of any -- is an
 * address constant; the global initializer machinery then handles every
 * type of literal as it handles a named object's.
 *
 * @param p  the parser
 * @param e  the compound literal: a CC_EXPR_CAST whose operand is its
 *           brace list
 * @return   0, or -1 when out of memory
 */
static int hoist_file_literal(parser_t *p, cc_expr_t *e) {
    char name[48];
    cc_global_t g;

    snprintf(name, sizeof(name), "__cc_literal_%zu", p->file_literal_count++);
    memset(&g, 0, sizeof(g));
    g.name = cc_strdup(name);
    g.line = e->line;
    g.col = e->col;
    g.storage = CC_STORAGE_STATIC;
    g.init = e->lhs;
    g.ctype = e->ctype;
    if (g.name == NULL || push_global(p->tu, g) != 0) {
        free(g.name);
        return -1;
    }
    e->lhs = NULL;
    e->kind = CC_EXPR_IDENT;
    e->ident = cc_strdup(name);
    return e->ident != NULL ? 0 : -1;
}

static cc_expr_t *parse_unary(parser_t *p) {
    if (p->tok.kind == TOK_IDENT && parser_is_gnu_mode() && tok_is_ident(p, "__extension__")) {
        if (next_tok(p) != 0) {
            return NULL;
        }
        return parse_unary(p);
    }

    if (p->tok.kind == TOK_KW_SIZEOF) {
        cc_expr_t *e;
        if (next_tok(p) != 0) {
            return NULL;
        }
        e = new_expr(CC_EXPR_SIZEOF);
        if (e == NULL) {
            return NULL;
        }
        if (is_type_name_start_after_lparen(p)) {
            const cc_ctype_t *sizeof_ctype = NULL;
            cc_bounds_t vla_bounds;
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            if (parse_type_name(p, "expected type name in sizeof", &sizeof_ctype, &vla_bounds) != 0) {
                free_expr(e);
                return NULL;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after sizeof type") != 0) {
                cc_bounds_free(&vla_bounds);
                free_expr(e);
                return NULL;
            }
            if (p->tok.kind != TOK_LBRACE && cc_bounds_any(&vla_bounds)) {
                /* `sizeof (T[n][m])`: computed here, its bounds evaluated (C11 6.5.3.4p2) */
                cc_expr_t *bytes = vla_type_size_expr(p, sizeof_ctype, &vla_bounds);

                free_expr(e);
                return bytes;
            }
            cc_bounds_free(&vla_bounds);
            /* the type, for sema and lowering when it cannot be folded here (a structure not yet laid out) */
            e->aux_ctype = sizeof_ctype;
            if (p->tok.kind != TOK_LBRACE && fold_sizeof_type(p, e, sizeof_ctype) != 0) {
                free_expr(e);
                return NULL;
            }
            if (p->tok.kind == TOK_LBRACE) {
                cc_expr_t *ce = new_expr(CC_EXPR_CAST);
                if (ce == NULL) {
                    free_expr(e);
                    return NULL;
                }
                ce->ctype = sizeof_ctype;
                ce->lhs = parse_initializer_expr(p);
                if (ce->lhs == NULL) {
                    free_expr(ce);
                    free_expr(e);
                    return NULL;
                }
                e->lhs = ce;
                e->aux_ctype = NULL;
            }
            return e;
        }
        e->lhs = parse_unary(p);
        if (e->lhs == NULL) {
            free_expr(e);
            return NULL;
        }
        fold_sizeof_expr(p, e);
        return e;
    }

    if (p->tok.kind == TOK_IDENT &&
        (tok_is_ident(p, "_Alignof") || (parser_is_c23_or_newer() && tok_is_ident(p, "alignof")) ||
         (parser_is_gnu_mode() && (tok_is_ident(p, "__alignof__") || tok_is_ident(p, "__alignof"))))) {
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
        if (is_declspec_start(p)) {
            const cc_ctype_t *act = NULL;

            if (parse_type_name(p, "expected type name in _Alignof/alignof", &act, NULL) != 0) {
                return NULL;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after _Alignof/alignof type") != 0) {
                return NULL;
            }
            align = parser_ct_align(p, act);
            if (align <= 0) {
                align = cc_target_pointer_size();
            }
            return new_int_expr(align);
        } else {
            ae = parse_assign(p);
            if (ae == NULL) {
                return NULL;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after _Alignof/alignof expression") != 0) {
                free_expr(ae);
                return NULL;
            }
            align = parser_ct_align(p, parser_expr_ctype(p, ae));
            free_expr(ae);
        }
        if (align <= 0) {
            align = cc_target_pointer_size();
        }
        return new_int_expr(align);
    }

    if (p->tok.kind == TOK_LPAREN) {
        parser_t q = *p;
        cc_expr_t *e = new_expr(CC_EXPR_CAST);
        if (next_tok(&q) == 0 && is_declspec_start(&q)) {
            if (e == NULL) {
                return NULL;
            }
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            if (parse_type_name(p, "expected cast type", &e->ctype, NULL) != 0) {
                free_expr(e);
                return NULL;
            }
            if (e->ctype != NULL && e->ctype->kind == CC_CT_FUNCTION) {
                set_diag(p->diag, p->tok.line, p->tok.col, "cast specifies function type");
                free_expr(e);
                return NULL;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after cast type") != 0) {
                free_expr(e);
                return NULL;
            }
            if (p->tok.kind == TOK_LBRACE) {
                e->lhs = parse_initializer_expr(p);
                if (e->lhs == NULL) {
                    free_expr(e);
                    return NULL;
                }
                /* `(T[]){...}`: the bound comes from the list (C11 6.5.2.5p4, 6.7.9p22) */
                if (e->ctype != NULL && e->ctype->kind == CC_CT_ARRAY && e->ctype->count < 0) {
                    long n = parser_init_list_bound_ct(p, e->ctype->of, e->lhs);

                    if (n > 0) {
                        e->ctype = cc_ct_array(e->ctype->of, n);
                    }
                }
                /* a file-scope array literal stands for its address: an object of static storage */
                if (p->scope_depth == 0 && e->ctype != NULL && e->ctype->kind == CC_CT_ARRAY &&
                    hoist_file_literal(p, e) != 0) {
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
        free(e);
    }

    if (p->tok.kind == TOK_AMP) {
        cc_expr_t *e = new_expr(CC_EXPR_ADDR);
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
        /* the address of a file-scope compound literal: an object of static storage (an array one is already) */
        if (p->scope_depth == 0 && e->lhs->kind == CC_EXPR_CAST && e->lhs->lhs != NULL &&
            e->lhs->lhs->kind == CC_EXPR_INIT_LIST && hoist_file_literal(p, e->lhs) != 0) {
            free_expr(e);
            return NULL;
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
        e->ident = local_label_dup_mapped_n(p, p->tok.start, p->tok.len);
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
        return e;
    }

    if (p->tok.kind == TOK_IDENT && (tok_is_ident(p, "__real__") || tok_is_ident(p, "__real") ||
                                     tok_is_ident(p, "__imag__") || tok_is_ident(p, "__imag"))) {
        /*
         * GNU __real__ z and __imag__ z: the member of the structure standing
         * for z's complex type, so they are lvalues as gcc has them.  Sema
         * gives a real operand's meaning (itself, and zero).
         */
        int imag = p->tok.start[2] == 'i';
        cc_expr_t *m = new_expr(CC_EXPR_MEMBER);
        cc_expr_t *operand;

        if (m == NULL) {
            return NULL;
        }
        m->line = p->tok.line;
        m->col = p->tok.col;
        m->ident = cc_strdup(imag ? "__im" : "__re");
        if (m->ident == NULL || next_tok(p) != 0) {
            free_expr(m);
            return NULL;
        }
        operand = parse_unary(p);
        if (operand == NULL) {
            free_expr(m);
            return NULL;
        }
        m->lhs = operand;
        m->member_is_part = 1;
        return m;
    }
    if (p->tok.kind == TOK_MINUS) {
        cc_expr_t *neg_one;
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            return NULL;
        }
        neg_one = new_int_expr(-1);
        if (neg_one == NULL) {
            free_expr(rhs);
            return NULL;
        }
        return mark_unary_form(new_bin_expr(CC_BIN_MUL, neg_one, rhs), CC_UNARY_RHS);
    }
    if (p->tok.kind == TOK_PLUS) {
        cc_expr_t *one;
        cc_expr_t *rhs;
        if (next_tok(p) != 0) {
            return NULL;
        }
        rhs = parse_unary(p);
        if (rhs == NULL) {
            return NULL;
        }
        /*
         * Unary plus performs integer promotions. Model it as multiplication
         * by one so existing usual-arithmetic-conversion semantics apply, and
         * non-arithmetic operands are still rejected.
         */
        one = new_int_expr(1);
        if (one == NULL) {
            free_expr(rhs);
            return NULL;
        }
        return mark_unary_form(new_bin_expr(CC_BIN_MUL, one, rhs), CC_UNARY_RHS);
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
        return mark_unary_form(new_bin_expr(CC_BIN_BXOR, rhs, mask), CC_UNARY_LHS);
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
    cc_expr_t *e;
    if (p->parse_depth >= CC_MAX_PARSE_DEPTH) {
        set_diag(p->diag, p->tok.line, p->tok.col,
                 "expression nesting depth exceeds limit");
        return NULL;
    }
    p->parse_depth++;
    e = parse_comma(p);
    p->parse_depth--;
    return e;
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

/** A named declarator's type and what came with it. */
typedef struct {
    char *name;                    /**< the declared name (owned) */
    const cc_ctype_t *ctype;       /**< the type */
    cc_bounds_t vla_bounds;        /**< a variable-length array's (or its pointee's) run-time bounds */
    int saw_restrict;              /**< a pointer level is restrict-qualified */
} named_decl_t;

/**
 * Parse the declarator of one declaration, after its specifiers, and give
 * its type.  A parameter list right after the name is left for the
 * caller, which declares a function.
 *
 * @param p     the parser, at the declarator
 * @param base  the specifiers' type (parser_t.last_spec_ctype)
 * @param out   receives the declaration; free its name and vla_bounds
 *              when done with them
 * @return      0, or -1 on error (diagnosed)
 */
static int parse_named_decl(parser_t *p, const cc_ctype_t *base, named_decl_t *out) {
    declarator_t d;
    size_t line = p->tok.line;
    size_t col = p->tok.col;
    size_t i;
    const cc_ctype_t *t;

    memset(out, 0, sizeof(*out));
    memset(&d, 0, sizeof(d));
    if (parse_declarator_into(p, &d, DECL_STOP_AT_PARAMS) != 0) {
        declarator_free(&d);
        return -1;
    }
    t = declarator_type(&d, base);
    out->ctype = t;
    if (t == NULL) {
        set_diag(p->diag, line, col, "out of memory building a declaration's type");
        declarator_free(&d);
        return -1;
    }
    for (i = 0; i < d.count; ++i) {
        if (d.ops[i].kind == DECL_OP_POINTER && (d.ops[i].quals & CC_CQ_RESTRICT) != 0) {
            out->saw_restrict = 1;
        }
    }
    /* `T (*p)[n]`, a pointer to an array: its pointer op is outermost */
    if ((d.nested && d.inner_arrays == 0 && t->kind == CC_CT_POINTER && t->of->kind == CC_CT_ARRAY)
            ? take_vla_bounds(&d, t->of, 1, &out->vla_bounds) != 0
            : take_vla_bounds(&d, t, 0, &out->vla_bounds) != 0) {
        set_diag(p->diag, line, col, "out of memory recording array bounds");
        declarator_free(&d);
        return -1;
    }
    out->name = d.name;
    d.name = NULL;
    declarator_free(&d);
    return 0;
}

/**
 * Parse the declarator of one structure member, after its specifiers and
 * before any bit-field width, and give its type, its element type and,
 * for an array, its element count.
 *
 * @param p     the parser, at the declarator
 * @param base  the specifiers' type (parser_t.last_spec_ctype)
 * @param out   receives the member; free its name when done
 * @return      0, or -1 on error (diagnosed)
 */
static int parse_member_decl(parser_t *p, const cc_ctype_t *base, member_decl_t *out) {
    declarator_t d;
    const cc_ctype_t *t;
    const cc_ctype_t *e;
    size_t line = p->tok.line;
    size_t col = p->tok.col;

    memset(out, 0, sizeof(*out));
    out->count = -1;
    memset(&d, 0, sizeof(d));
    if (parse_declarator_into(p, &d, DECL_ABSTRACT | DECL_MEMBER) != 0) {
        declarator_free(&d);
        return -1;
    }
    t = declarator_type(&d, base);
    if (t == NULL) {
        set_diag(p->diag, line, col, "out of memory building a member's type");
        goto fail;
    }
    if (t->kind == CC_CT_FUNCTION) {
        set_diag(p->diag, line, col, "a member cannot have function type");
        goto fail;
    }
    e = t;
    if (t->kind == CC_CT_ARRAY) {
        out->is_array = 1;
        out->count = 1;
        for (; e->kind == CC_CT_ARRAY; e = e->of) {
            if (e->count < 0) {
                out->unsized |= (e == t);
                out->count = 0;
            } else if (out->count > 0 && e->count > 0 && out->count > LONG_MAX / e->count) {
                out->count = 0;
            } else {
                out->count *= e->count;
            }
        }
    }
    out->elem = e;
    out->ctype = t;
    out->name = d.name;
    d.name = NULL;
    declarator_free(&d);
    return 0;

fail:
    declarator_free(&d);
    return -1;
}

static int parse_decl_stmt_list(parser_t *p, cc_stmt_t **arr, size_t *count, int need_semi) {
    int is_typedef = 0;
    int base_saw_restrict = 0;
    int decl_storage = 0;
    const cc_ctype_t *base_spec = NULL;
    int base_struct_id; /* the structure the specifiers name, else -1 */
    decl_attrs_t base_attrs;

    decl_attrs_reset(&base_attrs);
    if (parse_declspec(p, &base_spec, 1, "expected declaration type", &is_typedef, &base_saw_restrict,
                       &base_attrs) != 0) {
        return -1;
    }
    decl_storage = p->last_storage;
    if (base_attrs.vector_bytes > 0 || base_attrs.vector_elems > 0) {
        if (parser_apply_vector_attr(p, &base_attrs, &base_spec) != 0) {
            decl_attrs_clear(&base_attrs);
            return -1;
        }
    }
    base_struct_id = base_spec->unqual->kind == CC_CT_RECORD ? base_spec->unqual->id : -1;
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
        int is_fn_decl = 0;
        int track_var = 0;
        int fn_decl_hoisted = 0;
        decl_attrs_t suffix_attrs;
        decl_attrs_t merged_attrs;
        memset(&s, 0, sizeof(s));
        s.line = p->tok.line;
        s.col = p->tok.col;
        s.kind = CC_STMT_DECL;
        s.storage = decl_storage;
        decl_attrs_reset(&suffix_attrs);
        decl_attrs_reset(&merged_attrs);

        {
            named_decl_t nd;

            if (parse_named_decl(p, base_spec, &nd) != 0) {
                free_stmt(&s);
                return -1;
            }
            s.decl_name = nd.name;
            s.ctype = nd.ctype;
            s.array_bounds = nd.vla_bounds;
            if ((base_saw_restrict || nd.saw_restrict) && cc_ct_decay(s.ctype)->kind != CC_CT_POINTER) {
                set_diag(p->diag, p->tok.line, p->tok.col, "restrict qualifier requires a pointer type");
                free_stmt(&s);
                return -1;
            }
        }
        if (skip_decl_gnu_suffix(p, &suffix_attrs) != 0) {
            free_stmt(&s);
            return -1;
        }
        if (s.ctype != NULL && s.ctype->kind != CC_CT_ARRAY && parser_apply_vector_attr(p, &suffix_attrs, &s.ctype) != 0) {
            decl_attrs_clear(&suffix_attrs);
            free_stmt(&s);
            return -1;
        }
        long merged_format_index = 0;
        long merged_format_first_to_check = 0;

        if (decl_attrs_merge(&merged_attrs, &base_attrs) != 0 || decl_attrs_merge(&merged_attrs, &suffix_attrs) != 0) {
            free_stmt(&s);
            return -1;
        }
        merged_format_index = merged_attrs.format_index;
        merged_format_first_to_check = merged_attrs.format_first_to_check;
        s.attr_flags = merged_attrs.flags;
        s.attr_align = merged_attrs.align;
        if (merged_attrs.section != NULL) {
            s.attr_section = cc_strndup(merged_attrs.section, strlen(merged_attrs.section));
            if (s.attr_section == NULL) {
                free_stmt(&s);
                return -1;
            }
        }
        if (merged_attrs.alias != NULL) {
            s.attr_alias = cc_strndup(merged_attrs.alias, strlen(merged_attrs.alias));
            if (s.attr_alias == NULL) {
                free_stmt(&s);
                return -1;
            }
        }
        decl_attrs_clear(&suffix_attrs);
        decl_attrs_clear(&merged_attrs);
        /* a parameter list after a plain name: a function declaration */
        if (!is_typedef && p->tok.kind == TOK_LPAREN) {
            cc_function_t fn_decl;

            memset(&fn_decl, 0, sizeof(fn_decl));
            fn_decl.line = s.line;
            fn_decl.col = s.col;
            fn_decl.ret_ctype = s.ctype; /* the declarator ended at the name: the specifiers' type */
            fn_decl.storage = s.storage;
            fn_decl.attr_flags = s.attr_flags;
            fn_decl.attr_align = s.attr_align;
            fn_decl.attr_format_index = merged_format_index;
            fn_decl.attr_format_first_to_check = merged_format_first_to_check;
            fn_decl.has_body = 0;
            if (next_tok(p) != 0) {
                free_stmt(&s);
                return -1;
            }
            if (p->tok.kind != TOK_RPAREN) {
                if (parse_params(p, &fn_decl, 0) != 0) {
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
        if (!is_typedef && !is_fn_decl && s.decl_name != NULL && s.ctype != NULL &&
            s.ctype->kind == CC_CT_FUNCTION) {
            /* `fn_t f;`, a function type through a typedef: a function declaration (C11 6.7.8p3) */
            cc_function_t fn_decl;
            size_t pi;

            memset(&fn_decl, 0, sizeof(fn_decl));
            fn_decl.line = s.line;
            fn_decl.col = s.col;
            fn_decl.ret_ctype = s.ctype->of;
            fn_decl.storage = s.storage;
            fn_decl.attr_flags = s.attr_flags;
            fn_decl.attr_align = s.attr_align;
            fn_decl.attr_format_index = merged_format_index;
            fn_decl.attr_format_first_to_check = merged_format_first_to_check;
            fn_decl.has_body = 0;
            fn_decl.is_variadic = s.ctype->variadic;
            fn_decl.has_prototype = s.ctype->prototyped;
            if (s.ctype->param_count > 0) {
                fn_decl.params = (cc_param_t *)calloc(s.ctype->param_count, sizeof(*fn_decl.params));
                if (fn_decl.params == NULL) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "out of memory declaring a function");
                    free_stmt(&s);
                    return -1;
                }
                fn_decl.param_count = s.ctype->param_count;
                for (pi = 0; pi < s.ctype->param_count; ++pi) {
                    fn_decl.params[pi].ctype = s.ctype->params[pi];
                }
            }
            fn_decl.name = s.decl_name;
            s.decl_name = NULL;
            fn_decl.attr_section = s.attr_section;
            s.attr_section = NULL;
            fn_decl.attr_alias = s.attr_alias;
            s.attr_alias = NULL;
            if (parser_push_hoisted_func(p, &fn_decl) != 0) {
                free_func(&fn_decl);
                free_stmt(&s);
                return -1;
            }
            is_fn_decl = 1;
            fn_decl_hoisted = 1;
        }
        if (is_typedef && p->tok.kind == TOK_LPAREN) {
            /* `typedef T name(params)`: the name is the function type, its parameters as declared */
            cc_function_t fn_type;
            const cc_ctype_t *fct;

            memset(&fn_type, 0, sizeof(fn_type));
            fn_type.ret_ctype = s.ctype;
            if (next_tok(p) != 0) {
                free_stmt(&s);
                return -1;
            }
            if (p->tok.kind != TOK_RPAREN && parse_params(p, &fn_type, 0) != 0) {
                free_func(&fn_type);
                free_stmt(&s);
                return -1;
            }
            if (expect(p, TOK_RPAREN, "expected ')' after parameter list") != 0) {
                free_func(&fn_type);
                free_stmt(&s);
                return -1;
            }
            if (parser_is_c23_or_newer() && fn_type.param_count == 0 && !fn_type.is_variadic &&
                !fn_type.has_prototype) {
                fn_type.has_prototype = 1;
            }
            fct = parser_function_ctype(&fn_type);
            free_func(&fn_type);
            if (fct == NULL) {
                set_diag(p->diag, p->tok.line, p->tok.col, "cannot represent the function type of a typedef");
                free_stmt(&s);
                return -1;
            }
            s.ctype = fct;
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
        /* `T a[] = {...}`: the initializer completes the array type */
        if (s.ctype != NULL && s.ctype->kind == CC_CT_ARRAY && s.ctype->count < 0 && s.expr != NULL) {
            long inferred = 0;
            if (s.expr->kind == CC_EXPR_INIT_LIST) {
                inferred = parser_init_list_bound_ct(p, s.ctype->of, s.expr);
            } else if (s.expr->kind == CC_EXPR_STR) {
                inferred = parser_string_literal_array_bound(s.expr);
            }
            if (inferred > 0) {
                s.ctype = cc_ct_array(s.ctype->of, inferred);
            }
        }
        if (is_typedef) {
            int trc;
            if (s.expr != NULL) {
                set_diag(p->diag, p->tok.line, p->tok.col, "typedef declarator cannot have an initializer");
                free_stmt(&s);
                return -1;
            }
            trc = typedef_push(p, s.decl_name, s.ctype);
            if (trc > 0) {
                int existing = typedef_find_current_scope_n(p, s.decl_name, strlen(s.decl_name));
                if (existing >= 0 && p->diag != NULL && p->diag->message[0] == '\0') {
                    char old_spelled[96];
                    char new_spelled[96];

                    cc_ct_format(p->typedefs[existing].ctype, old_spelled, sizeof(old_spelled));
                    cc_ct_format(s.ctype, new_spelled, sizeof(new_spelled));
                    p->diag->line = p->tok.line;
                    p->diag->col = p->tok.col;
                    snprintf(p->diag->message, sizeof(p->diag->message),
                             "duplicate typedef name in this scope: %s (was '%s', now '%s')", s.decl_name,
                             old_spelled, new_spelled);
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
                if (var_push(p, s.decl_name, s.ctype) != 0) {
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
                cc_diag_report(p->diag);
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
                    cc_diag_report(p->diag);
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
            cc_diag_report(p->diag);
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
        local_label_pop_to_depth(p, saved_depth);
        p->scope_depth = saved_depth;
        return -1;
    }
    typedef_pop_to_depth(p, saved_depth);
    var_pop_to_depth(p, saved_depth);
    local_label_pop_to_depth(p, saved_depth);
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

static int parse_stmt_impl(parser_t *p, cc_stmt_t *s);

static int parse_stmt(parser_t *p, cc_stmt_t *s) {
    int rc;
    if (p->parse_depth >= CC_MAX_PARSE_DEPTH) {
        set_diag(p->diag, p->tok.line, p->tok.col,
                 "statement nesting depth exceeds limit");
        return -1;
    }
    p->parse_depth++;
    rc = parse_stmt_impl(p, s);
    p->parse_depth--;
    return rc;
}

static int parse_stmt_impl(parser_t *p, cc_stmt_t *s) {
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
            if (local_label_push(p, p->tok.start, p->tok.len) != 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "out of memory recording local label");
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
        s->label_name = local_label_dup_mapped_n(p, p->tok.start, p->tok.len);
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
        long long hi = 0;
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
            if (eval_const_case_value(p, hi_expr, &hi) != 0) {
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
        s->label_name = local_label_dup_mapped_n(p, p->tok.start, p->tok.len);
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
    free(f->nest_parent);
    free(f->attr_section);
    free(f->attr_alias);
    for (i = 0; i < f->param_count; ++i) {
        free(f->params[i].name);
        cc_bounds_free(&f->params[i].vla_bounds);
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
        free(tu->structs[i].layout);
    }
    free(tu->structs);
    tu->structs = NULL;
    tu->struct_count = 0;
}

static int can_parse_oldstyle_param_names(parser_t *p) {
    parser_t q;

    if (p == NULL || p->tok.kind != TOK_IDENT || typedef_find_visible_n(p, p->tok.start, p->tok.len) >= 0) {
        return 0;
    }
    q = *p;
    q.diag = NULL;
    for (;;) {
        if (q.tok.kind != TOK_IDENT) {
            return 0;
        }
        if (next_tok(&q) != 0) {
            return 0;
        }
        if (q.tok.kind == TOK_COMMA) {
            if (next_tok(&q) != 0) {
                return 0;
            }
            continue;
        }
        break;
    }
    if (q.tok.kind != TOK_RPAREN) {
        return 0;
    }
    if (next_tok(&q) != 0) {
        return 0;
    }
    if (skip_decl_gnu_suffix(&q, NULL) != 0) {
        return 0;
    }
    if (q.tok.kind == TOK_LBRACE || is_declspec_start(&q)) {
        return 1;
    }
    return 0;
}

/**
 * Parse one parameter's declarator, after its specifiers, and give the
 * parameter's type as the function sees it -- an array adjusted to a
 * pointer to its element, a function to a pointer to itself (C11
 * 6.7.6.3p7-8).
 *
 * @param p                 the parser, at the declarator (which may be
 *                          abstract)
 * @param base              the specifiers' type (parser_t.last_spec_ctype)
 * @param out_name          receives the name (owned), or NULL if none
 * @param out_ctype         receives the adjusted type
 * @param out_saw_restrict  receives whether a pointer level, or the
 *                          adjusted array, is restrict-qualified
 * @param out_vla_bounds    for a pointer to an array, receives the
 *                          run-time bounds of that array; empty otherwise
 * @return                  0, or -1 on error (diagnosed)
 */
static int parse_param_decl(parser_t *p, const cc_ctype_t *base, char **out_name, const cc_ctype_t **out_ctype,
                            int *out_saw_restrict, cc_bounds_t *out_vla_bounds) {
    declarator_t d;
    const cc_ctype_t *t;
    size_t line = p->tok.line;
    size_t col = p->tok.col;
    size_t i;

    *out_name = NULL;
    memset(out_vla_bounds, 0, sizeof(*out_vla_bounds));
    memset(&d, 0, sizeof(d));
    if (parse_declarator_into(p, &d, DECL_ABSTRACT | DECL_PARAM) != 0) {
        declarator_free(&d);
        return -1;
    }
    t = declarator_type(&d, base);
    if (t != NULL && t->kind == CC_CT_ARRAY) {
        t = cc_ct_qualified(cc_ct_pointer(t->of), d.saw_restrict ? CC_CQ_RESTRICT : 0);
    } else if (t != NULL && t->kind == CC_CT_FUNCTION) {
        t = cc_ct_pointer(t);
    }
    if (t == NULL) {
        set_diag(p->diag, line, col, "out of memory building a parameter's type");
        declarator_free(&d);
        return -1;
    }
    /* a pointer to an array: its outermost op is the pointer, or the array adjusted to one */
    if (t->kind == CC_CT_POINTER && take_vla_bounds(&d, t->of, 1, out_vla_bounds) != 0) {
        set_diag(p->diag, line, col, "out of memory recording array bounds");
        declarator_free(&d);
        return -1;
    }
    *out_saw_restrict = d.saw_restrict;
    for (i = 0; i < d.count; ++i) {
        if (d.ops[i].kind == DECL_OP_POINTER && (d.ops[i].quals & CC_CQ_RESTRICT) != 0) {
            *out_saw_restrict = 1;
        }
    }
    *out_ctype = t;
    *out_name = d.name;
    d.name = NULL;
    declarator_free(&d);
    return 0;
}

static int parse_params(parser_t *p, cc_function_t *f, int allow_oldstyle_names) {
    if (allow_oldstyle_names && can_parse_oldstyle_param_names(p) &&
        p->tok.kind == TOK_IDENT &&
        typedef_find_visible_n(p, p->tok.start, p->tok.len) < 0) {
        cc_tok_kind_t k = peek_kind(p);
        if (k == TOK_COMMA || k == TOK_RPAREN) {
            for (;;) {
                if (p->tok.kind != TOK_IDENT) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "expected old-style parameter name");
                    return -1;
                }
                if (push_param(f, cc_ct_basic(CC_CT_INT), p->tok.start, p->tok.len, 0) != 0) {
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
        const cc_ctype_t *pspec = NULL;
        char *pname = NULL;
        int param_storage = 0;
        int ptype_restrict = 0;
        int dty_restrict = 0;
        const cc_ctype_t *pctype = NULL;
        char anon_buf[32];
        cc_bounds_t pvla_bounds;
        int pushed;

        if (p->tok.kind == TOK_ELLIPSIS) {
            f->is_variadic = 1;
            f->has_prototype = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            break;
        }

        if (parse_declspec(p, &pspec, 1, "expected parameter type", NULL, &ptype_restrict, NULL) != 0) {
            return -1;
        }
        param_storage = p->last_storage;
        if (parse_param_decl(p, pspec, &pname, &pctype, &dty_restrict, &pvla_bounds) != 0) {
            return -1;
        }
        if ((ptype_restrict || dty_restrict) && (pctype == NULL || cc_ct_decay(pctype)->kind != CC_CT_POINTER)) {
            set_diag(p->diag, p->tok.line, p->tok.col, "restrict qualifier requires a pointer parameter type");
            pushed = -1;
        } else if (skip_decl_gnu_suffix(p, NULL) != 0) {
            pushed = -1;
        } else {
            const char *name = pname;

            if (name == NULL) {
                snprintf(anon_buf, sizeof(anon_buf), "__anon_param_%zu", f->param_count);
                name = anon_buf;
            }
            pushed = push_param(f, pctype, name, strlen(name), param_storage);
        }
        free(pname);
        if (pushed != 0) {
            cc_bounds_free(&pvla_bounds);
            return -1;
        }
        f->params[f->param_count - 1].vla_bounds = pvla_bounds;
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

/**
 * Parse a function definition's (or declaration's) own parameter list,
 * `(` through `)`, into the function: the declarator parser calls this
 * at the list that directly follows the function's name.
 *
 * @param p    the parser, at `(`
 * @param ctx  the cc_function_t being parsed
 * @return     0, or -1 on error (diagnosed)
 */
static int parse_function_params(parser_t *p, void *ctx) {
    cc_function_t *f = (cc_function_t *)ctx;

    if (expect(p, TOK_LPAREN, "expected '(' after function name") != 0) {
        return -1;
    }
    if (p->tok.kind != TOK_RPAREN && parse_params(p, f, 1) != 0) {
        return -1;
    }
    if (expect(p, TOK_RPAREN, "expected ')' after parameter list") != 0) {
        return -1;
    }
    if (parser_is_c23_or_newer() && f->param_count == 0 && !f->is_variadic && !f->has_prototype) {
        /* in C23 an empty parameter list is a prototype for a parameterless function */
        f->has_prototype = 1;
    }
    return 0;
}

/**
 * Parse a function definition's declarator, after the return type's
 * specifiers: the name, its parameter list (into f, by
 * parse_function_params()), and whatever the declarator wraps around
 * them -- `int (*f(void))[4]` returns a pointer to an array,
 * `int (*(*f(void))(int))(int)` a pointer to a function returning a
 * function pointer.
 *
 * @param p          the parser, at the declarator
 * @param f          the function; receives name, params and ret_ctype
 * @param base       the specifiers' type
 * @return           0, or -1 on error (diagnosed)
 */
static int parse_function_declarator(parser_t *p, cc_function_t *f, const cc_ctype_t *base) {
    declarator_t d;
    const cc_ctype_t *t;
    size_t line = p->tok.line;
    size_t col = p->tok.col;

    memset(&d, 0, sizeof(d));
    d.name_params = parse_function_params;
    d.name_params_ctx = f;
    if (parse_declarator_into(p, &d, 0) != 0) {
        declarator_free(&d);
        return -1;
    }
    t = declarator_type(&d, base);
    if (d.name == NULL || t == NULL || t->kind != CC_CT_FUNCTION) {
        set_diag(p->diag, line, col, d.name == NULL ? "expected function name" : "expected '(' after function name");
        declarator_free(&d);
        return -1;
    }
    f->ret_ctype = t->of;
    f->name = d.name;
    d.name = NULL;
    declarator_free(&d);
    return 0;
}

static int parse_function(parser_t *p, cc_function_t *f) {
    int is_typedef = 0;
    int fn_storage = 0;
    const cc_ctype_t *base;
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
    f->has_oldstyle_param_decls = 0;
    decl_attrs_reset(&spec_attrs);
    decl_attrs_reset(&suffix_attrs);
    decl_attrs_reset(&merged_attrs);

    if (is_declspec_start(p)) {
        if (parse_declspec(p, &base, 1, "expected function return type", &is_typedef, NULL, &spec_attrs) != 0) {
            return -1;
        }
        fn_storage = p->last_storage;
        if (is_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef is not valid in function definition");
            decl_attrs_clear(&spec_attrs);
            return -1;
        }
    } else if (!cc_lang_at_least(1999)) {
        /*
         * An omitted return type (`main() { ... }` => `int main() { ... }`)
         * is implicit int: C89 only, as gcc has it.
         */
        base = cc_ct_basic(CC_CT_INT);
        fn_storage = 0;
        is_typedef = 0;
    } else {
        set_diag(p->diag, p->tok.line, p->tok.col, "expected function return type");
        return -1;
    }
    if (parse_function_declarator(p, f, base) != 0) {
        decl_attrs_clear(&spec_attrs);
        return -1;
    }
    f->storage = fn_storage;
    if ((f->storage & CC_STORAGE_INLINE) != 0 && (f->storage & CC_STORAGE_STATIC) == 0 && cc_options_gnu89_inline()) {
        /*
         * GNU89 inline is C99's reversed: plain `inline` is an external
         * definition, and `extern inline` is for inlining only -- what C99
         * calls an inline definition, which cc emits as a local copy and
         * no external symbol.
         */
        f->storage ^= CC_STORAGE_EXTERN;
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
    f->attr_format_index = merged_attrs.format_index;
    f->attr_format_first_to_check = merged_attrs.format_first_to_check;
    if (merged_attrs.section != NULL) {
        f->attr_section = cc_strndup(merged_attrs.section, strlen(merged_attrs.section));
        if (f->attr_section == NULL) {
            decl_attrs_clear(&spec_attrs);
            decl_attrs_clear(&suffix_attrs);
            decl_attrs_clear(&merged_attrs);
            return -1;
        }
    }
    if (merged_attrs.alias != NULL) {
        f->attr_alias = cc_strndup(merged_attrs.alias, strlen(merged_attrs.alias));
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
    /* old-style (K&R) definitions: C up to C17, and gcc takes them in C23 too */
    if (!f->has_prototype && p->tok.kind != TOK_LBRACE && p->tok.kind != TOK_EOF &&
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
    if (oldstyle_decl_count > 0) {
        f->has_oldstyle_param_decls = 1;
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
        /* its declared type, adjusted as a parameter's is: an array or a function to a pointer */
        f->params[pidx].ctype = ds->ctype != NULL ? cc_ct_decay(ds->ctype) : NULL;
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
            if (f->params[i].ctype != NULL && f->params[i].ctype->unqual->kind == CC_CT_VOID) {
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
            if (f->params[i].name != NULL && var_push(p, f->params[i].name, f->params[i].ctype) != 0) {
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
                cc_diag_report(p->diag);
                if (parser_sync_block_stmt(p) != 0) {
                    return -1;
                }
                continue;
            }
            nested.storage |= CC_STORAGE_STATIC;
            /* where it is: what it sees of f is f's parameters and the declarations before it */
            nested.nest_parent = f->name != NULL ? cc_strdup(f->name) : NULL;
            nested.nest_stmt_index = f->stmt_count;
            if ((f->name != NULL && nested.nest_parent == NULL) || parser_push_hoisted_func(p, &nested) != 0) {
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
                    cc_diag_report(p->diag);
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
            cc_diag_report(p->diag);
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
    local_label_pop_to_depth(p, saved_depth);
    struct_hide_to_depth(p, saved_depth);
    p->scope_depth = saved_depth;
    return 0;
}

/**
 * The parameter-list hook probe_is_function_head() gives the declarator
 * parser: skip the list and note that the name had one.
 *
 * @param p    the parser, at `(`
 * @param ctx  an int set to 1
 * @return     0, or -1 if the list is unbalanced
 */
static int probe_skip_params(parser_t *p, void *ctx) {
    *(int *)ctx = 1;
    return skip_balanced_parens(p);
}

/**
 * Whether the declaration at the parser declares a function -- its
 * declarator's name directly followed by a parameter list -- rather than
 * an object.  Parses a copy of the parser, so consumes nothing.
 *
 * @param p  the parser, at the declaration's specifiers
 * @return   1 if it declares a function, else 0
 */
static int probe_is_function_head(parser_t *p) {
    parser_t q = *p;
    int is_typedef = 0;
    declarator_t d;
    int rc = 0;
    const char *lf = p->lx.logical_file;

    q.diag = NULL;
    q.lx.logical_file = lf != NULL ? cc_strndup(lf, strlen(lf)) : NULL;
    if (lf != NULL && q.lx.logical_file == NULL) {
        return 0;
    }
    q.structs = NULL;
    q.struct_count = 0;
    q.struct_cap = 0;
    q.is_probe = 1;
    q.enum_consts = NULL;
    q.enum_const_count = 0;
    q.enum_const_cap = 0;
    q.enum_tags = NULL;
    q.enum_tag_count = 0;
    q.enum_tag_cap = 0;
    if (parse_declspec(&q, NULL, 1, "", &is_typedef, NULL, NULL) != 0) {
        goto done;
    }
    if (is_typedef) {
        goto done;
    }
    /* a function iff the name is directly followed by a parameter list, however deeply it is nested */
    memset(&d, 0, sizeof(d));
    d.name_params = probe_skip_params;
    d.name_params_ctx = &rc;
    if (parse_declarator_into(&q, &d, 0) != 0) {
        rc = 0;
    }
    declarator_free(&d);
done:
    parser_free_enum_consts(&q);
    parser_free_enum_tags(&q);
    parser_free_structs(&q);
    cc_lexer_deinit(&q.lx);
    return rc;
}

int cc_parse_file(const char *path, cc_translation_unit_t *out, cc_diag_t *diag) {
    FILE *fp;
    size_t sz;
    char *buf;
    parser_t p;
    size_t cap;
    size_t nread;

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
    cap = 4096;
    buf = (char *)malloc(cap + 1);
    if (buf == NULL) {
        fclose(fp);
        set_diag(diag, 0, 0, "out of memory");
        return -1;
    }
    sz = 0;
    while ((nread = fread(buf + sz, 1, cap - sz, fp)) > 0) {
        sz += nread;
        if (sz == cap) {
            size_t new_cap;
            char *new_buf;
            if (cap > ((size_t)-1) / 2 - 1) {
                free(buf);
                fclose(fp);
                set_diag(diag, 0, 0, "source file too large");
                return -1;
            }
            new_cap = cap * 2;
            new_buf = (char *)realloc(buf, new_cap + 1);
            if (new_buf == NULL) {
                free(buf);
                fclose(fp);
                set_diag(diag, 0, 0, "out of memory");
                return -1;
            }
            buf = new_buf;
            cap = new_cap;
        }
    }
    if (ferror(fp)) {
        free(buf);
        fclose(fp);
        set_diag(diag, 0, 0, "failed to read source file");
        return -1;
    }
    buf[sz] = '\0';
    fclose(fp);
    /* trigraphs were the preprocessor's to replace (translation phase 1) */

    memset(&p, 0, sizeof(p));
    p.diag = diag;
    p.tu = out;
    p.scope_depth = 0;
    cc_lexer_init(&p.lx, buf, sz, path);
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
        typedef_push(&p, "nullptr_t", cc_ct_pointer(cc_ct_basic(CC_CT_VOID))) != 0) {
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
                cc_diag_report(diag);
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
                cc_diag_report(diag);
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
                cc_diag_report(diag);
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
                g.storage = decls[di].storage;
                g.attr_flags = decls[di].attr_flags;
                g.attr_align = decls[di].attr_align;
                g.attr_section = decls[di].attr_section;
                decls[di].attr_section = NULL;
                g.attr_alias = decls[di].attr_alias;
                decls[di].attr_alias = NULL;
                g.init = decls[di].expr;
                decls[di].expr = NULL;
                g.ctype = decls[di].ctype;
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
            cc_diag_report(diag);
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
    parser_free_local_labels(&p);
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
