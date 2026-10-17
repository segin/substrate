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
