/*
 * licm.c: loop-invariant code motion.
 *
 * An integer computation inside a loop whose operands are the same on
 * every trip -- defined only outside the loop, or computed by another
 * such instruction -- is made once, before the loop, in its preheader:
 * the one block outside the loop that enters it, which does nothing but
 * jump to its header.  A matrix multiply's `&a[i][0]`, which cc made
 * with an imul and an add on every trip of the innermost loop, is made
 * once per row, and with the inner loops' work hoisted again out of the
 * next loop, once per i.
 *
 * Only what cannot fail and reads no memory moves: constants, addresses
 * of globals, arithmetic but division, shifts and masks, on integers --
 * but an index's scaling by 2, 4 or 8, which an address makes for
 * nothing.  A copy of a variable the loop does not write stays, and what
 * is hoisted reads the variable instead: the copy made before the loop
 * would be a second register held across it.  An operation moved before the loop runs even where the
 * loop's body would not have, so a division, which may trap, a load,
 * which may fault or read what the loop writes, and floating arithmetic,
 * which may raise an exception gcc's -ftrapping-math keeps, stay where
 * they are.  The value defined must be defined once, and neither it nor
 * an operand may have its address taken, as a store through a pointer
 * may change one.
 *
 * And only what every trip makes moves -- what is in a block that
 * dominates each jump back to the header -- but for constants, which are
 * immediates where they are read: the work of one arm of a switch or an
 * if, made before the loop, holds a register across all of it for the
 * trips that never take that arm.  A compare stays too, as a branch reads
 * its flags where it is made; before the loop it would be a setcc and a
 * register held.  cc built by itself kept a dozen compares of a switch's
 * arms in registers around asm_immediate_fits()'s loop.
 *
 * Loops are the natural loops of the back edges (an edge to a block that
 * dominates its source), innermost first, so that what leaves an inner
 * loop may leave the one around it too.  A loop without a preheader, and
 * a function with a computed goto, whose labels may be entered from
 * anywhere, are left as they are.
 */

#include "cc_middle.h"
#include "cc_ssa.h"

#include <stdlib.h>
#include <string.h>

/** A function's blocks, the edges between them, their dominators and the instructions hoisted into each. */
typedef struct {
    const cc_ssa_function_t *f;
    cc_ssa_function_t *fw;  /**< the same, to rewrite the operands of what is hoisted */
    int nb;            /**< blocks */
    int *start;        /**< per block: its first instruction */
    int *end;          /**< per block: one past its last */
    int *blk;          /**< per instruction: the block it is in, as instructions are hoisted */
    int *succ_off;     /**< per block (and one more): where its successors start in succ */
    int *succ;
    int *pred_off;     /**< the same for predecessors */
    int *pred;
    int *rpo;          /**< per block: its reverse postorder number from the entry, -1 if unreachable */
    int *idom;         /**< per block: its immediate dominator, -1 for the entry and the unreachable */
    int **moved;       /**< per block: the instructions hoisted into it, in order, before its terminator */
    int *nmoved;
    int *capmoved;
} licm_cfg_t;

/** Whether control never passes from @p in to the instruction after it. */
static int ends_block(const cc_ssa_instr_t *in) {
    return in->op == CC_SSA_BR || in->op == CC_SSA_BR_COND || in->op == CC_SSA_RET || in->op == CC_SSA_TRAP;
}

/** Free what @p c holds. */
static void cfg_free(licm_cfg_t *c) {
    int b;

    for (b = 0; c->moved != NULL && b < c->nb; ++b) {
        free(c->moved[b]);
    }
    free(c->start);
    free(c->end);
    free(c->blk);
    free(c->succ_off);
    free(c->succ);
    free(c->pred_off);
    free(c->pred);
    free(c->rpo);
    free(c->idom);
    free(c->moved);
    free(c->nmoved);
    free(c->capmoved);
}

/**
 * Call @p fn for each successor of block @p b: the labels its terminator and any asm goto in it branch to, by
 * their blocks, and the next block where it falls through.
 */
