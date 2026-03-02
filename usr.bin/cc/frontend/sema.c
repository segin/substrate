#include "cc_frontend.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cc_parser_set_pointer_size(int bytes);
void cc_parser_set_std_mode(const char *std_mode);
void cc_parser_set_gnu89_inline(int enabled);

static int g_pointer_size_bytes = 8;
static int g_allow_implicit_funcdecl = 0;
static int g_implicit_funcdecl_override = -1;
static int g_warn_all = 0;
static int g_warn_error = 0;
static int g_pedantic = 0;
static int g_pedantic_errors = 0;
static int g_std_c23 = 0;
static int g_gnu89_inline_override = -1;
static size_t g_diag_ctx_line = 0;
static size_t g_diag_ctx_col = 0;

static int std_mode_allows_implicit_function_decls(const char *std_mode) {
    if (std_mode == NULL || std_mode[0] == '\0') {
        return 0;
    }
    if (strncmp(std_mode, "gnu", 3) == 0) {
        return 1;
    }
    if (strcmp(std_mode, "c89") == 0 || strcmp(std_mode, "c90") == 0 || strcmp(std_mode, "c95") == 0 ||
        strcmp(std_mode, "gnu89") == 0 || strcmp(std_mode, "gnu90") == 0 || strcmp(std_mode, "gnu95") == 0) {
        return 1;
    }
    return 0;
}

static int std_mode_is_c23_or_newer(const char *std_mode) {
    if (std_mode == NULL || std_mode[0] == '\0') {
        return 0;
    }
    if (strcmp(std_mode, "c23") == 0 || strcmp(std_mode, "gnu23") == 0 || strcmp(std_mode, "c2x") == 0 ||
        strcmp(std_mode, "gnu2x") == 0) {
        return 1;
    }
    return 0;
}

typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
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
    if (d->line == 0 && d->col == 0) {
        d->line = g_diag_ctx_line;
        d->col = g_diag_ctx_col;
    }
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

static void sema_diag_clear(cc_diag_t *d) {
    if (d == NULL) {
        return;
    }
    d->line = 0;
    d->col = 0;
    d->message[0] = '\0';
}

static void sema_diag_report_and_clear(cc_diag_t *d) {
    if (d == NULL || d->message[0] == '\0') {
        return;
    }
    if (d->line == 0 && d->col == 0) {
        d->line = g_diag_ctx_line;
        d->col = g_diag_ctx_col;
    }
    if (d->line != 0) {
        if (d->path[0] != '\0') {
            fprintf(stderr, "%s:%zu:%zu: error: %s\n", d->path, d->line, d->col, d->message);
        } else {
            fprintf(stderr, "cc:%zu:%zu: error: %s\n", d->line, d->col, d->message);
        }
    } else if (d->path[0] != '\0') {
        fprintf(stderr, "%s: error: %s\n", d->path, d->message);
    } else {
        fprintf(stderr, "cc: error: %s\n", d->message);
    }
    d->error_count++;
    sema_diag_clear(d);
}

static void set_diag_context(size_t line, size_t col) {
    if (line == 0 && col == 0) {
        return;
    }
    g_diag_ctx_line = line;
    g_diag_ctx_col = col;
}

static int emit_warning(cc_diag_t *diag, size_t line, size_t col, const char *msg, int pedantic_only) {
    int as_error = g_warn_error || (pedantic_only && g_pedantic_errors);

    if (pedantic_only) {
        if (!g_pedantic) {
            return 0;
        }
    } else if (!g_warn_all) {
        return 0;
    }

    if (line == 0 && col == 0) {
        line = g_diag_ctx_line;
        col = g_diag_ctx_col;
    }

    if (line != 0) {
        fprintf(stderr, "cc:%zu:%zu: warning: %s\n", line, col, msg);
    } else {
        fprintf(stderr, "cc: warning: %s\n", msg);
    }

    if (!as_error) {
        return 0;
    }

    if (diag != NULL && diag->message[0] == '\0') {
        diag->line = line;
        diag->col = col;
        snprintf(diag->message, sizeof(diag->message), "%s", msg);
    }
    return -1;
}

static int emit_required_warning(cc_diag_t *diag, size_t line, size_t col, const char *msg) {
    if (line == 0 && col == 0) {
        line = g_diag_ctx_line;
        col = g_diag_ctx_col;
    }
    if (line != 0) {
        fprintf(stderr, "cc:%zu:%zu: warning: %s\n", line, col, msg);
    } else {
        fprintf(stderr, "cc: warning: %s\n", msg);
    }
    if (!g_warn_error) {
        return 0;
    }
    if (diag != NULL && diag->message[0] == '\0') {
        diag->line = line;
        diag->col = col;
        snprintf(diag->message, sizeof(diag->message), "%s", msg);
    }
    return -1;
}

