#include "cc_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cc_parser_set_pointer_size(int bytes);
void cc_parser_set_std_mode(const char *std_mode);

static int g_pointer_size_bytes = 8;
static int g_allow_implicit_funcdecl = 0;

static int std_mode_allows_implicit_function_decls(const char *std_mode) {
    if (std_mode == NULL || std_mode[0] == '\0') {
        return 0;
    }
    if (strcmp(std_mode, "c89") == 0 || strcmp(std_mode, "c90") == 0 || strcmp(std_mode, "c95") == 0 ||
        strcmp(std_mode, "gnu89") == 0 || strcmp(std_mode, "gnu90") == 0 || strcmp(std_mode, "gnu95") == 0) {
        return 1;
    }
    return 0;
}

typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
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

static int is_power_of_two_long(long v) {
    return v > 0 && (v & (v - 1)) == 0;
}

static int validate_attr_align(long align, cc_diag_t *diag, const char *what) {
    if (is_power_of_two_long(align)) {
        return 0;
    }
    if (diag != NULL && diag->message[0] == '\0') {
        snprintf(diag->message, sizeof(diag->message), "%s requires a positive power-of-two value", what);
    }
    return -1;
}

static int validate_attr_section(const char *section, cc_diag_t *diag, const char *what) {
    if (section != NULL && section[0] != '\0' && strchr(section, '\n') == NULL && strchr(section, '\r') == NULL) {
        return 0;
    }
    if (diag != NULL && diag->message[0] == '\0') {
        snprintf(diag->message, sizeof(diag->message), "%s requires a valid section name", what);
    }
    return -1;
}

