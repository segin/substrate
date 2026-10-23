/*
 * regalloc_i386.c: which i386 values live in a callee-saved register.
 *
 * The i386 emitter keeps values in %eax, %ecx and %edx only for a while:
 * from one instruction to the next in a block, and into a block one
 * jump comes in by (i386.c's register cache).  Past that a value is in
 * its frame slot, and every block that reads it loads it again -- in
 * 16-bit code a frame access is five to eight bytes.  Here the values
 * read and written most get %ebx, %esi or %edi for the whole of their
 * live range: liveness over the blocks, one interval per value from its
 * first point live to its last, and the busiest first into a register
 * no interval there meets.  The registers are callee-saved: a call keeps
 * them, and the prologue saves the caller's.
 *
 * The same intervals let the frame share a slot between values whose
 * intervals do not meet (x86_value_spans(), for common.c's
 * build_slot_layout()), across blocks as within one.
 */

#include "cc_x86.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** The most bits of liveness (values x blocks) a function's sets may take; past it, no value has a home. */
#define HOME_MAX_LIVE_BITS (64UL * 1024 * 1024)
/** The fewest reads and writes for a value to have a home. */
#define HOME_MIN_WEIGHT 3
/** The fewest for a value to make a register used that no other value uses yet: it costs a save and restores. */
#define HOME_NEW_REG_WEIGHT 12

typedef uint32_t word_t;
#define WORD_BITS 32

/** The blocks of a function and the values live into and out of each. */
typedef struct {
    int nblocks;
    int nbits;          /**< the values the function reads or writes, numbered 0 up in the sets */
    int *bit_of;        /**< per value: its bit in the sets, or -1 for one never read or written */
    int *value_of;      /**< per bit: its value */
    size_t nwords;      /**< words of a set of values */
    int *block_of;      /**< per instruction: its block */
    int *first;         /**< per block: its first instruction */
    int *last;          /**< per block: its last */
    int *succ;          /**< per block: two successors, -1 for none */
    int *goto_at;       /**< per block: the asm goto it ends in, whose labels are successors too; else -1 */
    int *label_block;   /**< per label: its block, or -1 */
    word_t *use;        /**< per block: the values read before it defines them */
    word_t *def;        /**< per block: the values it defines */
    word_t *in;         /**< per block: live in */
    word_t *out;        /**< per block: live out */
} live_t;

static void live_free(live_t *l) {
    free(l->bit_of);
    free(l->value_of);
    free(l->block_of);
    free(l->first);
    free(l->last);
    free(l->succ);
    free(l->goto_at);
    free(l->label_block);
    free(l->use);
    free(l->def);
    free(l->in);
    free(l->out);
    memset(l, 0, sizeof(*l));
}

static int bit_test(const word_t *s, int v) {
    return (s[v / WORD_BITS] >> (v % WORD_BITS)) & 1u;
}

static void bit_set(word_t *s, int v) {
    s[v / WORD_BITS] |= (word_t)1u << (v % WORD_BITS);
}

/** Whether instruction @p in ends its block: what runs next is not just the instruction after it. */
static int ends_block(const cc_ssa_instr_t *in) {
    return in->op == CC_SSA_BR || in->op == CC_SSA_BR_COND || in->op == CC_SSA_RET || in->op == CC_SSA_TRAP ||
           (in->op == CC_SSA_ASM && in->asm_is_goto);
}

typedef struct {
    const live_t *l;
    int b;
} use_ctx_t;

/** x86_for_each_use() callback: a read before the block defines it is the block's use. */
static void note_block_use(int v, void *ctx) {
    use_ctx_t *c = (use_ctx_t *)ctx;
    size_t base = (size_t)c->b * c->l->nwords;
    int bit = c->l->bit_of[v];

    if (!bit_test(c->l->def + base, bit)) {
        bit_set(c->l->use + base, bit);
    }
}

/** Each value instruction @p in defines: its result and an asm's outputs. */
static void for_each_def(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, void (*fn)(int v, void *ctx), void *ctx) {
    size_t k;

    if (in->dst >= 0 && in->dst < f->value_count) {
        fn(in->dst, ctx);
    }
    for (k = 0; in->op == CC_SSA_ASM && k < in->asm_out_count; ++k) {
        int o = in->asm_out_values[k];

        if (CC_SSA_ASM_MEM_INDIRECT_P(o)) {
            o = CC_SSA_ASM_MEM_INDIRECT_DECODE(o);
        }
        if (o >= 0 && o < f->value_count) {
            fn(o, ctx);
        }
    }
}

