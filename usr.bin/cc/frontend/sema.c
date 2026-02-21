#include "cc_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    cc_type_t type;
    int depth;
} var_entry_t;

typedef struct {
    char **items;
    size_t count;
} name_list_t;

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

static void name_list_free(name_list_t *l) {
    size_t i;
    if (l == NULL) {
        return;
    }
    for (i = 0; i < l->count; ++i) {
        free(l->items[i]);
    }
    free(l->items);
    l->items = NULL;
    l->count = 0;
}

static int name_list_push(name_list_t *l, const char *name) {
    char **next;
    char *dup;
    next = (char **)realloc(l->items, (l->count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    l->items = next;
    dup = (char *)malloc(strlen(name) + 1);
    if (dup == NULL) {
        return -1;
    }
    strcpy(dup, name);
    l->items[l->count++] = dup;
    return 0;
}

static int collect_labels_gotos_stmt(const cc_stmt_t *s, name_list_t *labels, name_list_t *gotos, cc_diag_t *diag) {
    size_t i;
    if (s == NULL) {
        return 0;
    }
    switch (s->kind) {
    case CC_STMT_LABEL:
        if (s->label_name == NULL || s->label_name[0] == '\0') {
            set_diag(diag, "malformed label statement");
            return -1;
        }
        if (names_find(labels->items, labels->count, s->label_name) >= 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "duplicate label: %s", s->label_name);
            }
            return -1;
        }
        if (name_list_push(labels, s->label_name) != 0) {
            set_diag(diag, "out of memory collecting labels");
            return -1;
        }
        return collect_labels_gotos_stmt(s->then_branch, labels, gotos, diag);

    case CC_STMT_GOTO:
        if (s->label_name == NULL || s->label_name[0] == '\0') {
            set_diag(diag, "malformed goto statement");
            return -1;
        }
        if (names_find(gotos->items, gotos->count, s->label_name) < 0 && name_list_push(gotos, s->label_name) != 0) {
            set_diag(diag, "out of memory collecting goto targets");
            return -1;
        }
        return 0;

    case CC_STMT_IF:
        if (collect_labels_gotos_stmt(s->then_branch, labels, gotos, diag) != 0) {
            return -1;
        }
        return collect_labels_gotos_stmt(s->else_branch, labels, gotos, diag);

    case CC_STMT_WHILE:
    case CC_STMT_DO:
    case CC_STMT_SWITCH:
        return collect_labels_gotos_stmt(s->then_branch, labels, gotos, diag);

    case CC_STMT_FOR:
        if (collect_labels_gotos_stmt(s->init_stmt, labels, gotos, diag) != 0) {
            return -1;
        }
        return collect_labels_gotos_stmt(s->then_branch, labels, gotos, diag);

    case CC_STMT_BLOCK:
        for (i = 0; i < s->block_count; ++i) {
            if (collect_labels_gotos_stmt(&s->block_stmts[i], labels, gotos, diag) != 0) {
                return -1;
            }
        }
        return 0;

    default:
        return 0;
    }
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

static int func_decl_compatible(const cc_function_t *a, const cc_function_t *b) {
    size_t i;
    if (a->ret_type != b->ret_type || a->is_variadic != b->is_variadic || a->param_count != b->param_count) {
        return 0;
    }
    for (i = 0; i < a->param_count; ++i) {
        if (a->params[i].type != b->params[i].type) {
            return 0;
        }
    }
    return 1;
}

static int is_float_type(cc_type_t t) {
    return t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE;
}

static int is_unsigned_integral_type(cc_type_t t) {
    return t == CC_TYPE_UCHAR || t == CC_TYPE_UINT || t == CC_TYPE_ULONG_LONG;
}

static int is_integral_type(cc_type_t t) {
    return t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_UCHAR || t == CC_TYPE_INT || t == CC_TYPE_UINT ||
           t == CC_TYPE_LONG_LONG || t == CC_TYPE_ULONG_LONG;
}

static int is_numeric_type(cc_type_t t) {
    return is_integral_type(t) || is_float_type(t);
}

static int can_convert(cc_type_t dst, cc_type_t src) {
    if (dst == src) {
        return 1;
    }
    if (is_numeric_type(dst) && is_numeric_type(src)) {
        return 1;
    }
    return 0;
}

static cc_type_t integral_promo_type(cc_type_t t) {
    if (t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_UCHAR) {
        return CC_TYPE_INT;
    }
    return t;
}

static cc_type_t common_arith_type(cc_type_t a, cc_type_t b) {
    cc_type_t ap;
    cc_type_t bp;

    if (a == CC_TYPE_VOID || b == CC_TYPE_VOID) {
        return CC_TYPE_VOID;
    }
    if (a == CC_TYPE_DOUBLE || b == CC_TYPE_DOUBLE) {
        return CC_TYPE_DOUBLE;
    }
    if (a == CC_TYPE_FLOAT || b == CC_TYPE_FLOAT) {
        return CC_TYPE_FLOAT;
    }

    ap = integral_promo_type(a);
    bp = integral_promo_type(b);

    if (ap == CC_TYPE_ULONG_LONG || bp == CC_TYPE_ULONG_LONG) {
        return CC_TYPE_ULONG_LONG;
    }
    if (ap == CC_TYPE_LONG_LONG || bp == CC_TYPE_LONG_LONG) {
        if (is_unsigned_integral_type(ap) || is_unsigned_integral_type(bp)) {
            return CC_TYPE_ULONG_LONG;
        }
        return CC_TYPE_LONG_LONG;
    }
    if (ap == CC_TYPE_UINT || bp == CC_TYPE_UINT) {
        return CC_TYPE_UINT;
    }
    return CC_TYPE_INT;
}

static int is_cmp_op(cc_binop_t op) {
    return op == CC_BIN_EQ || op == CC_BIN_NE || op == CC_BIN_LT || op == CC_BIN_LE || op == CC_BIN_GT ||
           op == CC_BIN_GE;
}

static int is_logical_op(cc_binop_t op) {
    return op == CC_BIN_LAND || op == CC_BIN_LOR;
}

static int is_shift_op(cc_binop_t op) {
    return op == CC_BIN_SHL || op == CC_BIN_SHR;
}

static int is_bitwise_op(cc_binop_t op) {
    return op == CC_BIN_BAND || op == CC_BIN_BXOR || op == CC_BIN_BOR;
}

static long type_size_bytes(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_UCHAR:
        return 1;
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

static int eval_const_int_expr(const cc_expr_t *e, long *out) {
    long a;
    long b;

    if (e == NULL || out == NULL) {
        return -1;
    }

    switch (e->kind) {
    case CC_EXPR_INT:
        *out = e->int_val;
        return 0;

    case CC_EXPR_BIN:
        if (eval_const_int_expr(e->lhs, &a) != 0 || eval_const_int_expr(e->rhs, &b) != 0) {
            return -1;
        }
        switch (e->op) {
        case CC_BIN_ADD:
            *out = a + b;
            return 0;
        case CC_BIN_SUB:
            *out = a - b;
            return 0;
        case CC_BIN_MUL:
            *out = a * b;
            return 0;
        case CC_BIN_DIV:
            if (b == 0) {
                return -1;
            }
            *out = a / b;
            return 0;
        case CC_BIN_MOD:
            if (b == 0) {
                return -1;
            }
            *out = a % b;
            return 0;
        case CC_BIN_SHL:
            *out = a << (b & 63);
            return 0;
        case CC_BIN_SHR:
            *out = a >> (b & 63);
            return 0;
        case CC_BIN_BAND:
            *out = a & b;
            return 0;
        case CC_BIN_BOR:
            *out = a | b;
            return 0;
        case CC_BIN_BXOR:
            *out = a ^ b;
            return 0;
        case CC_BIN_EQ:
            *out = (a == b) ? 1 : 0;
            return 0;
        case CC_BIN_NE:
            *out = (a != b) ? 1 : 0;
            return 0;
        case CC_BIN_LT:
            *out = (a < b) ? 1 : 0;
            return 0;
        case CC_BIN_LE:
            *out = (a <= b) ? 1 : 0;
            return 0;
        case CC_BIN_GT:
            *out = (a > b) ? 1 : 0;
            return 0;
        case CC_BIN_GE:
            *out = (a >= b) ? 1 : 0;
            return 0;
        case CC_BIN_LAND:
            *out = (a != 0 && b != 0) ? 1 : 0;
            return 0;
        case CC_BIN_LOR:
            *out = (a != 0 || b != 0) ? 1 : 0;
            return 0;
        case CC_BIN_COMMA:
            *out = b;
            return 0;
        default:
            return -1;
        }

    case CC_EXPR_CAST:
        if (e->aux_type == CC_TYPE_VOID) {
            return -1;
        }
        return eval_const_int_expr(e->lhs, out);

    case CC_EXPR_SIZEOF:
        if (e->lhs != NULL) {
            *out = type_size_bytes(e->lhs->value_type);
        } else {
            *out = type_size_bytes(e->aux_type);
        }
        return *out < 0 ? -1 : 0;

    case CC_EXPR_TERNARY:
        if (eval_const_int_expr(e->lhs, &a) != 0) {
            return -1;
        }
        if (a != 0) {
            return eval_const_int_expr(e->rhs, out);
        }
        return eval_const_int_expr(e->third, out);

    default:
        return -1;
    }
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
        if (e->op == CC_BIN_COMMA) {
            e->value_type = e->rhs->value_type;
            return 0;
        }
        if (is_logical_op(e->op)) {
            if (!is_numeric_type(e->lhs->value_type) || !is_numeric_type(e->rhs->value_type)) {
                set_diag(diag, "logical operators require numeric operands");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            return 0;
        }
        if (is_shift_op(e->op)) {
            if (!is_integral_type(e->lhs->value_type) || !is_integral_type(e->rhs->value_type)) {
                set_diag(diag, "shift operators require integer operands");
                return -1;
            }
            e->value_type = common_arith_type(e->lhs->value_type, e->rhs->value_type);
            if (e->value_type == CC_TYPE_VOID) {
                set_diag(diag, "void expression used in arithmetic operation");
                return -1;
            }
            return 0;
        }
        if (is_bitwise_op(e->op)) {
            if (!is_integral_type(e->lhs->value_type) || !is_integral_type(e->rhs->value_type)) {
                set_diag(diag, "bitwise operators require integer operands");
                return -1;
            }
            e->value_type = common_arith_type(e->lhs->value_type, e->rhs->value_type);
            if (e->value_type == CC_TYPE_VOID) {
                set_diag(diag, "void expression used in arithmetic operation");
                return -1;
            }
            return 0;
        }
        if (is_cmp_op(e->op)) {
            if (!is_numeric_type(e->lhs->value_type) || !is_numeric_type(e->rhs->value_type)) {
                set_diag(diag, "comparison operators require numeric operands");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            return 0;
        }
        if (e->op == CC_BIN_MOD) {
            if (!is_integral_type(e->lhs->value_type) || !is_integral_type(e->rhs->value_type)) {
                set_diag(diag, "modulo operator requires integer operands");
                return -1;
            }
            e->value_type = common_arith_type(e->lhs->value_type, e->rhs->value_type);
            if (e->value_type == CC_TYPE_VOID) {
                set_diag(diag, "void expression used in arithmetic operation");
                return -1;
            }
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

    case CC_EXPR_UPDATE: {
        int idx = vars_find_visible(vars, var_count, e->ident, depth);
        if (idx < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "update of undeclared identifier: %s",
                         e->ident ? e->ident : "<null>");
            }
            return -1;
        }
        if (!is_numeric_type(vars[idx].type)) {
            set_diag(diag, "++/-- currently require numeric scalar operands");
            return -1;
        }
        e->value_type = vars[idx].type;
        return 0;
    }

    case CC_EXPR_CAST:
        if (e->lhs == NULL) {
            set_diag(diag, "malformed cast expression");
            return -1;
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (e->aux_type == CC_TYPE_VOID) {
            e->value_type = CC_TYPE_VOID;
            return 0;
        }
        if (!is_numeric_type(e->lhs->value_type) || !is_numeric_type(e->aux_type)) {
            set_diag(diag, "cast currently supports numeric scalar types only");
            return -1;
        }
        e->value_type = e->aux_type;
        return 0;

    case CC_EXPR_SIZEOF:
        if (e->lhs != NULL) {
            if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (type_size_bytes(e->lhs->value_type) < 0) {
                set_diag(diag, "sizeof unsupported for this operand type");
                return -1;
            }
        } else {
            if (type_size_bytes(e->aux_type) < 0) {
                set_diag(diag, "sizeof unsupported for this type");
                return -1;
            }
        }
        e->value_type = CC_TYPE_INT;
        return 0;

    case CC_EXPR_TERNARY:
        if (e->lhs == NULL || e->rhs == NULL || e->third == NULL) {
            set_diag(diag, "malformed conditional expression");
            return -1;
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0 ||
            check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0 ||
            check_expr(tu, e->third, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (e->lhs->value_type == CC_TYPE_VOID) {
            set_diag(diag, "conditional expression condition cannot be void");
            return -1;
        }
        if (e->rhs->value_type == CC_TYPE_VOID || e->third->value_type == CC_TYPE_VOID) {
            set_diag(diag, "conditional expression arms cannot be void");
            return -1;
        }
        e->value_type = common_arith_type(e->rhs->value_type, e->third->value_type);
        if (e->value_type == CC_TYPE_VOID) {
            set_diag(diag, "incompatible types in conditional expression");
            return -1;
        }
        return 0;

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
        {
            size_t saved = *var_count;
            int for_depth = depth;

            if (s->init_stmt != NULL) {
                if (check_stmt(tu, s->init_stmt, vars, var_count, depth + 1, fn_ret_type, loop_depth, switch_depth,
                               saw_return, diag) != 0) {
                    return -1;
                }
                for_depth = depth + 1;
            } else if (s->init_expr != NULL && check_expr(tu, s->init_expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }

            if (s->expr != NULL) {
                if (check_expr(tu, s->expr, *vars, *var_count, for_depth, diag) != 0) {
                    return -1;
                }
                if (s->expr->value_type == CC_TYPE_VOID) {
                    set_diag(diag, "for condition cannot be void");
                    return -1;
                }
            }
            if (s->post_expr != NULL && check_expr(tu, s->post_expr, *vars, *var_count, for_depth, diag) != 0) {
                return -1;
            }
            if (check_stmt(tu, s->then_branch, vars, var_count, for_depth, fn_ret_type, loop_depth + 1, switch_depth,
                           saw_return, diag) != 0) {
                return -1;
            }
            for (i = saved; i < *var_count; ++i) {
                free((*vars)[i].name);
            }
            *var_count = saved;
            return 0;
        }

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
        if (!is_integral_type(s->expr->value_type)) {
            set_diag(diag, "switch expression must be integer");
            return -1;
        }
        {
            size_t i1;
            int seen_default = 0;
            for (i1 = 0; i1 < s->then_branch->block_count; ++i1) {
                const cc_stmt_t *ci = &s->then_branch->block_stmts[i1];
                long vi = 0;
                if (ci->kind == CC_STMT_DEFAULT) {
                    if (seen_default) {
                        set_diag(diag, "duplicate default label in switch");
                        return -1;
                    }
                    seen_default = 1;
                    continue;
                }
                if (ci->kind != CC_STMT_CASE) {
                    continue;
                }
                if (eval_const_int_expr(ci->expr, &vi) != 0) {
                    set_diag(diag, "case label must be an integer constant expression");
                    return -1;
                }
                {
                    size_t i2;
                    for (i2 = i1 + 1; i2 < s->then_branch->block_count; ++i2) {
                        const cc_stmt_t *cj = &s->then_branch->block_stmts[i2];
                        long vj = 0;
                        if (cj->kind != CC_STMT_CASE) {
                            continue;
                        }
                        if (eval_const_int_expr(cj->expr, &vj) == 0 && vj == vi) {
                            set_diag(diag, "duplicate case value in switch");
                            return -1;
                        }
                    }
                }
            }
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
        if (eval_const_int_expr(s->expr, &s->expr->int_val) != 0) {
            set_diag(diag, "case label must be an integer constant expression");
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

    case CC_STMT_GOTO:
        return 0;

    case CC_STMT_LABEL:
        if (s->then_branch == NULL) {
            set_diag(diag, "malformed labeled statement");
            return -1;
        }
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, loop_depth, switch_depth,
                          saw_return, diag);

    default:
        set_diag(diag, "unsupported statement kind");
        return -1;
    }
}

int cc_sema_check(const cc_translation_unit_t *tu, cc_diag_t *diag) {
    size_t i;

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
        size_t j;

        if (f->name == NULL || f->name[0] == '\0') {
            set_diag(diag, "function with missing name");
            goto fail_global;
        }
        for (j = 0; j < i; ++j) {
            const cc_function_t *prev = &tu->funcs[j];
            if (strcmp(prev->name, f->name) != 0) {
                continue;
            }
            if (!func_decl_compatible(prev, f)) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "conflicting declarations for function: %s",
                             f->name);
                }
                goto fail_global;
            }
            if (prev->has_body && f->has_body) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "duplicate function definition: %s", f->name);
                }
                goto fail_global;
            }
        }
    }

    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *f = &tu->funcs[i];
        var_entry_t *vars = NULL;
        name_list_t labels = {0};
        name_list_t gotos = {0};
        size_t var_count = 0;
        size_t j;
        int saw_return = 0;

        if (!f->has_body) {
            continue;
        }

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

        for (j = 0; j < f->stmt_count; ++j) {
            if (collect_labels_gotos_stmt(&f->stmts[j], &labels, &gotos, diag) != 0) {
                goto fail_func;
            }
        }
        for (j = 0; j < gotos.count; ++j) {
            if (names_find(labels.items, labels.count, gotos.items[j]) < 0) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "goto to unknown label: %s", gotos.items[j]);
                }
                goto fail_func;
            }
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
        name_list_free(&labels);
        name_list_free(&gotos);
        continue;

fail_func:
        for (j = 0; j < var_count; ++j) {
            free(vars[j].name);
        }
        free(vars);
        name_list_free(&labels);
        name_list_free(&gotos);
        goto fail_global;
    }

    return 0;

fail_global:
    return -1;
}