static int storage_class_count(int storage) {
    int mask = storage & (CC_STORAGE_STATIC | CC_STORAGE_EXTERN | CC_STORAGE_AUTO | CC_STORAGE_REGISTER);
    int n = 0;
    if ((mask & CC_STORAGE_STATIC) != 0) {
        n++;
    }
    if ((mask & CC_STORAGE_EXTERN) != 0) {
        n++;
    }
    if ((mask & CC_STORAGE_AUTO) != 0) {
        n++;
    }
    if ((mask & CC_STORAGE_REGISTER) != 0) {
        n++;
    }
    return n;
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

static int vars_push(var_entry_t **vars, size_t *count, const char *name, cc_type_t type, int struct_id, int depth) {
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
    (*vars)[*count].struct_id = struct_id;
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
        if (s->expr != NULL) {
            return 0;
        }
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

static const cc_global_t *find_global(const cc_translation_unit_t *tu, const char *name) {
    size_t i;
    if (tu == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < tu->global_count; ++i) {
        if (strcmp(tu->globals[i].name, name) == 0) {
            return &tu->globals[i];
        }
    }
    return NULL;
}

static const cc_struct_def_t *find_struct_def(const cc_translation_unit_t *tu, int struct_id) {
    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        return NULL;
    }
    return &tu->structs[struct_id];
}

static const cc_struct_member_t *find_struct_member(const cc_translation_unit_t *tu, int struct_id, const char *name) {
    const cc_struct_def_t *sd;
    size_t i;
    sd = find_struct_def(tu, struct_id);
    if (sd == NULL || !sd->complete || name == NULL) {
        return NULL;
    }
    for (i = 0; i < sd->member_count; ++i) {
        if (strcmp(sd->members[i].name, name) == 0) {
            return &sd->members[i];
        }
    }
    return NULL;
}

static int find_struct_member_index(const cc_translation_unit_t *tu, int struct_id, const char *name) {
    const cc_struct_def_t *sd;
    size_t i;

    sd = find_struct_def(tu, struct_id);
    if (sd == NULL || !sd->complete || name == NULL) {
        return -1;
    }
    for (i = 0; i < sd->member_count; ++i) {
        if (strcmp(sd->members[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int func_decl_compatible(const cc_function_t *a, const cc_function_t *b) {
    size_t i;
    if (a->ret_type != b->ret_type || a->ret_struct_id != b->ret_struct_id || a->is_variadic != b->is_variadic ||
        a->param_count != b->param_count) {
        return 0;
    }
    for (i = 0; i < a->param_count; ++i) {
        if (a->params[i].type != b->params[i].type || a->params[i].type_struct_id != b->params[i].type_struct_id) {
            return 0;
        }
    }
    return 1;
}

static int is_float_type(cc_type_t t) {
    return t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE;
}

static int is_pointer_type(cc_type_t t) {
    return t >= CC_TYPE_PTR_VOID && t <= CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE;
}

static int is_unsigned_integral_type(cc_type_t t) {
    return t == CC_TYPE_UCHAR || t == CC_TYPE_USHORT || t == CC_TYPE_UINT || t == CC_TYPE_ULONG_LONG;
}

static int is_integral_type(cc_type_t t) {
    return t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_UCHAR || t == CC_TYPE_SHORT ||
           t == CC_TYPE_USHORT || t == CC_TYPE_INT || t == CC_TYPE_UINT || t == CC_TYPE_LONG_LONG ||
           t == CC_TYPE_ULONG_LONG;
}

static int is_numeric_type(cc_type_t t) {
    return is_integral_type(t) || is_float_type(t);
}

static int is_scalar_type(cc_type_t t) {
    return is_numeric_type(t) || is_pointer_type(t);
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

static cc_type_t ptr_base_type(cc_type_t t) {
    switch (t) {
    case CC_TYPE_PTR_VOID:
        return CC_TYPE_VOID;
    case CC_TYPE_PTR_BOOL:
        return CC_TYPE_BOOL;
    case CC_TYPE_PTR_CHAR:
        return CC_TYPE_CHAR;
    case CC_TYPE_PTR_UCHAR:
        return CC_TYPE_UCHAR;
    case CC_TYPE_PTR_SHORT:
        return CC_TYPE_SHORT;
    case CC_TYPE_PTR_USHORT:
        return CC_TYPE_USHORT;
    case CC_TYPE_PTR_INT:
        return CC_TYPE_INT;
    case CC_TYPE_PTR_UINT:
        return CC_TYPE_UINT;
    case CC_TYPE_PTR_LONG_LONG:
        return CC_TYPE_LONG_LONG;
    case CC_TYPE_PTR_ULONG_LONG:
        return CC_TYPE_ULONG_LONG;
    case CC_TYPE_PTR_FLOAT:
        return CC_TYPE_FLOAT;
    case CC_TYPE_PTR_DOUBLE:
        return CC_TYPE_DOUBLE;
    case CC_TYPE_PTR_PTR_VOID:
        return CC_TYPE_PTR_VOID;
    case CC_TYPE_PTR_PTR_BOOL:
        return CC_TYPE_PTR_BOOL;
    case CC_TYPE_PTR_PTR_CHAR:
        return CC_TYPE_PTR_CHAR;
    case CC_TYPE_PTR_PTR_UCHAR:
        return CC_TYPE_PTR_UCHAR;
    case CC_TYPE_PTR_PTR_SHORT:
        return CC_TYPE_PTR_SHORT;
    case CC_TYPE_PTR_PTR_USHORT:
        return CC_TYPE_PTR_USHORT;
    case CC_TYPE_PTR_PTR_INT:
        return CC_TYPE_PTR_INT;
    case CC_TYPE_PTR_PTR_UINT:
        return CC_TYPE_PTR_UINT;
    case CC_TYPE_PTR_PTR_LONG_LONG:
        return CC_TYPE_PTR_LONG_LONG;
    case CC_TYPE_PTR_PTR_ULONG_LONG:
        return CC_TYPE_PTR_ULONG_LONG;
    case CC_TYPE_PTR_PTR_FLOAT:
        return CC_TYPE_PTR_FLOAT;
    case CC_TYPE_PTR_PTR_DOUBLE:
        return CC_TYPE_PTR_DOUBLE;
    case CC_TYPE_PTR_PTR_PTR_VOID:
        return CC_TYPE_PTR_PTR_VOID;
    case CC_TYPE_PTR_PTR_PTR_BOOL:
        return CC_TYPE_PTR_PTR_BOOL;
    case CC_TYPE_PTR_PTR_PTR_CHAR:
        return CC_TYPE_PTR_PTR_CHAR;
    case CC_TYPE_PTR_PTR_PTR_UCHAR:
        return CC_TYPE_PTR_PTR_UCHAR;
    case CC_TYPE_PTR_PTR_PTR_SHORT:
        return CC_TYPE_PTR_PTR_SHORT;
    case CC_TYPE_PTR_PTR_PTR_USHORT:
        return CC_TYPE_PTR_PTR_USHORT;
    case CC_TYPE_PTR_PTR_PTR_INT:
        return CC_TYPE_PTR_PTR_INT;
    case CC_TYPE_PTR_PTR_PTR_UINT:
        return CC_TYPE_PTR_PTR_UINT;
    case CC_TYPE_PTR_PTR_PTR_LONG_LONG:
        return CC_TYPE_PTR_PTR_LONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_ULONG_LONG:
        return CC_TYPE_PTR_PTR_ULONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_FLOAT:
        return CC_TYPE_PTR_PTR_FLOAT;
    case CC_TYPE_PTR_PTR_PTR_DOUBLE:
        return CC_TYPE_PTR_PTR_DOUBLE;
    case CC_TYPE_PTR_PTR_PTR_PTR_VOID:
        return CC_TYPE_PTR_PTR_PTR_VOID;
    case CC_TYPE_PTR_PTR_PTR_PTR_BOOL:
        return CC_TYPE_PTR_PTR_PTR_BOOL;
    case CC_TYPE_PTR_PTR_PTR_PTR_CHAR:
        return CC_TYPE_PTR_PTR_PTR_CHAR;
    case CC_TYPE_PTR_PTR_PTR_PTR_UCHAR:
        return CC_TYPE_PTR_PTR_PTR_UCHAR;
    case CC_TYPE_PTR_PTR_PTR_PTR_SHORT:
        return CC_TYPE_PTR_PTR_PTR_SHORT;
    case CC_TYPE_PTR_PTR_PTR_PTR_USHORT:
        return CC_TYPE_PTR_PTR_PTR_USHORT;
    case CC_TYPE_PTR_PTR_PTR_PTR_INT:
        return CC_TYPE_PTR_PTR_PTR_INT;
    case CC_TYPE_PTR_PTR_PTR_PTR_UINT:
        return CC_TYPE_PTR_PTR_PTR_UINT;
    case CC_TYPE_PTR_PTR_PTR_PTR_LONG_LONG:
        return CC_TYPE_PTR_PTR_PTR_LONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_PTR_ULONG_LONG:
        return CC_TYPE_PTR_PTR_PTR_ULONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_PTR_FLOAT:
        return CC_TYPE_PTR_PTR_PTR_FLOAT;
    case CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE:
        return CC_TYPE_PTR_PTR_PTR_DOUBLE;
    default:
        return CC_TYPE_VOID;
    }
}

static int is_null_ptr_constant(const cc_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == CC_EXPR_INT && e->int_val == 0) {
        return 1;
    }
    if (e->kind == CC_EXPR_CAST) {
        return is_null_ptr_constant(e->lhs);
    }
    if (e->kind == CC_EXPR_BIN && e->op == CC_BIN_COMMA) {
        return is_null_ptr_constant(e->rhs);
    }
    return 0;
}

static int can_convert(cc_type_t dst, cc_type_t src) {
    if (dst == src) {
        return 1;
    }
    if (is_numeric_type(dst) && is_numeric_type(src)) {
        return 1;
    }
    if (is_pointer_type(dst) && is_pointer_type(src)) {
        cc_type_t dbase = ptr_base_type(dst);
        cc_type_t sbase = ptr_base_type(src);
        if (dbase == CC_TYPE_VOID || sbase == CC_TYPE_VOID || dbase == sbase) {
            return 1;
        }
    }
    if (is_pointer_type(dst) && is_integral_type(src)) {
        return 1;
    }
    if (is_integral_type(dst) && is_pointer_type(src)) {
        return 1;
    }
    return 0;
}

static cc_type_t integral_promo_type(cc_type_t t) {
    if (t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_UCHAR || t == CC_TYPE_SHORT ||
        t == CC_TYPE_USHORT) {
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

static int builtin_bswap_bits(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "__builtin_bswap16") == 0) {
        return 16;
    }
    if (strcmp(name, "__builtin_bswap32") == 0) {
        return 32;
    }
    if (strcmp(name, "__builtin_bswap64") == 0) {
        return 64;
    }
    return 0;
}

typedef enum {
    BUILTIN_NONE = 0,
    BUILTIN_VA_START,
    BUILTIN_VA_END,
    BUILTIN_VA_COPY,
    BUILTIN_VA_ARG,
    BUILTIN_EXPECT,
    BUILTIN_CONSTANT_P,
    BUILTIN_TRAP,
    BUILTIN_CTZ,
    BUILTIN_SYNC_FETCH_ADD,
    BUILTIN_SYNC_FETCH_SUB,
    BUILTIN_SYNC_SUB_AND_FETCH,
    BUILTIN_SYNC_BOOL_CAS,
    BUILTIN_SYNC_LOCK_TEST_AND_SET,
    BUILTIN_SYNC_LOCK_RELEASE,
    BUILTIN_SYNC_SYNCHRONIZE,
    BUILTIN_ATOMIC_FETCH_ADD,
    BUILTIN_ATOMIC_FETCH_SUB,
    BUILTIN_ATOMIC_EXCHANGE_N,
    BUILTIN_ATOMIC_LOAD_N,
    BUILTIN_ATOMIC_STORE_N
} builtin_kind_t;

static builtin_kind_t builtin_kind(const char *name) {
    if (name == NULL) {
        return BUILTIN_NONE;
    }
    if (strcmp(name, "__builtin_va_start") == 0) {
        return BUILTIN_VA_START;
    }
    if (strcmp(name, "__builtin_va_end") == 0) {
        return BUILTIN_VA_END;
    }
    if (strcmp(name, "__builtin_va_copy") == 0) {
        return BUILTIN_VA_COPY;
    }
    if (strcmp(name, "__builtin_va_arg") == 0) {
        return BUILTIN_VA_ARG;
    }
    if (strcmp(name, "__builtin_expect") == 0) {
        return BUILTIN_EXPECT;
    }
    if (strcmp(name, "__builtin_constant_p") == 0) {
        return BUILTIN_CONSTANT_P;
    }
    if (strcmp(name, "__builtin_trap") == 0) {
        return BUILTIN_TRAP;
    }
    if (strcmp(name, "__builtin_ctz") == 0) {
        return BUILTIN_CTZ;
    }
    if (strcmp(name, "__sync_fetch_and_add") == 0) {
        return BUILTIN_SYNC_FETCH_ADD;
    }
    if (strcmp(name, "__sync_fetch_and_sub") == 0) {
        return BUILTIN_SYNC_FETCH_SUB;
    }
    if (strcmp(name, "__sync_sub_and_fetch") == 0) {
        return BUILTIN_SYNC_SUB_AND_FETCH;
    }
    if (strcmp(name, "__sync_bool_compare_and_swap") == 0) {
        return BUILTIN_SYNC_BOOL_CAS;
    }
    if (strcmp(name, "__sync_lock_test_and_set") == 0) {
        return BUILTIN_SYNC_LOCK_TEST_AND_SET;
    }
    if (strcmp(name, "__sync_lock_release") == 0) {
        return BUILTIN_SYNC_LOCK_RELEASE;
    }
    if (strcmp(name, "__sync_synchronize") == 0) {
        return BUILTIN_SYNC_SYNCHRONIZE;
    }
    if (strcmp(name, "__atomic_fetch_add") == 0) {
        return BUILTIN_ATOMIC_FETCH_ADD;
    }
    if (strcmp(name, "__atomic_fetch_sub") == 0) {
        return BUILTIN_ATOMIC_FETCH_SUB;
    }
    if (strcmp(name, "__atomic_exchange_n") == 0) {
        return BUILTIN_ATOMIC_EXCHANGE_N;
    }
    if (strcmp(name, "__atomic_load_n") == 0) {
        return BUILTIN_ATOMIC_LOAD_N;
    }
    if (strcmp(name, "__atomic_store_n") == 0) {
        return BUILTIN_ATOMIC_STORE_N;
    }
    return BUILTIN_NONE;
}

static long type_size_bytes(cc_type_t t) {
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
    case CC_TYPE_PTR_VOID:
    case CC_TYPE_PTR_BOOL:
    case CC_TYPE_PTR_CHAR:
    case CC_TYPE_PTR_UCHAR:
    case CC_TYPE_PTR_SHORT:
    case CC_TYPE_PTR_USHORT:
    case CC_TYPE_PTR_INT:
    case CC_TYPE_PTR_UINT:
    case CC_TYPE_PTR_LONG_LONG:
    case CC_TYPE_PTR_ULONG_LONG:
    case CC_TYPE_PTR_FLOAT:
    case CC_TYPE_PTR_DOUBLE:
    case CC_TYPE_PTR_PTR_VOID:
    case CC_TYPE_PTR_PTR_BOOL:
    case CC_TYPE_PTR_PTR_CHAR:
    case CC_TYPE_PTR_PTR_UCHAR:
    case CC_TYPE_PTR_PTR_SHORT:
    case CC_TYPE_PTR_PTR_USHORT:
    case CC_TYPE_PTR_PTR_INT:
    case CC_TYPE_PTR_PTR_UINT:
    case CC_TYPE_PTR_PTR_LONG_LONG:
    case CC_TYPE_PTR_PTR_ULONG_LONG:
    case CC_TYPE_PTR_PTR_FLOAT:
    case CC_TYPE_PTR_PTR_DOUBLE:
    case CC_TYPE_PTR_PTR_PTR_VOID:
    case CC_TYPE_PTR_PTR_PTR_BOOL:
    case CC_TYPE_PTR_PTR_PTR_CHAR:
    case CC_TYPE_PTR_PTR_PTR_UCHAR:
    case CC_TYPE_PTR_PTR_PTR_SHORT:
    case CC_TYPE_PTR_PTR_PTR_USHORT:
    case CC_TYPE_PTR_PTR_PTR_INT:
    case CC_TYPE_PTR_PTR_PTR_UINT:
    case CC_TYPE_PTR_PTR_PTR_LONG_LONG:
    case CC_TYPE_PTR_PTR_PTR_ULONG_LONG:
    case CC_TYPE_PTR_PTR_PTR_FLOAT:
    case CC_TYPE_PTR_PTR_PTR_DOUBLE:
    case CC_TYPE_PTR_PTR_PTR_PTR_VOID:
    case CC_TYPE_PTR_PTR_PTR_PTR_BOOL:
    case CC_TYPE_PTR_PTR_PTR_PTR_CHAR:
    case CC_TYPE_PTR_PTR_PTR_PTR_UCHAR:
    case CC_TYPE_PTR_PTR_PTR_PTR_SHORT:
    case CC_TYPE_PTR_PTR_PTR_PTR_USHORT:
    case CC_TYPE_PTR_PTR_PTR_PTR_INT:
    case CC_TYPE_PTR_PTR_PTR_PTR_UINT:
    case CC_TYPE_PTR_PTR_PTR_PTR_LONG_LONG:
    case CC_TYPE_PTR_PTR_PTR_PTR_ULONG_LONG:
    case CC_TYPE_PTR_PTR_PTR_PTR_FLOAT:
    case CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE:
        return g_pointer_size_bytes;
    default:
        return -1;
    }
}

static long type_size_bytes_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
    long n = type_size_bytes(t);
    if (n > 0) {
        return n;
    }
    if (t == CC_TYPE_VOID && tu != NULL && struct_id >= 0 && (size_t)struct_id < tu->struct_count &&
        tu->structs[struct_id].complete) {
        return tu->structs[struct_id].size;
    }
    return -1;
}

static int eval_const_int_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long *out) {
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
        if (eval_const_int_expr(tu, e->lhs, &a) != 0 || eval_const_int_expr(tu, e->rhs, &b) != 0) {
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
        return eval_const_int_expr(tu, e->lhs, out);

    case CC_EXPR_SIZEOF:
        if (e->lhs != NULL) {
            *out = type_size_bytes_struct(tu, e->lhs->value_type, e->lhs->struct_id);
        } else {
            *out = type_size_bytes_struct(tu, e->aux_type, e->aux_struct_id);
        }
        return *out < 0 ? -1 : 0;

    case CC_EXPR_TERNARY:
        if (eval_const_int_expr(tu, e->lhs, &a) != 0) {
            return -1;
        }
        if (a != 0) {
            return eval_const_int_expr(tu, e->rhs, out);
        }
        return eval_const_int_expr(tu, e->third, out);

    default:
        return -1;
    }
}

static int check_struct_initializer(const cc_translation_unit_t *tu, const char *name, int struct_id, cc_expr_t *init,
                                    var_entry_t *vars, size_t var_count, int depth, cc_diag_t *diag);
static cc_expr_t *unwrap_scalar_initializer_expr(cc_expr_t *init, cc_diag_t *diag);

static int check_expr(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count, int depth,
                      cc_diag_t *diag) {
    size_t i;

    if (e == NULL) {
        set_diag(diag, "null expression in semantic analysis");
        return -1;
    }

    switch (e->kind) {
    case CC_EXPR_INT:
        if (!is_integral_type(e->value_type)) {
            e->value_type = CC_TYPE_INT;
        }
        e->struct_id = -1;
        return 0;

    case CC_EXPR_FLOAT:
        e->value_type = CC_TYPE_DOUBLE;
        e->struct_id = -1;
        return 0;

    case CC_EXPR_STR:
        e->value_type = CC_TYPE_PTR_CHAR;
        e->struct_id = -1;
        return 0;

    case CC_EXPR_IDENT: {
        int idx = vars_find_visible(vars, var_count, e->ident, depth);
        if (idx >= 0) {
            e->value_type = vars[idx].type;
            e->struct_id = vars[idx].struct_id;
            return 0;
        }
        if (e->ident != NULL && strcmp(e->ident, "__func__") == 0) {
            e->value_type = CC_TYPE_PTR_CHAR;
            e->struct_id = -1;
            return 0;
        }
        {
            const cc_global_t *g = find_global(tu, e->ident);
            if (g != NULL) {
                e->value_type = g->type;
                e->struct_id = g->type_struct_id;
                return 0;
            }
            if (find_function(tu, e->ident) != NULL) {
                e->value_type = CC_TYPE_PTR_VOID;
                e->struct_id = -1;
                return 0;
            }
            /* Fallback for unresolved extern symbols inside function scope. */
            e->value_type = CC_TYPE_PTR_VOID;
            e->struct_id = -1;
            return 0;
        }
    }

    case CC_EXPR_MEMBER: {
        const cc_struct_member_t *m;
        int sid = -1;
        if (e->lhs == NULL && e->rhs != NULL && e->ident != NULL) {
            if (check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            e->value_type = e->rhs->value_type;
            e->struct_id = e->rhs->struct_id;
            return 0;
        }
        if (e->lhs == NULL || e->ident == NULL) {
            set_diag(diag, "malformed member expression");
            return -1;
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (e->member_is_arrow) {
            if (!is_pointer_type(e->lhs->value_type)) {
                set_diag(diag, "-> requires pointer operand");
                return -1;
            }
            sid = e->lhs->struct_id;
            if (sid < 0) {
                set_diag(diag, "-> requires pointer to known struct type");
                return -1;
            }
        } else {
            sid = e->lhs->struct_id;
            if (sid < 0) {
                set_diag(diag, ". requires struct operand");
                return -1;
            }
        }
        m = find_struct_member(tu, sid, e->ident);
        if (m == NULL) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "unknown member '%s' for struct", e->ident);
            }
            return -1;
        }
        e->value_type = m->type;
        e->struct_id = m->type_struct_id;
        e->member_offset = m->offset;
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
            if (!is_scalar_type(e->lhs->value_type) || !is_scalar_type(e->rhs->value_type)) {
                set_diag(diag, "logical operators require scalar operands");
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
            if (e->op == CC_BIN_EQ || e->op == CC_BIN_NE) {
                if (is_numeric_type(e->lhs->value_type) && is_numeric_type(e->rhs->value_type)) {
                    e->value_type = CC_TYPE_INT;
                    return 0;
                }
                if (is_pointer_type(e->lhs->value_type) && is_pointer_type(e->rhs->value_type)) {
                    if (can_convert(e->lhs->value_type, e->rhs->value_type) ||
                        can_convert(e->rhs->value_type, e->lhs->value_type)) {
                        e->value_type = CC_TYPE_INT;
                        return 0;
                    }
                    set_diag(diag, "incompatible pointer types in comparison");
                    return -1;
                }
                if (is_pointer_type(e->lhs->value_type) && is_integral_type(e->rhs->value_type) &&
                    is_null_ptr_constant(e->rhs)) {
                    e->value_type = CC_TYPE_INT;
                    return 0;
                }
                if (is_pointer_type(e->rhs->value_type) && is_integral_type(e->lhs->value_type) &&
                    is_null_ptr_constant(e->lhs)) {
                    e->value_type = CC_TYPE_INT;
                    return 0;
                }
                set_diag(diag, "comparison operators require compatible scalar operands");
                return -1;
            }
            if (is_numeric_type(e->lhs->value_type) && is_numeric_type(e->rhs->value_type)) {
                e->value_type = CC_TYPE_INT;
                return 0;
            }
            if (is_pointer_type(e->lhs->value_type) && is_pointer_type(e->rhs->value_type)) {
                if (can_convert(e->lhs->value_type, e->rhs->value_type) ||
                    can_convert(e->rhs->value_type, e->lhs->value_type)) {
                    e->value_type = CC_TYPE_INT;
                    return 0;
                }
                set_diag(diag, "incompatible pointer types in comparison");
                return -1;
            }
            set_diag(diag, "ordered comparison operators require numeric or compatible pointer operands");
            return -1;
        }
        if (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) {
            if (e->op == CC_BIN_SUB && is_pointer_type(e->lhs->value_type) && is_pointer_type(e->rhs->value_type)) {
                if (!can_convert(e->lhs->value_type, e->rhs->value_type) &&
                    !can_convert(e->rhs->value_type, e->lhs->value_type)) {
                    set_diag(diag, "incompatible pointer types in subtraction");
                    return -1;
                }
                e->value_type = CC_TYPE_INT;
                e->struct_id = -1;
                return 0;
            }
            if (is_pointer_type(e->lhs->value_type) && is_integral_type(e->rhs->value_type)) {
                e->value_type = e->lhs->value_type;
                e->struct_id = e->lhs->struct_id;
                return 0;
            }
            if (e->op == CC_BIN_ADD && is_integral_type(e->lhs->value_type) && is_pointer_type(e->rhs->value_type)) {
                e->value_type = e->rhs->value_type;
                e->struct_id = e->rhs->struct_id;
                return 0;
            }
            if (is_pointer_type(e->lhs->value_type) || is_pointer_type(e->rhs->value_type)) {
                set_diag(diag, "unsupported pointer arithmetic form");
                return -1;
            }
        } else if (is_pointer_type(e->lhs->value_type) || is_pointer_type(e->rhs->value_type)) {
            set_diag(diag, "arithmetic operators require numeric operands");
            return -1;
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
        const cc_function_t *callee = NULL;
        builtin_kind_t bk = builtin_kind(e->ident);
        int bswap_bits = builtin_bswap_bits(e->ident);
        cc_type_t elem_type;
        if (e->ident != NULL) {
            callee = find_function(tu, e->ident);
        }
        if (bk == BUILTIN_VA_START) {
            if (e->arg_count != 2) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "__builtin_va_start expects exactly 2 arguments");
                }
                return -1;
            }
            for (i = 0; i < e->arg_count; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_va_start first argument must be a pointer");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_VA_END) {
            if (e->arg_count != 1) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "__builtin_va_end expects exactly 1 argument");
                }
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_va_end argument must be a pointer");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_VA_COPY) {
            if (e->arg_count != 2) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "__builtin_va_copy expects exactly 2 arguments");
                }
                return -1;
            }
            for (i = 0; i < e->arg_count; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
                if (!is_pointer_type(e->args[i]->value_type)) {
                    set_diag(diag, "__builtin_va_copy arguments must be pointers");
                    return -1;
                }
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_VA_ARG) {
            if (e->arg_count != 1) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "__builtin_va_arg expects va_list and a type");
                }
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_va_arg first operand must be a pointer");
                return -1;
            }
            if (e->aux_type == CC_TYPE_VOID) {
                set_diag(diag, "__builtin_va_arg requires a non-void type");
                return -1;
            }
            e->value_type = e->aux_type;
            e->struct_id = e->aux_struct_id;
            return 0;
        }
        if (bk == BUILTIN_EXPECT) {
            if (e->arg_count != 2) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "__builtin_expect expects exactly 2 arguments");
                }
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0 ||
                check_expr(tu, e->args[1], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_scalar_type(e->args[0]->value_type) || !is_integral_type(e->args[1]->value_type)) {
                set_diag(diag, "__builtin_expect expects (scalar, integer)");
                return -1;
            }
            e->value_type = e->args[0]->value_type;
            e->struct_id = e->args[0]->struct_id;
            return 0;
        }
        if (bk == BUILTIN_CONSTANT_P) {
            if (e->arg_count != 1) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "__builtin_constant_p expects exactly 1 argument");
                }
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_TRAP) {
            if (e->arg_count != 0) {
                set_diag(diag, "__builtin_trap expects exactly 0 arguments");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_CTZ) {
            if (e->arg_count != 1) {
                set_diag(diag, "__builtin_ctz expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_integral_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_ctz argument must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_SYNC_SYNCHRONIZE) {
            if (e->arg_count != 0) {
                set_diag(diag, "__sync_synchronize expects exactly 0 arguments");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_SYNC_LOCK_RELEASE) {
            if (e->arg_count != 1) {
                set_diag(diag, "__sync_lock_release expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__sync_lock_release first argument must be a pointer");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_SYNC_FETCH_ADD || bk == BUILTIN_SYNC_FETCH_SUB || bk == BUILTIN_SYNC_SUB_AND_FETCH ||
            bk == BUILTIN_SYNC_LOCK_TEST_AND_SET || bk == BUILTIN_ATOMIC_FETCH_ADD ||
            bk == BUILTIN_ATOMIC_FETCH_SUB || bk == BUILTIN_ATOMIC_EXCHANGE_N) {
            int expect_argc = (bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB ||
                               bk == BUILTIN_ATOMIC_EXCHANGE_N)
                                  ? 3
                                  : 2;
            if (e->arg_count != (size_t)expect_argc) {
                set_diag(diag, "atomic fetch/exchange builtin has wrong argument count");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0 ||
                check_expr(tu, e->args[1], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if ((bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB ||
                 bk == BUILTIN_ATOMIC_EXCHANGE_N) &&
                check_expr(tu, e->args[2], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "atomic fetch/exchange first argument must be a pointer");
                return -1;
            }
            elem_type = ptr_base_type(e->args[0]->value_type);
            if (elem_type == CC_TYPE_VOID) {
                elem_type = CC_TYPE_INT;
            }
            if (!can_convert(elem_type, e->args[1]->value_type)) {
                set_diag(diag, "atomic fetch/exchange value type mismatch");
                return -1;
            }
            e->value_type = elem_type;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_SYNC_BOOL_CAS) {
            if (e->arg_count != 3) {
                set_diag(diag, "__sync_bool_compare_and_swap expects exactly 3 arguments");
                return -1;
            }
            for (i = 0; i < 3; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__sync_bool_compare_and_swap first argument must be a pointer");
                return -1;
            }
            elem_type = ptr_base_type(e->args[0]->value_type);
            if (elem_type == CC_TYPE_VOID) {
                elem_type = CC_TYPE_INT;
            }
            if (!can_convert(elem_type, e->args[1]->value_type) || !can_convert(elem_type, e->args[2]->value_type)) {
                set_diag(diag, "__sync_bool_compare_and_swap value type mismatch");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_ATOMIC_LOAD_N) {
            if (e->arg_count != 2) {
                set_diag(diag, "__atomic_load_n expects exactly 2 arguments");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0 ||
                check_expr(tu, e->args[1], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__atomic_load_n first argument must be a pointer");
                return -1;
            }
            elem_type = ptr_base_type(e->args[0]->value_type);
            if (elem_type == CC_TYPE_VOID) {
                elem_type = CC_TYPE_INT;
            }
            e->value_type = elem_type;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_ATOMIC_STORE_N) {
            if (e->arg_count != 3) {
                set_diag(diag, "__atomic_store_n expects exactly 3 arguments");
                return -1;
            }
            for (i = 0; i < 3; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__atomic_store_n first argument must be a pointer");
                return -1;
            }
            elem_type = ptr_base_type(e->args[0]->value_type);
            if (elem_type == CC_TYPE_VOID) {
                elem_type = CC_TYPE_INT;
            }
            if (!can_convert(elem_type, e->args[1]->value_type)) {
                set_diag(diag, "__atomic_store_n value type mismatch");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (e->ident != NULL && callee == NULL && bswap_bits != 0) {
            if (e->arg_count != 1) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "%s expects exactly 1 argument", e->ident);
                }
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_integral_type(e->args[0]->value_type)) {
                set_diag(diag, "byte-swap builtin requires integral argument");
                return -1;
            }
            if (bswap_bits == 16) {
                e->value_type = CC_TYPE_USHORT;
            } else if (bswap_bits == 32) {
                e->value_type = CC_TYPE_UINT;
            } else {
                e->value_type = CC_TYPE_ULONG_LONG;
            }
            e->struct_id = -1;
            return 0;
        }
        if (e->ident == NULL) {
            if (e->lhs == NULL) {
                set_diag(diag, "malformed call expression");
                return -1;
            }
            if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_pointer_type(e->lhs->value_type)) {
                set_diag(diag, "call target must be a function pointer");
                return -1;
            }
        }
        if (callee != NULL && !callee->is_variadic && e->arg_count != callee->param_count) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "call to %s has %zu args but %zu required", e->ident,
                         e->arg_count, callee->param_count);
            }
            return -1;
        }
        if (callee != NULL && callee->is_variadic && e->arg_count < callee->param_count) {
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
            if (callee != NULL && i < callee->param_count && !can_convert(callee->params[i].type, e->args[i]->value_type) &&
                !(is_pointer_type(callee->params[i].type) && is_integral_type(e->args[i]->value_type) &&
                  is_null_ptr_constant(e->args[i]))) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "cannot convert arg %zu in call to %s", i,
                             e->ident);
                }
                return -1;
            }
        }
        if (callee != NULL) {
            e->value_type = callee->ret_type;
            e->struct_id = callee->ret_struct_id;
        } else if (e->ident != NULL) {
            if (!g_allow_implicit_funcdecl) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "implicit function declaration is not allowed in this mode: %s", e->ident);
                }
                return -1;
            }
            /* C89-style fallback for undeclared functions: assume extern int f(...). */
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
        } else {
            e->value_type = ptr_base_type(e->lhs->value_type);
            e->struct_id = -1;
            if (e->lhs->struct_id >= 0 && (e->value_type == CC_TYPE_VOID || is_pointer_type(e->value_type))) {
                e->struct_id = e->lhs->struct_id;
            }
        }
        return 0;
    }

    case CC_EXPR_ASSIGN: {
        cc_type_t dst_type;
        int dst_struct_id = -1;
        int assign_ok;
        if (e->ident != NULL) {
            int idx = vars_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                dst_type = vars[idx].type;
                dst_struct_id = vars[idx].struct_id;
            } else {
                const cc_global_t *g = find_global(tu, e->ident);
                if (g == NULL) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "assignment to undeclared identifier: %s",
                                 e->ident);
                    }
                    return -1;
                }
                dst_type = g->type;
                dst_struct_id = g->type_struct_id;
            }
        } else if (e->lhs != NULL && (e->lhs->kind == CC_EXPR_DEREF || e->lhs->kind == CC_EXPR_MEMBER)) {
            if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            dst_type = e->lhs->value_type;
            dst_struct_id = e->lhs->struct_id;
        } else {
            set_diag(diag, "assignment target must be identifier, dereference, or member lvalue");
            return -1;
        }
        if (check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        assign_ok = can_convert(dst_type, e->rhs->value_type) ||
                    (is_pointer_type(dst_type) && is_integral_type(e->rhs->value_type) && is_null_ptr_constant(e->rhs));
        if (!assign_ok && dst_type == CC_TYPE_VOID && dst_struct_id >= 0 && e->rhs->value_type == CC_TYPE_VOID &&
            e->rhs->struct_id == dst_struct_id) {
            assign_ok = 1;
        }
        if (!assign_ok) {
            if (getenv("CC_DEBUG_SEMA_ASSIGN") != NULL) {
                fprintf(stderr,
                        "cc-debug: bad assign dst_type=%d dst_sid=%d rhs_type=%d rhs_sid=%d lhs_kind=%d rhs_kind=%d\n",
                        (int)dst_type, dst_struct_id, (int)e->rhs->value_type, e->rhs->struct_id,
                        e->lhs != NULL ? (int)e->lhs->kind : -1, e->rhs != NULL ? (int)e->rhs->kind : -1);
            }
            if (diag != NULL && diag->message[0] == '\0') {
                if (e->ident != NULL) {
                    snprintf(diag->message, sizeof(diag->message), "cannot assign expression to %s", e->ident);
                } else {
                    snprintf(diag->message, sizeof(diag->message), "%s", "cannot assign expression through pointer");
                }
            }
            return -1;
        }
        e->value_type = dst_type;
        e->struct_id = dst_struct_id;
        return 0;
    }

    case CC_EXPR_ADDR:
        if (e->lhs == NULL) {
            set_diag(diag, "malformed address-of expression");
            return -1;
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (e->lhs->kind == CC_EXPR_DEREF) {
            e->value_type = e->lhs->lhs != NULL ? e->lhs->lhs->value_type : CC_TYPE_VOID;
            if (!is_pointer_type(e->value_type)) {
                set_diag(diag, "address-of dereference requires pointer operand");
                return -1;
            }
            e->struct_id = e->lhs->lhs != NULL ? e->lhs->lhs->struct_id : -1;
            return 0;
        }
        if (e->lhs->kind == CC_EXPR_IDENT && e->lhs->ident != NULL && find_function(tu, e->lhs->ident) != NULL) {
            e->value_type = e->lhs->value_type;
            e->struct_id = e->lhs->struct_id;
            return 0;
        }
        if (e->lhs->kind != CC_EXPR_IDENT && e->lhs->kind != CC_EXPR_MEMBER) {
            set_diag(diag, "address-of currently requires an identifier or member lvalue");
            return -1;
        }
        e->value_type = ptr_of_type(e->lhs->value_type);
        if (!is_pointer_type(e->value_type)) {
            set_diag(diag, "cannot take address of this expression type");
            return -1;
        }
        e->struct_id = e->lhs->struct_id;
        return 0;

    case CC_EXPR_LABEL_ADDR:
        if (e->ident == NULL || e->ident[0] == '\0') {
            set_diag(diag, "malformed label-address expression");
            return -1;
        }
        e->value_type = CC_TYPE_PTR_VOID;
        e->struct_id = -1;
        return 0;

    case CC_EXPR_DEREF:
        if (e->lhs == NULL) {
            set_diag(diag, "malformed dereference expression");
            return -1;
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!is_pointer_type(e->lhs->value_type)) {
            if (getenv("CC_DEBUG_SEMA_DEREF") != NULL) {
                fprintf(stderr, "cc-debug: bad deref lhs kind=%d type=%d struct_id=%d ident=%s\n",
                        (int)e->lhs->kind, (int)e->lhs->value_type, e->lhs->struct_id,
                        e->lhs->ident != NULL ? e->lhs->ident : "<null>");
            }
            set_diag(diag, "dereference requires pointer operand");
            return -1;
        }
        e->value_type = ptr_base_type(e->lhs->value_type);
        e->struct_id = -1;
        if (e->lhs->struct_id >= 0 && (e->value_type == CC_TYPE_VOID || is_pointer_type(e->value_type))) {
            e->struct_id = e->lhs->struct_id;
        }
        if (e->value_type == CC_TYPE_VOID) {
            if (e->struct_id >= 0) {
                return 0;
            }
            set_diag(diag, "cannot dereference void pointer");
            return -1;
        }
        return 0;

    case CC_EXPR_UPDATE: {
        cc_type_t t;
        if (e->ident != NULL) {
            int idx = vars_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                t = vars[idx].type;
            } else {
                const cc_global_t *g = find_global(tu, e->ident);
                if (g == NULL) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "update of undeclared identifier: %s",
                                 e->ident ? e->ident : "<null>");
                    }
                    return -1;
                }
                t = g->type;
            }
        } else if (e->lhs != NULL && (e->lhs->kind == CC_EXPR_DEREF || e->lhs->kind == CC_EXPR_MEMBER)) {
            if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            t = e->lhs->value_type;
        } else {
            set_diag(diag, "++/-- target must be identifier, dereference, or member lvalue");
            return -1;
        }
        if (is_pointer_type(t)) {
            /* GNU-style extension: treat void* increments as byte-wise. */
        } else if (!is_numeric_type(t)) {
            set_diag(diag, "++/-- currently require numeric or pointer scalar operands");
            return -1;
        }
        e->value_type = t;
        e->struct_id = -1;
        return 0;
    }

    case CC_EXPR_CAST:
        if (e->lhs == NULL) {
            set_diag(diag, "malformed cast expression");
            return -1;
        }
        if (e->lhs->kind == CC_EXPR_INIT_LIST) {
            if (e->aux_type == CC_TYPE_VOID && e->aux_struct_id >= 0) {
                if (check_struct_initializer(tu, "<compound-literal>", e->aux_struct_id, e->lhs, vars, var_count,
                                             depth, diag) != 0) {
                    return -1;
                }
                e->value_type = CC_TYPE_VOID;
                e->struct_id = e->aux_struct_id;
                return 0;
            }
            e->lhs = unwrap_scalar_initializer_expr(e->lhs, diag);
            if (e->lhs == NULL) {
                return -1;
            }
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (e->aux_type == CC_TYPE_VOID) {
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (is_numeric_type(e->lhs->value_type) && is_numeric_type(e->aux_type)) {
            e->value_type = e->aux_type;
            e->struct_id = -1;
            return 0;
        }
        if (is_pointer_type(e->aux_type) &&
            (is_pointer_type(e->lhs->value_type) || is_integral_type(e->lhs->value_type))) {
            e->value_type = e->aux_type;
            e->struct_id = e->aux_struct_id;
            return 0;
        }
        if (is_integral_type(e->aux_type) && is_pointer_type(e->lhs->value_type)) {
            e->value_type = e->aux_type;
            e->struct_id = -1;
            return 0;
        }
        set_diag(diag, "cast currently supports numeric and pointer/integer conversions only");
        return -1;

    case CC_EXPR_SIZEOF:
        if (e->lhs != NULL) {
            if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (type_size_bytes_struct(tu, e->lhs->value_type, e->lhs->struct_id) < 0) {
                set_diag(diag, "sizeof unsupported for this operand type");
                return -1;
            }
        } else {
            if (type_size_bytes_struct(tu, e->aux_type, e->aux_struct_id) < 0) {
                set_diag(diag, "sizeof unsupported for this type");
                return -1;
            }
        }
        e->value_type = CC_TYPE_INT;
        e->struct_id = -1;
        return 0;

    case CC_EXPR_INIT_LIST:
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (e->arg_count > 0) {
            e->value_type = e->args[0]->value_type;
            e->struct_id = e->args[0]->struct_id;
        } else {
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
        }
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
        if (is_pointer_type(e->rhs->value_type) && is_pointer_type(e->third->value_type)) {
            if (can_convert(e->rhs->value_type, e->third->value_type)) {
                e->value_type = e->rhs->value_type;
                e->struct_id = e->rhs->struct_id;
                return 0;
            }
            if (can_convert(e->third->value_type, e->rhs->value_type)) {
                e->value_type = e->third->value_type;
                e->struct_id = e->third->struct_id;
                return 0;
            }
            set_diag(diag, "incompatible pointer types in conditional expression");
            return -1;
        }
        if (is_pointer_type(e->rhs->value_type) && is_integral_type(e->third->value_type) &&
            is_null_ptr_constant(e->third)) {
            e->value_type = e->rhs->value_type;
            e->struct_id = e->rhs->struct_id;
            return 0;
        }
        if (is_pointer_type(e->third->value_type) && is_integral_type(e->rhs->value_type) &&
            is_null_ptr_constant(e->rhs)) {
            e->value_type = e->third->value_type;
            e->struct_id = e->third->struct_id;
            return 0;
        }
        e->value_type = common_arith_type(e->rhs->value_type, e->third->value_type);
        if (e->value_type == CC_TYPE_VOID) {
            set_diag(diag, "incompatible types in conditional expression");
            return -1;
        }
        e->struct_id = -1;
        return 0;

    default:
        set_diag(diag, "unsupported expression kind");
        return -1;
    }
}

