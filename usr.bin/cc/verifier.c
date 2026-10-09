#include "ir.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    size_t block_idx;
    size_t instr_idx;
} def_loc_t;

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

static void set_errf(ir_error_t *err, size_t line, const char *fmt, ...) {
    va_list ap;
    char buf[512];

    if (err == NULL || err->msg != NULL) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    err->line = line;
    err->msg = xstrdup(buf);
}

static int is_arg(const ir_func_t *f, const char *name) {
    size_t i;
    for (i = 0; i < f->arg_count; ++i) {
        if (strcmp(f->args[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int find_def(const def_loc_t *defs, size_t def_count, const char *name, def_loc_t *out) {
    size_t i;
    for (i = 0; i < def_count; ++i) {
        if (strcmp(defs[i].name, name) == 0) {
            if (out != NULL) {
                *out = defs[i];
            }
            return 1;
        }
    }
    return 0;
}

static int find_block_idx(const ir_func_t *f, const char *name, size_t *idx) {
    size_t i;
    for (i = 0; i < f->block_count; ++i) {
        if (strcmp(f->blocks[i].name, name) == 0) {
            *idx = i;
            return 1;
        }
    }
    return 0;
}

static int compute_reachable(const ir_func_t *f, unsigned char *reach) {
    size_t changed = 1;

    if (f->block_count == 0) {
        return 0;
    }

    reach[0] = 1;
    while (changed) {
        size_t i;
        changed = 0;
        for (i = 0; i < f->block_count; ++i) {
            size_t j;
            const ir_block_t *b = &f->blocks[i];
            if (!reach[i]) {
                continue;
            }
            for (j = 0; j < b->succ_count; ++j) {
                size_t sidx;
                if (!find_block_idx(f, b->succs[j], &sidx)) {
                    return -1;
                }
                if (!reach[sidx]) {
                    reach[sidx] = 1;
                    changed = 1;
                }
            }
        }
    }
    return 0;
}

static int compute_dominators(const ir_func_t *f, unsigned char *dom) {
    size_t n = f->block_count;
    size_t i;
    size_t j;
    int changed = 1;

    if (n == 0) {
        return 0;
    }

    for (i = 0; i < n; ++i) {
        for (j = 0; j < n; ++j) {
            dom[i * n + j] = (i == 0) ? (j == 0) : 1;
        }
    }

    while (changed) {
        changed = 0;
        for (i = 1; i < n; ++i) {
            unsigned char *cur = &dom[i * n];
            unsigned char *newset = (unsigned char *)malloc(n * sizeof(*newset));
            size_t p;
            if (newset == NULL) {
                return -1;
            }

            for (j = 0; j < n; ++j) {
                newset[j] = 1;
            }

            for (p = 0; p < f->blocks[i].pred_count; ++p) {
                size_t pred_idx;
                if (!find_block_idx(f, f->blocks[i].preds[p], &pred_idx)) {
                    free(newset);
                    return -1;
                }
                for (j = 0; j < n; ++j) {
                    newset[j] = (unsigned char)(newset[j] & dom[pred_idx * n + j]);
                }
            }
            newset[i] = 1;

            for (j = 0; j < n; ++j) {
                if (cur[j] != newset[j]) {
                    cur[j] = newset[j];
                    changed = 1;
                }
            }
            free(newset);
        }
    }

    return 0;
}

static int verify_function(const ir_func_t *f, ir_error_t *err) {
    size_t i;
    size_t j;
    def_loc_t *defs = NULL;
    size_t def_count = 0;
    size_t def_cap = 0;
    unsigned char *reach = NULL;
    unsigned char *dom = NULL;

    if (f->block_count == 0) {
        set_errf(err, 0, "function @%s has no blocks", f->name);
        return -1;
    }

    for (i = 0; i < f->block_count; ++i) {
        const ir_block_t *b = &f->blocks[i];

        if (b->instr_count == 0) {
            set_errf(err, b->line, "function @%s block %s is empty", f->name, b->name);
            return -1;
        }

        for (j = 0; j < b->instr_count; ++j) {
            const ir_instr_t *in = &b->instrs[j];
            if (in->is_phi && j > 0 && !b->instrs[j - 1].is_phi) {
                set_errf(err, in->line, "phi in block %s must be grouped at block start", b->name);
                return -1;
            }
            if (in->is_terminator && j + 1 != b->instr_count) {
                set_errf(err, in->line, "terminator in block %s must be last", b->name);
                return -1;
            }
        }

        if (!b->instrs[b->instr_count - 1].is_terminator) {
            set_errf(err, b->instrs[b->instr_count - 1].line,
                     "block %s missing terminator", b->name);
            return -1;
        }

        for (j = 0; j < b->succ_count; ++j) {
            size_t idx;
            if (!find_block_idx(f, b->succs[j], &idx)) {
                set_errf(err, b->line, "block %s has unknown successor %s", b->name, b->succs[j]);
                return -1;
            }
        }

        for (j = 0; j < b->instr_count; ++j) {
            const ir_instr_t *in = &b->instrs[j];
            if (in->is_phi && in->phi_incoming_count != (int)b->pred_count) {
                set_errf(err, in->line,
                         "phi in block %s has %d incomings, expected %zu",
                         b->name, in->phi_incoming_count, b->pred_count);
                return -1;
            }

            if (in->def != NULL) {
                if (find_def(defs, def_count, in->def, NULL) || is_arg(f, in->def)) {
                    set_errf(err, in->line, "duplicate SSA definition %%%s", in->def);
                    free(defs);
                    return -1;
                }
                if (def_count == def_cap) {
                    size_t ncap = def_cap == 0 ? 64 : def_cap * 2;
                    def_loc_t *next = (def_loc_t *)realloc(defs, ncap * sizeof(*next));
                    if (next == NULL) {
                        set_errf(err, in->line, "out of memory in verifier");
                        free(defs);
                        return -1;
                    }
                    defs = next;
                    def_cap = ncap;
                }
                defs[def_count].name = in->def;
                defs[def_count].block_idx = i;
                defs[def_count].instr_idx = j;
                def_count++;
            }
        }
    }

    reach = (unsigned char *)calloc(f->block_count, sizeof(*reach));
    dom = (unsigned char *)calloc(f->block_count * f->block_count, sizeof(*dom));
    if (reach == NULL || dom == NULL) {
        set_errf(err, 0, "out of memory in verifier");
        free(reach);
        free(dom);
        free(defs);
        return -1;
    }

    if (compute_reachable(f, reach) != 0) {
        set_errf(err, 0, "CFG references an unknown block");
        free(reach);
        free(dom);
        free(defs);
        return -1;
    }

    for (i = 0; i < f->block_count; ++i) {
        if (!reach[i]) {
            set_errf(err, f->blocks[i].line, "block %s is unreachable from entry", f->blocks[i].name);
            free(reach);
            free(dom);
            free(defs);
            return -1;
        }
    }

    if (compute_dominators(f, dom) != 0) {
        set_errf(err, 0, "failed to compute dominators");
        free(reach);
        free(dom);
        free(defs);
        return -1;
    }

    for (i = 0; i < f->block_count; ++i) {
        const ir_block_t *b = &f->blocks[i];
        for (j = 0; j < b->instr_count; ++j) {
            size_t u;
            const ir_instr_t *in = &b->instrs[j];

            for (u = 0; u < in->use_count; ++u) {
                def_loc_t d;

                if (is_arg(f, in->uses[u].name)) {
                    continue;
                }
                if (!find_def(defs, def_count, in->uses[u].name, &d)) {
                    set_errf(err, in->line, "use of undefined value %%%s", in->uses[u].name);
                    free(reach);
                    free(dom);
                    free(defs);
                    return -1;
                }

                if (!in->is_phi) {
                    if (!dom[i * f->block_count + d.block_idx]) {
                        set_errf(err, in->line,
                                 "value %%%s does not dominate its use in block %s",
                                 in->uses[u].name, b->name);
                        free(reach);
                        free(dom);
                        free(defs);
                        return -1;
                    }
                    if (d.block_idx == i && d.instr_idx >= j) {
                        set_errf(err, in->line,
                                 "use-before-def of %%%s in block %s",
                                 in->uses[u].name, b->name);
                        free(reach);
                        free(dom);
                        free(defs);
                        return -1;
                    }
                }
            }
        }
    }

    free(reach);
    free(dom);
    free(defs);
    return 0;
}

int ir_verify_module(const ir_module_t *m, ir_error_t *err) {
    size_t i;

    if (m->name == NULL || m->name[0] == '\0') {
        set_errf(err, 0, "module name missing");
        return -1;
    }
    if (m->target == NULL || m->target[0] == '\0') {
        set_errf(err, 0, "target missing");
        return -1;
    }

    for (i = 0; i < m->func_count; ++i) {
        if (verify_function(&m->funcs[i], err) != 0) {
            return -1;
        }
    }

    return 0;
}
