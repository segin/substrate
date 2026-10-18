/*
 * parse_sema.c: the semantics the parser needs while it parses -- the
 * variables in scope, the types of expressions (for `typeof` and for the
 * constant expressions below), and the evaluation of integer constant
 * expressions in the parser's context (array bounds, enumerators, case
 * labels, static assertions).  The parser proper (parser.c) is syntax.
 */

#include "cc_frontend.h"
#include "cc_consteval.h"
#include "cc_ctype.h"
#include "cc_decimal.h"
#include "cc_diag.h"
#include "cc_lang.h"
#include "cc_lexer.h"
#include "cc_options.h"
#include "cc_types.h"
#include "cc_util.h"
#include "cc_parser.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const cc_function_t *parser_find_function_decl(parser_t *p, const char *name);
static const cc_global_t *parser_find_global_decl(parser_t *p, const char *name);
static const cc_ctype_t *parser_ident_ctype(parser_t *p, const char *name);
static const cc_ctype_t *parser_usual_arith(const cc_ctype_t *a, const cc_ctype_t *b);
static const cc_ctype_t *parser_bin_ctype(parser_t *p, const cc_expr_t *e);
static const cc_ctype_t *parser_complete_operand_array(parser_t *p, const cc_expr_t *e, const cc_ctype_t *t);
static long parser_const_sizeof(void *ctx, const cc_expr_t *e);
static cc_const_env_t parser_const_env(parser_t *p);

/**
 * The declaration of function @p name seen so far that says most about
 * it: one with a prototype before one without, a definition before a
 * declaration.
 *
 * @return the declaration, or NULL if none has been seen
 */
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

/** The first file-scope object named @p name seen so far, or NULL. */
static const cc_global_t *parser_find_global_decl(parser_t *p, const char *name) {
    size_t i;

    if (p == NULL || p->tu == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < p->tu->global_count; ++i) {
        const cc_global_t *g = &p->tu->globals[i];
        if (g->name != NULL && strcmp(g->name, name) == 0) {
            return g;
        }
    }
    return NULL;
}

