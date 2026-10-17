/*
 * nested.c: what a GNU C nested function sees of the function it is
 * defined in.
 *
 * A nested function is hoisted to a function of its own, with
 * nest_parent naming the function it was defined in and nest_stmt_index
 * saying where.  The enclosing function's variables visible there -- its
 * parameters, then its top-level declarations before that point, and for
 * a function nested in a nested function, first the variables that one
 * sees -- are what the nested function may use.  Sema puts them in an
 * outer scope; lowering passes their addresses to the nested function in
 * an array, in this order, whenever the nested function uses any of them.
 */

#include <stdlib.h>
#include <string.h>

#include "cc_frontend.h"

/**
 * The definition (with a body) of a function, by name.
 *
 * @param tu    the translation unit
 * @param name  the function's name
 * @return      the definition, or NULL if there is none
 */
const cc_function_t *cc_find_function_def(const cc_translation_unit_t *tu, const char *name) {
    size_t i;

    if (tu == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < tu->func_count; ++i) {
        if (tu->funcs[i].has_body && tu->funcs[i].name != NULL && strcmp(tu->funcs[i].name, name) == 0) {
            return &tu->funcs[i];
        }
    }
    return NULL;
}

/**
 * Append one variable to a growing list.
 *
 * @return 0, or -1 when out of memory
 */
static int outer_push(cc_outer_var_t **list, size_t *count, size_t *cap, const cc_outer_var_t *v) {
    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 8 : *cap * 2;
        cc_outer_var_t *next = (cc_outer_var_t *)realloc(*list, ncap * sizeof(*next));

        if (next == NULL) {
            return -1;
        }
        *list = next;
        *cap = ncap;
    }
    (*list)[(*count)++] = *v;
    return 0;
}

/**
 * Append the variables a run of top-level statements declares, looking
 * into the synthetic blocks a declaration with several declarators makes.
 *
 * @return 0, or -1 when out of memory
 */