static int is_zero_initializer_expr(const cc_expr_t *e) {
    size_t i;
    if (e == NULL) {
        return 1;
    }
    switch (e->kind) {
    case CC_EXPR_INT:
        return e->int_val == 0;
    case CC_EXPR_FLOAT:
        return e->float_val == 0.0;
    case CC_EXPR_CAST:
        return is_zero_initializer_expr(e->lhs);
    case CC_EXPR_MEMBER:
        if (e->lhs == NULL && e->rhs != NULL) {
            return is_zero_initializer_expr(e->rhs);
        }
        return 0;
    case CC_EXPR_INIT_LIST:
        for (i = 0; i < e->arg_count; ++i) {
            if (!is_zero_initializer_expr(e->args[i])) {
                return 0;
            }
        }
        return 1;
    default:
        return 0;
    }
}

static cc_expr_t *unwrap_scalar_initializer_expr(cc_expr_t *init, cc_diag_t *diag) {
    cc_expr_t *item;
    if (init == NULL) {
        set_diag(diag, "initializer list is empty");
        return NULL;
    }
    if (init->kind != CC_EXPR_INIT_LIST) {
        return init;
    }
    if (init->arg_count == 0) {
        set_diag(diag, "initializer list is empty");
        return NULL;
    }
    if (init->arg_count > 1) {
        set_diag(diag, "too many elements in scalar initializer");
        return NULL;
    }
    item = init->args[0];
    if (item != NULL && item->kind == CC_EXPR_MEMBER) {
        set_diag(diag, "designated initializer is invalid for scalar type");
        return NULL;
    }
    return item;
}

