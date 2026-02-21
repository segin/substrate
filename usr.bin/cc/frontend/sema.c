#include "cc_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    cc_type_t type;
    int depth;
} var_entry_t;

static void set_diag(cc_diag_t *d, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    d->line = 0;
    d->col = 0;
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

static int names_find(char **names, size_t count, const char *name) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (strcmp(names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int vars_find_visible(var_entry_t *vars, size_t count, const char *name, int depth) {
    size_t i = count;
    while (i > 0) {
        i--;
        if (vars[i].depth <= depth && strcmp(vars[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int vars_find_depth(var_entry_t *vars, size_t count, const char *name, int depth) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (vars[i].depth == depth && strcmp(vars[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int vars_push(var_entry_t **vars, size_t *count, const char *name, cc_type_t type, int depth) {
    var_entry_t *next;
    char *dup;

    next = (var_entry_t *)realloc(*vars, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *vars = next;

    dup = (char *)malloc(strlen(name) + 1);
    if (dup == NULL) {
        return -1;
    }
    strcpy(dup, name);
    (*vars)[*count].name = dup;
    (*vars)[*count].type = type;
    (*vars)[*count].depth = depth;
    (*count)++;
    return 0;
}

static const cc_function_t *find_function(const cc_translation_unit_t *tu, const char *name) {
    size_t i;
    for (i = 0; i < tu->func_count; ++i) {
        if (strcmp(tu->funcs[i].name, name) == 0) {
            return &tu->funcs[i];
        }
    }
    return NULL;
}

static int can_convert(cc_type_t dst, cc_type_t src) {
    if (dst == src) {
        return 1;
    }
    if ((dst == CC_TYPE_INT || dst == CC_TYPE_DOUBLE) && (src == CC_TYPE_INT || src == CC_TYPE_DOUBLE)) {
        return 1;
    }
    return 0;
}

static cc_type_t common_arith_type(cc_type_t a, cc_type_t b) {
    if (a == CC_TYPE_VOID || b == CC_TYPE_VOID) {
        return CC_TYPE_VOID;
    }
    if (a == CC_TYPE_DOUBLE || b == CC_TYPE_DOUBLE) {
        return CC_TYPE_DOUBLE;
    }
    return CC_TYPE_INT;
}

static int is_cmp_op(cc_binop_t op) {
    return op == CC_BIN_EQ || op == CC_BIN_NE || op == CC_BIN_LT || op == CC_BIN_LE || op == CC_BIN_GT ||
           op == CC_BIN_GE;
}

static int check_expr(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count, int depth,
                      cc_diag_t *diag) {
    size_t i;

    if (e == NULL) {
        set_diag(diag, "null expression in semantic analysis");
        return -1;
    }

    switch (e->kind) {
    case CC_EXPR_INT:
        e->value_type = CC_TYPE_INT;
        return 0;

    case CC_EXPR_FLOAT:
        e->value_type = CC_TYPE_DOUBLE;
        return 0;

    case CC_EXPR_IDENT: {
        int idx = vars_find_visible(vars, var_count, e->ident, depth);
        if (idx < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "unknown identifier: %s", e->ident);
            }
            return -1;
        }
        e->value_type = vars[idx].type;
        return 0;
    }

    case CC_EXPR_BIN:
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (is_cmp_op(e->op)) {
            if (e->lhs->value_type != CC_TYPE_INT || e->rhs->value_type != CC_TYPE_INT) {
                set_diag(diag, "comparison operators currently require integer operands");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            return 0;
        }
        e->value_type = common_arith_type(e->lhs->value_type, e->rhs->value_type);
        if (e->value_type == CC_TYPE_VOID) {
            set_diag(diag, "void expression used in arithmetic operation");
            return -1;
        }
        return 0;

    case CC_EXPR_CALL: {
        const cc_function_t *callee = find_function(tu, e->ident);
        if (callee == NULL) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "unknown function: %s", e->ident);
            }
            return -1;
        }
        if (!callee->is_variadic && e->arg_count != callee->param_count) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "call to %s has %zu args but %zu required", e->ident,
                         e->arg_count, callee->param_count);
            }
            return -1;
        }
        if (callee->is_variadic && e->arg_count < callee->param_count) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "variadic call to %s has %zu args but needs at least %zu", e->ident, e->arg_count,
                         callee->param_count);
            }
            return -1;
        }
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (i < callee->param_count && !can_convert(callee->params[i].type, e->args[i]->value_type)) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "cannot convert arg %zu in call to %s", i,
                             e->ident);
                }
                return -1;
            }
        }
        e->value_type = callee->ret_type;
        return 0;
    }

    case CC_EXPR_ASSIGN: {
        int idx = vars_find_visible(vars, var_count, e->ident, depth);
        if (idx < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "assignment to undeclared identifier: %s",
                         e->ident ? e->ident : "<null>");
            }
            return -1;
        }
        if (check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!can_convert(vars[idx].type, e->rhs->value_type)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "cannot assign expression to %s", e->ident);
            }
            return -1;
        }
        e->value_type = vars[idx].type;
        return 0;
    }

    default:
        set_diag(diag, "unsupported expression kind");
        return -1;
    }
}

