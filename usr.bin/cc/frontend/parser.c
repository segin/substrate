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
    int int_is_longlong;
    size_t line;
    size_t col;
} cc_token_t;

typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
    int depth;
} typedef_entry_t;

typedef struct {
    char *name;
    long value;
} enum_const_entry_t;

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
    typedef_entry_t *typedefs;
    size_t typedef_count;
    size_t typedef_cap;
    cc_struct_def_t *structs;
    size_t struct_count;
    size_t struct_cap;
    enum_const_entry_t *enum_consts;
    size_t enum_const_count;
    size_t enum_const_cap;
    int scope_depth;
    int last_storage;
} parser_t;

typedef struct {
    int flags;
    long align;
    char *section;
} decl_attrs_t;

static int g_parser_pointer_size_bytes = 8;
static int g_parser_enable_trigraphs = 1;
static int g_parser_allow_oldstyle_funcdecl = 0;
static int g_parser_std_c11 = 0;
static int g_parser_std_c17 = 0;
static int g_parser_std_c23 = 0;
static int is_decl_qual_tok(cc_tok_kind_t k);
static int is_decl_qual_at_token(parser_t *p);
static cc_type_t ptr_of_type(cc_type_t t);
static int tok_is_ident(parser_t *p, const char *s);
static void decl_attrs_reset(decl_attrs_t *a);
static void decl_attrs_clear(decl_attrs_t *a);
static int decl_attrs_merge(decl_attrs_t *dst, const decl_attrs_t *src);
static int skip_cxx_attribute_seq(parser_t *p);
static int skip_balanced_parens(parser_t *p);
static int skip_decl_gnu_suffix(parser_t *p, decl_attrs_t *out_attrs);
static int parse_type_name(parser_t *p, cc_type_t *out_type, int *out_struct_id, int allow_void, const char *what);
static cc_expr_t *parse_expr(parser_t *p);
static cc_expr_t *parse_cond(parser_t *p);
static void free_expr(cc_expr_t *e);
static int eval_const_array_bound_expr(const cc_expr_t *e, long *out);
static int parse_array_extent(parser_t *p, long *out_n, int *out_const_n);

static int parser_is_c11_or_newer(void) {
    return g_parser_std_c11 || g_parser_std_c17 || g_parser_std_c23;
}

static int parser_is_c23_or_newer(void) {
    return g_parser_std_c23;
}

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

