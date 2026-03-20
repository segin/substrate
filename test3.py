with open("usr.bin/cc/frontend/parser.c", "r") as f:
    text = f.read()
import re

# 1. Update push_stmt_arr signature and impl
text = text.replace(
"""static int push_stmt_arr(cc_stmt_t **arr, size_t *count, cc_stmt_t s) {
    cc_stmt_t *next = (cc_stmt_t *)realloc(*arr, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *arr = next;
    (*arr)[(*count)++] = s;
    return 0;
}""",
"""static int push_stmt_arr(cc_stmt_t **arr, size_t *count, size_t *cap, cc_stmt_t s) {
    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 8 : *cap * 2;
        cc_stmt_t *next = (cc_stmt_t *)realloc(*arr, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *arr = next;
        *cap = ncap;
    }
    (*arr)[(*count)++] = s;
    return 0;
}"""
)

# 2. Update push_stmt_func
text = text.replace(
"""static int push_stmt_func(cc_function_t *f, cc_stmt_t s) {
    return push_stmt_arr(&f->stmts, &f->stmt_count, s);
}""",
"""static int push_stmt_func(cc_function_t *f, cc_stmt_t s) {
    return push_stmt_arr(&f->stmts, &f->stmt_count, &f->stmt_cap, s);
}"""
)

# 3. Update parse_decl_stmt_list
text = text.replace(
"""static int parse_decl_stmt_list(parser_t *p, cc_stmt_t **arr, size_t *count, int need_semi) {""",
"""static int parse_decl_stmt_list(parser_t *p, cc_stmt_t **arr, size_t *count, size_t *cap, int need_semi) {"""
)
text = text.replace(
"""                if (push_stmt_arr(arr, count, s) != 0) {""",
"""                if (push_stmt_arr(arr, count, cap, s) != 0) {"""
)

# 4. Find and replace calls to parse_decl_stmt_list and push_stmt_arr
# Replace inside `parse_block_stmt` (which uses `s->block_stmts` and `s->block_count`)
text = text.replace("push_stmt_arr(&s->block_stmts, &s->block_count, child)", "push_stmt_arr(&s->block_stmts, &s->block_count, &s->block_cap, child)")
text = text.replace("push_stmt_arr(&s->block_stmts, &s->block_count, label_stmt)", "push_stmt_arr(&s->block_stmts, &s->block_count, &s->block_cap, label_stmt)")
text = text.replace("push_stmt_arr(&s->block_stmts, &s->block_count, tail_stmt)", "push_stmt_arr(&s->block_stmts, &s->block_count, &s->block_cap, tail_stmt)")

# In parse_stmt:
text = text.replace("parse_decl_stmt_list(p, &s->init_stmt->block_stmts, &s->init_stmt->block_count, 1)", "parse_decl_stmt_list(p, &s->init_stmt->block_stmts, &s->init_stmt->block_count, &s->init_stmt->block_cap, 1)")

# There is a decls list in parse_stmt:
text = re.sub(
r"        cc_stmt_t \*decls = NULL;\n        size_t decl_count = 0;\n        size_t stmt_line = p->tok\.line;",
r"        cc_stmt_t *decls = NULL;\n        size_t decl_count = 0;\n        size_t decl_cap = 0;\n        size_t stmt_line = p->tok.line;",
text)
text = text.replace("parse_decl_stmt_list(p, &decls, &decl_count, 1)", "parse_decl_stmt_list(p, &decls, &decl_count, &decl_cap, 1)")

# In parse_function for oldstyle_decls:
text = re.sub(
r"    cc_stmt_t \*oldstyle_decls = NULL;\n    size_t oldstyle_decl_count = 0;\n    size_t oldstyle_i;",
r"    cc_stmt_t *oldstyle_decls = NULL;\n    size_t oldstyle_decl_count = 0;\n    size_t oldstyle_decl_cap = 0;\n    size_t oldstyle_i;",
text)
text = text.replace("parse_decl_stmt_list(p, &oldstyle_decls, &oldstyle_decl_count, 1)", "parse_decl_stmt_list(p, &oldstyle_decls, &oldstyle_decl_count, &oldstyle_decl_cap, 1)")
text = text.replace("parse_decl_stmt_list(p, &f->stmts, &f->stmt_count, 1)", "parse_decl_stmt_list(p, &f->stmts, &f->stmt_count, &f->stmt_cap, 1)")