static int outer_push_decls(const cc_stmt_t *stmts, size_t n, cc_outer_var_t **list, size_t *count, size_t *cap) {
    size_t i;

    for (i = 0; i < n; ++i) {
        const cc_stmt_t *s = &stmts[i];

        if (s->kind == CC_STMT_BLOCK && s->is_synthetic_block) {
            if (outer_push_decls(s->block_stmts, s->block_count, list, count, cap) != 0) {
                return -1;
            }
        } else if (s->kind == CC_STMT_DECL && s->decl_name != NULL) {
            cc_outer_var_t v;

            v.name = s->decl_name;
            v.storage = s->storage;
            v.ctype = s->ctype;
            if (outer_push(list, count, cap, &v) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/**
 * The enclosing function's variables a nested function may use, in the
 * order lowering lays out their addresses.  A name may appear more than
 * once (a later declaration hides an earlier one); the last is the one
 * in scope.
 *
 * @param tu     the translation unit
 * @param f      the function
 * @param out    receives the list (free it); NULL if there is none
 * @param count  receives its length; 0 if @p f is not a nested function
 * @return       0, or -1 when out of memory or the enclosing function is
 *               missing
 */
int cc_nested_outer_vars(const cc_translation_unit_t *tu, const cc_function_t *f, cc_outer_var_t **out,
                         size_t *count) {
    const cc_function_t *parent;
    cc_outer_var_t *list = NULL;
    size_t n = 0;
    size_t cap = 0;
    size_t i;

    *out = NULL;
    *count = 0;
    if (f == NULL || f->nest_parent == NULL) {
        return 0;
    }
    parent = cc_find_function_def(tu, f->nest_parent);
    if (parent == NULL || parent == f) {
        return -1;
    }
    if (parent->nest_parent != NULL) {
        cc_outer_var_t *up;
        size_t up_n;

        if (cc_nested_outer_vars(tu, parent, &up, &up_n) != 0) {
            return -1;
        }
        for (i = 0; i < up_n; ++i) {
            if (outer_push(&list, &n, &cap, &up[i]) != 0) {
                free(up);
                free(list);
                return -1;
            }
        }
        free(up);
    }
    for (i = 0; i < parent->param_count; ++i) {
        const cc_param_t *pa = &parent->params[i];
        cc_outer_var_t v;

        if (pa->name == NULL) {
            continue;
        }
        v.name = pa->name;
        v.storage = pa->storage;
        v.ctype = pa->ctype;
        if (outer_push(&list, &n, &cap, &v) != 0) {
            free(list);
            return -1;
        }
    }
    if (outer_push_decls(parent->stmts, f->nest_stmt_index < parent->stmt_count ? f->nest_stmt_index
                                                                                 : parent->stmt_count,
                         &list, &n, &cap) != 0) {
        free(list);
        return -1;
    }
    *out = list;
    *count = n;
    return 0;
}

/** Whether a name is one of the outer variables. */
static int outer_has(const cc_outer_var_t *list, size_t n, const char *name) {
    size_t i;

    for (i = 0; name != NULL && i < n; ++i) {
        if (strcmp(list[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/** The deepest chain of nested functions calling one another that is followed. */
#define NESTED_VISIT_MAX 32

/** What the walk of a nested function's body looks for, and the functions being walked. */
typedef struct {
    const cc_translation_unit_t *tu;
    const cc_outer_var_t *list; /**< the outer variables the function can name */
    size_t n;
    const cc_function_t **visiting; /**< the nested functions whose bodies are being walked */
    size_t visiting_count;
} uses_walk_t;

static int uses_outer_rec(const cc_translation_unit_t *tu, const cc_function_t *f, const cc_function_t **visiting,
                          size_t visiting_count);
static int stmt_uses_outer(const cc_stmt_t *s, const uses_walk_t *w);

/**
 * Whether an expression names any of the outer variables, or calls a
 * nested function that needs them (whose environment the caller must then
 * have to pass on).
 */
static int expr_uses_outer(const cc_expr_t *e, const uses_walk_t *w) {
    size_t i;

    if (e == NULL) {
        return 0;
    }
    if (e->kind == CC_EXPR_IDENT && outer_has(w->list, w->n, e->ident)) {
        return 1;
    }
    if (e->kind == CC_EXPR_CALL && e->ident != NULL) {
        const cc_function_t *g = cc_find_function_def(w->tu, e->ident);

        if (g != NULL && g->nest_parent != NULL &&
            uses_outer_rec(w->tu, g, w->visiting, w->visiting_count)) {
            return 1;
        }
    }
    if (expr_uses_outer(e->lhs, w) || expr_uses_outer(e->rhs, w) || expr_uses_outer(e->third, w)) {
        return 1;
    }
    for (i = 0; i < e->arg_count; ++i) {
        if (expr_uses_outer(e->args[i], w)) {
            return 1;
        }
    }
    for (i = 0; i < e->stmt_expr_count; ++i) {
        if (stmt_uses_outer(&e->stmt_expr_stmts[i], w)) {
            return 1;
        }
    }
    return 0;
}

/** Whether a statement names any of the outer variables (see expr_uses_outer()). */
static int stmt_uses_outer(const cc_stmt_t *s, const uses_walk_t *w) {
    size_t i;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < s->array_bounds.count; ++i) {
        if (expr_uses_outer(s->array_bounds.at[i], w)) {
            return 1;
        }
    }
    if (expr_uses_outer(s->expr, w) || expr_uses_outer(s->init_expr, w) || expr_uses_outer(s->post_expr, w) ||
        stmt_uses_outer(s->init_stmt, w) || stmt_uses_outer(s->then_branch, w) ||
        stmt_uses_outer(s->else_branch, w)) {
        return 1;
    }
    for (i = 0; i < s->block_count; ++i) {
        if (stmt_uses_outer(&s->block_stmts[i], w)) {
            return 1;
        }
    }
    for (i = 0; i < s->asm_output_count; ++i) {
        if (expr_uses_outer(s->asm_outputs[i].expr, w)) {
            return 1;
        }
    }
    for (i = 0; i < s->asm_input_count; ++i) {
        if (expr_uses_outer(s->asm_inputs[i].expr, w)) {
            return 1;
        }
    }
    return 0;
}

/**
 * cc_nested_uses_outer() for @p f, with the nested functions already being
 * walked: a call back into one of them (recursion, or two that call each
 * other) is not followed again, since its own walk decides it.
 */
static int uses_outer_rec(const cc_translation_unit_t *tu, const cc_function_t *f, const cc_function_t **visiting,
                          size_t visiting_count) {
    const cc_function_t *stack[NESTED_VISIT_MAX];
    cc_outer_var_t *list;
    uses_walk_t w;
    size_t n;
    size_t i;
    size_t kept = 0;
    int uses = 0;

    if (f == NULL || f->nest_parent == NULL || visiting_count >= NESTED_VISIT_MAX) {
        return 0;
    }
    for (i = 0; i < visiting_count; ++i) {
        if (visiting[i] == f) {
            return 0;
        }
    }
    if (cc_nested_outer_vars(tu, f, &list, &n) != 0) {
        return 0;
    }
    /* a parameter of f hides an outer variable of the same name */
    for (i = 0; i < n; ++i) {
        size_t j;
        int hidden = 0;

        for (j = 0; j < f->param_count; ++j) {
            if (f->params[j].name != NULL && strcmp(f->params[j].name, list[i].name) == 0) {
                hidden = 1;
                break;
            }
        }
        if (!hidden) {
            list[kept++] = list[i];
        }
    }
    for (i = 0; i < visiting_count; ++i) {
        stack[i] = visiting[i];
    }
    stack[visiting_count] = f;
    w.tu = tu;
    w.list = list;
    w.n = kept;
    w.visiting = stack;
    w.visiting_count = visiting_count + 1;
    for (i = 0; i < f->stmt_count && !uses; ++i) {
        uses = stmt_uses_outer(&f->stmts[i], &w);
    }
    free(list);
    return uses;
}

/**
 * Whether a nested function uses any of the enclosing function's variables,
 * and so is given their addresses in the static chain register: it names one --
 * any identifier with an outer variable's name other than one of its own
 * parameters; one hidden by a local of it still counts, which costs only
 * the argument -- or calls a nested function that does.
 *
 * @param tu  the translation unit
 * @param f   the function, or NULL
 * @return    1 or 0 (0 for a function that is not nested)
 */
int cc_nested_uses_outer(const cc_translation_unit_t *tu, const cc_function_t *f) {
    return uses_outer_rec(tu, f, NULL, 0);
}