static void each_succ(const licm_cfg_t *c, const int *label_block, int b, void (*fn)(void *ctx, int s), void *ctx) {
    const cc_ssa_function_t *f = c->f;
    const cc_ssa_instr_t *last = &f->instrs[c->end[b] - 1];
    int i;

#define TO(l) fn(ctx, (l) >= 0 && (l) < f->label_count ? label_block[(l)] : -1)
    for (i = c->start[b]; i < c->end[b]; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t k;

        for (k = 0; in->op == CC_SSA_ASM && k < in->asm_goto_count; ++k) {
            TO(in->asm_goto_labels[k]);
        }
    }
    if (last->op == CC_SSA_BR) {
        TO(last->label);
    } else if (last->op == CC_SSA_BR_COND) {
        TO(last->true_label);
        TO(last->false_label);
    } else if (!ends_block(last) && b + 1 < c->nb) {
        fn(ctx, b + 1);
    }
#undef TO
}

typedef struct {
    int *count;
    int *list;
    int *fill;
    int from;
} edge_ctx_t;

/** each_succ() callback: count an edge into its target. */
static void count_edge(void *ctx, int s) {
    edge_ctx_t *e = (edge_ctx_t *)ctx;

    if (s >= 0) {
        e->count[e->from]++;
    }
}

/** each_succ() callback: list an edge's target. */
static void list_edge(void *ctx, int s) {
    edge_ctx_t *e = (edge_ctx_t *)ctx;

    if (s >= 0) {
        e->list[e->fill[e->from]++] = s;
    }
}

/** Find @p f's blocks and the edges between them. @return 0, 1 to leave the function alone, -1 out of memory */
static int cfg_build(licm_cfg_t *c, const cc_ssa_function_t *f) {
    int n = (int)f->instr_count;
    int *label_block = NULL;
    int *fill = NULL;
    edge_ctx_t e;
    int nedges = 0;
    int i;
    int b;

    memset(c, 0, sizeof(*c));
    c->f = f;
    for (i = 0; i < n; ++i) {
        if (f->instrs[i].op == CC_SSA_LADDR) {
            return 1; /* a computed goto may enter any label whose address is taken */
        }
        if (i == 0 || f->instrs[i].op == CC_SSA_LABEL || ends_block(&f->instrs[i - 1])) {
            c->nb++;
        }
    }
    c->start = (int *)malloc((size_t)c->nb * sizeof(int));
    c->end = (int *)malloc((size_t)c->nb * sizeof(int));
    c->blk = (int *)malloc((size_t)n * sizeof(int));
    c->succ_off = (int *)calloc((size_t)c->nb + 1, sizeof(int));
    c->pred_off = (int *)calloc((size_t)c->nb + 1, sizeof(int));
    c->rpo = (int *)malloc((size_t)c->nb * sizeof(int));
    c->idom = (int *)malloc((size_t)c->nb * sizeof(int));
    c->moved = (int **)calloc((size_t)c->nb, sizeof(int *));
    c->nmoved = (int *)calloc((size_t)c->nb, sizeof(int));
    c->capmoved = (int *)calloc((size_t)c->nb, sizeof(int));
    label_block = (int *)malloc((size_t)(f->label_count > 0 ? f->label_count : 1) * sizeof(int));
    fill = (int *)calloc((size_t)c->nb + 1, sizeof(int));
    if (c->start == NULL || c->end == NULL || c->blk == NULL || c->succ_off == NULL || c->pred_off == NULL ||
        c->rpo == NULL || c->idom == NULL || c->moved == NULL || c->nmoved == NULL || c->capmoved == NULL ||
        label_block == NULL || fill == NULL) {
        free(label_block);
        free(fill);
        return -1;
    }
    for (i = 0; i < f->label_count; ++i) {
        label_block[i] = -1;
    }
    b = -1;
    for (i = 0; i < n; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (i == 0 || in->op == CC_SSA_LABEL || ends_block(&f->instrs[i - 1])) {
            if (b >= 0) {
                c->end[b] = i;
            }
            c->start[++b] = i;
        }
        c->blk[i] = b;
        if (in->op == CC_SSA_LABEL && in->label >= 0 && in->label < f->label_count) {
            label_block[in->label] = b;
        }
    }
    c->end[b] = n;

    /* successors, then predecessors, each a list per block */
    e.count = c->succ_off;
    for (b = 0; b < c->nb; ++b) {
        e.from = b;
        each_succ(c, label_block, b, count_edge, &e);
    }
    for (b = 0; b < c->nb; ++b) {
        nedges += c->succ_off[b];
    }
    c->succ = (int *)malloc((size_t)(nedges > 0 ? nedges : 1) * sizeof(int));
    c->pred = (int *)malloc((size_t)(nedges > 0 ? nedges : 1) * sizeof(int));
    if (c->succ == NULL || c->pred == NULL) {
        free(label_block);
        free(fill);
        return -1;
    }
    for (b = c->nb; b > 0; --b) {
        c->succ_off[b] = c->succ_off[b - 1];
    }
    c->succ_off[0] = 0;
    for (b = 0; b < c->nb; ++b) {
        c->succ_off[b + 1] += c->succ_off[b];
        fill[b] = c->succ_off[b];
    }
    e.list = c->succ;
    e.fill = fill;
    for (b = 0; b < c->nb; ++b) {
        e.from = b;
        each_succ(c, label_block, b, list_edge, &e);
    }
    for (i = 0; i < nedges; ++i) {
        c->pred_off[c->succ[i] + 1]++;
    }
    for (b = 0; b < c->nb; ++b) {
        c->pred_off[b + 1] += c->pred_off[b];
        fill[b] = c->pred_off[b];
    }
    for (b = 0; b < c->nb; ++b) {
        int k;

        for (k = c->succ_off[b]; k < c->succ_off[b + 1]; ++k) {
            c->pred[fill[c->succ[k]]++] = b;
        }
    }
    free(label_block);
    free(fill);
    return 0;
}

