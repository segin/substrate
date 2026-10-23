/*
 * mem.c: what the optimizer learns about a function's own stack slots.
 *
 * A local is a stack slot (STACKALLOC), and a value kept in one is out of
 * the folder's sight.  When a slot's address does not escape -- it is used
 * only as the address of loads and stores, directly or through moves and
 * constant offsets -- three things are safe here:
 *
 *  - a slot stored once is read as the value stored, by every load after
 *    the store in the stream (a load before it reads an uninitialized
 *    local), and a slot nothing loads loses its stores
 *    (forward_slot_copies());
 *  - a structure slot whose fields are only read and written whole is
 *    split into one slot per field, and its zeroing memset into a store of
 *    0 to each (scalarize_slots());
 *  - a scalar slot only ever stored constants holds, at each point, one of
 *    a set of them, which a branch on a compare of it narrows; a compare
 *    against a constant the set rules in or out folds (slot_value_sets()).
 *
 * gcc does as much (SRA, then value-range propagation across the loop):
 * Linux's scoped_seqlock_read() keeps its state in an on-stack struct
 * ss_tmp across a loop, and the call of __scoped_seqlock_bug(), an
 * undefined function, in the state that cannot reach it must be proven
 * dead for vmlinux to link.
 */

#include "cc_middle.h"
#include "cc_options.h"
#include "cc_ssa.h"
#include "cc_target.h"
#include "cc_util.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** The most constants a slot's value set tells apart (bits of a mask; all 32 set means "not known"). */
#define SET_MAX 31
/** The most slot x block states slot_value_sets() keeps; past it, it does nothing. */
#define SET_MAX_STATES (4L * 1024 * 1024)

/** Whether @p v names a value of @p f. */
static int is_value(const cc_ssa_function_t *f, int v) {
    return v >= 0 && v < f->value_count;
}

/** How many times each value is defined (by an instruction's dst or an asm output); free it. */
static int *count_defs(const cc_ssa_function_t *f) {
    int *defs = (int *)calloc((size_t)(f->value_count > 0 ? f->value_count : 1), sizeof(*defs));
    size_t i;
    size_t k;

    if (defs == NULL) {
        return NULL;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (is_value(f, in->dst)) {
            defs[in->dst]++;
        }
        for (k = 0; in->op == CC_SSA_ASM && k < in->asm_out_count; ++k) {
            int v = in->asm_out_values[k];

            if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                v = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
            }
            if (is_value(f, v)) {
                defs[v] += 2;
            }
        }
    }
    return defs;
}

/** Each value's defining instruction when it has one definition, else -1; free it. */
static int *find_def_at(const cc_ssa_function_t *f, const int *defs) {
    int *at = (int *)malloc((size_t)(f->value_count > 0 ? f->value_count : 1) * sizeof(*at));
    size_t i;
    int v;

    if (at == NULL) {
        return NULL;
    }
    for (v = 0; v < f->value_count; ++v) {
        at[v] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        int d = f->instrs[i].dst;

        if (is_value(f, d) && defs[d] == 1) {
            at[d] = (int)i;
        }
    }
    return at;
}

/** The constant value @p v always holds: its one definition is an integer CONST (*@p out), else 0. */
static int single_const(const cc_ssa_function_t *f, const int *defs, const int *def_at, int v, long long *out) {
    if (!is_value(f, v) || defs[v] != 1 || def_at[v] < 0 || f->instrs[def_at[v]].op != CC_SSA_CONST ||
        cc_ssa_value_class(f, v) != CC_VAL_I64) {
        return 0;
    }
    *out = f->instrs[def_at[v]].imm;
    return 1;
}

/**
 * Call @p fn for each value @p in uses, with the role it has: 0 an operand, 1 the address of a load or store,
 * 2 the value a store writes.
 */
static void for_each_use(const cc_ssa_instr_t *in, void (*fn)(void *ctx, int v, int role), void *ctx) {
    size_t i;

    switch (in->op) {
    case CC_SSA_PARAM:
    case CC_SSA_CONST:
    case CC_SSA_STR:
    case CC_SSA_GADDR:
    case CC_SSA_LADDR:
    case CC_SSA_STACKALLOC:
    case CC_SSA_CHAIN:
    case CC_SSA_FRAMEADDR:
    case CC_SSA_REGREAD:
    case CC_SSA_LABEL:
    case CC_SSA_BR:
        /* no value operands, whatever their fields hold */
        return;
    default:
        break;
    }
    if (in->op == CC_SSA_LOAD) {
        if (in->lhs >= 0) {
            fn(ctx, in->lhs, 1);
        }
        return;
    }
    if (in->op == CC_SSA_STORE) {
        if (in->lhs >= 0) {
            fn(ctx, in->lhs, 1);
        }
        if (in->rhs >= 0) {
            fn(ctx, in->rhs, 2);
        }
        return;
    }
    if (in->lhs >= 0) {
        fn(ctx, in->lhs, 0);
    }
    if (in->rhs >= 0) {
        fn(ctx, in->rhs, 0);
    }
    if (in->call_chain > 0) {
        fn(ctx, in->call_chain - 1, 0);
    }
    for (i = 0; i < in->arg_count; ++i) {
        if (in->args[i] >= 0) {
            fn(ctx, in->args[i], 0);
        }
    }
    for (i = 0; i < in->asm_out_count + in->asm_in_count; ++i) {
        int v = i < in->asm_out_count ? in->asm_out_values[i] : in->asm_in_values[i - in->asm_out_count];

        if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
            v = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
        }
        if (v >= 0) {
            fn(ctx, v, 0);
        }
    }
}

/* ---- where each value points: a slot and an offset into it ---- */

/** What the address analysis knows of a function's values. */
typedef struct {
    const cc_ssa_function_t *f;
    int *slot;              /**< per value: the STACKALLOC value it points into, or -1 */
    long long *off;         /**< per value: its offset into that slot */
    unsigned char *escapes; /**< per STACKALLOC value: its address is used other than to load and store */
    int *defs;              /**< per value: its definitions (count_defs()) */
    int *def_at;            /**< per value: its one defining instruction, or -1 */
    int *uses;              /**< per value: how many operands name it */
} addr_info_t;

static void addr_info_free(addr_info_t *a) {
    free(a->slot);
    free(a->off);
    free(a->escapes);
    free(a->defs);
    free(a->def_at);
    free(a->uses);
    memset(a, 0, sizeof(*a));
}

/** for_each_use() callback: count a use. */
static void count_use(void *ctx, int v, int role) {
    addr_info_t *a = (addr_info_t *)ctx;

    (void)role;
    if (is_value(a->f, v)) {
        a->uses[v]++;
    }
}

/** Mark the slot @p v points into, if any, as escaping. */
static void escape_value(addr_info_t *a, int v) {
    if (is_value(a->f, v) && a->slot[v] >= 0) {
        a->escapes[a->slot[v]] = 1;
    }
}

/** for_each_use() callback: an operand that is an address into a slot is an escape, unless a load's or store's. */
static void note_use(void *ctx, int v, int role) {
    addr_info_t *a = (addr_info_t *)ctx;

    if (role != 1) {
        escape_value(a, v);
    }
}

/**
 * Whether call @p in is `memset(p, 0, n)`, n a constant (*@p n), whose result nothing uses: what cc makes of a
 * structure's initializer, which zeroes it first.
 */
static int is_zeroing_memset(const addr_info_t *a, const cc_ssa_instr_t *in, long long *n) {
    long long c;

    return in->op == CC_SSA_CALL && in->sym != NULL && strcmp(in->sym, "memset") == 0 && in->arg_count == 3 &&
           in->call_chain == 0 && (in->dst < 0 || (is_value(a->f, in->dst) && a->uses[in->dst] == 0)) &&
           single_const(a->f, a->defs, a->def_at, in->args[1], &c) && c == 0 &&
           single_const(a->f, a->defs, a->def_at, in->args[2], n);
}

/** The size of slot @p s (its STACKALLOC's), or -1. */
static long slot_size(const addr_info_t *a, int s) {
    return is_value(a->f, s) && a->def_at[s] >= 0 && a->f->instrs[a->def_at[s]].op == CC_SSA_STACKALLOC
               ? (long)a->f->instrs[a->def_at[s]].imm
               : -1;
}

/**
 * Work out where each value points: a STACKALLOC, a single-definition move of such an address, or one plus a
 * constant.  An address used any other way -- passed, stored, compared, offset by a variable, moved into a value
 * defined more than once -- escapes its slot, and under an asm goto every slot does when @p asm_goto_escapes.  A
 * zeroing memset of a whole slot does not count as an escape when @p allow_memset.
 *
 * @return 0, or -1 when out of memory
 */
