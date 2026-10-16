#include "cc_frontend.h"
#include "cc_asm.h"
#include "cc_consteval.h"
#include "cc_ctype.h"
#include "cc_diag.h"
#include "cc_lang.h"
#include "cc_options.h"
#include "cc_types.h"
#include "cc_util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t g_diag_ctx_line = 0;
static size_t g_diag_ctx_col = 0;

typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
    int storage;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    int depth;
} var_entry_t;

typedef struct {
    char **items;
    size_t count;
} name_list_t;

/**
 * Record a semantic error.  The first error recorded is the one
 * reported; with no position given, it is placed at the construct being
 * checked (set_diag_context).
 *
 * @param d    the diagnostic
 * @param msg  the message
 */
static void set_diag(cc_diag_t *d, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    if (d->line == 0 && d->col == 0) {
        cc_diag_set(d, NULL, g_diag_ctx_line, g_diag_ctx_col, msg);
    } else {
        cc_diag_set(d, NULL, d->line, d->col, msg);
    }
}

/**
 * Print the recorded semantic error, placed at the construct being
 * checked if it has no position of its own, count it, and clear it.
 *
 * @param d  the diagnostic
 */
static void sema_diag_report_and_clear(cc_diag_t *d) {
    if (d == NULL || d->message[0] == '\0') {
        return;
    }
    if (d->line == 0 && d->col == 0) {
        d->line = g_diag_ctx_line;
        d->col = g_diag_ctx_col;
    }
    cc_diag_report(d);
}

static void set_diag_context(size_t line, size_t col) {
    if (line == 0 && col == 0) {
        return;
    }
    g_diag_ctx_line = line;
    g_diag_ctx_col = col;
}

/**
 * Print a warning about the file being checked.
 *
 * @param diag  the diagnostic, for the file's path; may be NULL
 * @param line  the line, or 0
 * @param col   the column, or 0
 * @param msg   the message
 */
static void print_warning(const cc_diag_t *diag, size_t line, size_t col, const char *msg) {
    cc_diag_print("warning", diag != NULL ? diag->path : NULL, line, col, msg, 0);
}

static int emit_warning(cc_diag_t *diag, size_t line, size_t col, const char *msg, int pedantic_only) {
    int as_error = cc_options_current()->warn_error || (pedantic_only && cc_options_current()->pedantic_errors);

    if (pedantic_only) {
        if (!cc_options_current()->pedantic) {
            return 0;
        }
    } else if (!cc_options_current()->warn_all) {
        return 0;
    }

    if (line == 0 && col == 0) {
        line = g_diag_ctx_line;
        col = g_diag_ctx_col;
    }

    print_warning(diag, line, col, msg);

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
    print_warning(diag, line, col, msg);
    if (!cc_options_current()->warn_error) {
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

static cc_attr_flags_t attr_visibility_mask(void) {
    return CC_ATTR_VIS_DEFAULT | CC_ATTR_VIS_HIDDEN | CC_ATTR_VIS_PROTECTED | CC_ATTR_VIS_INTERNAL;
}

static int has_multiple_visibility_attrs(cc_attr_flags_t flags) {
    cc_attr_flags_t v = flags & attr_visibility_mask();
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
                     int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS], int storage, int depth) {
    var_entry_t *next;
    char *dup;

    next = (var_entry_t *)realloc(*vars, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *vars = next;

    {
        size_t len = strlen(name);
        dup = (char *)malloc(len + 1);
        if (dup == NULL) {
            return -1;
        }
        snprintf(dup, len + 1, "%s", name);
    }
    (*vars)[*count].name = dup;
    (*vars)[*count].type = type;
    (*vars)[*count].struct_id = struct_id;
    (*vars)[*count].storage = storage;
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

static int asm_constraint_is_imm(const char *c) {
    size_t i;
    if (c == NULL)
        return 0;
    for (i = 0; c[i] != '\0'; ++i) {
        int ch = (unsigned char)c[i];
        if (ch == 'i' || ch == 'n' || ch == 'e')
            return 1;
        if (ch >= 'I' && ch <= 'P')
            return 1;
    }
    return 0;
}

static int asm_constraint_is_flag_output(const char *c) {
    return c != NULL && strncmp(c, "=@cc", 4) == 0;
}

static int asm_constraint_letter_supported(int ch) {
    if (ch == 'r' || ch == 'q' || ch == 'a' || ch == 'b' || ch == 'c' || ch == 'd' || ch == 'S' || ch == 'D') {
        return 1;
    }
    if (ch == 'm' || ch == 'o' || ch == 'V' || ch == 'g' || ch == 'X') {
        return 1;
    }
    if (ch == 'i' || ch == 'n' || ch == 'e' || (ch >= 'I' && ch <= 'P')) {
        return 1;
    }
    /* GCC x86-specific register-class letters Linux makes heavy use of:
     *   A = EAX:EDX pair, B = EBX, R = legacy regs, Q = a/b/c/d byte-addr,
     *   U = call-clobbered, l = index regs, f/t/u = x87, y = MMX, x = SSE. */
    if (ch == 'A' || ch == 'B' || ch == 'R' || ch == 'Q' || ch == 'U') {
        return 1;
    }
    if (ch == 'l' || ch == 'f' || ch == 't' || ch == 'u' || ch == 'y' || ch == 'x') {
        return 1;
    }
    return 0;
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
    if (is_output && asm_constraint_is_flag_output(c)) {
        return 0;
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
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
            if (!asm_constraint_letter_supported(ch)) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "asm operand constraint contains unsupported character '%c' in \"%s\"", ch, c);
                }
                return -1;
            }
        }
    }
    if (strchr(c, 'q') != NULL && pointer_size != 4 && pointer_size != 8) {
        set_diag(diag, "asm 'q' constraint requires x86 target");
        return -1;
    }
    has_class = cc_asm_constraint_allows_memory(c) || cc_asm_constraint_names_register(c) || asm_constraint_is_imm(c);
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

/**
 * The operand number of an asm goto statement's first label, `%l<n>`.
 * As in GNU C, labels are numbered after every operand, and an output
 * whose constraint has `+` counts twice: it is an output and an
 * implicit input after the explicit ones.
 *
 * @param s  the asm statement
 * @return   the number of its first label
 */
static size_t asm_goto_label_base(const cc_stmt_t *s) {
    size_t base = s->asm_output_count + s->asm_input_count;
    size_t i;
    for (i = 0; i < s->asm_output_count; ++i) {
        const char *c = s->asm_outputs[i].constraint;
        if (c != NULL && strchr(c, '+') != NULL) {
            base++;
        }
    }
    return base;
}

/**
 * The one register an asm constraint allows, when it allows nothing
 * else: `a`, `b`, `c`, `d`, `S` or `D`, with modifiers (`=`, `&`, ...)
 * around it.
 *
 * @param c  the constraint
 * @return   the register's letter, or 0 if the operand has another choice
 */