/** for_each_def() callback: the block defines it. */
static void note_block_def(int v, void *ctx) {
    use_ctx_t *c = (use_ctx_t *)ctx;

    bit_set(c->l->def + (size_t)c->b * c->l->nwords, c->l->bit_of[v]);
}

/** x86_for_each_use() and for_each_def() callback: give the value a bit in the sets if it has none yet. */
static void note_bit(int v, void *ctx) {
    live_t *l = (live_t *)ctx;

    if (l->bit_of[v] < 0) {
        l->value_of[l->nbits] = v;
        l->bit_of[v] = l->nbits++;
    }
}

/**
 * Split @p f into blocks -- at each label and after each jump, branch, return, trap or asm goto -- and work out
 * what is live into and out of each.  1 when @p f is too big for it (nothing done), -1 when out of memory.
 */
static int live_build(live_t *l, const cc_ssa_function_t *f) {
    int *label_block = NULL;
    size_t i;
    int b;
    int changed;

    memset(l, 0, sizeof(*l));
    l->block_of = (int *)malloc(f->instr_count * sizeof(*l->block_of));
    label_block = (int *)malloc((size_t)(f->label_count > 0 ? f->label_count : 1) * sizeof(*label_block));
    if (l->block_of == NULL || label_block == NULL) {
        free(label_block);
        live_free(l);
        return -1;
    }
    for (i = 0; i < (size_t)f->label_count; ++i) {
        label_block[i] = -1;
    }
    b = 0;
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (i > 0 && (in->op == CC_SSA_LABEL || ends_block(&f->instrs[i - 1]))) {
            b++;
        }
        l->block_of[i] = b;
        if (in->op == CC_SSA_LABEL && in->label >= 0 && in->label < f->label_count) {
            label_block[in->label] = b;
        }
    }
    l->nblocks = b + 1;
    /*
     * the sets have a bit for each value the function reads or writes only: numbering is sparse after the
     * middle end's passes -- Linux's hidinput_configure_usage() reads or writes 6.5k of its 60k values
     */
    l->bit_of = (int *)malloc((size_t)f->value_count * sizeof(*l->bit_of));
    l->value_of = (int *)malloc((size_t)f->value_count * sizeof(*l->value_of));
    if (l->bit_of == NULL || l->value_of == NULL) {
        free(label_block);
        live_free(l);
        return -1;
    }
    for (i = 0; i < (size_t)f->value_count; ++i) {
        l->bit_of[i] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        x86_for_each_use(f, &f->instrs[i], note_bit, l);
        for_each_def(f, &f->instrs[i], note_bit, l);
    }
    l->nwords = ((size_t)l->nbits + WORD_BITS - 1) / WORD_BITS;
    if (l->nwords == 0) {
        l->nwords = 1;
    }
    if ((unsigned long)l->nblocks * l->nwords * WORD_BITS > HOME_MAX_LIVE_BITS) {
        free(label_block);
        live_free(l);
        return 1;
    }
    l->first = (int *)malloc((size_t)l->nblocks * sizeof(*l->first));
    l->last = (int *)malloc((size_t)l->nblocks * sizeof(*l->last));
    l->succ = (int *)malloc((size_t)l->nblocks * 2 * sizeof(*l->succ));
    l->goto_at = (int *)malloc((size_t)l->nblocks * sizeof(*l->goto_at));
    l->use = (word_t *)calloc((size_t)l->nblocks * l->nwords, sizeof(word_t));
    l->def = (word_t *)calloc((size_t)l->nblocks * l->nwords, sizeof(word_t));
    l->in = (word_t *)calloc((size_t)l->nblocks * l->nwords, sizeof(word_t));
    l->out = (word_t *)calloc((size_t)l->nblocks * l->nwords, sizeof(word_t));
    if (l->first == NULL || l->last == NULL || l->succ == NULL || l->goto_at == NULL || l->use == NULL ||
        l->def == NULL || l->in == NULL || l->out == NULL) {
        free(label_block);
        live_free(l);
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        use_ctx_t c;

        b = l->block_of[i];
        if (i == 0 || l->block_of[i - 1] != b) {
            l->first[b] = (int)i;
        }
        l->last[b] = (int)i;
        c.l = l;
        c.b = b;
        x86_for_each_use(f, in, note_block_use, &c);
        for_each_def(f, in, note_block_def, &c);
    }
    for (b = 0; b < l->nblocks; ++b) {
        const cc_ssa_instr_t *end = &f->instrs[l->last[b]];
        int *s = &l->succ[2 * b];

        s[0] = -1;
        s[1] = -1;
        l->goto_at[b] = -1;
        if (end->op == CC_SSA_ASM && end->asm_is_goto) {
            /* on to the next instruction, or to any of its labels */
            l->goto_at[b] = l->last[b];
            s[0] = b + 1 < l->nblocks ? b + 1 : -1;
        } else if (end->op == CC_SSA_BR) {
            s[0] = end->label >= 0 && end->label < f->label_count ? label_block[end->label] : -1;
        } else if (end->op == CC_SSA_BR_COND) {
            s[0] = end->true_label >= 0 && end->true_label < f->label_count ? label_block[end->true_label] : -1;
            s[1] = end->false_label >= 0 && end->false_label < f->label_count ? label_block[end->false_label] : -1;
        } else if (end->op != CC_SSA_RET && end->op != CC_SSA_TRAP && b + 1 < l->nblocks) {
            s[0] = b + 1;
        }
    }
    l->label_block = label_block;
    /* out = the successors' in; in = use + (out - def); backwards until nothing changes */
    do {
        changed = 0;
        for (b = l->nblocks - 1; b >= 0; --b) {
            word_t *out = l->out + (size_t)b * l->nwords;
            word_t *inb = l->in + (size_t)b * l->nwords;
            const word_t *use = l->use + (size_t)b * l->nwords;
            const word_t *def = l->def + (size_t)b * l->nwords;
            size_t w;
            int k;

            for (k = 0; k < 2; ++k) {
                int s = l->succ[2 * b + k];

                if (s >= 0) {
                    const word_t *sin = l->in + (size_t)s * l->nwords;

                    for (w = 0; w < l->nwords; ++w) {
                        out[w] |= sin[w];
                    }
                }
            }
            if (l->goto_at[b] >= 0) {
                const cc_ssa_instr_t *g = &f->instrs[l->goto_at[b]];
                size_t t;

                for (t = 0; t < g->asm_goto_count && g->asm_goto_labels != NULL; ++t) {
                    int lab = g->asm_goto_labels[t];
                    int s = lab >= 0 && lab < f->label_count ? l->label_block[lab] : -1;

                    if (s >= 0) {
                        const word_t *sin = l->in + (size_t)s * l->nwords;

                        for (w = 0; w < l->nwords; ++w) {
                            out[w] |= sin[w];
                        }
                    }
                }
            }
            for (w = 0; w < l->nwords; ++w) {
                word_t nin = use[w] | (out[w] & ~def[w]);

                if (nin != inb[w]) {
                    inb[w] = nin;
                    changed = 1;
                }
            }
        }
    } while (changed);
    return 0;
}