/** Number the blocks reachable from the entry in reverse postorder (rpo) and find their dominators (idom). */
static int cfg_dominators(licm_cfg_t *c) {
    int *order = (int *)malloc((size_t)c->nb * sizeof(int));     /* blocks by rpo number */
    int *stack = (int *)malloc((size_t)c->nb * sizeof(int));
    int *next = (int *)malloc((size_t)c->nb * sizeof(int));      /* per block on the stack: its next successor */
    unsigned char *seen = (unsigned char *)calloc((size_t)c->nb, 1);
    int top = 0;
    int post = c->nb;
    int changed;
    int b;
    int k;

    if (order == NULL || stack == NULL || next == NULL || seen == NULL) {
        free(order);
        free(stack);
        free(next);
        free(seen);
        return -1;
    }
    for (b = 0; b < c->nb; ++b) {
        c->rpo[b] = -1;
        c->idom[b] = -1;
    }
    stack[top++] = 0;
    seen[0] = 1;
    next[0] = c->succ_off[0];
    while (top > 0) {
        int t = stack[top - 1];

        if (next[t] < c->succ_off[t + 1]) {
            int s = c->succ[next[t]++];

            if (!seen[s]) {
                seen[s] = 1;
                next[s] = c->succ_off[s];
                stack[top++] = s;
            }
            continue;
        }
        c->rpo[t] = --post;
        top--;
    }
    /* reachable blocks hold the numbers post..nb-1: make them 0.. */
    for (b = 0; b < c->nb; ++b) {
        if (c->rpo[b] >= 0) {
            c->rpo[b] -= post;
            order[c->rpo[b]] = b;
        }
    }
    /* Cooper, Harvey and Kennedy's iteration */
    c->idom[0] = 0;
    do {
        changed = 0;
        for (k = 1; k < c->nb - post; ++k) {
            int bb = order[k];
            int nd = -1;
            int p;

            for (p = c->pred_off[bb]; p < c->pred_off[bb + 1]; ++p) {
                int q = c->pred[p];

                if (c->rpo[q] < 0 || c->idom[q] < 0) {
                    continue;
                }
                if (nd < 0) {
                    nd = q;
                    continue;
                }
                {
                    int x = q;
                    int y = nd;

                    while (x != y) {
                        while (c->rpo[x] > c->rpo[y]) {
                            x = c->idom[x];
                        }
                        while (c->rpo[y] > c->rpo[x]) {
                            y = c->idom[y];
                        }
                    }
                    nd = x;
                }
            }
            if (nd >= 0 && c->idom[bb] != nd) {
                c->idom[bb] = nd;
                changed = 1;
            }
        }
    } while (changed);
    c->idom[0] = -1;
    free(order);
    free(stack);
    free(next);
    free(seen);
    return 0;
}

/** Whether block @p a dominates block @p b, both reachable. */
static int dominates(const licm_cfg_t *c, int a, int b) {
    while (b >= 0) {
        if (b == a) {
            return 1;
        }
        b = c->idom[b];
    }
    return 0;
}

/** A natural loop: its header, its blocks (in[b]) and how many, and those every trip runs (always[b]). */
typedef struct {
    int header;
    int size;
    unsigned char *in;
    unsigned char *always;
} licm_loop_t;