static int addr_info_build(addr_info_t *a, const cc_ssa_function_t *f, int allow_memset, int asm_goto_escapes) {
    size_t n = (size_t)(f->value_count > 0 ? f->value_count : 1);
    size_t i;
    int v;
    int changed;

    memset(a, 0, sizeof(*a));
    a->f = f;
    a->defs = count_defs(f);
    a->def_at = a->defs != NULL ? find_def_at(f, a->defs) : NULL;
    a->slot = (int *)malloc(n * sizeof(*a->slot));
    a->off = (long long *)calloc(n, sizeof(*a->off));
    a->escapes = (unsigned char *)calloc(n, 1);
    a->uses = (int *)calloc(n, sizeof(*a->uses));
    if (a->defs == NULL || a->def_at == NULL || a->slot == NULL || a->off == NULL || a->escapes == NULL ||
        a->uses == NULL) {
        addr_info_free(a);
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        for_each_use(&f->instrs[i], count_use, a);
    }
    for (v = 0; v < f->value_count; ++v) {
        a->slot[v] = -1;
    }
    /* addresses through moves and constant offsets, in as many rounds as they take */
    do {
        changed = 0;
        for (i = 0; i < f->instr_count; ++i) {
            const cc_ssa_instr_t *in = &f->instrs[i];
            int d = in->dst;
            long long k;

            if (!is_value(f, d) || a->slot[d] >= 0 || a->defs[d] != 1) {
                continue;
            }
            if (in->op == CC_SSA_STACKALLOC) {
                a->slot[d] = d;
                a->off[d] = 0;
                changed = 1;
            } else if (in->op == CC_SSA_MOV && is_value(f, in->lhs) && a->slot[in->lhs] >= 0) {
                a->slot[d] = a->slot[in->lhs];
                a->off[d] = a->off[in->lhs];
                changed = 1;
            } else if (in->op == CC_SSA_ADD && is_value(f, in->lhs) && a->slot[in->lhs] >= 0 &&
                       single_const(f, a->defs, a->def_at, in->rhs, &k)) {
                a->slot[d] = a->slot[in->lhs];
                a->off[d] = a->off[in->lhs] + k;
                changed = 1;
            }
        }
    } while (changed);
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        long long sz;

        if ((in->op == CC_SSA_MOV || in->op == CC_SSA_ADD) && is_value(f, in->dst) && a->slot[in->dst] >= 0) {
            /* the address derivation itself: its operand is the base, not an escape */
            continue;
        }
        if (allow_memset && is_zeroing_memset(a, in, &sz) && is_value(f, in->args[0]) && a->slot[in->args[0]] >= 0 &&
            a->off[in->args[0]] == 0 && sz == slot_size(a, a->slot[in->args[0]])) {
            continue;
        }
        if (asm_goto_escapes && in->op == CC_SSA_ASM && in->asm_is_goto) {
            /* asm goto: nothing stays known about the function's memory */
            for (v = 0; v < f->value_count; ++v) {
                if (a->slot[v] == v) {
                    a->escapes[v] = 1;
                }
            }
        }
        for_each_use(in, note_use, a);
    }
    return 0;
}

/* ---- (a) a slot stored once is read as its value ---- */

/**
 * Forward each slot stored exactly once, directly and at one size, into its loads after the store, and drop
 * the stores of a slot nothing loads.
 *
 * @return 1 if anything changed, 0 if not, -1 when out of memory
 */
static int forward_slot_copies(cc_ssa_function_t *f) {
    addr_info_t a;
    int *stores;
    int *store_at; /* per slot: the value its first store writes */
    int *loads;
    long *size;
    unsigned char *bad;
    size_t i;
    size_t w;
    int changed = 0;

    if (f->value_count <= 0) {
        return 0;
    }
    if (addr_info_build(&a, f, 0, 1) != 0) {
        return -1;
    }
    stores = (int *)calloc((size_t)f->value_count, sizeof(*stores));
    store_at = (int *)malloc((size_t)f->value_count * sizeof(*store_at));
    loads = (int *)calloc((size_t)f->value_count, sizeof(*loads));
    size = (long *)calloc((size_t)f->value_count, sizeof(*size));
    bad = (unsigned char *)calloc((size_t)f->value_count, 1);
    if (stores == NULL || store_at == NULL || loads == NULL || size == NULL || bad == NULL) {
        free(stores);
        free(store_at);
        free(loads);
        free(size);
        free(bad);
        addr_info_free(&a);
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int s;

        if (in->op != CC_SSA_LOAD && in->op != CC_SSA_STORE) {
            continue;
        }
        if (!is_value(f, in->lhs) || a.slot[in->lhs] < 0) {
            continue;
        }
        s = a.slot[in->lhs];
        if (in->lhs != s) {
            /* through an offset or a copy of the address: not a whole-slot access this pass forwards */
            bad[s] = 1;
            continue;
        }
        if ((size[s] != 0 && size[s] != (long)in->imm) ||
            (in->imm != 1 && in->imm != 2 && in->imm != 4 && in->imm != 8)) {
            /* accesses of different sizes, or an aggregate's copy, are not one value */
            bad[s] = 1;
        }
        size[s] = (long)in->imm;
        if (in->op == CC_SSA_STORE) {
            if (stores[s]++ == 0) {
                store_at[s] = in->rhs; /* the value stored */
            }
            /* a value defined more than once, or not of the store's size, is not forwarded */
            if (!is_value(f, in->rhs) || a.defs[in->rhs] != 1 || cc_ssa_value_size(f, in->rhs) != (uint32_t)in->imm) {
                bad[s] = 1;
            }
        } else {
            loads[s]++;
            /* a load before the store, or reading it as another kind of value */
            if (stores[s] == 0 || !is_value(f, in->dst) || !is_value(f, store_at[s]) ||
                cc_ssa_value_class(f, in->dst) != cc_ssa_value_class(f, store_at[s]) ||
                cc_ssa_value_size(f, in->dst) != (uint32_t)in->imm) {
                bad[s] = 1;
            }
        }
    }
    w = 0;
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        int s = -1;

        if ((in->op == CC_SSA_LOAD || in->op == CC_SSA_STORE) && is_value(f, in->lhs) && a.slot[in->lhs] == in->lhs) {
            s = in->lhs;
        }
        if (s >= 0 && !a.escapes[s] && !bad[s]) {
            if (in->op == CC_SSA_STORE && loads[s] == 0) {
                /* nothing reads it */
                cc_ssa_instr_free(in);
                changed = 1;
                continue;
            }
            if (in->op == CC_SSA_LOAD && stores[s] == 1) {
                /* the value stored, kept from the scan: instructions before this one may have moved */
                in->op = CC_SSA_MOV;
                in->lhs = store_at[s];
                in->rhs = -1;
                in->imm = 0;
                changed = 1;
            }
        }
        if (w != i) {
            f->instrs[w] = f->instrs[i];
        }
        w++;
    }
    f->instr_count = w;
    free(stores);
    free(store_at);
    free(loads);
    free(size);
    free(bad);
    addr_info_free(&a);
    return changed;
}

/* ---- (b) a structure slot split into its fields ---- */

/** One field of a slot being split: where it is in the slot, and the slot that holds it now. */
typedef struct {
    int slot;       /**< the slot it is a field of */
    long long off;  /**< its offset in it */
    long size;      /**< its size */
    int new_slot;   /**< the STACKALLOC value that holds it from now, or -1 until made */
} field_t;

/** The instruction list being rebuilt. */
typedef struct {
    cc_ssa_instr_t *at;
    size_t count;
    size_t cap;
} ilist_t;

/** Append @p in, whose pointers the list now owns; 0, or -1 when out of memory. */
static int ilist_put(ilist_t *l, const cc_ssa_instr_t *in) {
    if (l->count == l->cap) {
        size_t ncap = l->cap == 0 ? 64 : l->cap * 2;
        cc_ssa_instr_t *next = (cc_ssa_instr_t *)realloc(l->at, ncap * sizeof(*next));

        if (next == NULL) {
            return -1;
        }
        l->at = next;
        l->cap = ncap;
    }
    l->at[l->count++] = *in;
    return 0;
}

/** An instruction of operation @p op and no operands. */
static cc_ssa_instr_t plain_instr(cc_ssa_opcode_t op) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = -1;
    in.true_label = -1;
    in.false_label = -1;
    in.param_index = -1;
    return in;
}

/** The field of slot @p s at @p off of @p size, or -1. */
static long find_field(const field_t *fields, size_t n, int s, long long off, long size) {
    size_t k;

    for (k = 0; k < n; ++k) {
        if (fields[k].slot == s && fields[k].off == off && fields[k].size == size) {
            return (long)k;
        }
    }
    return -1;
}

