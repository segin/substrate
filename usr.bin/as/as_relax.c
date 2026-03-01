#include "as_relax.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct {
    char *name;
    size_t stmt_index;
    unsigned line;
} label_loc_t;

typedef struct {
    label_loc_t *items;
    size_t count;
    size_t cap;
} label_vec_t;

typedef struct {
    const as_parse_result_t *parsed;
    const as_relax_cfg_t *cfg;
    as_relax_result_t *out;
    char *errbuf;
    size_t errbuf_sz;
    label_vec_t labels;
} relax_ctx_t;

static void set_err(relax_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

void as_relax_result_init(as_relax_result_t *r) {
    if (r == NULL) {
        return;
    }
    r->branches = NULL;
    r->branch_count = 0;
    r->passes = 0;
    r->stabilized = 0;
}

void as_relax_result_free(as_relax_result_t *r) {
    size_t i;

    if (r == NULL) {
        return;
    }
    for (i = 0; i < r->branch_count; ++i) {
        free(r->branches[i].mnemonic);
        free(r->branches[i].target_name);
    }
    free(r->branches);
    memset(r, 0, sizeof(*r));
}

static int push_label(label_vec_t *v, const char *name, size_t stmt_index, unsigned line) {
    label_loc_t *next;

    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 32 : v->cap * 2;
        next = (label_loc_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }

    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return -1;
    }
    v->items[v->count].stmt_index = stmt_index;
    v->items[v->count].line = line;
    v->count++;
    return 0;
}

static void free_labels(label_vec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int is_x86_branch(const char *mnemonic) {
    if (mnemonic == NULL || mnemonic[0] == '\0') {
        return 0;
    }
    if (strcmp(mnemonic, "jmp") == 0) {
        return 1;
    }
    return mnemonic[0] == 'j' && mnemonic[1] != '\0';
}

static int is_arm_branch(const char *mnemonic) {
    if (mnemonic == NULL) {
        return 0;
    }
    return strcmp(mnemonic, "b") == 0 || strcmp(mnemonic, "bl") == 0;
}

static size_t find_label_stmt_by_name(const label_vec_t *v, const char *name) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i].name, name) == 0) {
            return v->items[i].stmt_index;
        }
    }
    return (size_t)-1;
}

static size_t find_stmt_by_line(const as_parse_result_t *p, unsigned line) {
    size_t i;

    for (i = 0; i < p->count; ++i) {
        if (p->items[i].line == line) {
            return i;
        }
    }
    return (size_t)-1;
}