/** Order loops by their size, smallest (innermost) first. */
static int loop_cmp(const void *a, const void *b) {
    const licm_loop_t *x = (const licm_loop_t *)a;
    const licm_loop_t *y = (const licm_loop_t *)b;

    return x->size != y->size ? (x->size < y->size ? -1 : 1) : (x->header < y->header ? -1 : x->header > y->header);
}

/** Find @p c's natural loops, one per header (its back edges' loops merged), into *@p out; their count, or -1. */
static int find_loops(const licm_cfg_t *c, licm_loop_t **out) {
    licm_loop_t *loops = NULL;
    int nloops = 0;
    int *work = (int *)malloc((size_t)c->nb * sizeof(int));
    int b;

    *out = NULL;
    if (work == NULL) {
        return -1;
    }
    for (b = 0; b < c->nb; ++b) {
        int k;

        if (c->rpo[b] < 0) {
            continue;
        }
        for (k = c->succ_off[b]; k < c->succ_off[b + 1]; ++k) {
            int h = c->succ[k];
            licm_loop_t *l = NULL;
            int top = 0;
            int j;

            if (!dominates(c, h, b)) {
                continue;
            }
            for (j = 0; j < nloops; ++j) {
                if (loops[j].header == h) {
                    l = &loops[j];
                }
            }
            if (l == NULL) {
                licm_loop_t *nl = (licm_loop_t *)realloc(loops, (size_t)(nloops + 1) * sizeof(*loops));

                if (nl == NULL) {
                    goto oom;
                }
                loops = nl;
                l = &loops[nloops++];
                l->header = h;
                l->size = 1;
                l->in = (unsigned char *)calloc((size_t)c->nb, 1);
                l->always = (unsigned char *)calloc((size_t)c->nb, 1);
                if (l->in == NULL || l->always == NULL) {
                    free(l->in);
                    free(l->always);
                    nloops--;
                    goto oom;
                }
                l->in[h] = 1;
            }
            /* the blocks that reach b without passing h */
            if (!l->in[b]) {
                l->in[b] = 1;
                l->size++;
                work[top++] = b;
            }
            while (top > 0) {
                int t = work[--top];
                int p;

                for (p = c->pred_off[t]; p < c->pred_off[t + 1]; ++p) {
                    int q = c->pred[p];

                    if (c->rpo[q] >= 0 && !l->in[q]) {
                        l->in[q] = 1;
                        l->size++;
                        work[top++] = q;
                    }
                }
            }
        }
    }
    free(work);
    /* a block every trip runs: one that dominates each block jumping back to the header */
    for (b = 0; b < nloops; ++b) {
        licm_loop_t *l = &loops[b];
        int t;

        for (t = 0; t < c->nb; ++t) {
            int p;

            l->always[t] = l->in[t];
            for (p = c->pred_off[l->header]; p < c->pred_off[l->header + 1] && l->always[t]; ++p) {
                int q = c->pred[p];

                if (l->in[q] && !dominates(c, t, q)) {
                    l->always[t] = 0;
                }
            }
        }
    }
    if (nloops > 1) {
        qsort(loops, (size_t)nloops, sizeof(*loops), loop_cmp);
    }
    *out = loops;
    return nloops;
oom:
    for (b = 0; b < nloops; ++b) {
        free(loops[b].in);
        free(loops[b].always);
    }
    free(loops);
    free(work);
    return -1;
}

/**
 * The preheader of loop @p l: its header's one predecessor outside it, which has no other successor and comes
 * before the header, so that what it defines is defined before the loop in the stream too; else -1.
 */
static int preheader(const licm_cfg_t *c, const licm_loop_t *l) {
    int h = l->header;
    int ph = -1;
    int p;

    for (p = c->pred_off[h]; p < c->pred_off[h + 1]; ++p) {
        int q = c->pred[p];

        if (l->in[q]) {
            continue;
        }
        if (ph >= 0 && ph != q) {
            return -1;
        }
        ph = q;
    }
    if (ph < 0 || c->succ_off[ph + 1] - c->succ_off[ph] != 1 || ph >= h) {
        return -1;
    }
    return ph;
}

/** What the pass knows of each value: its definitions, which instructions, and whether its address is taken. */
typedef struct {
    int *ndefs;
    int *def_off;      /**< per value (and one more): where its defining instructions start in defs */
    int *defs;
    unsigned char *addr_taken;
} licm_values_t;