static int check_struct_initializer(const cc_translation_unit_t *tu, const char *name, int struct_id, cc_expr_t *init,
                                    var_entry_t *vars, size_t var_count, int depth, cc_diag_t *diag) {
    size_t i;
    size_t next_member = 0;
    const cc_struct_def_t *sd;

    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        set_diag(diag, "struct initializer uses unknown struct type");
        return -1;
    }
    if (init == NULL || init->kind != CC_EXPR_INIT_LIST) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "struct initializer for %s must use braces",
                     name != NULL ? name : "<anon>");
        }
        return -1;
    }
    sd = &tu->structs[struct_id];
    if (sd->is_union) {
        if (init->arg_count > 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "too many initializers for union %s",
                         name != NULL ? name : "<anon>");
            }
            return -1;
        }
    } else if (init->arg_count > sd->member_count) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "too many initializers for struct %s",
                     name != NULL ? name : "<anon>");
        }
        return -1;
    }
    for (i = 0; i < init->arg_count; ++i) {
        cc_expr_t *raw = init->args[i];
        cc_expr_t *item = raw;
        const cc_struct_member_t *m;
        size_t member_idx = next_member;

        if (raw != NULL && raw->kind == CC_EXPR_MEMBER && raw->lhs == NULL && raw->rhs != NULL && raw->ident != NULL) {
            int didx = find_struct_member_index(tu, struct_id, raw->ident);
            if (didx < 0) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "unknown designated member '%s' for struct %s", raw->ident,
                             name != NULL ? name : "<anon>");
                }
                return -1;
            }
            member_idx = (size_t)didx;
            item = raw->rhs;
        }
        if (member_idx >= sd->member_count) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "too many initializers for struct %s",
                         name != NULL ? name : "<anon>");
            }
            return -1;
        }

        m = &sd->members[member_idx];
        if (!sd->is_union) {
            next_member = member_idx + 1;
        }
        if (sd->has_flexible_array && member_idx + 1 == sd->member_count && m->size == 0) {
            if (!is_zero_initializer_expr(item)) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "flexible array member cannot be initialized in struct %s",
                             name != NULL ? name : "<anon>");
                }
                return -1;
            }
            continue;
        }
        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            if (item->kind != CC_EXPR_INIT_LIST) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "member initializer %zu for aggregate %s must use braces", i,
                             name != NULL ? name : "<anon>");
                }
                return -1;
            }
            if (check_struct_initializer(tu, name, m->type_struct_id, item, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            continue;
        }
        if (check_expr(tu, item, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!can_convert(m->type, item->value_type) &&
            !(is_pointer_type(m->type) && is_integral_type(item->value_type) && is_null_ptr_constant(item))) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "cannot convert member initializer %zu for struct %s", i,
                         name != NULL ? name : "<anon>");
            }
            return -1;
        }
    }
    return 0;
}

