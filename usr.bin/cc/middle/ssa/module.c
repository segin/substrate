#include "cc_ssa.h"
#include "cc_util.h"

#include <stdlib.h>
#include <string.h>

void cc_ssa_module_init(cc_ssa_module_t *m) {
    memset(m, 0, sizeof(*m));
}

void cc_ssa_global_free(cc_ssa_global_t *g) {
    size_t j;

    if (g == NULL) {
        return;
    }
    free(g->name);
    free(g->attr_section);
    free(g->attr_alias);
    free(g->init_str);
    free(g->init_sym);
    for (j = 0; j < g->init_item_count; ++j) {
        free(g->init_items[j].init_str);
        free(g->init_items[j].init_sym);
    }
    free(g->init_items);
    memset(g, 0, sizeof(*g));
}

void cc_ssa_module_free(cc_ssa_module_t *m) {
    size_t i;

    if (m == NULL) {
        return;
    }

    for (i = 0; i < m->global_count; ++i) {
        cc_ssa_global_free(&m->globals[i]);
    }
    free(m->globals);

    for (i = 0; i < m->func_count; ++i) {
        cc_ssa_function_free(&m->funcs[i]);
    }
    free(m->funcs);
    for (i = 0; i < m->toplevel_asm_count; ++i) {
        free(m->toplevel_asm[i]);
    }
    free(m->toplevel_asm);
    for (i = 0; i < m->local_decl_count; ++i) {
        free(m->local_decls[i]);
    }
    free(m->local_decls);
    for (i = 0; i < m->weak_decl_count; ++i) {
        free(m->weak_decls[i]);
    }
    free(m->weak_decls);
    for (i = 0; i < m->asm_label_count; ++i) {
        free(m->asm_label_names[i]);
        free(m->asm_labels[i]);
    }
    free(m->asm_label_names);
    free(m->asm_labels);
    for (i = 0; i < m->dbg_var_count; ++i) {
        free(m->dbg_vars[i].sym);
    }
    free(m->dbg_vars);
    memset(m, 0, sizeof(*m));
}

/**
 * Record a variable a function declares, for debug information.
 *
 * @param m  the module
 * @param v  the variable; its sym, if any, now belongs to @p m
 * @return   one more than its index (what cc_ssa_instr_t.dbg_var holds), or 0 when out of memory (sym freed)
 */
int cc_ssa_module_add_dbg_var(cc_ssa_module_t *m, const cc_ssa_dbg_var_t *v) {
    if (m->dbg_var_count == m->dbg_var_cap) {
        size_t ncap = m->dbg_var_cap == 0 ? 64 : m->dbg_var_cap * 2;
        cc_ssa_dbg_var_t *next = (cc_ssa_dbg_var_t *)realloc(m->dbg_vars, ncap * sizeof(*next));

        if (next == NULL) {
            free(v->sym);
            return 0;
        }
        m->dbg_vars = next;
        m->dbg_var_cap = ncap;
    }
    m->dbg_vars[m->dbg_var_count++] = *v;
    return (int)m->dbg_var_count;
}

/** The label @p m gives the name of @p n characters at @p name, or NULL when it gives it none. */
static const char *asm_label_for(const cc_ssa_module_t *m, const char *name, size_t n) {
    size_t i;

    for (i = 0; i < m->asm_label_count; ++i) {
        if (strlen(m->asm_label_names[i]) == n && strncmp(m->asm_label_names[i], name, n) == 0) {
            return m->asm_labels[i];
        }
    }
    return NULL;
}

int cc_ssa_module_add_asm_label(cc_ssa_module_t *m, const char *name, const char *label) {
    char **names;
    char **labels;
    char *name_dup;
    char *label_dup;

    if (m == NULL || name == NULL || label == NULL || asm_label_for(m, name, strlen(name)) != NULL) {
        return 0;
    }
    names = (char **)realloc(m->asm_label_names, (m->asm_label_count + 1) * sizeof(*names));
    if (names == NULL) {
        return -1;
    }
    m->asm_label_names = names;
    labels = (char **)realloc(m->asm_labels, (m->asm_label_count + 1) * sizeof(*labels));
    if (labels == NULL) {
        return -1;
    }
    m->asm_labels = labels;
    name_dup = cc_strdup(name);
    label_dup = cc_strdup(label);
    if (name_dup == NULL || label_dup == NULL) {
        free(name_dup);
        free(label_dup);
        return -1;
    }
    m->asm_label_names[m->asm_label_count] = name_dup;
    m->asm_labels[m->asm_label_count] = label_dup;
    m->asm_label_count++;
    return 0;
}