typedef struct {
    int *lo;
    int *hi;
    int *weight;
    int at;
} span_ctx_t;

/** x86_for_each_use() and for_each_def() callback: the value is live at the instruction. */
static void note_access(int v, void *ctx) {
    span_ctx_t *c = (span_ctx_t *)ctx;

    if (c->at < c->lo[v]) {
        c->lo[v] = c->at;
    }
    if (c->at > c->hi[v]) {
        c->hi[v] = c->at;
    }
}

/** x86_for_each_use() and for_each_def() callback: the value is accessed at the instruction. */
static void note_weight(int v, void *ctx) {
    span_ctx_t *c = (span_ctx_t *)ctx;

    c->weight[v]++;
}

int x86_value_spans(const cc_ssa_function_t *f, const int *alias, int *lo, int *hi) {
    live_t l;
    span_ctx_t sc;
    size_t i;
    int v;
    int k;
    int b;
    int rc;

    for (v = 0; v < f->value_count; ++v) {
        lo[v] = INT32_MAX;
        hi[v] = -1;
    }
    if (f->value_count <= 0 || f->instr_count == 0) {
        return 0;
    }
    for (i = 0; i < f->instr_count; ++i) {
        /* a jump to a label's address goes where the blocks do not say */
        if (f->instrs[i].op == CC_SSA_LADDR) {
            return 1;
        }
    }
    rc = live_build(&l, f);
    if (rc != 0) {
        return rc;
    }
    sc.lo = lo;
    sc.hi = hi;
    sc.weight = NULL;
    /* each value's interval: where it is read and written, and the blocks it is live into or out of */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        sc.at = (int)i;
        x86_for_each_use(f, in, note_access, &sc);
        for_each_def(f, in, note_access, &sc);
    }
    for (b = 0; b < l.nblocks; ++b) {
        const word_t *inb = l.in + (size_t)b * l.nwords;
        const word_t *out = l.out + (size_t)b * l.nwords;
        size_t w;

        for (w = 0; w < l.nwords; ++w) {
            word_t live = inb[w] | out[w];

            while (live != 0) {
                int bit = 0;

                while (((live >> bit) & 1u) == 0) {
                    bit++;
                }
                live &= ~((word_t)1u << bit);
                k = (int)(w * WORD_BITS) + bit;
                if (k >= l.nbits) {
                    continue;
                }
                v = l.value_of[k];
                if (bit_test(inb, k) && l.first[b] < lo[v]) {
                    lo[v] = l.first[b];
                }
                if (bit_test(out, k) && l.last[b] > hi[v]) {
                    hi[v] = l.last[b];
                }
            }
        }
    }
    /* a folded address is read as its base (alias): the base is live wherever the address is, a loop around included */
    for (v = 0; alias != NULL && v < f->value_count; ++v) {
        int base = alias[v];

        if (base < 0 || base >= f->value_count || base == v || hi[v] < 0) {
            continue;
        }
        if (lo[v] < lo[base]) {
            lo[base] = lo[v];
        }
        if (hi[v] > hi[base]) {
            hi[base] = hi[v];
        }
    }
    live_free(&l);
    return 0;
}