static int check_stmt(const cc_translation_unit_t *tu, cc_stmt_t *s, var_entry_t **vars, size_t *var_count, int depth,
                      cc_type_t fn_ret_type, int loop_depth, int switch_depth, int *saw_return, cc_diag_t *diag) {
    size_t i;

    switch (s->kind) {
    case CC_STMT_DECL:
        if (s->type == CC_TYPE_VOID) {
            set_diag(diag, "void variable declarations are not supported");
            return -1;
        }
        if (vars_find_depth(*vars, *var_count, s->decl_name, depth) >= 0) {
            set_diag(diag, "duplicate local/parameter name");
            return -1;
        }
        if (s->expr != NULL) {
            if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }
            if (!can_convert(s->type, s->expr->value_type)) {
                set_diag(diag, "cannot initialize variable with incompatible type");
                return -1;
            }
        }
        if (vars_push(vars, var_count, s->decl_name, s->type, depth) != 0) {
            set_diag(diag, "out of memory adding local variable");
            return -1;
        }
        return 0;

    case CC_STMT_EXPR:
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        return 0;

    case CC_STMT_RETURN:
        if (fn_ret_type == CC_TYPE_VOID) {
            if (s->expr != NULL) {
                set_diag(diag, "void function cannot return a value");
                return -1;
            }
        } else {
            if (s->expr == NULL) {
                set_diag(diag, "non-void function must return a value");
                return -1;
            }
            if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }
            if (!can_convert(fn_ret_type, s->expr->value_type)) {
                set_diag(diag, "return type mismatch");
                return -1;
            }
        }
        *saw_return = 1;
        return 0;

    case CC_STMT_IF:
        if (s->expr == NULL || s->then_branch == NULL) {
            set_diag(diag, "malformed if statement");
            return -1;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (s->expr->value_type == CC_TYPE_VOID) {
            set_diag(diag, "if condition cannot be void");
            return -1;
        }
        {
            size_t saved = *var_count;
            if (check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, loop_depth, switch_depth,
                           saw_return, diag) != 0) {
                return -1;
            }
            for (i = saved; i < *var_count; ++i) {
                free((*vars)[i].name);
            }
            *var_count = saved;
        }
        if (s->else_branch != NULL) {
            size_t saved = *var_count;
            if (check_stmt(tu, s->else_branch, vars, var_count, depth, fn_ret_type, loop_depth, switch_depth,
                           saw_return, diag) != 0) {
                return -1;
            }
            for (i = saved; i < *var_count; ++i) {
                free((*vars)[i].name);
            }
            *var_count = saved;
        }
        return 0;

    case CC_STMT_BLOCK:
        {
            size_t saved = *var_count;
            for (i = 0; i < s->block_count; ++i) {
                if (check_stmt(tu, &s->block_stmts[i], vars, var_count, depth + 1, fn_ret_type, loop_depth,
                               switch_depth,
                               saw_return, diag) != 0) {
                    return -1;
                }
            }
            for (i = saved; i < *var_count; ++i) {
                free((*vars)[i].name);
            }
            *var_count = saved;
            return 0;
        }

    case CC_STMT_WHILE:
        if (s->expr == NULL || s->then_branch == NULL) {
            set_diag(diag, "malformed while statement");
            return -1;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (s->expr->value_type == CC_TYPE_VOID) {
            set_diag(diag, "while condition cannot be void");
            return -1;
        }
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, loop_depth + 1, switch_depth,
                          saw_return, diag);

    case CC_STMT_DO:
        if (s->expr == NULL || s->then_branch == NULL) {
            set_diag(diag, "malformed do-while statement");
            return -1;
        }
        if (check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, loop_depth + 1, switch_depth,
                       saw_return, diag) != 0) {
            return -1;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (s->expr->value_type == CC_TYPE_VOID) {
            set_diag(diag, "do-while condition cannot be void");
            return -1;
        }
        return 0;

    case CC_STMT_FOR:
        if (s->then_branch == NULL) {
            set_diag(diag, "malformed for statement");
            return -1;
        }
        if (s->init_expr != NULL && check_expr(tu, s->init_expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (s->expr != NULL) {
            if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }
            if (s->expr->value_type == CC_TYPE_VOID) {
                set_diag(diag, "for condition cannot be void");
                return -1;
            }
        }
        if (s->post_expr != NULL && check_expr(tu, s->post_expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, loop_depth + 1, switch_depth,
                          saw_return, diag);

    case CC_STMT_SWITCH:
        if (s->expr == NULL || s->then_branch == NULL) {
            set_diag(diag, "malformed switch statement");
            return -1;
        }
        if (s->then_branch->kind != CC_STMT_BLOCK) {
            set_diag(diag, "switch body must be a block statement");
            return -1;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (s->expr->value_type != CC_TYPE_INT) {
            set_diag(diag, "switch expression must be integer");
            return -1;
        }
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, loop_depth, switch_depth + 1,
                          saw_return, diag);

    case CC_STMT_CASE:
        if (switch_depth <= 0) {
            set_diag(diag, "case label used outside switch");
            return -1;
        }
        if (s->expr == NULL) {
            set_diag(diag, "malformed case label");
            return -1;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (s->expr->kind != CC_EXPR_INT) {
            set_diag(diag, "case label must be an integer constant");
            return -1;
        }
        return 0;

    case CC_STMT_DEFAULT:
        if (switch_depth <= 0) {
            set_diag(diag, "default label used outside switch");
            return -1;
        }
        return 0;

    case CC_STMT_BREAK:
        if (loop_depth <= 0 && switch_depth <= 0) {
            set_diag(diag, "break used outside loop/switch");
            return -1;
        }
        return 0;

    case CC_STMT_CONTINUE:
        if (loop_depth <= 0) {
            set_diag(diag, "continue used outside loop");
            return -1;
        }
        return 0;

    default:
        set_diag(diag, "unsupported statement kind");
        return -1;
    }
}

int cc_sema_check(const cc_translation_unit_t *tu, cc_diag_t *diag) {
    size_t i;
    char **fn_names = NULL;
    size_t fn_name_count = 0;

    if (diag != NULL) {
        diag->line = 0;
        diag->col = 0;
        diag->message[0] = '\0';
    }

    if (tu == NULL || tu->func_count == 0) {
        set_diag(diag, "translation unit is empty");
        return -1;
    }

    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *f = &tu->funcs[i];
        char *dup;

        if (f->name == NULL || f->name[0] == '\0') {
            set_diag(diag, "function with missing name");
            goto fail_global;
        }
        if (names_find(fn_names, fn_name_count, f->name) >= 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "duplicate function definition: %s", f->name);
            }
            goto fail_global;
        }
        dup = (char *)malloc(strlen(f->name) + 1);
        if (dup == NULL) {
            set_diag(diag, "out of memory");
            goto fail_global;
        }
        strcpy(dup, f->name);
        {
            char **next = (char **)realloc(fn_names, (fn_name_count + 1) * sizeof(*next));
            if (next == NULL) {
                free(dup);
                set_diag(diag, "out of memory");
                goto fail_global;
            }
            fn_names = next;
            fn_names[fn_name_count++] = dup;
        }
    }

    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *f = &tu->funcs[i];
        var_entry_t *vars = NULL;
        size_t var_count = 0;
        size_t j;
        int saw_return = 0;

        for (j = 0; j < f->param_count; ++j) {
            if (f->params[j].type == CC_TYPE_VOID) {
                set_diag(diag, "void is not valid for named parameter type");
                goto fail_func;
            }
            if (vars_find_depth(vars, var_count, f->params[j].name, 0) >= 0) {
                set_diag(diag, "duplicate parameter name");
                goto fail_func;
            }
            if (vars_push(&vars, &var_count, f->params[j].name, f->params[j].type, 0) != 0) {
                set_diag(diag, "out of memory adding parameter");
                goto fail_func;
            }
        }

        if (f->stmt_count == 0) {
            set_diag(diag, "function has empty body");
            goto fail_func;
        }

        for (j = 0; j < f->stmt_count; ++j) {
                if (check_stmt(tu, &f->stmts[j], &vars, &var_count, 0, f->ret_type, 0, 0, &saw_return, diag) != 0) {
                    goto fail_func;
                }
        }

        if (!saw_return && f->ret_type != CC_TYPE_VOID) {
            set_diag(diag, "non-void function requires at least one return statement");
            goto fail_func;
        }

        for (j = 0; j < var_count; ++j) {
            free(vars[j].name);
        }
        free(vars);
        continue;

fail_func:
        for (j = 0; j < var_count; ++j) {
            free(vars[j].name);
        }
        free(vars);
        goto fail_global;
    }

    for (i = 0; i < fn_name_count; ++i) {
        free(fn_names[i]);
    }
    free(fn_names);
    return 0;

fail_global:
    for (i = 0; i < fn_name_count; ++i) {
        free(fn_names[i]);
    }
    free(fn_names);
    return -1;
}