/** Each instruction's values defined: its result, and an asm's outputs. */
static int each_def(const cc_ssa_instr_t *in, int k) {
    int v;

    if (k == 0) {
        return in->dst;
    }
    if (in->op != CC_SSA_ASM || (size_t)(k - 1) >= in->asm_out_count) {
        return -2;
    }
    v = in->asm_out_values[k - 1];
    return CC_SSA_ASM_MEM_INDIRECT_P(v) ? CC_SSA_ASM_MEM_INDIRECT_DECODE(v) : v;
}

/** Fill @p vs for @p f. @return 0, or -1 when out of memory */
static int values_build(licm_values_t *vs, const cc_ssa_function_t *f) {
    int nv = f->value_count > 0 ? f->value_count : 1;
    int *fill;
    size_t i;
    int v;
    int k;

    vs->ndefs = (int *)calloc((size_t)nv, sizeof(int));
    vs->def_off = (int *)calloc((size_t)nv + 1, sizeof(int));
    vs->addr_taken = (unsigned char *)calloc((size_t)nv, 1);
    fill = (int *)malloc((size_t)nv * sizeof(int));
    vs->defs = NULL;
    if (vs->ndefs == NULL || vs->def_off == NULL || vs->addr_taken == NULL || fill == NULL) {
        free(fill);
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        for (k = 0; (v = each_def(in, k)) != -2; ++k) {
            if (v >= 0 && v < f->value_count) {
                vs->ndefs[v]++;
            }
        }
        if (in->op == CC_SSA_ADDR && in->lhs >= 0 && in->lhs < f->value_count) {
            vs->addr_taken[in->lhs] = 1;
        }
    }
    for (v = 0; v < f->value_count; ++v) {
        vs->def_off[v + 1] = vs->def_off[v] + vs->ndefs[v];
        fill[v] = vs->def_off[v];
    }
    vs->defs = (int *)malloc((size_t)(vs->def_off[f->value_count] > 0 ? vs->def_off[f->value_count] : 1) * sizeof(int));
    if (vs->defs == NULL) {
        free(fill);
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        for (k = 0; (v = each_def(&f->instrs[i], k)) != -2; ++k) {
            if (v >= 0 && v < f->value_count) {
                vs->defs[fill[v]++] = (int)i;
            }
        }
    }
    free(fill);
    return 0;
}

/** Free what @p vs holds. */
static void values_free(licm_values_t *vs) {
    free(vs->ndefs);
    free(vs->def_off);
    free(vs->defs);
    free(vs->addr_taken);
}

/** Whether value @p v is a plain integer: not floating, not a structure, its address not taken. */
static int plain_int(const cc_ssa_function_t *f, const licm_values_t *vs, int v) {
    return v >= 0 && v < f->value_count && f->value_ty[v].kind == CC_TY_INT && !vs->addr_taken[v];
}

/** Whether no definition of value @p v is in loop @p l, so that it holds on every trip what it held before it. */
static int defined_outside(const licm_cfg_t *c, const licm_values_t *vs, const licm_loop_t *l, int v) {
    int k;

    if (!plain_int(c->f, vs, v)) {
        return 0;
    }
    for (k = vs->def_off[v]; k < vs->def_off[v + 1]; ++k) {
        if (l->in[c->blk[vs->defs[k]]]) {
            return 0;
        }
    }
    return 1;
}

/**
 * The value that @p v, defined once in loop @p l, is a copy of, when that value is defined outside the loop and of
 * v's own type; else -1.  Lowering reads a variable through such a copy (`set %v60, %i`), and what the loop makes
 * of it is made before the loop of the variable itself.  The copy stays where it is: made before the loop, it
 * would be a second register live across all of it for a value the variable holds already -- an i386 sieve's outer
 * counter went to the stack for one.
 */
static int copy_source(const licm_cfg_t *c, const licm_values_t *vs, const licm_loop_t *l, int v) {
    const cc_ssa_function_t *f = c->f;
    const cc_ssa_instr_t *d;
    int s;

    if (v < 0 || v >= f->value_count || vs->ndefs[v] != 1 || !plain_int(f, vs, v)) {
        return -1;
    }
    d = &f->instrs[vs->defs[vs->def_off[v]]];
    s = d->lhs;
    if (d->op != CC_SSA_MOV || s < 0 || d->rhs >= 0 || !l->in[c->blk[vs->defs[vs->def_off[v]]]] ||
        !defined_outside(c, vs, l, s) || f->value_ty[s].size != f->value_ty[v].size ||
        f->value_ty[s].is_unsigned != f->value_ty[v].is_unsigned) {
        return -1;
    }
    return s;
}