static int check_array_initializer(const cc_translation_unit_t *tu, const char *name, cc_type_t array_type,
                                   int array_struct_id, long array_len, cc_expr_t *init, var_entry_t *vars,
                                   size_t var_count, int depth, long *out_inferred_len, cc_diag_t *diag) {
    size_t i;
    cc_type_t elem_type;

    if (init == NULL || init->kind != CC_EXPR_INIT_LIST) {
        set_diag(diag, "array initializer must use an initializer list");
        return -1;
    }
    if (!is_pointer_type(array_type) || array_len < 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "cannot initialize non-array object %s with initializer list",
                     name != NULL ? name : "<anon>");
        }
        return -1;
    }

    elem_type = ptr_base_type(array_type);
    if (elem_type == CC_TYPE_VOID && array_struct_id < 0) {
        set_diag(diag, "array initializer has unsupported element type");
        return -1;
    }
    if (array_len > 0 && init->arg_count > (size_t)array_len) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "too many initializers for array %s",
                     name != NULL ? name : "<anon>");
        }
        return -1;
    }
    if (array_len == 0 && init->arg_count == 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "cannot deduce array size for %s from empty initializer list",
                     name != NULL ? name : "<anon>");
        }
        return -1;
    }
    for (i = 0; i < init->arg_count; ++i) {
        cc_expr_t *item = init->args[i];
        if (elem_type == CC_TYPE_VOID && array_struct_id >= 0) {
            if (item->kind != CC_EXPR_INIT_LIST) {
                if (is_null_ptr_constant(item)) {
                    continue;
                }
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "array initializer %zu for %s must use braces for struct elements", i,
                             name != NULL ? name : "<anon>");
                }
                return -1;
            }
            if (check_struct_initializer(tu, name, array_struct_id, item, vars, var_count, depth, diag) != 0) {
                return -1;
            }
        } else {
            if (check_expr(tu, item, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!can_convert(elem_type, item->value_type) &&
                !(is_pointer_type(elem_type) && is_integral_type(item->value_type) && is_null_ptr_constant(item))) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "cannot convert initializer %zu for array %s", i,
                             name != NULL ? name : "<anon>");
                }
                return -1;
            }
        }
    }
    if (out_inferred_len != NULL && array_len == 0) {
        *out_inferred_len = (long)init->arg_count;
    }
    return 0;
}