/** Whether @p c can be in an assembler symbol's name. */
static int asm_sym_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
           c == '$';
}

/**
 * Copy symbol expression @p s with each name in it that has an asm label replaced by its label.
 *
 * @param m    the module, with its labels
 * @param s    the expression: a name, perhaps with an offset or less another name
 * @param out  where to write the new expression and its terminator; NULL only to measure it
 * @param changed  set when a name has a label (may be NULL)
 * @return     the new expression's length
 */
static size_t asm_label_copy(const cc_ssa_module_t *m, const char *s, char *out, int *changed) {
    size_t len = 0;
    size_t i = 0;

    while (s[i] != '\0') {
        size_t n = 0;
        const char *label = NULL;
        const char *from;

        while (asm_sym_char(s[i + n])) {
            ++n;
        }
        if (n == 0) {
            n = 1; /* an operator */
        } else if (!(s[i] >= '0' && s[i] <= '9')) {
            /* (a number is no name) */
            label = asm_label_for(m, s + i, n);
        }
        from = label != NULL ? label : s + i;
        if (label != NULL && changed != NULL) {
            *changed = 1;
        }
        if (out != NULL) {
            memcpy(out + len, from, label != NULL ? strlen(label) : n);
        }
        len += label != NULL ? strlen(label) : n;
        i += n;
    }
    if (out != NULL) {
        out[len] = '\0';
    }
    return len;
}

/**
 * Give the names in symbol expression *@p sym that have asm labels their labels.
 *
 * @param m    the module, with its labels
 * @param sym  the expression, replaced by a new one when a name in it changes; NULL is left alone
 * @return     0, or -1 when out of memory (*@p sym unchanged)
 */
static int asm_label_rewrite(const cc_ssa_module_t *m, char **sym) {
    int changed = 0;
    size_t len;
    char *out;

    if (sym == NULL || *sym == NULL) {
        return 0;
    }
    len = asm_label_copy(m, *sym, NULL, &changed);
    if (!changed) {
        return 0;
    }
    out = (char *)malloc(len + 1);
    if (out == NULL) {
        return -1;
    }
    asm_label_copy(m, *sym, out, NULL);
    free(*sym);
    *sym = out;
    return 0;
}

int cc_ssa_module_apply_asm_labels(cc_ssa_module_t *m) {
    size_t i;

    if (m == NULL || m->asm_label_count == 0) {
        return 0;
    }
    for (i = 0; i < m->func_count; ++i) {
        cc_ssa_function_t *f = &m->funcs[i];
        size_t k;

        if (asm_label_rewrite(m, &f->name) != 0) {
            return -1;
        }
        for (k = 0; k < f->instr_count; ++k) {
            cc_ssa_instr_t *in = &f->instrs[k];

            /* (only these name symbols: an ASM's is its template, a STR's its text, a REGREAD's a register) */
            if ((in->op == CC_SSA_CALL || in->op == CC_SSA_GADDR) && asm_label_rewrite(m, &in->sym) != 0) {
                return -1;
            }
        }
        for (k = 0; k < f->keep_sym_count; ++k) {
            if (asm_label_rewrite(m, &f->keep_syms[k]) != 0) {
                return -1;
            }
        }
    }
    for (i = 0; i < m->global_count; ++i) {
        cc_ssa_global_t *g = &m->globals[i];
        size_t k;

        if (asm_label_rewrite(m, &g->name) != 0 || asm_label_rewrite(m, &g->init_sym) != 0) {
            return -1;
        }
        for (k = 0; k < g->init_item_count; ++k) {
            if (asm_label_rewrite(m, &g->init_items[k].init_sym) != 0) {
                return -1;
            }
        }
    }
    for (i = 0; i < m->local_decl_count; ++i) {
        if (asm_label_rewrite(m, &m->local_decls[i]) != 0) {
            return -1;
        }
    }
    for (i = 0; i < m->weak_decl_count; ++i) {
        if (asm_label_rewrite(m, &m->weak_decls[i]) != 0) {
            return -1;
        }
    }
    return 0;
}