/**
 * Whether operand @p v of an instruction in loop @p l is the same on every trip: no definition of it is in the
 * loop, or it is a copy of one that has none (copy_source()), which a hoisted instruction reads instead.
 */
static int invariant(const licm_cfg_t *c, const licm_values_t *vs, const licm_loop_t *l, int v) {
    if (v < 0) {
        return 1; /* (no operand: an immediate) */
    }
    return defined_outside(c, vs, l, v) || copy_source(c, vs, l, v) >= 0;
}

/** Whether value @p v is defined once, by an integer constant, which is then *@p k. */
static int const_value(const licm_cfg_t *c, const licm_values_t *vs, int v, long long *k) {
    const cc_ssa_instr_t *d;

    if (v < 0 || v >= c->f->value_count || vs->ndefs[v] != 1) {
        return 0;
    }
    d = &c->f->instrs[vs->defs[vs->def_off[v]]];
    if (d->op != CC_SSA_CONST || !plain_int(c->f, vs, v)) {
        return 0;
    }
    *k = d->imm;
    return 1;
}

/**
 * Whether @p in scales an index by 2, 4 or 8 -- a multiply by one of them, a shift left by 1 to 3 -- which an
 * address takes for nothing, `(%base,%index,8)`: made before the loop, it would be an imul and a register held.
 */
static int index_scaling(const licm_cfg_t *c, const licm_values_t *vs, const cc_ssa_instr_t *in) {
    long long k = 0;

    if (in->op == CC_SSA_MUL) {
        return (const_value(c, vs, in->rhs, &k) || const_value(c, vs, in->lhs, &k)) && (k == 2 || k == 4 || k == 8);
    }
    if (in->op == CC_SSA_SHL) {
        if (in->rhs < 0) {
            k = in->imm;
        } else if (!const_value(c, vs, in->rhs, &k)) {
            return 0;
        }
        return k >= 1 && k <= 3;
    }
    return 0;
}

/** Whether instruction @p i, in loop @p l, may be made once before it. */
static int hoistable(const licm_cfg_t *c, const licm_values_t *vs, const licm_loop_t *l, int i) {
    const cc_ssa_instr_t *in = &c->f->instrs[i];

    if (index_scaling(c, vs, in)) {
        return 0;
    }
    switch (in->op) {
    case CC_SSA_CONST:
    case CC_SSA_STR:
        /* (an immediate wherever it is read: free to move out of a block some trips skip) */
        return in->dst >= 0 && plain_int(c->f, vs, in->dst) && vs->ndefs[in->dst] == 1;
    case CC_SSA_GADDR:
        break;
    case CC_SSA_LO32:
    case CC_SSA_HI32:
        if (in->lhs < 0 || in->rhs >= 0) {
            return 0;
        }
        break;
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
    case CC_SSA_PAIR:
    case CC_SSA_UMULW:
    case CC_SSA_UMULH:
        if (in->lhs < 0) {
            return 0;
        }
        break;
    default:
        return 0;
    }
    if (in->arg_count != 0 || in->call_chain != 0 || !plain_int(c->f, vs, in->dst) || vs->ndefs[in->dst] != 1) {
        return 0;
    }
    if (!l->always[c->blk[i]]) {
        /* made on the trips that reach it only: before the loop, it would hold a register for nothing on the rest */
        return 0;
    }
    return invariant(c, vs, l, in->lhs) && invariant(c, vs, l, in->rhs);
}

/**
 * Append instruction @p i, of loop @p l, to those hoisted into block @p b, an operand that is a copy in the loop
 * read as what it copies (copy_source()). @return 0, or -1 when out of memory
 */
static int push_moved(licm_cfg_t *c, const licm_values_t *vs, const licm_loop_t *l, int b, int i) {
    cc_ssa_instr_t *in = &c->fw->instrs[i];
    int s;

    if ((s = copy_source(c, vs, l, in->lhs)) >= 0) {
        in->lhs = s;
    }
    if ((s = copy_source(c, vs, l, in->rhs)) >= 0) {
        in->rhs = s;
    }
    if (c->nmoved[b] == c->capmoved[b]) {
        int ncap = c->capmoved[b] > 0 ? 2 * c->capmoved[b] : 8;
        int *nm = (int *)realloc(c->moved[b], (size_t)ncap * sizeof(int));

        if (nm == NULL) {
            return -1;
        }
        c->moved[b] = nm;
        c->capmoved[b] = ncap;
    }
    c->moved[b][c->nmoved[b]++] = i;
    c->blk[i] = b;
    return 0;
}