static int check_stmt(const cc_translation_unit_t *tu, cc_stmt_t *s, var_entry_t **vars, size_t *var_count, int depth,
                      cc_type_t fn_ret_type, int fn_attr_flags, int loop_depth, int switch_depth, int *saw_return,
                      cc_diag_t *diag) {
    size_t i;

    switch (s->kind) {
    case CC_STMT_DECL:
        {
            cc_type_t init_type = s->type;
            int sc_count = storage_class_count(s->storage);
            if ((s->attr_flags & CC_ATTR_NORETURN) != 0) {
                set_diag(diag, "noreturn attribute is only valid on functions");
                return -1;
            }
            if ((s->attr_flags & CC_ATTR_SECTION) != 0) {
                set_diag(diag, "section attribute is only supported on file-scope objects/functions");
                return -1;
            }
            if ((s->attr_flags & CC_ATTR_ALIGNED) != 0 &&
                validate_attr_align(s->attr_align, diag, "aligned attribute on local declaration") != 0) {
                return -1;
            }
            if (sc_count > 1) {
                set_diag(diag, "multiple storage-class specifiers in local declaration");
                return -1;
            }
            if ((s->storage & CC_STORAGE_INLINE) != 0) {
                set_diag(diag, "inline is only valid on function declarations");
                return -1;
            }
            if ((s->storage & CC_STORAGE_EXTERN) != 0 && s->expr != NULL) {
                set_diag(diag, "extern local declaration cannot have an initializer");
                return -1;
            }
            if (s->array_len >= 0 && is_pointer_type(s->type)) {
                init_type = ptr_base_type(s->type);
            }
            if (s->type == CC_TYPE_VOID && s->type_struct_id < 0) {
                set_diag(diag, "void variable declarations are not supported");
                return -1;
            }
            if (vars_find_depth(*vars, *var_count, s->decl_name, depth) >= 0) {
                set_diag(diag, "duplicate local/parameter name");
                return -1;
            }
            if (vars_push(vars, var_count, s->decl_name, s->type, s->type_struct_id, depth) != 0) {
                set_diag(diag, "out of memory adding local variable");
                return -1;
            }
            if (s->expr != NULL) {
                if (s->expr->kind == CC_EXPR_INIT_LIST) {
                    if (is_pointer_type(s->type) && s->array_len >= 0) {
                        long inferred_len = -1;
                        if (check_array_initializer(tu, s->decl_name, s->type, s->type_struct_id, s->array_len, s->expr,
                                                    *vars, *var_count, depth, &inferred_len, diag) != 0) {
                            free((*vars)[*var_count - 1].name);
                            (*var_count)--;
                            return -1;
                        }
                        if (s->array_len == 0 && inferred_len > 0) {
                            s->array_len = inferred_len;
                        }
                    } else if (s->type == CC_TYPE_VOID && s->type_struct_id >= 0) {
                        if (check_struct_initializer(tu, s->decl_name, s->type_struct_id, s->expr, *vars, *var_count,
                                                     depth, diag) != 0) {
                            free((*vars)[*var_count - 1].name);
                            (*var_count)--;
                            return -1;
                        }
                    } else {
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message),
                                     "initializer list is unsupported for variable '%s'",
                                     s->decl_name != NULL ? s->decl_name : "<anon>");
                        }
                        free((*vars)[*var_count - 1].name);
                        (*var_count)--;
                        return -1;
                    }
                } else {
                    if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                        free((*vars)[*var_count - 1].name);
                        (*var_count)--;
                        return -1;
                    }
                    if (!can_convert(init_type, s->expr->value_type) &&
                        !(is_pointer_type(init_type) && is_integral_type(s->expr->value_type) &&
                          is_null_ptr_constant(s->expr))) {
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message),
                                     "cannot initialize variable '%s' (type=%d) with expression type=%d",
                                     s->decl_name != NULL ? s->decl_name : "<anon>", (int)init_type,
                                     (int)s->expr->value_type);
                        }
                        free((*vars)[*var_count - 1].name);
                        (*var_count)--;
                        return -1;
                    }
                }
            }
            return 0;
        }

    case CC_STMT_EXPR:
        if (s->expr == NULL) {
            return 0;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        return 0;

    case CC_STMT_RETURN:
        if ((fn_attr_flags & CC_ATTR_NORETURN) != 0) {
            set_diag(diag, "noreturn function must not contain a return statement");
            return -1;
        }
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
            if (check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_attr_flags, loop_depth,
                           switch_depth,
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
            if (check_stmt(tu, s->else_branch, vars, var_count, depth, fn_ret_type, fn_attr_flags, loop_depth,
                           switch_depth,
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
                if (check_stmt(tu, &s->block_stmts[i], vars, var_count, depth + 1, fn_ret_type, fn_attr_flags,
                               loop_depth,
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
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_attr_flags, loop_depth + 1,
                          switch_depth,
                          saw_return, diag);

    case CC_STMT_DO:
        if (s->expr == NULL || s->then_branch == NULL) {
            set_diag(diag, "malformed do-while statement");
            return -1;
        }
        if (check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_attr_flags, loop_depth + 1,
                       switch_depth,
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
                if (s->init_stmt->kind == CC_STMT_BLOCK) {
                    for (i = 0; i < s->init_stmt->block_count; ++i) {
                        if (check_stmt(tu, &s->init_stmt->block_stmts[i], vars, var_count, depth + 1, fn_ret_type,
                                       fn_attr_flags, loop_depth, switch_depth, saw_return, diag) != 0) {
                            return -1;
                        }
                    }
                } else {
                    if (check_stmt(tu, s->init_stmt, vars, var_count, depth + 1, fn_ret_type, loop_depth,
                                   fn_attr_flags, switch_depth, saw_return, diag) != 0) {
                        return -1;
                    }
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
            if (check_stmt(tu, s->then_branch, vars, var_count, for_depth, fn_ret_type, fn_attr_flags,
                           loop_depth + 1, switch_depth,
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
                if (eval_const_int_expr(tu, ci->expr, &vi) != 0) {
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
                        if (eval_const_int_expr(tu, cj->expr, &vj) == 0 && vj == vi) {
                            set_diag(diag, "duplicate case value in switch");
                            return -1;
                        }
                    }
                }
            }
        }
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_attr_flags, loop_depth,
                          switch_depth + 1,
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
        if (eval_const_int_expr(tu, s->expr, &s->expr->int_val) != 0) {
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
        if (s->expr != NULL) {
            if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_pointer_type(s->expr->value_type) && !is_integral_type(s->expr->value_type)) {
                set_diag(diag, "computed goto requires pointer or integer target expression");
                return -1;
            }
        }
        return 0;

    case CC_STMT_LABEL:
        if (s->then_branch == NULL) {
            set_diag(diag, "malformed labeled statement");
            return -1;
        }
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_attr_flags, loop_depth,
                          switch_depth,
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

    if (tu == NULL || (tu->func_count == 0 && tu->global_count == 0)) {
        set_diag(diag, "translation unit is empty");
        return -1;
    }

    for (i = 0; i < tu->global_count; ++i) {
        const cc_global_t *g = &tu->globals[i];
        int sc_count = storage_class_count(g->storage);
        size_t j;
        if (g->name == NULL || g->name[0] == '\0') {
            set_diag(diag, "file-scope declaration with missing name");
            goto fail_global;
        }
        if (sc_count > 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "multiple storage-class specifiers in file-scope declaration: %s", g->name);
            }
            goto fail_global;
        }
        if ((g->storage & (CC_STORAGE_AUTO | CC_STORAGE_REGISTER)) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "file-scope object cannot use auto/register storage: %s", g->name);
            }
            goto fail_global;
        }
        if ((g->storage & CC_STORAGE_INLINE) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "inline is only valid on function declarations: %s", g->name);
            }
            goto fail_global;
        }
        if (g->type == CC_TYPE_VOID && g->type_struct_id < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "invalid void file-scope object: %s", g->name);
            }
            goto fail_global;
        }
        if ((g->attr_flags & CC_ATTR_NORETURN) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "noreturn attribute is invalid for object '%s'",
                         g->name);
            }
            goto fail_global;
        }
        if ((g->attr_flags & CC_ATTR_ALIGNED) != 0 &&
            validate_attr_align(g->attr_align, diag, "aligned attribute on file-scope object") != 0) {
            goto fail_global;
        }
        if ((g->attr_flags & CC_ATTR_SECTION) != 0 &&
            validate_attr_section(g->attr_section, diag, "section attribute on file-scope object") != 0) {
            goto fail_global;
        }
        for (j = 0; j < i; ++j) {
            if (strcmp(tu->globals[j].name, g->name) == 0) {
                const cc_global_t *prev = &tu->globals[j];
                int prev_static = (prev->storage & CC_STORAGE_STATIC) != 0;
                int cur_static = (g->storage & CC_STORAGE_STATIC) != 0;
                if (prev->type != g->type || prev->type_struct_id != g->type_struct_id) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "conflicting file-scope object types: %s",
                                 g->name);
                    }
                    goto fail_global;
                }
                if (prev_static != cur_static) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "conflicting linkage for file-scope object: %s",
                                 g->name);
                    }
                    goto fail_global;
                }
                if (prev->init != NULL && g->init != NULL) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "duplicate file-scope definition: %s", g->name);
                    }
                    goto fail_global;
                }
                break;
            }
        }
        if (g->init != NULL) {
            if (g->init->kind == CC_EXPR_INIT_LIST) {
                if (is_pointer_type(g->type) && g->array_len >= 0) {
                    if (check_array_initializer(tu, g->name, g->type, g->type_struct_id, g->array_len, g->init, NULL,
                                                0, 0, NULL, diag) != 0) {
                        goto fail_global;
                    }
                } else if (g->type == CC_TYPE_VOID && g->type_struct_id >= 0) {
                    if (check_struct_initializer(tu, g->name, g->type_struct_id, g->init, NULL, 0, 0, diag) != 0) {
                        goto fail_global;
                    }
                } else {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "initializer list is unsupported for file-scope object %s", g->name);
                    }
                    goto fail_global;
                }
            } else {
                if (check_expr(tu, g->init, NULL, 0, 0, diag) != 0) {
                    goto fail_global;
                }
                if (!can_convert(g->type, g->init->value_type) &&
                    !(is_pointer_type(g->type) && is_integral_type(g->init->value_type) &&
                      is_null_ptr_constant(g->init))) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "cannot initialize file-scope object %s",
                                 g->name);
                    }
                    goto fail_global;
                }
            }
        }
    }

    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *f = &tu->funcs[i];
        int f_sc_count = storage_class_count(f->storage);
        size_t j;

        if (f->name == NULL || f->name[0] == '\0') {
            set_diag(diag, "function with missing name");
            goto fail_global;
        }
        if ((f->attr_flags & CC_ATTR_PACKED) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "packed attribute is invalid for function '%s'",
                         f->name);
            }
            goto fail_global;
        }
        if (f_sc_count > 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "multiple storage-class specifiers in function declaration: %s", f->name);
            }
            goto fail_global;
        }
        if ((f->storage & (CC_STORAGE_AUTO | CC_STORAGE_REGISTER)) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "function declaration cannot use auto/register storage: %s", f->name);
            }
            goto fail_global;
        }
        if ((f->attr_flags & CC_ATTR_NORETURN) != 0 && !(f->ret_type == CC_TYPE_VOID && f->ret_struct_id < 0)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "noreturn function '%s' must have void return type",
                         f->name);
            }
            goto fail_global;
        }
        if ((f->attr_flags & CC_ATTR_ALIGNED) != 0 &&
            validate_attr_align(f->attr_align, diag, "aligned attribute on function") != 0) {
            goto fail_global;
        }
        if ((f->attr_flags & CC_ATTR_SECTION) != 0 &&
            validate_attr_section(f->attr_section, diag, "section attribute on function") != 0) {
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
        size_t k;
        int saw_return = 0;
        int fn_attr_flags = f->attr_flags;

        if (!f->has_body) {
            continue;
        }

        for (k = 0; k < tu->func_count; ++k) {
            if (strcmp(tu->funcs[k].name, f->name) == 0) {
                fn_attr_flags |= tu->funcs[k].attr_flags;
            }
        }

        for (j = 0; j < f->param_count; ++j) {
            if (f->params[j].type == CC_TYPE_VOID && f->params[j].type_struct_id < 0) {
                set_diag(diag, "void is not valid for named parameter type");
                goto fail_func;
            }
            if (vars_find_depth(vars, var_count, f->params[j].name, 0) >= 0) {
                set_diag(diag, "duplicate parameter name");
                goto fail_func;
            }
            if (vars_push(&vars, &var_count, f->params[j].name, f->params[j].type, f->params[j].type_struct_id, 0) !=
                0) {
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
            if (check_stmt(tu, &f->stmts[j], &vars, &var_count, 0, f->ret_type, fn_attr_flags, 0, 0, &saw_return,
                           diag) != 0) {
                goto fail_func;
            }
        }

        (void)saw_return;

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

void cc_frontend_set_pointer_size(int bytes) {
    if (bytes == 4 || bytes == 8) {
        g_pointer_size_bytes = bytes;
        cc_parser_set_pointer_size(bytes);
    }
}

void cc_frontend_set_std_mode(const char *std_mode) {
    g_allow_implicit_funcdecl = std_mode_allows_implicit_function_decls(std_mode);
    cc_parser_set_std_mode(std_mode);
}