# In cc_parse_file:
text = re.sub(
r"            cc_stmt_t \*decls = NULL;\n            size_t decl_count = 0;\n            size_t di;",
r"            cc_stmt_t *decls = NULL;\n            size_t decl_count = 0;\n            size_t decl_cap = 0;\n            size_t di;",
text)
text = text.replace("parse_decl_stmt_list(&p, &decls, &decl_count, 1)", "parse_decl_stmt_list(&p, &decls, &decl_count, &decl_cap, 1)")

# Replace other reallocations: tu_push_function, push_global, push_arg, push_param, push_asm_operand, push_string_item
# I'll just use the already working python scripts or manual logic for these.

# tu_push_function
tu_push_function_old = """static int tu_push_function(cc_translation_unit_t *tu, const cc_function_t *f) {
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
}"""
tu_push_function_new = """static int tu_push_function(cc_translation_unit_t *tu, const cc_function_t *f) {
    cc_function_t *next;
    if (tu == NULL || f == NULL) {
        return -1;
    }
    if (tu->func_count == tu->func_cap) {
        size_t ncap = tu->func_cap == 0 ? 16 : tu->func_cap * 2;
        next = (cc_function_t *)realloc(tu->funcs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        tu->funcs = next;
        tu->func_cap = ncap;
    }
    tu->funcs[tu->func_count++] = *f;
    return 0;
}"""
text = text.replace(tu_push_function_old, tu_push_function_new)

# push_global
push_global_old = """static int push_global(cc_translation_unit_t *tu, cc_global_t g) {
    if (tu->global_count > 0 && tu->globals[tu->global_count - 1].name != NULL && g.name != NULL &&
        strcmp(tu->globals[tu->global_count - 1].name, g.name) == 0) {
        if (g.init != NULL && tu->globals[tu->global_count - 1].init == NULL) {
            tu->globals[tu->global_count - 1].init = g.init;
            g.init = NULL;
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
}"""
push_global_new = """static int push_global(cc_translation_unit_t *tu, cc_global_t g) {
    if (tu->global_count > 0 && tu->globals[tu->global_count - 1].name != NULL && g.name != NULL &&
        strcmp(tu->globals[tu->global_count - 1].name, g.name) == 0) {
        if (g.init != NULL && tu->globals[tu->global_count - 1].init == NULL) {
            tu->globals[tu->global_count - 1].init = g.init;
            g.init = NULL;
        }
        free_global_decl(&g);
        return 0;
    }

    if (tu->global_count == tu->global_cap) {
        size_t ncap = tu->global_cap == 0 ? 32 : tu->global_cap * 2;
        cc_global_t *next = (cc_global_t *)realloc(tu->globals, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        tu->globals = next;
        tu->global_cap = ncap;
    }
    tu->globals[tu->global_count++] = g;
    return 0;
}"""
text = text.replace(push_global_old, push_global_new)

# push_arg
push_arg_old = """static int push_arg(cc_expr_t *call, cc_expr_t *arg) {
    cc_expr_t **next = (cc_expr_t **)realloc(call->args, (call->arg_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    call->args = next;
    call->args[call->arg_count++] = arg;
    return 0;
}"""
push_arg_new = """static int push_arg(cc_expr_t *call, cc_expr_t *arg) {
    if (call->arg_count == call->arg_cap) {
        size_t ncap = call->arg_cap == 0 ? 8 : call->arg_cap * 2;
        cc_expr_t **next = (cc_expr_t **)realloc(call->args, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        call->args = next;
        call->arg_cap = ncap;
    }
    call->args[call->arg_count++] = arg;
    return 0;
}"""
text = text.replace(push_arg_old, push_arg_new)

# push_param
push_param_old = """static int push_param(cc_function_t *f, cc_type_t type, const char *name, size_t n, long array_len, int array_ndim,
                      const long array_dims[CC_MAX_ARRAY_DIMS], int storage) {
    cc_param_t *next = (cc_param_t *)realloc(f->params, (f->param_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    f->params = next;
    f->params[f->param_count].name = xstrdup_n(name, n);"""
push_param_new = """static int push_param(cc_function_t *f, cc_type_t type, const char *name, size_t n, long array_len, int array_ndim,
                      const long array_dims[CC_MAX_ARRAY_DIMS], int storage) {
    if (f->param_count == f->param_cap) {
        size_t ncap = f->param_cap == 0 ? 8 : f->param_cap * 2;
        cc_param_t *next = (cc_param_t *)realloc(f->params, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        f->params = next;
        f->param_cap = ncap;
    }
    f->params[f->param_count].name = xstrdup_n(name, n);"""
text = text.replace(push_param_old, push_param_new)

with open("usr.bin/cc/frontend/parser.c", "w") as f:
    f.write(text)