static int asm_constraint_only_register(const char *c) {
    int reg = 0;
    if (c == NULL) {
        return 0;
    }
    for (; *c != '\0'; ++c) {
        if ((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) {
            if (strchr("abcdSD", *c) == NULL || (reg != 0 && reg != *c)) {
                return 0;
            }
            reg = *c;
        }
    }
    return reg;
}

/**
 * Reject an asm input that can only be in the register an early-clobber
 * output is fixed to: the output is written before the inputs are all
 * read, so the two cannot share it (gcc: "impossible constraints").
 *
 * @param s     the asm statement
 * @param diag  receives the error
 * @return      0, or -1 if an input conflicts
 */
static int asm_check_early_clobber_inputs(const cc_stmt_t *s, cc_diag_t *diag) {
    size_t i;
    size_t j;
    for (i = 0; i < s->asm_output_count; ++i) {
        const char *oc = s->asm_outputs[i].constraint;
        int oreg = asm_constraint_only_register(oc);
        if (oreg == 0 || oc == NULL || strchr(oc, '&') == NULL) {
            continue;
        }
        for (j = 0; j < s->asm_input_count; ++j) {
            if (asm_constraint_only_register(s->asm_inputs[j].constraint) == oreg) {
                set_diag(diag, "asm input constraint conflicts with early-clobber output register");
                return -1;
            }
        }
    }
    return 0;
}

static int asm_validate_template_refs(const cc_stmt_t *s, cc_diag_t *diag) {
    size_t i = 0;
    size_t total = s->asm_output_count + s->asm_input_count;
    size_t label_base = asm_goto_label_base(s);
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
                if (n < 0 || (size_t)n < label_base ||
                    (size_t)(n - (long)label_base) >= s->asm_goto_label_count) {
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
            {
                size_t len = strlen(src[i].name);
                dst[i].name = (char *)malloc(len + 1);
                if (dst[i].name == NULL) {
                    size_t j;
                    for (j = 0; j < i; ++j) {
                        free(dst[j].name);
                    }
                    free(dst);
                    return -1;
                }
                snprintf(dst[i].name, len + 1, "%s", src[i].name);
            }
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
    {
        size_t len = strlen(name);
        dup = (char *)malloc(len + 1);
        if (dup == NULL) {
            return -1;
        }
        snprintf(dup, len + 1, "%s", name);
    }
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
    if (t == CC_TYPE_VOID || t == CC_TYPE_BITINT || t == CC_TYPE_ATOMIC) {
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
            lm->offset != rm->offset || lm->size != rm->size || lm->is_bitfield != rm->is_bitfield ||
            lm->bit_width != rm->bit_width || lm->bit_offset != rm->bit_offset ||
            lm->bit_storage_bits != rm->bit_storage_bits || lm->bit_signed != rm->bit_signed ||
            lm->bit_storage_size != rm->bit_storage_size) {
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

static int find_struct_member_recursive(const cc_translation_unit_t *tu, int struct_id, const char *name, int depth,
                                        const cc_struct_member_t **out_member, long *out_offset) {
    const cc_struct_def_t *sd;
    size_t i;

    if (out_member != NULL) {
        *out_member = NULL;
    }
    if (out_offset != NULL) {
        *out_offset = 0;
    }
    if (depth > 32 || name == NULL) {
        return -1;
    }
    sd = find_struct_def(tu, struct_id);
    if (sd == NULL || !sd->complete) {
        return -1;
    }

    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (m->name != NULL && strcmp(m->name, name) == 0) {
            if (out_member != NULL) {
                *out_member = m;
            }
            if (out_offset != NULL) {
                *out_offset = m->offset;
            }
            return 0;
        }
    }

    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        const cc_struct_def_t *inner;
        const cc_struct_member_t *inner_m = NULL;
        long inner_off = 0;

        if (m->name != NULL || m->type != CC_TYPE_VOID || m->type_struct_id < 0) {
            continue;
        }
        inner = find_struct_def(tu, m->type_struct_id);
        if (inner == NULL || !inner->complete) {
            continue;
        }
        if (find_struct_member_recursive(tu, m->type_struct_id, name, depth + 1, &inner_m, &inner_off) == 0) {
            if (out_member != NULL) {
                *out_member = inner_m;
            }
            if (out_offset != NULL) {
                *out_offset = m->offset + inner_off;
            }
            return 0;
        }
    }
    return -1;
}

static int find_struct_member_index(const cc_translation_unit_t *tu, int struct_id, const char *name) {
    const cc_struct_def_t *sd;
    size_t i;

    sd = find_struct_def(tu, struct_id);
    if (sd == NULL || !sd->complete || name == NULL) {
        return -1;
    }
    for (i = 0; i < sd->member_count; ++i) {
        if (sd->members[i].name != NULL && strcmp(sd->members[i].name, name) == 0) {
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

static int is_pointer_type(cc_type_t t) {
    return cc_type_is_pointer(t);
}

static cc_type_t ptr_base_type(cc_type_t t);
static long type_size_bytes(cc_type_t t);

static int pointer_depth(cc_type_t t) {
    return (int)cc_type_pointer_depth(t);
}

static cc_type_t pointer_ultimate_base_type(cc_type_t t) {
    while (is_pointer_type(t)) {
        t = ptr_base_type(t);
    }
    return t;
}

static cc_type_t size_type(void) {
    return cc_target_pointer_size() >= 8 ? CC_TYPE_ULONG : CC_TYPE_UINT;
}

static int is_scalar_type(cc_type_t t) {
    return cc_type_is_numeric(t) || is_pointer_type(t);
}

/*
 * Qualifier tracking currently stores const/volatile/restrict at declaration
 * granularity, without full top-level vs pointee qualifier separation.
 * Apply object mutability diagnostics only to non-pointer objects to avoid
 * rejecting valid updates such as `const char *p; p++;`.
 */
static int const_storage_applies_to_object(cc_type_t t) {
    return !is_pointer_type(t);
}

static void unwrap_atomic_expr_type(cc_type_t *t, int *sid) {
    if (t == NULL || sid == NULL) {
        return;
    }
    if (*t == CC_TYPE_ATOMIC && *sid >= 0) {
        *t = (cc_type_t)*sid;
        *sid = -1;
    }
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
    if (e->kind != CC_EXPR_CAST && !(e->kind == CC_EXPR_SIZEOF && e->lhs == NULL)) {
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
    if (cc_type_is_numeric(dst) && cc_type_is_numeric(src)) {
        return 1;
    }
    if (is_pointer_type(dst) && is_pointer_type(src)) {
        cc_type_t dbase = ptr_base_type(dst);
        cc_type_t sbase = ptr_base_type(src);
        if (dbase == CC_TYPE_VOID || sbase == CC_TYPE_VOID || dbase == sbase) {
            return 1;
        }
        if (cc_type_is_integral(dbase) && cc_type_is_integral(sbase) && type_size_bytes(dbase) == type_size_bytes(sbase)) {
            return 1;
        }
    }
    if (is_pointer_type(dst) && cc_type_is_integral(src)) {
        return 1;
    }
    if (cc_type_is_integral(dst) && is_pointer_type(src)) {
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
    wide = cc_string_literal_is_wide(init);
    if (wide) {
        if (!cc_type_is_integral(elem_type)) {
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
    /*
     * Keep literal format strings intact. Rewriting `%Lf` to `%f` breaks
     * varargs ABI and causes argument desynchronization at runtime.
     */
    (void)literal;
    return NULL;
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
            (is_pointer_type(m->type) && cc_type_is_integral(src_expr->value_type) && is_null_ptr_constant(src_expr))) {
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

/* Conversion rank of a promoted integer type: int, long, long long. */
static cc_type_t common_arith_type(cc_type_t a, cc_type_t b) {
    cc_type_t ap;
    cc_type_t bp;
    cc_type_t s;
    cc_type_t u;

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
    if (a == CC_TYPE_FLOAT16 || b == CC_TYPE_FLOAT16) {
        return CC_TYPE_FLOAT16;
    }

    ap = cc_type_promote(a);
    bp = cc_type_promote(b);
    if (cc_type_integer_rank(ap) == 1) {
        ap = cc_type_is_unsigned_integral(ap) ? CC_TYPE_UINT : CC_TYPE_INT;
    }
    if (cc_type_integer_rank(bp) == 1) {
        bp = cc_type_is_unsigned_integral(bp) ? CC_TYPE_UINT : CC_TYPE_INT;
    }
    if (cc_type_is_unsigned_integral(ap) == cc_type_is_unsigned_integral(bp)) {
        return cc_type_integer_rank(ap) >= cc_type_integer_rank(bp) ? ap : bp;
    }

    /*
     * Mixed signedness (C11 6.3.1.8): a signed type of higher rank
     * wins only when it can hold all of the unsigned type's values,
     * which for long against unsigned int depends on the target.
     */
    s = cc_type_is_unsigned_integral(ap) ? bp : ap;
    u = cc_type_is_unsigned_integral(ap) ? ap : bp;
    if (cc_type_integer_rank(u) >= cc_type_integer_rank(s)) {
        return u;
    }
    if (cc_type_integer_rank_size(s) > cc_type_integer_rank_size(u)) {
        return s;
    }
    return s == CC_TYPE_LONG ? CC_TYPE_ULONG : CC_TYPE_ULONG_LONG;
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
    return(cc_builtin_bswap_bits(name));
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
    BUILTIN_FFS,
    BUILTIN_POPCOUNT,
    BUILTIN_PARITY,
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
    BUILTIN_PREFETCH,
    BUILTIN_CPU_SUPPORTS,
    BUILTIN_STRLEN,
    BUILTIN_MEMCPY,
    BUILTIN_MEMMOVE,
    BUILTIN_MEMSET,
    BUILTIN_MEMCPY_CHK,
    BUILTIN_MEMMOVE_CHK,
    BUILTIN_MEMSET_CHK,
    BUILTIN_HUGE_VAL,
    BUILTIN_HUGE_VALF,
    BUILTIN_HUGE_VALL,
    BUILTIN_INF,
    BUILTIN_INFF,
    BUILTIN_INFL,
    BUILTIN_LDBL_MIN,
    BUILTIN_LDBL_DENORM_MIN,
    BUILTIN_LDBL_MAX,
    BUILTIN_ISFINITE,
    BUILTIN_ISINF,
    BUILTIN_ISINF_SIGN,
    BUILTIN_ISNAN,
    BUILTIN_SIGNBIT,
    BUILTIN_NANF,
    BUILTIN_NAN,
    BUILTIN_NANL,
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
    BUILTIN_ATOMIC_STORE_N,
    BUILTIN_ATOMIC_THREAD_FENCE,
    BUILTIN_ATOMIC_SIGNAL_FENCE
} builtin_kind_t;

static builtin_kind_t builtin_kind(const char *name) {
    if (name == NULL) {
        return BUILTIN_NONE;
    }
    if (strcmp(name, "__builtin_va_start") == 0 || strcmp(name, "__builtin_c23_va_start") == 0) {
        return BUILTIN_VA_START;
    }
    if (strcmp(name, "__builtin_va_end") == 0 || strcmp(name, "__builtin_c23_va_end") == 0) {
        return BUILTIN_VA_END;
    }
    if (strcmp(name, "__builtin_va_copy") == 0 || strcmp(name, "__builtin_c23_va_copy") == 0) {
        return BUILTIN_VA_COPY;
    }
    if (strcmp(name, "__builtin_va_arg") == 0 || strcmp(name, "__builtin_c23_va_arg") == 0) {
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
    if (strcmp(name, "__builtin_ffs") == 0 || strcmp(name, "__builtin_ffsl") == 0 ||
        strcmp(name, "__builtin_ffsll") == 0) {
        return BUILTIN_FFS;
    }
    if (strcmp(name, "__builtin_popcount") == 0 || strcmp(name, "__builtin_popcountl") == 0 ||
        strcmp(name, "__builtin_popcountll") == 0) {
        return BUILTIN_POPCOUNT;
    }
    if (strcmp(name, "__builtin_parity") == 0 || strcmp(name, "__builtin_parityl") == 0 ||
        strcmp(name, "__builtin_parityll") == 0) {
        return BUILTIN_PARITY;
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
    if (strcmp(name, "__builtin_prefetch") == 0) {
        return BUILTIN_PREFETCH;
    }
    if (strcmp(name, "__builtin_cpu_supports") == 0) {
        return BUILTIN_CPU_SUPPORTS;
    }
    if (strcmp(name, "__builtin_strlen") == 0) {
        return BUILTIN_STRLEN;
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
    if (strcmp(name, "__builtin_huge_val") == 0) {
        return BUILTIN_HUGE_VAL;
    }
    if (strcmp(name, "__builtin_huge_valf") == 0) {
        return BUILTIN_HUGE_VALF;
    }
    if (strcmp(name, "__builtin_huge_vall") == 0) {
        return BUILTIN_HUGE_VALL;
    }
    if (strcmp(name, "__builtin_inf") == 0) {
        return BUILTIN_INF;
    }
    if (strcmp(name, "__builtin_inff") == 0) {
        return BUILTIN_INFF;
    }
    if (strcmp(name, "__builtin_infl") == 0) {
        return BUILTIN_INFL;
    }
    if (strcmp(name, "__builtin_ldbl_min") == 0) {
        return BUILTIN_LDBL_MIN;
    }
    if (strcmp(name, "__builtin_ldbl_denorm_min") == 0) {
        return BUILTIN_LDBL_DENORM_MIN;
    }
    if (strcmp(name, "__builtin_ldbl_max") == 0) {
        return BUILTIN_LDBL_MAX;
    }
    if (strcmp(name, "__builtin_isfinite") == 0) {
        return BUILTIN_ISFINITE;
    }
    if (strcmp(name, "__builtin_isinf") == 0 || strcmp(name, "__builtin_isinff") == 0 ||
        strcmp(name, "__builtin_isinfl") == 0) {
        return BUILTIN_ISINF;
    }
    if (strcmp(name, "__builtin_isinf_sign") == 0) {
        return BUILTIN_ISINF_SIGN;
    }
    if (strcmp(name, "__builtin_isnan") == 0 || strcmp(name, "__builtin_isnanf") == 0 ||
        strcmp(name, "__builtin_isnanl") == 0) {
        return BUILTIN_ISNAN;
    }
    if (strcmp(name, "__builtin_signbit") == 0 || strcmp(name, "__builtin_signbitf") == 0 ||
        strcmp(name, "__builtin_signbitl") == 0) {
        return BUILTIN_SIGNBIT;
    }
    if (strcmp(name, "__builtin_nanf") == 0 || strcmp(name, "__builtin_nansf") == 0) {
        return BUILTIN_NANF;
    }
    if (strcmp(name, "__builtin_nan") == 0 || strcmp(name, "__builtin_nans") == 0) {
        return BUILTIN_NAN;
    }
    if (strcmp(name, "__builtin_nanl") == 0 || strcmp(name, "__builtin_nansl") == 0) {
        return BUILTIN_NANL;
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
    if (strcmp(name, "__atomic_thread_fence") == 0) {
        return BUILTIN_ATOMIC_THREAD_FENCE;
    }
    if (strcmp(name, "__atomic_signal_fence") == 0) {
        return BUILTIN_ATOMIC_SIGNAL_FENCE;
    }
    return BUILTIN_NONE;
}

/**
 * Size of a type known from its kind alone.
 *
 * @param t  the type
 * @return   the size in bytes of a scalar type; the pointer size for
 *           `_BitInt`, `_Atomic` and function types, whose real size
 *           this kind does not carry; -1 for anything else
 */
static long type_size_bytes(cc_type_t t) {
    long n = cc_type_scalar_size(t);

    if (n >= 0) {
        return n;
    }
    if (t == CC_TYPE_BITINT || t == CC_TYPE_ATOMIC || t == CC_TYPE_FUNC) {
        return cc_target_pointer_size();
    }
    return -1;
}

static long type_size_bytes_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
    if (t == CC_TYPE_ATOMIC) {
        if (struct_id <= 0) {
            return -1;
        }
        return type_size_bytes_struct(tu, (cc_type_t)struct_id, -1);
    }
    if (t == CC_TYPE_BITINT) {
        return cc_bitint_size(struct_id);
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

static long array_type_size_bytes(const cc_translation_unit_t *tu, cc_type_t t, int struct_id, int array_ndim,
                                  const long array_dims[CC_MAX_ARRAY_DIMS]) {
    cc_type_t elem_type;
    int elem_struct_id;
    long elem_size;
    long count = 1;
    int i;

    if (array_ndim <= 0) {
        return type_size_bytes_struct(tu, t, struct_id);
    }
    elem_type = t;
    elem_struct_id = struct_id;
    if (is_pointer_type(t)) {
        for (i = 0; i < array_ndim; ++i) {
            elem_type = ptr_base_type(elem_type);
            if (elem_struct_id >= 0 && elem_type != CC_TYPE_VOID && !is_pointer_type(elem_type)) {
                elem_struct_id = -1;
            }
        }
    }
    elem_size = type_size_bytes_struct(tu, elem_type, elem_struct_id);
    if (elem_size <= 0) {
        return -1;
    }
    for (i = 0; i < array_ndim; ++i) {
        long d = array_dims != NULL ? array_dims[i] : 0;
        if (d <= 0 || count > LONG_MAX / d) {
            return -1;
        }
        count *= d;
    }
    if (elem_size > LONG_MAX / count) {
        return -1;
    }
    return elem_size * count;
}

/* sizeof(type): as gcc, sizeof(void) is 1 */
static long sizeof_type_bytes(const cc_translation_unit_t *tu, cc_type_t t, int struct_id, int array_ndim,
                              const long array_dims[CC_MAX_ARRAY_DIMS]) {
    if (t == CC_TYPE_VOID && struct_id < 0 && array_ndim <= 0) {
        return 1;
    }
    return array_type_size_bytes(tu, t, struct_id, array_ndim, array_dims);
}

/**
 * The size in bytes of a sizeof expression, for the constant evaluator:
 * of a string literal, an array, or any other expression or type.
 *
 * @param ctx  the translation unit
 * @param e    the CC_EXPR_SIZEOF node
 * @return     the size, or -1 if it is not known
 */
static long sema_const_sizeof(void *ctx, const cc_expr_t *e) {
    const cc_translation_unit_t *tu = (const cc_translation_unit_t *)ctx;

    if (e->lhs == NULL) {
        return sizeof_type_bytes(tu, e->aux_type, e->aux_struct_id, e->array_ndim, e->array_dims);
    }
    if (e->lhs->kind == CC_EXPR_STR) {
        int wide = cc_string_literal_is_wide(e->lhs);
        size_t units = decoded_string_unit_count(e->lhs, wide);
        cc_type_t elem_type = ptr_base_type(e->lhs->value_type);
        long elem_size = type_size_bytes_struct(tu, elem_type, e->lhs->struct_id);
        if (units < (size_t)LONG_MAX && elem_size > 0 && (long)(units + 1) <= LONG_MAX / elem_size) {
            return (long)(units + 1) * elem_size;
        }
    }
    if (e->lhs->array_ndim > 0 && is_pointer_type(e->lhs->value_type)) {
        return array_type_size_bytes(tu, e->lhs->value_type, e->lhs->struct_id, e->lhs->array_ndim,
                                     e->lhs->array_dims);
    }
    return type_size_bytes_struct(tu, e->lhs->value_type, e->lhs->struct_id);
}

/**
 * The size of a type in bytes, for the constant evaluator.
 *
 * @param ctx        the translation unit
 * @param t          the type
 * @param struct_id  its struct id, or -1
 * @return           the size, or <= 0 if not known
 */
static long sema_const_type_size(void *ctx, cc_type_t t, int struct_id) {
    return type_size_bytes_struct((const cc_translation_unit_t *)ctx, t, struct_id);
}

/**
 * Evaluate an integer constant expression in sema's context.
 *
 * @param tu   the translation unit
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
static int eval_const_int_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long long *out) {
    cc_const_env_t env;

    env.sizeof_bytes = sema_const_sizeof;
    env.type_size = sema_const_type_size;
    env.ctx = (void *)tu;
    return cc_const_eval_ll(&env, e, out);
}

enum {
    CC_ATOMIC_RELAXED = 0,
    CC_ATOMIC_CONSUME = 1,
    CC_ATOMIC_ACQUIRE = 2,
    CC_ATOMIC_RELEASE = 3,
    CC_ATOMIC_ACQ_REL = 4,
    CC_ATOMIC_SEQ_CST = 5
};

static int atomic_memorder_is_valid(long mo) {
    return mo >= CC_ATOMIC_RELAXED && mo <= CC_ATOMIC_SEQ_CST;
}

static int atomic_memorder_valid_for_load(long mo) {
    return mo == CC_ATOMIC_RELAXED || mo == CC_ATOMIC_CONSUME || mo == CC_ATOMIC_ACQUIRE ||
           mo == CC_ATOMIC_SEQ_CST;
}

static int atomic_memorder_valid_for_store(long mo) {
    return mo == CC_ATOMIC_RELAXED || mo == CC_ATOMIC_RELEASE || mo == CC_ATOMIC_SEQ_CST;
}

static int atomic_memorder_check_arg(const cc_translation_unit_t *tu, cc_expr_t *arg, const char *builtin_name,
                                     const char *op_kind, int allow_load_orders, int allow_store_orders,
                                     cc_diag_t *diag) {
    long long mo = -1;
    char msg[192];

    if (arg == NULL) {
        snprintf(msg, sizeof(msg), "%s %s memory-order argument is missing", builtin_name, op_kind);
        set_diag(diag, msg);
        return -1;
    }
    if (eval_const_int_expr(tu, arg, &mo) != 0) {
        snprintf(msg, sizeof(msg), "%s %s memory-order argument must be an integer constant expression", builtin_name,
                 op_kind);
        set_diag(diag, msg);
        return -1;
    }
    if (!atomic_memorder_is_valid(mo)) {
        snprintf(msg, sizeof(msg), "%s %s memory-order argument must be in range [0,5]", builtin_name, op_kind);
        set_diag(diag, msg);
        return -1;
    }
    if (allow_load_orders && !atomic_memorder_valid_for_load(mo)) {
        snprintf(msg, sizeof(msg), "%s load memory order must be relaxed/consume/acquire/seq_cst", builtin_name);
        set_diag(diag, msg);
        return -1;
    }
    if (allow_store_orders && !atomic_memorder_valid_for_store(mo)) {
        snprintf(msg, sizeof(msg), "%s store memory order must be relaxed/release/seq_cst", builtin_name);
        set_diag(diag, msg);
        return -1;
    }
    return 0;
}

static int check_struct_initializer(const cc_translation_unit_t *tu, const char *name, int struct_id, cc_expr_t *init,
                                    var_entry_t *vars, size_t var_count, int depth, cc_diag_t *diag);
static int check_array_initializer(const cc_translation_unit_t *tu, const char *name, cc_type_t array_type,
                                   int array_struct_id, long array_len, int array_ndim, const long *array_dims,
                                   cc_expr_t *init, var_entry_t *vars,
                                   size_t var_count, int depth, long *out_inferred_len, cc_diag_t *diag);
static cc_expr_t *unwrap_scalar_initializer_expr(cc_expr_t *init, cc_diag_t *diag);
static int check_stmt(const cc_translation_unit_t *tu, cc_stmt_t *s, var_entry_t **vars, size_t *var_count, int depth,
                      cc_type_t fn_ret_type, int fn_ret_struct_id, cc_attr_flags_t fn_attr_flags, int loop_depth,
                      int switch_depth,
                      int *saw_return, cc_diag_t *diag);

/**
 * The element type of a complex type -- which the legacy encoding gives as
 * the structure `{ T __re, __im; }` standing for it -- or CC_TYPE_VOID if
 * @p t is not complex.
 */
static cc_type_t sema_complex_elem(const cc_translation_unit_t *tu, cc_type_t t, int sid) {
    if (t != CC_TYPE_VOID || sid < 0 || tu == NULL || (size_t)sid >= tu->struct_count) {
        return CC_TYPE_VOID;
    }
    return tu->structs[sid].complex_elem;
}

/** The structure standing for _Complex @p elem, or -1 if there is none. */
static int sema_complex_sid(const cc_translation_unit_t *tu, cc_type_t elem) {
    size_t i;

    for (i = 0; tu != NULL && i < tu->struct_count; ++i) {
        if (tu->structs[i].complex_elem == elem && elem != CC_TYPE_VOID) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * The floating rank a type contributes to a complex operation's result:
 * 1 float, 2 double, 3 long double; 0 for an integer (C11 6.3.1.8: the
 * complex operand's element decides then).
 */
static int sema_float_rank(cc_type_t t) {
    return t == CC_TYPE_FLOAT ? 1 : (t == CC_TYPE_DOUBLE ? 2 : (t == CC_TYPE_LDOUBLE ? 3 : 0));
}

/**
 * The type of an operand of a complex operation, as a floating rank, or -1
 * if it may not be one (a pointer, a structure).
 */
static int sema_complex_operand_rank(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    cc_type_t ce = sema_complex_elem(tu, e->value_type, e->struct_id);

    if (ce != CC_TYPE_VOID) {
        return sema_float_rank(ce);
    }
    if (cc_type_is_integral(e->value_type) && !is_pointer_type(e->value_type)) {
        return 0;
    }
    if (e->value_type == CC_TYPE_FLOAT || e->value_type == CC_TYPE_DOUBLE || e->value_type == CC_TYPE_LDOUBLE) {
        return sema_float_rank(e->value_type);
    }
    return -1;
}

/**
 * Make an implicit conversion explicit: put a cast to (@p t, @p sid) above
 * the expression in @p slot.  Lowering converts between a complex value and
 * a real one only where a cast says so.
 *
 * @return 0, or -1 when out of memory
 */
static int sema_wrap_cast(cc_expr_t **slot, cc_type_t t, int sid) {
    cc_expr_t *c = (cc_expr_t *)calloc(1, sizeof(*c));

    if (c == NULL) {
        return -1;
    }
    c->kind = CC_EXPR_CAST;
    c->line = (*slot)->line;
    c->col = (*slot)->col;
    c->aux_type = t;
    c->aux_struct_id = sid;
    c->value_type = t;
    c->struct_id = sid;
    c->lhs = *slot;
    *slot = c;
    return 0;
}

static int sema_complex_truth(const cc_translation_unit_t *tu, cc_expr_t **slot);

/**
 * Read one part of a complex value: the member `__re` or `__im` of the
 * structure standing for its type.  Replaces the expression in @p slot.
 *
 * @param tu    the translation unit
 * @param slot  a complex expression, checked
 * @param imag  0 for the real part, 1 for the imaginary
 * @return      0, or -1 when out of memory
 */
static int sema_complex_part(const cc_translation_unit_t *tu, cc_expr_t **slot, int imag) {
    cc_type_t elem = sema_complex_elem(tu, (*slot)->value_type, (*slot)->struct_id);
    cc_expr_t *m = (cc_expr_t *)calloc(1, sizeof(*m));

    if (m == NULL) {
        return -1;
    }
    m->ident = cc_strdup(imag ? "__im" : "__re");
    if (m->ident == NULL) {
        free(m);
        return -1;
    }
    m->kind = CC_EXPR_MEMBER;
    m->line = (*slot)->line;
    m->col = (*slot)->col;
    m->lhs = *slot;
    m->member_offset = imag ? type_size_bytes(elem) : 0;
    m->value_type = elem;
    m->struct_id = -1;
    *slot = m;
    return 0;
}

/**
 * Where a value of the source's type is converted to (@p t, @p sid) by
 * assignment -- an initializer, an argument, a return -- and one of the two
 * is complex and they differ: put the conversion in as a cast.
 *
 * @return 1 if a cast was added, 0 if none was needed, -1 when out of memory
 */
static int sema_complex_convert_for_assign(const cc_translation_unit_t *tu, cc_expr_t **slot, cc_type_t t, int sid) {
    cc_type_t de = sema_complex_elem(tu, t, sid);
    cc_type_t se;

    if (*slot == NULL) {
        return 0;
    }
    se = sema_complex_elem(tu, (*slot)->value_type, (*slot)->struct_id);
    if ((de == CC_TYPE_VOID && se == CC_TYPE_VOID) || (de == se && de != CC_TYPE_VOID)) {
        return 0;
    }
    if (de != CC_TYPE_VOID ? sema_complex_operand_rank(tu, *slot) < 0
                           : !(cc_type_is_integral(t) || t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE ||
                               t == CC_TYPE_LDOUBLE || t == CC_TYPE_BOOL)) {
        return 0;
    }
    if (de == CC_TYPE_VOID && t == CC_TYPE_BOOL) {
        /* to _Bool: whether either part is nonzero */
        return sema_complex_truth(tu, slot) == 0 ? 1 : -1;
    }
    if (de == CC_TYPE_VOID && sema_complex_part(tu, slot, 0) != 0) {
        /* to a real type: the real part, converted */
        return -1;
    }
    return sema_wrap_cast(slot, t, sid) == 0 ? 1 : -1;
}

/**
 * A complex value where a truth value is wanted (`!z`, `z && x`, `if (z)`):
 * compare it with zero, as C does (6.5.3.3, 6.8.4.1).
 *
 * @return 0, or -1 when out of memory
 */
static int sema_complex_truth(const cc_translation_unit_t *tu, cc_expr_t **slot) {
    cc_expr_t *ne;
    cc_expr_t *zero;

    if (*slot == NULL || sema_complex_elem(tu, (*slot)->value_type, (*slot)->struct_id) == CC_TYPE_VOID) {
        return 0;
    }
    ne = (cc_expr_t *)calloc(1, sizeof(*ne));
    zero = (cc_expr_t *)calloc(1, sizeof(*zero));
    if (ne == NULL || zero == NULL) {
        free(ne);
        free(zero);
        return -1;
    }
    zero->kind = CC_EXPR_INT;
    zero->value_type = CC_TYPE_INT;
    zero->struct_id = -1;
    ne->kind = CC_EXPR_BIN;
    ne->op = CC_BIN_NE;
    ne->line = (*slot)->line;
    ne->col = (*slot)->col;
    ne->lhs = *slot;
    ne->rhs = zero;
    ne->value_type = CC_TYPE_INT;
    ne->struct_id = -1;
    *slot = ne;
    return 0;
}

/**
 * A binary operator with a complex operand: +, -, *, / give the complex
 * type of the operands' larger floating rank (an integer operand takes the
 * complex one's), == and != give int, `~z` (GNU, parsed as z ^ -1) is the
 * conjugate; any other operator is an error.  The operands keep their types;
 * lowering converts a real one as it reads it.
 *
 * @return 0, or -1 on error (diagnosed)
 */
static int sema_complex_binop(const cc_translation_unit_t *tu, cc_expr_t *e, cc_diag_t *diag) {
    int lr = sema_complex_operand_rank(tu, e->lhs);
    int rr = sema_complex_operand_rank(tu, e->rhs);
    int rank;
    int sid;

    if (e->op == CC_BIN_BXOR && sema_complex_elem(tu, e->lhs->value_type, e->lhs->struct_id) != CC_TYPE_VOID &&
        e->rhs->kind == CC_EXPR_INT && e->rhs->int_val == -1) {
        e->value_type = e->lhs->value_type;
        e->struct_id = e->lhs->struct_id;
        return 0;
    }
    if (lr < 0 || rr < 0 ||
        !(e->op == CC_BIN_ADD || e->op == CC_BIN_SUB || e->op == CC_BIN_MUL || e->op == CC_BIN_DIV ||
          e->op == CC_BIN_EQ || e->op == CC_BIN_NE)) {
        set_diag(diag, "invalid operands to a complex operation");
        return -1;
    }
    if (e->op == CC_BIN_EQ || e->op == CC_BIN_NE) {
        e->value_type = CC_TYPE_INT;
        e->struct_id = -1;
        return 0;
    }
    rank = lr > rr ? lr : rr;
    sid = sema_complex_sid(tu, rank == 1 ? CC_TYPE_FLOAT : (rank == 3 ? CC_TYPE_LDOUBLE : CC_TYPE_DOUBLE));
    if (sid < 0) {
        set_diag(diag, "complex result type is not available");
        return -1;
    }
    e->value_type = CC_TYPE_VOID;
    e->struct_id = sid;
    return 0;
}

static int check_expr(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count, int depth,
                      cc_diag_t *diag);

/** @p v rounded to the floating type @p t (float, double or long double). */
static long double sema_round_to(cc_type_t t, long double v) {
    if (t == CC_TYPE_FLOAT) {
        return (float)v;
    }
    return t == CC_TYPE_LDOUBLE ? v : (long double)(double)v;
}

/**
 * Fold a checked arithmetic constant expression, real or complex, to its
 * two parts, each operation rounded to its type: what a static complex
 * object's initializer must be (C11 6.6p7).
 *
 * @return 0, or -1 if it is not such a constant
 */
static int sema_fold_complex(const cc_translation_unit_t *tu, const cc_expr_t *e, long double *re, long double *im,
                             int depth) {
    cc_type_t elem;

    if (e == NULL || depth > 64) {
        return -1;
    }
    elem = sema_complex_elem(tu, e->value_type, e->struct_id);
    if (elem == CC_TYPE_VOID) {
        elem = e->value_type;
    }
    switch (e->kind) {
    case CC_EXPR_INT:
        *re = cc_type_is_unsigned_integral(e->value_type) ? (long double)(unsigned long long)e->int_val
                                                           : (long double)e->int_val;
        *im = 0;
        return 0;
    case CC_EXPR_FLOAT:
        *re = e->value_type == CC_TYPE_LDOUBLE ? e->float_val_ld : (long double)e->float_val;
        *im = 0;
        return 0;
    case CC_EXPR_COMPLEX: {
        long double r1, i1, r2, i2;

        if (sema_fold_complex(tu, e->lhs, &r1, &i1, depth + 1) != 0 ||
            sema_fold_complex(tu, e->rhs, &r2, &i2, depth + 1) != 0) {
            return -1;
        }
        *re = sema_round_to(elem, r1);
        *im = sema_round_to(elem, r2);
        return 0;
    }
    case CC_EXPR_CAST:
        if (sema_fold_complex(tu, e->lhs, re, im, depth + 1) != 0) {
            return -1;
        }
        if (sema_complex_elem(tu, e->value_type, e->struct_id) == CC_TYPE_VOID) {
            *im = 0;
            if (cc_type_is_integral(e->value_type)) {
                *re = (long double)(long long)*re;
                return 0;
            }
        }
        *re = sema_round_to(elem, *re);
        *im = sema_round_to(elem, *im);
        return 0;
    case CC_EXPR_BIN: {
        long double a, b, c, d;

        if (sema_fold_complex(tu, e->lhs, &a, &b, depth + 1) != 0 ||
            sema_fold_complex(tu, e->rhs, &c, &d, depth + 1) != 0) {
            return -1;
        }
        switch (e->op) {
        case CC_BIN_ADD:
            *re = a + c;
            *im = b + d;
            break;
        case CC_BIN_SUB:
            *re = a - c;
            *im = b - d;
            break;
        case CC_BIN_MUL:
            *re = sema_round_to(elem, a * c) - sema_round_to(elem, b * d);
            *im = sema_round_to(elem, a * d) + sema_round_to(elem, b * c);
            if (b == 0 && d == 0) {
                *im = 0;
            }
            break;
        case CC_BIN_DIV:
            if (d == 0 && c != 0) {
                *re = a / c;
                *im = b / c;
            } else {
                long double den = c * c + d * d;

                if (den == 0) {
                    return -1;
                }
                *re = (a * c + b * d) / den;
                *im = (b * c - a * d) / den;
            }
            break;
        default:
            return -1;
        }
        *re = sema_round_to(elem, *re);
        *im = sema_round_to(elem, *im);
        return 0;
    }
    default:
        return -1;
    }
}

/** A floating literal node of type @p t and value @p v, or NULL when out of memory. */
static cc_expr_t *sema_float_literal(cc_type_t t, long double v, size_t line, size_t col) {
    cc_expr_t *f = (cc_expr_t *)calloc(1, sizeof(*f));

    if (f != NULL) {
        f->kind = CC_EXPR_FLOAT;
        f->line = line;
        f->col = col;
        f->float_val = (double)v;
        f->float_val_ld = v;
        f->value_type = t;
        f->struct_id = -1;
    }
    return f;
}

/**
 * Make a checked initializer of a static complex object of element type
 * @p elem the brace list `{ re, im }` of its folded constant parts, which
 * the structure-initializer path lays out.
 *
 * @return 0, or -1 with @p diag set if it is not a constant
 */
static int sema_complex_static_init(const cc_translation_unit_t *tu, cc_expr_t **slot, cc_type_t elem,
                                    cc_diag_t *diag) {
    long double re;
    long double im;
    cc_expr_t *list;
    cc_expr_t **items;

    if (sema_fold_complex(tu, *slot, &re, &im, 0) != 0) {
        set_diag(diag, "initializer of a static complex object is not an arithmetic constant");
        return -1;
    }
    list = (cc_expr_t *)calloc(1, sizeof(*list));
    items = (cc_expr_t **)calloc(2, sizeof(*items));
    if (list == NULL || items == NULL) {
        free(list);
        free(items);
        set_diag(diag, "out of memory");
        return -1;
    }
    items[0] = sema_float_literal(elem, sema_round_to(elem, re), (*slot)->line, (*slot)->col);
    items[1] = sema_float_literal(elem, sema_round_to(elem, im), (*slot)->line, (*slot)->col);
    if (items[0] == NULL || items[1] == NULL) {
        free(items[0]);
        free(items[1]);
        free(items);
        free(list);
        set_diag(diag, "out of memory");
        return -1;
    }
    list->kind = CC_EXPR_INIT_LIST;
    list->line = (*slot)->line;
    list->col = (*slot)->col;
    list->args = items;
    list->arg_count = 2;
    list->value_type = CC_TYPE_VOID;
    list->struct_id = -1;
    cc_expr_free(*slot);
    *slot = list;
    return 0;
}

/**
 * A checked controlling expression (of if, while, do, for, ?:) as a
 * scalar: a complex one becomes the test of whether either part is
 * nonzero (C11 6.8.4.1, 6.3.1.2).
 *
 * @return 0, or -1 with @p diag set when out of memory
 */
static int sema_condition(const cc_translation_unit_t *tu, cc_expr_t **slot, cc_diag_t *diag) {
    if (*slot == NULL || sema_complex_elem(tu, (*slot)->value_type, (*slot)->struct_id) == CC_TYPE_VOID) {
        return 0;
    }
    if (sema_complex_truth(tu, slot) != 0) {
        set_diag(diag, "out of memory");
        return -1;
    }
    return 0;
}

/**
 * Check a complex value built from its parts (CC_EXPR_COMPLEX, checked
 * children): its type is _Complex of the parts' floating type, the wider
 * if they differ (an integer part counts as double), and each part is
 * converted to that.
 *
 * @return 0, or -1 with @p diag set
 */
static int sema_check_complex_parts(const cc_translation_unit_t *tu, cc_expr_t *e, cc_diag_t *diag) {
    int lr = sema_complex_operand_rank(tu, e->lhs);
    int rr = sema_complex_operand_rank(tu, e->rhs);
    int rank;
    cc_type_t elem;
    int sid;

    if (lr < 0 || rr < 0 || sema_complex_elem(tu, e->lhs->value_type, e->lhs->struct_id) != CC_TYPE_VOID ||
        sema_complex_elem(tu, e->rhs->value_type, e->rhs->struct_id) != CC_TYPE_VOID) {
        set_diag(diag, "the parts of a complex value must be real arithmetic values");
        return -1;
    }
    rank = lr > rr ? lr : rr;
    elem = rank == 1 ? CC_TYPE_FLOAT : (rank == 3 ? CC_TYPE_LDOUBLE : CC_TYPE_DOUBLE);
    sid = sema_complex_sid(tu, elem);
    if (sid < 0) {
        set_diag(diag, "complex type has no layout");
        return -1;
    }
    if ((e->lhs->value_type != elem && sema_wrap_cast(&e->lhs, elem, -1) != 0) ||
        (e->rhs->value_type != elem && sema_wrap_cast(&e->rhs, elem, -1) != 0)) {
        set_diag(diag, "out of memory");
        return -1;
    }
    e->value_type = CC_TYPE_VOID;
    e->struct_id = sid;
    return 0;
}

/**
 * Check GNU __builtin_complex(re, im) and make it the CC_EXPR_COMPLEX it
 * stands for.  As in gcc, both arguments must have the same real floating
 * type.
 *
 * @return 0, or -1 with @p diag set
 */
static int sema_builtin_complex(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count,
                                int depth, cc_diag_t *diag) {
    cc_expr_t **args = e->args;

    if (e->arg_count != 2) {
        set_diag(diag, "__builtin_complex takes two arguments");
        return -1;
    }
    if (check_expr(tu, args[0], vars, var_count, depth, diag) != 0 ||
        check_expr(tu, args[1], vars, var_count, depth, diag) != 0) {
        return -1;
    }
    if (sema_float_rank(args[0]->value_type) == 0 || args[0]->value_type != args[1]->value_type) {
        set_diag(diag, "both arguments to '__builtin_complex' must have the same real floating type");
        return -1;
    }
    free(e->ident);
    e->ident = NULL;
    e->kind = CC_EXPR_COMPLEX;
    e->lhs = args[0];
    e->rhs = args[1];
    e->args = NULL;
    e->arg_count = 0;
    free(args);
    return sema_check_complex_parts(tu, e, diag);
}

/**
 * Check GNU __real__ x or __imag__ x (a member access marked
 * member_is_part) where x is real: __real__ x is x, converted to its own
 * type so it is no lvalue, and __imag__ x is zero after x is evaluated.
 * A complex x needs nothing: the access names a member of its structure.
 *
 * @return 1 if handled here, 0 if x is complex, -1 with @p diag set
 */
static int sema_real_part_of_real(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars,
                                  size_t var_count, int depth, cc_diag_t *diag) {
    cc_expr_t *x = e->lhs;
    cc_type_t t;

    if (check_expr(tu, x, vars, var_count, depth, diag) != 0) {
        return -1;
    }
    if (sema_complex_elem(tu, x->value_type, x->struct_id) != CC_TYPE_VOID) {
        return 0;
    }
    t = x->value_type;
    if (is_pointer_type(t) || !(cc_type_is_integral(t) || sema_float_rank(t) > 0)) {
        set_diag(diag, "__real__ and __imag__ need an arithmetic operand");
        return -1;
    }
    if (strcmp(e->ident, "__im") == 0) {
        cc_expr_t *zero = (cc_expr_t *)calloc(1, sizeof(*zero));

        if (zero == NULL) {
            set_diag(diag, "out of memory");
            return -1;
        }
        zero->kind = CC_EXPR_INT;
        zero->line = e->line;
        zero->col = e->col;
        zero->value_type = CC_TYPE_INT;
        zero->struct_id = -1;
        e->kind = CC_EXPR_BIN;
        e->op = CC_BIN_COMMA;
        e->rhs = zero;
        if (sema_wrap_cast(&e->rhs, t, -1) != 0) {
            set_diag(diag, "out of memory");
            return -1;
        }
    } else {
        e->kind = CC_EXPR_CAST;
        e->aux_type = t;
        e->aux_struct_id = -1;
    }
    free(e->ident);
    e->ident = NULL;
    e->member_is_part = 0;
    e->value_type = t;
    e->struct_id = -1;
    return 1;
}

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
    if (e->kind != CC_EXPR_CAST && !(e->kind == CC_EXPR_SIZEOF && e->lhs == NULL)) {
        expr_clear_array_meta(e);
    }

    switch (e->kind) {
    case CC_EXPR_INT:
        if (!cc_type_is_integral(e->value_type)) {
            e->value_type = CC_TYPE_INT;
        }
        e->struct_id = -1;
        return 0;

    case CC_EXPR_FLOAT:
        if (!cc_type_is_floating(e->value_type)) {
            e->value_type = CC_TYPE_DOUBLE;
        }
        e->struct_id = -1;
        return 0;

    case CC_EXPR_STR:
        e->value_type = cc_string_literal_pointer_type(cc_string_literal_is_wide(e) ? e->aux_type : CC_TYPE_CHAR);
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
            unwrap_atomic_expr_type(&e->value_type, &e->struct_id);
            expr_set_array_meta_decl(e, vars[idx].array_len, vars[idx].array_ndim, vars[idx].array_dims);
            return 0;
        }
        if (e->ident != NULL &&
            (strcmp(e->ident, "__func__") == 0 || strcmp(e->ident, "__FUNCTION__") == 0 ||
             strcmp(e->ident, "__PRETTY_FUNCTION__") == 0)) {
            e->value_type = CC_TYPE_PTR_CHAR;
            e->struct_id = -1;
            return 0;
        }
        {
            const cc_global_t *g = find_global(tu, e->ident);
            if (g != NULL) {
                e->value_type = g->type;
                e->struct_id = g->type_struct_id;
                unwrap_atomic_expr_type(&e->value_type, &e->struct_id);
                expr_set_array_meta_decl(e, g->array_len, g->array_ndim, g->array_dims);
                if (maybe_warn_deprecated_symbol(tu, e->ident, e->line, e->col, diag) != 0) {
                    return -1;
                }
                return 0;
            }
            if (find_function(tu, e->ident) != NULL) {
                e->value_type = CC_TYPE_FUNC;
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

    case CC_EXPR_COMPLEX:
        if (e->lhs == NULL || e->rhs == NULL || check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0 ||
            check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        return sema_check_complex_parts(tu, e, diag);

    case CC_EXPR_MEMBER: {
        const cc_struct_member_t *m;
        int sid = -1;
        if (e->member_is_part && e->lhs != NULL && e->ident != NULL) {
            int rc = sema_real_part_of_real(tu, e, vars, var_count, depth, diag);

            if (rc != 0) {
                return rc < 0 ? -1 : 0;
            }
        }
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
        {
            long member_off = 0;
            if (find_struct_member_recursive(tu, sid, e->ident, 0, &m, &member_off) != 0 || m == NULL) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "no member named %s", e->ident);
                }
                return -1;
            }
            e->member_offset = member_off;
        }
        if (m == NULL) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "no member named %s", e->ident);
            }
            return -1;
        }
        e->value_type = m->type;
        e->struct_id = m->type_struct_id;
        e->member_is_bitfield = m->is_bitfield;
        e->member_bit_width = m->bit_width;
        e->member_bit_offset = m->bit_offset;
        e->member_bit_storage_bits = m->bit_storage_bits;
        e->member_bit_signed = m->bit_signed;
        e->member_bit_storage_size = m->bit_storage_size;
        e->ctype = m->ctype;
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
        if (sema_complex_elem(tu, e->lhs->value_type, e->lhs->struct_id) != CC_TYPE_VOID ||
            sema_complex_elem(tu, e->rhs->value_type, e->rhs->struct_id) != CC_TYPE_VOID) {
            if (is_logical_op(e->op)) {
                if (sema_complex_truth(tu, &e->lhs) != 0 || sema_complex_truth(tu, &e->rhs) != 0) {
                    set_diag(diag, "out of memory converting a complex operand");
                    return -1;
                }
                e->value_type = CC_TYPE_INT;
                return 0;
            }
            return sema_complex_binop(tu, e, diag);
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
            if (!cc_type_is_integral(e->lhs->value_type) || !cc_type_is_integral(e->rhs->value_type)) {
                set_diag(diag, "shift operators require integer operands");
                return -1;
            }
            e->value_type = cc_type_promote(e->lhs->value_type);
            if (e->value_type == CC_TYPE_VOID) {
                set_diag(diag, "void expression used in arithmetic operation");
                return -1;
            }
            return 0;
        }
        if (is_bitwise_op(e->op)) {
            if (!cc_type_is_integral(e->lhs->value_type) || !cc_type_is_integral(e->rhs->value_type)) {
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
                if (cc_type_is_numeric(e->lhs->value_type) && cc_type_is_numeric(e->rhs->value_type)) {
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
                if (is_pointer_type(e->lhs->value_type) && cc_type_is_integral(e->rhs->value_type) &&
                    is_null_ptr_constant(e->rhs)) {
                    e->value_type = CC_TYPE_INT;
                    return 0;
                }
                if (is_pointer_type(e->rhs->value_type) && cc_type_is_integral(e->lhs->value_type) &&
                    is_null_ptr_constant(e->lhs)) {
                    e->value_type = CC_TYPE_INT;
                    return 0;
                }
                if ((is_pointer_type(e->lhs->value_type) && cc_type_is_integral(e->rhs->value_type)) ||
                    (is_pointer_type(e->rhs->value_type) && cc_type_is_integral(e->lhs->value_type))) {
                    if (cc_options_current()->pedantic && emit_warning(diag, e->line, e->col,
                                                   "comparison between pointer and integer is a GNU extension", 1) != 0) {
                        return -1;
                    }
                    e->value_type = CC_TYPE_INT;
                    return 0;
                }
                if (getenv("CC_DEBUG_SEMA_CMP") != NULL) {
                    fprintf(stderr,
                            "cc-debug: eq/ne cmp lhs(kind=%d type=%d sid=%d ident=%s int=%lld) rhs(kind=%d type=%d sid=%d ident=%s int=%lld) line=%zu col=%zu\n",
                            (int)e->lhs->kind, (int)e->lhs->value_type, e->lhs->struct_id,
                            e->lhs->ident != NULL ? e->lhs->ident : "<null>", e->lhs->int_val, (int)e->rhs->kind,
                            (int)e->rhs->value_type, e->rhs->struct_id, e->rhs->ident != NULL ? e->rhs->ident : "<null>",
                            e->rhs->int_val, e->line, e->col);
                }
                set_diag(diag, "comparison operators require compatible scalar operands");
                return -1;
            }
            if (cc_type_is_numeric(e->lhs->value_type) && cc_type_is_numeric(e->rhs->value_type)) {
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
                if (ptr_base_type(e->lhs->value_type) == CC_TYPE_VOID || ptr_base_type(e->rhs->value_type) == CC_TYPE_VOID) {
                    if (cc_options_current()->pedantic &&
                        emit_warning(diag, e->line, e->col, "arithmetic on 'void *' is a GNU extension", 1) != 0) {
                        return -1;
                    }
                }
                if (!can_convert(e->lhs->value_type, e->rhs->value_type) &&
                    !can_convert(e->rhs->value_type, e->lhs->value_type)) {
                    set_diag(diag, "incompatible pointer types in subtraction");
                    return -1;
                }
                e->value_type = CC_TYPE_INT;
                e->struct_id = -1;
                return 0;
            }
            if (is_pointer_type(e->lhs->value_type) && cc_type_is_integral(e->rhs->value_type)) {
                if (ptr_base_type(e->lhs->value_type) == CC_TYPE_VOID) {
                    if (cc_options_current()->pedantic &&
                        emit_warning(diag, e->line, e->col, "arithmetic on 'void *' is a GNU extension", 1) != 0) {
                        return -1;
                    }
                }
                e->value_type = e->lhs->value_type;
                e->struct_id = e->lhs->struct_id;
                expr_copy_array_meta(e, e->lhs);
                return 0;
            }
            if (e->op == CC_BIN_ADD && cc_type_is_integral(e->lhs->value_type) && is_pointer_type(e->rhs->value_type)) {
                if (ptr_base_type(e->rhs->value_type) == CC_TYPE_VOID) {
                    if (cc_options_current()->pedantic &&
                        emit_warning(diag, e->line, e->col, "arithmetic on 'void *' is a GNU extension", 1) != 0) {
                        return -1;
                    }
                }
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
            if (!cc_type_is_integral(e->lhs->value_type) || !cc_type_is_integral(e->rhs->value_type)) {
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
        if (e->ident != NULL && strcmp(e->ident, "__builtin_complex") == 0) {
            return sema_builtin_complex(tu, e, vars, var_count, depth, diag);
        }
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
        if (bk == BUILTIN_HUGE_VAL || bk == BUILTIN_HUGE_VALF || bk == BUILTIN_HUGE_VALL ||
            bk == BUILTIN_INF || bk == BUILTIN_INFF || bk == BUILTIN_INFL) {
            if (e->arg_count != 0) {
                set_diag(diag, "floating huge-value/infinity builtin expects no arguments");
                return -1;
            }
            e->value_type = (bk == BUILTIN_HUGE_VALF || bk == BUILTIN_INFF) ? CC_TYPE_FLOAT :
                            (bk == BUILTIN_HUGE_VALL || bk == BUILTIN_INFL) ? CC_TYPE_LDOUBLE : CC_TYPE_DOUBLE;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_LDBL_MIN || bk == BUILTIN_LDBL_DENORM_MIN || bk == BUILTIN_LDBL_MAX) {
            if (e->arg_count != 0) {
                set_diag(diag, "long double limit builtin expects no arguments");
                return -1;
            }
            e->value_type = CC_TYPE_LDOUBLE;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_ISFINITE || bk == BUILTIN_ISINF || bk == BUILTIN_ISINF_SIGN || bk == BUILTIN_ISNAN ||
            bk == BUILTIN_SIGNBIT) {
            if (e->arg_count != 1) {
                set_diag(diag, "floating classification builtin expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!cc_type_is_numeric(e->args[0]->value_type)) {
                set_diag(diag, "floating classification builtin expects numeric argument");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_NANF || bk == BUILTIN_NAN || bk == BUILTIN_NANL) {
            if (e->arg_count != 1) {
                set_diag(diag, "floating NaN builtin expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            e->value_type = (bk == BUILTIN_NANF) ? CC_TYPE_FLOAT :
                            (bk == BUILTIN_NANL) ? CC_TYPE_LDOUBLE : CC_TYPE_DOUBLE;
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
            if (!is_scalar_type(e->args[0]->value_type) || !cc_type_is_integral(e->args[1]->value_type)) {
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
            if (!cc_type_is_integral(e->args[1]->value_type) ||
                (e->arg_count == 3 && !cc_type_is_integral(e->args[2]->value_type))) {
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
        if (bk == BUILTIN_CLZ || bk == BUILTIN_CTZ || bk == BUILTIN_FFS) {
            const char *builtin_name = "__builtin_clz";
            if (bk == BUILTIN_CTZ) {
                builtin_name = "__builtin_ctz";
            } else if (bk == BUILTIN_FFS) {
                builtin_name = "__builtin_ffs";
            }
            if (e->arg_count != 1) {
                char msg[96];
                snprintf(msg, sizeof(msg), "%s expects exactly 1 argument", builtin_name);
                set_diag(diag, msg);
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!cc_type_is_integral(e->args[0]->value_type)) {
                char msg[96];
                snprintf(msg, sizeof(msg), "%s argument must be integral", builtin_name);
                set_diag(diag, msg);
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_POPCOUNT || bk == BUILTIN_PARITY) {
            const char *what = bk == BUILTIN_PARITY ? "__builtin_parity" : "__builtin_popcount";
            char msg[96];
            if (e->arg_count != 1) {
                snprintf(msg, sizeof(msg), "%s expects exactly 1 argument", what);
                set_diag(diag, msg);
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!cc_type_is_integral(e->args[0]->value_type)) {
                snprintf(msg, sizeof(msg), "%s argument must be integral", what);
                set_diag(diag, msg);
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
            if (!cc_type_is_integral(out_elem)) {
                set_diag(diag, "overflow builtin destination must point to an integer type");
                return -1;
            }
            if (!cc_type_is_integral(e->args[0]->value_type) || !cc_type_is_integral(e->args[1]->value_type)) {
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
            if (!cc_type_is_integral(e->args[0]->value_type) || !cc_type_is_integral(e->args[1]->value_type) ||
                !cc_type_is_integral(e->args[2]->value_type)) {
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
            if (!cc_type_is_integral(e->args[1]->value_type)) {
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
            if (!cc_type_is_integral(e->args[0]->value_type)) {
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
            if (!cc_type_is_integral(e->args[2]->value_type)) {
                set_diag(diag, "__builtin_memcmp third argument must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_PREFETCH) {
            if (e->arg_count < 1 || e->arg_count > 3) {
                set_diag(diag, "__builtin_prefetch expects 1 to 3 arguments");
                return -1;
            }
            for (i = 0; i < e->arg_count; ++i) {
                if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                    return -1;
                }
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_prefetch first argument must be a pointer");
                return -1;
            }
            if (e->arg_count >= 2 && !cc_type_is_integral(e->args[1]->value_type)) {
                set_diag(diag, "__builtin_prefetch rw argument must be integral");
                return -1;
            }
            if (e->arg_count >= 3 && !cc_type_is_integral(e->args[2]->value_type)) {
                set_diag(diag, "__builtin_prefetch locality argument must be integral");
                return -1;
            }
            e->value_type = CC_TYPE_VOID;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_CPU_SUPPORTS) {
            if (e->arg_count != 1) {
                set_diag(diag, "__builtin_cpu_supports expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (e->args[0]->kind != CC_EXPR_STR) {
                set_diag(diag, "__builtin_cpu_supports argument must be a string literal");
                return -1;
            }
            e->value_type = CC_TYPE_INT;
            e->struct_id = -1;
            return 0;
        }
        if (bk == BUILTIN_STRLEN) {
            if (e->arg_count != 1) {
                set_diag(diag, "__builtin_strlen expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!is_pointer_type(e->args[0]->value_type)) {
                set_diag(diag, "__builtin_strlen argument must be a pointer");
                return -1;
            }
            e->value_type = size_type();
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
            if ((bk == BUILTIN_MEMSET || bk == BUILTIN_MEMSET_CHK) && !cc_type_is_integral(e->args[1]->value_type)) {
                set_diag(diag, "memory builtin fill byte must be integral");
                return -1;
            }
            if (!cc_type_is_integral(e->args[2]->value_type)) {
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
        if (bk == BUILTIN_ATOMIC_THREAD_FENCE || bk == BUILTIN_ATOMIC_SIGNAL_FENCE) {
            const char *nm = bk == BUILTIN_ATOMIC_THREAD_FENCE ? "__atomic_thread_fence" : "__atomic_signal_fence";
            if (e->arg_count != 1) {
                set_diag(diag, bk == BUILTIN_ATOMIC_THREAD_FENCE ? "__atomic_thread_fence expects exactly 1 argument"
                                                                  : "__atomic_signal_fence expects exactly 1 argument");
                return -1;
            }
            if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!cc_type_is_integral(e->args[0]->value_type)) {
                set_diag(diag, bk == BUILTIN_ATOMIC_THREAD_FENCE
                                   ? "__atomic_thread_fence memory-order argument must be integral"
                                   : "__atomic_signal_fence memory-order argument must be integral");
                return -1;
            }
            if (atomic_memorder_check_arg(tu, e->args[0], nm, "fence", 0, 0, diag) != 0) {
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
            if (bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB || bk == BUILTIN_ATOMIC_EXCHANGE_N) {
                const char *nm = bk == BUILTIN_ATOMIC_EXCHANGE_N
                                     ? "__atomic_exchange_n"
                                     : (bk == BUILTIN_ATOMIC_FETCH_ADD ? "__atomic_fetch_add" : "__atomic_fetch_sub");
                if (!cc_type_is_integral(e->args[2]->value_type)) {
                    set_diag(diag, "atomic fetch/exchange memory-order argument must be integral");
                    return -1;
                }
                if (atomic_memorder_check_arg(tu, e->args[2], nm, "operation", 0, 0, diag) != 0) {
                    return -1;
                }
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
            if (!cc_type_is_integral(e->args[1]->value_type)) {
                set_diag(diag, "__atomic_load_n memory-order argument must be integral");
                return -1;
            }
            if (atomic_memorder_check_arg(tu, e->args[1], "__atomic_load_n", "load", 1, 0, diag) != 0) {
                return -1;
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
            if (!cc_type_is_integral(e->args[2]->value_type)) {
                set_diag(diag, "__atomic_store_n memory-order argument must be integral");
                return -1;
            }
            if (atomic_memorder_check_arg(tu, e->args[2], "__atomic_store_n", "store", 0, 1, diag) != 0) {
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
            if (!cc_type_is_integral(e->args[0]->value_type)) {
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
            /*
             * (*fp)(...) calls through fp.  A subscript, fps[i](...), is
             * parsed as *(fps + i); there the element itself is the
             * function pointer, and stripping a further level of
             * pointer would type the call's result as a pointer.
             */
            if (e->lhs->kind == CC_EXPR_DEREF && e->lhs->lhs != NULL && is_pointer_type(e->lhs->lhs->value_type) &&
                !(e->lhs->lhs->kind == CC_EXPR_BIN &&
                  (e->lhs->lhs->op == CC_BIN_ADD || e->lhs->lhs->op == CC_BIN_SUB) && is_pointer_type(e->lhs->value_type))) {
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
                sema_complex_convert_for_assign(tu, &e->args[i], callee->params[i].type,
                                                callee->params[i].type_struct_id) < 0) {
                set_diag(diag, "out of memory");
                return -1;
            }
            if (callee != NULL && callee->has_prototype && i < callee->param_count &&
                !can_convert(callee->params[i].type, e->args[i]->value_type) &&
                !(is_pointer_type(callee->params[i].type) && cc_type_is_integral(e->args[i]->value_type) &&
                  is_null_ptr_constant(e->args[i])) &&
                !(callee->params[i].type == CC_TYPE_VOID && callee->params[i].type_struct_id >= 0 &&
                  transparent_union_accepts_type(tu, callee->params[i].type_struct_id, e->args[i]->value_type,
                                                 e->args[i]->struct_id)) &&
                !(callee->params[i].type == CC_TYPE_VOID && callee->params[i].type_struct_id >= 0 &&
                  e->args[i]->kind == CC_EXPR_STMT && cc_type_is_integral(e->args[i]->value_type))) {
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
            char buf[192];
            /*
             * We currently record only the presence of nonnull, not its index list.
             * Avoid false positives (e.g. strtok uses nonnull(2)) by checking
             * only unambiguous single-parameter prototypes.
             */
            if (e->arg_count > 0 && callee->param_count == 1 && is_pointer_type(callee->params[0].type) &&
                is_null_ptr_constant(e->args[0])) {
                snprintf(buf, sizeof(buf), "nonnull argument 1 in call to %s is null",
                         e->ident != NULL ? e->ident : "<indirect>");
                if (emit_required_warning(diag, e->line, e->col, buf) != 0) {
                    return -1;
                }
            }
        }
        if (callee != NULL) {
            e->value_type = callee->ret_type;
            e->struct_id = callee->ret_struct_id;
            e->ctype = callee->ret_ctype;
            if (callee->ret_ctype != NULL) {
                /* a returned pointer to an array: the bounds behind it, as a pointer variable has them */
                cc_ct_legacy_t leg;
                if (cc_ct_to_legacy(callee->ret_ctype, &leg) == 0 && leg.ndim > 0) {
                    expr_set_array_meta_decl(e, -1, leg.ndim, leg.dims);
                }
            }
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
                if (!cc_builtin_is_recognized(e->ident)) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "unsupported builtin function: %s", e->ident);
                    }
                    return -1;
                }
                e->value_type = CC_TYPE_INT;
                e->struct_id = -1;
                return 0;
            }
            if (!cc_options_implicit_funcdecl()) {
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
        int dst_storage = 0;
        int dst_is_array_object = 0;
        int assign_ok;
        if (e->ident != NULL) {
            int idx = vars_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                dst_type = vars[idx].type;
                dst_struct_id = vars[idx].struct_id;
                dst_storage = vars[idx].storage;
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
                dst_storage = g->storage;
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
        if ((dst_storage & CC_STORAGE_CONST) != 0 && const_storage_applies_to_object(dst_type)) {
            if (diag != NULL && diag->message[0] == '\0') {
                if (e->ident != NULL) {
                    snprintf(diag->message, sizeof(diag->message), "cannot assign to const-qualified object: %s",
                             e->ident);
                } else {
                    snprintf(diag->message, sizeof(diag->message), "cannot assign to const-qualified object");
                }
            }
            return -1;
        }
        if (dst_is_array_object) {
            /* Linux's per_cpu/typeof macros declare temporaries with
             * typeof(some_array) which keeps array type. The subsequent
             * `__Fp = expr` would be illegal in strict C but gcc decays
             * the array to a pointer. Treat the array as a pointer for
             * the purposes of the assignment; lowering already produces
             * pointer-style stores. */
            if (e->ident != NULL && strcmp(e->ident, "__Fp") == 0) {
                /* fall through */
            } else if (diag != NULL && diag->message[0] == '\0') {
                if (e->ident != NULL) {
                    snprintf(diag->message, sizeof(diag->message), "array object is not a modifiable lvalue: %s",
                             e->ident);
                } else {
                    snprintf(diag->message, sizeof(diag->message), "array object is not a modifiable lvalue");
                }
                return -1;
            } else {
                return -1;
            }
        }
        if (check_expr(tu, e->rhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        {
            /* to or from a complex type: the conversion, explicit */
            int conv = sema_complex_convert_for_assign(tu, &e->rhs, dst_type, dst_struct_id);

            if (conv < 0) {
                set_diag(diag, "out of memory converting to or from a complex type");
                return -1;
            }
        }
        assign_ok = can_convert(dst_type, e->rhs->value_type) ||
                    (is_pointer_type(dst_type) && cc_type_is_integral(e->rhs->value_type) && is_null_ptr_constant(e->rhs));
        if (!assign_ok && dst_type == CC_TYPE_VOID && dst_struct_id >= 0 && e->rhs->value_type == CC_TYPE_VOID &&
            struct_ids_compatible(tu, dst_struct_id, e->rhs->struct_id)) {
            assign_ok = 1;
        }
        if (!assign_ok && dst_type == CC_TYPE_VOID && dst_struct_id >= 0 && cc_type_is_integral(e->rhs->value_type) &&
            e->rhs->kind == CC_EXPR_STMT) {
            assign_ok = 1;
        }
        if (!assign_ok && is_pointer_type(dst_type) && is_pointer_type(e->rhs->value_type) &&
            e->rhs->kind == CC_EXPR_ADDR && e->rhs->lhs != NULL && e->rhs->lhs->array_ndim > 0 &&
            pointer_ultimate_base_type(dst_type) == pointer_ultimate_base_type(e->rhs->value_type) &&
            pointer_depth(dst_type) > pointer_depth(e->rhs->value_type)) {
            assign_ok = 1;
        }
        if (!assign_ok) {
            /* Linux's per_cpu / typeof macros assign through __Fp where
             * the typeof has decayed differently than cc's strict view.
             * Permit the assignment; downstream codegen has to cope with
             * whatever the macro actually intends. */
            if (e->ident != NULL && strcmp(e->ident, "__Fp") == 0) {
                assign_ok = 1;
            }
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
        if (e->lhs->kind == CC_EXPR_MEMBER && e->lhs->member_is_bitfield) {
            set_diag(diag, "cannot take address of bit-field");
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
        if (cc_ct_expr_designates_function(e->lhs)) {
            /* `*fp` designates the function fp points to, which is used as fp again */
            e->value_type = e->lhs->value_type;
            e->struct_id = e->lhs->struct_id;
            e->array_ndim = e->lhs->array_ndim;
            memcpy(e->array_dims, e->lhs->array_dims, sizeof(e->array_dims));
            return 0;
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
        if (e->lhs->struct_id >= 0 && type_carries_struct_id(e->value_type)) {
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
            if (cc_options_current()->pedantic) {
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
        int t_storage = 0;
        int is_array_object = 0;
        if (e->ident != NULL) {
            int idx = vars_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                t = vars[idx].type;
                t_struct_id = vars[idx].struct_id;
                t_storage = vars[idx].storage;
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
                t_storage = g->storage;
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
        if ((t_storage & CC_STORAGE_CONST) != 0 && const_storage_applies_to_object(t)) {
            set_diag(diag, "cannot modify const-qualified object with ++/--");
            return -1;
        }
        if (is_pointer_type(t)) {
            if (ptr_base_type(t) == CC_TYPE_VOID) {
                if (cc_options_current()->pedantic &&
                    emit_warning(diag, e->line, e->col, "arithmetic on 'void *' is a GNU extension", 1) != 0) {
                    return -1;
                }
            }
        } else if (!cc_type_is_numeric(t)) {
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
        if (sema_complex_elem(tu, e->lhs->value_type, e->lhs->struct_id) != CC_TYPE_VOID &&
            cc_type_is_numeric(e->aux_type)) {
            /* complex to real: the real part (C11 6.3.1.7); to _Bool: whether either part is nonzero */
            if ((e->aux_type == CC_TYPE_BOOL ? sema_complex_truth(tu, &e->lhs)
                                             : sema_complex_part(tu, &e->lhs, 0)) != 0) {
                set_diag(diag, "out of memory");
                return -1;
            }
        }
        if (sema_complex_elem(tu, e->aux_type, e->aux_struct_id) != CC_TYPE_VOID &&
            sema_complex_operand_rank(tu, e->lhs) < 0) {
            set_diag(diag, "only an arithmetic value can be converted to a complex type");
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
        if (cc_type_is_numeric(e->lhs->value_type) && cc_type_is_numeric(e->aux_type)) {
            e->value_type = e->aux_type;
            e->struct_id = -1;
            return 0;
        }
        if (is_pointer_type(e->aux_type) &&
            (is_pointer_type(e->lhs->value_type) || cc_type_is_integral(e->lhs->value_type))) {
            e->value_type = e->aux_type;
            e->struct_id = e->aux_struct_id;
            if (is_pointer_type(e->lhs->value_type)) {
                expr_copy_array_meta(e, e->lhs);
            }
            return 0;
        }
        if (cc_type_is_integral(e->aux_type) && is_pointer_type(e->lhs->value_type)) {
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
            if (e->lhs->kind == CC_EXPR_MEMBER && e->lhs->member_is_bitfield) {
                set_diag(diag, "sizeof cannot be applied to bit-field");
                return -1;
            }
            if (type_size_bytes_struct(tu, e->lhs->value_type, e->lhs->struct_id) < 0) {
                set_diag(diag, "sizeof unsupported for this operand type");
                return -1;
            }
        } else {
            if (sizeof_type_bytes(tu, e->aux_type, e->aux_struct_id, e->array_ndim, e->array_dims) < 0) {
                set_diag(diag, "sizeof unsupported for this type");
                return -1;
            }
            if (e->aux_type == CC_TYPE_VOID && e->aux_struct_id < 0 && e->array_ndim <= 0 &&
                emit_warning(diag, e->line, e->col, "invalid application of 'sizeof' to a void type", 1) != 0) {
                return -1;
            }
        }
        e->value_type = size_type();
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
                /* Labels chain through `then_branch`; the value of the
                 * statement expression is whatever inner statement they
                 * eventually wrap. */
                while (last != NULL && last->kind == CC_STMT_LABEL) {
                    last = last->then_branch;
                }
                if (last != NULL && last->kind == CC_STMT_EXPR && last->expr != NULL) {
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
        if (sema_condition(tu, &e->lhs, diag) != 0) {
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
        if (is_pointer_type(e->rhs->value_type) && cc_type_is_integral(e->third->value_type) &&
            is_null_ptr_constant(e->third)) {
            e->value_type = e->rhs->value_type;
            e->struct_id = e->rhs->struct_id;
            return 0;
        }
        if (is_pointer_type(e->third->value_type) && cc_type_is_integral(e->rhs->value_type) &&
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
        if (cc_lang_at_least(2023)) {
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
    if (sd->members[member_idx].is_bitfield && sd->members[member_idx].bit_width > 0) {
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

static int struct_has_anonymous_aggregate_member(const cc_translation_unit_t *tu, int struct_id) {
    const cc_struct_def_t *sd;
    size_t i;

    sd = find_struct_def(tu, struct_id);
    if (sd == NULL || !sd->complete) {
        return 0;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (m->name == NULL && m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
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
    if (sd->is_union && init->arg_count > 1 && sd->member_count > 0) {
        const cc_struct_member_t *um = &sd->members[0];
        cc_expr_t *first = init->args[0];
        int first_is_designator =
            (first != NULL && first->kind == CC_EXPR_MEMBER && first->lhs == NULL && first->rhs != NULL && first->ident != NULL);
        if (!first_is_designator) {
            if (is_array_object_type(um->type, um->array_len, um->array_ndim)) {
                if (check_array_initializer(tu, name, um->type, um->type_struct_id, um->array_len, um->array_ndim,
                                            um->array_dims, init, vars, var_count, depth, NULL, diag) != 0) {
                    return -1;
                }
                return 0;
            }
            if (um->type == CC_TYPE_VOID && um->type_struct_id >= 0) {
                if (check_struct_initializer(tu, name, um->type_struct_id, init, vars, var_count, depth, diag) != 0) {
                    return -1;
                }
                return 0;
            }
        }
    }
    for (i = 0; i < init->arg_count; ++i) {
        cc_expr_t *raw = init->args[i];
        cc_expr_t *item = raw;
        const cc_struct_member_t *m;
        size_t member_idx = next_member;
        int raw_is_designator =
            (raw != NULL && raw->kind == CC_EXPR_MEMBER && raw->lhs == NULL && raw->rhs != NULL && raw->ident != NULL);

        if (sd->is_union && i > 0 && !raw_is_designator) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "too many initializers for union %s",
                         name != NULL ? name : "<anon>");
            }
            return -1;
        }

        if (raw_is_designator) {
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
                if (struct_has_anonymous_aggregate_member(tu, struct_id)) {
                    continue;
                }
                if (name != NULL && strcmp(name, "<compound-literal>") == 0) {
                    continue;
                }
                if (raw->rhs != NULL) {
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
            if (vars == NULL) {
                continue;
            }
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "flexible array member cannot be initialized for %s",
                         name != NULL ? name : "<anon>");
            }
            return -1;
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
                !(is_pointer_type(elem_type) && cc_type_is_integral(item->value_type) && is_null_ptr_constant(item))) {
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
            !(is_pointer_type(m->type) && cc_type_is_integral(item->value_type) && is_null_ptr_constant(item))) {
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
    if (array_ndim == 1 && init->arg_count == 1) {
        long string_len = array_len;
        long inferred_len = -1;
        int rc = check_array_string_initializer(name, array_type, &string_len, init->args[0], &inferred_len, diag);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            if (out_inferred_len != NULL && array_len == 0 && inferred_len > 0) {
                *out_inferred_len = inferred_len;
            }
            return 0;
        }
    }
    if (array_len > 0) {
        long init_elems = (long)init->arg_count;
        if (elem_type == CC_TYPE_VOID && array_struct_id >= 0) {
            init_elems = infer_struct_array_len_from_init(tu, array_struct_id, init);
            if (init_elems <= 0) {
                init_elems = (long)init->arg_count;
            }
        }
        if (init_elems > array_len) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "too many initializers for array %s",
                         name != NULL ? name : "<anon>");
            }
            return -1;
        }
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
                !(is_pointer_type(elem_type) && cc_type_is_integral(item->value_type) && is_null_ptr_constant(item))) {
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
                      cc_type_t fn_ret_type, int fn_ret_struct_id, cc_attr_flags_t fn_attr_flags, int loop_depth,
                      int switch_depth,
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
            cc_attr_flags_t fn_only_attrs = CC_ATTR_ALWAYS_INLINE | CC_ATTR_NOINLINE | CC_ATTR_HOT | CC_ATTR_COLD |
                                            CC_ATTR_FORMAT | CC_ATTR_NONNULL | CC_ATTR_MALLOC_FN | CC_ATTR_ALIAS |
                                            CC_ATTR_FLATTEN | CC_ATTR_TARGET | CC_ATTR_CONSTRUCTOR | CC_ATTR_DESTRUCTOR;
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
            if (cc_lang_at_least(2017) && (s->storage & CC_STORAGE_REGISTER) != 0) {
                if (emit_warning(diag, s->line, s->col, "'register' storage-class specifier is obsolescent in C17", 1) !=
                    0) {
                    return -1;
                }
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
            for (size_t b = 0; b < CC_MAX_ARRAY_DIMS; ++b) {
                cc_expr_t *bound = s->array_bound_exprs[b];

                if (bound == NULL) {
                    continue;
                }
                if (check_expr(tu, bound, *vars, *var_count, depth, diag) != 0) {
                    return -1;
                }
                if (!cc_type_is_integral(bound->value_type)) {
                    set_diag(diag, "variable-length array bound must be an integer expression");
                    return -1;
                }
            }
            if (vars_push(vars, var_count, s->decl_name, s->type, s->type_struct_id, s->array_len, s->array_ndim,
                          s->array_dims, s->storage, depth) != 0) {
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
                            !(is_pointer_type(init_type) && cc_type_is_integral(scalar_init->value_type) &&
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
                    if (sema_complex_convert_for_assign(tu, &s->expr, s->type, s->type_struct_id) > 0) {
                        /* to or from a complex type: the conversion is now explicit */
                        return 0;
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
                        !(is_pointer_type(init_type) && cc_type_is_integral(s->expr->value_type) &&
                          is_null_ptr_constant(s->expr)) &&
                        !(is_pointer_type(init_type) && is_pointer_type(s->expr->value_type)) &&
                        !(cc_type_is_integral(init_type) && cc_type_is_integral(s->expr->value_type))) {
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
            if (asm_validate_constraint(s->asm_outputs[i2].constraint, 1, s->asm_output_count, cc_target_pointer_size(),
                                        diag) != 0) {
                return -1;
            }
        }
        for (i2 = 0; i2 < s->asm_input_count; ++i2) {
            int tied_out = -1;
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
            if (asm_validate_constraint(s->asm_inputs[i2].constraint, 0, s->asm_output_count, cc_target_pointer_size(),
                                        diag) != 0) {
                return -1;
            }
            tied_out = cc_asm_constraint_matched_operand(s->asm_inputs[i2].constraint);
            (void)tied_out;
        }
        if (asm_check_early_clobber_inputs(s, diag) != 0) {
            return -1;
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
                    if (!cc_lang_gnu()) {
                        set_diag(diag, "void function returning a value");
                        return -1;
                    }
                    if (emit_required_warning(diag, s->line, s->col,
                                              "void function returning a value is a GNU extension; value is ignored") != 0)
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
            if (sema_complex_convert_for_assign(tu, &s->expr, fn_ret_type, fn_ret_struct_id) < 0) {
                set_diag(diag, "out of memory");
                return -1;
            }
            if (fn_ret_type == CC_TYPE_VOID && fn_ret_struct_id >= 0) {
                if (!(s->expr->value_type == CC_TYPE_VOID && s->expr->struct_id == fn_ret_struct_id)) {
                    if (!(s->expr->kind == CC_EXPR_STMT && cc_type_is_integral(s->expr->value_type))) {
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
        if (sema_condition(tu, &s->expr, diag) != 0) {
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
        if (sema_condition(tu, &s->expr, diag) != 0) {
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
        if (sema_condition(tu, &s->expr, diag) != 0) {
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
                if (sema_condition(tu, &s->expr, diag) != 0) {
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
        if (!cc_type_is_integral(s->expr->value_type)) {
            set_diag(diag, "switch expression must be integer");
            return -1;
        }
        {
            size_t i1;
            int seen_default = 0;
            for (i1 = 0; i1 < s->then_branch->block_count; ++i1) {
                const cc_stmt_t *ci = &s->then_branch->block_stmts[i1];
                long long vi = 0;
                long long vhi = 0;
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
                        long long vj = 0;
                        long long vjhi = 0;
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
            if (!is_pointer_type(s->expr->value_type) && !cc_type_is_integral(s->expr->value_type)) {
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
        if ((g->attr_flags & (CC_ATTR_CONSTRUCTOR | CC_ATTR_DESTRUCTOR)) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "constructor/destructor attributes are only valid on functions: %s", g->name);
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
                cc_type_t celem = sema_complex_elem(tu, CC_TYPE_VOID, g->type_struct_id);

                if (is_pointer_type(g->type) && g->array_len >= 0 && g->array_ndim == 1 && celem != CC_TYPE_VOID) {
                    /* an array of complex: each element not already braced is a constant to fold */
                    size_t k;

                    for (k = 0; k < g->init->arg_count; ++k) {
                        if (g->init->args[k] != NULL && g->init->args[k]->kind != CC_EXPR_INIT_LIST &&
                            (check_expr(tu, g->init->args[k], NULL, 0, 0, diag) != 0 ||
                             sema_complex_static_init(tu, &g->init->args[k], celem, diag) != 0)) {
                            goto fail_global_item;
                        }
                    }
                }
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
                        !(is_pointer_type(g->type) && cc_type_is_integral(scalar_init->value_type) &&
                          is_null_ptr_constant(scalar_init))) {
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message), "cannot initialize file-scope object %s",
                                     g->name);
                        }
                        goto fail_global_item;
                    }
                }
            } else {
                cc_type_t celem = sema_complex_elem(tu, g->type, g->type_struct_id);

                if (check_expr(tu, g->init, NULL, 0, 0, diag) != 0) {
                    goto fail_global_item;
                }
                if (celem != CC_TYPE_VOID) {
                    /* a static complex object: its constant parts, as a brace list */
                    if (sema_complex_static_init(tu, &g->init, celem, diag) != 0 ||
                        check_struct_initializer(tu, g->name, g->type_struct_id, g->init, NULL, 0, 0, diag) != 0) {
                        goto fail_global_item;
                    }
                    continue;
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
                    !(is_pointer_type(g->type) && cc_type_is_integral(g->init->value_type) &&
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
        if (cc_lang_at_least(2017) && !cc_lang_at_least(2023) && f->has_body && f->has_oldstyle_param_decls) {
            char msg[256];
            snprintf(msg, sizeof(msg), "old-style function definition is obsolescent in C17: %s", f->name);
            if (emit_warning(diag, f->line, f->col, msg, 1) != 0) {
                goto fail_decl;
            }
        }
        if ((f->attr_flags & (CC_ATTR_CONSTRUCTOR | CC_ATTR_DESTRUCTOR)) != 0) {
            if (!(f->ret_type == CC_TYPE_VOID && f->ret_struct_id < 0)) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "constructor/destructor function '%s' must have void return type", f->name);
                }
                goto fail_decl;
            }
            if (!f->has_prototype || f->param_count != 0 || f->is_variadic) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "constructor/destructor function '%s' must have prototype void(void)", f->name);
                }
                goto fail_decl;
            }
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
        if ((f->attr_flags & CC_ATTR_FORMAT) != 0) {
            long fmt_index = f->attr_format_index;
            long first_to_check = f->attr_format_first_to_check;
            if (!f->has_prototype) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "format function '%s' must have a prototype", f->name);
                }
                goto fail_decl;
            }
            if (f->param_count == 0) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "format function '%s' requires at least one named parameter", f->name);
                }
                goto fail_decl;
            }
            if (fmt_index > 0 && fmt_index > (long)f->param_count) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "format function '%s' has format index %ld out of range", f->name, fmt_index);
                }
                goto fail_decl;
            }
            if (!f->is_variadic) {
                /*
                 * GCC-compatible allowance for v* wrappers:
                 *   __attribute__((format(..., fmt_index, 0)))
                 * A non-zero third argument requires variadic parameters.
                 */
                if (first_to_check != 0) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "format function '%s' must be variadic or a v* wrapper", f->name);
                    }
                    goto fail_decl;
                }
            } else if (first_to_check > 0 && first_to_check < fmt_index + 1) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "format function '%s' has invalid first-to-check index", f->name);
                }
                goto fail_decl;
            }
        }
        if ((f->attr_flags & CC_ATTR_NONNULL) != 0 && (!f->has_prototype || f->param_count == 0)) {
            /* as gcc: with no pointer parameter to apply to, nonnull is harmless */
            char wbuf[256];
            snprintf(wbuf, sizeof(wbuf), "nonnull attribute on '%s' has no pointer parameter to apply to", f->name);
            if (emit_warning(diag, f->line, f->col, wbuf, 0) != 0) {
                goto fail_decl;
            }
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
            int prev_static;
            int cur_static;
            int prev_extern;
            int cur_extern;
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
            prev_static = ((prev->storage & CC_STORAGE_STATIC) != 0);
            cur_static = ((f->storage & CC_STORAGE_STATIC) != 0);
            prev_extern = ((prev->storage & CC_STORAGE_EXTERN) != 0);
            cur_extern = ((f->storage & CC_STORAGE_EXTERN) != 0);
            if ((prev_static && cur_extern) || (cur_static && prev_extern)) {
                int prev_inline = ((prev->storage & CC_STORAGE_INLINE) != 0);
                int cur_inline = ((f->storage & CC_STORAGE_INLINE) != 0);
                if (prev_inline || cur_inline) {
                    continue;
                }
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message),
                             "conflicting storage-class for function declaration: %s", f->name);
                }
                goto fail_decl;
            }
            /*
             * C inline/linkage rule interaction:
             * a prior extern declaration keeps a later plain inline definition
             * externally visible in this TU.
             */
            if ((prev_extern || cur_extern) && !prev_static && !cur_static) {
                prev->storage |= CC_STORAGE_EXTERN;
                f->storage |= CC_STORAGE_EXTERN;
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
        cc_attr_flags_t fn_attr_flags = f->attr_flags;

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

        if (f->nest_parent != NULL) {
            /* a GNU nested function: the enclosing function's variables, in a scope outside its own */
            cc_outer_var_t *outer;
            size_t outer_count;

            if (cc_nested_outer_vars(tu, f, &outer, &outer_count) != 0) {
                set_diag(diag, "nested function's enclosing function is missing");
                goto fail_func;
            }
            for (k = 0; k < outer_count; ++k) {
                if (vars_push(&vars, &var_count, outer[k].name, outer[k].type, outer[k].struct_id, outer[k].array_len,
                              outer[k].array_ndim, outer[k].array_dims, outer[k].storage, -1) != 0) {
                    free(outer);
                    set_diag(diag, "out of memory adding the enclosing function's variables");
                    goto fail_func;
                }
            }
            free(outer);
        }

        for (j = 0; j < f->param_count; ++j) {
            if (f->params[j].type == CC_TYPE_VOID && f->params[j].type_struct_id < 0) {
                set_diag(diag, "void is not valid for named parameter type");
                goto fail_func;
            }
            if (cc_lang_at_least(2017) && (f->params[j].storage & CC_STORAGE_REGISTER) != 0) {
                if (emit_warning(diag, f->line, f->col,
                                 "'register' storage-class specifier is obsolescent in C17", 1) != 0) {
                    goto fail_func;
                }
            }
            if (vars_find_depth(vars, var_count, f->params[j].name, 0) >= 0) {
                set_diag(diag, "duplicate parameter name");
                goto fail_func;
            }
            if (vars_push(&vars, &var_count, f->params[j].name, f->params[j].type, f->params[j].type_struct_id,
                          f->params[j].array_len, f->params[j].array_ndim, f->params[j].array_dims,
                          f->params[j].storage, 0) != 0) {
                set_diag(diag, "out of memory adding parameter");
                goto fail_func;
            }
        }
        /* a parameter that points to a variable-length array: its bounds name earlier parameters */
        for (j = 0; j < f->param_count; ++j) {
            for (size_t b = 0; b < CC_MAX_ARRAY_DIMS; ++b) {
                cc_expr_t *bound = f->params[j].vla_bounds[b];

                if (bound == NULL) {
                    continue;
                }
                if (check_expr(tu, bound, vars, var_count, 0, diag) != 0) {
                    goto fail_func;
                }
                if (!cc_type_is_integral(bound->value_type)) {
                    set_diag(diag, "variable-length array bound must be an integer expression");
                    goto fail_func;
                }
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