static int maybe_warn_assignment_condition(const cc_expr_t *cond, const char *where, cc_diag_t *diag) {
    char buf[160];

    if (cond == NULL || cond->kind != CC_EXPR_ASSIGN || cond->paren_wrapped) {
        return 0;
    }
    if (cond->ident != NULL && cond->rhs != NULL && cond->rhs->kind == CC_EXPR_BIN && cond->rhs->lhs != NULL &&
        cond->rhs->lhs->kind == CC_EXPR_IDENT && cond->rhs->lhs->ident != NULL &&
        strcmp(cond->ident, cond->rhs->lhs->ident) == 0) {
        /*
         * parse_assign lowers compound assignments (e.g. x /= y) into
         * x = x / y. Do not warn on that form in conditions.
         */
        return 0;
    }

    snprintf(buf, sizeof(buf), "assignment used as condition in %s", where);
    return emit_warning(diag, cond->line, cond->col, buf, 0);
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

static int attr_visibility_mask(void) {
    return CC_ATTR_VIS_DEFAULT | CC_ATTR_VIS_HIDDEN | CC_ATTR_VIS_PROTECTED | CC_ATTR_VIS_INTERNAL;
}

static int has_multiple_visibility_attrs(int flags) {
    int v = flags & attr_visibility_mask();
    int n = 0;
    if ((v & CC_ATTR_VIS_DEFAULT) != 0) {
        n++;
    }
    if ((v & CC_ATTR_VIS_HIDDEN) != 0) {
        n++;
    }
    if ((v & CC_ATTR_VIS_PROTECTED) != 0) {
        n++;
    }
    if ((v & CC_ATTR_VIS_INTERNAL) != 0) {
        n++;
    }
    return n > 1;
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
    (void)depth;
    while (i > 0) {
        i--;
        if (strcmp(vars[i].name, name) == 0) {
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

static int vars_push(var_entry_t **vars, size_t *count, const char *name, cc_type_t type, int struct_id, long array_len,
                     int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS], int depth) {
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
    (*vars)[*count].array_len = array_len;
    (*vars)[*count].array_ndim = array_ndim;
    if (array_dims != NULL) {
        memcpy((*vars)[*count].array_dims, array_dims, sizeof((*vars)[*count].array_dims));
    } else {
        memset((*vars)[*count].array_dims, 0, sizeof((*vars)[*count].array_dims));
    }
    (*vars)[*count].depth = depth;
    (*count)++;
    return 0;
}

static int asm_operand_is_lvalue(const cc_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == CC_EXPR_IDENT || e->kind == CC_EXPR_DEREF || e->kind == CC_EXPR_MEMBER) {
        return 1;
    }
    return 0;
}

static int asm_valid_constraint_char(int ch) {
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
        return 1;
    }
    if (ch == '=' || ch == '+' || ch == '&' || ch == '%' || ch == '!' || ch == '*' || ch == '@' || ch == '?' ||
        ch == '^' || ch == '$' || ch == '<' || ch == '>' || ch == '#' || ch == '|' || ch == '~' || ch == ',') {
        return 1;
    }
    return 0;
}

static int asm_constraint_allows_memory(const char *c) {
    return c != NULL && (strchr(c, 'm') != NULL || strchr(c, 'o') != NULL || strchr(c, 'V') != NULL);
}

static int asm_constraint_allows_register(const char *c) {
    if (c == NULL) {
        return 0;
    }
    return strchr(c, 'r') != NULL || strchr(c, 'q') != NULL || strchr(c, 'a') != NULL || strchr(c, 'b') != NULL ||
           strchr(c, 'c') != NULL || strchr(c, 'd') != NULL || strchr(c, 'S') != NULL || strchr(c, 'D') != NULL;
}

static int asm_constraint_is_imm(const char *c) {
    size_t i;
    if (c == NULL)
        return 0;
    for (i = 0; c[i] != '\0'; ++i) {
        int ch = (unsigned char)c[i];
        if (ch == 'i' || ch == 'n')
            return 1;
        if (ch >= 'I' && ch <= 'P')
            return 1;
    }
    return 0;
}

static int asm_constraint_is_flag_output(const char *c) {
    return c != NULL && strncmp(c, "=@cc", 4) == 0;
}

static int asm_validate_constraint(const char *c, int is_output, size_t out_count, int pointer_size, cc_diag_t *diag) {
    size_t i;
    int has_class = 0;
    if (c == NULL || c[0] == '\0') {
        set_diag(diag, "asm operand constraint cannot be empty");
        return -1;
    }
    if (!is_output && c[0] >= '0' && c[0] <= '9') {
        long n = 0;
        for (i = 0; c[i] >= '0' && c[i] <= '9'; ++i) {
            n = n * 10 + (long)(c[i] - '0');
        }
        if (n < 0 || (size_t)n >= out_count) {
            set_diag(diag, "asm matching constraint references invalid output index");
            return -1;
        }
        if (c[i] != '\0') {
            set_diag(diag, "asm matching constraint must be a pure numeric index");
            return -1;
        }
        return 0;
    }
    if (is_output && strchr(c, '=') == NULL && strchr(c, '+') == NULL) {
        set_diag(diag, "asm output constraint must contain '=' or '+'");
        return -1;
    }
    for (i = 0; c[i] != '\0'; ++i) {
        int ch = (unsigned char)c[i];
        if (!asm_valid_constraint_char(ch)) {
            if (diag != NULL && diag->message[0] == '\0') {
                if (ch >= 32 && ch < 127) {
                    snprintf(diag->message, sizeof(diag->message),
                             "asm operand constraint contains unsupported character '%c' in \"%s\"", ch, c);
                } else {
                    snprintf(diag->message, sizeof(diag->message),
                             "asm operand constraint contains unsupported byte 0x%02x in \"%s\"", ch, c);
                }
            }
            return -1;
        }
    }
    if (strchr(c, 'q') != NULL && pointer_size != 4 && pointer_size != 8) {
        set_diag(diag, "asm 'q' constraint requires x86 target");
        return -1;
    }
    if (is_output && asm_constraint_is_flag_output(c)) {
        return 0;
    }
    has_class = asm_constraint_allows_memory(c) || asm_constraint_allows_register(c) || asm_constraint_is_imm(c);
    if (!has_class) {
        return 0;
    }
    return 0;
}

static int asm_find_named_operand(const cc_stmt_t *s, const char *name) {
    size_t i;
    for (i = 0; i < s->asm_output_count; ++i) {
        if (s->asm_outputs[i].name != NULL && strcmp(s->asm_outputs[i].name, name) == 0) {
            return 1;
        }
    }
    for (i = 0; i < s->asm_input_count; ++i) {
        if (s->asm_inputs[i].name != NULL && strcmp(s->asm_inputs[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int asm_find_goto_label(const cc_stmt_t *s, const char *name) {
    size_t i;
    for (i = 0; i < s->asm_goto_label_count; ++i) {
        if (s->asm_goto_labels[i] != NULL && strcmp(s->asm_goto_labels[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int asm_validate_template_refs(const cc_stmt_t *s, cc_diag_t *diag) {
    size_t i = 0;
    size_t total = s->asm_output_count + s->asm_input_count;
    const char *t = s->asm_template;
    if (t == NULL) {
        set_diag(diag, "asm statement missing template");
        return -1;
    }
    while (t[i] != '\0') {
        int has_modifier = 0;
        if (t[i] != '%') {
            i++;
            continue;
        }
        i++;
        if (t[i] == '%') {
            i++;
            continue;
        }
        if (((t[i] >= 'A' && t[i] <= 'Z') || (t[i] >= 'a' && t[i] <= 'z')) && t[i] != 'l' && t[i] != 'L') {
            has_modifier = 1;
            i++;
        }
        if (t[i] >= '0' && t[i] <= '9') {
            long n = 0;
            while (t[i] >= '0' && t[i] <= '9') {
                n = n * 10 + (long)(t[i] - '0');
                i++;
            }
            if (n < 0 || (size_t)n >= total) {
                set_diag(diag, "asm template operand index is out of range");
                return -1;
            }
            continue;
        }
        if (has_modifier && t[i] == '[') {
            size_t b = ++i;
            while (t[i] != '\0' && t[i] != ']') {
                i++;
            }
            if (t[i] != ']') {
                set_diag(diag, "asm template has unterminated named operand");
                return -1;
            }
            if (i == b) {
                set_diag(diag, "asm template has empty named operand");
                return -1;
            }
            {
                char *nm = (char *)malloc(i - b + 1);
                if (nm == NULL) {
                    set_diag(diag, "out of memory validating asm template");
                    return -1;
                }
                memcpy(nm, t + b, i - b);
                nm[i - b] = '\0';
                if (!asm_find_named_operand(s, nm)) {
                    free(nm);
                    set_diag(diag, "asm template references unknown named operand");
                    return -1;
                }
                free(nm);
            }
            i++;
            continue;
        }
        if (t[i] == 'l') {
            i++;
            if (t[i] >= '0' && t[i] <= '9') {
                long n = 0;
                while (t[i] >= '0' && t[i] <= '9') {
                    n = n * 10 + (long)(t[i] - '0');
                    i++;
                }
                if (n < 0 || (size_t)n >= s->asm_goto_label_count) {
                    set_diag(diag, "asm template goto-label index is out of range");
                    return -1;
                }
                continue;
            }
            if (t[i] == '[') {
                size_t b = ++i;
                while (t[i] != '\0' && t[i] != ']') {
                    i++;
                }
                if (t[i] != ']') {
                    set_diag(diag, "asm template has unterminated goto label");
                    return -1;
                }
                if (i == b) {
                    set_diag(diag, "asm template has empty goto label");
                    return -1;
                }
                {
                    char *nm = (char *)malloc(i - b + 1);
                    if (nm == NULL) {
                        set_diag(diag, "out of memory validating asm template");
                        return -1;
                    }
                    memcpy(nm, t + b, i - b);
                    nm[i - b] = '\0';
                    if (!asm_find_goto_label(s, nm)) {
                        free(nm);
                        set_diag(diag, "asm template references unknown goto label");
                        return -1;
                    }
                    free(nm);
                }
                i++;
                continue;
            }
            set_diag(diag, "asm template has malformed %l label reference");
            return -1;
        }
        if (t[i] == '[') {
            size_t b = ++i;
            while (t[i] != '\0' && t[i] != ']') {
                i++;
            }
            if (t[i] != ']') {
                set_diag(diag, "asm template has unterminated named operand");
                return -1;
            }
            if (i == b) {
                set_diag(diag, "asm template has empty named operand");
                return -1;
            }
            {
                char *nm = (char *)malloc(i - b + 1);
                if (nm == NULL) {
                    set_diag(diag, "out of memory validating asm template");
                    return -1;
                }
                memcpy(nm, t + b, i - b);
                nm[i - b] = '\0';
                if (!asm_find_named_operand(s, nm)) {
                    free(nm);
                    set_diag(diag, "asm template references unknown named operand");
                    return -1;
                }
                free(nm);
            }
            i++;
            continue;
        }
        if (diag != NULL && diag->message[0] == '\0') {
            unsigned char uc = (unsigned char)t[i];
            if (t[i] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "asm template has dangling '%%' reference");
            } else if (uc >= 32 && uc < 127) {
                snprintf(diag->message, sizeof(diag->message), "asm template has unsupported '%%%c' reference", t[i]);
            } else {
                snprintf(diag->message, sizeof(diag->message),
                         "asm template has unsupported '%%' reference byte 0x%02x", (unsigned)uc);
            }
        }
        return -1;
    }
    return 0;
}

static int vars_clone(var_entry_t **out, const var_entry_t *src, size_t count) {
    size_t i;
    var_entry_t *dst;

    *out = NULL;
    if (count == 0) {
        return 0;
    }
    dst = (var_entry_t *)calloc(count, sizeof(*dst));
    if (dst == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        dst[i] = src[i];
        dst[i].name = NULL;
        if (src[i].name != NULL) {
            dst[i].name = (char *)malloc(strlen(src[i].name) + 1);
            if (dst[i].name == NULL) {
                size_t j;
                for (j = 0; j < i; ++j) {
                    free(dst[j].name);
                }
                free(dst);
                return -1;
            }
            strcpy(dst[i].name, src[i].name);
        }
    }
    *out = dst;
    return 0;
}

static void vars_free(var_entry_t *vars, size_t count) {
    size_t i;
    if (vars == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        free(vars[i].name);
    }
    free(vars);
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

    case CC_STMT_ASM:
        for (i = 0; i < s->asm_goto_label_count; ++i) {
            const char *name = s->asm_goto_labels[i];
            if (name == NULL || name[0] == '\0') {
                set_diag(diag, "malformed asm goto label");
                return -1;
            }
            if (names_find(gotos->items, gotos->count, name) < 0 && name_list_push(gotos, name) != 0) {
                set_diag(diag, "out of memory collecting asm goto targets");
                return -1;
            }
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
    const cc_function_t *best = NULL;
    int best_score = -1;
    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *f = &tu->funcs[i];
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

static int maybe_warn_deprecated_symbol(const cc_translation_unit_t *tu, const char *name, size_t line, size_t col,
                                        cc_diag_t *diag) {
    char buf[192];
    const cc_function_t *f = find_function(tu, name);
    const cc_global_t *g = find_global(tu, name);

    if (f != NULL && (f->attr_flags & CC_ATTR_DEPRECATED) != 0) {
        snprintf(buf, sizeof(buf), "deprecated function used: %s", name != NULL ? name : "<anon>");
        return emit_required_warning(diag, line, col, buf);
    }
    if (g != NULL && (g->attr_flags & CC_ATTR_DEPRECATED) != 0) {
        snprintf(buf, sizeof(buf), "deprecated object used: %s", name != NULL ? name : "<anon>");
        return emit_required_warning(diag, line, col, buf);
    }
    return 0;
}

static const cc_struct_def_t *find_struct_def(const cc_translation_unit_t *tu, int struct_id) {
    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        return NULL;
    }
    return &tu->structs[struct_id];
}

static int type_carries_struct_id(cc_type_t t) {
    if (t == CC_TYPE_VOID) {
        return 1;
    }
    if (!cc_type_is_pointer(t)) {
        return 0;
    }
    return cc_type_pointer_base(t) == CC_TYPE_VOID;
}

static int struct_ids_compatible_depth(const cc_translation_unit_t *tu, int lhs, int rhs, int depth);

static int struct_members_compatible(const cc_translation_unit_t *tu, const cc_struct_def_t *lsd,
                                     const cc_struct_def_t *rsd, int depth) {
    size_t i;

    if (lsd == NULL || rsd == NULL) {
        return 0;
    }
    if (lsd->member_count != rsd->member_count) {
        return 0;
    }
    for (i = 0; i < lsd->member_count; ++i) {
        const cc_struct_member_t *lm = &lsd->members[i];
        const cc_struct_member_t *rm = &rsd->members[i];
        if (lm->name == NULL || rm->name == NULL) {
            return 0;
        }
        if (strcmp(lm->name, rm->name) != 0) {
            return 0;
        }
        if (lm->type != rm->type || lm->array_len != rm->array_len || lm->array_ndim != rm->array_ndim ||
            lm->offset != rm->offset || lm->size != rm->size) {
            return 0;
        }
        if (memcmp(lm->array_dims, rm->array_dims, sizeof(lm->array_dims)) != 0) {
            return 0;
        }
        if (type_carries_struct_id(lm->type)) {
            if (!struct_ids_compatible_depth(tu, lm->type_struct_id, rm->type_struct_id, depth + 1)) {
                return 0;
            }
        } else if (lm->type_struct_id != rm->type_struct_id) {
            return 0;
        }
    }
    return 1;
}

static int struct_ids_compatible_depth(const cc_translation_unit_t *tu, int lhs, int rhs, int depth) {
    const cc_struct_def_t *lsd;
    const cc_struct_def_t *rsd;

    if (lhs == rhs) {
        return 1;
    }
    if (lhs < 0 || rhs < 0 || depth > 32) {
        return 0;
    }
    lsd = find_struct_def(tu, lhs);
    rsd = find_struct_def(tu, rhs);
    if (lsd == NULL || rsd == NULL) {
        return 0;
    }
    if (lsd->is_union != rsd->is_union) {
        return 0;
    }
    if (lsd->tag != NULL && rsd->tag != NULL && strcmp(lsd->tag, rsd->tag) == 0 && lsd->depth == rsd->depth) {
        return 1;
    }
    if (!lsd->complete || !rsd->complete) {
        return 0;
    }
    if (lsd->size != rsd->size || lsd->align != rsd->align) {
        return 0;
    }
    return struct_members_compatible(tu, lsd, rsd, depth);
}

static int struct_ids_compatible(const cc_translation_unit_t *tu, int lhs, int rhs) {
    return struct_ids_compatible_depth(tu, lhs, rhs, 0);
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
    if (a->ret_type != b->ret_type || a->ret_struct_id != b->ret_struct_id) {
        return 0;
    }
    if (!a->has_prototype || !b->has_prototype) {
        if (a->is_variadic || b->is_variadic) {
            return 0;
        }
        return 1;
    }
    if (a->is_variadic != b->is_variadic || a->param_count != b->param_count) {
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
    return t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE || t == CC_TYPE_LDOUBLE || t == CC_TYPE_DECIMAL32 ||
           t == CC_TYPE_DECIMAL64 || t == CC_TYPE_DECIMAL128;
}

static int is_complex_type(cc_type_t t) {
    return t == CC_TYPE_COMPLEX || t == CC_TYPE_IMAGINARY;
}

static int is_pointer_type(cc_type_t t) {
    return cc_type_is_pointer(t);
}

static cc_type_t ptr_base_type(cc_type_t t);
static long type_size_bytes(cc_type_t t);

static int pointer_depth(cc_type_t t) {
    return (int)cc_type_pointer_depth(t);
}

static int is_unsigned_integral_type(cc_type_t t) {
    return t == CC_TYPE_UCHAR || t == CC_TYPE_USHORT || t == CC_TYPE_UINT || t == CC_TYPE_ULONG ||
           t == CC_TYPE_ULONG_LONG;
}

static int is_integral_type(cc_type_t t) {
    return t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_SCHAR || t == CC_TYPE_UCHAR || t == CC_TYPE_SHORT ||
           t == CC_TYPE_USHORT || t == CC_TYPE_INT || t == CC_TYPE_UINT || t == CC_TYPE_LONG || t == CC_TYPE_ULONG ||
           t == CC_TYPE_LONG_LONG || t == CC_TYPE_ULONG_LONG || t == CC_TYPE_ENUM || t == CC_TYPE_BITINT;
}

static int is_numeric_type(cc_type_t t) {
    return is_integral_type(t) || is_float_type(t) || is_complex_type(t);
}

static int is_scalar_type(cc_type_t t) {
    return is_numeric_type(t) || is_pointer_type(t);
}

static int is_array_object_type(cc_type_t t, long array_len, int array_ndim) {
    if (!is_pointer_type(t) || array_len < 0 || array_ndim <= 0) {
        return 0;
    }
    /*
     * Array declarators are encoded as pointer depth plus explicit array
     * rank metadata. Arrays whose element type is itself a pointer (including
     * function pointers in our lowered model) therefore have depth > rank.
     */
    return pointer_depth(t) >= array_ndim;
}

static void expr_clear_array_meta(cc_expr_t *e) {
    if (e == NULL) {
        return;
    }
    e->array_ndim = 0;
    memset(e->array_dims, 0, sizeof(e->array_dims));
}

static void expr_set_array_meta_decl(cc_expr_t *e, long array_len, int array_ndim,
                                     const long array_dims[CC_MAX_ARRAY_DIMS]) {
    int i;
    if (e->kind != CC_EXPR_CAST) {
        expr_clear_array_meta(e);
    }
    if (e == NULL || array_ndim <= 0 || array_dims == NULL) {
        return;
    }
    if (array_len >= 0) {
        e->array_ndim = array_ndim;
        memcpy(e->array_dims, array_dims, sizeof(e->array_dims));
        return;
    }
    if (array_ndim + 1 > CC_MAX_ARRAY_DIMS) {
        return;
    }
    e->array_ndim = array_ndim + 1;
    e->array_dims[0] = 0;
    for (i = 0; i < array_ndim; ++i) {
        e->array_dims[i + 1] = array_dims[i];
    }
}

static void expr_copy_array_meta(cc_expr_t *dst, const cc_expr_t *src) {
    if (dst == NULL) {
        return;
    }
    if (src == NULL) {
        expr_clear_array_meta(dst);
        return;
    }
    dst->array_ndim = src->array_ndim;
    memcpy(dst->array_dims, src->array_dims, sizeof(dst->array_dims));
}

static cc_type_t ptr_of_type(cc_type_t t) {
    return cc_type_make_pointer(t);
}

static cc_type_t ptr_base_type(cc_type_t t) {
    return cc_type_deref_once(t);
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
        if (is_integral_type(dbase) && is_integral_type(sbase) && type_size_bytes(dbase) == type_size_bytes(sbase)) {
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

static size_t utf8_seq_len(unsigned char b0) {
    if ((b0 & 0x80u) == 0) return 1;
    if ((b0 & 0xE0u) == 0xC0u) return 2;
    if ((b0 & 0xF0u) == 0xE0u) return 3;
    if ((b0 & 0xF8u) == 0xF0u) return 4;
    return 1;
}

static size_t decoded_string_unit_count(const cc_expr_t *e, int wide) {
    const unsigned char *s;
    size_t n = 0;
    size_t i = 0;
    size_t units = 0;

    if (e == NULL || e->kind != CC_EXPR_STR || e->ident == NULL) {
        return 0;
    }
    s = (const unsigned char *)e->ident;
    n = strlen(e->ident);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        i = 1;
        n -= 1;
    }
    while (i < n) {
        if (s[i] == '\\') {
            i++;
            if (i >= n) {
                break;
            }
            if (s[i] == 'x') {
                i++;
                while (i < n) {
                    unsigned char c = s[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                        break;
                    }
                    i++;
                }
            } else if (s[i] >= '0' && s[i] <= '7') {
                int k = 0;
                while (i < n && k < 3 && s[i] >= '0' && s[i] <= '7') {
                    i++;
                    k++;
                }
            } else {
                i++;
            }
            units++;
            continue;
        }
        if (!wide) {
            i++;
            units++;
            continue;
        }
        {
            size_t adv = utf8_seq_len(s[i]);
            if (adv == 0) adv = 1;
            if (i + adv > n) adv = 1;
            i += adv;
            units++;
        }
    }
    return units;
}

static int check_array_string_initializer(const char *name, cc_type_t array_type, long *array_len_io, cc_expr_t *init,
                                          long *inferred_len_out, cc_diag_t *diag) {
    cc_type_t elem_type;
    int wide;
    size_t units;
    long need;

    if (!is_pointer_type(array_type) || array_len_io == NULL || init == NULL || init->kind != CC_EXPR_STR) {
        return 1;
    }
    elem_type = ptr_base_type(array_type);
    wide = (init->aux_type == CC_TYPE_INT || init->aux_type == CC_TYPE_UINT || init->aux_type == CC_TYPE_LONG_LONG ||
            init->aux_type == CC_TYPE_ULONG_LONG);
    if (wide) {
        if (!is_integral_type(elem_type)) {
            return 1;
        }
    } else if (!(elem_type == CC_TYPE_CHAR || elem_type == CC_TYPE_UCHAR)) {
        return 1;
    }

    units = decoded_string_unit_count(init, wide);
    need = (long)units + 1;
    if (*array_len_io == 0) {
        *array_len_io = need;
        if (inferred_len_out != NULL) {
            *inferred_len_out = need;
        }
        return 0;
    }
    if (*array_len_io > 0 && (long)units > *array_len_io) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "string initializer too long for array %s",
                     name != NULL ? name : "<anon>");
        }
        return -1;
    }
    return 0;
}

static int generic_assoc_matches(cc_type_t assoc_type, int assoc_struct_id, cc_type_t ctrl_type, int ctrl_struct_id) {
    if (assoc_type == ctrl_type && assoc_struct_id == ctrl_struct_id) {
        return 1;
    }
    if (is_pointer_type(assoc_type) && is_pointer_type(ctrl_type)) {
        if (assoc_type == ctrl_type && (assoc_struct_id == ctrl_struct_id || assoc_struct_id < 0 || ctrl_struct_id < 0)) {
            return 1;
        }
        return 0;
    }
    return 0;
}

static int printf_like_format_index(const char *name, size_t *idx_out) {
    if (name == NULL || idx_out == NULL) {
        return 0;
    }
    if (strcmp(name, "printf") == 0 || strcmp(name, "vprintf") == 0) {
        *idx_out = 0;
        return 1;
    }
    if (strcmp(name, "fprintf") == 0 || strcmp(name, "sprintf") == 0 || strcmp(name, "vfprintf") == 0 ||
        strcmp(name, "vsprintf") == 0) {
        *idx_out = 1;
        return 1;
    }
    if (strcmp(name, "snprintf") == 0 || strcmp(name, "vsnprintf") == 0) {
        *idx_out = 2;
        return 1;
    }
    return 0;
}

static char *rewrite_printf_long_double_format(const char *literal) {
    size_t n;
    char *out;
    size_t i = 0;
    size_t o = 0;
    int changed = 0;
    if (literal == NULL) {
        return NULL;
    }
    n = strlen(literal);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    while (i < n) {
        if (literal[i] != '%') {
            out[o++] = literal[i++];
            continue;
        }
        out[o++] = literal[i++];
        if (i < n && literal[i] == '%') {
            out[o++] = literal[i++];
            continue;
        }
        while (i < n) {
            char c = literal[i];
            if (c == 'L' && i + 1 < n &&
                (literal[i + 1] == 'f' || literal[i + 1] == 'F' || literal[i + 1] == 'e' || literal[i + 1] == 'E' ||
                 literal[i + 1] == 'g' || literal[i + 1] == 'G' || literal[i + 1] == 'a' || literal[i + 1] == 'A')) {
                changed = 1;
                i++;
                continue;
            }
            out[o++] = c;
            i++;
            if (c == 'd' || c == 'i' || c == 'o' || c == 'u' || c == 'x' || c == 'X' || c == 'f' || c == 'F' ||
                c == 'e' || c == 'E' || c == 'g' || c == 'G' || c == 'a' || c == 'A' || c == 'c' || c == 's' ||
                c == 'p' || c == 'n') {
                break;
            }
        }
    }
    out[o] = '\0';
    if (!changed) {
        free(out);
        return NULL;
    }
    return out;
}

static const cc_struct_member_t *find_union_cast_member(const cc_translation_unit_t *tu, int struct_id,
                                                        const cc_expr_t *src_expr) {
    const cc_struct_def_t *sd;
    size_t i;
    if (tu == NULL || src_expr == NULL) {
        return NULL;
    }
    sd = find_struct_def(tu, struct_id);
    if (sd == NULL || !sd->complete || !sd->is_union) {
        return NULL;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (can_convert(m->type, src_expr->value_type) ||
            (is_pointer_type(m->type) && is_integral_type(src_expr->value_type) && is_null_ptr_constant(src_expr))) {
            return m;
        }
    }
    return NULL;
}

static int transparent_union_accepts_type(const cc_translation_unit_t *tu, int struct_id, cc_type_t src_type,
                                          int src_struct_id) {
    const cc_struct_def_t *sd;
    size_t i;
    if (tu == NULL || struct_id < 0) {
        return 0;
    }
    sd = find_struct_def(tu, struct_id);
    if (sd == NULL || !sd->complete || !sd->is_union) {
        return 0;
    }
    if ((sd->attr_flags & CC_ATTR_TRANSPARENT_UNION) == 0 && !is_pointer_type(src_type)) {
        return 0;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (!can_convert(m->type, src_type)) {
            continue;
        }
        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0 && src_type == CC_TYPE_VOID && src_struct_id >= 0 &&
            m->type_struct_id != src_struct_id) {
            continue;
        }
        return 1;
    }
    return 0;
}

static const cc_function_t *generic_selected_function(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    const cc_expr_t *sel;
    if (tu == NULL || e == NULL || e->kind != CC_EXPR_GENERIC || e->args == NULL || e->arg_count == 0) {
        return NULL;
    }
    if (e->generic_selected < 0 || (size_t)e->generic_selected >= e->arg_count) {
        return NULL;
    }
    sel = e->args[e->generic_selected];
    if (sel == NULL || sel->kind != CC_EXPR_IDENT || sel->ident == NULL) {
        return NULL;
    }
    return find_function(tu, sel->ident);
}

static const cc_function_t *infer_callee_function_expr(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    const cc_function_t *a;
    const cc_function_t *b;

    if (tu == NULL || e == NULL) {
        return NULL;
    }
    switch (e->kind) {
    case CC_EXPR_IDENT:
        if (e->ident == NULL) {
            return NULL;
        }
        return find_function(tu, e->ident);
    case CC_EXPR_ADDR:
    case CC_EXPR_DEREF:
    case CC_EXPR_CAST:
        return infer_callee_function_expr(tu, e->lhs);
    case CC_EXPR_TERNARY:
        if (e->rhs == NULL || e->third == NULL) {
            return NULL;
        }
        a = infer_callee_function_expr(tu, e->rhs);
        b = infer_callee_function_expr(tu, e->third);
        if (a == NULL || b == NULL) {
            return NULL;
        }
        if (a->ret_type != b->ret_type || a->ret_struct_id != b->ret_struct_id) {
            return NULL;
        }
        return a;
    case CC_EXPR_GENERIC:
        return generic_selected_function(tu, e);
    default:
        return NULL;
    }
}

static cc_type_t integral_promo_type(cc_type_t t) {
    if (t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_SCHAR || t == CC_TYPE_UCHAR || t == CC_TYPE_SHORT ||
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
    if (a == CC_TYPE_COMPLEX || b == CC_TYPE_COMPLEX) {
        return CC_TYPE_COMPLEX;
    }
    if (a == CC_TYPE_IMAGINARY || b == CC_TYPE_IMAGINARY) {
        return CC_TYPE_IMAGINARY;
    }
    if (a == CC_TYPE_DECIMAL128 || b == CC_TYPE_DECIMAL128) {
        return CC_TYPE_DECIMAL128;
    }
    if (a == CC_TYPE_DECIMAL64 || b == CC_TYPE_DECIMAL64) {
        return CC_TYPE_DECIMAL64;
    }
    if (a == CC_TYPE_DECIMAL32 || b == CC_TYPE_DECIMAL32) {
        return CC_TYPE_DECIMAL32;
    }
    if (a == CC_TYPE_LDOUBLE || b == CC_TYPE_LDOUBLE) {
        return CC_TYPE_LDOUBLE;
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
    if (ap == CC_TYPE_ULONG || bp == CC_TYPE_ULONG) {
        return CC_TYPE_ULONG;
    }
    if (ap == CC_TYPE_LONG || bp == CC_TYPE_LONG) {
        if (is_unsigned_integral_type(ap) || is_unsigned_integral_type(bp)) {
            return CC_TYPE_ULONG;
        }
        return CC_TYPE_LONG;
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
    BUILTIN_UNREACHABLE,
    BUILTIN_ASSUME,
    BUILTIN_ASSUME_ALIGNED,
    BUILTIN_UNPREDICTABLE,
    BUILTIN_CLZ,
    BUILTIN_CTZ,
    BUILTIN_POPCOUNT,
    BUILTIN_ADD_OVERFLOW,
    BUILTIN_SUB_OVERFLOW,
    BUILTIN_MUL_OVERFLOW,
    BUILTIN_ADD_OVERFLOW_P,
    BUILTIN_SUB_OVERFLOW_P,
    BUILTIN_MUL_OVERFLOW_P,
    BUILTIN_OBJECT_SIZE,
    BUILTIN_RETURN_ADDRESS,
    BUILTIN_FRAME_ADDRESS,
    BUILTIN_MEMCMP,
    BUILTIN_MEMCPY,
    BUILTIN_MEMMOVE,
    BUILTIN_MEMSET,
    BUILTIN_MEMCPY_CHK,
    BUILTIN_MEMMOVE_CHK,
    BUILTIN_MEMSET_CHK,
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
    if (strcmp(name, "__builtin_unreachable") == 0) {
        return BUILTIN_UNREACHABLE;
    }
    if (strcmp(name, "__builtin_assume") == 0) {
        return BUILTIN_ASSUME;
    }
    if (strcmp(name, "__builtin_assume_aligned") == 0) {
        return BUILTIN_ASSUME_ALIGNED;
    }
    if (strcmp(name, "__builtin_unpredictable") == 0) {
        return BUILTIN_UNPREDICTABLE;
    }
    if (strcmp(name, "__builtin_clz") == 0 || strcmp(name, "__builtin_clzl") == 0 ||
        strcmp(name, "__builtin_clzll") == 0) {
        return BUILTIN_CLZ;
    }
    if (strcmp(name, "__builtin_ctz") == 0 || strcmp(name, "__builtin_ctzl") == 0 ||
        strcmp(name, "__builtin_ctzll") == 0) {
        return BUILTIN_CTZ;
    }
    if (strcmp(name, "__builtin_popcount") == 0 || strcmp(name, "__builtin_popcountl") == 0 ||
        strcmp(name, "__builtin_popcountll") == 0) {
        return BUILTIN_POPCOUNT;
    }
    if (strcmp(name, "__builtin_add_overflow") == 0) {
        return BUILTIN_ADD_OVERFLOW;
    }
    if (strcmp(name, "__builtin_sub_overflow") == 0) {
        return BUILTIN_SUB_OVERFLOW;
    }
    if (strcmp(name, "__builtin_mul_overflow") == 0) {
        return BUILTIN_MUL_OVERFLOW;
    }
    if (strcmp(name, "__builtin_add_overflow_p") == 0) {
        return BUILTIN_ADD_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_sub_overflow_p") == 0) {
        return BUILTIN_SUB_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_mul_overflow_p") == 0) {
        return BUILTIN_MUL_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_object_size") == 0) {
        return BUILTIN_OBJECT_SIZE;
    }
    if (strcmp(name, "__builtin_return_address") == 0) {
        return BUILTIN_RETURN_ADDRESS;
    }
    if (strcmp(name, "__builtin_frame_address") == 0) {
        return BUILTIN_FRAME_ADDRESS;
    }
    if (strcmp(name, "__builtin_memcmp") == 0) {
        return BUILTIN_MEMCMP;
    }
    if (strcmp(name, "__builtin_memcpy") == 0) {
        return BUILTIN_MEMCPY;
    }
    if (strcmp(name, "__builtin_memmove") == 0) {
        return BUILTIN_MEMMOVE;
    }
    if (strcmp(name, "__builtin_memset") == 0) {
        return BUILTIN_MEMSET;
    }
    if (strcmp(name, "__builtin___memcpy_chk") == 0) {
        return BUILTIN_MEMCPY_CHK;
    }
    if (strcmp(name, "__builtin___memmove_chk") == 0) {
        return BUILTIN_MEMMOVE_CHK;
    }
    if (strcmp(name, "__builtin___memset_chk") == 0) {
        return BUILTIN_MEMSET_CHK;
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
    if (cc_type_is_pointer(t)) {
        return g_pointer_size_bytes;
    }
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
        return 1;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 2;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_FLOAT:
        return 4;
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
        return g_pointer_size_bytes;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_DOUBLE:
        return 8;
    case CC_TYPE_LDOUBLE:
        return 16;
    case CC_TYPE_ENUM:
        return 4;
    case CC_TYPE_COMPLEX:
        return 16;
    case CC_TYPE_IMAGINARY:
        return 8;
    case CC_TYPE_BITINT:
        return g_pointer_size_bytes;
    case CC_TYPE_DECIMAL32:
        return 4;
    case CC_TYPE_DECIMAL64:
        return 8;
    case CC_TYPE_DECIMAL128:
        return 16;
    case CC_TYPE_ATOMIC:
        return g_pointer_size_bytes;
    case CC_TYPE_FUNC:
        return g_pointer_size_bytes;
    default:
        return -1;
    }
}

static long type_size_bytes_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
    if (t == CC_TYPE_BITINT) {
        if (struct_id <= 0) {
            return -1;
        }
        return (struct_id + 7) / 8;
    }
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
            if (e->rhs == NULL) {
                *out = a;
                return 0;
            }
            return eval_const_int_expr(tu, e->rhs, out);
        }
        return eval_const_int_expr(tu, e->third, out);

    default:
        return -1;
    }
}

static int check_struct_initializer(const cc_translation_unit_t *tu, const char *name, int struct_id, cc_expr_t *init,
                                    var_entry_t *vars, size_t var_count, int depth, cc_diag_t *diag);
static int check_array_initializer(const cc_translation_unit_t *tu, const char *name, cc_type_t array_type,
                                   int array_struct_id, long array_len, int array_ndim, const long *array_dims,
                                   cc_expr_t *init, var_entry_t *vars,
                                   size_t var_count, int depth, long *out_inferred_len, cc_diag_t *diag);
static cc_expr_t *unwrap_scalar_initializer_expr(cc_expr_t *init, cc_diag_t *diag);
static int check_stmt(const cc_translation_unit_t *tu, cc_stmt_t *s, var_entry_t **vars, size_t *var_count, int depth,
                      cc_type_t fn_ret_type, int fn_ret_struct_id, int fn_attr_flags, int loop_depth, int switch_depth,
                      int *saw_return, cc_diag_t *diag);

static int check_expr(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count, int depth,
                      cc_diag_t *diag) {
    size_t i;

    if (e != NULL) {
        set_diag_context(e->line, e->col);
    }
    if (diag != NULL && diag->message[0] == '\0') {
        if (e != NULL && (e->line != 0 || e->col != 0)) {
            diag->line = e->line;
            diag->col = e->col;
        } else if (g_diag_ctx_line != 0 || g_diag_ctx_col != 0) {
            diag->line = g_diag_ctx_line;
            diag->col = g_diag_ctx_col;
        }
    }

    if (e == NULL) {
        set_diag(diag, "null expression in semantic analysis");
        return -1;
    }
    if (e->kind != CC_EXPR_CAST) {
        expr_clear_array_meta(e);
    }

    switch (e->kind) {
    case CC_EXPR_INT:
        if (!is_integral_type(e->value_type)) {
            e->value_type = CC_TYPE_INT;
        }
        e->struct_id = -1;
        return 0;

    case CC_EXPR_FLOAT:
        if (!is_float_type(e->value_type)) {
            e->value_type = CC_TYPE_DOUBLE;
        }
        e->struct_id = -1;
        return 0;

    case CC_EXPR_STR:
        if (e->aux_type == CC_TYPE_INT || e->aux_type == CC_TYPE_UINT || e->aux_type == CC_TYPE_LONG_LONG ||
            e->aux_type == CC_TYPE_ULONG_LONG) {
            e->value_type = CC_TYPE_PTR_INT;
        } else {
            e->value_type = CC_TYPE_PTR_CHAR;
        }
        e->struct_id = -1;
        return 0;

    case CC_EXPR_GENERIC: {
        long selected = -1;
        long default_idx = -1;
        long first_match = -1;
        size_t match_count = 0;

        if (e->lhs == NULL || e->args == NULL || e->arg_count == 0 || e->generic_count != e->arg_count ||
            e->generic_types == NULL || e->generic_struct_ids == NULL || e->generic_is_default == NULL) {
            set_diag(diag, "malformed _Generic expression");
            return -1;
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (e->generic_is_default[i]) {
                default_idx = (long)i;
                continue;
            }
            if (generic_assoc_matches(e->generic_types[i], e->generic_struct_ids[i], e->lhs->value_type,
                                      e->lhs->struct_id)) {
                if (first_match < 0) {
                    first_match = (long)i;
                }
                match_count++;
            }
        }
        if (match_count == 0) {
            selected = default_idx;
        } else if (match_count > 1 && is_pointer_type(e->lhs->value_type) && default_idx >= 0) {
            selected = default_idx;
        } else {
            selected = first_match;
        }
        if (selected < 0) {
            selected = 0;
        }
        e->generic_selected = selected;
        e->value_type = e->args[selected]->value_type;
        e->struct_id = e->args[selected]->struct_id;
        expr_copy_array_meta(e, e->args[selected]);
        return 0;
    }

    case CC_EXPR_IDENT: {
        int idx = vars_find_visible(vars, var_count, e->ident, depth);
        if (idx >= 0) {
            e->value_type = vars[idx].type;
            e->struct_id = vars[idx].struct_id;
            expr_set_array_meta_decl(e, vars[idx].array_len, vars[idx].array_ndim, vars[idx].array_dims);
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
                expr_set_array_meta_decl(e, g->array_len, g->array_ndim, g->array_dims);
                if (maybe_warn_deprecated_symbol(tu, e->ident, e->line, e->col, diag) != 0) {
                    return -1;
                }
                return 0;
            }
            if (find_function(tu, e->ident) != NULL) {
                e->value_type = CC_TYPE_PTR_VOID;
                e->struct_id = -1;
                if (maybe_warn_deprecated_symbol(tu, e->ident, e->line, e->col, diag) != 0) {
                    return -1;
                }
                return 0;
            }
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "use of undeclared identifier: %s",
                         e->ident != NULL ? e->ident : "<null>");
            }
            return -1;
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
                if (getenv("CC_DEBUG_SEMA_MEMBER") != NULL) {
                    fprintf(stderr, "cc-debug: stmt-expr-count=%zu\n", e->lhs->stmt_expr_count);
                    if (e->lhs->kind == CC_EXPR_STMT && e->lhs->stmt_expr_count > 0) {
                        const cc_stmt_t *ls = &e->lhs->stmt_expr_stmts[e->lhs->stmt_expr_count - 1];
                        fprintf(stderr, "cc-debug: stmt-expr tail kind=%d tail-expr-kind=%d tail-expr-type=%d\n",
                                (int)ls->kind, (ls->expr != NULL) ? (int)ls->expr->kind : -1,
                                (ls->expr != NULL) ? (int)ls->expr->value_type : -1);
                    }
                    fprintf(stderr,
                            "cc-debug: bad arrow lhs kind=%d type=%d struct_id=%d ident=%s member=%s line=%zu col=%zu\n",
                            (int)e->lhs->kind, (int)e->lhs->value_type, e->lhs->struct_id,
                            e->lhs->ident != NULL ? e->lhs->ident : "<null>", e->ident != NULL ? e->ident : "<null>",
                            e->line, e->col);
                }
                set_diag(diag, "-> requires pointer operand");
                return -1;
            }
            sid = e->lhs->struct_id;
            if (sid < 0) {
                set_diag(diag, "-> requires pointer to struct or union");
                return -1;
            }
        } else {
            sid = e->lhs->struct_id;
            if (sid < 0) {
                set_diag(diag, ". requires struct or union operand");
                return -1;
            }
        }
        m = find_struct_member(tu, sid, e->ident);
        if (m == NULL) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "no member named %s", e->ident);
            }
            return -1;
        }
        e->value_type = m->type;
        e->struct_id = m->type_struct_id;
        e->member_offset = m->offset;
        expr_set_array_meta_decl(e, m->array_len, m->array_ndim, m->array_dims);
        return 0;
    }

    case CC_EXPR_BIN:
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        e->struct_id = -1;
        if (e->op == CC_BIN_COMMA) {
            e->value_type = e->rhs->value_type;
            e->struct_id = e->rhs->struct_id;
            expr_copy_array_meta(e, e->rhs);
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
            e->value_type = integral_promo_type(e->lhs->value_type);
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
                if ((is_pointer_type(e->lhs->value_type) && is_integral_type(e->rhs->value_type)) ||
                    (is_pointer_type(e->rhs->value_type) && is_integral_type(e->lhs->value_type))) {
                    if (g_pedantic && emit_warning(diag, e->line, e->col,
                                                   "comparison between pointer and integer is a GNU extension", 1) != 0) {
                        return -1;
                    }
                    e->value_type = CC_TYPE_INT;
                    return 0;
                }
                if (getenv("CC_DEBUG_SEMA_CMP") != NULL) {
                    fprintf(stderr,
                            "cc-debug: eq/ne cmp lhs(kind=%d type=%d sid=%d ident=%s int=%ld) rhs(kind=%d type=%d sid=%d ident=%s int=%ld) line=%zu col=%zu\n",
                            (int)e->lhs->kind, (int)e->lhs->value_type, e->lhs->struct_id,
                            e->lhs->ident != NULL ? e->lhs->ident : "<null>", e->lhs->int_val, (int)e->rhs->kind,
                            (int)e->rhs->value_type, e->rhs->struct_id, e->rhs->ident != NULL ? e->rhs->ident : "<null>",
                            e->rhs->int_val, e->line, e->col);
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
            if (getenv("CC_DEBUG_SEMA_CMP") != NULL) {
                fprintf(stderr,
                        "cc-debug: ordered cmp lhs(kind=%d type=%d sid=%d ident=%s) rhs(kind=%d type=%d sid=%d ident=%s) line=%zu col=%zu\n",
                        (int)e->lhs->kind, (int)e->lhs->value_type, e->lhs->struct_id,
                        e->lhs->ident != NULL ? e->lhs->ident : "<null>", (int)e->rhs->kind, (int)e->rhs->value_type,
                        e->rhs->struct_id, e->rhs->ident != NULL ? e->rhs->ident : "<null>", e->line, e->col);
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
                expr_copy_array_meta(e, e->lhs);
                return 0;
            }
            if (e->op == CC_BIN_ADD && is_integral_type(e->lhs->value_type) && is_pointer_type(e->rhs->value_type)) {
                e->value_type = e->rhs->value_type;
                e->struct_id = e->rhs->struct_id;
                expr_copy_array_meta(e, e->rhs);
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
        cc_type_t indirect_callee_type = CC_TYPE_VOID;
        int indirect_callee_struct_id = -1;
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
            if (e->aux_type == CC_TYPE_VOID && e->aux_struct_id < 0) {
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
        if (bk == BUILTIN_UNREACHABLE) {
            if (e->arg_count != 0) {
                set_diag(diag, "__builtin_unreachable expects exactly 0 arguments");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_ASSUME) {
            if (e->arg_count != 1) {
                set_diag(diag, "__builtin_assume expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_scalar_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_assume argument must be scalar");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_ASSUME_ALIGNED) {
            if (e->arg_count < 2 || e->arg_count > 3) {
                set_diag(diag, "__builtin_assume_aligned expects 2 or 3 arguments");
                return -1;
            }
            for (i = 0; i < e->arg_count; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_assume_aligned first argument must be a pointer");
                return -1;
            }
            if (!is_integral_type(e->args[1]->value_type) ||
                (e->arg_count == 3 && !is_integral_type(e->args[2]->value_type))) {
                set_diag(diag, "__builtin_assume_aligned alignment/offset must be integral");
                return -1;
            }
            e->value_type = e->args[0]->value_type;
            e->struct_id = e->args[0]->struct_id;
            return 0;
        }
        if (bk == BUILTIN_UNPREDICTABLE) {
            if (e->arg_count != 1) {
                set_diag(diag, "__builtin_unpredictable expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_scalar_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_unpredictable argument must be scalar");
                return -1;
            }
            e->value_type = e->args[0]->value_type;
            e->struct_id = e->args[0]->struct_id;
            return 0;
        }
        if (bk == BUILTIN_CLZ || bk == BUILTIN_CTZ) {
            if (e->arg_count != 1) {
                set_diag(diag, bk == BUILTIN_CLZ ? "__builtin_clz expects exactly 1 argument"
                                                 : "__builtin_ctz expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_integral_type(e->args[0]->value_type)) {
                set_diag(diag, bk == BUILTIN_CLZ ? "__builtin_clz argument must be integral"
                                                 : "__builtin_ctz argument must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_POPCOUNT) {
            if (e->arg_count != 1) {
                set_diag(diag, "__builtin_popcount expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_integral_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_popcount argument must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_ADD_OVERFLOW || bk == BUILTIN_SUB_OVERFLOW || bk == BUILTIN_MUL_OVERFLOW) {
            cc_type_t out_elem;
            if (e->arg_count != 3) {
                set_diag(diag, "__builtin_*_overflow expects exactly 3 arguments");
                return -1;
            }
            for (i = 0; i < 3; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_pointer_type(e->args[2]->value_type)) {
                set_diag(diag, "overflow builtin third argument must be a pointer");
                return -1;
            }
            out_elem = ptr_base_type(e->args[2]->value_type);
            if (!is_integral_type(out_elem)) {
                set_diag(diag, "overflow builtin destination must point to an integer type");
                return -1;
            }
            if (!is_integral_type(e->args[0]->value_type) || !is_integral_type(e->args[1]->value_type)) {
                set_diag(diag, "overflow builtin operands must be integers");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_ADD_OVERFLOW_P || bk == BUILTIN_SUB_OVERFLOW_P || bk == BUILTIN_MUL_OVERFLOW_P) {
            if (e->arg_count != 3) {
                set_diag(diag, "__builtin_*_overflow_p expects exactly 3 arguments");
                return -1;
            }
            for (i = 0; i < 3; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_integral_type(e->args[0]->value_type) || !is_integral_type(e->args[1]->value_type) ||
                !is_integral_type(e->args[2]->value_type)) {
                set_diag(diag, "overflow_p builtin operands must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_OBJECT_SIZE) {
            if (e->arg_count != 2) {
                set_diag(diag, "__builtin_object_size expects exactly 2 arguments");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0 ||
                check_expr(tu, e->args[1], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_integral_type(e->args[1]->value_type)) {
                set_diag(diag, "__builtin_object_size second argument must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_ULONG_LONG;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_RETURN_ADDRESS || bk == BUILTIN_FRAME_ADDRESS) {
            if (e->arg_count != 1) {
                set_diag(diag, bk == BUILTIN_RETURN_ADDRESS ? "__builtin_return_address expects 1 argument"
                                                            : "__builtin_frame_address expects 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_integral_type(e->args[0]->value_type)) {
                set_diag(diag, bk == BUILTIN_RETURN_ADDRESS ? "__builtin_return_address argument must be integral"
                                                            : "__builtin_frame_address argument must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_PTR_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_MEMCMP) {
            if (e->arg_count != 3) {
                set_diag(diag, "__builtin_memcmp has wrong argument count");
                return -1;
            }
            for (i = 0; i < e->arg_count; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_pointer_type(e->args[0]->value_type) || !is_pointer_type(e->args[1]->value_type)) {
                set_diag(diag, "__builtin_memcmp first two arguments must be pointers");
                return -1;
            }
            if (!is_integral_type(e->args[2]->value_type)) {
                set_diag(diag, "__builtin_memcmp third argument must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMSET || bk == BUILTIN_MEMCPY_CHK ||
            bk == BUILTIN_MEMMOVE_CHK || bk == BUILTIN_MEMSET_CHK) {
            size_t min_args = (bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMSET) ? 3 : 4;
            if (e->arg_count < min_args) {
                set_diag(diag, min_args == 3 ? "__builtin_mem* has wrong argument count"
                                             : "__builtin___mem*_chk has wrong argument count");
                return -1;
            }
            for (i = 0; i < e->arg_count; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "memory builtin first argument must be a pointer");
                return -1;
            }
            if ((bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMCPY_CHK ||
                 bk == BUILTIN_MEMMOVE_CHK) &&
                !is_pointer_type(e->args[1]->value_type)) {
                set_diag(diag, "memory builtin source argument must be a pointer");
                return -1;
            }
            if ((bk == BUILTIN_MEMSET || bk == BUILTIN_MEMSET_CHK) && !is_integral_type(e->args[1]->value_type)) {
                set_diag(diag, "memory builtin fill byte must be integral");
                return -1;
            }
            if (!is_integral_type(e->args[2]->value_type)) {
                set_diag(diag, "memory builtin size argument must be integral");
                return -1;
            }
            e->value_type = e->args[0]->value_type;
            e->struct_id = e->args[0]->struct_id;
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
            if (e->lhs->kind == CC_EXPR_DEREF && e->lhs->lhs != NULL && is_pointer_type(e->lhs->lhs->value_type)) {
                indirect_callee_type = e->lhs->lhs->value_type;
                indirect_callee_struct_id = e->lhs->lhs->struct_id;
            } else if (is_pointer_type(e->lhs->value_type)) {
                indirect_callee_type = e->lhs->value_type;
                indirect_callee_struct_id = e->lhs->struct_id;
            } else {
                set_diag(diag, "call target must be a function pointer");
                return -1;
            }
        }
        if (callee != NULL && callee->has_prototype && !callee->is_variadic && e->arg_count != callee->param_count) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "call to %s has %zu args but %zu required", e->ident,
                         e->arg_count, callee->param_count);
            }
            return -1;
        }
        if (callee != NULL && callee->has_prototype && callee->is_variadic && e->arg_count < callee->param_count) {
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
            if (callee != NULL && callee->has_prototype && i < callee->param_count &&
                !can_convert(callee->params[i].type, e->args[i]->value_type) &&
                !(is_pointer_type(callee->params[i].type) && is_integral_type(e->args[i]->value_type) &&
                  is_null_ptr_constant(e->args[i])) &&
                !(callee->params[i].type == CC_TYPE_VOID && callee->params[i].type_struct_id >= 0 &&
                  transparent_union_accepts_type(tu, callee->params[i].type_struct_id, e->args[i]->value_type,
                                                 e->args[i]->struct_id)) &&
                !(callee->params[i].type == CC_TYPE_VOID && callee->params[i].type_struct_id >= 0 &&
                  e->args[i]->kind == CC_EXPR_STMT && is_integral_type(e->args[i]->value_type))) {
                if (getenv("CC_DEBUG_CALL_ARGS") != NULL) {
                    fprintf(stderr,
                            "cc-debug: call %s arg%zu expect(type=%d sid=%d) got(type=%d sid=%d)\n",
                            e->ident != NULL ? e->ident : "<indirect>", i + 1, (int)callee->params[i].type,
                            callee->params[i].type_struct_id, (int)e->args[i]->value_type, e->args[i]->struct_id);
                }
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "cannot convert arg %zu in call to %s", i + 1,
                             e->ident);
                }
                return -1;
            }
        }
        if (e->ident != NULL) {
            size_t fmt_i;
            if (printf_like_format_index(e->ident, &fmt_i) && fmt_i < e->arg_count && e->args[fmt_i] != NULL &&
                e->args[fmt_i]->kind == CC_EXPR_STR && e->args[fmt_i]->ident != NULL) {
                char *rw = rewrite_printf_long_double_format(e->args[fmt_i]->ident);
                if (rw != NULL) {
                    free(e->args[fmt_i]->ident);
                    e->args[fmt_i]->ident = rw;
                }
            }
        }
        if (callee != NULL && callee->has_prototype && (callee->attr_flags & CC_ATTR_NONNULL) != 0) {
            /*
             * We currently record only the presence of nonnull, not its index list.
             * Avoid false positives (e.g. strtok uses nonnull(2)) by checking
             * only unambiguous single-parameter prototypes.
             */
            if (e->arg_count > 0 && callee->param_count == 1 && is_pointer_type(callee->params[0].type) &&
                is_null_ptr_constant(e->args[0])) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "nonnull argument 1 in call to %s is null",
                             e->ident != NULL ? e->ident : "<indirect>");
                }
                return -1;
            }
        }
        if (callee != NULL) {
            e->value_type = callee->ret_type;
            e->struct_id = callee->ret_struct_id;
            if ((callee->attr_flags & CC_ATTR_DEPRECATED) != 0) {
                char buf[192];
                snprintf(buf, sizeof(buf), "deprecated function used: %s", callee->name != NULL ? callee->name : "<anon>");
                if (emit_required_warning(diag, e->line, e->col, buf) != 0) {
                    return -1;
                }
            }
        } else if (e->ident != NULL) {
            int idx = vars_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                if (!is_pointer_type(vars[idx].type)) {
                    set_diag(diag, "call target must be a function pointer");
                    return -1;
                }
                e->value_type = ptr_base_type(vars[idx].type);
                e->struct_id = vars[idx].struct_id;
                return 0;
            } else {
                const cc_global_t *g = find_global(tu, e->ident);
                if (g != NULL) {
                    if (!is_pointer_type(g->type)) {
                        set_diag(diag, "call target must be a function pointer");
                        return -1;
                    }
                    e->value_type = ptr_base_type(g->type);
                    e->struct_id = g->type_struct_id;
                    return 0;
                }
            }
            if (strncmp(e->ident, "__builtin_", 10) == 0) {
                /* Fallback for unimplemented builtin signatures. */
                e->value_type = CC_TYPE_INT;
                e->struct_id = -1;
                return 0;
            }
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
            const cc_function_t *gfn = generic_selected_function(tu, e->lhs);
            if (gfn != NULL) {
                e->value_type = gfn->ret_type;
                e->struct_id = gfn->ret_struct_id;
            } else {
                const cc_function_t *ifn = infer_callee_function_expr(tu, e->lhs);
                if (ifn != NULL) {
                    e->value_type = ifn->ret_type;
                    e->struct_id = ifn->ret_struct_id;
                    return 0;
                }
                e->value_type = ptr_base_type(indirect_callee_type);
                e->struct_id = -1;
                if (indirect_callee_struct_id >= 0 &&
                    (e->value_type == CC_TYPE_VOID || is_pointer_type(e->value_type))) {
                    e->struct_id = indirect_callee_struct_id;
                }
            }
        }
        return 0;
    }

    case CC_EXPR_ASSIGN: {
        cc_type_t dst_type;
        int dst_struct_id = -1;
        int dst_is_array_object = 0;
        int assign_ok;
        if (e->ident != NULL) {
            int idx = vars_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                dst_type = vars[idx].type;
                dst_struct_id = vars[idx].struct_id;
                dst_is_array_object = is_array_object_type(vars[idx].type, vars[idx].array_len, vars[idx].array_ndim);
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
                dst_is_array_object = is_array_object_type(g->type, g->array_len, g->array_ndim);
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
        if (dst_is_array_object) {
            if (diag != NULL && diag->message[0] == '\0') {
                if (e->ident != NULL) {
                    snprintf(diag->message, sizeof(diag->message), "array object is not a modifiable lvalue: %s",
                             e->ident);
                } else {
                    snprintf(diag->message, sizeof(diag->message), "array object is not a modifiable lvalue");
                }
            }
            return -1;
        }
        if (check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        assign_ok = can_convert(dst_type, e->rhs->value_type) ||
                    (is_pointer_type(dst_type) && is_integral_type(e->rhs->value_type) && is_null_ptr_constant(e->rhs));
        if (!assign_ok && dst_type == CC_TYPE_VOID && dst_struct_id >= 0 && e->rhs->value_type == CC_TYPE_VOID &&
            struct_ids_compatible(tu, dst_struct_id, e->rhs->struct_id)) {
            assign_ok = 1;
        }
        if (!assign_ok && dst_type == CC_TYPE_VOID && dst_struct_id >= 0 && is_integral_type(e->rhs->value_type) &&
            e->rhs->kind == CC_EXPR_STMT) {
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
        if (e->ident != NULL) {
            int idx = vars_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                expr_set_array_meta_decl(e, vars[idx].array_len, vars[idx].array_ndim, vars[idx].array_dims);
            } else {
                const cc_global_t *g = find_global(tu, e->ident);
                if (g != NULL) {
                    expr_set_array_meta_decl(e, g->array_len, g->array_ndim, g->array_dims);
                }
            }
        } else {
            expr_copy_array_meta(e, e->lhs);
        }
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
            expr_copy_array_meta(e, e->lhs->lhs);
            return 0;
        }
        if (e->lhs->kind == CC_EXPR_IDENT && e->lhs->ident != NULL) {
            int vidx = vars_find_visible(vars, var_count, e->lhs->ident, depth);
            if (vidx < 0 && find_function(tu, e->lhs->ident) != NULL) {
                e->value_type = e->lhs->value_type;
                e->struct_id = e->lhs->struct_id;
                return 0;
            }
        }
        if (e->lhs->kind == CC_EXPR_CAST && e->lhs->value_type == CC_TYPE_VOID && e->lhs->struct_id >= 0) {
            e->value_type = CC_TYPE_PTR_VOID;
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
        if (e->lhs->array_ndim > 0 && is_pointer_type(e->value_type)) {
            int i;
            e->array_ndim = e->lhs->array_ndim - 1;
            if (e->array_ndim < 0) {
                e->array_ndim = 0;
            }
            for (i = 0; i < e->array_ndim; ++i) {
                e->array_dims[i] = e->lhs->array_dims[i + 1];
            }
            for (; i < CC_MAX_ARRAY_DIMS; ++i) {
                e->array_dims[i] = 0;
            }
        }
        if (e->value_type == CC_TYPE_VOID) {
            if (e->struct_id >= 0) {
                return 0;
            }
            if (g_pedantic) {
                if (emit_warning(diag, e->line, e->col, "dereferencing 'void *' is a GNU extension", 1) != 0) {
                    return -1;
                }
            }
            return 0;
        }
        return 0;

    case CC_EXPR_UPDATE: {
        cc_type_t t;
        int t_struct_id = -1;
        int is_array_object = 0;
        if (e->ident != NULL) {
            int idx = vars_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                t = vars[idx].type;
                t_struct_id = vars[idx].struct_id;
                is_array_object = is_array_object_type(vars[idx].type, vars[idx].array_len, vars[idx].array_ndim);
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
                t_struct_id = g->type_struct_id;
                is_array_object = is_array_object_type(g->type, g->array_len, g->array_ndim);
            }
        } else if (e->lhs != NULL && (e->lhs->kind == CC_EXPR_DEREF || e->lhs->kind == CC_EXPR_MEMBER)) {
            if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            t = e->lhs->value_type;
            t_struct_id = e->lhs->struct_id;
        } else {
            set_diag(diag, "++/-- target must be identifier, dereference, or member lvalue");
            return -1;
        }
        if (is_array_object) {
            set_diag(diag, "array object is not a modifiable lvalue for ++/--");
            return -1;
        }
        if (is_pointer_type(t)) {
            /* GNU-style extension: treat void* increments as byte-wise. */
        } else if (!is_numeric_type(t)) {
            set_diag(diag, "++/-- currently require numeric or pointer scalar operands");
            return -1;
        }
        e->value_type = t;
        if (is_pointer_type(t) && t_struct_id >= 0) {
            e->struct_id = t_struct_id;
        } else {
            e->struct_id = -1;
        }
        return 0;
    }

    case CC_EXPR_CAST:
        if (!(e->lhs != NULL && e->lhs->kind == CC_EXPR_INIT_LIST && e->array_ndim > 0 && is_pointer_type(e->aux_type))) {
            expr_clear_array_meta(e);
        }
        if (e->lhs == NULL) {
            set_diag(diag, "malformed cast expression");
            return -1;
        }
        if (e->lhs->kind == CC_EXPR_INIT_LIST) {
            if (e->array_ndim > 0 && is_pointer_type(e->aux_type)) {
                long cast_array_len = e->array_dims[0] > 0 ? e->array_dims[0] : 0;
                long inferred_len = -1;
                if (check_array_initializer(tu, "<compound-literal>", e->aux_type, e->aux_struct_id, cast_array_len,
                                            e->array_ndim, e->array_dims, e->lhs, vars, var_count, depth, &inferred_len,
                                            diag) != 0) {
                    return -1;
                }
                if (cast_array_len == 0 && inferred_len > 0) {
                    e->array_dims[0] = inferred_len;
                }
                e->value_type = e->aux_type;
                e->struct_id = e->aux_struct_id;
                return 0;
            }
            if (is_pointer_type(e->aux_type)) {
                long inferred_len = -1;
                long fallback_dims[CC_MAX_ARRAY_DIMS] = {0, 0, 0, 0};
                if (check_array_initializer(tu, "<compound-literal>", e->aux_type, e->aux_struct_id, 0, 1, fallback_dims,
                                            e->lhs, vars, var_count, depth, &inferred_len, diag) != 0) {
                    return -1;
                }
                e->value_type = e->aux_type;
                e->struct_id = e->aux_struct_id;
                return 0;
            }
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
            if (e->aux_struct_id >= 0) {
                const cc_struct_def_t *sd = find_struct_def(tu, e->aux_struct_id);
                if (sd == NULL || !sd->complete) {
                    set_diag(diag, "cast targets unknown aggregate type");
                    return -1;
                }
                if (sd->is_union && e->lhs->kind != CC_EXPR_INIT_LIST) {
                    const cc_struct_member_t *m = find_union_cast_member(tu, e->aux_struct_id, e->lhs);
                    if (m == NULL) {
                        set_diag(diag, "GNU union cast has no compatible destination member");
                        return -1;
                    }
                    e->member_offset = m->offset;
                    e->value_type = CC_TYPE_VOID;
                    e->struct_id = e->aux_struct_id;
                    return 0;
                }
                e->value_type = CC_TYPE_VOID;
                e->struct_id = e->aux_struct_id;
                return 0;
            }
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
            if (is_pointer_type(e->lhs->value_type)) {
                expr_copy_array_meta(e, e->lhs);
            }
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

    case CC_EXPR_STMT:
        {
            var_entry_t *lvars = NULL;
            size_t lcount = var_count;
            int saw_return = 0;
            size_t j;
            if (emit_warning(diag, e->line, e->col, "statement expression is a GNU extension (non-C99)", 1) != 0) {
                return -1;
            }
            if (vars_clone(&lvars, vars, var_count) != 0) {
                set_diag(diag, "out of memory cloning scope for statement expression");
                return -1;
            }
            for (j = 0; j < e->stmt_expr_count; ++j) {
                if (check_stmt(tu, &e->stmt_expr_stmts[j], &lvars, &lcount, depth + 1, CC_TYPE_INT, -1, 0, 0, 0,
                               &saw_return, diag) != 0) {
                    vars_free(lvars, lcount);
                    return -1;
                }
            }
            if (e->stmt_expr_count > 0) {
                const cc_stmt_t *last = &e->stmt_expr_stmts[e->stmt_expr_count - 1];
                if (last->kind == CC_STMT_EXPR && last->expr != NULL) {
                    e->value_type = last->expr->value_type;
                    e->struct_id = last->expr->struct_id;
                } else {
                    e->value_type = CC_TYPE_VOID;
                    e->struct_id = -1;
                }
            } else {
                e->value_type = CC_TYPE_VOID;
                e->struct_id = -1;
            }
            vars_free(lvars, lcount);
            return 0;
        }

    case CC_EXPR_TERNARY:
        {
            int lhs_is_plain_void;
            int rhs_is_plain_void;
            int third_is_plain_void;
        if (e->lhs == NULL || e->third == NULL) {
            set_diag(diag, "malformed conditional expression");
            return -1;
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0 ||
            (e->rhs != NULL && check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) ||
            check_expr(tu, e->third, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        lhs_is_plain_void = (e->lhs->value_type == CC_TYPE_VOID && e->lhs->struct_id < 0);
        rhs_is_plain_void = (e->rhs != NULL && e->rhs->value_type == CC_TYPE_VOID && e->rhs->struct_id < 0);
        third_is_plain_void = (e->third->value_type == CC_TYPE_VOID && e->third->struct_id < 0);
        if (e->lhs->value_type == CC_TYPE_VOID) {
            set_diag(diag, "conditional expression condition cannot be void");
            return -1;
        }
        if (e->rhs == NULL) {
            if (emit_warning(diag, e->line, e->col, "omitted middle operand in ?: is a GNU extension (non-C99)", 1) !=
                0) {
                return -1;
            }
            if (lhs_is_plain_void || third_is_plain_void) {
                if (lhs_is_plain_void && third_is_plain_void) {
                    e->value_type = CC_TYPE_VOID;
                    e->struct_id = -1;
                    return 0;
                }
                if (emit_warning(diag, e->line, e->col,
                                 "conditional expression with one void arm is a GNU extension (non-C99)", 1) != 0) {
                    return -1;
                }
                if (lhs_is_plain_void) {
                    e->value_type = e->third->value_type;
                    e->struct_id = e->third->struct_id;
                } else {
                    e->value_type = e->lhs->value_type;
                    e->struct_id = e->lhs->struct_id;
                }
                return 0;
            }
            if (is_pointer_type(e->lhs->value_type) && is_pointer_type(e->third->value_type)) {
                if (can_convert(e->lhs->value_type, e->third->value_type)) {
                    e->value_type = e->lhs->value_type;
                    e->struct_id = e->lhs->struct_id;
                    return 0;
                }
                if (can_convert(e->third->value_type, e->lhs->value_type)) {
                    e->value_type = e->third->value_type;
                    e->struct_id = e->third->struct_id;
                    return 0;
                }
                set_diag(diag, "incompatible pointer types in conditional expression");
                return -1;
            }
            if (e->lhs->value_type == CC_TYPE_VOID && e->lhs->struct_id >= 0 && e->third->value_type == CC_TYPE_VOID &&
                e->third->struct_id >= 0) {
                if (!struct_ids_compatible(tu, e->lhs->struct_id, e->third->struct_id)) {
                    set_diag(diag, "incompatible struct types in conditional expression");
                    return -1;
                }
                e->value_type = CC_TYPE_VOID;
                e->struct_id = e->lhs->struct_id;
                return 0;
            }
            e->value_type = common_arith_type(e->lhs->value_type, e->third->value_type);
            if (e->value_type == CC_TYPE_VOID) {
                set_diag(diag, "incompatible types in conditional expression");
                return -1;
            }
            e->struct_id = -1;
            return 0;
        }
        if (rhs_is_plain_void || third_is_plain_void) {
            if (rhs_is_plain_void && third_is_plain_void) {
                e->value_type = CC_TYPE_VOID;
                e->struct_id = -1;
                return 0;
            }
            if (emit_warning(diag, e->line, e->col,
                             "conditional expression with one void arm is a GNU extension (non-C99)", 1) != 0) {
                return -1;
            }
            if (rhs_is_plain_void) {
                e->value_type = e->third->value_type;
                e->struct_id = e->third->struct_id;
            } else {
                e->value_type = e->rhs->value_type;
                e->struct_id = e->rhs->struct_id;
            }
            return 0;
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
        if (e->rhs->value_type == CC_TYPE_VOID && e->rhs->struct_id >= 0 && e->third->value_type == CC_TYPE_VOID &&
            e->third->struct_id >= 0) {
            if (!struct_ids_compatible(tu, e->rhs->struct_id, e->third->struct_id)) {
                set_diag(diag, "incompatible struct types in conditional expression");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = e->rhs->struct_id;
            return 0;
        }
        e->value_type = common_arith_type(e->rhs->value_type, e->third->value_type);
        if (e->value_type == CC_TYPE_VOID) {
            set_diag(diag, "incompatible types in conditional expression");
            return -1;
        }
        e->struct_id = -1;
        return 0;
        }

    default:
        set_diag(diag, "unsupported expression kind");
        return -1;
    }
}

static int __attribute__((unused)) is_zero_initializer_expr(const cc_expr_t *e) {
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
        if (g_std_c23) {
            init->kind = CC_EXPR_INT;
            init->int_val = 0;
            init->value_type = CC_TYPE_INT;
            init->struct_id = -1;
            return init;
        }
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

static size_t struct_next_init_member(const cc_struct_def_t *sd, size_t member_idx) {
    size_t next = member_idx + 1;
    long off;
    if (sd == NULL || member_idx >= sd->member_count) {
        return next;
    }
    if (sd->members[member_idx].size == 0) {
        return next;
    }
    off = sd->members[member_idx].offset;
    while (next < sd->member_count && sd->members[next].offset == off) {
        next++;
    }
    return next;
}

static int struct_has_aggregate_member(const cc_translation_unit_t *tu, int struct_id) {
    const cc_struct_def_t *sd;
    size_t i;
    (void)tu;
    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        return 0;
    }
    sd = &tu->structs[struct_id];
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            return 1;
        }
        if (is_array_object_type(m->type, m->array_len, m->array_ndim)) {
            return 1;
        }
    }
    return 0;
}

static long struct_scalar_slots(const cc_translation_unit_t *tu, int struct_id, int depth) {
    const cc_struct_def_t *sd;
    size_t i;
    long slots = 0;
    if (depth > 32 || tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        return -1;
    }
    sd = &tu->structs[struct_id];
    if (sd->is_union) {
        return 1;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (sd->has_flexible_array && i + 1 == sd->member_count) {
            continue;
        }
        if (is_array_object_type(m->type, m->array_len, m->array_ndim)) {
            long cnt = m->array_len > 0 ? m->array_len : 1;
            cc_type_t elem = ptr_base_type(m->type);
            if (elem == CC_TYPE_VOID && m->type_struct_id >= 0) {
                long sub = struct_scalar_slots(tu, m->type_struct_id, depth + 1);
                if (sub < 0) {
                    return -1;
                }
                if (slots > LONG_MAX - cnt * sub) {
                    return -1;
                }
                slots += cnt * sub;
            } else {
                if (slots > LONG_MAX - cnt) {
                    return -1;
                }
                slots += cnt;
            }
            continue;
        }
        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            long sub = struct_scalar_slots(tu, m->type_struct_id, depth + 1);
            if (sub < 0) {
                return -1;
            }
            if (slots > LONG_MAX - sub) {
                return -1;
            }
            slots += sub;
            continue;
        }
        if (slots == LONG_MAX) {
            return -1;
        }
        slots++;
    }
    if (slots <= 0) {
        slots = 1;
    }
    return slots;
}

static long infer_struct_array_len_from_init(const cc_translation_unit_t *tu, int struct_id, cc_expr_t *init) {
    long slots;
    size_t cur = 0;
    long elems = 0;
    if (tu == NULL || init == NULL || init->kind != CC_EXPR_INIT_LIST) {
        return -1;
    }
    slots = struct_scalar_slots(tu, struct_id, 0);
    if (slots <= 0) {
        return -1;
    }
    while (cur < init->arg_count) {
        cc_expr_t *raw = init->args[cur];
        if (raw != NULL && raw->kind == CC_EXPR_INIT_LIST) {
            elems++;
            cur++;
            continue;
        }
        if (raw != NULL && raw->kind == CC_EXPR_CAST && raw->aux_type == CC_TYPE_VOID && raw->aux_struct_id == struct_id &&
            raw->lhs != NULL && raw->lhs->kind == CC_EXPR_INIT_LIST) {
            elems++;
            cur++;
            continue;
        }
        {
            long consumed = 0;
            while (cur < init->arg_count && consumed < slots) {
                cc_expr_t *it = init->args[cur];
                if (consumed > 0 && it != NULL && it->kind == CC_EXPR_INIT_LIST) {
                    break;
                }
                consumed++;
                cur++;
            }
            if (consumed == 0) {
                cur++;
            }
            elems++;
        }
    }
    return elems > 0 ? elems : 1;
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
    (void)sd;
    for (i = 0; i < init->arg_count; ++i) {
        cc_expr_t *raw = init->args[i];
        cc_expr_t *item = raw;
        const cc_struct_member_t *m;
        size_t member_idx = next_member;

        if (raw != NULL && raw->kind == CC_EXPR_MEMBER && raw->lhs == NULL && raw->rhs != NULL && raw->ident != NULL) {
            int didx = find_struct_member_index(tu, struct_id, raw->ident);
            if (didx < 0) {
                if (raw->rhs != NULL &&
                    (raw->rhs->kind == CC_EXPR_INIT_LIST ||
                     (raw->rhs->kind == CC_EXPR_CAST && raw->rhs->lhs != NULL &&
                      raw->rhs->lhs->kind == CC_EXPR_INIT_LIST))) {
                    /*
                     * GNU-style nested aggregate initializers can legally route
                     * designators through anonymous wrappers we don't model
                     * precisely yet. Accept and continue conservatively.
                     */
                    continue;
                }
                if (name != NULL && strcmp(name, "<compound-literal>") == 0) {
                    continue;
                }
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
            if (!sd->is_union && struct_has_aggregate_member(tu, struct_id)) {
                member_idx = sd->member_count - 1;
            } else {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "too many initializers for struct %s",
                             name != NULL ? name : "<anon>");
                }
                return -1;
            }
        }

        m = &sd->members[member_idx];
        if (!sd->is_union) {
            next_member = struct_next_init_member(sd, member_idx);
        }
        if (sd->has_flexible_array && member_idx + 1 == sd->member_count) {
            /* GNU-compatible extension: accept and ignore flexible-array initializers. */
            continue;
        }
        if (is_array_object_type(m->type, m->array_len, m->array_ndim)) {
            cc_type_t elem_type = ptr_base_type(m->type);
            if (item->kind == CC_EXPR_INIT_LIST) {
                if (check_array_initializer(tu, name, m->type, m->type_struct_id, m->array_len, m->array_ndim,
                                            m->array_dims, item, vars, var_count, depth, NULL, diag) != 0) {
                    return -1;
                }
                continue;
            }
            if (check_expr(tu, item, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!can_convert(elem_type, item->value_type) &&
                !(is_pointer_type(elem_type) && is_integral_type(item->value_type) && is_null_ptr_constant(item))) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "cannot convert array member initializer %zu for %s",
                             i, name != NULL ? name : "<anon>");
                }
                return -1;
            }
            continue;
        }
        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            cc_expr_t *nested = NULL;
            if (item->kind == CC_EXPR_INIT_LIST) {
                nested = item;
            } else if (item->kind == CC_EXPR_CAST && item->aux_type == CC_TYPE_VOID &&
                       item->aux_struct_id == m->type_struct_id && item->lhs != NULL &&
                       item->lhs->kind == CC_EXPR_INIT_LIST) {
                nested = item->lhs;
            }
            if (nested != NULL &&
                check_struct_initializer(tu, name, m->type_struct_id, nested, vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (nested == NULL && check_expr(tu, item, vars, var_count, depth, diag) != 0) {
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
                                   int array_struct_id, long array_len, int array_ndim, const long *array_dims,
                                   cc_expr_t *init, var_entry_t *vars,
                                   size_t var_count, int depth, long *out_inferred_len, cc_diag_t *diag) {
    size_t i;
    cc_type_t elem_type;
    int saw_nonlist_struct_item = 0;

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
        if (array_ndim > 1) {
            long child_len = 0;
            if (array_dims != NULL && array_ndim - 1 < CC_MAX_ARRAY_DIMS) {
                child_len = array_dims[1];
            }
            if (item->kind != CC_EXPR_INIT_LIST) {
                /*
                 * Accept brace-elided multidimensional initializers and
                 * leave precise element shaping to lowering, which already
                 * flattens with a cursor model.
                 */
                if (check_expr(tu, item, vars, var_count, depth, diag) != 0) {
                    return -1;
                }
                continue;
            }
            if (check_array_initializer(tu, name, elem_type, array_struct_id, child_len, array_ndim - 1,
                                        array_dims != NULL ? array_dims + 1 : NULL, item, vars, var_count, depth, NULL,
                                        diag) != 0) {
                return -1;
            }
            continue;
        }
        if (elem_type == CC_TYPE_VOID && array_struct_id >= 0) {
            if (item->kind != CC_EXPR_INIT_LIST) {
                /*
                 * Accept brace-elided aggregate initializers and defer strict
                 * element shaping to lowering, which has full type/size context.
                 */
                if (check_expr(tu, item, vars, var_count, depth, diag) != 0) {
                    return -1;
                }
                saw_nonlist_struct_item = 1;
                continue;
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
        if (elem_type == CC_TYPE_VOID && array_struct_id >= 0 && saw_nonlist_struct_item) {
            long inferred = infer_struct_array_len_from_init(tu, array_struct_id, init);
            *out_inferred_len = inferred > 0 ? inferred : 0;
        } else {
            *out_inferred_len = (long)init->arg_count;
        }
    }
    return 0;
}

static int check_stmt(const cc_translation_unit_t *tu, cc_stmt_t *s, var_entry_t **vars, size_t *var_count, int depth,
                      cc_type_t fn_ret_type, int fn_ret_struct_id, int fn_attr_flags, int loop_depth, int switch_depth,
                      int *saw_return, cc_diag_t *diag) {
    size_t i;

    if (s != NULL) {
        set_diag_context(s->line, s->col);
    }
    if (diag != NULL && diag->message[0] == '\0') {
        if (s != NULL && (s->line != 0 || s->col != 0)) {
            diag->line = s->line;
            diag->col = s->col;
        } else if (g_diag_ctx_line != 0 || g_diag_ctx_col != 0) {
            diag->line = g_diag_ctx_line;
            diag->col = g_diag_ctx_col;
        }
    }
    if (s != NULL && (s->attr_flags & CC_ATTR_FALLTHROUGH) != 0 && s->kind != CC_STMT_EXPR) {
        set_diag(diag, "fallthrough attribute requires an empty statement");
        return -1;
    }

    switch (s->kind) {
    case CC_STMT_DECL:
        {
            cc_type_t init_type = s->type;
            int auto_type_decl = (s->storage & CC_STORAGE_AUTO_TYPE) != 0;
            int init_checked = 0;
            int sc_count = storage_class_count(s->storage);
            int fn_only_attrs = CC_ATTR_ALWAYS_INLINE | CC_ATTR_NOINLINE | CC_ATTR_HOT | CC_ATTR_COLD |
                                CC_ATTR_FORMAT | CC_ATTR_NONNULL | CC_ATTR_MALLOC_FN | CC_ATTR_ALIAS |
                                CC_ATTR_FLATTEN | CC_ATTR_TARGET;
            if ((s->attr_flags & CC_ATTR_NORETURN) != 0) {
                set_diag(diag, "noreturn attribute is only valid on functions");
                return -1;
            }
            if ((s->attr_flags & fn_only_attrs) != 0) {
                set_diag(diag, "function-only attribute used on local declaration");
                return -1;
            }
            if ((s->attr_flags & CC_ATTR_WEAK) != 0) {
                set_diag(diag, "weak attribute is only supported on file-scope objects/functions");
                return -1;
            }
            if ((s->attr_flags & attr_visibility_mask()) != 0) {
                set_diag(diag, "visibility attribute is only supported on file-scope objects/functions");
                return -1;
            }
            if ((s->attr_flags & CC_ATTR_ALIAS) != 0) {
                set_diag(diag, "alias attribute is only supported on file-scope objects/functions");
                return -1;
            }
            if ((s->attr_flags & CC_ATTR_SECTION) != 0 &&
                validate_attr_section(s->attr_section, diag, "section attribute on local declaration") != 0) {
                return -1;
            }
            if (has_multiple_visibility_attrs(s->attr_flags)) {
                set_diag(diag, "conflicting visibility attributes on local declaration");
                return -1;
            }
            if ((s->attr_flags & CC_ATTR_ALWAYS_INLINE) != 0 && (s->attr_flags & CC_ATTR_NOINLINE) != 0) {
                set_diag(diag, "conflicting always_inline/noinline attributes");
                return -1;
            }
            if ((s->attr_flags & CC_ATTR_HOT) != 0 && (s->attr_flags & CC_ATTR_COLD) != 0) {
                set_diag(diag, "conflicting hot/cold attributes");
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
            if ((s->storage & CC_STORAGE_THREAD_LOCAL) != 0 &&
                (s->storage & (CC_STORAGE_STATIC | CC_STORAGE_EXTERN)) == 0) {
                set_diag(diag, "thread_local local declaration requires static or extern storage");
                return -1;
            }
            if ((s->storage & CC_STORAGE_EXTERN) != 0 && s->expr != NULL) {
                set_diag(diag, "extern local declaration cannot have an initializer");
                return -1;
            }
            if (auto_type_decl) {
                if (s->expr == NULL) {
                    set_diag(diag, "auto type deduction requires an initializer");
                    return -1;
                }
                if (s->expr->kind == CC_EXPR_INIT_LIST) {
                    set_diag(diag, "auto type deduction from initializer list is not supported");
                    return -1;
                }
                if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                    return -1;
                }
                s->type = s->expr->value_type;
                s->type_struct_id = s->expr->struct_id;
                s->storage &= ~CC_STORAGE_AUTO_TYPE;
                init_type = s->type;
                init_checked = 1;
                if (getenv("CC_DEBUG_AUTO_TYPE") != NULL) {
                    fprintf(stderr, "cc-debug: __auto_type %s deduced type=%d sid=%d\n",
                            s->decl_name != NULL ? s->decl_name : "<anon>", (int)s->type, s->type_struct_id);
                }
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
            if (vars_push(vars, var_count, s->decl_name, s->type, s->type_struct_id, s->array_len, s->array_ndim,
                          s->array_dims, depth) != 0) {
                set_diag(diag, "out of memory adding local variable");
                return -1;
            }
            if (s->expr != NULL) {
                if (s->expr->kind == CC_EXPR_INIT_LIST) {
                    if (is_pointer_type(s->type) && s->array_len >= 0) {
                        long inferred_len = -1;
                        if (check_array_initializer(tu, s->decl_name, s->type, s->type_struct_id, s->array_len,
                                                    s->array_ndim, s->array_dims, s->expr, *vars, *var_count, depth,
                                                    &inferred_len, diag) != 0) {
                            free((*vars)[*var_count - 1].name);
                            (*var_count)--;
                            return -1;
                        }
                        if (s->array_len == 0 && inferred_len > 0) {
                            s->array_len = inferred_len;
                            (*vars)[*var_count - 1].array_len = inferred_len;
                            if (s->array_ndim > 0 && s->array_dims[0] == 0) {
                                s->array_dims[0] = inferred_len;
                                (*vars)[*var_count - 1].array_dims[0] = inferred_len;
                            }
                        }
                    } else if (s->type == CC_TYPE_VOID && s->type_struct_id >= 0) {
                        if (check_struct_initializer(tu, s->decl_name, s->type_struct_id, s->expr, *vars, *var_count,
                                                     depth, diag) != 0) {
                            free((*vars)[*var_count - 1].name);
                            (*var_count)--;
                            return -1;
                        }
                    } else {
                        cc_expr_t *scalar_init = unwrap_scalar_initializer_expr(s->expr, diag);
                        if (scalar_init == NULL) {
                            free((*vars)[*var_count - 1].name);
                            (*var_count)--;
                            return -1;
                        }
                        if (check_expr(tu, scalar_init, *vars, *var_count, depth, diag) != 0) {
                            free((*vars)[*var_count - 1].name);
                            (*var_count)--;
                            return -1;
                        }
                        if (!can_convert(init_type, scalar_init->value_type) &&
                            !(is_pointer_type(init_type) && is_integral_type(scalar_init->value_type) &&
                              is_null_ptr_constant(scalar_init))) {
                            if (diag != NULL && diag->message[0] == '\0') {
                                snprintf(diag->message, sizeof(diag->message), "cannot initialize variable '%s'",
                                         s->decl_name != NULL ? s->decl_name : "<anon>");
                            }
                            free((*vars)[*var_count - 1].name);
                            (*var_count)--;
                            return -1;
                        }
                        s->expr = scalar_init;
                    }
                } else {
                    if (!init_checked && check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                        free((*vars)[*var_count - 1].name);
                        (*var_count)--;
                        return -1;
                    }
                    if (is_pointer_type(s->type) && s->array_len >= 0 && s->expr->kind == CC_EXPR_STR) {
                        long inferred_len = -1;
                        int rc = check_array_string_initializer(s->decl_name, s->type, &s->array_len, s->expr,
                                                                &inferred_len, diag);
                        if (rc < 0) {
                            free((*vars)[*var_count - 1].name);
                            (*var_count)--;
                            return -1;
                        }
                        if (rc == 0) {
                            (*vars)[*var_count - 1].array_len = s->array_len;
                            if (s->array_ndim > 0 && s->array_dims[0] == 0 && inferred_len > 0) {
                                s->array_dims[0] = inferred_len;
                                (*vars)[*var_count - 1].array_dims[0] = inferred_len;
                            }
                            return 0;
                        }
                    }
                    if (!init_checked && !can_convert(init_type, s->expr->value_type) &&
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
        if ((s->attr_flags & CC_ATTR_FALLTHROUGH) != 0) {
            if (s->expr != NULL) {
                set_diag(diag, "fallthrough attribute requires an empty statement");
                return -1;
            }
            if (switch_depth <= 0) {
                set_diag(diag, "fallthrough attribute is only valid in switch statements");
                return -1;
            }
            return 0;
        }
        if (s->expr == NULL) {
            return 0;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (!(s->expr->kind == CC_EXPR_CAST && s->expr->aux_type == CC_TYPE_VOID)) {
            const cc_expr_t *ce = s->expr;
            if (ce->kind == CC_EXPR_CALL && ce->ident != NULL) {
                const cc_function_t *f = find_function(tu, ce->ident);
                if (f != NULL && (f->attr_flags & CC_ATTR_NODISCARD) != 0) {
                    char buf[192];
                    snprintf(buf, sizeof(buf), "ignoring nodiscard return value from %s",
                             f->name != NULL ? f->name : "<anon>");
                    if (emit_required_warning(diag, ce->line, ce->col, buf) != 0) {
                        return -1;
                    }
                }
            }
        }
        return 0;

    case CC_STMT_ASM: {
        size_t i2;
        if (s->asm_template == NULL) {
            set_diag(diag, "asm statement is missing a template");
            return -1;
        }
        if (s->asm_is_goto && s->asm_goto_label_count == 0) {
            set_diag(diag, "asm goto requires at least one destination label");
            return -1;
        }
        if (!s->asm_is_goto && s->asm_goto_label_count > 0) {
            set_diag(diag, "asm goto label list requires 'asm goto'");
            return -1;
        }
        if (asm_validate_template_refs(s, diag) != 0) {
            return -1;
        }
        for (i2 = 0; i2 < s->asm_output_count; ++i2) {
            if (s->asm_outputs[i2].expr == NULL || !asm_operand_is_lvalue(s->asm_outputs[i2].expr)) {
                set_diag(diag, "asm output operand must be an lvalue");
                return -1;
            }
            if (check_expr(tu, s->asm_outputs[i2].expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }
            if (s->asm_outputs[i2].expr->value_type == CC_TYPE_FLOAT ||
                s->asm_outputs[i2].expr->value_type == CC_TYPE_DOUBLE) {
                set_diag(diag, "floating asm outputs are not supported yet");
                return -1;
            }
            if (asm_validate_constraint(s->asm_outputs[i2].constraint, 1, s->asm_output_count, g_pointer_size_bytes,
                                        diag) != 0) {
                return -1;
            }
        }
        for (i2 = 0; i2 < s->asm_input_count; ++i2) {
            if (s->asm_inputs[i2].expr == NULL) {
                set_diag(diag, "asm input operand requires an expression");
                return -1;
            }
            if (check_expr(tu, s->asm_inputs[i2].expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }
            if (s->asm_inputs[i2].expr->value_type == CC_TYPE_FLOAT ||
                s->asm_inputs[i2].expr->value_type == CC_TYPE_DOUBLE) {
                set_diag(diag, "floating asm inputs are not supported yet");
                return -1;
            }
            if (asm_validate_constraint(s->asm_inputs[i2].constraint, 0, s->asm_output_count, g_pointer_size_bytes,
                                        diag) != 0) {
                return -1;
            }
        }
        for (i2 = 0; i2 < s->asm_clobber_count; ++i2) {
            if (s->asm_clobbers[i2] == NULL || s->asm_clobbers[i2][0] == '\0') {
                set_diag(diag, "asm clobber name cannot be empty");
                return -1;
            }
            if (strchr(s->asm_clobbers[i2], '\n') != NULL || strchr(s->asm_clobbers[i2], '\r') != NULL) {
                set_diag(diag, "asm clobber contains invalid characters");
                return -1;
            }
        }
        for (i2 = 0; i2 < s->asm_goto_label_count; ++i2) {
            if (s->asm_goto_labels[i2] == NULL || s->asm_goto_labels[i2][0] == '\0') {
                set_diag(diag, "asm goto label cannot be empty");
                return -1;
            }
        }
        return 0;
    }

    case CC_STMT_RETURN:
        if ((fn_attr_flags & CC_ATTR_NORETURN) != 0) {
            set_diag(diag, "noreturn function must not contain a return statement");
            return -1;
        }
        if (fn_ret_type == CC_TYPE_VOID && fn_ret_struct_id < 0) {
            if (s->expr != NULL) {
                if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                    return -1;
                }
                if (s->expr->value_type != CC_TYPE_VOID) {
                    set_diag(diag, "void function cannot return a value");
                    return -1;
                }
            }
        } else {
            if (s->expr == NULL) {
                set_diag(diag, "non-void function must return a value");
                return -1;
            }
            if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }
            if (fn_ret_type == CC_TYPE_VOID && fn_ret_struct_id >= 0) {
                if (!(s->expr->value_type == CC_TYPE_VOID && s->expr->struct_id == fn_ret_struct_id)) {
                    if (!(s->expr->kind == CC_EXPR_STMT && is_integral_type(s->expr->value_type))) {
                        set_diag(diag, "return type mismatch");
                        return -1;
                    }
                }
            } else if (!can_convert(fn_ret_type, s->expr->value_type)) {
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
        if (maybe_warn_assignment_condition(s->expr, "if", diag) != 0) {
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
            if (check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_ret_struct_id, fn_attr_flags, loop_depth,
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
            if (check_stmt(tu, s->else_branch, vars, var_count, depth, fn_ret_type, fn_ret_struct_id, fn_attr_flags, loop_depth,
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
            int child_depth = depth + 1;
            if (s->is_synthetic_block) {
                child_depth = depth;
            }
            for (i = 0; i < s->block_count; ++i) {
                if (check_stmt(tu, &s->block_stmts[i], vars, var_count, child_depth, fn_ret_type, fn_ret_struct_id, fn_attr_flags,
                               loop_depth,
                               switch_depth,
                               saw_return, diag) != 0) {
                    return -1;
                }
            }
            if (!s->is_synthetic_block) {
                for (i = saved; i < *var_count; ++i) {
                    free((*vars)[i].name);
                }
                *var_count = saved;
            }
            return 0;
        }

    case CC_STMT_WHILE:
        if (s->expr == NULL || s->then_branch == NULL) {
            set_diag(diag, "malformed while statement");
            return -1;
        }
        if (maybe_warn_assignment_condition(s->expr, "while", diag) != 0) {
            return -1;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (s->expr->value_type == CC_TYPE_VOID) {
            set_diag(diag, "while condition cannot be void");
            return -1;
        }
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_ret_struct_id, fn_attr_flags, loop_depth + 1,
                          switch_depth,
                          saw_return, diag);

    case CC_STMT_DO:
        if (s->expr == NULL || s->then_branch == NULL) {
            set_diag(diag, "malformed do-while statement");
            return -1;
        }
        if (check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_ret_struct_id, fn_attr_flags, loop_depth + 1,
                       switch_depth,
                       saw_return, diag) != 0) {
            return -1;
        }
        if (check_expr(tu, s->expr, *vars, *var_count, depth, diag) != 0) {
            return -1;
        }
        if (maybe_warn_assignment_condition(s->expr, "do-while", diag) != 0) {
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
                                       fn_ret_struct_id, fn_attr_flags, loop_depth, switch_depth, saw_return, diag) != 0) {
                            return -1;
                        }
                    }
                } else {
                    if (check_stmt(tu, s->init_stmt, vars, var_count, depth + 1, fn_ret_type, fn_ret_struct_id, fn_attr_flags,
                                   loop_depth, switch_depth, saw_return, diag) != 0) {
                        return -1;
                    }
                }
                for_depth = depth + 1;
            } else if (s->init_expr != NULL && check_expr(tu, s->init_expr, *vars, *var_count, depth, diag) != 0) {
                return -1;
            }

            if (s->expr != NULL) {
                if (maybe_warn_assignment_condition(s->expr, "for", diag) != 0) {
                    return -1;
                }
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
            if (check_stmt(tu, s->then_branch, vars, var_count, for_depth, fn_ret_type, fn_ret_struct_id, fn_attr_flags,
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
                long vhi = 0;
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
                vhi = ci->case_has_range ? ci->case_hi : vi;
                if (ci->case_has_range && vhi < vi) {
                    set_diag(diag, "case range upper bound must be >= lower bound");
                    return -1;
                }
                {
                    size_t i2;
                    for (i2 = i1 + 1; i2 < s->then_branch->block_count; ++i2) {
                        const cc_stmt_t *cj = &s->then_branch->block_stmts[i2];
                        long vj = 0;
                        long vjhi = 0;
                        if (cj->kind != CC_STMT_CASE) {
                            continue;
                        }
                        if (eval_const_int_expr(tu, cj->expr, &vj) != 0) {
                            continue;
                        }
                        vjhi = cj->case_has_range ? cj->case_hi : vj;
                        if (vjhi < vj) {
                            continue;
                        }
                        if (!(vhi < vj || vjhi < vi)) {
                            set_diag(diag, "duplicate/overlapping case value in switch");
                            return -1;
                        }
                    }
                }
            }
        }
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_ret_struct_id, fn_attr_flags, loop_depth,
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
        if (s->case_has_range) {
            if (s->case_hi < s->expr->int_val) {
                set_diag(diag, "case range upper bound must be >= lower bound");
                return -1;
            }
            if (emit_warning(diag, s->line, s->col, "case ranges are a GNU extension (non-C99)", 1) != 0) {
                return -1;
            }
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
            if (emit_warning(diag, s->line, s->col, "computed goto is a GNU extension (non-C99)", 1) != 0) {
                return -1;
            }
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
        return check_stmt(tu, s->then_branch, vars, var_count, depth, fn_ret_type, fn_ret_struct_id, fn_attr_flags, loop_depth,
                          switch_depth,
                          saw_return, diag);

    default:
        set_diag(diag, "unsupported statement kind");
        return -1;
    }
}

int cc_sema_check(const cc_translation_unit_t *tu, cc_diag_t *diag) {
    size_t i;
    int had_error = 0;

    if (diag != NULL) {
        diag->line = 0;
        diag->col = 0;
        diag->error_count = 0;
        diag->message[0] = '\0';
    }

    if (tu == NULL) {
        set_diag(diag, "invalid translation unit");
        return -1;
    }
    if (tu->func_count == 0 && tu->global_count == 0) {
        return 0;
    }

    for (i = 0; i < tu->global_count; ++i) {
        cc_global_t *g = &tu->globals[i];
        int sc_count = storage_class_count(g->storage);
        size_t j;
        set_diag_context(g->line, g->col);
        if (diag != NULL && diag->message[0] == '\0' && (g->line != 0 || g->col != 0)) {
            diag->line = g->line;
            diag->col = g->col;
        }
        if (g->name == NULL || g->name[0] == '\0') {
            set_diag(diag, "file-scope declaration with missing name");
            goto fail_global_item;
        }
        if (sc_count > 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "multiple storage-class specifiers in file-scope declaration: %s", g->name);
            }
            goto fail_global_item;
        }
        if ((g->storage & CC_STORAGE_AUTO) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "file-scope object cannot use auto storage: %s", g->name);
            }
            goto fail_global_item;
        }
        if ((g->storage & CC_STORAGE_INLINE) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "inline is only valid on function declarations: %s", g->name);
            }
            goto fail_global_item;
        }
        if (g->type == CC_TYPE_VOID && g->type_struct_id < 0 &&
            !((g->storage & CC_STORAGE_EXTERN) != 0 && g->init == NULL)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "invalid void file-scope object: %s", g->name);
            }
            goto fail_global_item;
        }
        if (has_multiple_visibility_attrs(g->attr_flags)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "conflicting visibility attributes for object '%s'",
                         g->name);
            }
            goto fail_global_item;
        }
        if ((g->attr_flags & CC_ATTR_ALIGNED) != 0 &&
            validate_attr_align(g->attr_align, diag, "aligned attribute on file-scope object") != 0) {
            goto fail_global_item;
        }
        if ((g->attr_flags & CC_ATTR_SECTION) != 0 &&
            validate_attr_section(g->attr_section, diag, "section attribute on file-scope object") != 0) {
            goto fail_global_item;
        }
        if ((g->attr_flags & CC_ATTR_ALIAS) != 0) {
            if (g->attr_alias == NULL || g->attr_alias[0] == '\0') {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "alias attribute on object '%s' requires a target",
                             g->name);
                }
                goto fail_global_item;
            }
            if (g->init != NULL) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "alias object '%s' cannot have an initializer", g->name);
                }
                goto fail_global_item;
            }
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
                    goto fail_global_item;
                }
                if (prev->array_ndim > 0 && g->array_ndim > 0) {
                    int ad;
                    if (prev->array_ndim != g->array_ndim) {
                        if (getenv("CC_DEBUG_GLOBAL_ARRAY") != NULL) {
                            fprintf(stderr, "cc-debug: array ndim mismatch %s prev_ndim=%d cur_ndim=%d\n", g->name,
                                    prev->array_ndim, g->array_ndim);
                        }
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message),
                                     "conflicting file-scope array declarators: %s", g->name);
                        }
                        goto fail_global_item;
                    }
                    for (ad = 0; ad < prev->array_ndim; ++ad) {
                        long pd = prev->array_dims[ad];
                        long gd = g->array_dims[ad];
                        /*
                         * Treat zero-length dimensions as incomplete bounds
                         * (`extern T a[]`) that are compatible with concrete
                         * bounds in another declaration.
                         */
                        if (pd != 0 && gd != 0 && pd != gd) {
                            if (getenv("CC_DEBUG_GLOBAL_ARRAY") != NULL) {
                                fprintf(stderr,
                                        "cc-debug: array dim mismatch %s dim=%d prev=%ld cur=%ld prev_len=%ld cur_len=%ld\n",
                                        g->name, ad, pd, gd, prev->array_len, g->array_len);
                            }
                            if (diag != NULL && diag->message[0] == '\0') {
                                snprintf(diag->message, sizeof(diag->message),
                                         "conflicting file-scope array declarators: %s", g->name);
                            }
                            goto fail_global_item;
                        }
                    }
                }
                if (prev_static != cur_static) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "conflicting linkage for file-scope object: %s",
                                 g->name);
                    }
                    goto fail_global_item;
                }
                if ((prev->attr_flags & CC_ATTR_ALIAS) != 0 || (g->attr_flags & CC_ATTR_ALIAS) != 0) {
                    if ((prev->attr_flags & CC_ATTR_ALIAS) == 0 || (g->attr_flags & CC_ATTR_ALIAS) == 0 ||
                        prev->attr_alias == NULL || g->attr_alias == NULL ||
                        strcmp(prev->attr_alias, g->attr_alias) != 0) {
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message),
                                     "conflicting alias targets for file-scope object: %s", g->name);
                        }
                        goto fail_global_item;
                    }
                }
                if (prev->init != NULL && g->init != NULL) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "duplicate file-scope definition: %s", g->name);
                    }
                    goto fail_global_item;
                }
                break;
            }
        }
        if (g->init != NULL) {
            if (g->init->kind == CC_EXPR_INIT_LIST) {
                if (is_pointer_type(g->type) && g->array_len >= 0) {
                    long inferred_len = -1;
                    if (check_array_initializer(tu, g->name, g->type, g->type_struct_id, g->array_len, g->array_ndim,
                                                g->array_dims, g->init, NULL, 0, 0, &inferred_len, diag) != 0) {
                        goto fail_global_item;
                    }
                    if (g->array_len == 0 && inferred_len > 0) {
                        g->array_len = inferred_len;
                        if (g->array_ndim > 0 && g->array_dims[0] == 0) {
                            g->array_dims[0] = inferred_len;
                        }
                    }
                } else if (g->type == CC_TYPE_VOID && g->type_struct_id >= 0) {
                    if (check_struct_initializer(tu, g->name, g->type_struct_id, g->init, NULL, 0, 0, diag) != 0) {
                        goto fail_global_item;
                    }
                } else {
                    cc_expr_t *scalar_init = unwrap_scalar_initializer_expr(g->init, diag);
                    if (scalar_init == NULL) {
                        goto fail_global_item;
                    }
                    if (check_expr(tu, scalar_init, NULL, 0, 0, diag) != 0) {
                        goto fail_global_item;
                    }
                    if (!can_convert(g->type, scalar_init->value_type) &&
                        !(is_pointer_type(g->type) && is_integral_type(scalar_init->value_type) &&
                          is_null_ptr_constant(scalar_init))) {
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message), "cannot initialize file-scope object %s",
                                     g->name);
                        }
                        goto fail_global_item;
                    }
                }
            } else {
                if (check_expr(tu, g->init, NULL, 0, 0, diag) != 0) {
                    goto fail_global_item;
                }
                if (is_pointer_type(g->type) && g->array_len >= 0 && g->init->kind == CC_EXPR_STR) {
                    long inferred_len = -1;
                    int rc = check_array_string_initializer(g->name, g->type, &g->array_len, g->init, &inferred_len,
                                                            diag);
                    if (rc < 0) {
                        goto fail_global_item;
                    }
                    if (rc == 0) {
                        if (g->array_ndim > 0 && g->array_dims[0] == 0 && inferred_len > 0) {
                            g->array_dims[0] = inferred_len;
                        }
                        continue;
                    }
                }
                if (!can_convert(g->type, g->init->value_type) &&
                    !(is_pointer_type(g->type) && is_integral_type(g->init->value_type) &&
                      is_null_ptr_constant(g->init))) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "cannot initialize file-scope object %s",
                                 g->name);
                    }
                    goto fail_global_item;
                }
            }
        }
        continue;

