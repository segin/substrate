/*
 * linscan.c: the x86 register allocator, over machine IR (cc_x86_mir.h).
 *
 * Linear scan over live intervals with holes.  The instructions are in
 * one order, instruction i at positions 2i (where it reads) and 2i+1
 * (where it writes); liveness over the blocks gives each virtual
 * register its ranges of positions, and the physical registers that
 * instructions name -- a call's arguments and clobbers, a division's
 * %eax and %edx -- are fixed ranges no virtual register may meet.  The
 * intervals are taken in order of their start, each into a register free
 * for the whole of it: the one a copy suggests (a hint), else the first
 * of the target's order.  When none is, the cheaper of it and what holds
 * the best register goes to memory: its weight is how often it is read
 * and written (ten times in a loop) per instruction it spans.
 *
 * A spilled register lives in a frame slot.  Each operand of it becomes
 * the slot itself where the instruction can take memory there, else a
 * short-lived register loaded before the instruction and stored after
 * it, never spilled; and the allocation starts over, until a round
 * spills nothing.  Spilled registers whose intervals do not meet share a
 * slot.
 *
 * Before allocating, instructions that compute a register nothing reads
 * are dropped, which is how the selector's folding (an address into the
 * loads that use it, a constant into an immediate) leaves no trace.
 */

#include "cc_x86_mir.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** The most bits of liveness (virtual registers x blocks) a function's sets may take; past it, the caller's path. */
#define LS_MAX_LIVE_BITS (64UL * 1024 * 1024)
/** The most rounds of allocation before giving up. */
#define LS_MAX_ROUNDS 12

typedef uint32_t word_t;
#define WORD_BITS 32

/** A range of positions, [from, to). */
typedef struct {
    int from;
    int to;
} xrange_t;

/** A live interval: a virtual register's ranges, or a physical register's fixed ones. */
typedef struct {
    xrange_t *r;
    int n;
    int cap;
    int cursor;        /**< the first range that may still matter, as positions go up */
    int phys;          /**< the register given it, or -1 */
    double freq;       /**< its reads and writes, ten times each loop deep */
    double weight;     /**< freq per instruction spanned; huge when it may not be spilled */
    float hint_w[XR_NPHYS]; /**< per physical register: how often copies to and from it suggest it, by loop depth */
    float hint_own[XR_NPHYS]; /**< the same of its own copies alone, not passed on along chains of them */
    int hint_vr;       /**< a virtual register a copy suggests sharing one with, or XR_NONE */
    float hint_vr_w;   /**< how often that copy runs, by loop depth: its partner's register is worth as much */
    unsigned mask;     /**< the registers it may have */
    unsigned char spilled;
} ival_t;

/** One function's allocation state. */
typedef struct {
    xfunc_t *xf;
    const xtarget_t *t;
    int nblocks;
    int *block_of;   /**< per instruction */
    int *bfirst;     /**< per block: first instruction */
    int *blast;      /**< and last */
    int *succ;       /**< per block: two successors, -1 for none */
    int *label_block;/**< per label */
    size_t nwords;   /**< words of a set of virtual registers */
    word_t *use;     /**< per block */
    word_t *def;
    word_t *in;
    word_t *out;
    int *depth;      /**< per instruction: how many loops it is in */
    ival_t *iv;      /**< per virtual register */
    int niv;         /**< the virtual registers there were when iv was made */
    ival_t fixed[XR_NPHYS];
} ls_t;

static int bit_test(const word_t *s, int v) {
    return (s[v / WORD_BITS] >> (v % WORD_BITS)) & 1u;
}

static void bit_set(word_t *s, int v) {
    s[v / WORD_BITS] |= (word_t)1u << (v % WORD_BITS);
}

static void bit_clear(word_t *s, int v) {
    s[v / WORD_BITS] &= ~((word_t)1u << (v % WORD_BITS));
}

/** Free the per-round state of @p ls, keeping xf and t. */
static void ls_free(ls_t *ls) {
    int i;

    free(ls->block_of);
    free(ls->bfirst);
    free(ls->blast);
    free(ls->succ);
    free(ls->label_block);
    free(ls->use);
    free(ls->def);
    free(ls->in);
    free(ls->out);
    free(ls->depth);
    if (ls->iv != NULL) {
        for (i = 0; i < ls->niv; ++i) {
            free(ls->iv[i].r);
        }
    }
    free(ls->iv);
    for (i = 0; i < XR_NPHYS; ++i) {
        free(ls->fixed[i].r);
    }
    ls->block_of = ls->bfirst = ls->blast = ls->succ = ls->label_block = ls->depth = NULL;
    ls->use = ls->def = ls->in = ls->out = NULL;
    ls->iv = NULL;
    memset(ls->fixed, 0, sizeof(ls->fixed));
}

/** Call @p fn for each virtual register instruction @p in reads (@p role 1) or writes (2), and the operand it is in. */
static void for_each_vreg(const xinst_t *in, int role, void (*fn)(int r, int k, void *ctx), void *ctx) {
    int k;

    for (k = 0; k < in->nops; ++k) {
        const xop_t *o = &in->o[k];

        if (o->kind == XO_REG && XR_IS_VIRT(o->reg) && (xi_role(in, k) & role) != 0) {
            fn(o->reg, k, ctx);
        } else if (o->kind == XO_MEM && role == 1) {
            /* an address's registers are read, whatever is done with the memory */
            if (XR_IS_VIRT(o->base)) {
                fn(o->base, k, ctx);
            }
            if (XR_IS_VIRT(o->index)) {
                fn(o->index, k, ctx);
            }
        }
    }
}

/** The physical registers @p in reads that the allocator gives out: its operands' and the implicit ones. */
static unsigned phys_uses(const xinst_t *in, unsigned alloc) {
    unsigned m = in->imp_use;
    int k;

    for (k = 0; k < in->nops; ++k) {
        const xop_t *o = &in->o[k];

        if (o->kind == XO_REG && o->reg >= 0 && !XR_IS_VIRT(o->reg) && (xi_role(in, k) & 1) != 0) {
            m |= XR_BIT(o->reg);
        } else if (o->kind == XO_MEM) {
            if (o->base >= 0 && !XR_IS_VIRT(o->base)) {
                m |= XR_BIT(o->base);
            }
            if (o->index >= 0 && !XR_IS_VIRT(o->index)) {
                m |= XR_BIT(o->index);
            }
        }
    }
    return m & alloc;
}

/** And the ones it writes or clobbers. */
static unsigned phys_defs(const xinst_t *in, unsigned alloc) {
    unsigned m = in->imp_def | in->clobber;
    int k;

    for (k = 0; k < in->nops; ++k) {
        const xop_t *o = &in->o[k];

        if (o->kind == XO_REG && o->reg >= 0 && !XR_IS_VIRT(o->reg) && (xi_role(in, k) & 2) != 0) {
            m |= XR_BIT(o->reg);
        }
    }
    return m & alloc;
}

typedef struct {
    ls_t *ls;
    int b;
} blk_ctx_t;

/** for_each_vreg() callback: a read before the block writes it is the block's use. */
static void note_use(int r, int k, void *ctx) {
    blk_ctx_t *c = (blk_ctx_t *)ctx;
    size_t base = (size_t)c->b * c->ls->nwords;
    int v = r - XR_VBASE;

    (void)k;
    if (!bit_test(c->ls->def + base, v)) {
        bit_set(c->ls->use + base, v);
    }
}

/** for_each_vreg() callback: the block writes it. */
static void note_def(int r, int k, void *ctx) {
    blk_ctx_t *c = (blk_ctx_t *)ctx;

    (void)k;
    bit_set(c->ls->def + (size_t)c->b * c->ls->nwords, r - XR_VBASE);
}

/**
 * Split the instructions into blocks and work out what is live into and out of each.
 *
 * @return 0; 1 when the function is too big for it; -1 when out of memory
 */