/** Whether instruction @p i is block @p b's terminator, which what is hoisted into b goes before. */
static int is_terminator(const licm_cfg_t *c, int b, int i) {
    return i == c->end[b] - 1 && ends_block(&c->f->instrs[i]);
}

/**
 * Hoist what loop @p l computes the same on every trip into preheader @p ph, over its blocks in the stream's
 * order and the instructions already hoisted into them, until nothing more moves.
 *
 * @return how many instructions moved, or -1 when out of memory
 */
static int hoist_loop(licm_cfg_t *c, const licm_values_t *vs, const licm_loop_t *l, int ph) {
    int moved = 0;
    int again;

    do {
        int b;

        again = 0;
        for (b = 0; b < c->nb; ++b) {
            int i;
            int k;

            if (!l->in[b]) {
                continue;
            }
            for (i = c->start[b]; i < c->end[b]; ++i) {
                if (c->blk[i] == b && !is_terminator(c, b, i) && hoistable(c, vs, l, i)) {
                    if (push_moved(c, vs, l, ph, i) != 0) {
                        return -1;
                    }
                    moved++;
                    again = 1;
                }
            }
            for (k = 0; k < c->nmoved[b]; ++k) {
                i = c->moved[b][k];
                if (c->blk[i] == b && hoistable(c, vs, l, i)) {
                    if (push_moved(c, vs, l, ph, i) != 0) {
                        return -1;
                    }
                    moved++;
                    again = 1;
                }
            }
        }
    } while (again);
    return moved;
}

/** Lay @p f's instructions out again: each block's own, what was hoisted into it, then its terminator. */
static int relayout(cc_ssa_function_t *f, const licm_cfg_t *c) {
    cc_ssa_instr_t *out = (cc_ssa_instr_t *)malloc(f->instr_cap * sizeof(*out));
    size_t w = 0;
    int b;

    if (out == NULL) {
        return -1;
    }
    for (b = 0; b < c->nb; ++b) {
        int last = c->end[b] - 1;
        int term = is_terminator(c, b, last);
        int i;
        int k;

        for (i = c->start[b]; i < c->end[b] - term; ++i) {
            if (c->blk[i] == b) {
                out[w++] = f->instrs[i];
            }
        }
        for (k = 0; k < c->nmoved[b]; ++k) {
            if (c->blk[c->moved[b][k]] == b) {
                out[w++] = f->instrs[c->moved[b][k]];
            }
        }
        if (term) {
            out[w++] = f->instrs[last];
        }
    }
    free(f->instrs);
    f->instrs = out;
    f->instr_count = w;
    return 0;
}

int cc_hoist_invariants(cc_ssa_function_t *f) {
    licm_cfg_t c;
    licm_values_t vs;
    licm_loop_t *loops = NULL;
    int nloops = 0;
    int moved = 0;
    int rc;
    int j;

    if (f->instr_count == 0 || f->value_count <= 0) {
        return 0;
    }
    rc = cfg_build(&c, f);
    c.fw = f;
    if (rc != 0) {
        cfg_free(&c);
        return rc < 0 ? -1 : 0;
    }
    memset(&vs, 0, sizeof(vs));
    if (cfg_dominators(&c) != 0 || (nloops = find_loops(&c, &loops)) < 0 || values_build(&vs, f) != 0) {
        rc = -1;
    }
    for (j = 0; j < nloops && rc == 0; ++j) {
        int ph = preheader(&c, &loops[j]);
        int n;

        if (ph < 0) {
            continue;
        }
        n = hoist_loop(&c, &vs, &loops[j], ph);
        if (n < 0) {
            rc = -1;
        }
        moved += n > 0 ? n : 0;
    }
    if (rc == 0 && moved > 0) {
        rc = relayout(f, &c);
    }
    for (j = 0; j < nloops; ++j) {
        free(loops[j].in);
        free(loops[j].always);
    }
    free(loops);
    values_free(&vs);
    cfg_free(&c);
    return rc < 0 ? -1 : moved > 0;
}