fail_global_item:
        had_error = 1;
        sema_diag_report_and_clear(diag);
        continue;
    }

    for (i = 0; i < tu->func_count; ++i) {
        cc_function_t *f = &tu->funcs[i];
        int f_sc_count = storage_class_count(f->storage);
        size_t j;
        set_diag_context(f->line, f->col);
        if (diag != NULL && diag->message[0] == '\0' && (f->line != 0 || f->col != 0)) {
            diag->line = f->line;
            diag->col = f->col;
        }

        if (f->name == NULL || f->name[0] == '\0') {
            set_diag(diag, "function with missing name");
            goto fail_decl;
        }
        if ((f->attr_flags & CC_ATTR_PACKED) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "packed attribute is invalid for function '%s'",
                         f->name);
            }
            goto fail_decl;
        }
        if (has_multiple_visibility_attrs(f->attr_flags)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "conflicting visibility attributes for function '%s'",
                         f->name);
            }
            goto fail_decl;
        }
        if ((f->attr_flags & CC_ATTR_ALWAYS_INLINE) != 0 && (f->attr_flags & CC_ATTR_NOINLINE) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "function '%s' has conflicting always_inline/noinline attributes", f->name);
            }
            goto fail_decl;
        }
        if ((f->attr_flags & CC_ATTR_HOT) != 0 && (f->attr_flags & CC_ATTR_COLD) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "function '%s' has conflicting hot/cold attributes", f->name);
            }
            goto fail_decl;
        }
        if (f_sc_count > 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "multiple storage-class specifiers in function declaration: %s", f->name);
            }
            goto fail_decl;
        }
        if ((f->storage & (CC_STORAGE_AUTO | CC_STORAGE_REGISTER)) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "function declaration cannot use auto/register storage: %s", f->name);
            }
            goto fail_decl;
        }
        if ((f->storage & CC_STORAGE_THREAD_LOCAL) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "function declaration cannot use thread_local storage: %s", f->name);
            }
            goto fail_decl;
        }
        if ((f->attr_flags & CC_ATTR_NORETURN) != 0 && !(f->ret_type == CC_TYPE_VOID && f->ret_struct_id < 0)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "noreturn function '%s' must have void return type",
                         f->name);
            }
            goto fail_decl;
        }
        if ((f->attr_flags & CC_ATTR_ALIGNED) != 0 &&
            validate_attr_align(f->attr_align, diag, "aligned attribute on function") != 0) {
            goto fail_decl;
        }
        if ((f->attr_flags & CC_ATTR_SECTION) != 0 &&
            validate_attr_section(f->attr_section, diag, "section attribute on function") != 0) {
            goto fail_decl;
        }
        if ((f->attr_flags & CC_ATTR_MALLOC_FN) != 0 && !is_pointer_type(f->ret_type)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "malloc function '%s' must return a pointer type",
                         f->name);
            }
            goto fail_decl;
        }
        /* GCC accepts format-like attributes on some non-variadic declarations (e.g. v*printf forms). */
        if ((f->attr_flags & CC_ATTR_NONNULL) != 0 && (!f->has_prototype || f->param_count == 0)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "nonnull function '%s' requires at least one prototype parameter", f->name);
            }
            goto fail_decl;
        }
        if ((f->attr_flags & CC_ATTR_ALIAS) != 0) {
            if (f->has_body) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "alias function '%s' cannot have a definition body", f->name);
                }
                goto fail_decl;
            }
            if (f->attr_alias == NULL || f->attr_alias[0] == '\0') {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "alias function '%s' requires a target", f->name);
                }
                goto fail_decl;
            }
        }
        for (j = 0; j < i; ++j) {
            cc_function_t *prev = &tu->funcs[j];
            if (strcmp(prev->name, f->name) != 0) {
                continue;
            }
            if (!func_decl_compatible(prev, f)) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "conflicting declarations for function: %s",
                             f->name);
                }
                goto fail_decl;
            }
            if (prev->has_body && f->has_body) {
                int prev_inline = ((prev->storage & CC_STORAGE_INLINE) != 0);
                int cur_inline = ((f->storage & CC_STORAGE_INLINE) != 0);
                if (prev_inline || cur_inline) {
                    if (!prev_inline && cur_inline) {
                        f->has_body = 0;
                    } else if (prev_inline && !cur_inline) {
                        prev->has_body = 0;
                    } else {
                        f->has_body = 0;
                    }
                    continue;
                }
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "duplicate function definition: %s", f->name);
                }
                goto fail_decl;
            }
            if ((prev->attr_flags & CC_ATTR_ALIAS) != 0 || (f->attr_flags & CC_ATTR_ALIAS) != 0) {
                if ((prev->attr_flags & CC_ATTR_ALIAS) != 0 && (f->attr_flags & CC_ATTR_ALIAS) != 0 &&
                    (prev->attr_alias == NULL || f->attr_alias == NULL ||
                     strcmp(prev->attr_alias, f->attr_alias) != 0)) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "conflicting alias targets for function: %s", f->name);
                    }
                    goto fail_decl;
                }
            }
        }
        continue;

