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

typedef struct {
    char *name;
    cc_type_t type;
    int depth;
} typedef_entry_t;

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
    int scope_depth;
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

static int typedef_find_visible(const parser_t *p, const char *name) {
    return typedef_find_visible_n(p, name, strlen(name));
}

static int typedef_push(parser_t *p, const char *name, cc_type_t type) {
    typedef_entry_t *next;
    char *dup;

    if (typedef_find_visible(p, name) >= 0) {
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

static int is_declspec_tok(cc_tok_kind_t k);

static int is_declspec_start(parser_t *p) {
    return is_declspec_tok(p->tok.kind) ||
           (p->tok.kind == TOK_IDENT && typedef_find_visible_n(p, p->tok.start, p->tok.len) >= 0);
}

static int is_type_name_start_after_lparen(parser_t *p) {
    cc_token_t t;
    if (peek_tok(p, &t) != 0) {
        return 0;
    }
    return is_declspec_tok(t.kind) || (t.kind == TOK_IDENT && typedef_find_visible_n(p, t.start, t.len) >= 0);
}

static int is_declspec_tok(cc_tok_kind_t k) {
    switch (k) {
    case TOK_KW_AUTO:
    case TOK_KW_BOOL:
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
    case TOK_KW_VOLATILE:
    case TOK_KW_VOID:
        return 1;
    default:
        return 0;
    }
}

static int parse_declspec(parser_t *p, cc_type_t *out_type, int allow_void, const char *what, int *out_typedef) {
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
    int seen_opaque_tag = 0;
    int seen_typedef = 0;
    int seen_alias = 0;
    cc_type_t alias_type = CC_TYPE_VOID;

    if (out_typedef != NULL) {
        *out_typedef = 0;
    }

    while (1) {
        if (p->tok.kind == TOK_IDENT) {
            int tidx = typedef_find_visible_n(p, p->tok.start, p->tok.len);
            if (tidx >= 0) {
                seen = 1;
                seen_type = 1;
                seen_alias = 1;
                alias_type = p->typedefs[tidx].type;
                if (next_tok(p) != 0) {
                    return -1;
                }
                continue;
            }
            if (seen_typedef && !seen_type) {
                seen = 1;
                seen_type = 1;
                seen_alias = 1;
                alias_type = CC_TYPE_VOID;
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
        case TOK_KW_STRUCT:
        case TOK_KW_UNION:
        case TOK_KW_ENUM: {
            int brace_depth = 0;
            seen_type = 1;
            seen_opaque_tag = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            if (p->tok.kind == TOK_IDENT) {
                if (next_tok(p) != 0) {
                    return -1;
                }
            }
            if (p->tok.kind != TOK_LBRACE) {
                continue;
            }
            brace_depth = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            while (brace_depth > 0) {
                if (p->tok.kind == TOK_EOF) {
                    set_diag(p->diag, p->tok.line, p->tok.col, "unterminated aggregate declaration");
                    return -1;
                }
                if (p->tok.kind == TOK_LBRACE) {
                    brace_depth++;
                } else if (p->tok.kind == TOK_RBRACE) {
                    brace_depth--;
                }
                if (next_tok(p) != 0) {
                    return -1;
                }
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
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }

    if (seen_void) {
        if (!allow_void || seen_bool || seen_char || seen_int || seen_float || seen_double || seen_long || seen_short) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid use of void in declaration specifiers");
            return -1;
        }
        *out_type = CC_TYPE_VOID;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }

    if (seen_signed && seen_unsigned) {
        set_diag(p->diag, p->tok.line, p->tok.col, "conflicting signed/unsigned in declaration specifiers");
        return -1;
    }
    if (seen_short && seen_long > 0) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid short/long combination in declaration specifiers");
        return -1;
    }

    if ((seen_signed || seen_unsigned) && (seen_float || seen_double || seen_bool || seen_void)) {
        set_diag(p->diag, p->tok.line, p->tok.col, "invalid signed/unsigned type combination");
        return -1;
    }
    if (seen_opaque_tag) {
        if (seen_void || seen_bool || seen_char || seen_int || seen_float || seen_double || seen_long || seen_short ||
            seen_signed || seen_unsigned) {
            set_diag(p->diag, p->tok.line, p->tok.col, "invalid aggregate declaration specifiers");
            return -1;
        }
        *out_type = CC_TYPE_VOID;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }

    if (seen_double) {
        *out_type = CC_TYPE_DOUBLE;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }
    if (seen_float) {
        *out_type = CC_TYPE_FLOAT;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }
    if (seen_bool) {
        *out_type = CC_TYPE_BOOL;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }
    if (seen_char) {
        *out_type = seen_unsigned ? CC_TYPE_UCHAR : CC_TYPE_CHAR;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }
    if (seen_long > 0) {
        *out_type = seen_unsigned ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }
    if (seen_short) {
        *out_type = seen_unsigned ? CC_TYPE_USHORT : CC_TYPE_SHORT;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }
    if (seen_int) {
        *out_type = seen_unsigned ? CC_TYPE_UINT : CC_TYPE_INT;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }

    /* e.g. signed/unsigned without explicit base type => int */
    if (seen_signed || seen_unsigned) {
        *out_type = seen_unsigned ? CC_TYPE_UINT : CC_TYPE_INT;
        if (out_typedef != NULL) {
            *out_typedef = seen_typedef;
        } else if (seen_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
            return -1;
        }
        return 0;
    }

    *out_type = CC_TYPE_INT;
    if (out_typedef != NULL) {
        *out_typedef = seen_typedef;
    } else if (seen_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef not allowed here");
        return -1;
    }
    return 0;
}

static int is_decl_qual_tok(cc_tok_kind_t k) {
    return k == TOK_KW_CONST || k == TOK_KW_VOLATILE || k == TOK_KW_RESTRICT;
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
        while (is_decl_qual_tok(p->tok.kind)) {
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

static int tok_is_ident(parser_t *p, const char *s) {
    size_t n = strlen(s);
    return p->tok.kind == TOK_IDENT && p->tok.len == n && strncmp(p->tok.start, s, n) == 0;
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

static int skip_decl_gnu_suffix(parser_t *p) {
    while (tok_is_ident(p, "__attribute__") || tok_is_ident(p, "__asm__") || tok_is_ident(p, "__asm")) {
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

static int parse_type_name(parser_t *p, cc_type_t *out_type, int allow_void, const char *what) {
    cc_type_t ty;
    if (parse_declspec(p, &ty, allow_void, what, NULL) != 0) {
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
        while (is_decl_qual_tok(p->tok.kind)) {
            if (next_tok(p) != 0) {
                return -1;
            }
        }
    }
    *out_type = ty;
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
        while (is_decl_qual_tok(p->tok.kind)) {
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
            while (is_decl_qual_tok(p->tok.kind)) {
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
    dst->int_val = src->int_val;
    dst->float_val = src->float_val;
    dst->op = src->op;
    dst->update_postfix = src->update_postfix;
    dst->aux_type = src->aux_type;

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

        if (p->tok.kind == TOK_PLUS_PLUS || p->tok.kind == TOK_MINUS_MINUS) {
            cc_expr_t *upd;
            if (e->kind == CC_EXPR_IDENT && e->ident != NULL) {
                upd = new_update_ident_expr(e->ident, p->tok.kind == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 1);
                free_expr(e);
            } else if (e->kind == CC_EXPR_DEREF && e->lhs != NULL) {
                upd = new_update_lvalue_expr(e, p->tok.kind == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 1);
                e = NULL;
            } else {
                set_diag(p->diag, p->tok.line, p->tok.col, "++/-- requires an identifier or dereference lvalue");
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
            if (parse_type_name(p, &e->aux_type, 1, "expected type name in sizeof") != 0) {
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

    if (p->tok.kind == TOK_LPAREN && is_type_name_start_after_lparen(p)) {
        cc_expr_t *e = new_expr(CC_EXPR_CAST);
        if (e == NULL) {
            return NULL;
        }
        if (next_tok(p) != 0) {
            free_expr(e);
            return NULL;
        }
        if (parse_type_name(p, &e->aux_type, 1, "expected cast type") != 0) {
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
        if (rhs->kind == CC_EXPR_DEREF && rhs->lhs != NULL) {
            return new_update_lvalue_expr(rhs, op == TOK_PLUS_PLUS ? CC_BIN_ADD : CC_BIN_SUB, 0);
        }
        set_diag(p->diag, p->tok.line, p->tok.col, "++/-- requires an identifier or dereference lvalue");
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

        if (lhs->kind == CC_EXPR_IDENT && lhs->ident != NULL) {
            lhs_is_ident = 1;
        } else if (lhs->kind == CC_EXPR_DEREF) {
            lhs_is_deref = 1;
        } else {
            set_diag(p->diag, p->tok.line, p->tok.col,
                     "left-hand side of assignment must be an identifier or dereference");
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
        if (lhs_is_deref) {
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

static int parse_decl_stmt(parser_t *p, cc_stmt_t *s, int need_semi) {
    int is_typedef = 0;
    memset(s, 0, sizeof(*s));
    s->kind = CC_STMT_DECL;
    if (parse_declspec(p, &s->type, 1, "expected declaration type", &is_typedef) != 0) {
        return -1;
    }
    if (is_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef declaration is not allowed here");
        return -1;
    }
    if (parse_named_declarator(p, s->type, &s->type, &s->decl_name, "expected identifier after declaration type") !=
        0) {
        return -1;
    }
    if (p->tok.kind == TOK_ASSIGN) {
        if (next_tok(p) != 0) {
            return -1;
        }
        s->expr = parse_assign(p);
        if (s->expr == NULL) {
            return -1;
        }
    }
    if (need_semi) {
        return expect(p, TOK_SEMI, "expected ';' after declaration");
    }
    return 0;
}

static int parse_decl_stmt_list(parser_t *p, cc_stmt_t **arr, size_t *count, int need_semi) {
    cc_type_t base_type;
    int is_typedef = 0;

    if (parse_declspec(p, &base_type, 1, "expected declaration type", &is_typedef) != 0) {
        return -1;
    }
    if (p->tok.kind == TOK_SEMI) {
        if (is_typedef) {
            set_diag(p->diag, p->tok.line, p->tok.col, "expected identifier after declaration type");
            return -1;
        }
        if (need_semi) {
            return expect(p, TOK_SEMI, "expected ';' after declaration");
        }
        return 0;
    }

    for (;;) {
        cc_stmt_t s;
        int complex_fn_ptr_decl = 0;
        memset(&s, 0, sizeof(s));
        s.kind = CC_STMT_DECL;
        s.type = base_type;

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
                while (is_decl_qual_tok(p->tok.kind)) {
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
        if (skip_decl_gnu_suffix(p) != 0) {
            free_stmt(&s);
            return -1;
        }
        if (is_typedef && !complex_fn_ptr_decl && p->tok.kind == TOK_LPAREN) {
            int depth = 1;
            s.type = CC_TYPE_VOID;
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
            s.expr = parse_assign(p);
            if (s.expr == NULL) {
                free_stmt(&s);
                return -1;
            }
        }
        if (is_typedef) {
            int trc;
            if (s.expr != NULL) {
                set_diag(p->diag, p->tok.line, p->tok.col, "typedef declarator cannot have an initializer");
                free_stmt(&s);
                return -1;
            }
            trc = typedef_push(p, s.decl_name, s.type);
            if (trc > 0) {
                set_diag(p->diag, p->tok.line, p->tok.col, "duplicate typedef name in this scope");
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
            return -1;
        }
    }

    if (need_semi) {
        return expect(p, TOK_SEMI, "expected ';' after declaration");
    }
    return 0;
}

static int parse_block_stmt(parser_t *p, cc_stmt_t *s) {
    int saved_depth = p->scope_depth;
    memset(s, 0, sizeof(*s));
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
    if (p->tok.kind == TOK_KW_VOID && peek_kind(p) == TOK_RPAREN) {
        if (next_tok(p) != 0) {
            return -1;
        }
        return 0;
    }

    while (p->tok.kind != TOK_RPAREN) {
        cc_type_t ptype;
        cc_type_t dty;
        char *pname = NULL;
        char anon_buf[32];

        if (p->tok.kind == TOK_ELLIPSIS) {
            f->is_variadic = 1;
            if (next_tok(p) != 0) {
                return -1;
            }
            break;
        }

        if (parse_declspec(p, &ptype, 1, "expected parameter type", NULL) != 0) {
            return -1;
        }
        if (parse_param_declarator(p, ptype, &dty, &pname) != 0) {
            return -1;
        }
        if (pname == NULL) {
            snprintf(anon_buf, sizeof(anon_buf), "__anon_param_%zu", f->param_count);
            if (push_param(f, dty, anon_buf, strlen(anon_buf)) != 0) {
                return -1;
            }
        } else {
            if (push_param(f, dty, pname, strlen(pname)) != 0) {
                free(pname);
                return -1;
            }
            free(pname);
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
    cc_type_t ftype;
    int is_typedef = 0;
    int saved_depth = p->scope_depth;
    memset(f, 0, sizeof(*f));
    f->has_body = 0;

    if (parse_declspec(p, &ftype, 1, "expected function return type", &is_typedef) != 0) {
        return -1;
    }
    if (is_typedef) {
        set_diag(p->diag, p->tok.line, p->tok.col, "typedef is not valid in function definition");
        return -1;
    }
    if (parse_named_declarator(p, ftype, &f->ret_type, &f->name, "expected function name") != 0) {
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
            if (f->params[i].type == CC_TYPE_VOID) {
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
    int is_typedef = 0;
    char *name = NULL;
    int rc = 0;

    q.diag = NULL;
    if (parse_declspec(&q, &ty, 1, "", &is_typedef) != 0) {
        return 0;
    }
    if (is_typedef) {
        return 0;
    }
    if (parse_named_declarator(&q, ty, &ty, &name, "") != 0) {
        return 0;
    }
    rc = (q.tok.kind == TOK_LPAREN);
    free(name);
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
    sz = (long)normalize_c95_trigraphs(buf, (size_t)sz);
    buf[sz] = '\0';

    memset(&p, 0, sizeof(p));
    p.diag = diag;
    p.scope_depth = 0;
    cc_lexer_init(&p.lx, buf, (size_t)sz);
    if (next_tok(&p) != 0) {
        parser_free_typedefs(&p);
        free(buf);
        cc_tu_free(out);
        return -1;
    }

    while (p.tok.kind != TOK_EOF) {
        if (is_declspec_start(&p) && !probe_is_function_head(&p)) {
            if (parse_decl_stmt_list(&p, NULL, NULL, 1) != 0) {
                parser_free_typedefs(&p);
                free(buf);
                cc_tu_free(out);
                return -1;
            }
            continue;
        }
        cc_function_t f;
        cc_function_t *next = (cc_function_t *)realloc(out->funcs, (out->func_count + 1) * sizeof(*next));
        if (next == NULL) {
            parser_free_typedefs(&p);
            free(buf);
            cc_tu_free(out);
            set_diag(diag, 0, 0, "out of memory");
            return -1;
        }
        out->funcs = next;

        if (parse_function(&p, &f) != 0) {
            parser_free_typedefs(&p);
            free(buf);
            cc_tu_free(out);
            return -1;
        }
        out->funcs[out->func_count++] = f;
    }

    parser_free_typedefs(&p);
    free(buf);
    return 0;
}
