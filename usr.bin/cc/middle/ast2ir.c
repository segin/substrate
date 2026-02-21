#include "cc_frontend.h"
#include "cc_ssa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    cc_type_t type;
    int value;
    int depth;
} var_entry_t;

static char *xstrdup(const char *s) {
    size_t n;
    char *p;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static void set_diag(cc_diag_t *d, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    d->line = 0;
    d->col = 0;
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

static cc_value_type_t type_to_val(cc_type_t t) {
    return t == CC_TYPE_DOUBLE ? CC_VAL_F64 : CC_VAL_I64;
}

static int is_cmp_op(cc_binop_t op) {
    return op == CC_BIN_EQ || op == CC_BIN_NE || op == CC_BIN_LT || op == CC_BIN_LE || op == CC_BIN_GT ||
           op == CC_BIN_GE;
}

static cc_cmp_kind_t bin_to_cmp(cc_binop_t op) {
    switch (op) {
    case CC_BIN_EQ:
        return CC_CMP_EQ;
    case CC_BIN_NE:
        return CC_CMP_NE;
    case CC_BIN_LT:
        return CC_CMP_LT;
    case CC_BIN_LE:
        return CC_CMP_LE;
    case CC_BIN_GT:
        return CC_CMP_GT;
    case CC_BIN_GE:
        return CC_CMP_GE;
    default:
        return CC_CMP_EQ;
    }
}

static int push_instr(cc_ssa_function_t *f, cc_ssa_instr_t in) {
    if (f->instr_count == f->instr_cap) {
        size_t ncap = f->instr_cap == 0 ? 16 : f->instr_cap * 2;
        cc_ssa_instr_t *next = (cc_ssa_instr_t *)realloc(f->instrs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        f->instrs = next;
        f->instr_cap = ncap;
    }
    f->instrs[f->instr_count++] = in;
    return 0;
}

static int new_value(cc_ssa_function_t *f, cc_value_type_t vt) {
    int id = f->value_count;
    f->value_count++;

    if ((size_t)f->value_count > f->value_cap) {
        size_t ncap = f->value_cap == 0 ? 32 : f->value_cap * 2;
        while (ncap < (size_t)f->value_count) {
            ncap *= 2;
        }
        cc_value_type_t *next = (cc_value_type_t *)realloc(f->value_types, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        f->value_types = next;
        f->value_cap = ncap;
    }

    f->value_types[id] = vt;
    return id;
}

static int new_label(cc_ssa_function_t *f) {
    return f->label_count++;
}

static cc_value_type_t value_type(const cc_ssa_function_t *f, int v) {
    if (v < 0 || (size_t)v >= (size_t)f->value_count) {
        return CC_VAL_I64;
    }
    return f->value_types[v];
}

static int var_find_visible(var_entry_t *vars, size_t var_count, const char *name, int depth) {
    size_t i = var_count;
    while (i > 0) {
        i--;
        if (vars[i].depth <= depth && strcmp(vars[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int var_define(var_entry_t **vars, size_t *var_count, const char *name, cc_type_t type, int value, int depth) {
    var_entry_t *next;
    char *dup;

    next = (var_entry_t *)realloc(*vars, (*var_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *vars = next;

    dup = xstrdup(name);
    if (dup == NULL) {
        return -1;
    }
    (*vars)[*var_count].name = dup;
    (*vars)[*var_count].type = type;
    (*vars)[*var_count].value = value;
    (*vars)[*var_count].depth = depth;
    (*var_count)++;
    return 0;
}

static int var_set(var_entry_t *vars, size_t var_count, const char *name, int depth, int value) {
    int idx = var_find_visible(vars, var_count, name, depth);
    if (idx < 0) {
        return -1;
    }
    vars[idx].value = value;
    return 0;
}

static const cc_function_t *find_fn(const cc_translation_unit_t *tu, const char *name) {
    size_t i;
    for (i = 0; i < tu->func_count; ++i) {
        if (strcmp(tu->funcs[i].name, name) == 0) {
            return &tu->funcs[i];
        }
    }
    return NULL;
}

static int cast_value(cc_ssa_function_t *sf, int v, cc_value_type_t dst, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    cc_value_type_t src = value_type(sf, v);

    if (src == dst) {
        return v;
    }

    memset(&in, 0, sizeof(in));
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.param_index = -1;

    if (src == CC_VAL_I64 && dst == CC_VAL_F64) {
        in.op = CC_SSA_I2F;
    } else if (src == CC_VAL_F64 && dst == CC_VAL_I64) {
        in.op = CC_SSA_F2I;
    } else {
        set_diag(diag, "unsupported cast in lowering");
        return -1;
    }

    in.lhs = v;
    in.dst = new_value(sf, dst);
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        return -1;
    }
    return in.dst;
}

static int lower_expr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t *vars, size_t var_count,
                      int depth, const cc_expr_t *e, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int lhs;
    int rhs;
    size_t i;

    if (e == NULL) {
        set_diag(diag, "null expression during AST->SSA lowering");
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.param_index = -1;
    in.label = -1;
    in.true_label = -1;
    in.false_label = -1;

    switch (e->kind) {
    case CC_EXPR_INT:
        in.op = CC_SSA_CONST;
        in.dst = new_value(sf, CC_VAL_I64);
        in.imm = e->int_val;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;

    case CC_EXPR_FLOAT:
        in.op = CC_SSA_CONST;
        in.dst = new_value(sf, CC_VAL_F64);
        in.fimm = e->float_val;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;

    case CC_EXPR_IDENT: {
        int idx = var_find_visible(vars, var_count, e->ident, depth);
        if (idx < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "unknown identifier during AST->SSA lowering: %s",
                         e->ident);
            }
            return -1;
        }
        return vars[idx].value;
    }

    case CC_EXPR_BIN: {
        cc_value_type_t vt;
        lhs = lower_expr(tu, sf, vars, var_count, depth, e->lhs, diag);
        if (lhs < 0) {
            return -1;
        }
        rhs = lower_expr(tu, sf, vars, var_count, depth, e->rhs, diag);
        if (rhs < 0) {
            return -1;
        }

        if (is_cmp_op(e->op)) {
            lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
            rhs = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            in.op = CC_SSA_CMP;
            in.cmp_kind = bin_to_cmp(e->op);
            in.dst = new_value(sf, CC_VAL_I64);
            in.lhs = lhs;
            in.rhs = rhs;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            return in.dst;
        }

        vt = type_to_val(e->value_type);
        lhs = cast_value(sf, lhs, vt, diag);
        rhs = cast_value(sf, rhs, vt, diag);
        if (lhs < 0 || rhs < 0) {
            return -1;
        }

        switch (e->op) {
        case CC_BIN_ADD:
            in.op = CC_SSA_ADD;
            break;
        case CC_BIN_SUB:
            in.op = CC_SSA_SUB;
            break;
        case CC_BIN_MUL:
            in.op = CC_SSA_MUL;
            break;
        case CC_BIN_DIV:
            in.op = CC_SSA_DIV;
            break;
        default:
            set_diag(diag, "unsupported binary operation");
            return -1;
        }
        in.dst = new_value(sf, vt);
        in.lhs = lhs;
        in.rhs = rhs;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_CALL: {
        const cc_function_t *callee = find_fn(tu, e->ident);
        in.op = CC_SSA_CALL;
        in.call_is_variadic = callee != NULL ? callee->is_variadic : 0;
        in.sym = xstrdup(e->ident);
        if (in.sym == NULL) {
            return -1;
        }
        in.arg_count = e->arg_count;
        if (in.arg_count > 0) {
            in.args = (int *)calloc(in.arg_count, sizeof(*in.args));
            if (in.args == NULL) {
                free(in.sym);
                return -1;
            }
        }

        for (i = 0; i < in.arg_count; ++i) {
            cc_value_type_t want;
            int av = lower_expr(tu, sf, vars, var_count, depth, e->args[i], diag);
            if (av < 0) {
                free(in.sym);
                free(in.args);
                return -1;
            }
            want = value_type(sf, av);
            if (callee != NULL && i < callee->param_count) {
                want = type_to_val(callee->params[i].type);
                av = cast_value(sf, av, want, diag);
                if (av < 0) {
                    free(in.sym);
                    free(in.args);
                    return -1;
                }
            }
            in.args[i] = av;
        }

        if (e->value_type == CC_TYPE_VOID) {
            in.dst = new_value(sf, CC_VAL_I64);
        } else {
            in.dst = new_value(sf, type_to_val(e->value_type));
        }
        if (in.dst < 0) {
            free(in.sym);
            free(in.args);
            return -1;
        }

        if (push_instr(sf, in) != 0) {
            free(in.sym);
            free(in.args);
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_ASSIGN: {
        int idx = var_find_visible(vars, var_count, e->ident, depth);
        cc_value_type_t want;
        if (idx < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "assignment to unknown identifier during AST->SSA lowering: %s", e->ident);
            }
            return -1;
        }
        rhs = lower_expr(tu, sf, vars, var_count, depth, e->rhs, diag);
        if (rhs < 0) {
            return -1;
        }
        want = type_to_val(vars[idx].type);
        rhs = cast_value(sf, rhs, want, diag);
        if (rhs < 0) {
            return -1;
        }
        if (var_set(vars, var_count, e->ident, depth, rhs) != 0) {
            return -1;
        }
        return rhs;
    }

    default:
        set_diag(diag, "unsupported expression kind in AST->SSA lowering");
        return -1;
    }
}

static int emit_label_instr(cc_ssa_function_t *sf, int label) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LABEL;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = label;
    return push_instr(sf, in);
}

static int emit_br_instr(cc_ssa_function_t *sf, int label) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_BR;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = label;
    return push_instr(sf, in);
}

static int emit_br_cond_instr(cc_ssa_function_t *sf, int cond, int label_true, int label_false) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_BR_COND;
    in.dst = -1;
    in.lhs = cond;
    in.rhs = -1;
    in.true_label = label_true;
    in.false_label = label_false;
    return push_instr(sf, in);
}

static int lower_stmt(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t **vars, size_t *var_count,
                      int depth, const cc_stmt_t *s, int *saw_ret, cc_diag_t *diag) {
    size_t j;

    if (s->kind == CC_STMT_DECL) {
        int v;
        if (s->expr != NULL) {
            v = lower_expr(tu, sf, *vars, *var_count, depth, s->expr, diag);
            if (v < 0) {
                return -1;
            }
            v = cast_value(sf, v, type_to_val(s->type), diag);
            if (v < 0) {
                return -1;
            }
        } else {
            cc_ssa_instr_t in;
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_CONST;
            in.dst = new_value(sf, type_to_val(s->type));
            in.lhs = -1;
            in.rhs = -1;
            in.imm = 0;
            in.fimm = 0.0;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending declaration default const");
                return -1;
            }
            v = in.dst;
        }
        if (var_define(vars, var_count, s->decl_name, s->type, v, depth) != 0) {
            set_diag(diag, "out of memory defining local variable");
            return -1;
        }
        return 0;
    }

    if (s->kind == CC_STMT_EXPR) {
        if (s->expr != NULL && lower_expr(tu, sf, *vars, *var_count, depth, s->expr, diag) < 0) {
            return -1;
        }
        return 0;
    }

    if (s->kind == CC_STMT_RETURN) {
        cc_ssa_instr_t ret_in;
        int rv = -1;

        if (s->expr != NULL) {
            rv = lower_expr(tu, sf, *vars, *var_count, depth, s->expr, diag);
            if (rv < 0) {
                return -1;
            }
            rv = cast_value(sf, rv, sf->ret_type, diag);
            if (rv < 0) {
                return -1;
            }
        }

        memset(&ret_in, 0, sizeof(ret_in));
        ret_in.op = CC_SSA_RET;
        ret_in.dst = -1;
        ret_in.lhs = rv;
        ret_in.rhs = -1;
        ret_in.param_index = -1;
        if (push_instr(sf, ret_in) != 0) {
            set_diag(diag, "out of memory appending SSA return instruction");
            return -1;
        }
        *saw_ret = 1;
        return 0;
    }

    if (s->kind == CC_STMT_IF) {
        int cond;
        int l_then = new_label(sf);
        int l_else = new_label(sf);
        int l_end = new_label(sf);

        cond = lower_expr(tu, sf, *vars, *var_count, depth, s->expr, diag);
        if (cond < 0) {
            return -1;
        }
        cond = cast_value(sf, cond, CC_VAL_I64, diag);
        if (cond < 0) {
            return -1;
        }

        if (emit_br_cond_instr(sf, cond, l_then, s->else_branch != NULL ? l_else : l_end) != 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_then) != 0) {
            return -1;
        }
        {
            size_t saved = *var_count;
            if (lower_stmt(tu, sf, vars, var_count, depth, s->then_branch, saw_ret, diag) != 0) {
                return -1;
            }
            while (*var_count > saved) {
                (*var_count)--;
                free((*vars)[*var_count].name);
            }
        }
        if (emit_br_instr(sf, l_end) != 0) {
            return -1;
        }

        if (s->else_branch != NULL) {
            if (emit_label_instr(sf, l_else) != 0) {
                return -1;
            }
            {
                size_t saved = *var_count;
                if (lower_stmt(tu, sf, vars, var_count, depth, s->else_branch, saw_ret, diag) != 0) {
                    return -1;
                }
                while (*var_count > saved) {
                    (*var_count)--;
                    free((*vars)[*var_count].name);
                }
            }
            if (emit_br_instr(sf, l_end) != 0) {
                return -1;
            }
        }

        if (emit_label_instr(sf, l_end) != 0) {
            return -1;
        }
        return 0;
    }

    if (s->kind == CC_STMT_BLOCK) {
        size_t saved = *var_count;
        for (j = 0; j < s->block_count; ++j) {
            if (lower_stmt(tu, sf, vars, var_count, depth + 1, &s->block_stmts[j], saw_ret, diag) != 0) {
                return -1;
            }
        }
        while (*var_count > saved) {
            (*var_count)--;
            free((*vars)[*var_count].name);
        }
        return 0;
    }

    set_diag(diag, "unsupported statement kind during AST->SSA lowering");
    return -1;
}

static int append_default_return(cc_ssa_function_t *sf) {
    cc_ssa_instr_t ret_in;

    if (sf->ret_type == CC_VAL_F64) {
        cc_ssa_instr_t c;
        memset(&c, 0, sizeof(c));
        c.op = CC_SSA_CONST;
        c.dst = new_value(sf, CC_VAL_F64);
        c.fimm = 0.0;
        if (c.dst < 0 || push_instr(sf, c) != 0) {
            return -1;
        }
        memset(&ret_in, 0, sizeof(ret_in));
        ret_in.op = CC_SSA_RET;
        ret_in.lhs = c.dst;
        ret_in.dst = -1;
        return push_instr(sf, ret_in);
    }

    if (sf->ret_type == CC_VAL_I64) {
        cc_ssa_instr_t c;
        memset(&c, 0, sizeof(c));
        c.op = CC_SSA_CONST;
        c.dst = new_value(sf, CC_VAL_I64);
        c.imm = 0;
        if (c.dst < 0 || push_instr(sf, c) != 0) {
            return -1;
        }
        memset(&ret_in, 0, sizeof(ret_in));
        ret_in.op = CC_SSA_RET;
        ret_in.lhs = c.dst;
        ret_in.dst = -1;
        return push_instr(sf, ret_in);
    }

    memset(&ret_in, 0, sizeof(ret_in));
    ret_in.op = CC_SSA_RET;
    ret_in.lhs = -1;
    ret_in.dst = -1;
    return push_instr(sf, ret_in);
}

int cc_ast_to_ssa(const cc_translation_unit_t *tu, cc_ssa_module_t *out, cc_diag_t *diag) {
    size_t i;

    cc_ssa_module_init(out);
    if (diag != NULL) {
        diag->line = 0;
        diag->col = 0;
        diag->message[0] = '\0';
    }

    out->funcs = (cc_ssa_function_t *)calloc(tu->func_count, sizeof(*out->funcs));
    if (out->funcs == NULL) {
        set_diag(diag, "out of memory allocating SSA functions");
        return -1;
    }
    out->func_count = tu->func_count;

    for (i = 0; i < tu->func_count; ++i) {
        cc_ssa_function_t *sf = &out->funcs[i];
        const cc_function_t *af = &tu->funcs[i];
        var_entry_t *vars = NULL;
        size_t var_count = 0;
        size_t j;
        int saw_ret = 0;

        sf->name = xstrdup(af->name);
        if (sf->name == NULL) {
            set_diag(diag, "out of memory duplicating function name");
            cc_ssa_module_free(out);
            return -1;
        }
        sf->ret_type = af->ret_type == CC_TYPE_DOUBLE ? CC_VAL_F64 : CC_VAL_I64;
        sf->is_variadic = af->is_variadic;
        sf->param_count = af->param_count;
        sf->param_values = (int *)calloc(af->param_count == 0 ? 1 : af->param_count, sizeof(*sf->param_values));
        sf->param_types =
            (cc_value_type_t *)calloc(af->param_count == 0 ? 1 : af->param_count, sizeof(*sf->param_types));
        if (sf->param_values == NULL || sf->param_types == NULL) {
            set_diag(diag, "out of memory allocating parameter map");
            cc_ssa_module_free(out);
            return -1;
        }

        for (j = 0; j < af->param_count; ++j) {
            cc_ssa_instr_t in;
            int v;

            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_PARAM;
            in.dst = new_value(sf, type_to_val(af->params[j].type));
            in.lhs = -1;
            in.rhs = -1;
            in.param_index = (int)j;
            if (in.dst < 0) {
                set_diag(diag, "out of memory assigning param SSA value");
                cc_ssa_module_free(out);
                return -1;
            }
            sf->param_values[j] = in.dst;
            sf->param_types[j] = type_to_val(af->params[j].type);
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending SSA parameter instruction");
                cc_ssa_module_free(out);
                return -1;
            }

            v = in.dst;
            if (var_define(&vars, &var_count, af->params[j].name, af->params[j].type, v, 0) != 0) {
                set_diag(diag, "out of memory defining parameter variable");
                cc_ssa_module_free(out);
                return -1;
            }
        }

        for (j = 0; j < af->stmt_count; ++j) {
            if (lower_stmt(tu, sf, &vars, &var_count, 0, &af->stmts[j], &saw_ret, diag) != 0) {
                cc_ssa_module_free(out);
                return -1;
            }
        }

        if (sf->instr_count == 0 || sf->instrs[sf->instr_count - 1].op != CC_SSA_RET) {
            if (append_default_return(sf) != 0) {
                set_diag(diag, "out of memory appending default return");
                cc_ssa_module_free(out);
                return -1;
            }
        }

        for (j = 0; j < var_count; ++j) {
            free(vars[j].name);
        }
        free(vars);
    }

    return 0;
}