/**
 * Split each non-escaping slot all of whose accesses are loads and stores of whole fields that do not
 * overlap, at constant offsets -- and a zeroing memset of the whole slot -- into one slot per field.
 *
 * @return 1 if anything changed, 0 if not, -1 when out of memory
 */
static int scalarize_slots(cc_ssa_function_t *f) {
    addr_info_t a;
    field_t *fields = NULL;
    size_t nfields = 0;
    size_t capfields = 0;
    unsigned char *split = NULL; /* per slot: 0 unseen, 1 splittable, 2 not */
    int *nfield_of = NULL;
    ilist_t out;
    size_t i;
    int s;
    int any = 0;
    int rc = 0;

    memset(&out, 0, sizeof(out));
    if (f->value_count <= 0) {
        return 0;
    }
    if (addr_info_build(&a, f, 1, 1) != 0) {
        return -1;
    }
    split = (unsigned char *)calloc((size_t)f->value_count, 1);
    nfield_of = (int *)calloc((size_t)f->value_count, sizeof(*nfield_of));
    if (split == NULL || nfield_of == NULL) {
        rc = -1;
        goto done;
    }
    /* the fields each slot is accessed as: an access overlapping another unequally, and it stays whole */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        long long off;
        long size;
        size_t k;

        if ((in->op != CC_SSA_LOAD && in->op != CC_SSA_STORE) || !is_value(f, in->lhs) || a.slot[in->lhs] < 0) {
            continue;
        }
        s = a.slot[in->lhs];
        off = a.off[in->lhs];
        size = (long)in->imm;
        if (a.escapes[s] || split[s] == 2) {
            continue;
        }
        split[s] = 1;
        if ((size != 1 && size != 2 && size != 4 && size != 8) || off < 0 || off + size > slot_size(&a, s)) {
            split[s] = 2;
            continue;
        }
        if (find_field(fields, nfields, s, off, size) >= 0) {
            continue;
        }
        for (k = 0; k < nfields; ++k) {
            if (fields[k].slot == s && off < fields[k].off + fields[k].size && fields[k].off < off + size) {
                split[s] = 2;
            }
        }
        if (split[s] == 2) {
            continue;
        }
        if (nfields == capfields) {
            size_t ncap = capfields == 0 ? 16 : capfields * 2;
            field_t *next = (field_t *)realloc(fields, ncap * sizeof(*next));

            if (next == NULL) {
                rc = -1;
                goto done;
            }
            fields = next;
            capfields = ncap;
        }
        fields[nfields].slot = s;
        fields[nfields].off = off;
        fields[nfields].size = size;
        fields[nfields].new_slot = -1;
        nfields++;
        nfield_of[s]++;
    }
    /* worth it for a slot of several fields, or of one not covering it */
    for (i = 0; i < nfields; ++i) {
        s = fields[i].slot;
        if (a.escapes[s] || split[s] != 1) {
            continue;
        }
        if (nfield_of[s] > 1 || fields[i].off != 0 || fields[i].size != slot_size(&a, s)) {
            any = 1;
        } else {
            split[s] = 2; /* one field, the whole slot: nothing to split */
        }
    }
    if (!any) {
        goto done;
    }
    /* each field's slot, made before any access is rewritten: every access of the slot goes, or none */
    for (i = 0; i < nfields; ++i) {
        s = fields[i].slot;
        if (a.escapes[s] || split[s] != 1) {
            continue;
        }
        fields[i].new_slot = cc_ssa_function_new_value(f, CC_VAL_I64);
        if (fields[i].new_slot < 0) {
            rc = -1;
            goto done;
        }
        f->value_ty[fields[i].new_slot] = f->value_ty[s];
    }
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        long long n;
        size_t k;

        if (in->op == CC_SSA_STACKALLOC && is_value(f, in->dst) && a.slot[in->dst] == in->dst &&
            !a.escapes[in->dst] && split[in->dst] == 1) {
            /* the fields' slots, allocated where the slot is; the slot itself stays for dce */
            s = in->dst;
            for (k = 0; k < nfields; ++k) {
                cc_ssa_instr_t na;

                if (fields[k].slot != s || fields[k].new_slot < 0) {
                    continue;
                }
                na = plain_instr(CC_SSA_STACKALLOC);
                na.dst = fields[k].new_slot;
                na.imm = fields[k].size;
                if (ilist_put(&out, &na) != 0) {
                    rc = -1;
                    goto done;
                }
            }
            if (ilist_put(&out, in) != 0) {
                rc = -1;
                goto done;
            }
            continue;
        }
        if ((in->op == CC_SSA_LOAD || in->op == CC_SSA_STORE) && is_value(f, in->lhs) && a.slot[in->lhs] >= 0 &&
            split[a.slot[in->lhs]] == 1 && !a.escapes[a.slot[in->lhs]]) {
            long fk = find_field(fields, nfields, a.slot[in->lhs], a.off[in->lhs], (long)in->imm);

            if (fk >= 0 && fields[fk].new_slot >= 0) {
                in->lhs = fields[fk].new_slot;
            }
        } else if (is_zeroing_memset(&a, in, &n) && is_value(f, in->args[0]) && a.slot[in->args[0]] >= 0 &&
                   a.off[in->args[0]] == 0 && split[a.slot[in->args[0]]] == 1 && !a.escapes[a.slot[in->args[0]]] &&
                   n == slot_size(&a, a.slot[in->args[0]])) {
            /* memset(slot, 0, its size): 0 stored to each field */
            s = a.slot[in->args[0]];
            for (k = 0; k < nfields; ++k) {
                cc_ssa_instr_t z;
                cc_ssa_instr_t st;

                if (fields[k].slot != s || fields[k].new_slot < 0) {
                    continue;
                }
                z = plain_instr(CC_SSA_CONST);
                z.dst = cc_ssa_function_new_value(f, CC_VAL_I64);
                if (z.dst < 0) {
                    rc = -1;
                    goto done;
                }
                cc_ssa_value_set_size(f, z.dst, (uint32_t)fields[k].size);
                st = plain_instr(CC_SSA_STORE);
                st.lhs = fields[k].new_slot;
                st.rhs = z.dst;
                st.imm = fields[k].size;
                if (ilist_put(&out, &z) != 0 || ilist_put(&out, &st) != 0) {
                    rc = -1;
                    goto done;
                }
            }
            cc_ssa_instr_free(in);
            continue;
        }
        if (ilist_put(&out, in) != 0) {
            rc = -1;
            goto done;
        }
    }
    free(f->instrs);
    f->instrs = out.at;
    f->instr_count = out.count;
    f->instr_cap = out.cap;
    out.at = NULL;
    rc = 1;
done:
    if (rc < 0 && out.at != NULL) {
        /*
         * out of memory part way: the instructions moved to out are owned there, the rest by f; give f back
         * what it had in order is not possible, so free both -- the compilation fails anyway
         */
        size_t k;

        for (k = 0; k < out.count; ++k) {
            cc_ssa_instr_free(&out.at[k]);
        }
        free(f->instrs);
        f->instrs = NULL;
        f->instr_count = 0;
        f->instr_cap = 0;
    }
    free(out.at);
    free(fields);
    free(split);
    free(nfield_of);
    addr_info_free(&a);
    return rc;
}

/* ---- (c) the constants a slot can hold, block by block ---- */

/** A function's blocks: where each starts and ends, and where each goes. */
typedef struct {
    size_t count;
    size_t *first;    /**< per block: its first instruction */
    size_t *end;      /**< per block: one past its last */
    int *block_of_label; /**< per label: the block it starts, or -1 */
    int *succ;        /**< per block: up to two successors (-1 none) at [2b], [2b+1] */
    unsigned char *opaque; /**< per block: it ends in an asm goto or reaches a label by address: give up */
} cfg_t;

static void cfg_free(cfg_t *c) {
    free(c->first);
    free(c->end);
    free(c->block_of_label);
    free(c->succ);
    free(c->opaque);
    memset(c, 0, sizeof(*c));
}

/** Whether @p op ends a block. */
static int ends_block(cc_ssa_opcode_t op) {
    return op == CC_SSA_BR || op == CC_SSA_BR_COND || op == CC_SSA_RET || op == CC_SSA_TRAP;
}

/**
 * Split @p f into blocks: one starts at the beginning, at each label, and after each branch, return or trap.
 *
 * @return 0, or -1 when out of memory
 */