fail_decl:
        had_error = 1;
        sema_diag_report_and_clear(diag);
        continue;
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

        set_diag_context(f->line, f->col);
        if (diag != NULL && diag->message[0] == '\0' && (f->line != 0 || f->col != 0)) {
            diag->line = f->line;
            diag->col = f->col;
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
            if (vars_push(&vars, &var_count, f->params[j].name, f->params[j].type, f->params[j].type_struct_id, -1, 0,
                          NULL, 0) != 0) {
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
            size_t saved_stmt_var_count = var_count;
            if (check_stmt(tu, &f->stmts[j], &vars, &var_count, 0, f->ret_type, f->ret_struct_id, fn_attr_flags, 0, 0, &saw_return,
                           diag) != 0) {
                size_t vi;
                had_error = 1;
                sema_diag_report_and_clear(diag);
                for (vi = saved_stmt_var_count; vi < var_count; ++vi) {
                    free(vars[vi].name);
                }
                var_count = saved_stmt_var_count;
                continue;
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
        had_error = 1;
        sema_diag_report_and_clear(diag);
        continue;
    }

    if (had_error) {
        if (diag != NULL && diag->error_count > 0) {
            snprintf(diag->message, sizeof(diag->message), "%zu error(s) generated", diag->error_count);
        }
        return -1;
    }

    return 0;
}

void cc_frontend_set_pointer_size(int bytes) {
    if (bytes == 4 || bytes == 8) {
        g_pointer_size_bytes = bytes;
        cc_parser_set_pointer_size(bytes);
    }
}

void cc_frontend_set_std_mode(const char *std_mode) {
    g_allow_implicit_funcdecl = std_mode_allows_implicit_function_decls(std_mode);
    if (g_implicit_funcdecl_override >= 0) {
        g_allow_implicit_funcdecl = g_implicit_funcdecl_override ? 1 : 0;
    }
    g_std_c23 = std_mode_is_c23_or_newer(std_mode);
    cc_parser_set_std_mode(std_mode);
    if (g_gnu89_inline_override >= 0) {
        cc_parser_set_gnu89_inline(g_gnu89_inline_override);
    }
}

void cc_frontend_set_gnu89_inline_mode(int enabled, int override_set) {
    if (!override_set) {
        g_gnu89_inline_override = -1;
        return;
    }
    g_gnu89_inline_override = enabled ? 1 : 0;
    cc_parser_set_gnu89_inline(g_gnu89_inline_override);
}

void cc_frontend_set_implicit_funcdecl_policy(int allow, int override_set) {
    if (!override_set) {
        g_implicit_funcdecl_override = -1;
        return;
    }
    g_implicit_funcdecl_override = allow ? 1 : 0;
    g_allow_implicit_funcdecl = g_implicit_funcdecl_override;
}

void cc_frontend_set_diag_flags(int wall, int werror, int pedantic, int pedantic_errors) {
    g_warn_all = wall ? 1 : 0;
    g_warn_error = werror ? 1 : 0;
    g_pedantic = pedantic ? 1 : 0;
    g_pedantic_errors = pedantic_errors ? 1 : 0;
}
