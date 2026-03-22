import re

with open("usr.bin/cc/frontend/parser.c", "r") as f:
    text = f.read()

# 1. tu_push_function
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

# 2. push_global
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

# Actually let's search for push_global with regex to replace it
import re

push_global_pattern = re.compile(r"static int push_global\(cc_translation_unit_t \*tu, cc_global_t g\) \{.*?\n    cc_global_t \*next = \(cc_global_t \*\)realloc\(tu->globals, \(tu->global_count \+ 1\) \* sizeof\(\*next\)\);\n    if \(next == NULL\) \{\n        return -1;\n    \}\n    tu->globals = next;\n    tu->globals\[tu->global_count\+\+\] = g;\n    return 0;\n\}", re.DOTALL)

push_global_replacement = """static int push_global(cc_translation_unit_t *tu, cc_global_t g) {
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

text = push_global_pattern.sub(push_global_replacement, text)

# 3. push_arg
push_arg_pattern = re.compile(r"static int push_arg\(cc_expr_t \*call, cc_expr_t \*arg\) \{\n    cc_expr_t \*\*next = \(cc_expr_t \*\*\)realloc\(call->args, \(call->arg_count \+ 1\) \* sizeof\(\*next\)\);\n    if \(next == NULL\) \{\n        return -1;\n    \}\n    call->args = next;\n    call->args\[call->arg_count\+\+\] = arg;\n    return 0;\n\}")

push_arg_replacement = """static int push_arg(cc_expr_t *call, cc_expr_t *arg) {
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

text = push_arg_pattern.sub(push_arg_replacement, text)

# 4. push_param
push_param_pattern = re.compile(r"static int push_param\(cc_function_t \*f, cc_type_t type, const char \*name, size_t n, long array_len, int array_ndim,\n                      const long array_dims\[CC_MAX_ARRAY_DIMS\], int storage\) \{\n    cc_param_t \*next = \(cc_param_t \*\)realloc\(f->params, \(f->param_count \+ 1\) \* sizeof\(\*next\)\);\n    if \(next == NULL\) \{\n        return -1;\n    \}\n    f->params = next;\n    f->params\[f->param_count\]\.name = xstrdup_n\(name, n\);")

push_param_replacement = """static int push_param(cc_function_t *f, cc_type_t type, const char *name, size_t n, long array_len, int array_ndim,
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

text = push_param_pattern.sub(push_param_replacement, text)

# 5. push_stmt_arr
push_stmt_arr_pattern = re.compile(r"static int push_stmt_arr\(cc_stmt_t \*\*arr, size_t \*count, cc_stmt_t s\) \{\n    cc_stmt_t \*next = \(cc_stmt_t \*\)realloc\(\*arr, \(\*count \+ 1\) \* sizeof\(\*next\)\);\n    if \(next == NULL\) \{\n        return -1;\n    \}\n    \*arr = next;\n    \(\*arr\)\[\(\*count\)\+\+\] = s;\n    return 0;\n\}")

# Wait, `push_stmt_arr` takes a pointer to `arr` and `count`, so it cannot easily update `cap` unless we pass `cap` as a pointer. Let's look at `push_stmt_arr`.