static int add_branch(relax_ctx_t *ctx, size_t stmt_index, const as_instruction_t *in) {
    as_relax_branch_t *next;
    as_relax_branch_t *b;

    next = (as_relax_branch_t *)realloc(ctx->out->branches, (ctx->out->branch_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    ctx->out->branches = next;
    b = &ctx->out->branches[ctx->out->branch_count++];
    memset(b, 0, sizeof(*b));
    b->stmt_index = stmt_index;
    b->mnemonic = xstrdup(in->mnemonic);
    if (b->mnemonic == NULL) {
        return -1;
    }

    if (ctx->cfg->arch == AS_PARSER_ARCH_X86) {
        b->kind = AS_BRANCH_KIND_SHORT;
    } else {
        b->kind = AS_BRANCH_KIND_NEAR;
    }

    if (in->operand_count > 0 && in->operands[0].kind == AS_OPERAND_LABEL_REF && in->operands[0].u.expr != NULL) {
        const as_expr_t *e = in->operands[0].u.expr;
        if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL) {
            b->target_name = xstrdup(e->symbol);
            if (b->target_name == NULL) {
                return -1;
            }
        } else if (e->kind == AS_EXPR_LOCAL_REF && e->local_resolved) {
            b->target_line = e->local_target_line;
        }
    }

    return 0;
}

static int collect_inputs(relax_ctx_t *ctx) {
    size_t i;

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        size_t j;

        for (j = 0; j < st->label_count; ++j) {
            if (push_label(&ctx->labels, st->labels[j].name, i, st->labels[j].line) != 0) {
                return -1;
            }
        }

        if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }

        if ((ctx->cfg->arch == AS_PARSER_ARCH_X86 && is_x86_branch(st->u.instr.mnemonic)) ||
            (ctx->cfg->arch == AS_PARSER_ARCH_ARM && is_arm_branch(st->u.instr.mnemonic))) {
            if (add_branch(ctx, i, &st->u.instr) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static size_t branch_size(const as_relax_branch_t *b) {
    switch (b->kind) {
    case AS_BRANCH_KIND_SHORT:
        return 2;
    case AS_BRANCH_KIND_NEAR:
        return 5;
    case AS_BRANCH_KIND_FAR:
        return 6;
    default:
        return 4;
    }
}

static ssize_t branch_index_for_stmt(const as_relax_result_t *r, size_t stmt_idx) {
    size_t i;

    for (i = 0; i < r->branch_count; ++i) {
        if (r->branches[i].stmt_index == stmt_idx) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static size_t stmt_size(const as_parse_result_t *p, const as_relax_result_t *r, size_t stmt_idx) {
    const as_stmt_t *st = &p->items[stmt_idx];
    ssize_t bi = branch_index_for_stmt(r, stmt_idx);

    if (bi >= 0) {
        return branch_size(&r->branches[bi]);
    }
    if (st->kind == AS_STMT_INSTRUCTION) {
        return 4;
    }
    return 0;
}

static int apply_pass(relax_ctx_t *ctx, size_t *offsets, int *changed_out) {
    size_t i;
    long short_min;
    long short_max;
    long near_min;
    long near_max;
    long arm_abs;

    short_min = ctx->cfg->x86_short_min != 0 ? ctx->cfg->x86_short_min : -128;
    short_max = ctx->cfg->x86_short_max != 0 ? ctx->cfg->x86_short_max : 127;
    near_min = ctx->cfg->x86_near_min != 0 ? ctx->cfg->x86_near_min : (-2147483647L - 1L);
    near_max = ctx->cfg->x86_near_max != 0 ? ctx->cfg->x86_near_max : 2147483647L;
    arm_abs = ctx->cfg->arm_branch_abs_range != 0 ? ctx->cfg->arm_branch_abs_range : (32L * 1024L * 1024L);

    *changed_out = 0;

    for (i = 0; i < ctx->out->branch_count; ++i) {
        as_relax_branch_t *b = &ctx->out->branches[i];
        size_t target_stmt = (size_t)-1;
        long disp;
        size_t cur_off;
        size_t cur_size;

        b->out_of_range = 0;
        b->veneer_needed = 0;

        if (b->target_name != NULL) {
            target_stmt = find_label_stmt_by_name(&ctx->labels, b->target_name);
        } else if (b->target_line != 0) {
            target_stmt = find_stmt_by_line(ctx->parsed, b->target_line);
        }
        if (target_stmt == (size_t)-1) {
            continue;
        }

        cur_off = offsets[b->stmt_index];
        cur_size = stmt_size(ctx->parsed, ctx->out, b->stmt_index);
        disp = (long)offsets[target_stmt] - (long)(cur_off + cur_size);
        b->displacement = disp;

        if (ctx->cfg->arch == AS_PARSER_ARCH_X86) {
            if (b->kind == AS_BRANCH_KIND_SHORT && (disp < short_min || disp > short_max)) {
                b->kind = AS_BRANCH_KIND_NEAR;
                *changed_out = 1;
            } else if (b->kind == AS_BRANCH_KIND_NEAR && (disp < near_min || disp > near_max)) {
                b->kind = AS_BRANCH_KIND_FAR;
                *changed_out = 1;
            }
            if (b->kind == AS_BRANCH_KIND_FAR && (disp < near_min || disp > near_max)) {
                b->out_of_range = 1;
            }
        } else if (ctx->cfg->arch == AS_PARSER_ARCH_ARM) {
            if (disp < -arm_abs || disp > arm_abs) {
                b->out_of_range = 1;
                b->veneer_needed = 1;
            }
        }
    }

    return 0;
}

int as_relax_branches(const as_parse_result_t *parsed, const as_relax_cfg_t *cfg,
                      as_relax_result_t *out, char *errbuf, size_t errbuf_sz) {
    relax_ctx_t ctx;
    unsigned pass;
    unsigned max_passes;
    size_t *offsets = NULL;

    if (parsed == NULL || cfg == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.parsed = parsed;
    ctx.cfg = cfg;
    ctx.out = out;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (collect_inputs(&ctx) != 0) {
        set_err(&ctx, "failed to collect branch candidates");
        free_labels(&ctx.labels);
        return -1;
    }

    offsets = (size_t *)calloc(parsed->count + 1, sizeof(size_t));
    if (offsets == NULL) {
        set_err(&ctx, "out of memory");
        free_labels(&ctx.labels);
        return -1;
    }

    max_passes = cfg->max_passes != 0 ? cfg->max_passes : 16;
    for (pass = 1; pass <= max_passes; ++pass) {
        int changed = 0;
        size_t i;

        offsets[0] = 0;
        for (i = 0; i < parsed->count; ++i) {
            offsets[i + 1] = offsets[i] + stmt_size(parsed, out, i);
        }

        if (apply_pass(&ctx, offsets, &changed) != 0) {
            set_err(&ctx, "relaxation pass failed");
            free(offsets);
            free_labels(&ctx.labels);
            return -1;
        }

        out->passes = pass;
        if (!changed) {
            out->stabilized = 1;
            break;
        }
    }

    free(offsets);
    free_labels(&ctx.labels);
    return 0;
}