static int ls_liveness(ls_t *ls) {
    xfunc_t *xf = ls->xf;
    int i;
    int b;
    int changed;

    ls->block_of = (int *)malloc((size_t)(xf->nins > 0 ? xf->nins : 1) * sizeof(int));
    ls->label_block = (int *)malloc((size_t)(xf->nlabels > 0 ? xf->nlabels : 1) * sizeof(int));
    if (ls->block_of == NULL || ls->label_block == NULL) {
        return -1;
    }
    for (i = 0; i < xf->nlabels; ++i) {
        ls->label_block[i] = -1;
    }
    b = 0;
    for (i = 0; i < xf->nins; ++i) {
        if (i > 0 && (xf->ins[i].op == XI_LABEL || xi_ends_block(&xf->ins[i - 1]))) {
            b++;
        }
        ls->block_of[i] = b;
        if (xf->ins[i].op == XI_LABEL && xf->ins[i].label >= 0 && xf->ins[i].label < xf->nlabels) {
            ls->label_block[xf->ins[i].label] = b;
        }
    }
    ls->nblocks = xf->nins > 0 ? b + 1 : 0;
    ls->nwords = ((size_t)xf->nvr + WORD_BITS - 1) / WORD_BITS;
    if (ls->nwords == 0) {
        ls->nwords = 1;
    }
    if ((unsigned long)ls->nblocks * ls->nwords * WORD_BITS > LS_MAX_LIVE_BITS) {
        return 1;
    }
    ls->bfirst = (int *)malloc((size_t)(ls->nblocks + 1) * sizeof(int));
    ls->blast = (int *)malloc((size_t)(ls->nblocks + 1) * sizeof(int));
    ls->succ = (int *)malloc((size_t)(ls->nblocks + 1) * 2 * sizeof(int));
    ls->use = (word_t *)calloc((size_t)(ls->nblocks + 1) * ls->nwords, sizeof(word_t));
    ls->def = (word_t *)calloc((size_t)(ls->nblocks + 1) * ls->nwords, sizeof(word_t));
    ls->in = (word_t *)calloc((size_t)(ls->nblocks + 1) * ls->nwords, sizeof(word_t));
    ls->out = (word_t *)calloc((size_t)(ls->nblocks + 1) * ls->nwords, sizeof(word_t));
    if (ls->bfirst == NULL || ls->blast == NULL || ls->succ == NULL || ls->use == NULL || ls->def == NULL ||
        ls->in == NULL || ls->out == NULL) {
        return -1;
    }
    for (i = 0; i < xf->nins; ++i) {
        const xinst_t *in = &xf->ins[i];
        blk_ctx_t c;

        b = ls->block_of[i];
        if (i == 0 || ls->block_of[i - 1] != b) {
            ls->bfirst[b] = i;
        }
        ls->blast[b] = i;
        c.ls = ls;
        c.b = b;
        for_each_vreg(in, 1, note_use, &c);
        for_each_vreg(in, 2, note_def, &c);
    }
    for (b = 0; b < ls->nblocks; ++b) {
        const xinst_t *end = &xf->ins[ls->blast[b]];
        int *s = &ls->succ[2 * b];

        s[0] = -1;
        s[1] = -1;
        if (end->op == XI_JMP || end->op == XI_JCC) {
            s[0] = end->label >= 0 && end->label < xf->nlabels ? ls->label_block[end->label] : -1;
        }
        if (end->op != XI_JMP && end->op != XI_JMPT && end->op != XI_RET && end->op != XI_TRAP &&
            b + 1 < ls->nblocks) {
            s[1] = b + 1;
        }
    }
    do {
        changed = 0;
        for (b = ls->nblocks - 1; b >= 0; --b) {
            word_t *out = ls->out + (size_t)b * ls->nwords;
            word_t *inb = ls->in + (size_t)b * ls->nwords;
            const word_t *use = ls->use + (size_t)b * ls->nwords;
            const word_t *def = ls->def + (size_t)b * ls->nwords;
            size_t w;
            int k;

            for (k = 0; k < 2; ++k) {
                int s = ls->succ[2 * b + k];

                if (s >= 0) {
                    const word_t *sin = ls->in + (size_t)s * ls->nwords;

                    for (w = 0; w < ls->nwords; ++w) {
                        out[w] |= sin[w];
                    }
                }
            }
            {
                /* an asm goto goes on to any of its labels too, and a jump through a table to any of its */
                const xinst_t *end = &xf->ins[ls->blast[b]];

                for (k = 0; (end->op == XI_ASM || end->op == XI_JMPT) && k < end->ntargets; ++k) {
                    int lab = end->targets[k];
                    int s = lab >= 0 && lab < xf->nlabels ? ls->label_block[lab] : -1;

                    if (s >= 0) {
                        const word_t *sin = ls->in + (size_t)s * ls->nwords;

                        for (w = 0; w < ls->nwords; ++w) {
                            out[w] |= sin[w];
                        }
                    }
                }
            }
            for (w = 0; w < ls->nwords; ++w) {
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
    word_t *live;
} live_ctx_t;

/** for_each_vreg() callback: the register is live. */
static void mark_live(int r, int k, void *ctx) {
    (void)k;
    bit_set(((live_ctx_t *)ctx)->live, r - XR_VBASE);
}

/**
 * Drop the instructions that only compute a register no one reads after them, block by block from the end.
 *
 * @return how many were dropped, or -1 when out of memory
 */
static int ls_dce(ls_t *ls) {
    xfunc_t *xf = ls->xf;
    word_t *live = (word_t *)malloc(ls->nwords * sizeof(word_t));
    unsigned char *dead = (unsigned char *)calloc((size_t)(xf->nins > 0 ? xf->nins : 1), 1);
    live_ctx_t c;
    int dropped = 0;
    int b;
    int i;
    int j;

    if (live == NULL || dead == NULL) {
        free(live);
        free(dead);
        return -1;
    }
    c.live = live;
    for (b = 0; b < ls->nblocks; ++b) {
        memcpy(live, ls->out + (size_t)b * ls->nwords, ls->nwords * sizeof(word_t));
        for (i = ls->blast[b]; i >= ls->bfirst[b]; --i) {
            const xinst_t *in = &xf->ins[i];

            if (xi_pure(in) && !bit_test(live, in->o[in->nops - 1].reg - XR_VBASE)) {
                dead[i] = 1;
                dropped++;
                continue;
            }
            /* what it writes is dead before it, unless it reads it too */
            {
                int k;

                for (k = 0; k < in->nops; ++k) {
                    const xop_t *o = &in->o[k];

                    if (o->kind == XO_REG && XR_IS_VIRT(o->reg) && xi_role(in, k) == 2) {
                        bit_clear(live, o->reg - XR_VBASE);
                    }
                }
            }
            for_each_vreg(in, 1, mark_live, &c);
        }
    }
    for (i = 0, j = 0; i < xf->nins; ++i) {
        if (!dead[i]) {
            xf->ins[j++] = xf->ins[i];
        }
    }
    xf->nins = j;
    free(live);
    free(dead);
    return dropped;
}

/** Add range [@p from, @p to) to @p iv, ranges being added from the last position back. */
static int iv_add(ival_t *iv, int from, int to) {
    if (iv->n > 0) {
        xrange_t *last = &iv->r[iv->n - 1];

        /* meeting or overlapping the one added before (which starts later): one range */
        if (to >= last->from) {
            if (from < last->from) {
                last->from = from;
            }
            if (to > last->to) {
                last->to = to;
            }
            return 0;
        }
    }
    if (iv->n == iv->cap) {
        int ncap = iv->cap > 0 ? iv->cap * 2 : 4;
        xrange_t *n = (xrange_t *)realloc(iv->r, (size_t)ncap * sizeof(*n));

        if (n == NULL) {
            return -1;
        }
        iv->r = n;
        iv->cap = ncap;
    }
    iv->r[iv->n].from = from;
    iv->r[iv->n].to = to;
    iv->n++;
    return 0;
}

/** Make the latest range of @p iv start at @p from: it is written there. */
static void iv_start_at(ival_t *iv, int from) {
    if (iv->n > 0 && iv->r[iv->n - 1].from <= from) {
        iv->r[iv->n - 1].from = from;
    }
}

/** Put the ranges of @p iv in order (they were added from the end) and join those that meet. */
static void iv_finish(ival_t *iv) {
    int i;
    int j;

    for (i = 0, j = iv->n - 1; i < j; ++i, --j) {
        xrange_t t = iv->r[i];

        iv->r[i] = iv->r[j];
        iv->r[j] = t;
    }
    for (i = 1, j = 0; i < iv->n; ++i) {
        if (iv->r[i].from <= iv->r[j].to) {
            if (iv->r[i].to > iv->r[j].to) {
                iv->r[j].to = iv->r[i].to;
            }
        } else {
            iv->r[++j] = iv->r[i];
        }
    }
    if (iv->n > 0) {
        iv->n = j + 1;
    }
    iv->cursor = 0;
}

typedef struct {
    ls_t *ls;
    word_t *live;
    int first;   /**< the block's first position */
    int pos;     /**< the instruction's */
    int oom;
} build_ctx_t;

/** for_each_vreg() callback: a read; live from the block's start, unless it is already. */
static void build_use(int r, int k, void *ctx) {
    build_ctx_t *c = (build_ctx_t *)ctx;
    int v = r - XR_VBASE;

    (void)k;
    if (!bit_test(c->live, v)) {
        bit_set(c->live, v);
        if (iv_add(&c->ls->iv[v], c->first, c->pos + 1) != 0) {
            c->oom = 1;
        }
    }
}

/** Find the loops: each jump back to a label puts what is between in one more. */
static int ls_depth(ls_t *ls) {
    xfunc_t *xf = ls->xf;
    int *at = (int *)malloc((size_t)(xf->nlabels > 0 ? xf->nlabels : 1) * sizeof(int));
    int i;
    int d = 0;

    ls->depth = (int *)calloc((size_t)xf->nins + 1, sizeof(int));
    if (at == NULL || ls->depth == NULL) {
        free(at);
        return -1;
    }
    for (i = 0; i < xf->nlabels; ++i) {
        at[i] = -1;
    }
    for (i = 0; i < xf->nins; ++i) {
        if (xf->ins[i].op == XI_LABEL && xf->ins[i].label >= 0 && xf->ins[i].label < xf->nlabels) {
            at[xf->ins[i].label] = i;
        }
    }
    for (i = 0; i < xf->nins; ++i) {
        const xinst_t *in = &xf->ins[i];

        if ((in->op == XI_JMP || in->op == XI_JCC) && in->label >= 0 && in->label < xf->nlabels &&
            at[in->label] >= 0 && at[in->label] <= i) {
            ls->depth[at[in->label]]++;
            ls->depth[i + 1]--;
        }
    }
    for (i = 0; i < xf->nins; ++i) {
        d += ls->depth[i];
        ls->depth[i] = d;
    }
    free(at);
    return 0;
}

typedef struct {
    ls_t *ls;
    double f;
    int size;
} freq_ctx_t;

/** for_each_vreg() callback: count an access. */
static void note_freq(int r, int k, void *ctx) {
    freq_ctx_t *c = (freq_ctx_t *)ctx;

    (void)k;
    c->ls->iv[r - XR_VBASE].freq += c->f;
}

/**
 * Build every interval: the virtual registers' from the blocks' liveness, the physical registers' from the
 * instructions that name them (a physical register is never live from one block into another), and the
 * weights, hints and register masks.
 */
static int ls_build(ls_t *ls) {
    xfunc_t *xf = ls->xf;
    word_t *live = (word_t *)malloc(ls->nwords * sizeof(word_t));
    build_ctx_t c;
    int *copies;     /* the copies between virtual registers, as pairs */
    int ncopies = 0;
    int cap_copies;
    int b;
    int i;
    int k;

    ls->iv = (ival_t *)calloc((size_t)(xf->nvr > 0 ? xf->nvr : 1), sizeof(ival_t));
    ls->niv = xf->nvr;
    if (live == NULL || ls->iv == NULL || ls_depth(ls) != 0) {
        free(live);
        return -1;
    }
    memset(&c, 0, sizeof(c));
    c.ls = ls;
    c.live = live;
    for (b = ls->nblocks - 1; b >= 0; --b) {
        unsigned plive = 0;
        int r;
        size_t w;

        c.first = 2 * ls->bfirst[b];
        memcpy(live, ls->out + (size_t)b * ls->nwords, ls->nwords * sizeof(word_t));
        for (w = 0; w < ls->nwords; ++w) {
            word_t m = live[w];

            while (m != 0) {
                int bit = 0;

                while (((m >> bit) & 1u) == 0) {
                    bit++;
                }
                m &= ~((word_t)1u << bit);
                if (iv_add(&ls->iv[(int)(w * WORD_BITS) + bit], c.first, 2 * ls->blast[b] + 2) != 0) {
                    free(live);
                    return -1;
                }
            }
        }
        for (i = ls->blast[b]; i >= ls->bfirst[b]; --i) {
            const xinst_t *in = &xf->ins[i];
            unsigned pd = phys_defs(in, ls->t->alloc_mask);
            unsigned pu = phys_uses(in, ls->t->alloc_mask);

            c.pos = 2 * i;
            /* what it writes starts here */
            for (k = 0; k < in->nops; ++k) {
                const xop_t *o = &in->o[k];

                if (o->kind == XO_REG && XR_IS_VIRT(o->reg) && (xi_role(in, k) & 2) != 0) {
                    int v = o->reg - XR_VBASE;

                    if (bit_test(live, v)) {
                        iv_start_at(&ls->iv[v], 2 * i + 1);
                        bit_clear(live, v);
                    } else if (iv_add(&ls->iv[v], 2 * i + 1, 2 * i + 2) != 0) {
                        free(live);
                        return -1;
                    }
                }
            }
            for (r = 0; r < XR_NPHYS; ++r) {
                if ((pd & XR_BIT(r)) == 0) {
                    continue;
                }
                if ((plive & XR_BIT(r)) != 0) {
                    iv_start_at(&ls->fixed[r], 2 * i + 1);
                    plive &= ~XR_BIT(r);
                } else if (iv_add(&ls->fixed[r], 2 * i + 1, 2 * i + 2) != 0) {
                    free(live);
                    return -1;
                }
            }
            /* what it reads is live up to here */
            for_each_vreg(in, 1, build_use, &c);
            if (c.oom) {
                free(live);
                return -1;
            }
            for (r = 0; r < XR_NPHYS; ++r) {
                if ((pu & XR_BIT(r)) != 0 && (plive & XR_BIT(r)) == 0) {
                    plive |= XR_BIT(r);
                    if (iv_add(&ls->fixed[r], c.first, 2 * i + 1) != 0) {
                        free(live);
                        return -1;
                    }
                }
            }
        }
    }
    free(live);
    for (i = 0; i < xf->nvr; ++i) {
        iv_finish(&ls->iv[i]);
        ls->iv[i].phys = -1;
        ls->iv[i].hint_vr = XR_NONE;
        /* a vector register's interval takes only vector registers, a general one's only general ones */
        ls->iv[i].mask = xf->vr[i].xmm ? ls->t->alloc_mask & ls->t->xmm_mask : ls->t->alloc_mask & ~ls->t->xmm_mask;
    }
    for (i = 0; i < XR_NPHYS; ++i) {
        iv_finish(&ls->fixed[i]);
    }
    /* accesses, byte operands, and the copies that suggest a register */
    cap_copies = xf->nins;
    copies = (int *)malloc((size_t)(cap_copies > 0 ? cap_copies : 1) * 2 * sizeof(int));
    if (copies == NULL) {
        return -1;
    }
    for (i = 0; i < xf->nins; ++i) {
        const xinst_t *in = &xf->ins[i];
        freq_ctx_t fc;
        int d = ls->depth[i] < 4 ? ls->depth[i] : 4;

        fc.ls = ls;
        fc.f = 1.0;
        while (d-- > 0) {
            fc.f *= 10.0;
        }
        for_each_vreg(in, 1, note_freq, &fc);
        for_each_vreg(in, 2, note_freq, &fc);
        for (k = 0; k < in->nops; ++k) {
            const xop_t *o = &in->o[k];

            if (o->kind == XO_REG && XR_IS_VIRT(o->reg) && o->size == 1) {
                ls->iv[o->reg - XR_VBASE].mask &= ls->t->byte_mask;
            }
        }
        if (in->op == XI_MOV && in->o[0].kind == XO_REG && in->o[1].kind == XO_REG) {
            int s = in->o[0].reg;
            int dd = in->o[1].reg;

            if (XR_IS_VIRT(s) && XR_IS_VIRT(dd)) {
                if (ncopies < cap_copies) {
                    copies[2 * ncopies] = s - XR_VBASE;
                    copies[2 * ncopies + 1] = dd - XR_VBASE;
                    ncopies++;
                }
                if (ls->iv[dd - XR_VBASE].hint_vr == XR_NONE) {
                    ls->iv[dd - XR_VBASE].hint_vr = s;
                    ls->iv[dd - XR_VBASE].hint_vr_w = (float)fc.f;
                }
                if (ls->iv[s - XR_VBASE].hint_vr == XR_NONE) {
                    ls->iv[s - XR_VBASE].hint_vr = dd;
                    ls->iv[s - XR_VBASE].hint_vr_w = (float)fc.f;
                }
            } else if (XR_IS_VIRT(s) && dd >= 0 && dd < XR_NPHYS) {
                /* (each copy a vote, weighed as its accesses are: the one in a loop counts) */
                ls->iv[s - XR_VBASE].hint_w[dd] += (float)fc.f;
            } else if (XR_IS_VIRT(dd) && s >= 0 && s < XR_NPHYS) {
                ls->iv[dd - XR_VBASE].hint_w[s] += (float)fc.f;
            }
        }
    }
    {
        /*
         * and what a chain of copies suggests: a variable copied into a temporary that goes to an asm's register
         * or a call's argument wants that register too -- each round its own votes and half of its
         * neighbours'
         */
        float *next = (float *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * XR_NPHYS * sizeof(float));
        float *base = (float *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * XR_NPHYS * sizeof(float));
        int round;

        /* (each round from a register's own votes: what it passes on does not come back to it the stronger) */
        for (i = 0; next != NULL && base != NULL && i < xf->nvr; ++i) {
            memcpy(base + (size_t)i * XR_NPHYS, ls->iv[i].hint_w, sizeof(ls->iv[i].hint_w));
            memcpy(ls->iv[i].hint_own, ls->iv[i].hint_w, sizeof(ls->iv[i].hint_w));
        }
        for (round = 0; next != NULL && base != NULL && round < 3 && ncopies > 0; ++round) {
            memcpy(next, base, (size_t)xf->nvr * XR_NPHYS * sizeof(float));
            for (i = 0; i < ncopies; ++i) {
                int a = copies[2 * i];
                int d = copies[2 * i + 1];

                for (k = 0; k < XR_NPHYS; ++k) {
                    next[(size_t)a * XR_NPHYS + k] += 0.5f * ls->iv[d].hint_w[k];
                    next[(size_t)d * XR_NPHYS + k] += 0.5f * ls->iv[a].hint_w[k];
                }
            }
            for (i = 0; i < xf->nvr; ++i) {
                memcpy(ls->iv[i].hint_w, next + (size_t)i * XR_NPHYS, sizeof(ls->iv[i].hint_w));
            }
        }
        free(next);
        free(base);
    }
    free(copies);
    for (i = 0; i < xf->nvr; ++i) {
        ival_t *iv = &ls->iv[i];

        if (iv->n == 0) {
            continue;
        }
        if (xf->vr[i].no_spill) {
            iv->weight = 1e30;
        } else {
            int span = (iv->r[iv->n - 1].to - iv->r[0].from) / 2 + 1;

            iv->weight = iv->freq / (double)span;
        }
    }
    return 0;
}

/** Whether @p iv covers position @p pos, its cursor moved past what ends before it (positions only go up). */
static int iv_covers(ival_t *iv, int pos) {
    while (iv->cursor < iv->n && iv->r[iv->cursor].to <= pos) {
        iv->cursor++;
    }
    return iv->cursor < iv->n && iv->r[iv->cursor].from <= pos;
}

/** Whether intervals @p a and @p b meet, from their cursors on. */
static int iv_meet(const ival_t *a, const ival_t *b) {
    int i = a->cursor;
    int j = b->cursor;

    while (i < a->n && j < b->n) {
        if (a->r[i].to <= b->r[j].from) {
            i++;
        } else if (b->r[j].to <= a->r[i].from) {
            j++;
        } else {
            return 1;
        }
    }
    return 0;
}

/** Whether @p a meets fixed interval @p f: found from the first range of @p f that ends after @p a starts. */
static int iv_meet_fixed(const ival_t *a, const ival_t *f) {
    int lo = 0;
    int hi = f->n;
    int start = a->r[0].from;
    ival_t tmp;

    while (lo < hi) {
        int mid = (lo + hi) / 2;

        if (f->r[mid].to <= start) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    tmp = *f;
    tmp.cursor = lo;
    return iv_meet(a, &tmp);
}

/** qsort() order of virtual register numbers: their intervals' starts. */
static const ls_t *sort_ls;

static int by_start(const void *x, const void *y) {
    const ival_t *a = &sort_ls->iv[*(const int *)x];
    const ival_t *b = &sort_ls->iv[*(const int *)y];

    if (a->r[0].from != b->r[0].from) {
        return a->r[0].from < b->r[0].from ? -1 : 1;
    }
    return *(const int *)x - *(const int *)y;
}

/** Remove element @p i of @p list of @p *n, not keeping the order. */
static void list_drop(int *list, int *n, int i) {
    list[i] = list[--*n];
}

/**
 * The scan: give each interval a register or mark it spilled.
 *
 * @return how many were spilled; -2 when one that may not be spilled finds no register; -1 when out of memory
 */
static int ls_scan(ls_t *ls) {
    xfunc_t *xf = ls->xf;
    int *order = (int *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * sizeof(int));
    int *active = (int *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * sizeof(int));
    int *inactive = (int *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * sizeof(int));
    int nact = 0;
    int ninact = 0;
    int n = 0;
    int spills = 0;
    int i;

    if (order == NULL || active == NULL || inactive == NULL) {
        free(order);
        free(active);
        free(inactive);
        return -1;
    }
    for (i = 0; i < xf->nvr; ++i) {
        if (ls->iv[i].n > 0 && !xf->vr[i].memory) {
            order[n++] = i;
        }
    }
    sort_ls = ls;
    qsort(order, (size_t)n, sizeof(int), by_start);
    for (i = 0; i < n; ++i) {
        int v = order[i];
        ival_t *cur = &ls->iv[v];
        int pos = cur->r[0].from;
        unsigned blocked = 0; /* registers a fixed interval keeps from it */
        unsigned busy = 0;    /* and those an interval that meets it has */
        int pick = -1;
        int k;

        for (k = 0; k < nact;) {
            ival_t *a = &ls->iv[active[k]];

            if (a->r[a->n - 1].to <= pos) {
                list_drop(active, &nact, k);
            } else if (!iv_covers(a, pos)) {
                inactive[ninact++] = active[k];
                list_drop(active, &nact, k);
            } else {
                k++;
            }
        }
        for (k = 0; k < ninact;) {
            ival_t *a = &ls->iv[inactive[k]];

            if (a->r[a->n - 1].to <= pos) {
                list_drop(inactive, &ninact, k);
            } else if (iv_covers(a, pos)) {
                active[nact++] = inactive[k];
                list_drop(inactive, &ninact, k);
            } else {
                k++;
            }
        }
        for (k = 0; k < XR_NPHYS; ++k) {
            if ((cur->mask & XR_BIT(k)) != 0 && ls->fixed[k].n > 0 && iv_meet_fixed(cur, &ls->fixed[k])) {
                blocked |= XR_BIT(k);
            }
        }
        for (k = 0; k < nact; ++k) {
            busy |= XR_BIT(ls->iv[active[k]].phys);
        }
        for (k = 0; k < ninact; ++k) {
            const ival_t *a = &ls->iv[inactive[k]];

            if ((busy & XR_BIT(a->phys)) == 0 && iv_meet(cur, a)) {
                busy |= XR_BIT(a->phys);
            }
        }
        {
            unsigned free_mask = cur->mask & ~blocked & ~busy;
            int hint = -1;
            float best_w = 0.0f;
            int partner = -1;

            {
                /* the partner a chain of copies leads to that has a register already: r = (r + 4) through copies */
                int hv = cur->hint_vr;
                int hops;

                for (hops = 0; hops < 4 && hv != XR_NONE; ++hops) {
                    const ival_t *h = &ls->iv[hv - XR_VBASE];

                    if (h->phys >= 0 && !h->spilled) {
                        partner = h->phys;
                        break;
                    }
                    hv = h->hint_vr;
                }
            }
            /*
             * the free register copies suggest most -- a copy with a virtual register already given one counting
             * for that register as the copy runs, both ways, over what longer chains of copies pass on
             */
            for (k = 0; k < XR_NPHYS; ++k) {
                float wk = cur->hint_w[k] + (k == partner ? 2.0f * cur->hint_vr_w + 0.5f : 0.0f);

                if ((free_mask & XR_BIT(k)) != 0 && wk > best_w) {
                    best_w = wk;
                    hint = k;
                }
            }
            if (partner >= 0 && (free_mask & XR_BIT(partner)) != 0 && hint != partner) {
                /* the partner's register, free, unless a copy of its own asks more for another */
                int own_more = 0;

                for (k = 0; k < XR_NPHYS && !own_more; ++k) {
                    own_more = k != partner && (free_mask & XR_BIT(k)) != 0 &&
                               cur->hint_own[k] > 2.0f * cur->hint_vr_w + cur->hint_own[partner];
                }
                if (!own_more) {
                    hint = partner;
                }
            }
            if (hint >= 0 && (free_mask & XR_BIT(hint)) != 0) {
                pick = hint;
            }
            for (k = 0; pick < 0 && k < ls->t->norder; ++k) {
                if ((free_mask & XR_BIT(ls->t->order[k])) != 0) {
                    pick = ls->t->order[k];
                }
            }
        }
        if (pick < 0) {
            /* none free: the register whose intervals here weigh least, if they weigh less than this one */
            double best = 1e300;
            int r;

            for (k = 0; k < ls->t->norder; ++k) {
                double cost = 0.0;
                int j;

                r = ls->t->order[k];
                if ((cur->mask & XR_BIT(r)) == 0 || (blocked & XR_BIT(r)) != 0) {
                    continue;
                }
                for (j = 0; j < nact; ++j) {
                    if (ls->iv[active[j]].phys == r) {
                        cost += ls->iv[active[j]].weight;
                    }
                }
                for (j = 0; j < ninact; ++j) {
                    if (ls->iv[inactive[j]].phys == r && iv_meet(cur, &ls->iv[inactive[j]])) {
                        cost += ls->iv[inactive[j]].weight;
                    }
                }
                if (cost < best) {
                    best = cost;
                    pick = r;
                }
            }
            if (pick >= 0 && best < cur->weight && best < 1e29) {
                int j;

                for (j = 0; j < nact;) {
                    if (ls->iv[active[j]].phys == pick) {
                        ls->iv[active[j]].spilled = 1;
                        ls->iv[active[j]].phys = -1;
                        spills++;
                        list_drop(active, &nact, j);
                    } else {
                        j++;
                    }
                }
                for (j = 0; j < ninact;) {
                    if (ls->iv[inactive[j]].phys == pick && iv_meet(cur, &ls->iv[inactive[j]])) {
                        ls->iv[inactive[j]].spilled = 1;
                        ls->iv[inactive[j]].phys = -1;
                        spills++;
                        list_drop(inactive, &ninact, j);
                    } else {
                        j++;
                    }
                }
            } else {
                if (xf->vr[v].no_spill) {
                    free(order);
                    free(active);
                    free(inactive);
                    return -2;
                }
                cur->spilled = 1;
                spills++;
                continue;
            }
        }
        cur->phys = pick;
        active[nact++] = v;
    }
    free(order);
    free(active);
    free(inactive);
    return spills;
}

/** The slot a spilled virtual register lives in: an object and the offset in it. */
typedef struct {
    int obj;
    int off;
} slot_t;

/** The bytes a spill slot of virtual register @p r takes: a word, or a double's eight for a vector register. */
static int slot_bytes(const ls_t *ls, int r) {
    return ls->xf->vr[r - XR_VBASE].xmm ? 8 : ls->t->word;
}

/**
 * Give each spilled register a slot: its home if it has one, else a slot of its size (slot_bytes()) shared with
 * others of that size whose intervals do not meet it.
 */
static int ls_assign_slots(ls_t *ls, slot_t *slot) {
    xfunc_t *xf = ls->xf;
    int *list = (int *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * sizeof(int));
    int *slot_obj = (int *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * sizeof(int));
    int *slot_end = (int *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * sizeof(int));
    int *slot_size = (int *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * sizeof(int));
    int nslots = 0;
    int n = 0;
    int i;

    if (list == NULL || slot_obj == NULL || slot_end == NULL || slot_size == NULL) {
        free(slot_size);
        free(list);
        free(slot_obj);
        free(slot_end);
        return -1;
    }
    for (i = 0; i < xf->nvr; ++i) {
        slot[i].obj = -1;
        slot[i].off = 0;
        if (!(ls->iv[i].spilled || xf->vr[i].memory)) {
            continue;
        }
        if (xf->vr[i].home_obj >= 0) {
            slot[i].obj = xf->vr[i].home_obj;
            slot[i].off = xf->vr[i].home_off;
        } else if (ls->iv[i].n > 0) {
            list[n++] = i;
        }
    }
    sort_ls = ls;
    qsort(list, (size_t)n, sizeof(int), by_start);
    for (i = 0; i < n; ++i) {
        const ival_t *iv = &ls->iv[list[i]];
        int s;
        int pick = -1;

        int bytes = slot_bytes(ls, list[i] + XR_VBASE);

        for (s = 0; s < nslots && pick < 0; ++s) {
            if (slot_end[s] <= iv->r[0].from && slot_size[s] == bytes) {
                pick = s;
            }
        }
        if (pick < 0) {
            slot_obj[nslots] = xf_new_obj(xf, XF_SPILL, bytes, bytes, 0);
            if (slot_obj[nslots] < 0) {
                free(list);
                free(slot_obj);
                free(slot_end);
                free(slot_size);
                return -1;
            }
            slot_size[nslots] = bytes;
            pick = nslots++;
        }
        slot_end[pick] = iv->r[iv->n - 1].to;
        slot[list[i]].obj = slot_obj[pick];
    }
    free(list);
    free(slot_obj);
    free(slot_end);
    free(slot_size);
    return 0;
}

/** Whether operand @p o is memory at slot @p s. */
static int op_is_slot(const xop_t *o, const slot_t *s) {
    return o->kind == XO_MEM && o->base == XR_FRAME && o->obj == s->obj && o->disp == s->off && o->sym == NULL;
}

/** Whether instruction @p in reads register @p r: an operand it reads, or the base or index of an address. */
static int reads_reg(const xinst_t *in, int r) {
    int q;

    for (q = 0; q < in->nops; ++q) {
        const xop_t *p = &in->o[q];

        if ((p->kind == XO_REG && p->reg == r && (xi_role(in, q) & 1) != 0) ||
            (p->kind == XO_MEM && (p->base == r || p->index == r))) {
            return 1;
        }
    }
    return 0;
}

/** What count_ref() counts in: per register, the instructions that reference it, and the last one that did. */
typedef struct {
    int *count;
    int *seen;
    int at;
    int nvr;
} ref_count_t;

/** for_each_vreg() callback: count an instruction referencing a register, once, for ls_rewrite()'s blocks. */
static void count_ref(int r, int k, void *ctx) {
    ref_count_t *c = (ref_count_t *)ctx;
    int v = r - XR_VBASE;

    (void)k;
    if (v < c->nvr && c->seen[v] != c->at) {
        c->seen[v] = c->at;
        c->count[v]++;
    }
}

/**
 * Rewrite every operand of a spilled register.  Where it is read or written more than once before the next label,
 * those share one new register (split): loaded from the slot at the first read, stored to it after each write, so
 * the slot holds the value at every label -- a register the next round may give it, as a short interval, where the
 * whole was spilled.  Elsewhere, and for one of those spilled in turn, each operand is the slot itself where the
 * instruction can take memory there, else a new register loaded before and stored after.
 *
 * @return 0, or -1 when out of memory
 */
static int ls_rewrite(ls_t *ls, const slot_t *slot) {
    xfunc_t *xf = ls->xf;
    xinst_t *old = xf->ins;
    int nold = xf->nins;
    int nvr = xf->nvr;
    int *count = (int *)calloc((size_t)(nvr > 0 ? nvr : 1), sizeof(int)); /* per register: references this block */
    int *local = (int *)malloc((size_t)(nvr > 0 ? nvr : 1) * sizeof(int)); /* and the register sharing them */
    int *seen = (int *)malloc((size_t)(nvr > 0 ? nvr : 1) * sizeof(int));
    ref_count_t rc;
    int i;

    if (count == NULL || local == NULL || seen == NULL) {
        free(count);
        free(local);
        free(seen);
        return -1;
    }
    for (i = 0; i < nvr; ++i) {
        local[i] = XR_NONE;
        seen[i] = -1;
    }
    rc.count = count;
    rc.seen = seen;
    rc.nvr = nvr;
    xf->ins = NULL;
    xf->nins = 0;
    xf->cap = 0;
    for (i = 0; i < nold; ++i) {
        xinst_t in = old[i];
        xinst_t after[6];
        int nafter = 0;
        int tmp_of[6];   /* the spilled registers given a temporary here, and the temporaries */
        int tmp_reg[6];
        int ntmp = 0;
        int k;

#define SPILLED(r) (XR_IS_VIRT(r) && (r) - XR_VBASE < nvr && slot[(r) - XR_VBASE].obj >= 0)
#define SHARED(r) (SPILLED(r) && !xf->vr[(r) - XR_VBASE].split && count[(r) - XR_VBASE] >= 2)
        if (i == 0 || in.op == XI_LABEL) {
            /* a block begins: its references counted, up to the next label */
            int q;

            for (q = 0; q < nvr; ++q) {
                count[q] = 0;
                local[q] = XR_NONE;
            }
            for (q = in.op == XI_LABEL ? i + 1 : i; q < nold && old[q].op != XI_LABEL; ++q) {
                rc.at = q;
                for_each_vreg(&old[q], 1, count_ref, &rc);
                for_each_vreg(&old[q], 2, count_ref, &rc);
            }
            for (q = 0; q < nvr; ++q) {
                if (count[q] > 0 && !SPILLED(q + XR_VBASE)) {
                    count[q] = 0;
                }
            }
        }
        /* a load of a parameter's incoming slot into the register that lives there: nothing to do */
        if (in.op == XI_MOV && in.o[1].kind == XO_REG && SPILLED(in.o[1].reg) &&
            op_is_slot(&in.o[0], &slot[in.o[1].reg - XR_VBASE])) {
            continue;
        }
        /* and a store of one to its own slot (a split register's, spilled in turn) */
        if (in.op == XI_MOV && in.o[0].kind == XO_REG && SPILLED(in.o[0].reg) &&
            op_is_slot(&in.o[1], &slot[in.o[0].reg - XR_VBASE])) {
            continue;
        }
        /* test r, r of a spilled one: compare its slot with 0 */
        if (in.op == XI_TEST && in.o[0].kind == XO_REG && in.o[1].kind == XO_REG && in.o[0].reg == in.o[1].reg &&
            SPILLED(in.o[0].reg) && !SHARED(in.o[0].reg) && !xf->vr[in.o[0].reg - XR_VBASE].xmm) {
            const slot_t *s = &slot[in.o[0].reg - XR_VBASE];

            in.op = XI_CMP;
            in.o[1] = xop_frame(s->obj, s->off, in.o[0].size);
            in.o[0] = xop_imm(0, in.o[1].size);
            xf->obj[s->obj].uses++;
            if (xf_emit(xf, &in) == NULL) {
                free(old);
                free(count);
                free(local);
                free(seen);
                return -1;
            }
            continue;
        }
        for (k = 0; k < in.nops; ++k) {
            xop_t *o = &in.o[k];

            if (o->kind == XO_REG && SPILLED(o->reg) && !SHARED(o->reg) && xi_mem_ok(&in, k)) {
                const slot_t *s = &slot[o->reg - XR_VBASE];
                int size = o->size;

                *o = xop_frame(s->obj, s->off, size);
                xf->obj[s->obj].uses++;
            }
        }
        for (k = 0; k < in.nops; ++k) {
            xop_t *o = &in.o[k];
            int *regs[2];
            int nr = 0;
            int role = xi_role(&in, k);
            int j;

            if (o->kind == XO_REG && SPILLED(o->reg)) {
                regs[nr++] = &o->reg;
            } else if (o->kind == XO_MEM) {
                if (SPILLED(o->base)) {
                    regs[nr++] = &o->base;
                }
                if (SPILLED(o->index)) {
                    regs[nr++] = &o->index;
                }
                role = 1;
            }
            for (j = 0; j < nr; ++j) {
                int r = *regs[j];
                const slot_t *s = &slot[r - XR_VBASE];
                int t = -1;
                int m;

                for (m = 0; m < ntmp; ++m) {
                    if (tmp_of[m] == r) {
                        t = tmp_reg[m];
                    }
                }
                int bytes = slot_bytes(ls, r);

                if (t < 0 && SHARED(r)) {
                    /* the block's register for it, made at its first reference here */
                    t = local[r - XR_VBASE];
                    if (t == XR_NONE) {
                        t = xf->vr[r - XR_VBASE].xmm ? xf_new_xvreg(xf) : xf_new_vreg(xf);
                        if (t == XR_NONE || ntmp >= 6) {
                            free(old);
                            free(count);
                            free(local);
                            free(seen);
                            return -1;
                        }
                        xf_vreg(xf, t)->split = 1;
                        xf_vreg(xf, t)->home_obj = s->obj;
                        xf_vreg(xf, t)->home_off = s->off;
                        local[r - XR_VBASE] = t;
                        if (reads_reg(&in, r)) {
                            xop_t from = xop_frame(s->obj, s->off, bytes);
                            xop_t to = xop_reg(t, bytes);
                            xinst_t ld = xi_make(XI_MOV, bytes, 2, &from, &to, NULL);

                            xf->obj[s->obj].uses++;
                            if (xf_emit(xf, &ld) == NULL) {
                                free(old);
                                free(count);
                                free(local);
                                free(seen);
                                return -1;
                            }
                        }
                    }
                    tmp_of[ntmp] = r;
                    tmp_reg[ntmp++] = t;
                }
                if (t < 0) {
                    xop_t from;
                    xop_t to;

                    t = xf->vr[r - XR_VBASE].xmm ? xf_new_xvreg(xf) : xf_new_vreg(xf);
                    if (t == XR_NONE || ntmp >= 6) {
                        free(old);
                        free(count);
                        free(local);
                        free(seen);
                        return -1;
                    }
                    xf_vreg(xf, t)->no_spill = 1;
                    tmp_of[ntmp] = r;
                    tmp_reg[ntmp++] = t;
                    from = xop_frame(s->obj, s->off, bytes);
                    to = xop_reg(t, bytes);
                    if (reads_reg(&in, r)) {
                        /* loaded before if this instruction reads it anywhere */
                        xinst_t ld = xi_make(XI_MOV, bytes, 2, &from, &to, NULL);

                        xf->obj[s->obj].uses++;
                        if (xf_emit(xf, &ld) == NULL) {
                            free(old);
                            free(count);
                            free(local);
                            free(seen);
                            return -1;
                        }
                    }
                }
                *regs[j] = t;
                if ((role & 2) != 0 && nafter < 6) {
                    xop_t from = xop_reg(t, bytes);
                    xop_t to = xop_frame(s->obj, s->off, bytes);

                    after[nafter++] = xi_make(XI_MOV, bytes, 2, &from, &to, NULL);
                    xf->obj[s->obj].uses++;
                }
            }
        }
#undef SHARED
#undef SPILLED
        if (xf_emit(xf, &in) == NULL) {
            free(old);
            free(count);
            free(local);
            free(seen);
            return -1;
        }
        for (k = 0; k < nafter; ++k) {
            if (xf_emit(xf, &after[k]) == NULL) {
                free(old);
                free(count);
                free(local);
                free(seen);
                return -1;
            }
        }
    }
    free(old);
    free(count);
    free(local);
    free(seen);
    return 0;
}

/** Per virtual register, where it is read and written, for ls_coalesce(). */
typedef struct {
    int ndef;
    int nuse;
    int block;      /**< the block all its reads and writes are in; -1 for none yet, -2 for more than one */
    int first_def;  /**< instruction indices */
    int first_use;
    int last_use;
} occ_t;

typedef struct {
    occ_t *occ;
    const ls_t *ls;
    int at;
    int write;
} occ_ctx_t;

/** for_each_vreg() callback: note a read or a write at the instruction. */
static void note_occ(int r, int k, void *ctx) {
    occ_ctx_t *c = (occ_ctx_t *)ctx;
    occ_t *o = &c->occ[r - XR_VBASE];
    int b = c->ls->block_of[c->at];

    (void)k;
    if (o->block == -1) {
        o->block = b;
    } else if (o->block != b) {
        o->block = -2;
    }
    if (c->write) {
        if (o->ndef++ == 0) {
            o->first_def = c->at;
        }
    } else {
        if (o->nuse++ == 0) {
            o->first_use = c->at;
        }
        o->last_use = c->at;
    }
}

/** Whether instruction @p in reads or writes virtual register @p r, as @p role says (1 read, 2 write). */
static int touches(const xinst_t *in, int r, int role) {
    int k;

    for (k = 0; k < in->nops; ++k) {
        const xop_t *o = &in->o[k];

        if (o->kind == XO_REG && o->reg == r && (xi_role(in, k) & role) != 0) {
            return 1;
        }
        if (o->kind == XO_MEM && (role & 1) != 0 && (o->base == r || o->index == r)) {
            return 1;
        }
    }
    return 0;
}

/** Replace virtual register @p from with @p to in every operand of @p in. */
static void rename_in(xinst_t *in, int from, int to) {
    int k;

    for (k = 0; k < in->nops; ++k) {
        xop_t *o = &in->o[k];

        if (o->kind == XO_REG && o->reg == from) {
            o->reg = to;
        } else if (o->kind == XO_MEM) {
            if (o->base == from) {
                o->base = to;
            }
            if (o->index == from) {
                o->index = to;
            }
        }
    }
}

/**
 * Whether @p in copies a whole virtual register into another of its class: a word between general registers, a
 * double between vector ones.
 */
static int same_class_copy(const ls_t *ls, const xinst_t *in) {
    int a;
    int b;

    if (in->op != XI_MOV || in->o[0].kind != XO_REG || in->o[1].kind != XO_REG || !XR_IS_VIRT(in->o[0].reg) ||
        !XR_IS_VIRT(in->o[1].reg)) {
        return 0;
    }
    a = ls->xf->vr[in->o[0].reg - XR_VBASE].xmm;
    b = ls->xf->vr[in->o[1].reg - XR_VBASE].xmm;
    return a == b && in->w == (a ? 8 : ls->t->word);
}

/**
 * After a copy `mov s, d` in a block, read d where s is read, until either changes: s is then read less, often
 * only by the copy, which ls_coalesce() can take away.
 *
 * @return how many reads were changed
 */
static int ls_forward(ls_t *ls) {
    xfunc_t *xf = ls->xf;
    int changed = 0;
    int i;

    for (i = 0; i < xf->nins; ++i) {
        const xinst_t *in = &xf->ins[i];
        int s;
        int d;
        int j;

        if (!same_class_copy(ls, in) || in->o[0].reg == in->o[1].reg || xf->vr[in->o[0].reg - XR_VBASE].memory ||
            xf->vr[in->o[1].reg - XR_VBASE].memory) {
            continue;
        }
        s = in->o[0].reg;
        d = in->o[1].reg;
        for (j = i + 1; j < xf->nins && ls->block_of[j] == ls->block_of[i]; ++j) {
            xinst_t *nx = &xf->ins[j];
            int k;

            if (touches(nx, s, 2)) {
                break;
            }
            for (k = 0; k < nx->nops; ++k) {
                xop_t *o = &nx->o[k];

                if (o->kind == XO_REG && o->reg == s && xi_role(nx, k) == 1) {
                    o->reg = d;
                    changed++;
                } else if (o->kind == XO_MEM) {
                    if (o->base == s) {
                        o->base = d;
                        changed++;
                    }
                    if (o->index == s) {
                        o->index = d;
                        changed++;
                    }
                }
            }
            if (touches(nx, d, 2)) {
                break;
            }
        }
    }
    return changed;
}

/**
 * Coalesce copies within a block: a copy `mov s, d` goes when d is written only there and read only in the block
 * after it, while s does not change -- d's reads read s -- or when s is read only there and written only in the
 * block before it, while nothing else reads or writes d -- s's writes write d.  Liveness must be current.
 *
 * @return how many copies went, or -1 when out of memory
 */
static int ls_coalesce(ls_t *ls) {
    xfunc_t *xf = ls->xf;
    occ_t *occ = (occ_t *)calloc((size_t)(xf->nvr > 0 ? xf->nvr : 1), sizeof(*occ));
    unsigned char *dirty = (unsigned char *)calloc((size_t)(xf->nvr > 0 ? xf->nvr : 1), 1);
    unsigned char *gone = (unsigned char *)calloc((size_t)(xf->nins > 0 ? xf->nins : 1), 1);
    occ_ctx_t c;
    int merged = 0;
    int i;
    int j;

    if (occ == NULL || dirty == NULL || gone == NULL) {
        free(occ);
        free(dirty);
        free(gone);
        return -1;
    }
    for (i = 0; i < xf->nvr; ++i) {
        occ[i].block = -1;
    }
    c.occ = occ;
    c.ls = ls;
    for (i = 0; i < xf->nins; ++i) {
        c.at = i;
        c.write = 0;
        for_each_vreg(&xf->ins[i], 1, note_occ, &c);
        c.write = 1;
        for_each_vreg(&xf->ins[i], 2, note_occ, &c);
    }
    for (i = 0; i < xf->nins; ++i) {
        const xinst_t *in = &xf->ins[i];
        int s;
        int d;
        int b = ls->block_of[i];
        const word_t *out = ls->out + (size_t)b * ls->nwords;
        const occ_t *os;
        const occ_t *od;

        if (!same_class_copy(ls, in) || in->o[0].reg == in->o[1].reg) {
            continue;
        }
        s = in->o[0].reg;
        d = in->o[1].reg;
        os = &occ[s - XR_VBASE];
        od = &occ[d - XR_VBASE];
        if (dirty[s - XR_VBASE] || dirty[d - XR_VBASE] || xf->vr[s - XR_VBASE].memory ||
            xf->vr[d - XR_VBASE].memory) {
            continue;
        }
        /* d's reads read s */
        if (od->ndef == 1 && od->block == b && od->nuse > 0 && od->first_use > i &&
            !bit_test(out, d - XR_VBASE)) {
            int ok = 1;

            for (j = i + 1; j <= od->last_use && ok; ++j) {
                ok = !touches(&xf->ins[j], s, 2);
            }
            if (ok) {
                for (j = i + 1; j <= od->last_use; ++j) {
                    rename_in(&xf->ins[j], d, s);
                }
                gone[i] = 1;
                dirty[s - XR_VBASE] = dirty[d - XR_VBASE] = 1;
                merged++;
                continue;
            }
        }
        /* s's writes write d */
        if (os->nuse == 1 && os->block == b && os->ndef > 0 && os->first_def < i && !bit_test(out, s - XR_VBASE) &&
            xf->vr[s - XR_VBASE].home_obj < 0) {
            int ok = 1;

            for (j = os->first_def; j < i && ok; ++j) {
                ok = !touches(&xf->ins[j], d, 3);
            }
            if (ok) {
                for (j = os->first_def; j < i; ++j) {
                    rename_in(&xf->ins[j], s, d);
                }
                gone[i] = 1;
                dirty[s - XR_VBASE] = dirty[d - XR_VBASE] = 1;
                merged++;
            }
        }
    }
    for (i = 0, j = 0; i < xf->nins; ++i) {
        if (!gone[i]) {
            xf->ins[j++] = xf->ins[i];
        }
    }
    xf->nins = j;
    free(occ);
    free(dirty);
    free(gone);
    return merged;
}

/**
 * Drop each store to a spill slot nothing reads, once registers are physical: a register shared through a region or
 * a block (ls_rewrite()) is stored after each write, so that a later reference may reload it, and where every one
 * of them was given the register the slot is written for nothing.
 */
static void drop_unread_spill_stores(xfunc_t *xf) {
    unsigned char *read = (unsigned char *)calloc((size_t)(xf->nobj > 0 ? xf->nobj : 1), 1);
    int i;
    int j = 0;

    if (read == NULL) {
        return;
    }
    for (i = 0; i < xf->nins; ++i) {
        const xinst_t *in = &xf->ins[i];
        int k;

        for (k = 0; k < in->nops; ++k) {
            const xop_t *o = &in->o[k];

            if (o->kind == XO_MEM && o->base == XR_FRAME && o->obj >= 0 && o->obj < xf->nobj &&
                xf->obj[o->obj].kind == XF_SPILL && !(in->op == XI_MOV && k == 1)) {
                read[o->obj] = 1; /* (any use but a plain store's) */
            }
        }
    }
    for (i = 0; i < xf->nins; ++i) {
        const xinst_t *in = &xf->ins[i];
        const xop_t *d = &in->o[1];

        if (in->op == XI_MOV && in->nops == 2 && d->kind == XO_MEM && d->base == XR_FRAME && d->obj >= 0 &&
            d->obj < xf->nobj && xf->obj[d->obj].kind == XF_SPILL && !read[d->obj]) {
            xf->obj[d->obj].uses--;
            continue;
        }
        xf->ins[j++] = *in;
    }
    xf->nins = j;
    free(read);
}

/** Whether operand @p o is a whole spill slot of @p xf: a frame object of kind XF_SPILL at its start. */
static int spill_slot_of(const xfunc_t *xf, const xop_t *o) {
    return o->kind == XO_MEM && o->base == XR_FRAME && o->index == XR_NONE && o->sym == NULL && o->obj >= 0 &&
                   o->obj < xf->nobj && xf->obj[o->obj].kind == XF_SPILL && o->disp == 0 &&
                   o->size == xf->obj[o->obj].size
               ? o->obj
               : -1;
}

/**
 * Within each block, read a spill slot from the register that still holds what it was last stored from or
 * loaded into: the reload becomes a copy, or goes when it is into that register.  Linear scan spills a value
 * for the whole of its life, and each read of it was a load, though the value was in a register an instruction
 * before.  Nothing but a spill slot's own loads and stores reaches it (its address is never taken), so a slot
 * stays as it was until an instruction names it as written, and a register until one writes or clobbers it.
 * Runs on physical registers, after allocation.
 */
static void forward_reloads(xfunc_t *xf) {
    int *holder = (int *)malloc((size_t)(xf->nobj > 0 ? xf->nobj : 1) * sizeof(int));
    int i;
    int j = 0;

    if (holder == NULL) {
        return;
    }
    for (i = 0; i < xf->nobj; ++i) {
        holder[i] = -1;
    }
    for (i = 0; i < xf->nins; ++i) {
        xinst_t *in = &xf->ins[i];
        unsigned defs;
        int k;
        int o;

        if (in->op == XI_LABEL || in->op == XI_ASM) {
            /* another way in; and an asm goto may leave by a way of its own */
            for (o = 0; o < xf->nobj; ++o) {
                holder[o] = -1;
            }
            xf->ins[j++] = *in;
            continue;
        }
        if (in->op == XI_MOV && in->o[1].kind == XO_REG && in->o[1].reg >= 0 && in->o[1].reg < XR_NPHYS &&
            (o = spill_slot_of(xf, &in->o[0])) >= 0 && holder[o] >= 0 &&
            XR_IS_XMM(holder[o]) == XR_IS_XMM(in->o[1].reg)) {
            /* a reload of what a register holds */
            int h = holder[o];

            xf->obj[o].uses--;
            if (h == in->o[1].reg) {
                continue;
            }
            in->o[0] = xop_reg(h, in->o[1].size);
        }
        defs = phys_defs(in, ~0u);
        for (k = 0; k < in->nops; ++k) {
            if (in->o[k].kind == XO_MEM && in->o[k].base == XR_FRAME && (xi_role(in, k) & 2) != 0 &&
                in->o[k].obj >= 0 && in->o[k].obj < xf->nobj) {
                holder[in->o[k].obj] = -1;
            }
        }
        if (defs != 0) {
            for (o = 0; o < xf->nobj; ++o) {
                if (holder[o] >= 0 && (defs & XR_BIT(holder[o])) != 0) {
                    holder[o] = -1;
                }
            }
        }
        if (in->op == XI_MOV && in->o[0].kind == XO_REG && in->o[0].reg >= 0 && in->o[0].reg < XR_NPHYS &&
            (o = spill_slot_of(xf, &in->o[1])) >= 0 && in->o[0].size == in->o[1].size) {
            holder[o] = in->o[0].reg; /* stored from it */
        } else if (in->op == XI_MOV && in->o[1].kind == XO_REG && in->o[1].reg >= 0 && in->o[1].reg < XR_NPHYS &&
                   (o = spill_slot_of(xf, &in->o[0])) >= 0 && in->o[0].size == in->o[1].size) {
            holder[o] = in->o[1].reg; /* loaded into it */
        }
        if (in->op == XI_JMP || in->op == XI_RET || in->op == XI_TRAP) {
            for (o = 0; o < xf->nobj; ++o) {
                holder[o] = -1;
            }
        }
        xf->ins[j++] = *in;
    }
    xf->nins = j;
    free(holder);
}

/**
 * Within each block, drop a copy between two registers that hold the same value already: one was copied from the
 * other, whole, and neither has been written since.  A parameter moved into a callee-saved register and moved
 * back for a call (mov %rdi, %rbx ... mov %rbx, %rdi) was a copy of what %rdi held.  Runs on physical registers,
 * after allocation: each register has a number for the value it holds, a new one where it is written, at a label
 * every one.
 */
static void drop_same_copies(xfunc_t *xf, int word) {
    unsigned val[XR_NPHYS];
    unsigned next = 1;
    int i;
    int j = 0;
    int r;

    for (r = 0; r < XR_NPHYS; ++r) {
        val[r] = next++;
    }
    for (i = 0; i < xf->nins; ++i) {
        xinst_t *in = &xf->ins[i];
        unsigned defs;
        int k;

        if (in->op == XI_LABEL || in->op == XI_ASM || in->op == XI_JMP || in->op == XI_RET) {
            for (r = 0; r < XR_NPHYS; ++r) {
                val[r] = next++;
            }
            xf->ins[j++] = *in;
            continue;
        }
        if (in->op == XI_MOV && in->nops == 2 && in->o[0].kind == XO_REG && in->o[1].kind == XO_REG &&
            in->o[0].reg >= 0 && in->o[0].reg < XR_NPHYS && in->o[1].reg >= 0 && in->o[1].reg < XR_NPHYS &&
            !XR_IS_XMM(in->o[0].reg) && !XR_IS_XMM(in->o[1].reg) && in->w == word && in->o[0].size == word &&
            in->o[1].size == word) {
            if (val[in->o[0].reg] == val[in->o[1].reg]) {
                continue; /* the same value already */
            }
            val[in->o[1].reg] = val[in->o[0].reg];
            xf->ins[j++] = *in;
            continue;
        }
        defs = phys_defs(in, ~0u);
        for (k = 0; k < in->nops; ++k) {
            /* (a register written in part is a new value too) */
            if (in->o[k].kind == XO_REG && in->o[k].reg >= 0 && in->o[k].reg < XR_NPHYS && (xi_role(in, k) & 2) != 0) {
                defs |= XR_BIT(in->o[k].reg);
            }
        }
        for (r = 0; r < XR_NPHYS; ++r) {
            if ((defs & XR_BIT(r)) != 0) {
                val[r] = next++;
            }
        }
        xf->ins[j++] = *in;
    }
    xf->nins = j;
}

int x86_mir_allocate(xfunc_t *xf, const xtarget_t *t) {
    ls_t ls;
    int round;
    int i;

    if (xf->oom) {
        return -1;
    }
    memset(&ls, 0, sizeof(ls));
    ls.xf = xf;
    ls.t = t;
    /* what nothing reads and the copies that can go, until no more do */
    for (round = 0; round < 8; ++round) {
        int rc = ls_liveness(&ls);
        int dropped;
        int fwd;

        if (rc != 0) {
            ls_free(&ls);
            return rc;
        }
        dropped = ls_dce(&ls);
        ls_free(&ls);
        if (dropped < 0) {
            return -1;
        }
        /* and the copies that need not be made */
        rc = ls_liveness(&ls);
        if (rc != 0) {
            ls_free(&ls);
            return rc;
        }
        fwd = ls_forward(&ls);
        rc = ls_coalesce(&ls);
        ls_free(&ls);
        if (rc < 0) {
            return -1;
        }
        rc += fwd;
        if (dropped == 0 && rc == 0) {
            break;
        }
    }
    for (round = 0; round < LS_MAX_ROUNDS; ++round) {
        int rc = ls_liveness(&ls);
        int spills;
        slot_t *slot;
        int any_memory = 0;
        int nslot = xf->nvr;

        if (rc == 0) {
            rc = ls_build(&ls);
        }
        if (rc != 0) {
            ls_free(&ls);
            return rc;
        }
        spills = ls_scan(&ls);
        if (spills == -2) {
            ls_free(&ls);
            return 1;
        }
        if (spills < 0) {
            ls_free(&ls);
            return -1;
        }
        for (i = 0; i < xf->nvr && !any_memory; ++i) {
            any_memory = xf->vr[i].memory && ls.iv[i].n > 0;
        }
        if (spills == 0 && !any_memory) {
            /* done: every register operand physical, and a copy of one to itself gone */
            int j = 0;

            for (i = 0; i < xf->nvr; ++i) {
                xf->vr[i].phys = ls.iv[i].phys;
            }
            for (i = 0; i < xf->nins; ++i) {
                xinst_t *in = &xf->ins[i];
                int k;

                for (k = 0; k < in->nops; ++k) {
                    xop_t *o = &in->o[k];

                    if (o->kind == XO_REG && XR_IS_VIRT(o->reg)) {
                        o->reg = ls.iv[o->reg - XR_VBASE].phys;
                    } else if (o->kind == XO_MEM) {
                        if (XR_IS_VIRT(o->base)) {
                            o->base = ls.iv[o->base - XR_VBASE].phys;
                        }
                        if (XR_IS_VIRT(o->index)) {
                            o->index = ls.iv[o->index - XR_VBASE].phys;
                        }
                    }
                }
                if (in->op == XI_MOV && in->o[0].kind == XO_REG && in->o[1].kind == XO_REG &&
                    in->o[0].reg == in->o[1].reg) {
                    continue;
                }
                xf->ins[j++] = *in;
            }
            xf->nins = j;
            forward_reloads(xf);
            drop_same_copies(xf, t->word);
            drop_unread_spill_stores(xf);
            ls_free(&ls);
            return 0;
        }
        slot = (slot_t *)malloc((size_t)(xf->nvr > 0 ? xf->nvr : 1) * sizeof(*slot));
        if (slot == NULL || ls_assign_slots(&ls, slot) != 0 || ls_rewrite(&ls, slot) != 0) {
            free(slot);
            ls_free(&ls);
            return -1;
        }
        /* a spilled register is gone from the instructions: what lives in memory is done with too */
        for (i = 0; i < nslot; ++i) {
            if (slot[i].obj >= 0) {
                xf->vr[i].memory = 0;
            }
        }
        free(slot);
        ls_free(&ls);
    }
    return 1;
}