typedef struct {
    int weight;
    int v;
} cand_t;

/** qsort() order: the busier value first, then the lower number. */
static int cand_cmp(const void *a, const void *b) {
    const cand_t *x = (const cand_t *)a;
    const cand_t *y = (const cand_t *)b;

    if (x->weight != y->weight) {
        return x->weight > y->weight ? -1 : 1;
    }
    return x->v - y->v;
}

int i386_assign_homes(const cc_ssa_function_t *f, const unsigned char *addr_taken, const unsigned char *remat,
                      const int *alias, int *home, unsigned *used) {
    span_ctx_t sc;
    unsigned char *skip = NULL;
    cand_t *cand = NULL;
    int ncand = 0;
    int *taken[I386_HOME_REGS]; /* per register: the values given it */
    int ntaken[I386_HOME_REGS];
    size_t i;
    int v;
    int r;
    int rc;

    *used = 0;
    for (v = 0; v < f->value_count; ++v) {
        home[v] = -1;
    }
    if (f->value_count <= 0 || f->instr_count == 0) {
        return 0;
    }
    for (i = 0; i < f->instr_count; ++i) {
        /*
         * an asm goto's jump would skip the restores of the callee-saved registers it saves around itself
         * (common.c, emit_inline_asm()): no homes
         */
        if (f->instrs[i].op == CC_SSA_ASM && f->instrs[i].asm_is_goto) {
            return 0;
        }
    }
    memset(taken, 0, sizeof(taken));
    memset(ntaken, 0, sizeof(ntaken));
    sc.lo = (int *)malloc((size_t)f->value_count * sizeof(int));
    sc.hi = (int *)malloc((size_t)f->value_count * sizeof(int));
    sc.weight = (int *)calloc((size_t)f->value_count, sizeof(int));
    skip = (unsigned char *)calloc((size_t)f->value_count, 1);
    cand = (cand_t *)malloc((size_t)f->value_count * sizeof(*cand));
    for (r = 0; r < I386_HOME_REGS; ++r) {
        taken[r] = (int *)malloc((size_t)f->value_count * sizeof(int));
    }
    if (sc.lo == NULL || sc.hi == NULL || sc.weight == NULL || skip == NULL || cand == NULL ||
        taken[0] == NULL || taken[1] == NULL || taken[2] == NULL) {
        rc = -1;
        goto done;
    }
    rc = x86_value_spans(f, alias, sc.lo, sc.hi);
    if (rc != 0) {
        /* 1: blocks it cannot follow, or too big; no value has a home */
        rc = rc < 0 ? -1 : 0;
        goto done;
    }
    /* how often each value is read and written */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        sc.at = (int)i;
        x86_for_each_use(f, in, note_weight, &sc);
        for_each_def(f, in, note_weight, &sc);
        if (in->op == CC_SSA_ASM) {
            size_t k;

            /* an asm's operand is read from and written to its slot: no home */
            for (k = 0; k < in->asm_out_count + in->asm_in_count; ++k) {
                int o = k < in->asm_out_count ? in->asm_out_values[k] : in->asm_in_values[k - in->asm_out_count];

                if (CC_SSA_ASM_MEM_INDIRECT_P(o)) {
                    o = CC_SSA_ASM_MEM_INDIRECT_DECODE(o);
                }
                if (o >= 0 && o < f->value_count) {
                    skip[o] = 1;
                }
            }
        }
        if ((in->op == CC_SSA_STACKALLOC || in->op == CC_SSA_DYNALLOC) && in->dst >= 0 && in->dst < f->value_count) {
            skip[in->dst] = 1;
        }
    }
    /* a folded address is read as its base (alias): read as often */
    for (v = 0; alias != NULL && v < f->value_count; ++v) {
        int base = alias[v];

        if (base < 0 || base >= f->value_count || base == v || sc.hi[v] < 0) {
            continue;
        }
        sc.weight[base] += sc.weight[v];
    }
    /*
     * the candidates: integer words, accessed often enough.  One live across an asm keeps its home: the asm
     * saves and restores each callee-saved register it uses (common.c, emit_inline_asm()).
     */
    for (v = 0; v < f->value_count; ++v) {
        if (skip[v] || addr_taken[v] || (remat != NULL && remat[v]) || sc.hi[v] < 0 ||
            sc.weight[v] < HOME_MIN_WEIGHT || cc_ssa_value_class(f, v) != CC_VAL_I64 ||
            cc_ssa_value_size(f, v) > 4) {
            continue;
        }
        cand[ncand].weight = sc.weight[v];
        cand[ncand].v = v;
        ncand++;
    }
    qsort(cand, (size_t)ncand, sizeof(*cand), cand_cmp);
    /* the busiest first, into a register already used where none of its values meets it, else a new one */
    for (i = 0; i < (size_t)ncand; ++i) {
        int pick = -1;
        int pass;

        v = cand[i].v;
        for (pass = 0; pass < 2 && pick < 0; ++pass) {
            for (r = 0; r < I386_HOME_REGS && pick < 0; ++r) {
                int k;
                int meets = 0;

                if ((pass == 0) != ((*used & (1u << r)) != 0)) {
                    continue;
                }
                if (pass == 1 && cand[i].weight < HOME_NEW_REG_WEIGHT) {
                    continue;
                }
                for (k = 0; k < ntaken[r]; ++k) {
                    int u = taken[r][k];

                    if (!(sc.hi[v] < sc.lo[u] || sc.hi[u] < sc.lo[v])) {
                        meets = 1;
                        break;
                    }
                }
                if (!meets) {
                    pick = r;
                }
            }
        }
        if (pick >= 0) {
            home[v] = pick;
            taken[pick][ntaken[pick]++] = v;
            *used |= 1u << pick;
        }
    }
    rc = 0;
done:
    if (rc != 0) {
        for (v = 0; v < f->value_count; ++v) {
            home[v] = -1;
        }
        *used = 0;
    }
    for (r = 0; r < I386_HOME_REGS; ++r) {
        free(taken[r]);
    }
    free(sc.lo);
    free(sc.hi);
    free(sc.weight);
    free(skip);
    free(cand);
    return rc;
}