static int cfg_build(cfg_t *c, const cc_ssa_function_t *f) {
    size_t i;
    size_t b;
    int l;

    memset(c, 0, sizeof(*c));
    c->first = (size_t *)malloc((f->instr_count + 1) * sizeof(*c->first));
    c->end = (size_t *)malloc((f->instr_count + 1) * sizeof(*c->end));
    c->block_of_label = (int *)malloc((size_t)(f->label_count > 0 ? f->label_count : 1) * sizeof(int));
    if (c->first == NULL || c->end == NULL || c->block_of_label == NULL) {
        cfg_free(c);
        return -1;
    }
    for (l = 0; l < f->label_count; ++l) {
        c->block_of_label[l] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int starts = i == 0 || in->op == CC_SSA_LABEL || ends_block(f->instrs[i - 1].op) ||
                     (f->instrs[i - 1].op == CC_SSA_ASM && f->instrs[i - 1].asm_is_goto);

        if (starts) {
            if (c->count > 0) {
                c->end[c->count - 1] = i;
            }
            c->first[c->count++] = i;
        }
        if (in->op == CC_SSA_LABEL && in->label >= 0 && in->label < f->label_count) {
            c->block_of_label[in->label] = (int)(c->count - 1);
        }
    }
    if (c->count > 0) {
        c->end[c->count - 1] = f->instr_count;
    }
    c->succ = (int *)malloc((c->count > 0 ? c->count : 1) * 2 * sizeof(int));
    c->opaque = (unsigned char *)calloc(c->count > 0 ? c->count : 1, 1);
    if (c->succ == NULL || c->opaque == NULL) {
        cfg_free(c);
        return -1;
    }
    for (b = 0; b < c->count; ++b) {
        const cc_ssa_instr_t *last = &f->instrs[c->end[b] - 1];
        int next = b + 1 < c->count ? (int)(b + 1) : -1;
        int s0 = -1;
        int s1 = -1;

        if (last->op == CC_SSA_BR) {
            s0 = last->label >= 0 && last->label < f->label_count ? c->block_of_label[last->label] : -1;
        } else if (last->op == CC_SSA_BR_COND) {
            s0 = last->true_label >= 0 && last->true_label < f->label_count ? c->block_of_label[last->true_label] : -1;
            s1 = last->false_label >= 0 && last->false_label < f->label_count ? c->block_of_label[last->false_label]
                                                                               : -1;
        } else if (last->op == CC_SSA_RET || last->op == CC_SSA_TRAP) {
            /* nowhere */
        } else {
            s0 = next;
            if (last->op == CC_SSA_ASM && last->asm_is_goto) {
                c->opaque[b] = 1;
            }
        }
        c->succ[2 * b] = s0;
        c->succ[2 * b + 1] = s1;
    }
    /* a label whose address is taken is reached by a computed goto, from anywhere: give up on the function */
    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op == CC_SSA_LADDR) {
            for (b = 0; b < c->count; ++b) {
                c->opaque[b] = 1;
            }
            break;
        }
    }
    return 0;
}

/** The slots slot_value_sets() follows, and the constants each is stored. */
typedef struct {
    int nslots;
    int *slot_of;        /**< per value: the index of the slot it is (a STACKALLOC value), or -1 */
    int *slot_value;     /**< per index: the slot's value */
    long *size;          /**< per index: the size its loads and stores have */
    int *ndom;           /**< per index: how many constants it is stored */
    long long *dom;      /**< per index x SET_MAX: those constants, truncated to its size */
} slots_t;

static void slots_free(slots_t *s) {
    free(s->slot_of);
    free(s->slot_value);
    free(s->size);
    free(s->ndom);
    free(s->dom);
    memset(s, 0, sizeof(*s));
}

/** @p v truncated to @p size bytes, as an unsigned number. */
static long long trunc_to(long long v, long size) {
    return size >= 8 ? v : (long long)((unsigned long long)v & ((1ULL << (size * 8)) - 1));
}

/** Constant @p c of a slot of @p size, as a load of it reads it: zero- or sign-extended. */
static long long as_loaded(long long c, long size, int is_unsigned) {
    if (size < 8 && !is_unsigned && (c & (1LL << (size * 8 - 1))) != 0) {
        return (long long)((unsigned long long)c | ~((1ULL << (size * 8)) - 1));
    }
    return c;
}

/** The index of constant @p c among slot @p j's, or -1. */
static int dom_index(const slots_t *s, int j, long long c) {
    int k;

    for (k = 0; k < s->ndom[j]; ++k) {
        if (s->dom[(size_t)j * SET_MAX + (size_t)k] == trunc_to(c, s->size[j])) {
            return k;
        }
    }
    return -1;
}

/**
 * The slots to follow: not escaping, every access a direct integer load or store of one size up to 8 bytes,
 * every store of a constant, at most SET_MAX of them.
 *
 * @return 0, or -1 when out of memory
 */
static int slots_find(slots_t *s, const cc_ssa_function_t *f, const addr_info_t *a) {
    size_t i;
    int v;
    unsigned char *ok;

    memset(s, 0, sizeof(*s));
    ok = (unsigned char *)calloc((size_t)f->value_count, 1);
    s->slot_of = (int *)malloc((size_t)f->value_count * sizeof(int));
    if (ok == NULL || s->slot_of == NULL) {
        free(ok);
        slots_free(s);
        return -1;
    }
    for (v = 0; v < f->value_count; ++v) {
        s->slot_of[v] = -1;
        ok[v] = a->slot[v] == v && !a->escapes[v];
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if ((in->op == CC_SSA_LOAD || in->op == CC_SSA_STORE) && is_value(f, in->lhs) && a->slot[in->lhs] >= 0) {
            int sl = a->slot[in->lhs];
            long long c;

            if (in->lhs != sl || in->imm < 1 || in->imm > 8 ||
                (in->op == CC_SSA_LOAD && (!is_value(f, in->dst) || cc_ssa_value_class(f, in->dst) != CC_VAL_I64)) ||
                (in->op == CC_SSA_STORE && !single_const(f, a->defs, a->def_at, in->rhs, &c))) {
                ok[sl] = 0;
            }
        }
    }
    for (v = 0; v < f->value_count; ++v) {
        if (ok[v]) {
            s->nslots++;
        }
    }
    if (s->nslots == 0) {
        free(ok);
        return 0;
    }
    s->slot_value = (int *)malloc((size_t)s->nslots * sizeof(int));
    s->size = (long *)calloc((size_t)s->nslots, sizeof(long));
    s->ndom = (int *)calloc((size_t)s->nslots, sizeof(int));
    s->dom = (long long *)malloc((size_t)s->nslots * SET_MAX * sizeof(long long));
    if (s->slot_value == NULL || s->size == NULL || s->ndom == NULL || s->dom == NULL) {
        free(ok);
        slots_free(s);
        return -1;
    }
    s->nslots = 0;
    for (v = 0; v < f->value_count; ++v) {
        if (ok[v]) {
            s->slot_of[v] = s->nslots;
            s->slot_value[s->nslots++] = v;
        }
    }
    free(ok);
    /* each slot's size and constants; one of another size, or too many constants, and it is dropped */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int j;
        long long c;

        if ((in->op != CC_SSA_LOAD && in->op != CC_SSA_STORE) || !is_value(f, in->lhs) ||
            (j = s->slot_of[in->lhs]) < 0) {
            continue;
        }
        if (s->size[j] == 0) {
            s->size[j] = (long)in->imm;
        } else if (s->size[j] != (long)in->imm) {
            s->slot_of[in->lhs] = -1;
            continue;
        }
        if (in->op == CC_SSA_STORE && single_const(f, a->defs, a->def_at, in->rhs, &c) && dom_index(s, j, c) < 0) {
            if (s->ndom[j] == SET_MAX) {
                s->slot_of[in->lhs] = -1;
                continue;
            }
            s->dom[(size_t)j * SET_MAX + (size_t)s->ndom[j]++] = trunc_to(c, s->size[j]);
        }
    }
    return 0;
}

/** The most operations between a load and the compare of what it read that cmp_eq_mask() follows. */
#define CHAIN_MAX 8

/** Whether value @p v is computed in 32-bit words, as the backend computes it (opt.c's value_is_word32()). */
static int word32_of(const cc_ssa_function_t *f, int v) {
    return cc_target_pointer_size() == 4 && (!is_value(f, v) || cc_ssa_value_size(f, v) < 8);
}