int var_find_visible_n(const parser_t *p, const char *name, size_t len) {
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

/**
 * Declare a variable in the current scope, or redeclare one already
 * declared in it.
 *
 * @param p      the parser
 * @param name   its name; an empty or NULL name declares nothing
 * @param ctype  its type
 * @return       0, or -1 when out of memory
 */
int var_push(parser_t *p, const char *name, const cc_ctype_t *ctype) {
    var_entry_t *next;
    char *dup;
    int i;
    size_t len;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    len = strlen(name);
    for (i = (int)p->var_count - 1; i >= 0; --i) {
        if (p->vars[i].depth < p->scope_depth) {
            break;
        }
        if (p->vars[i].depth == p->scope_depth && strlen(p->vars[i].name) == len &&
            strncmp(p->vars[i].name, name, len) == 0) {
            p->vars[i].ctype = ctype;
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
    dup = cc_strndup(name, len);
    if (dup == NULL) {
        return -1;
    }
    p->vars[p->var_count].name = dup;
    p->vars[p->var_count].depth = p->scope_depth;
    p->vars[p->var_count].ctype = ctype;
    p->var_count++;
    return 0;
}

void var_pop_to_depth(parser_t *p, int depth) {
    while (p->var_count > 0 && p->vars[p->var_count - 1].depth > depth) {
        free(p->vars[p->var_count - 1].name);
        p->var_count--;
    }
}

void parser_free_vars(parser_t *p) {
    size_t i;
    for (i = 0; i < p->var_count; ++i) {
        free(p->vars[i].name);
    }
    free(p->vars);
    p->vars = NULL;
    p->var_count = 0;
    p->var_cap = 0;
}

/** The type of a named object or function, as the parser knows it, or NULL. */
static const cc_ctype_t *parser_ident_ctype(parser_t *p, const char *name) {
    int vidx;
    const cc_global_t *g;
    const cc_function_t *f;

    if (name == NULL) {
        return NULL;
    }
    vidx = var_find_visible_n(p, name, strlen(name));
    if (vidx >= 0) {
        return p->vars[vidx].ctype;
    }
    g = parser_find_global_decl(p, name);
    if (g != NULL) {
        return g->ctype;
    }
    f = parser_find_function_decl(p, name);
    return f != NULL ? parser_function_ctype(f) : NULL;
}

/** The type the usual arithmetic conversions give operands of types @p a and @p b, or NULL. */
static const cc_ctype_t *parser_usual_arith(const cc_ctype_t *a, const cc_ctype_t *b) {
    static const cc_ctype_kind_t floats[] = {CC_CT_LDOUBLE, CC_CT_DOUBLE, CC_CT_FLOAT, CC_CT_FLOAT16};
    size_t i;

    if (!cc_ct_is_arithmetic(a) || !cc_ct_is_arithmetic(b)) {
        return NULL;
    }
    for (i = 0; i < sizeof(floats) / sizeof(floats[0]); ++i) {
        if (a->unqual->kind == floats[i] || b->unqual->kind == floats[i]) {
            return cc_ct_basic(floats[i]);
        }
    }
    return cc_ct_common_int(a->unqual, b->unqual);
}

/** The type of binary operator @p e as the parser can tell it (parser_expr_ctype()), or NULL. */
static const cc_ctype_t *parser_bin_ctype(parser_t *p, const cc_expr_t *e) {
    const cc_ctype_t *a = cc_ct_decay(parser_expr_ctype(p, e->lhs));
    const cc_ctype_t *b = cc_ct_decay(parser_expr_ctype(p, e->rhs));
    int is_cmp = e->op == CC_BIN_EQ || e->op == CC_BIN_NE || e->op == CC_BIN_LT || e->op == CC_BIN_LE ||
                 e->op == CC_BIN_GT || e->op == CC_BIN_GE;

    if (e->op == CC_BIN_COMMA) {
        return parser_expr_ctype(p, e->rhs);
    }
    if (a == NULL || b == NULL) {
        return NULL;
    }
    if (e->op == CC_BIN_LAND || e->op == CC_BIN_LOR || (is_cmp && !(a->kind == CC_CT_RECORD &&
                                                                    p->structs[a->id].vector_count > 0) &&
                                                       !(b->kind == CC_CT_RECORD &&
                                                         p->structs[b->id].vector_count > 0))) {
        return cc_ct_basic(CC_CT_INT);
    }
    if (!e->is_subscript && ((a->kind == CC_CT_RECORD && p->structs[a->id].vector_count > 0) ||
                             (b->kind == CC_CT_RECORD && p->structs[b->id].vector_count > 0))) {
        /* an operator with a vector operand: that vector type, or a comparison's */
        int vs = (a->kind == CC_CT_RECORD && p->structs[a->id].vector_count > 0) ? a->id : b->id;

        return cc_ct_record(is_cmp ? p->structs[vs].vector_cmp_sid : vs);
    }
    if ((e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) && a->kind == CC_CT_POINTER && b->kind != CC_CT_POINTER) {
        return a;
    }
    if (e->op == CC_BIN_ADD && a->kind != CC_CT_POINTER && b->kind == CC_CT_POINTER) {
        return b;
    }
    if (e->op == CC_BIN_SUB && a->kind == CC_CT_POINTER && b->kind == CC_CT_POINTER) {
        /* ptrdiff_t */
        return cc_ct_basic(cc_target_pointer_size() >= 8 ? CC_CT_LONG : CC_CT_INT);
    }
    if (e->op == CC_BIN_SHL || e->op == CC_BIN_SHR || (e->unary_form == CC_UNARY_LHS && cc_ct_is_integer(a))) {
        return cc_ct_promote(a);
    }
    if (e->unary_form == CC_UNARY_RHS && cc_ct_is_integer(b)) {
        return cc_ct_promote(b);
    }
    return parser_usual_arith(a, b);
}

/**
 * The type of expression @p e as far as the parser can tell before sema
 * checks it -- what `typeof (e)` names: a named object's or function's
 * declared type, a literal's, a cast's, and the types C gives the
 * operators applied to them.
 *
 * @param p  the parser
 * @param e  the expression
 * @return   the type, or NULL where the parser cannot tell it
 */
const cc_ctype_t *parser_expr_ctype(parser_t *p, const cc_expr_t *e) {
    const cc_ctype_t *a;
    const cc_ctype_t *b;

    if (e == NULL) {
        return NULL;
    }
    switch (e->kind) {
    case CC_EXPR_IDENT:
        return parser_ident_ctype(p, e->ident);
    case CC_EXPR_INT:
    case CC_EXPR_FLOAT:
        return e->ctype;
    case CC_EXPR_STR: {
        long n = parser_string_literal_array_bound(e);
        const cc_ctype_t *elem = cc_string_literal_elem(e);

        return n > 0 ? cc_ct_array(elem, n) : cc_ct_pointer(elem);
    }
    case CC_EXPR_CAST:
        return e->ctype;
    case CC_EXPR_ADDR:
        a = parser_expr_ctype(p, e->lhs);
        return a != NULL ? cc_ct_pointer(a) : NULL;
    case CC_EXPR_DEREF:
        a = parser_expr_ctype(p, e->lhs);
        if (a != NULL && a->unqual->kind == CC_CT_FUNCTION) {
            return a;
        }
        a = cc_ct_decay(a);
        return a != NULL && a->kind == CC_CT_POINTER ? a->of : NULL;
    case CC_EXPR_MEMBER: {
        const cc_struct_member_t *sm;

        a = cc_ct_decay(parser_expr_ctype(p, e->lhs));
        if (a != NULL && e->member_is_arrow) {
            a = a->kind == CC_CT_POINTER ? a->of->unqual : NULL;
        }
        if (a == NULL || a->kind != CC_CT_RECORD || e->ident == NULL) {
            return NULL;
        }
        sm = struct_member_find_n(p, a->id, e->ident, strlen(e->ident));
        return sm != NULL ? sm->ctype : NULL;
    }
    case CC_EXPR_BIN:
        return parser_bin_ctype(p, e);
    case CC_EXPR_CALL:
        if (e->ident != NULL) {
            const cc_function_t *f = parser_find_function_decl(p, e->ident);

            if (f != NULL) {
                return f->ret_ctype;
            }
        }
        a = parser_expr_ctype(p, e->ident != NULL ? NULL : e->lhs);
        if (a == NULL && e->ident != NULL) {
            a = parser_ident_ctype(p, e->ident);
        }
        if (a != NULL && a->unqual->kind == CC_CT_POINTER) {
            a = a->unqual->of;
            /* a pointer to a function typedef is recorded as a pointer to its return type */
            return a->unqual->kind == CC_CT_FUNCTION ? a->unqual->of : a;
        }
        return a != NULL && a->unqual->kind == CC_CT_FUNCTION ? a->unqual->of : NULL;
    case CC_EXPR_GENERIC: {
        long selected = -1;
        size_t i;

        a = cc_ct_decay(parser_expr_ctype(p, e->lhs));
        for (i = 0; i < e->generic_count; ++i) {
            if (e->generic_is_default[i]) {
                if (selected < 0) {
                    selected = (long)i;
                }
            } else if (a != NULL && e->generic_ctypes[i] != NULL && e->generic_ctypes[i]->unqual == a) {
                selected = (long)i;
                break;
            }
        }
        return selected >= 0 && (size_t)selected < e->arg_count ? parser_expr_ctype(p, e->args[selected]) : NULL;
    }
    case CC_EXPR_ASSIGN:
    case CC_EXPR_UPDATE:
        a = e->ident != NULL ? parser_ident_ctype(p, e->ident) : parser_expr_ctype(p, e->lhs);
        return a != NULL ? a->unqual : NULL;
    case CC_EXPR_TERNARY:
        a = cc_ct_decay(parser_expr_ctype(p, e->rhs != NULL ? e->rhs : e->lhs));
        b = cc_ct_decay(parser_expr_ctype(p, e->third));
        if (a == NULL || b == NULL || a == b || (a->kind == CC_CT_POINTER && b->kind == CC_CT_POINTER)) {
            return a != NULL ? a : b;
        }
        return parser_usual_arith(a, b) != NULL ? parser_usual_arith(a, b) : b;
    case CC_EXPR_STMT:
        if (e->stmt_expr_count > 0 && e->stmt_expr_stmts[e->stmt_expr_count - 1].kind == CC_STMT_EXPR) {
            return parser_expr_ctype(p, e->stmt_expr_stmts[e->stmt_expr_count - 1].expr);
        }
        return cc_ct_basic(CC_CT_VOID);
    case CC_EXPR_SIZEOF:
        return cc_ct_basic(cc_target_pointer_size() >= 8 ? CC_CT_ULONG : CC_CT_UINT);
    default:
        return e->ctype;
    }
}

/**
 * The bound an array operand of sizeof has though its type leaves it out:
 * a global's (`int g[] = {1, 2, 3}`, also when a block-scope `extern int
 * g[];` names it) or a compound literal's (`(int[]){1, 2}`), from its
 * initializer.
 *
 * @param p  the parser
 * @param e  the operand
 * @param t  its type, an array of unknown bound
 * @return   the completed type, or @p t
 */
static const cc_ctype_t *parser_complete_operand_array(parser_t *p, const cc_expr_t *e, const cc_ctype_t *t) {
    const cc_expr_t *init = NULL;
    long n = 0;

    if (e->kind == CC_EXPR_IDENT && e->ident != NULL) {
        const cc_global_t *g = parser_find_global_decl(p, e->ident);

        if (g == NULL) {
            return t;
        }
        if (g->ctype != NULL && g->ctype->kind == CC_CT_ARRAY && g->ctype->count > 0) {
            return g->ctype;
        }
        init = g->init;
    } else if (e->kind == CC_EXPR_CAST) {
        init = e->lhs;
    }
    if (init != NULL && init->kind == CC_EXPR_INIT_LIST) {
        n = parser_init_list_bound_ct(p, t->of, init);
    } else if (init != NULL && init->kind == CC_EXPR_STR) {
        n = parser_string_literal_array_bound(init);
    }
    return n > 0 ? cc_ct_array(t->of, n) : t;
}

/**
 * The size in bytes of a sizeof expression, for the constant evaluator:
 * of its operand's type -- an array's bound completed from its
 * initializer where its type leaves it out -- or of the type it names.
 *
 * @param ctx  the parser
 * @param e    the CC_EXPR_SIZEOF node
 * @return     the size, or -1 if it is not known
 */
static long parser_const_sizeof(void *ctx, const cc_expr_t *e) {
    parser_t *p = (parser_t *)ctx;
    const cc_ctype_t *t = e->lhs != NULL ? parser_expr_ctype(p, e->lhs) : e->aux_ctype;
    long sz;

    if (t != NULL && t->kind == CC_CT_ARRAY && t->count < 0 && e->lhs != NULL) {
        t = parser_complete_operand_array(p, e->lhs, t);
    }
    if (t == NULL) {
        return -1;
    }
    if (t->unqual->kind == CC_CT_VOID || t->unqual->kind == CC_CT_FUNCTION) {
        /* sizeof of void or a function is 1, as gcc has it */
        return 1;
    }
    sz = parser_ct_size(p, t);
    return sz >= 0 ? sz : -1;
}

/**
 * The constant evaluator's view of the parser.
 *
 * @param p  the parser
 * @return   an environment whose callback reads @p p
 */
static cc_const_env_t parser_const_env(parser_t *p) {
    cc_const_env_t env;

    env.sizeof_bytes = parser_const_sizeof;
    env.ctx = p;
    return env;
}

/**
 * Evaluate an integer constant expression in the parser's context.
 *
 * @param p    the parser
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int eval_const_expr_value(parser_t *p, const cc_expr_t *e, parser_const_value_t *out) {
    cc_const_env_t env = parser_const_env(p);

    return cc_const_eval(&env, e, out);
}

int eval_const_array_bound_expr(parser_t *p, const cc_expr_t *e, long *out) {
    parser_const_value_t v;
    int64_t s;

    if (e == NULL || out == NULL) {
        return -1;
    }
    if (eval_const_expr_value(p, e, &v) != 0) {
        return -1;
    }
    if (cc_const_is_unsigned(&v)) {
        if (v.bits > (uint64_t)LONG_MAX) {
            return -1;
        }
        *out = (long)v.bits;
        return 0;
    }
    s = (int64_t)v.bits;
    if (s < (int64_t)LONG_MIN || s > (int64_t)LONG_MAX) {
        return -1;
    }
    *out = (long)s;
    return 0;
}

/**
 * Evaluate a case label's integer constant expression to a long long, a
 * width every case value fits on any host (a host long may be 32 bits).
 * An unsigned value keeps its bits: 0xffffffffu stays 4294967295.
 *
 * @param p    the parser
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int eval_const_case_value(parser_t *p, const cc_expr_t *e, long long *out) {
    parser_const_value_t v;

    if (e == NULL || out == NULL || eval_const_expr_value(p, e, &v) != 0) {
        return -1;
    }
    /* the bits are already extended as the type reads them */
    *out = (long long)v.bits;
    return 0;
}

/* ---- initializers: one canonical form (C11 6.7.9) ---------------------- */

/** How deep the subobjects one initializer list reaches through may nest. */
#define INIT_MAX_DEPTH 64

/** One level of the object an initializer list is filling: an aggregate, its list, the part next in line. */
typedef struct {
    const cc_ctype_t *type; /**< the aggregate */
    cc_expr_t *list;        /**< its canonical list, being built */
    long idx;               /**< the part (element or member) the next undesignated initializer goes to */
} init_frame_t;

/**
 * The structure or union @p t names, if it is one an initializer list
 * fills member by member -- not one standing for a wide integer, complex,
 * vector or decimal type, whose initializer is a value.
 */
static const cc_struct_def_t *init_record(parser_t *p, const cc_ctype_t *t) {
    const cc_struct_def_t *sd;

    if (t == NULL || t->unqual->kind != CC_CT_RECORD || t->unqual->id < 0 || (size_t)t->unqual->id >= p->struct_count) {
        return NULL;
    }
    sd = &p->structs[t->unqual->id];
    if (sd->wide_bits > 0 || sd->complex_elem != NULL || sd->vector_count > 0 || sd->decimal_bits > 0) {
        return NULL;
    }
    return sd;
}

/** Whether an initializer list fills an object of type @p t part by part: an array, structure or union. */
static int init_is_aggregate(parser_t *p, const cc_ctype_t *t) {
    return t != NULL && (t->unqual->kind == CC_CT_ARRAY || init_record(p, t) != NULL);
}

/**
 * How many parts undesignated initializers fill in aggregate @p t: an
 * array's elements (without limit for one of unknown bound), a
 * structure's members, a union's first member.
 */
static long init_part_count(parser_t *p, const cc_ctype_t *t) {
    const cc_struct_def_t *sd = init_record(p, t);

    if (sd == NULL) {
        return t->unqual->count >= 0 ? t->unqual->count : LONG_MAX;
    }
    return sd->is_union ? (sd->member_count > 0 ? 1 : 0) : (long)sd->member_count;
}

/** The type of part @p idx of aggregate @p t: an element, or member @p idx. */
static const cc_ctype_t *init_part_type(parser_t *p, const cc_ctype_t *t, long idx) {
    const cc_struct_def_t *sd = init_record(p, t);

    return sd == NULL ? t->unqual->of : sd->members[idx].ctype;
}

/** Whether initializer @p v is a string literal initializing @p t, an array of characters or wide characters. */
static int init_is_string_for(const cc_ctype_t *t, const cc_expr_t *v) {
    return v != NULL && v->kind == CC_EXPR_STR && t->unqual->kind == CC_CT_ARRAY && cc_ct_is_integer(t->unqual->of);
}

/** Whether non-list initializer @p v initializes all of structure or union @p t: an expression of its type. */
static int init_is_whole(parser_t *p, const cc_ctype_t *t, cc_expr_t *v) {
    const cc_ctype_t *vt = parser_expr_ctype(p, v);

    return vt != NULL && vt->unqual->kind == CC_CT_RECORD && t->unqual->kind == CC_CT_RECORD &&
           vt->unqual->id == t->unqual->id;
}

/** A new, empty initializer list. */
static cc_expr_t *init_new_list(void) {
    cc_expr_t *l = (cc_expr_t *)calloc(1, sizeof(*l));

    if (l != NULL) {
        l->kind = CC_EXPR_INIT_LIST;
        l->generic_selected = -1;
    }
    return l;
}

/** The slot for part @p idx of the list @p f is building, the list grown to hold it; NULL when out of memory. */
static cc_expr_t **init_slot(parser_t *p, init_frame_t *f, long idx) {
    const cc_struct_def_t *sd = init_record(p, f->type);
    size_t at = (sd != NULL && sd->is_union) ? 0 : (size_t)idx;

    if (sd != NULL && sd->is_union && f->list->int_val != idx + 1) {
        /* another member of the union: what the earlier one was given goes */
        if (f->list->arg_count > 0) {
            cc_expr_free(f->list->args[0]);
            f->list->args[0] = NULL;
        }
        f->list->int_val = idx + 1;
    }
    if (at >= f->list->arg_count) {
        cc_expr_t **next = (cc_expr_t **)realloc(f->list->args, (at + 1) * sizeof(*next));

        if (next == NULL) {
            return NULL;
        }
        memset(next + f->list->arg_count, 0, (at + 1 - f->list->arg_count) * sizeof(*next));
        f->list->args = next;
        f->list->arg_count = at + 1;
    }
    return &f->list->args[at];
}

static int init_canon_list(parser_t *p, const cc_ctype_t *t, cc_expr_t *in, cc_expr_t **out);

/**
 * Put initializer @p value (taken over) in part @p idx of the list @p f
 * builds, replacing what was there: a brace-enclosed list for an
 * aggregate part canonicalized, anything else as it is.
 *
 * @return 0, or -1 on error (diagnosed)
 */
static int init_store(parser_t *p, init_frame_t *f, long idx, cc_expr_t *value) {
    const cc_ctype_t *sub = init_part_type(p, f->type, idx);
    cc_expr_t **slot = init_slot(p, f, idx);

    if (slot == NULL) {
        cc_expr_free(value);
        parser_error(p, "out of memory canonicalizing an initializer");
        return -1;
    }
    if (value->kind == CC_EXPR_INIT_LIST && init_is_aggregate(p, sub)) {
        cc_expr_t *canon = NULL;

        if (init_canon_list(p, sub, value, &canon) != 0) {
            return -1;
        }
        value = canon;
    }
    cc_expr_free(*slot);
    *slot = value;
    return 0;
}

/**
 * The list building aggregate part @p idx of the list @p f builds, for an
 * initializer reaching into the part: the one already there, or a new one.
 *
 * @return the list, or NULL on error (diagnosed)
 */
static cc_expr_t *init_child(parser_t *p, init_frame_t *f, long idx) {
    cc_expr_t **slot = init_slot(p, f, idx);

    if (slot == NULL) {
        parser_error(p, "out of memory canonicalizing an initializer");
        return NULL;
    }
    if (*slot == NULL) {
        *slot = init_new_list();
        if (*slot == NULL) {
            parser_error(p, "out of memory canonicalizing an initializer");
        }
        return *slot;
    }
    if ((*slot)->kind != CC_EXPR_INIT_LIST) {
        parser_error(p, "unsupported initializer: a designator into part of a subobject initialized as a whole");
        return NULL;
    }
    return *slot;
}

/**
 * Where member @p name of structure or union @p sid is, as the indexes of
 * the members leading to it: one, or more through anonymous structures
 * and unions.
 *
 * @return the number of indexes in @p path, or 0 if it has no such member
 */
static int init_member_path(parser_t *p, int sid, const char *name, long *path, int max) {
    const cc_struct_def_t *sd = &p->structs[sid];
    size_t i;

    if (max <= 0) {
        return 0;
    }
    for (i = 0; i < sd->member_count; ++i) {
        if (sd->members[i].name != NULL && strcmp(sd->members[i].name, name) == 0) {
            path[0] = (long)i;
            return 1;
        }
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_ctype_t *mt = sd->members[i].ctype;

        if (sd->members[i].name == NULL && mt != NULL && mt->unqual->kind == CC_CT_RECORD) {
            int n = init_member_path(p, mt->unqual->id, name, path + 1, max - 1);

            if (n > 0) {
                path[0] = (long)i;
                return n + 1;
            }
        }
    }
    return 0;
}

/**
 * Go from frame @p fr[*depth - 1] to its part @p idx: enter that part,
 * an aggregate, as a new frame.
 *
 * @return 0, or -1 on error (diagnosed)
 */
static int init_enter(parser_t *p, init_frame_t *fr, int *depth, long idx) {
    init_frame_t *f = &fr[*depth - 1];
    cc_expr_t *child;

    if (*depth >= INIT_MAX_DEPTH) {
        parser_error(p, "initializer nests too deeply");
        return -1;
    }
    f->idx = idx;
    child = init_child(p, f, idx);
    if (child == NULL) {
        return -1;
    }
    fr[*depth].type = init_part_type(p, f->type, idx);
    fr[*depth].list = child;
    fr[*depth].idx = 0;
    (*depth)++;
    return 0;
}

/**
 * Follow designation @p d from the list's own object, frame 0: every
 * designator but the last enters the part it names; the last makes the
 * part it names the current one.  A range is allowed last only.
 *
 * @param out_hi  receives the last designator's upper index (its own for one not a range)
 * @return        0, or -1 on error (diagnosed)
 */
static int init_designate(parser_t *p, init_frame_t *fr, int *depth, const cc_expr_t *d, long *out_hi) {
    size_t k;

    *depth = 1;
    for (k = 0; k < d->arg_count; ++k) {
        const cc_expr_t *step = d->args[k];
        init_frame_t *f = &fr[*depth - 1];
        const cc_struct_def_t *sd = init_record(p, f->type);
        long path[INIT_MAX_DEPTH];
        int n = 1;
        int j;

        if (step->kind == CC_EXPR_MEMBER) {
            if (sd == NULL) {
                parser_error(p, "member designator for an object that is not a structure or union");
                return -1;
            }
            n = init_member_path(p, f->type->unqual->id, step->ident, path, INIT_MAX_DEPTH);
            if (n == 0) {
                char msg[160];

                snprintf(msg, sizeof(msg), "unknown member '%s' in designated initializer", step->ident);
                parser_error(p, msg);
                return -1;
            }
            *out_hi = path[n - 1];
        } else {
            if (f->type->unqual->kind != CC_CT_ARRAY) {
                parser_error(p, "array designator for an object that is not an array");
                return -1;
            }
            if (f->type->unqual->count >= 0 && step->int_val_hi >= f->type->unqual->count) {
                parser_error(p, "array designator index exceeds the array bound");
                return -1;
            }
            if (step->int_val != step->int_val_hi && k + 1 < d->arg_count) {
                parser_error(p, "unsupported initializer: a designator range before another designator");
                return -1;
            }
            path[0] = (long)step->int_val;
            *out_hi = (long)step->int_val_hi;
        }
        /* through anonymous members to the one named, then into it unless it is the last designator */
        for (j = 0; j < n; ++j) {
            if (j + 1 < n || k + 1 < d->arg_count) {
                if (!init_is_aggregate(p, init_part_type(p, fr[*depth - 1].type, path[j]))) {
                    parser_error(p, "designator for a part of a scalar");
                    return -1;
                }
                if (init_enter(p, fr, depth, path[j]) != 0) {
                    return -1;
                }
            } else {
                fr[*depth - 1].idx = path[j];
            }
        }
    }
    return 0;
}

/**
 * Canonicalize brace-enclosed list @p in (taken over) for an aggregate of
 * type @p t into @p out: each initializer goes to the part a designation
 * names or, undesignated, the next part in order; one that is not a list,
 * a string for an array of characters or a whole structure value, met at
 * an aggregate part, fills that part's own parts in turn (its braces
 * elided).
 *
 * @return 0, or -1 on error (diagnosed)
 */
static int init_canon_list(parser_t *p, const cc_ctype_t *t, cc_expr_t *in, cc_expr_t **out) {
    init_frame_t fr[INIT_MAX_DEPTH];
    int depth = 1;
    size_t i;

    fr[0].type = t;
    fr[0].list = init_new_list();
    fr[0].idx = 0;
    if (fr[0].list == NULL) {
        cc_expr_free(in);
        parser_error(p, "out of memory canonicalizing an initializer");
        return -1;
    }
    fr[0].list->line = in->line;
    fr[0].list->col = in->col;
    if (in->arg_count == 1 && init_is_string_for(t, in->args[0])) {
        /* `char s[] = {"abc"}`: the braced string, as it is */
        cc_expr_free(fr[0].list);
        *out = in;
        return 0;
    }
    for (i = 0; i < in->arg_count; ++i) {
        cc_expr_t *item = in->args[i];
        cc_expr_t *value = item;
        long hi = 0;
        long k;

        in->args[i] = NULL;
        if (item->kind == CC_EXPR_DESIGNATION) {
            value = item->rhs;
            item->rhs = NULL;
            if (init_designate(p, fr, &depth, item, &hi) != 0) {
                cc_expr_free(item);
                cc_expr_free(value);
                goto fail;
            }
            cc_expr_free(item);
        } else {
            /* the next part: past the end of an aggregate entered by elision, the one after it */
            while (fr[depth - 1].idx >= init_part_count(p, fr[depth - 1].type)) {
                if (depth == 1) {
                    const cc_struct_def_t *sd = init_record(p, t);

                    cc_expr_free(value);
                    parser_error(p, sd == NULL     ? "excess elements in array initializer"
                                    : sd->is_union ? "excess elements in union initializer"
                                                   : "excess elements in struct initializer");
                    goto fail;
                }
                depth--;
                fr[depth - 1].idx++;
            }
            hi = fr[depth - 1].idx;
        }
        /* brace elision: an initializer that is not the part's own list fills the part's first scalar */
        for (;;) {
            init_frame_t *f = &fr[depth - 1];
            const cc_ctype_t *sub = init_part_type(p, f->type, f->idx);

            if (!init_is_aggregate(p, sub) || value->kind == CC_EXPR_INIT_LIST || init_is_string_for(sub, value) ||
                (init_record(p, sub) != NULL && init_is_whole(p, sub, value)) || hi != f->idx) {
                break;
            }
            if (init_enter(p, fr, &depth, f->idx) != 0) {
                cc_expr_free(value);
                goto fail;
            }
            hi = 0;
        }
        /* a GNU range `[lo ... hi] = v` puts a copy in each */
        for (k = fr[depth - 1].idx; k < hi; ++k) {
            cc_expr_t *copy = cc_expr_clone(value);

            if (copy == NULL || init_store(p, &fr[depth - 1], k, copy) != 0) {
                cc_expr_free(value);
                if (copy == NULL) {
                    parser_error(p, "out of memory canonicalizing an initializer");
                }
                goto fail;
            }
        }
        if (init_store(p, &fr[depth - 1], hi, value) != 0) {
            goto fail;
        }
        fr[depth - 1].idx = init_record(p, fr[depth - 1].type) != NULL && init_record(p, fr[depth - 1].type)->is_union
                                ? init_part_count(p, fr[depth - 1].type)
                                : hi + 1;
    }
    cc_expr_free(in);
    *out = fr[0].list;
    return 0;
fail:
    cc_expr_free(in);
    cc_expr_free(fr[0].list);
    return -1;
}

/** A zero for a part no initializer named: `0`, or `{0}` for a vector or an aggregate. */
static cc_expr_t *init_zero(parser_t *p, const cc_ctype_t *t) {
    cc_expr_t *z = (cc_expr_t *)calloc(1, sizeof(*z));
    const cc_struct_def_t *sd;

    if (z == NULL) {
        return NULL;
    }
    z->kind = CC_EXPR_INT;
    z->generic_selected = -1;
    z->ctype = cc_ct_basic(CC_CT_INT);
    sd = t->unqual->kind == CC_CT_RECORD && t->unqual->id >= 0 ? &p->structs[t->unqual->id] : NULL;
    if (init_is_aggregate(p, t) || (sd != NULL && sd->vector_count > 0)) {
        cc_expr_t *l = init_new_list();

        if (l == NULL) {
            cc_expr_free(z);
            return NULL;
        }
        l->args = (cc_expr_t **)malloc(sizeof(*l->args));
        if (l->args == NULL) {
            cc_expr_free(l);
            cc_expr_free(z);
            return NULL;
        }
        l->args[0] = z;
        l->arg_count = 1;
        return l;
    }
    return z;
}

/**
 * Finish canonical list @p l for aggregate @p t, and the lists in it: a
 * zero in each gap, and a union's one initializer designated where it is
 * not its first member's.
 *
 * @return 0, or -1 on error (diagnosed)
 */
static int init_finish(parser_t *p, const cc_ctype_t *t, cc_expr_t *l) {
    const cc_struct_def_t *sd = init_record(p, t);
    size_t i;

    if (sd != NULL && sd->is_union) {
        long member = l->int_val - 1;

        l->int_val = 0;
        if (l->arg_count == 0 || member < 0) {
            return 0;
        }
        if (l->args[0] != NULL && l->args[0]->kind == CC_EXPR_INIT_LIST &&
            init_is_aggregate(p, sd->members[member].ctype) && init_finish(p, sd->members[member].ctype, l->args[0]) != 0) {
            return -1;
        }
        if (member > 0) {
            cc_expr_t *d = (cc_expr_t *)calloc(1, sizeof(*d));

            if (sd->members[member].name == NULL) {
                cc_expr_free(d);
                parser_error(p, "unsupported initializer: an anonymous union member other than the first");
                return -1;
            }
            if (d == NULL || (d->ident = cc_strndup(sd->members[member].name, strlen(sd->members[member].name))) == NULL) {
                cc_expr_free(d);
                parser_error(p, "out of memory canonicalizing an initializer");
                return -1;
            }
            d->kind = CC_EXPR_MEMBER;
            d->generic_selected = -1;
            d->rhs = l->args[0];
            l->args[0] = d;
        }
        return 0;
    }
    for (i = 0; i < l->arg_count; ++i) {
        const cc_ctype_t *sub = init_part_type(p, t, (long)i);

        if (l->args[i] == NULL) {
            l->args[i] = init_zero(p, sub);
            if (l->args[i] == NULL) {
                parser_error(p, "out of memory canonicalizing an initializer");
                return -1;
            }
        } else if (l->args[i]->kind == CC_EXPR_INIT_LIST && init_is_aggregate(p, sub) &&
                   init_finish(p, sub, l->args[i]) != 0) {
            return -1;
        }
    }
    if (l->arg_count == 0 && init_part_count(p, t) > 0 && t->unqual->count != 0) {
        /* a part that a designator reached into and left empty: all zero */
        l->args = (cc_expr_t **)malloc(sizeof(*l->args));
        if (l->args == NULL || (l->args[0] = init_zero(p, init_part_type(p, t, 0))) == NULL) {
            parser_error(p, "out of memory canonicalizing an initializer");
            return -1;
        }
        l->arg_count = 1;
    }
    return 0;
}

int parser_canon_initializer(parser_t *p, const cc_ctype_t *t, cc_expr_t **slot, long *out_len) {
    cc_expr_t *canon = NULL;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (t == NULL || slot == NULL || *slot == NULL || (*slot)->kind != CC_EXPR_INIT_LIST || !init_is_aggregate(p, t)) {
        return 0;
    }
    if ((*slot)->arg_count == 1 && init_is_string_for(t, (*slot)->args[0])) {
        if (out_len != NULL) {
            *out_len = parser_string_literal_array_bound((*slot)->args[0]);
        }
        return 0;
    }
    if (init_canon_list(p, t, *slot, &canon) != 0) {
        *slot = NULL;
        return -1;
    }
    *slot = canon;
    if (init_finish(p, t, canon) != 0) {
        return -1;
    }
    if (out_len != NULL && t->unqual->kind == CC_CT_ARRAY) {
        *out_len = (long)canon->arg_count;
    }
    return 0;
}