static int peek_tok(parser_t *p, cc_token_t *out) {
    cc_lexer_t lx = p->lx;
    return cc_lexer_next(&lx, out);
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

static int typedef_push(parser_t *p, const char *name, cc_type_t type, int struct_id) {
    typedef_entry_t *next;
    char *dup;
    int existing;

    existing = typedef_find_current_scope_n(p, name, strlen(name));
    if (existing >= 0) {
        if (p->typedefs[existing].type == type && p->typedefs[existing].struct_id == struct_id) {
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

static int is_pointer_type(cc_type_t t) {
    return t >= CC_TYPE_PTR_VOID && t <= CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE;
}

static long scalar_type_size_bytes(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_UCHAR:
        return 1;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 2;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_FLOAT:
        return 4;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_DOUBLE:
        return 8;
    default:
        return -1;
    }
}

static long parser_type_size_bytes(const parser_t *p, cc_type_t t, int struct_id) {
    long n;
    if (is_pointer_type(t)) {
        return g_parser_pointer_size_bytes;
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
    long n = parser_type_size_bytes(p, t, struct_id);
    if (n <= 0) {
        return -1;
    }
    if (n > g_parser_pointer_size_bytes) {
        n = g_parser_pointer_size_bytes;
    }
    return n;
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
        if (msize <= 0) {
            msize = parser_type_size_bytes(p, sd->members[i].type, sd->members[i].type_struct_id);
        }
        if (msize <= 0) {
            msize = g_parser_pointer_size_bytes;
        }
        if (malign <= 0) {
            malign = g_parser_pointer_size_bytes;
        }
        if (packed) {
            malign = 1;
        }
        off = align_up_long(off, malign);
        sd->members[i].offset = off;
        off += msize;
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

static int struct_find_tag_n(const parser_t *p, const char *tag, size_t len) {
    size_t i;
    for (i = 0; i < p->struct_count; ++i) {
        if (p->structs[i].tag == NULL) {
            continue;
        }
        if (strlen(p->structs[i].tag) == len && strncmp(p->structs[i].tag, tag, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int struct_ensure(parser_t *p, const char *tag, size_t len) {
    cc_struct_def_t *next;
    char *dup = NULL;
    int idx;
    if (tag != NULL) {
        idx = struct_find_tag_n(p, tag, len);
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
    p->structs[p->struct_count].complete = 0;
    return (int)p->struct_count++;
}

static int struct_member_push(parser_t *p, int sid, const char *name, cc_type_t type, int type_struct_id, long offset,
                              long size) {
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
    sd->members[sd->member_count].offset = offset;
    sd->members[sd->member_count].size = size;
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

static int is_declspec_ident(parser_t *p) {
    if (tok_is_ident(p, "_Atomic") || tok_is_ident(p, "_Thread_local") || tok_is_ident(p, "_Alignas") ||
        tok_is_ident(p, "_Noreturn") || tok_is_ident(p, "_BitInt") || tok_is_ident(p, "_Decimal32") ||
        tok_is_ident(p, "_Decimal64") || tok_is_ident(p, "_Decimal128")) {
        return 1;
    }
    if (parser_is_c23_or_newer() &&
        (tok_is_ident(p, "bool") || tok_is_ident(p, "thread_local") || tok_is_ident(p, "alignas") ||
         tok_is_ident(p, "constexpr") || tok_is_ident(p, "typeof") || tok_is_ident(p, "typeof_unqual"))) {
        return 1;
    }
    return 0;
}

static int is_declspec_start(parser_t *p) {
    return is_declspec_tok(p->tok.kind) || is_declspec_ident(p) ||
           (p->tok.kind == TOK_IDENT && typedef_find_visible_n(p, p->tok.start, p->tok.len) >= 0) ||
           tok_is_ident(p, "__attribute__") ||
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
        if (typedef_find_visible_n(p, t.start, t.len) >= 0) {
            return 1;
        }
        if ((t.len == strlen("_Atomic") && strncmp(t.start, "_Atomic", t.len) == 0) ||
            (t.len == strlen("_BitInt") && strncmp(t.start, "_BitInt", t.len) == 0) ||
            (t.len == strlen("typeof") && strncmp(t.start, "typeof", t.len) == 0) ||
            (t.len == strlen("typeof_unqual") && strncmp(t.start, "typeof_unqual", t.len) == 0) ||
            (parser_is_c23_or_newer() && t.len == strlen("bool") && strncmp(t.start, "bool", t.len) == 0)) {
            return 1;
        }
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
    case TOK_KW_EXTENSION:
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

static int parse_declspec(parser_t *p, cc_type_t *out_type, int *out_struct_id, int allow_void, const char *what,
                          int *out_typedef, decl_attrs_t *out_attrs) {
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
    int seen_bitint = 0;
    long bitint_width = 0;
    int alias_struct_id = -1;
    int seen_struct_id = -1;
    cc_type_t alias_type = CC_TYPE_VOID;
#define RETURN_OK()          \
    do {                     \
        p->last_storage = storage_flags; \
        return 0;            \
    } while (0)

    p->last_storage = 0;
    if (out_typedef != NULL) {
        *out_typedef = 0;
    }
    if (out_struct_id != NULL) {
        *out_struct_id = -1;
    }
    if (out_attrs != NULL) {
        out_attrs->flags = 0;
        out_attrs->align = 0;
        out_attrs->section = NULL;
    }

    while (1) {
        if ((p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) || tok_is_ident(p, "__attribute__") ||
            tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm")) {
            if (skip_decl_gnu_suffix(p, out_attrs) != 0) {
                return -1;
            }
            continue;
        }
        if (p->tok.kind == TOK_IDENT) {
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
                    if (eval_const_array_bound_expr(ae, &align) != 0 || align <= 0) {
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
                    seen_alias = 1;
                    alias_type = aty;
                    alias_struct_id = asid;
                    continue;
                }
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
                seen_double = 1;
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
                if (eval_const_array_bound_expr(we, &w) != 0 || w <= 0) {
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
            if (parser_is_c23_or_newer() && (tok_is_ident(p, "typeof") || tok_is_ident(p, "typeof_unqual"))) {
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
                    ty = te->value_type;
                    sid = te->struct_id;
                    free_expr(te);
                }
                if (expect(p, TOK_RPAREN, "expected ')' after typeof/typeof_unqual") != 0) {
                    return -1;
                }
                alias_type = ty;
                alias_struct_id = sid;
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
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
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
        case TOK_KW_STRUCT:
        case TOK_KW_UNION: {
            int sid;
            int brace_depth = 0;
            const char *tag_start = NULL;
            size_t tag_len = 0;
            long off = 0;
            long max_align = 1;
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
            sid = struct_ensure(p, tag_start, tag_len);
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
                int mtypedef = 0;
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
                if (!is_declspec_start(p)) {
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
                if (parse_declspec(p, &mbase, &mbase_sid, 1, "expected member declaration type", &mtypedef, NULL) !=
                    0) {
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
                    long arr_count = 1;
                    int arr_saw = 0;
                    int arr_unsized = 0;
                    long msize;
                    long malign;
                    long moff;
                    int is_bitfield = 0;
                    int skip_member_decl = 0;
                    decl_attrs_t mdecl_attrs;
                    decl_attrs_reset(&mdecl_attrs);
                    while (p->tok.kind == TOK_STAR) {
                        mtype = ptr_of_type(mtype);
                        if (mtype == CC_TYPE_VOID) {
                            set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
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
                    if (p->tok.kind == TOK_LPAREN && peek_kind(p) == TOK_STAR) {
                        if (next_tok(p) != 0) {
                            return -1;
                        }
                        while (p->tok.kind == TOK_STAR) {
                            mtype = ptr_of_type(mtype);
                            if (mtype == CC_TYPE_VOID) {
                                set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
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
                        int paren_depth = 0;
                        int brack_depth = 0;
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
                        while (!(paren_depth == 0 && brack_depth == 0 &&
                                 (p->tok.kind == TOK_COMMA || p->tok.kind == TOK_SEMI))) {
                            if (p->tok.kind == TOK_EOF) {
                                set_diag(p->diag, p->tok.line, p->tok.col, "unterminated bit-field width");
                                free(mname);
                                return -1;
                            }
                            if (p->tok.kind == TOK_LPAREN) {
                                paren_depth++;
                            } else if (p->tok.kind == TOK_RPAREN) {
                                if (paren_depth > 0) {
                                    paren_depth--;
                                }
                            } else if (p->tok.kind == TOK_LBRACK) {
                                brack_depth++;
                            } else if (p->tok.kind == TOK_RBRACK) {
                                if (brack_depth > 0) {
                                    brack_depth--;
                                }
                            }
                            if (next_tok(p) != 0) {
                                free(mname);
                                return -1;
                            }
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
                            if (const_extent && n > 0 && arr_count > 0) {
                                arr_count *= n;
                            }
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
                    if (msize <= 0) {
                        msize = g_parser_pointer_size_bytes;
                    }
                    if (malign <= 0) {
                        malign = g_parser_pointer_size_bytes;
                    }
                    if ((mdecl_attrs.flags & CC_ATTR_PACKED) != 0) {
                        malign = 1;
                    }
                    if ((mdecl_attrs.flags & CC_ATTR_ALIGNED) != 0 && mdecl_attrs.align > malign) {
                        malign = mdecl_attrs.align;
                    }
                    if (arr_saw) {
                        cc_type_t ptype = ptr_of_type(mtype);
                        if (ptype != CC_TYPE_VOID) {
                            mtype = ptype;
                        } else {
                            mtype = CC_TYPE_PTR_VOID;
                        }
                        if (arr_unsized) {
                            if (is_union) {
                                set_diag(p->diag, p->tok.line, p->tok.col,
                                         "flexible array member is not allowed in unions");
                                decl_attrs_clear(&mdecl_attrs);
                                return -1;
                            }
                            if (is_bitfield || mname == NULL) {
                                set_diag(p->diag, p->tok.line, p->tok.col,
                                         "flexible array member must be a named non-bitfield member");
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
                        }
                    }
                    moff = is_union ? 0 : align_up_long(off, malign);
                    if (!skip_member_decl && mname != NULL) {
                        if (struct_member_push(p, sid, mname, mtype, mstruct, moff, msize) != 0) {
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
                            if (struct_member_push(p, sid, am->name, am->type, am->type_struct_id, moff + am->offset,
                                                   am->size) != 0) {
                                decl_attrs_clear(&mdecl_attrs);
                                return -1;
                            }
                        }
                    }
                    if (!skip_member_decl) {
                        if (is_union) {
                            if (msize > off) {
                                off = msize;
                            }
                        } else if (msize > 0) {
                            off = moff + msize;
                        }
                        if (malign > max_align) {
                            max_align = malign;
                        }
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
            seen_type = 1;
            seen_int = 1;
            seen_opaque_tag = 0;
            seen_struct_id = -1;
            if (next_tok(p) != 0) {
                return -1;
            }
            if (p->tok.kind == TOK_IDENT) {
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
            }
            if (p->tok.kind != TOK_LBRACE) {
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
                if (enum_const_push(p, p->tok.start, p->tok.len, enum_next) != 0) {
                    return -1;
                }
                if (next_tok(p) != 0) {
                    return -1;
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
                    if (eval_const_array_bound_expr(enum_expr, &v) != 0) {
                        free_expr(enum_expr);
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "enum value must be an integer constant expression");
                        return -1;
                    }
                    free_expr(enum_expr);
                    enum_next = v;
                    p->enum_consts[p->enum_const_count - 1].value = enum_next;
                }
                enum_next++;
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
        set_diag(p->diag, p->tok.line, p->tok.col, "expected type specifier in declaration");
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
        *out_type = bitint_width <= 32 ? (seen_unsigned ? CC_TYPE_UINT : CC_TYPE_INT)
                                       : (seen_unsigned ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG);
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
        (seen_float || seen_double || seen_complex || seen_imaginary || seen_bool || seen_void)) {
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
            seen_opaque_tag) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid complex/imaginary type combination");
            return -1;
        }
        if (seen_long > 0 && !seen_double) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid complex/imaginary long type combination");
            return -1;
        }
        *out_type = seen_float ? CC_TYPE_FLOAT : CC_TYPE_DOUBLE;
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
        *out_type = CC_TYPE_DOUBLE;
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
        *out_type = seen_unsigned ? CC_TYPE_UCHAR : CC_TYPE_CHAR;
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
        *out_type = seen_unsigned ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG;
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

static cc_type_t ptr_of_type(cc_type_t t) {
    switch (t) {
    case CC_TYPE_VOID:
        return CC_TYPE_PTR_VOID;
    case CC_TYPE_BOOL:
        return CC_TYPE_PTR_BOOL;
    case CC_TYPE_CHAR:
        return CC_TYPE_PTR_CHAR;
    case CC_TYPE_UCHAR:
        return CC_TYPE_PTR_UCHAR;
    case CC_TYPE_SHORT:
        return CC_TYPE_PTR_SHORT;
    case CC_TYPE_USHORT:
        return CC_TYPE_PTR_USHORT;
    case CC_TYPE_INT:
        return CC_TYPE_PTR_INT;
    case CC_TYPE_UINT:
        return CC_TYPE_PTR_UINT;
    case CC_TYPE_LONG_LONG:
        return CC_TYPE_PTR_LONG_LONG;
    case CC_TYPE_ULONG_LONG:
        return CC_TYPE_PTR_ULONG_LONG;
    case CC_TYPE_FLOAT:
        return CC_TYPE_PTR_FLOAT;
    case CC_TYPE_DOUBLE:
        return CC_TYPE_PTR_DOUBLE;
    case CC_TYPE_PTR_VOID:
        return CC_TYPE_PTR_PTR_VOID;
    case CC_TYPE_PTR_BOOL:
        return CC_TYPE_PTR_PTR_BOOL;
    case CC_TYPE_PTR_CHAR:
        return CC_TYPE_PTR_PTR_CHAR;
    case CC_TYPE_PTR_UCHAR:
        return CC_TYPE_PTR_PTR_UCHAR;
    case CC_TYPE_PTR_SHORT:
        return CC_TYPE_PTR_PTR_SHORT;
    case CC_TYPE_PTR_USHORT:
        return CC_TYPE_PTR_PTR_USHORT;
    case CC_TYPE_PTR_INT:
        return CC_TYPE_PTR_PTR_INT;
    case CC_TYPE_PTR_UINT:
        return CC_TYPE_PTR_PTR_UINT;
    case CC_TYPE_PTR_LONG_LONG:
        return CC_TYPE_PTR_PTR_LONG_LONG;
    case CC_TYPE_PTR_ULONG_LONG:
        return CC_TYPE_PTR_PTR_ULONG_LONG;
    case CC_TYPE_PTR_FLOAT:
        return CC_TYPE_PTR_PTR_FLOAT;
    case CC_TYPE_PTR_DOUBLE:
        return CC_TYPE_PTR_PTR_DOUBLE;
    case CC_TYPE_PTR_PTR_VOID:
        return CC_TYPE_PTR_PTR_PTR_VOID;
    case CC_TYPE_PTR_PTR_BOOL:
        return CC_TYPE_PTR_PTR_PTR_BOOL;
    case CC_TYPE_PTR_PTR_CHAR:
        return CC_TYPE_PTR_PTR_PTR_CHAR;
    case CC_TYPE_PTR_PTR_UCHAR:
        return CC_TYPE_PTR_PTR_PTR_UCHAR;
    case CC_TYPE_PTR_PTR_SHORT:
        return CC_TYPE_PTR_PTR_PTR_SHORT;
    case CC_TYPE_PTR_PTR_USHORT:
        return CC_TYPE_PTR_PTR_PTR_USHORT;
    case CC_TYPE_PTR_PTR_INT:
        return CC_TYPE_PTR_PTR_PTR_INT;
    case CC_TYPE_PTR_PTR_UINT:
        return CC_TYPE_PTR_PTR_PTR_UINT;
    case CC_TYPE_PTR_PTR_LONG_LONG:
        return CC_TYPE_PTR_PTR_PTR_LONG_LONG;
    case CC_TYPE_PTR_PTR_ULONG_LONG:
        return CC_TYPE_PTR_PTR_PTR_ULONG_LONG;
    case CC_TYPE_PTR_PTR_FLOAT:
        return CC_TYPE_PTR_PTR_PTR_FLOAT;
    case CC_TYPE_PTR_PTR_DOUBLE:
        return CC_TYPE_PTR_PTR_PTR_DOUBLE;
    case CC_TYPE_PTR_PTR_PTR_VOID:
        return CC_TYPE_PTR_PTR_PTR_PTR_VOID;
    case CC_TYPE_PTR_PTR_PTR_BOOL:
        return CC_TYPE_PTR_PTR_PTR_PTR_BOOL;
    case CC_TYPE_PTR_PTR_PTR_CHAR:
        return CC_TYPE_PTR_PTR_PTR_PTR_CHAR;
    case CC_TYPE_PTR_PTR_PTR_UCHAR:
        return CC_TYPE_PTR_PTR_PTR_PTR_UCHAR;
    case CC_TYPE_PTR_PTR_PTR_SHORT:
        return CC_TYPE_PTR_PTR_PTR_PTR_SHORT;
    case CC_TYPE_PTR_PTR_PTR_USHORT:
        return CC_TYPE_PTR_PTR_PTR_PTR_USHORT;
    case CC_TYPE_PTR_PTR_PTR_INT:
        return CC_TYPE_PTR_PTR_PTR_PTR_INT;
    case CC_TYPE_PTR_PTR_PTR_UINT:
        return CC_TYPE_PTR_PTR_PTR_PTR_UINT;
    case CC_TYPE_PTR_PTR_PTR_LONG_LONG:
        return CC_TYPE_PTR_PTR_PTR_PTR_LONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_ULONG_LONG:
        return CC_TYPE_PTR_PTR_PTR_PTR_ULONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_FLOAT:
        return CC_TYPE_PTR_PTR_PTR_PTR_FLOAT;
    case CC_TYPE_PTR_PTR_PTR_DOUBLE:
        return CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE;
    default:
        return CC_TYPE_VOID;
    }
}

static int parse_named_declarator(parser_t *p, cc_type_t base_type, cc_type_t *out_type, char **out_name,
                                  const char *name_err) {
    cc_type_t ty = base_type;
    while (p->tok.kind == TOK_STAR) {
        ty = ptr_of_type(ty);
        if (ty == CC_TYPE_VOID) {
            set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
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
    if (p->tok.kind != TOK_IDENT) {
        set_diag(p->diag, p->tok.line, p->tok.col, name_err);
        return -1;
    }
    *out_name = xstrdup_n(p->tok.start, p->tok.len);
    if (*out_name == NULL) {
        return -1;
    }
    *out_type = ty;
    return next_tok(p);
}

static int eval_const_array_bound_expr(const cc_expr_t *e, long *out) {
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
        return eval_const_array_bound_expr(e->lhs, out);
    case CC_EXPR_SIZEOF:
        st = e->aux_type;
        if (st == CC_TYPE_VOID && e->aux_struct_id < 0 && e->lhs != NULL) {
            st = e->lhs->value_type;
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
            return eval_const_array_bound_expr(e->rhs, out);
        }
        if (eval_const_array_bound_expr(e->lhs, &a) != 0 || eval_const_array_bound_expr(e->rhs, &b) != 0) {
            return -1;
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
        if (eval_const_array_bound_expr(e->lhs, &a) != 0) {
            return -1;
        }
        if (a != 0) {
            return eval_const_array_bound_expr(e->rhs, out);
        }
        return eval_const_array_bound_expr(e->third, out);
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
    if (eval_const_array_bound_expr(bound, &n) == 0 && n > 0) {
        is_const = 1;
    }
    free_expr(bound);
    if (expect(p, TOK_RBRACK, "expected ']' after array declarator") != 0) {
        return -1;
    }
    *out_n = n > 0 ? n : 1;
    *out_const_n = is_const;
    return 0;
}

static int parse_array_suffix(parser_t *p, cc_type_t *io_type, long *out_array_len) {
    long arr_len = -1;
    while (p->tok.kind == TOK_LBRACK) {
        long n = 1;
        int saw_const_n = 0;
        cc_type_t ty = ptr_of_type(*io_type);
        if (ty == CC_TYPE_VOID) {
            set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
            return -1;
        }
        *io_type = ty;
        if (parse_array_extent(p, &n, &saw_const_n) != 0) {
            return -1;
        }
        if (saw_const_n) {
            if (arr_len < 0) {
                arr_len = n;
            } else if (arr_len == 0) {
                arr_len = n;
            } else {
                arr_len *= n;
            }
        } else if (arr_len < 0) {
            arr_len = 0;
        }
    }
    if (out_array_len != NULL) {
        *out_array_len = arr_len;
    }
    return 0;
}

static int tok_is_ident(parser_t *p, const char *s) {
    size_t n = strlen(s);
    return p->tok.kind == TOK_IDENT && p->tok.len == n && strncmp(p->tok.start, s, n) == 0;
}

static void decl_attrs_reset(decl_attrs_t *a) {
    if (a == NULL) {
        return;
    }
    a->flags = 0;
    a->align = 0;
    a->section = NULL;
}

static void decl_attrs_clear(decl_attrs_t *a) {
    if (a == NULL) {
        return;
    }
    free(a->section);
    a->section = NULL;
    a->flags = 0;
    a->align = 0;
}

static int decl_attrs_merge(decl_attrs_t *dst, const decl_attrs_t *src) {
    char *sec_dup = NULL;
    if (dst == NULL || src == NULL) {
        return 0;
    }
    if ((src->flags & CC_ATTR_SECTION) != 0 && src->section != NULL) {
        sec_dup = xstrdup_n(src->section, strlen(src->section));
        if (sec_dup == NULL) {
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

static int skip_cxx_attribute_seq(parser_t *p) {
    int nested = 0;
    if (p->tok.kind != TOK_LBRACK || peek_kind(p) != TOK_LBRACK) {
        return 0;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    if (next_tok(p) != 0) {
        return -1;
    }
    while (1) {
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unterminated attribute specifier");
            return -1;
        }
        if (p->tok.kind == TOK_LBRACK && peek_kind(p) == TOK_LBRACK) {
            nested++;
            if (next_tok(p) != 0) {
                return -1;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            continue;
        }
        if (p->tok.kind == TOK_RBRACK && peek_kind(p) == TOK_RBRACK) {
            if (next_tok(p) != 0) {
                return -1;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
            if (nested == 0) {
                break;
            }
            nested--;
            continue;
        }
        if (next_tok(p) != 0) {
            return -1;
        }
    }
    return 0;
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
    int is_noreturn;
    int is_unused;
    int is_used;
    int has_num = 0;
    long num = 0;
    char *sec = NULL;

    if (p->tok.kind != TOK_IDENT) {
        return 0;
    }
    is_aligned = tok_is_ident(p, "aligned");
    is_section = tok_is_ident(p, "section");
    is_packed = tok_is_ident(p, "packed");
    is_noreturn = tok_is_ident(p, "noreturn");
    is_unused = tok_is_ident(p, "unused");
    is_used = tok_is_ident(p, "used");

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
        if (is_noreturn) {
            out_attrs->flags |= CC_ATTR_NORETURN;
        }
        if (is_unused) {
            out_attrs->flags |= CC_ATTR_UNUSED;
        }
        if (is_used) {
            out_attrs->flags |= CC_ATTR_USED;
        }
        if (is_aligned) {
            long align = (has_num && num > 0) ? num : g_parser_pointer_size_bytes;
            out_attrs->flags |= CC_ATTR_ALIGNED;
            if (align > out_attrs->align) {
                out_attrs->align = align;
            }
        }
        if (is_section && sec != NULL) {
            out_attrs->flags |= CC_ATTR_SECTION;
            free(out_attrs->section);
            out_attrs->section = sec;
            sec = NULL;
        }
    }
    free(sec);
    return 0;
}

static int parse_gnu_attribute_spec(parser_t *p, decl_attrs_t *out_attrs) {
    decl_attrs_t local_attrs;
    if (!tok_is_ident(p, "__attribute__")) {
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
            if (skip_cxx_attribute_seq(p) != 0) {
                return -1;
            }
            continue;
        }
        if (tok_is_ident(p, "__attribute__")) {
            if (parse_gnu_attribute_spec(p, out_attrs) != 0) {
                return -1;
            }
            continue;
        }
        if (!(tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm"))) {
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
    if (parse_declspec(p, &ty, &sid, allow_void, what, NULL, NULL) != 0) {
        return -1;
    }
    while (p->tok.kind == TOK_STAR) {
        ty = ptr_of_type(ty);
        if (ty == CC_TYPE_VOID) {
            set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
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
    *out_type = ty;
    if (out_struct_id != NULL) {
        *out_struct_id = sid;
    }
    return 0;
}

static int parse_param_declarator(parser_t *p, cc_type_t base_type, cc_type_t *out_type, char **out_name) {
    cc_type_t ty = base_type;
    *out_name = NULL;
    while (p->tok.kind == TOK_STAR) {
        ty = ptr_of_type(ty);
        if (ty == CC_TYPE_VOID) {
            set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
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
    if (p->tok.kind == TOK_LPAREN && peek_kind(p) == TOK_STAR) {
        if (next_tok(p) != 0) {
            return -1;
        }
        while (p->tok.kind == TOK_STAR) {
            ty = ptr_of_type(ty);
            if (ty == CC_TYPE_VOID) {
                set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
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
    while (p->tok.kind == TOK_LBRACK) {
        ty = ptr_of_type(ty);
        if (ty == CC_TYPE_VOID) {
            set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
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
    *out_type = ty;
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
    free(s->attr_section);
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
    f->params[f->param_count].type_struct_id = -1;
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
        if (cur_extern && !new_extern) {
            cur->storage &= ~CC_STORAGE_EXTERN;
        } else if (!cur_extern && new_extern) {
            /* Keep existing definition/tentative linkage. */
        } else {
            cur->storage |= (g.storage & (CC_STORAGE_INLINE | CC_STORAGE_AUTO | CC_STORAGE_REGISTER));
        }
        if (cur->attr_flags == 0 && g.attr_flags != 0) {
            cur->attr_flags = g.attr_flags;
        }
        if (cur->attr_align == 0 && g.attr_align > 0) {
            cur->attr_align = g.attr_align;
        }
        if (cur->attr_section == NULL && g.attr_section != NULL) {
            cur->attr_section = g.attr_section;
            g.attr_section = NULL;
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

static cc_expr_t *parse_initializer_item(parser_t *p) {
    if (p->tok.kind == TOK_DOT) {
        cc_expr_t *item;
        cc_expr_t *value;

        if (next_tok(p) != 0) {
            return NULL;
        }
        if (p->tok.kind != TOK_IDENT) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected member name in designated initializer");
            return NULL;
        }
        item = new_expr(CC_EXPR_MEMBER);
        if (item == NULL) {
            return NULL;
        }
        item->ident = xstrdup_n(p->tok.start, p->tok.len);
        if (item->ident == NULL) {
            free_expr(item);
            return NULL;
        }
        if (next_tok(p) != 0) {
            free_expr(item);
            return NULL;
        }
        if (expect(p, TOK_ASSIGN, "expected '=' after designated initializer") != 0) {
            free_expr(item);
            return NULL;
        }
        value = parse_initializer_expr(p);
        if (value == NULL) {
            free_expr(item);
            return NULL;
        }
        item->rhs = value;
        return item;
    }
    return parse_initializer_expr(p);
}

static cc_expr_t *parse_initializer_expr(parser_t *p) {
    cc_expr_t *list = NULL;
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
            long idx = -1;
            size_t fill_i;

            if (next_tok(p) != 0) {
                free_expr(list);
                return NULL;
            }
            idx_expr = parse_expr(p);
            if (idx_expr == NULL) {
                free_expr(list);
                return NULL;
            }
            if (eval_const_array_bound_expr(idx_expr, &idx) != 0 || idx < 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "array designator index must be a non-negative integer constant");
                free_expr(idx_expr);
                free_expr(list);
                return NULL;
            }
            free_expr(idx_expr);
            if (expect(p, TOK_RBRACK, "expected ']' after array designator index") != 0 ||
                expect(p, TOK_ASSIGN, "expected '=' after array designator") != 0) {
                free_expr(list);
                return NULL;
            }
            value = parse_initializer_expr(p);
            if (value == NULL) {
                free_expr(list);
                return NULL;
            }
            for (fill_i = list->arg_count; fill_i < (size_t)idx; ++fill_i) {
                cc_expr_t *z = new_int_expr(0);
                if (z == NULL || push_arg(list, z) != 0) {
                    free_expr(z);
                    free_expr(value);
                    free_expr(list);
                    return NULL;
                }
            }
            if ((size_t)idx < list->arg_count) {
                free_expr(list->args[idx]);
                list->args[idx] = value;
            } else if (push_arg(list, value) != 0) {
                free_expr(value);
                free_expr(list);
                return NULL;
            }
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
    if (!parser_is_c11_or_newer()) {
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
    } else if (!parser_is_c23_or_newer()) {
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
    if (eval_const_array_bound_expr(cond_expr, &cond_val) != 0) {
        free_expr(cond_expr);
        free_expr(msg_expr);
        set_diag(p->diag, p->tok.line, p->tok.col, "static assertion condition must be an integer constant");
        return -1;
    }
    if (cond_val == 0) {
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
    long selected = -1;
    long default_idx = -1;
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
        if (assoc.is_default) {
            default_idx = (long)count;
        }
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

    for (i = 0; i < count; ++i) {
        if (items[i].is_default) {
            continue;
        }
        if (items[i].type == control->value_type && items[i].struct_id == control->struct_id) {
            selected = (long)i;
            break;
        }
    }
    if (selected < 0) {
        selected = default_idx;
    }
    if (selected < 0) {
        selected = 0;
    }

    result = items[selected].expr;
    items[selected].expr = NULL;
    for (i = 0; i < count; ++i) {
        free_expr(items[i].expr);
    }
    free(items);
    free_expr(control);
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
            e->value_type = p->tok.float_is_single ? CC_TYPE_FLOAT : CC_TYPE_DOUBLE;
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

    if (p->tok.kind == TOK_IDENT && tok_is_ident(p, "_Generic")) {
        if (!parser_is_c11_or_newer()) {
            set_diag(p->diag, p->tok.line, p->tok.col, "_Generic requires C11 or newer");
            return NULL;
        }
        return parse_generic_expr(p);
    }

    if (p->tok.kind == TOK_STR) {
        char *lit = xstrdup_n(p->tok.start, p->tok.len);
        e = new_expr(CC_EXPR_STR);
        if (e == NULL || lit == NULL) {
            free(lit);
            return NULL;
        }
        e->ident = lit;
        e->value_type = CC_TYPE_PTR_CHAR;
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        while (p->tok.kind == TOK_STR) {
            char *part = xstrdup_n(p->tok.start, p->tok.len);
            size_t alen;
            size_t plen;
            char *next_lit;
            if (part == NULL) {
                free_expr(e);
                return NULL;
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
        int eidx = enum_const_find_visible_n(p, p->tok.start, p->tok.len);
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

                if (parse_type_name(p, &sty, &ssid, 1, "expected type name in __builtin_offsetof") != 0) {
                    free_expr(e);
                    return NULL;
                }
                if (!(sty == CC_TYPE_VOID && ssid >= 0)) {
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

                    m = struct_member_find_n(p, ssid, p->tok.start, p->tok.len);
                    if (m == NULL) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "unknown member in __builtin_offsetof designator");
                        free_expr(e);
                        return NULL;
                    }
                    total += m->offset;
                    if (next_tok(p) != 0) {
                        free_expr(e);
                        return NULL;
                    }
                    if (p->tok.kind != TOK_DOT) {
                        break;
                    }
                    if (!(m->type == CC_TYPE_VOID && m->type_struct_id >= 0)) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "nested __builtin_offsetof designator requires struct member");
                        free_expr(e);
                        return NULL;
                    }
                    ssid = m->type_struct_id;
                    if (next_tok(p) != 0) {
                        free_expr(e);
                        return NULL;
                    }
                    if (p->tok.kind != TOK_IDENT) {
                        set_diag(p->diag, p->tok.line, p->tok.col,
                                 "expected member name after '.' in __builtin_offsetof");
                        free_expr(e);
                        return NULL;
                    }
                }
                if (expect(p, TOK_RPAREN, "expected ')' after __builtin_offsetof arguments") != 0) {
                    free_expr(e);
                    return NULL;
                }
                free_expr(e);
                e = new_expr(CC_EXPR_INT);
                if (e == NULL) {
                    return NULL;
                }
                e->int_val = total;
                e->value_type = CC_TYPE_LONG_LONG;
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
    dst->int_val = src->int_val;
    dst->float_val = src->float_val;
    dst->op = src->op;
    dst->member_is_arrow = src->member_is_arrow;
    dst->member_offset = src->member_offset;
    dst->update_postfix = src->update_postfix;
    dst->aux_type = src->aux_type;
    dst->aux_struct_id = src->aux_struct_id;

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
            m->ident = xstrdup_n(p->tok.start, p->tok.len);
            if (m->ident == NULL) {
                free_expr(m);
                return NULL;
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
        (tok_is_ident(p, "_Alignof") || (parser_is_c23_or_newer() && tok_is_ident(p, "alignof")))) {
        cc_type_t aty = CC_TYPE_VOID;
        int asid = -1;
        long align;
        cc_expr_t *ae;
        if (!parser_is_c11_or_newer()) {
            set_diag(p->diag, p->tok.line, p->tok.col, "_Alignof requires C11 or newer");
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
        if (p->tok.kind == TOK_LPAREN && peek_kind(p) == TOK_STAR) {
            if (next_tok(p) != 0) {
                free_expr(e);
                return NULL;
            }
            while (p->tok.kind == TOK_STAR) {
                cc_type_t pty = ptr_of_type(e->aux_type);
                if (pty == CC_TYPE_VOID) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
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
    if (!parser_is_c23_or_newer()) {
        set_diag(p->diag, s->line, s->col, "auto type deduction requires C23 or newer");
        return -1;
    }
    if (s->expr == NULL) {
        set_diag(p->diag, s->line, s->col, "auto type deduction requires an initializer");
        return -1;
    }
    s->type = s->expr->value_type;
    s->type_struct_id = s->expr->struct_id;
    s->storage &= ~CC_STORAGE_AUTO_TYPE;
    return 0;
}

static int parse_decl_stmt(parser_t *p, cc_stmt_t *s, int need_semi) {
    int is_typedef = 0;
    int decl_storage = 0;
    int struct_id = -1;
    decl_attrs_t decl_attrs;
    decl_attrs_t suffix_attrs;
    decl_attrs_t merged_attrs;
    memset(s, 0, sizeof(*s));
    s->line = p->tok.line;
    s->col = p->tok.col;
    s->type_struct_id = -1;
    s->kind = CC_STMT_DECL;
    decl_attrs_reset(&decl_attrs);
    decl_attrs_reset(&suffix_attrs);
    decl_attrs_reset(&merged_attrs);
    if (parse_declspec(p, &s->type, &struct_id, 1, "expected declaration type", &is_typedef, &decl_attrs) != 0) {
        return -1;
    }
    decl_storage = p->last_storage;
    s->type_struct_id = struct_id;
    s->storage = decl_storage;
    if (is_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef declaration is not allowed here");
        decl_attrs_clear(&decl_attrs);
        return -1;
    }
    if (parse_named_declarator(p, s->type, &s->type, &s->decl_name, "expected identifier after declaration type") !=
        0) {
        decl_attrs_clear(&decl_attrs);
        return -1;
    }
    if (parse_array_suffix(p, &s->type, &s->array_len) != 0) {
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
    int is_typedef = 0;
    int decl_storage = 0;
    decl_attrs_t base_attrs;

    decl_attrs_reset(&base_attrs);
    if (parse_declspec(p, &base_type, &base_struct_id, 1, "expected declaration type", &is_typedef, &base_attrs) !=
        0) {
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
        int complex_fn_ptr_decl = 0;
        int is_fn_decl = 0;
        int prefixed_fn_ptr = 0;
        decl_attrs_t suffix_attrs;
        decl_attrs_t merged_attrs;
        memset(&s, 0, sizeof(s));
        s.line = p->tok.line;
        s.col = p->tok.col;
        s.kind = CC_STMT_DECL;
        s.type = base_type;
        s.type_struct_id = base_struct_id;
        s.storage = decl_storage;
        decl_attrs_reset(&suffix_attrs);
        decl_attrs_reset(&merged_attrs);

        if (p->tok.kind == TOK_STAR) {
            parser_t q = *p;
            q.diag = NULL;
            while (q.tok.kind == TOK_STAR) {
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
            if (q.tok.kind == TOK_LPAREN && peek_kind(&q) == TOK_STAR) {
                prefixed_fn_ptr = 1;
            }
        }
        if (prefixed_fn_ptr) {
            while (p->tok.kind == TOK_STAR) {
                s.type = ptr_of_type(s.type);
                if (s.type == CC_TYPE_VOID) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
                    free_stmt(&s);
                    return -1;
                }
                if (next_tok(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
                while (is_decl_qual_at_token(p)) {
                    if (next_tok(p) != 0) {
                        free_stmt(&s);
                        return -1;
                    }
                }
            }
        }

        if (p->tok.kind == TOK_LPAREN && peek_kind(p) == TOK_STAR) {
            if (next_tok(p) != 0) {
                free_stmt(&s);
                return -1;
            }
            while (p->tok.kind == TOK_STAR) {
                s.type = ptr_of_type(s.type);
                if (s.type == CC_TYPE_VOID) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "pointer depth > 4 is not yet supported");
                    free_stmt(&s);
                    return -1;
                }
                if (next_tok(p) != 0) {
                    free_stmt(&s);
                    return -1;
                }
                while (is_decl_qual_at_token(p)) {
                    if (next_tok(p) != 0) {
                        free_stmt(&s);
                        return -1;
                    }
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
            complex_fn_ptr_decl = 1;
        } else if (parse_named_declarator(p, base_type, &s.type, &s.decl_name,
                                           "expected identifier after declaration type") != 0) {
            free_stmt(&s);
            return -1;
        }
        if (parse_array_suffix(p, &s.type, &s.array_len) != 0) {
            free_stmt(&s);
            return -1;
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
        decl_attrs_clear(&suffix_attrs);
        decl_attrs_clear(&merged_attrs);
        if (!is_typedef && !complex_fn_ptr_decl && p->tok.kind == TOK_LPAREN) {
            if (skip_balanced_parens(p) != 0) {
                free_stmt(&s);
                return -1;
            }
            is_fn_decl = 1;
            s.type = CC_TYPE_VOID;
            s.type_struct_id = -1;
        }
        if (is_typedef && !complex_fn_ptr_decl && p->tok.kind == TOK_LPAREN) {
            int depth = 1;
            s.type = CC_TYPE_VOID;
            s.type_struct_id = -1;
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
        if (is_typedef) {
            int trc;
            if (s.expr != NULL) {
                set_diag(p->diag, p->tok.line, p->tok.col, "typedef declarator cannot have an initializer");
                free_stmt(&s);
                return -1;
            }
            trc = typedef_push(p, s.decl_name, s.type, s.type_struct_id);
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
            if (arr == NULL || count == NULL) {
                if (s.expr != NULL) {
                    set_diag(p->diag, p->tok.line, p->tok.col,
                             "file-scope initialized object declarations are unsupported");
                    free_stmt(&s);
                    return -1;
                }
                free_stmt(&s);
            } else if (is_fn_decl) {
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
            p->scope_depth = saved_depth;
            return -1;
        }
        if (is_declspec_start(p)) {
            if (parse_decl_stmt_list(p, &s->block_stmts, &s->block_count, 1) != 0) {
                typedef_pop_to_depth(p, saved_depth);
                p->scope_depth = saved_depth;
                return -1;
            }
            continue;
        }
        if (parse_stmt(p, &child) != 0) {
            free_stmt(&child);
            typedef_pop_to_depth(p, saved_depth);
            p->scope_depth = saved_depth;
            return -1;
        }
        if (push_stmt_arr(&s->block_stmts, &s->block_count, child) != 0) {
            free_stmt(&child);
            typedef_pop_to_depth(p, saved_depth);
            p->scope_depth = saved_depth;
            return -1;
        }
    }
    if (expect(p, TOK_RBRACE, "expected '}' after block") != 0) {
        typedef_pop_to_depth(p, saved_depth);
        p->scope_depth = saved_depth;
        return -1;
    }
    typedef_pop_to_depth(p, saved_depth);
    p->scope_depth = saved_depth;
    return 0;
}

static int parse_stmt(parser_t *p, cc_stmt_t *s) {
    memset(s, 0, sizeof(*s));
    s->line = p->tok.line;
    s->col = p->tok.col;

    while (p->tok.kind == TOK_KW_EXTENSION) {
        if (next_tok(p) != 0) {
            return -1;
        }
    }

    if (is_static_assert_tok(p)) {
        s->kind = CC_STMT_EXPR;
        s->expr = NULL;
        return parse_static_assert_decl(p, 1);
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

    if (tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm")) {
        s->kind = CC_STMT_EXPR;
        if (next_tok(p) != 0) {
            return -1;
        }
        while (p->tok.kind != TOK_LPAREN) {
            if (p->tok.kind == TOK_SEMI || p->tok.kind == TOK_EOF) {
                set_diag(p->diag, p->tok.line, p->tok.col, "expected '(' after __asm__");
                return -1;
            }
            if (next_tok(p) != 0) {
                return -1;
            }
        }
        if (skip_balanced_parens(p) != 0) {
            return -1;
        }
        return expect(p, TOK_SEMI, "expected ';' after __asm__ statement");
    }

    if (is_declspec_start(p)) {
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
    free(f->attr_section);
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
                if (push_param(f, CC_TYPE_INT, p->tok.start, p->tok.len) != 0) {
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
        char anon_buf[32];

        if (p->tok.kind == TOK_ELLIPSIS) {
            f->is_variadic = 1;
            f->has_prototype = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            break;
        }

        if (parse_declspec(p, &ptype, &ptype_sid, 1, "expected parameter type", NULL, NULL) != 0) {
            return -1;
        }
        if (parse_param_declarator(p, ptype, &dty, &pname) != 0) {
            return -1;
        }
        if (skip_decl_gnu_suffix(p, NULL) != 0) {
            free(pname);
            return -1;
        }
        if (pname == NULL) {
            snprintf(anon_buf, sizeof(anon_buf), "__anon_param_%zu", f->param_count);
            if (push_param(f, dty, anon_buf, strlen(anon_buf)) != 0) {
                return -1;
            }
            if (f->param_count > 0) {
                f->params[f->param_count - 1].type_struct_id = ptype_sid;
            }
        } else {
            if (push_param(f, dty, pname, strlen(pname)) != 0) {
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

static int parse_function(parser_t *p, cc_function_t *f) {
    cc_type_t ftype;
    int ftype_sid = -1;
    int is_typedef = 0;
    int fn_storage = 0;
    int saved_depth = p->scope_depth;
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

    if (parse_declspec(p, &ftype, &ftype_sid, 1, "expected function return type", &is_typedef, &spec_attrs) != 0) {
        return -1;
    }
    fn_storage = p->last_storage;
    if (is_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef is not valid in function definition");
        decl_attrs_clear(&spec_attrs);
        return -1;
    }
    if (parse_named_declarator(p, ftype, &f->ret_type, &f->name, "expected function name") != 0) {
        decl_attrs_clear(&spec_attrs);
        return -1;
    }
    f->ret_struct_id = ftype_sid;
    f->storage = fn_storage;
    if (f->storage & CC_STORAGE_INLINE) {
        /* Avoid duplicate external inline bodies across translation units. */
        f->storage |= CC_STORAGE_STATIC;
    }

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
    if (g_parser_allow_oldstyle_funcdecl && p->tok.kind != TOK_LBRACE && p->tok.kind != TOK_EOF) {
        while (p->tok.kind != TOK_LBRACE) {
            if (p->tok.kind == TOK_EOF) {
                set_diag(p->diag, p->tok.line, p->tok.col, "unexpected end of file in old-style function declaration");
                return -1;
            }
            if (!is_declspec_start(p)) {
                set_diag(p->diag, p->tok.line, p->tok.col, "expected declaration in old-style function definition");
                return -1;
            }
            if (parse_decl_stmt_list(p, NULL, NULL, 1) != 0) {
                return -1;
            }
        }
    }
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

    while (p->tok.kind != TOK_RBRACE) {
        cc_stmt_t s;
        if (p->tok.kind == TOK_EOF) {
            set_diag(p->diag, p->tok.line, p->tok.col, "unexpected end of file in function body");
            return -1;
        }
        if (is_declspec_start(p)) {
            if (parse_decl_stmt_list(p, &f->stmts, &f->stmt_count, 1) != 0) {
                return -1;
            }
            continue;
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
    typedef_pop_to_depth(p, saved_depth);
    p->scope_depth = saved_depth;
    return 0;
}

static int probe_is_function_head(parser_t *p) {
    parser_t q = *p;
    cc_type_t ty;
    int ty_sid = -1;
    int is_typedef = 0;
    char *name = NULL;
    int rc = 0;

    q.diag = NULL;
    q.structs = NULL;
    q.struct_count = 0;
    q.struct_cap = 0;
    q.enum_consts = NULL;
    q.enum_const_count = 0;
    q.enum_const_cap = 0;
    if (parse_declspec(&q, &ty, &ty_sid, 1, "", &is_typedef, NULL) != 0) {
        parser_free_enum_consts(&q);
        parser_free_structs(&q);
        return 0;
    }
    if (is_typedef) {
        parser_free_enum_consts(&q);
        parser_free_structs(&q);
        return 0;
    }
    if (parse_named_declarator(&q, ty, &ty, &name, "") != 0) {
        parser_free_enum_consts(&q);
        parser_free_structs(&q);
        return 0;
    }
    rc = (q.tok.kind == TOK_LPAREN);
    free(name);
    parser_free_enum_consts(&q);
    parser_free_structs(&q);
    return rc;
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
    if (g_parser_enable_trigraphs) {
        sz = (long)normalize_c95_trigraphs(buf, (size_t)sz);
        buf[sz] = '\0';
    }

    memset(&p, 0, sizeof(p));
    p.diag = diag;
    p.scope_depth = 0;
    cc_lexer_init(&p.lx, buf, (size_t)sz);
    if (next_tok(&p) != 0) {
        parser_free_typedefs(&p);
        parser_free_enum_consts(&p);
        parser_free_structs(&p);
        free(buf);
        cc_tu_free(out);
        return -1;
    }
    if (parser_is_c23_or_newer() && typedef_push(&p, "nullptr_t", CC_TYPE_PTR_VOID, -1) != 0) {
        parser_free_typedefs(&p);
        parser_free_enum_consts(&p);
        parser_free_structs(&p);
        free(buf);
        cc_tu_free(out);
        return -1;
    }
    while (p.tok.kind != TOK_EOF) {
        if (is_static_assert_tok(&p)) {
            if (parse_static_assert_decl(&p, 1) != 0) {
                parser_free_typedefs(&p);
                parser_free_enum_consts(&p);
                parser_free_structs(&p);
                free(buf);
                cc_tu_free(out);
                return -1;
            }
            continue;
        }
        if (is_declspec_start(&p) && !probe_is_function_head(&p)) {
            cc_stmt_t *decls = NULL;
            size_t decl_count = 0;
            size_t di;
            if (parse_decl_stmt_list(&p, &decls, &decl_count, 1) != 0) {
                parser_free_typedefs(&p);
                parser_free_enum_consts(&p);
                parser_free_structs(&p);
                free(buf);
                cc_tu_free(out);
                return -1;
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
                    parser_free_typedefs(&p);
                    parser_free_enum_consts(&p);
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
                g.storage = decls[di].storage;
                g.attr_flags = decls[di].attr_flags;
                g.attr_align = decls[di].attr_align;
                g.attr_section = decls[di].attr_section;
                decls[di].attr_section = NULL;
                g.init = decls[di].expr;
                decls[di].expr = NULL;
                if (push_global(out, g) != 0) {
                    while (di < decl_count) {
                        free_stmt(&decls[di]);
                        di++;
                    }
                    free(decls);
                    parser_free_typedefs(&p);
                    parser_free_enum_consts(&p);
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
        cc_function_t *next = (cc_function_t *)realloc(out->funcs, (out->func_count + 1) * sizeof(*next));
        if (next == NULL) {
            parser_free_typedefs(&p);
            parser_free_enum_consts(&p);
            parser_free_structs(&p);
            free(buf);
            cc_tu_free(out);
            set_diag(diag, 0, 0, "out of memory");
            return -1;
        }
        out->funcs = next;

        if (parse_function(&p, &f) != 0) {
            parser_free_typedefs(&p);
            parser_free_enum_consts(&p);
            parser_free_structs(&p);
            free(buf);
            cc_tu_free(out);
            return -1;
        }
        out->funcs[out->func_count++] = f;
    }

    parser_free_typedefs(&p);
    parser_free_enum_consts(&p);
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

    if (std_mode == NULL || std_mode[0] == '\0') {
        g_parser_enable_trigraphs = 1;
        g_parser_allow_oldstyle_funcdecl = 0;
        return;
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
    if (strcmp(std_mode, "c89") == 0 || strcmp(std_mode, "c90") == 0 || strcmp(std_mode, "c95") == 0 ||
        strcmp(std_mode, "gnu89") == 0 || strcmp(std_mode, "gnu90") == 0 || strcmp(std_mode, "gnu95") == 0) {
        g_parser_allow_oldstyle_funcdecl = 1;
    } else {
        g_parser_allow_oldstyle_funcdecl = 0;
    }
}