/** @p op of @p x and constant @p k, computed as the target does in a word of 32 bits when @p word32. */
static long long eval_op(cc_ssa_opcode_t op, long long x, long long k, int is_unsigned, int word32) {
    unsigned long long ux = (unsigned long long)x;
    unsigned long long uk = (unsigned long long)k;
    unsigned long long r;

    if (word32) {
        unsigned int w = (unsigned int)ux;
        unsigned int wk = (unsigned int)uk;

        switch (op) {
        case CC_SSA_ADD: w += wk; break;
        case CC_SSA_SUB: w -= wk; break;
        case CC_SSA_AND: w &= wk; break;
        case CC_SSA_OR: w |= wk; break;
        case CC_SSA_XOR: w ^= wk; break;
        case CC_SSA_SHL: w <<= (wk & 31u); break;
        case CC_SSA_SHR: w = is_unsigned ? w >> (wk & 31u) : (unsigned int)((int)w >> (wk & 31u)); break;
        default: break;
        }
        return (long long)(int)w;
    }
    switch (op) {
    case CC_SSA_ADD: r = ux + uk; break;
    case CC_SSA_SUB: r = ux - uk; break;
    case CC_SSA_AND: r = ux & uk; break;
    case CC_SSA_OR: r = ux | uk; break;
    case CC_SSA_XOR: r = ux ^ uk; break;
    case CC_SSA_SHL: r = ux << (uk & 63u); break;
    case CC_SSA_SHR: r = is_unsigned ? ux >> (uk & 63u) : (unsigned long long)(x >> (uk & 63u)); break;
    default: r = ux; break;
    }
    return (long long)r;
}

/** One step of the chain from a load to a compare: an operation with a constant. */
typedef struct {
    cc_ssa_opcode_t op;
    long long k;
    int is_unsigned;
    int word32;
} chain_op_t;

/**
 * For compare @p cmp, `x == k` or `x != k` with k constant: when x is what a load of a followed slot read, through
 * moves and operations with constants (the extension of a narrow value, a mask), the mask of the slot's constants
 * for which x == k (*@p eq), the slot's index (*@p j) and the load's instruction (*@p load_at).
 *
 * @return 1 if so, else 0
 */
static int cmp_eq_mask(const cc_ssa_function_t *f, const addr_info_t *a, const slots_t *s, const cc_ssa_instr_t *cmp,
                       int *j, size_t *load_at, uint32_t *eq) {
    chain_op_t chain[CHAIN_MAX];
    int n = 0;
    long long k;
    int x;
    int cmp_word32;
    const cc_ssa_instr_t *ld = NULL;
    int d;

    if (cmp->op != CC_SSA_CMP || (cmp->cmp_kind != CC_CMP_EQ && cmp->cmp_kind != CC_CMP_NE)) {
        return 0;
    }
    if (single_const(f, a->defs, a->def_at, cmp->rhs, &k)) {
        x = cmp->lhs;
    } else if (single_const(f, a->defs, a->def_at, cmp->lhs, &k)) {
        x = cmp->rhs;
    } else {
        return 0;
    }
    cmp_word32 = word32_of(f, cmp->lhs);
    while (is_value(f, x) && a->defs[x] == 1 && a->def_at[x] >= 0) {
        const cc_ssa_instr_t *in = &f->instrs[a->def_at[x]];
        long long c;

        if (in->op == CC_SSA_LOAD) {
            ld = in;
            break;
        }
        if (n == CHAIN_MAX) {
            return 0;
        }
        if (in->op == CC_SSA_MOV) {
            x = in->lhs;
            continue;
        }
        if (in->op != CC_SSA_ADD && in->op != CC_SSA_SUB && in->op != CC_SSA_AND && in->op != CC_SSA_OR &&
            in->op != CC_SSA_XOR && in->op != CC_SSA_SHL && in->op != CC_SSA_SHR) {
            return 0;
        }
        chain[n].op = in->op;
        chain[n].is_unsigned = in->is_unsigned;
        chain[n].word32 = word32_of(f, in->dst);
        if ((in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) && in->rhs < 0) {
            chain[n].k = in->imm;
            x = in->lhs;
        } else if (single_const(f, a->defs, a->def_at, in->rhs, &c)) {
            chain[n].k = c;
            x = in->lhs;
        } else if (in->op != CC_SSA_SUB && in->op != CC_SSA_SHL && in->op != CC_SSA_SHR &&
                   single_const(f, a->defs, a->def_at, in->lhs, &c)) {
            chain[n].k = c;
            x = in->rhs;
        } else {
            return 0;
        }
        n++;
    }
    if (ld == NULL || !is_value(f, ld->lhs) || s->slot_of[ld->lhs] < 0) {
        return 0;
    }
    *j = s->slot_of[ld->lhs];
    *load_at = (size_t)a->def_at[x];
    *eq = 0;
    for (d = 0; d < s->ndom[*j]; ++d) {
        long long v = as_loaded(s->dom[(size_t)*j * SET_MAX + (size_t)d], s->size[*j], ld->is_unsigned);
        int i;

        /* the chain was gathered from the compare back to the load: apply it the other way */
        for (i = n - 1; i >= 0; --i) {
            v = eval_op(chain[i].op, v, chain[i].k, chain[i].is_unsigned, chain[i].word32);
        }
        if (cmp_word32 ? (unsigned int)v == (unsigned int)k : v == k) {
            *eq |= 1u << d;
        }
    }
    return 1;
}

/** What a branch's condition says of a slot: the slot's index, and which of its constants make it true. */
typedef struct {
    int slot;          /**< the slot's index, or -1 when the condition says nothing of one */
    uint32_t on_true;  /**< the slot's constants for which the condition is true */
    uint32_t all;      /**< all of the slot's constants */
} cond_fact_t;

/**
 * What branch @p br_at, ending a block that starts at @p first, says of a slot: its condition a compare --
 * through `!= 0` and `== 0` of compares -- of what a load of a followed slot read with a constant
 * (cmp_eq_mask()), the load in this block and no store to the slot between it and the branch.
 */
static cond_fact_t branch_fact(const cc_ssa_function_t *f, const addr_info_t *a, const slots_t *s, size_t first,
                               size_t br_at) {
    cond_fact_t fact;
    int v = f->instrs[br_at].lhs;
    int polarity = 1; /* 1: the condition true means the compare below is true */
    int guard = 0;

    memset(&fact, 0, sizeof(fact));
    fact.slot = -1;
    while (is_value(f, v) && a->defs[v] == 1 && a->def_at[v] >= 0 && guard++ < 8) {
        const cc_ssa_instr_t *cmp = &f->instrs[a->def_at[v]];
        long long c;
        int inner;
        int j;
        size_t load_at;
        uint32_t eq;
        size_t i;

        if (cmp->op != CC_SSA_CMP || (cmp->cmp_kind != CC_CMP_EQ && cmp->cmp_kind != CC_CMP_NE)) {
            return fact;
        }
        /* (compare) != 0 is the compare; (compare) == 0 its negation */
        inner = single_const(f, a->defs, a->def_at, cmp->rhs, &c) && c == 0 ? cmp->lhs : -1;
        if (is_value(f, inner) && a->defs[inner] == 1 && a->def_at[inner] >= 0 &&
            f->instrs[a->def_at[inner]].op == CC_SSA_CMP) {
            if (cmp->cmp_kind == CC_CMP_EQ) {
                polarity = !polarity;
            }
            v = inner;
            continue;
        }
        if (!cmp_eq_mask(f, a, s, cmp, &j, &load_at, &eq) || load_at < first || load_at > br_at) {
            return fact;
        }
        for (i = load_at + 1; i < br_at; ++i) {
            if (f->instrs[i].op == CC_SSA_STORE && f->instrs[i].lhs == f->instrs[load_at].lhs) {
                return fact;
            }
        }
        fact.slot = j;
        fact.all = s->ndom[j] >= 32 ? 0xffffffffu : (1u << s->ndom[j]) - 1;
        /* the compare true: x == k for EQ, x != k for NE; the condition true when the compare is, by polarity */
        fact.on_true = (cmp->cmp_kind == CC_CMP_EQ) == polarity ? eq : fact.all & ~eq;
        return fact;
    }
    return fact;
}

/**
 * Narrow each followed slot's set of constants through block @p b, from @p in, into @p out (its state at the
 * block's end); with @p loadmask, record the set at each load of a followed slot there.
 */
static void block_transfer(const cc_ssa_function_t *f, const cfg_t *c, const slots_t *s, const addr_info_t *a,
                           size_t b, const uint32_t *in, uint32_t *out, uint32_t *loadmask) {
    size_t i;

    memcpy(out, in, (size_t)s->nslots * sizeof(*out));
    for (i = c->first[b]; i < c->end[b]; ++i) {
        const cc_ssa_instr_t *ins = &f->instrs[i];
        int j;
        long long k;

        if ((ins->op != CC_SSA_LOAD && ins->op != CC_SSA_STORE) || !is_value(f, ins->lhs) ||
            (j = s->slot_of[ins->lhs]) < 0) {
            continue;
        }
        if (ins->op == CC_SSA_STORE) {
            int d = single_const(f, a->defs, a->def_at, ins->rhs, &k) ? dom_index(s, j, k) : -1;

            out[j] = d >= 0 ? 1u << d : 0xffffffffu;
        } else if (loadmask != NULL && is_value(f, ins->dst)) {
            loadmask[ins->dst] = out[j];
        }
    }
}

/**
 * Fold the compares of loads of slots only stored constants against constants the slot's set at the load rules
 * in or out, the set worked out over the blocks with the branches on such compares narrowing it.
 *
 * @return 1 if a compare was folded, 0 if not, -1 when out of memory
 */
static int slot_value_sets(cc_ssa_function_t *f) {
    addr_info_t a;
    cfg_t c;
    slots_t s;
    uint32_t *in = NULL;
    uint32_t *tmp = NULL;
    uint32_t *loadmask = NULL;
    size_t *work = NULL;
    unsigned char *queued = NULL;
    size_t nwork = 0;
    size_t b;
    size_t i;
    int changed = 0;
    int rc = 0;

    if (f->value_count <= 0 || f->instr_count == 0) {
        return 0;
    }
    if (addr_info_build(&a, f, 0, 1) != 0) {
        return -1;
    }
    if (slots_find(&s, f, &a) != 0) {
        addr_info_free(&a);
        return -1;
    }
    if (s.nslots == 0) {
        slots_free(&s);
        addr_info_free(&a);
        return 0;
    }
    if (cfg_build(&c, f) != 0) {
        slots_free(&s);
        addr_info_free(&a);
        return -1;
    }
    for (b = 0; b < c.count; ++b) {
        if (c.opaque[b]) {
            goto done; /* control this analysis cannot follow */
        }
    }
    if ((long)s.nslots * (long)c.count > SET_MAX_STATES) {
        goto done;
    }
    in = (uint32_t *)calloc(c.count * (size_t)s.nslots, sizeof(*in));
    tmp = (uint32_t *)malloc((size_t)s.nslots * sizeof(*tmp));
    loadmask = (uint32_t *)calloc((size_t)f->value_count, sizeof(*loadmask));
    work = (size_t *)malloc(c.count * sizeof(*work));
    queued = (unsigned char *)calloc(c.count, 1);
    if (in == NULL || tmp == NULL || loadmask == NULL || work == NULL || queued == NULL) {
        rc = -1;
        goto done;
    }
    /*
     * The entry holds nothing (a load before any store reads an uninitialized local), and the sets only grow:
     * every block is visited once, the first first, and again while what reaches it grows.
     */
    for (b = c.count; b > 0; --b) {
        work[nwork++] = b - 1;
        queued[b - 1] = 1;
    }
    while (nwork > 0) {
        size_t e;
        const cc_ssa_instr_t *last;
        cond_fact_t fact;

        b = work[--nwork];
        queued[b] = 0;
        block_transfer(f, &c, &s, &a, b, &in[b * (size_t)s.nslots], tmp, NULL);
        last = &f->instrs[c.end[b] - 1];
        memset(&fact, 0, sizeof(fact));
        fact.slot = -1;
        if (last->op == CC_SSA_BR_COND) {
            fact = branch_fact(f, &a, &s, c.first[b], c.end[b] - 1);
        }
        for (e = 0; e < 2; ++e) {
            int succ = c.succ[2 * b + e];
            int j;
            int grew = 0;

            if (succ < 0) {
                continue;
            }
            for (j = 0; j < s.nslots; ++j) {
                uint32_t m = tmp[j];
                uint32_t *dst = &in[(size_t)succ * (size_t)s.nslots + (size_t)j];

                if (fact.slot == j && m != 0xffffffffu) {
                    /* the true edge (e == 0) carries the constants that make the condition true */
                    m &= e == 0 ? fact.on_true : fact.all & ~fact.on_true;
                }
                if ((*dst | m) != *dst) {
                    *dst |= m;
                    grew = 1;
                }
            }
            if (grew && !queued[succ]) {
                queued[succ] = 1;
                work[nwork++] = (size_t)succ;
            }
        }
    }
    /* the set at each load, from the converged block states */
    for (b = 0; b < c.count; ++b) {
        block_transfer(f, &c, &s, &a, b, &in[b * (size_t)s.nslots], tmp, loadmask);
    }
    /* a compare of such a load with a constant that the set decides */
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *cmp = &f->instrs[i];
        int j;
        size_t load_at;
        uint32_t m;
        uint32_t eq;

        if (!is_value(f, cmp->dst) || !cmp_eq_mask(f, &a, &s, cmp, &j, &load_at, &eq) ||
            !is_value(f, f->instrs[load_at].dst)) {
            continue;
        }
        m = loadmask[f->instrs[load_at].dst];
        if (m == 0 || m == 0xffffffffu) {
            continue; /* unreached, or a store of a constant it does not know */
        }
        eq &= m;
        if (eq != 0 && eq != m) {
            continue; /* some of the set equal, some not */
        }
        cmp->imm = (eq != 0) == (cmp->cmp_kind == CC_CMP_EQ) ? 1 : 0;
        cmp->op = CC_SSA_CONST;
        cmp->lhs = -1;
        cmp->rhs = -1;
        changed = 1;
    }
    rc = changed;
done:
    free(in);
    free(tmp);
    free(loadmask);
    free(work);
    free(queued);
    cfg_free(&c);
    slots_free(&s);
    addr_info_free(&a);
    return rc;
}

/* ---- (d) a copy read as what it copies ---- */

/** for_each_use() callback: an asm operand's value, or a value whose address is taken, is not a copy's. */
static void note_pinned(void *ctx, int v, int role) {
    unsigned char *pinned = (unsigned char *)ctx;

    (void)role;
    pinned[v] = 1;
}

/** Operand @p v read through @p repl, the copies resolved. */
static int resolve_copy(const int *repl, int v) {
    while (v >= 0 && repl[v] != v) {
        v = repl[v];
    }
    return v;
}

int cc_propagate_copies(cc_ssa_function_t *f) {
    int *defs;
    int *repl = NULL;
    unsigned char *pinned = NULL;
    size_t i;
    size_t k;
    int v;
    int any = 0;

    if (f->value_count <= 0) {
        return 0;
    }
    defs = count_defs(f);
    repl = (int *)malloc((size_t)f->value_count * sizeof(*repl));
    pinned = (unsigned char *)calloc((size_t)f->value_count, 1);
    if (defs == NULL || repl == NULL || pinned == NULL) {
        free(defs);
        free(repl);
        free(pinned);
        return -1;
    }
    for (v = 0; v < f->value_count; ++v) {
        repl[v] = v;
    }
    /* a value whose storage is reached otherwise than by its name: its address taken, or an asm's operand */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_ADDR && is_value(f, in->lhs)) {
            pinned[in->lhs] = 1;
        } else if (in->op == CC_SSA_ASM) {
            for_each_use(in, note_pinned, pinned);
        }
    }
    /*
     * dst = src, each defined once and neither pinned, and of one class, size and signedness: dst is src
     * wherever it is read.  src's definition is before the move on every path to it and the move before
     * every read of dst, and src never changes: it is the same value at each.
     */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op != CC_SSA_MOV || !is_value(f, in->dst) || !is_value(f, in->lhs) || in->dst == in->lhs ||
            defs[in->dst] != 1 || defs[in->lhs] != 1 || pinned[in->dst] || pinned[in->lhs] ||
            cc_ssa_value_class(f, in->dst) != cc_ssa_value_class(f, in->lhs) ||
            cc_ssa_value_size(f, in->dst) != cc_ssa_value_size(f, in->lhs) ||
            cc_ssa_value_is_unsigned(f, in->dst) != cc_ssa_value_is_unsigned(f, in->lhs)) {
            continue;
        }
        repl[in->dst] = in->lhs;
    }
    /* each read of a copy reads its source: the operands for_each_use() names, an asm's aside */
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        int r;

        switch (in->op) {
        case CC_SSA_PARAM:
        case CC_SSA_CONST:
        case CC_SSA_STR:
        case CC_SSA_GADDR:
        case CC_SSA_LADDR:
        case CC_SSA_STACKALLOC:
        case CC_SSA_CHAIN:
        case CC_SSA_FRAMEADDR:
        case CC_SSA_REGREAD:
        case CC_SSA_LABEL:
        case CC_SSA_BR:
        case CC_SSA_ASM:
        case CC_SSA_ADDR:
            continue;
        default:
            break;
        }
        if (in->lhs >= 0 && (r = resolve_copy(repl, in->lhs)) != in->lhs) {
            in->lhs = r;
            any = 1;
        }
        if (in->op != CC_SSA_LOAD && in->rhs >= 0 && (r = resolve_copy(repl, in->rhs)) != in->rhs) {
            in->rhs = r;
            any = 1;
        }
        if (in->op == CC_SSA_LOAD || in->op == CC_SSA_STORE) {
            continue;
        }
        if (in->call_chain > 0 && (r = resolve_copy(repl, in->call_chain - 1)) != in->call_chain - 1) {
            in->call_chain = r + 1;
            any = 1;
        }
        for (k = 0; k < in->arg_count; ++k) {
            if (in->args[k] >= 0 && (r = resolve_copy(repl, in->args[k])) != in->args[k]) {
                in->args[k] = r;
                any = 1;
            }
        }
    }
    free(defs);
    free(repl);
    free(pinned);
    return any;
}

/** for_each_use() callback: count a use. */
static void count_one_use(void *ctx, int v, int role) {
    int *uses = (int *)ctx;

    (void)role;
    uses[v]++;
}

int cc_coalesce_moves(cc_ssa_function_t *f) {
    int *defs;
    int *uses;
    size_t i;
    size_t w;
    int changed = 0;

    if (f->value_count <= 0 || f->instr_count < 2) {
        return 0;
    }
    defs = count_defs(f);
    uses = (int *)calloc((size_t)f->value_count, sizeof(*uses));
    if (defs == NULL || uses == NULL) {
        free(defs);
        free(uses);
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        for_each_use(&f->instrs[i], count_one_use, uses);
    }
    /*
     * t = op ..., then v = t, t made once and read only there, of v's class, size and signedness: op writes v
     * itself.  It reads its operands before it writes, so op v, k into v is v's update (x += k, p++).
     */
    for (i = 0; i + 1 < f->instr_count; ++i) {
        cc_ssa_instr_t *op = &f->instrs[i];
        cc_ssa_instr_t *mv = &f->instrs[i + 1];
        int t = op->dst;

        if (mv->op != CC_SSA_MOV || mv->lhs != t || !is_value(f, t) || !is_value(f, mv->dst) || mv->dst == t ||
            defs[t] != 1 || uses[t] != 1 ||
            (op->op != CC_SSA_ADD && op->op != CC_SSA_SUB && op->op != CC_SSA_MUL && op->op != CC_SSA_AND &&
             op->op != CC_SSA_OR && op->op != CC_SSA_XOR && op->op != CC_SSA_SHL && op->op != CC_SSA_SHR &&
             op->op != CC_SSA_LOAD && op->op != CC_SSA_CONST) ||
            cc_ssa_value_class(f, t) != cc_ssa_value_class(f, mv->dst) ||
            cc_ssa_value_size(f, t) != cc_ssa_value_size(f, mv->dst) ||
            cc_ssa_value_is_unsigned(f, t) != cc_ssa_value_is_unsigned(f, mv->dst)) {
            continue;
        }
        op->dst = mv->dst;
        defs[mv->dst]++;
        cc_ssa_instr_free(mv);
        mv->op = CC_SSA_LABEL;   /* marked for removal below: a label of no number */
        mv->label = -2;
        changed = 1;
        i++;
    }
    if (changed) {
        w = 0;
        for (i = 0; i < f->instr_count; ++i) {
            if (f->instrs[i].op == CC_SSA_LABEL && f->instrs[i].label == -2) {
                continue;
            }
            f->instrs[w++] = f->instrs[i];
        }
        f->instr_count = w;
    }
    free(defs);
    free(uses);
    return changed;
}

/* ---- (f) a definition nothing reads ---- */

/** Whether @p in only makes its result: removed when nothing reads it. */
static int defines_only(const cc_ssa_instr_t *in) {
    switch (in->op) {
    case CC_SSA_CONST:
    case CC_SSA_MOV:
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
    case CC_SSA_CMP:
    case CC_SSA_LO32:
    case CC_SSA_HI32:
    case CC_SSA_PAIR:
    case CC_SSA_GADDR:
    case CC_SSA_STR:
        return 1;
    default:
        return 0;
    }
}

typedef struct {
    uint32_t *set;
} bits_ctx_t;

/** for_each_use() callback: the value is live. */
static void bits_add(void *ctx, int v, int role) {
    bits_ctx_t *c = (bits_ctx_t *)ctx;

    (void)role;
    c->set[v / 32] |= (uint32_t)1u << (v % 32);
}

/** Whether asm output @p k of @p in is written only: `=`, not `+`, and not a memory operand by its address. */
static int asm_output_write_only(const cc_ssa_instr_t *in, size_t k) {
    const char *c = in->asm_out_constraints != NULL ? in->asm_out_constraints[k] : NULL;

    return c != NULL && strchr(c, '+') == NULL && !CC_SSA_ASM_MEM_INDIRECT_P(in->asm_out_values[k]);
}

/**
 * Liveness back across @p in in @p cur: what it defines is not live before it, what it reads is.  An asm's
 * write-only outputs are definitions, not reads; its other operands are read.
 */
static void live_step(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, bits_ctx_t *bc) {
    size_t k;

    if (is_value(f, in->dst)) {
        bc->set[in->dst / 32] &= ~((uint32_t)1u << (in->dst % 32));
    }
    if (in->op != CC_SSA_ASM) {
        for_each_use(in, bits_add, bc);
        return;
    }
    for (k = 0; k < in->asm_out_count; ++k) {
        int o = in->asm_out_values[k];

        if (asm_output_write_only(in, k) && is_value(f, o)) {
            bc->set[o / 32] &= ~((uint32_t)1u << (o % 32));
        }
    }
    for (k = 0; k < in->asm_out_count + in->asm_in_count; ++k) {
        int o = k < in->asm_out_count ? in->asm_out_values[k] : in->asm_in_values[k - in->asm_out_count];

        if (k < in->asm_out_count && asm_output_write_only(in, k)) {
            continue;
        }
        if (CC_SSA_ASM_MEM_INDIRECT_P(o)) {
            o = CC_SSA_ASM_MEM_INDIRECT_DECODE(o);
        }
        if (is_value(f, o)) {
            bits_add(bc, o, 0);
        }
    }
}

int cc_dead_defs(cc_ssa_function_t *f) {
    cfg_t c;
    size_t nw;
    uint32_t *live_in = NULL;
    uint32_t *live_out = NULL;
    uint32_t *cur = NULL;
    unsigned char *pinned = NULL;
    unsigned char *dead = NULL;
    size_t i;
    size_t b;
    size_t w;
    int changed;
    int removed = 0;
    int rc = 0;

    if (f->value_count <= 0 || f->instr_count == 0) {
        return 0;
    }
    if (cfg_build(&c, f) != 0) {
        return -1;
    }
    nw = ((size_t)f->value_count + 31) / 32;
    for (b = 0; b < c.count; ++b) {
        if (c.opaque[b]) {
            /* an asm goto or a computed goto: where control goes is not known */
            cfg_free(&c);
            return 0;
        }
    }
    if ((unsigned long)c.count * nw > 8UL * 1024 * 1024) {
        /* too big a function to be worth the sets */
        cfg_free(&c);
        return 0;
    }
    live_in = (uint32_t *)calloc(c.count * nw, sizeof(uint32_t));
    live_out = (uint32_t *)calloc(c.count * nw, sizeof(uint32_t));
    cur = (uint32_t *)calloc(nw, sizeof(uint32_t));
    pinned = (unsigned char *)calloc((size_t)f->value_count, 1);
    dead = (unsigned char *)calloc(f->instr_count, 1);
    if (live_in == NULL || live_out == NULL || cur == NULL || pinned == NULL || dead == NULL) {
        rc = -1;
        goto done;
    }
    /*
     * a value whose address is taken is read through it, and an asm's inputs and read-write outputs as it
     * pleases: always live.  A write-only output is only defined there (live_step()).
     */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t k;

        if (in->op == CC_SSA_ADDR && is_value(f, in->lhs)) {
            pinned[in->lhs] = 1;
        }
        for (k = 0; in->op == CC_SSA_ASM && k < in->asm_out_count + in->asm_in_count; ++k) {
            int o = k < in->asm_out_count ? in->asm_out_values[k] : in->asm_in_values[k - in->asm_out_count];

            if (k < in->asm_out_count && asm_output_write_only(in, k)) {
                continue;
            }
            if (CC_SSA_ASM_MEM_INDIRECT_P(o)) {
                o = CC_SSA_ASM_MEM_INDIRECT_DECODE(o);
            }
            if (is_value(f, o)) {
                pinned[o] = 1;
            }
        }
    }
    /* live in = (live out - defined) + read, back from each block's end, until nothing changes */
    do {
        changed = 0;
        for (b = c.count; b-- > 0;) {
            uint32_t *out = live_out + b * nw;
            uint32_t *inb = live_in + b * nw;
            bits_ctx_t bc;
            int k;

            for (k = 0; k < 2; ++k) {
                int s = c.succ[2 * b + k];

                if (s >= 0) {
                    for (w = 0; w < nw; ++w) {
                        out[w] |= live_in[(size_t)s * nw + w];
                    }
                }
            }
            memcpy(cur, out, nw * sizeof(uint32_t));
            bc.set = cur;
            for (i = c.end[b]; i-- > c.first[b];) {
                live_step(f, &f->instrs[i], &bc);
            }
            for (w = 0; w < nw; ++w) {
                if (cur[w] != inb[w]) {
                    inb[w] = cur[w];
                    changed = 1;
                }
            }
        }
    } while (changed);
    /* a definition only of its result, whose result is not live after it: gone */
    for (b = 0; b < c.count; ++b) {
        bits_ctx_t bc;

        memcpy(cur, live_out + b * nw, nw * sizeof(uint32_t));
        bc.set = cur;
        for (i = c.end[b]; i-- > c.first[b];) {
            const cc_ssa_instr_t *in = &f->instrs[i];
            int d = in->dst;

            if (defines_only(in) && is_value(f, d) && !pinned[d] && (cur[d / 32] & ((uint32_t)1u << (d % 32))) == 0) {
                dead[i] = 1;
                removed = 1;
                continue;
            }
            live_step(f, in, &bc);
        }
    }
    if (removed) {
        size_t k = 0;

        for (i = 0; i < f->instr_count; ++i) {
            if (dead[i]) {
                cc_ssa_instr_free(&f->instrs[i]);
                continue;
            }
            f->instrs[k++] = f->instrs[i];
        }
        f->instr_count = k;
    }
done:
    free(live_in);
    free(live_out);
    free(cur);
    free(pinned);
    free(dead);
    cfg_free(&c);
    return rc < 0 ? -1 : removed;
}

/* ---- (e) a scalar slot made a value ---- */

/** The bits of the target's integer register, which a shift works on: 32 on i386, 64 on x86-64. */
static int word_bits(const cc_ssa_instr_t *in) {
    (void)in;
    return cc_options_current()->target == CC_TARGET_I386 ? 32 : 64;
}

/**
 * Whether @p in loads 1 or 2 bytes into a wider integer, extending them (zload or load): a char or short
 * local read as an int.  Up to a word: a wider result is a pair of words on i386.
 */
static int widening_load(const cc_ssa_function_t *f, const cc_ssa_instr_t *in) {
    uint32_t dsize;

    if (in->op != CC_SSA_LOAD || (in->imm != 1 && in->imm != 2) || !is_value(f, in->dst) ||
        cc_ssa_value_class(f, in->dst) != CC_VAL_I64) {
        return 0;
    }
    dsize = cc_ssa_value_size(f, in->dst);
    return dsize > (uint32_t)in->imm && 8 * dsize <= (uint32_t)word_bits(in);
}

int cc_promote_slots(cc_ssa_function_t *f) {
    addr_info_t a;
    long *size = NULL;         /* per slot: the size of its accesses, 0 for none yet */
    int *cls = NULL;           /* per slot: the class of the values stored and loaded */
    int *proto = NULL;         /* per slot: a value stored or loaded, whose signedness the new one takes */
    unsigned char *bad = NULL; /* per slot: not promoted */
    int *var = NULL;           /* per slot: the value it becomes, or -1 */
    int nold = f->value_count; /* the values before those made here, which the arrays cover */
    ilist_t out;               /* the instructions rewritten */
    size_t i;
    int s;
    int changed = 0;
    int rc = 0;

    memset(&out, 0, sizeof(out));
    if (f->value_count <= 0) {
        return 0;
    }
    /*
     * an asm goto does not stop it: a slot made a value is the same storage under another name, whatever the
     * blocks -- Linux's static keys put one in most functions, and every local of intel_pmu_init() stayed in
     * memory, 1.6 KiB of its frame
     */
    if (addr_info_build(&a, f, 0, 0) != 0) {
        return -1;
    }
    size = (long *)calloc((size_t)f->value_count, sizeof(*size));
    cls = (int *)calloc((size_t)f->value_count, sizeof(*cls));
    proto = (int *)calloc((size_t)f->value_count, sizeof(*proto));
    bad = (unsigned char *)calloc((size_t)f->value_count, 1);
    var = (int *)malloc((size_t)f->value_count * sizeof(*var));
    if (size == NULL || cls == NULL || proto == NULL || bad == NULL || var == NULL) {
        rc = -1;
        goto done;
    }
    for (s = 0; s < nold; ++s) {
        proto[s] = -1;
    }
    /*
     * each access: of the whole slot, directly, at one size, of a value of exactly that size (or a load that
     * extends it, widening_load()) and one class
     */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int v;

        if ((in->op != CC_SSA_LOAD && in->op != CC_SSA_STORE) || !is_value(f, in->lhs) || a.slot[in->lhs] < 0) {
            continue;
        }
        s = a.slot[in->lhs];
        v = in->op == CC_SSA_STORE ? in->rhs : in->dst;
        if (in->lhs != s || (in->imm != 1 && in->imm != 2 && in->imm != 4 && in->imm != 8) ||
            (long)in->imm != slot_size(&a, s) || (size[s] != 0 && size[s] != (long)in->imm) || !is_value(f, v) ||
            (cc_ssa_value_size(f, v) != (uint32_t)in->imm && !widening_load(f, in)) ||
            (size[s] != 0 && cls[s] != (int)cc_ssa_value_class(f, v))) {
            bad[s] = 1;
            continue;
        }
        size[s] = (long)in->imm;
        cls[s] = (int)cc_ssa_value_class(f, v);
        if (cc_ssa_value_size(f, v) == (uint32_t)in->imm) {
            proto[s] = v;
        }
    }
    for (s = 0; s < nold; ++s) {
        var[s] = -1;
        if (a.slot[s] == s && size[s] != 0 && !bad[s] && !a.escapes[s]) {
            int nv = cc_ssa_function_new_value(f, (cc_value_type_t)cls[s]);

            if (nv < 0) {
                rc = -1;
                goto done;
            }
            cc_ssa_value_set_size(f, nv, (uint32_t)size[s]);
            cc_ssa_value_set_unsigned(f, nv, proto[s] >= 0 ? cc_ssa_value_is_unsigned(f, proto[s]) : 0);
            var[s] = nv;
        }
    }
    /* a store sets the value, a load reads it; a load wider than the slot extends it */
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];

        if ((in->op == CC_SSA_LOAD || in->op == CC_SSA_STORE) && in->lhs >= 0 && in->lhs < nold &&
            a.slot[in->lhs] == in->lhs && var[in->lhs] >= 0) {
            if (in->op == CC_SSA_LOAD && widening_load(f, in)) {
                /* dst = (var << k) >> k, k the bits of the word above the slot's */
                int k = word_bits(in) - 8 * (int)in->imm;
                int t = cc_ssa_function_new_value(f, CC_VAL_I64);
                cc_ssa_instr_t sh = plain_instr(CC_SSA_SHL);

                if (t < 0) {
                    rc = -1;
                    goto done;
                }
                cc_ssa_value_set_size(f, t, cc_ssa_value_size(f, in->dst));
                cc_ssa_value_set_unsigned(f, t, in->is_unsigned);
                sh.dst = t;
                sh.lhs = var[in->lhs];
                sh.imm = k;
                if (ilist_put(&out, &sh) != 0) {
                    rc = -1;
                    goto done;
                }
                in->op = CC_SSA_SHR;
                in->lhs = t;
                in->imm = k;
            } else if (in->op == CC_SSA_STORE) {
                in->dst = var[in->lhs];
                in->lhs = in->rhs;
                in->op = CC_SSA_MOV;
                in->imm = 0;
            } else {
                in->lhs = var[in->lhs];
                in->op = CC_SSA_MOV;
                in->imm = 0;
            }
            in->rhs = -1;
            changed = 1;
        }
        if (ilist_put(&out, in) != 0) {
            rc = -1;
            goto done;
        }
    }
    if (changed) {
        free(f->instrs);
        f->instrs = out.at;
        f->instr_count = out.count;
        f->instr_cap = out.cap;
        out.at = NULL;
    }
done:
    if (rc < 0 && out.at != NULL) {
        /* the instructions copied to out are f's too: only the array goes */
        free(out.at);
        out.at = NULL;
    }
    free(out.at);
    free(size);
    free(cls);
    free(proto);
    free(bad);
    free(var);
    addr_info_free(&a);
    return rc < 0 ? -1 : changed;
}

int cc_optimize_slots(cc_ssa_function_t *f) {
    int r1 = forward_slot_copies(f);
    int r2 = r1 < 0 ? -1 : scalarize_slots(f);
    int r3 = r2 < 0 ? -1 : slot_value_sets(f);

    if (r1 < 0 || r2 < 0 || r3 < 0) {
        return -1;
    }
    return r1 > 0 || r2 > 0 || r3 > 0;
}
