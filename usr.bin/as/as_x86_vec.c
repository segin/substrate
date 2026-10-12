#include "as_x86_vec.h"

#include "as_parser.h"
#include "as_strmap.h"
#include "as_x86_regs.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The emitter of the instructions with a VEX or an EVEX prefix, and
 * the choice of a row of their table.  See as_x86_vec.h. */

typedef struct {
    uint8_t *out;
    size_t cap;
    size_t at;
    char *errbuf;
    size_t errbuf_sz;
} vec_ctx_t;

static void vec_err(vec_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

static int put8(vec_ctx_t *ctx, unsigned v) {
    if (ctx->at >= ctx->cap) {
        vec_err(ctx, "the instruction is too long");
        return -1;
    }
    ctx->out[ctx->at++] = (uint8_t)v;
    return 0;
}

static int put32(vec_ctx_t *ctx, uint32_t v) {
    return put8(ctx, v & 0xffu) != 0 || put8(ctx, (v >> 8) & 0xffu) != 0 || put8(ctx, (v >> 16) & 0xffu) != 0 ||
                   put8(ctx, (v >> 24) & 0xffu) != 0
               ? -1
               : 0;
}

static as_strmap_t first_row;
static int first_row_made;
/* The row of the same name after each, or the table's count: made
 * with first_row, and never let go. */
static size_t *next_row;

/* The first row of a name; the rows of one name stand together. */
static int find_rows(const char *name, size_t *first) {
    if (!first_row_made) {
        size_t i;

        next_row = (size_t *)malloc(as_x86_vec_table_count * sizeof(*next_row));
        if (next_row == NULL) {
            return -1;
        }
        for (i = 0; i < as_x86_vec_table_count; ++i) {
            size_t was;

            next_row[i] = as_x86_vec_table_count;
            if (!as_strmap_get(&first_row, as_x86_vec_table[i].name, &was) &&
                as_strmap_put(&first_row, as_x86_vec_table[i].name, i) != 0) {
                return -1;
            }
        }
        /* The rows of a name stand together, and where a name has more
         * of them further on -- the forms APX adds to an instruction
         * that had rows before -- they are another run of it, and are
         * read after the first. */
        for (i = 0; i < as_x86_vec_table_count; ++i) {
            size_t run = 0;

            if (i > 0 && strcmp(as_x86_vec_table[i - 1].name, as_x86_vec_table[i].name) == 0) {
                next_row[i - 1] = i;
            } else if (as_strmap_get(&first_row, as_x86_vec_table[i].name, &run) && run != i) {
                while (next_row[run] != as_x86_vec_table_count) {
                    run = next_row[run];
                }
                next_row[run] = i;
            }
        }
        first_row_made = 1;
    }
    return as_strmap_get(&first_row, name, first) ? 0 : -1;
}

/*
 * A name that is another instruction with a number before its
 * operands: `vcmpltps` is `vcmpps $1`, `vpclmulhqlqdq` is `vpclmulqdq
 * $1`, `vpcmpnltud` is `vpcmpud $5`.  0 with the instruction's name in
 * base[0 .. base_sz) and the number in *imm, or -1.
 */
static int alias_of(const char *name, char *base, size_t base_sz, int *imm) {
    static const char *const cmp[] = {
        "eq",    "lt",    "le",    "unord",   "neq",    "nlt",    "nle",    "ord",    "eq_uq", "nge",   "ngt",
        "false", "neq_oq", "ge",   "gt",      "true",   "eq_os",  "lt_oq",  "le_oq",  "unord_s", "neq_us", "nlt_uq",
        "nle_uq", "ord_s", "eq_us", "nge_uq", "ngt_uq", "false_os", "neq_os", "ge_oq", "gt_oq", "true_us",
    };
    static const struct {
        const char *word;
        int imm;
    } clmul[] = {{"lqlq", 0x00}, {"hqlq", 0x01}, {"lqhq", 0x10}, {"hqhq", 0x11}},
      icmp[] = {{"eq", 0}, {"lt", 1}, {"le", 2}, {"neq", 4}, {"nlt", 5}, {"nle", 6}};
    size_t n = strlen(name);
    size_t i;
    /* The same names without the `v` are SSE's: of the compares, the
     * first eight. */
    int sse = name[0] != 'v';

    if (sse) {
        if (n > 5 && strncmp(name, "cmp", 3) == 0) {
            const char *suffix = name + n - 2;

            if (strcmp(suffix, "ps") == 0 || strcmp(suffix, "pd") == 0 || strcmp(suffix, "ss") == 0 ||
                strcmp(suffix, "sd") == 0) {
                for (i = 0; i < 8; ++i) {
                    if (strlen(cmp[i]) == n - 5 && strncmp(name + 3, cmp[i], n - 5) == 0) {
                        snprintf(base, base_sz, "cmp%s", suffix);
                        *imm = (int)i;
                        return 0;
                    }
                }
            }
        }
        if (n == 12 && strncmp(name, "pclmul", 6) == 0 && strcmp(name + 10, "dq") == 0) {
            for (i = 0; i < sizeof(clmul) / sizeof(clmul[0]); ++i) {
                if (strncmp(name + 6, clmul[i].word, 4) == 0) {
                    snprintf(base, base_sz, "pclmulqdq");
                    *imm = clmul[i].imm;
                    return 0;
                }
            }
        }
        return -1;
    }
    if (n > 6 && strncmp(name, "vcmp", 4) == 0) {
        const char *suffix = name + n - 2;

        if (strcmp(suffix, "ps") == 0 || strcmp(suffix, "pd") == 0 || strcmp(suffix, "ss") == 0 ||
            strcmp(suffix, "sd") == 0 || strcmp(suffix, "ph") == 0 || strcmp(suffix, "sh") == 0) {
            for (i = 0; i < sizeof(cmp) / sizeof(cmp[0]); ++i) {
                if (strlen(cmp[i]) == n - 6 && strncmp(name + 4, cmp[i], n - 6) == 0) {
                    snprintf(base, base_sz, "vcmp%s", suffix);
                    *imm = (int)i;
                    return 0;
                }
            }
        }
    }
    if (n == 13 && strncmp(name, "vpclmul", 7) == 0 && strcmp(name + 11, "dq") == 0) {
        for (i = 0; i < sizeof(clmul) / sizeof(clmul[0]); ++i) {
            if (strncmp(name + 7, clmul[i].word, 4) == 0) {
                snprintf(base, base_sz, "vpclmulqdq");
                *imm = clmul[i].imm;
                return 0;
            }
        }
    }
    if (n > 6 && strncmp(name, "vpcom", 5) == 0) {
        /* XOP's compares, which have eight and name them otherwise. */
        static const char *const com[] = {"lt", "le", "gt", "ge", "eq", "neq", "false", "true"};
        const char *elem = name + n - 1;

        if (strchr("bwdq", *elem) == NULL) {
            return -1;
        }
        if (elem[-1] == 'u' && n > 7) {
            elem--;
        }
        for (i = 0; i < sizeof(com) / sizeof(com[0]); ++i) {
            if (strlen(com[i]) == (size_t)(elem - (name + 5)) && strncmp(name + 5, com[i], strlen(com[i])) == 0) {
                snprintf(base, base_sz, "vpcom%s", elem);
                *imm = (int)i;
                return 0;
            }
        }
        return -1;
    }
    if (n > 6 && strncmp(name, "vpcmp", 5) == 0) {
        /* The element's letter, with a `u` before it for unsigned. */
        const char *elem = name + n - 1;

        if (strchr("bwdq", *elem) == NULL) {
            return -1;
        }
        if (elem[-1] == 'u' && n > 7) {
            elem--;
        }
        for (i = 0; i < sizeof(icmp) / sizeof(icmp[0]); ++i) {
            if (strlen(icmp[i].word) == (size_t)(elem - (name + 5)) &&
                strncmp(name + 5, icmp[i].word, strlen(icmp[i].word)) == 0) {
                snprintf(base, base_sz, "vpcmp%s", elem);
                *imm = icmp[i].imm;
                return 0;
            }
        }
    }
    return -1;
}

int as_x86_vec_knows(const char *mnemonic) {
    char base[24];
    size_t first;
    int imm;

    if (mnemonic == NULL) {
        return 0;
    }
    /* (A name whose rows here are APX's forms of an instruction that
     * is assembled elsewhere is not one this table is asked about.) */
    if (find_rows(mnemonic, &first) == 0) {
        return (as_x86_vec_table[first].flags & AS_VEC_F_APX) == 0;
    }
    return alias_of(mnemonic, base, sizeof(base), &imm) == 0 && find_rows(base, &first) == 0;
}

int as_x86_vec_has_apx(const char *mnemonic, size_t op_count, int asked, int high) {
    size_t first;
    size_t i;

    if (mnemonic == NULL || find_rows(mnemonic, &first) != 0 || (as_x86_vec_table[first].flags & AS_VEC_F_APX) == 0) {
        return 0;
    }
    /* (Most statements of such a name are not APX's, and are told by
     * how many operands they have, and by whether an EVEX form was
     * asked for: `add` of two is none of these.) */
    for (i = first; i < as_x86_vec_table_count; i = next_row[i]) {
        unsigned flags = as_x86_vec_table[i].flags;

        if (strlen(as_x86_vec_table[i].ops) != op_count) {
            continue;
        }
        if ((flags & (AS_VEC_F_PROMO | AS_VEC_F_PROMOHI)) == 0 || asked || ((flags & AS_VEC_F_PROMOHI) != 0 && high)) {
            return 1;
        }
    }
    return 0;
}

static int is_vector_class(unsigned cls) {
    return cls == AS_X86_REGCLASS_XMM || cls == AS_X86_REGCLASS_YMM || cls == AS_X86_REGCLASS_ZMM;
}

/* Is the operand what the row's character asks? */
static int operand_is(const as_x86_operand_t *op, char want) {
    int reg = op->kind == AS_X86_OP_REG;
    int mem = op->kind == AS_X86_OP_MEM;
    int plain_mem = mem && op->u.mem.index_class == 0;
    unsigned cls = reg ? op->reg_class : 0;

    switch (want) {
    case 'x':
        return cls == AS_X86_REGCLASS_XMM;
    case 'y':
        return cls == AS_X86_REGCLASS_YMM;
    case 'z':
        return cls == AS_X86_REGCLASS_ZMM;
    case 'X':
        return cls == AS_X86_REGCLASS_XMM || plain_mem;
    case 'Y':
        return cls == AS_X86_REGCLASS_YMM || plain_mem;
    case 'Z':
        return cls == AS_X86_REGCLASS_ZMM || plain_mem;
    case 'k':
        return cls == AS_X86_REGCLASS_MASK;
    case 'K':
        return cls == AS_X86_REGCLASS_MASK || plain_mem;
    case 'd':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 32;
    case 'q':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 64;
    case 'D':
        return (cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 32) || plain_mem;
    case 'Q':
        return (cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 64) || plain_mem;
    case 'R':
        return cls == AS_X86_REGCLASS_GENERAL && (op->size_bits == 32 || op->size_bits == 64);
    case 'E':
        return (cls == AS_X86_REGCLASS_GENERAL && (op->size_bits == 32 || op->size_bits == 64)) || plain_mem;
    case 'm':
        return plain_mem;
    case '1':
        return mem && op->u.mem.index_class == AS_X86_REGCLASS_XMM;
    case '2':
        return mem && op->u.mem.index_class == AS_X86_REGCLASS_YMM;
    case '3':
        return mem && op->u.mem.index_class == AS_X86_REGCLASS_ZMM;
    case 'i':
        return op->kind == AS_X86_OP_IMM;
    case 'l':
        /* %cl, where it is a count. */
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 8 && (unsigned)op->u.reg == 1u;
    case 'o':
        /* The number one, which a shift has a form of its own for. */
        return op->kind == AS_X86_OP_IMM && !op->imm_symbolic && op->u.imm == 1;
    case 's':
        /* A byte that the instruction extends by its sign. */
        return op->kind == AS_X86_OP_IMM && !op->imm_symbolic && op->u.imm >= -128 && op->u.imm <= 127;
    case 'u':
        /* A byte that is a count, a place or a choice, and is never
         * written with a sign. */
        return op->kind == AS_X86_OP_IMM && (op->imm_symbolic || (op->u.imm >= 0 && op->u.imm <= 255));
    case 't':
        return cls == AS_X86_REGCLASS_TILE;
    case 'B':
        return cls == AS_X86_REGCLASS_BOUND;
    case 'h':
        /* (Not %ah to %bh, nor %spl to %dil, which a REX prefix tells
         * apart: no row that takes a byte has needed them.) */
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 8 &&
               ((unsigned)op->u.reg < 4u || (unsigned)op->u.reg > 7u);
    case 'n':
        return cls == AS_X86_REGCLASS_MMX;
    case 'N':
        return cls == AS_X86_REGCLASS_MMX || plain_mem;
    case 'w':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 16;
    case 'W':
        return (cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 16) || plain_mem;
    case '0':
        return cls == AS_X86_REGCLASS_XMM && (unsigned)op->u.reg == 0u;
    /* The registers `monitor` and `mwait` are told things in, which may
     * be written and are not encoded: %eax, %ecx, %edx, or in 64-bit
     * code -- where no other has 64-bit registers -- those of 64 bits. */
    case 'a':
        return cls == AS_X86_REGCLASS_GENERAL && (op->size_bits == 32 || op->size_bits == 64) &&
               (unsigned)op->u.reg == 0u;
    case 'c':
        return cls == AS_X86_REGCLASS_GENERAL && (op->size_bits == 32 || op->size_bits == 64) &&
               (unsigned)op->u.reg == 1u;
    case 'e':
        return cls == AS_X86_REGCLASS_GENERAL && (op->size_bits == 32 || op->size_bits == 64) &&
               (unsigned)op->u.reg == 2u;
    case 'b':
        return cls == AS_X86_REGCLASS_GENERAL && (op->size_bits == 32 || op->size_bits == 64) &&
               (unsigned)op->u.reg == 3u;
    /* And the one an address is in: %rax, %eax or %ax, of which the one
     * that is not the mode's own asks for the address-size prefix. */
    case 'A':
        return cls == AS_X86_REGCLASS_GENERAL && (unsigned)op->u.reg == 0u &&
               (op->size_bits == 16 || op->size_bits == 32 || op->size_bits == 64);
    default:
        return 0;
    }
}

static int row_takes(const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq) {
    size_t i;

    if (strlen(t->ops) != rq->op_count) {
        return 0;
    }
    for (i = 0; i < rq->op_count; ++i) {
        if (!operand_is(&rq->ops[i], t->ops[i])) {
            return 0;
        }
    }
    return 1;
}

/* What the statement says that only an EVEX prefix can. */
static int needs_evex(const as_x86_vec_req_t *rq) {
    size_t i;

    if (rq->have_opmask || rq->zeroing || rq->broadcast != 0 || rq->sae || rq->rounding >= 0 ||
        (rq->pseudo & AS_PSEUDO_EVEX) != 0) {
        return 1;
    }
    for (i = 0; i < rq->op_count; ++i) {
        const as_x86_operand_t *op = &rq->ops[i];

        if (op->kind == AS_X86_OP_REG && is_vector_class(op->reg_class) && (unsigned)op->u.reg > 15u) {
            return 1;
        }
        if (op->kind == AS_X86_OP_MEM && op->u.mem.index_class != 0 && (unsigned)op->u.mem.index > 15u) {
            return 1;
        }
    }
    return 0;
}

/* Whether a statement has one of APX's general registers, %r16 to
 * %r31, which a VEX prefix has no bits for. */
static int has_high_general(const as_x86_vec_req_t *rq) {
    size_t i;

    for (i = 0; i < rq->op_count; ++i) {
        const as_x86_operand_t *op = &rq->ops[i];

        if (op->kind == AS_X86_OP_REG && op->reg_class == AS_X86_REGCLASS_GENERAL && (unsigned)op->u.reg > 15u) {
            return 1;
        }
        if (op->kind == AS_X86_OP_MEM && op->u.mem.addr_bits != 16u && !op->u.mem.rip_relative &&
            ((op->u.mem.has_base && (unsigned)op->u.mem.base > 15u) ||
             (op->u.mem.has_index && op->u.mem.index_class == 0 && (unsigned)op->u.mem.index > 15u))) {
            return 1;
        }
    }
    return 0;
}

/* The operand of a role, or NULL. */
static const as_x86_operand_t *role_op(const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq, char role) {
    const char *p = strchr(t->roles, role);

    return p != NULL ? &rq->ops[p - t->roles] : NULL;
}

static unsigned scale_bits(unsigned scale) {
    return scale == 8 ? 3u : scale == 4 ? 2u : scale == 2 ? 1u : 0u;
}

/*
 * ModRM, SIB and the displacement of `rm` with `reg` in ModRM.reg.
 * *x and *b are the fourth bits of the index and the base (or of a
 * register in r/m), *x4 the fifth of a register in r/m, *v4 that of a
 * vector index.
 *
 * `disp8_n` is what a one-byte displacement is multiplied by: 1 under
 * VEX, and under EVEX the bytes the instruction reads at the address.
 * 0 is a size not known, and then there is no one-byte displacement.
 */
static int put_modrm(vec_ctx_t *ctx, const as_x86_vec_req_t *rq, unsigned reg, const as_x86_operand_t *rm,
                     unsigned disp8_n, int force_sib, unsigned *x, unsigned *b, unsigned *x4, unsigned *v4) {
    const as_x86_mem_t *m;
    unsigned regbits = (reg & 7u) << 3;
    unsigned mod;
    unsigned base;
    int32_t disp;
    int disp8_ok;
    int32_t disp8 = 0;

    if (rm->kind == AS_X86_OP_REG) {
        *b = ((unsigned)rm->u.reg >> 3) & 1u;
        *x4 = ((unsigned)rm->u.reg >> 4) & 1u;
        return put8(ctx, 0xc0u | regbits | ((unsigned)rm->u.reg & 7u));
    }
    m = &rm->u.mem;
    if (m->addr_bits == 16u) {
        uint8_t bytes[3];
        size_t n = 0;
        size_t k;

        /* (A one-byte displacement only where it is the bytes it says.) */
        if (as_x86_modrm16(m, reg & 7u, disp8_n == 1u && (rq->pseudo & AS_PSEUDO_DISP32) == 0, bytes, &n) != 0) {
            vec_err(ctx, "not an address of 16-bit registers");
            return -1;
        }
        for (k = 0; k < n; ++k) {
            if (put8(ctx, bytes[k]) != 0) {
                return -1;
            }
        }
        return 0;
    }
    if (m->scale != 0 && m->scale != 1 && m->scale != 2 && m->scale != 4 && m->scale != 8) {
        vec_err(ctx, "a scale of %u", m->scale);
        return -1;
    }
    disp = m->has_disp ? m->disp : 0;
    if (m->rip_relative) {
        if (rq->mode != 64u) {
            vec_err(ctx, "%%rip is an address of 64-bit code");
            return -1;
        }
        return put8(ctx, regbits | 5u) != 0 || put32(ctx, (uint32_t)disp) != 0 ? -1 : 0;
    }
    if (m->has_index) {
        *x = ((unsigned)m->index >> 3) & 1u;
        *v4 = ((unsigned)m->index >> 4) & 1u;
    }
    if (!m->has_base) {
        /* No base: four bytes of displacement always.  With no index
         * either it is an absolute address, which 64-bit code writes
         * with a SIB byte, the form without being %rip's. */
        if (!m->has_index && rq->mode != 64u) {
            return put8(ctx, regbits | 5u) != 0 || put32(ctx, (uint32_t)disp) != 0 ? -1 : 0;
        }
        return put8(ctx, regbits | 4u) != 0 ||
                       put8(ctx, (scale_bits(m->scale) << 6) | (m->has_index ? ((unsigned)m->index & 7u) << 3 : 0x20u) |
                                         5u) != 0 ||
                       put32(ctx, (uint32_t)disp) != 0
                   ? -1
                   : 0;
    }
    base = (unsigned)m->base;
    *b = (base >> 3) & 1u;
    disp8_ok = 0;
    if (disp8_n != 0 && disp % (int32_t)disp8_n == 0) {
        disp8 = disp / (int32_t)disp8_n;
        disp8_ok = disp8 >= -128 && disp8 <= 127;
    }
    if (m->force_disp32 || (rq->pseudo & AS_PSEUDO_DISP32) != 0) {
        mod = 2;
    } else if ((rq->pseudo & AS_PSEUDO_DISP8) != 0 && disp8_ok) {
        mod = 1;
    } else if (disp == 0 && (base & 7u) != 5u) {
        mod = 0;
    } else if (disp == 0) {
        /* %rbp and %r13 with nothing are the form with no base; they
         * are given a displacement of nothing, times anything. */
        mod = 1;
        disp8 = 0;
    } else if (disp8_ok) {
        mod = 1;
    } else {
        mod = 2;
    }
    if (m->has_index || (base & 7u) == 4u || force_sib) {
        if (put8(ctx, (mod << 6) | regbits | 4u) != 0 ||
            put8(ctx, (scale_bits(m->scale) << 6) | (m->has_index ? ((unsigned)m->index & 7u) << 3 : 0x20u) |
                              (base & 7u)) != 0) {
            return -1;
        }
    } else if (put8(ctx, (mod << 6) | regbits | (base & 7u)) != 0) {
        return -1;
    }
    if (mod == 1) {
        return put8(ctx, (unsigned)disp8 & 0xffu);
    }
    return mod == 2 ? put32(ctx, (uint32_t)disp) : 0;
}

/* A register of a role: its number, 0 if the role has none. */
static unsigned role_reg(const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq, char role) {
    const as_x86_operand_t *op = role_op(t, rq, role);

    return op != NULL && op->kind == AS_X86_OP_REG ? (unsigned)op->u.reg : 0u;
}

/*
 * Write the row for the statement.  0, or -1 with the reason.
 */
static int emit_row(vec_ctx_t *ctx, const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq) {
    const as_x86_operand_t *rm = role_op(t, rq, 'm');
    const as_x86_operand_t *vop = role_op(t, rq, 'v');
    const as_x86_operand_t *imm = role_op(t, rq, 'i');
    const as_x86_operand_t *is4 = role_op(t, rq, '4');
    unsigned reg = (t->flags & AS_VEC_F_REGRM) != 0 ? role_reg(t, rq, 'm')
                   : strchr(t->roles, 'r') != NULL  ? role_reg(t, rq, 'r')
                                                    : (t->flags & 7u);
    unsigned vvvv = vop != NULL ? (unsigned)vop->u.reg : 0u;
    unsigned x = 0;
    unsigned b = 0;
    unsigned x4 = 0;
    unsigned v4 = 0;
    unsigned b4 = 0;
    unsigned xg4 = 0;
    int high = 0;
    unsigned w = t->w == AS_VEC_W1 ? 1u : 0u;
    unsigned len = t->len == AS_VEC_LIG ? 0u : t->len;
    uint8_t tail[16];
    vec_ctx_t tc = *ctx;
    size_t prefix;
    size_t i;

    if (rm != NULL && rm->kind == AS_X86_OP_MEM && rm->u.mem.rip_relative && (t->flags & AS_VEC_F_NORIP) != 0) {
        vec_err(ctx, "'%s' takes no address from %%rip", t->name);
        return -1;
    }
    if (rm == NULL && t->ops[0] != '\0' && t->enc != AS_VEC_LEGACY && (t->flags & AS_VEC_F_MODRM) == 0) {
        vec_err(ctx, "internal: a row with nothing in r/m");
        return -1;
    }
    if (rm == NULL && t->enc == AS_VEC_LEGACY) {
        /* No operand in ModRM: the opcode, and the byte after it that
         * the row gives, if it gives one (`monitor` is 0F 01 C8). */
        static const uint8_t pp_byte[] = {0, 0x66, 0xf3, 0xf2};
        const char *addr = strchr(t->ops, 'A');
        size_t k;

        for (k = 0; k < rq->op_count; ++k) {
            if (rq->ops[k].kind == AS_X86_OP_REG && rq->ops[k].size_bits == 64 && rq->mode != 64u) {
                vec_err(ctx, "a 64-bit register in %u-bit code", rq->mode);
                return -1;
            }
        }
        if (addr != NULL) {
            unsigned bits = rq->ops[addr - t->ops].size_bits;
            unsigned other = rq->mode == 64u ? 32u : rq->mode == 32u ? 16u : 32u;

            if (bits != rq->mode && bits != other) {
                vec_err(ctx, "an address in a %u-bit register in %u-bit code", bits, rq->mode);
                return -1;
            }
            if (bits == other && put8(ctx, 0x67u) != 0) {
                return -1;
            }
        }
        if ((t->pp != AS_VEC_PP_NONE && put8(ctx, pp_byte[t->pp]) != 0) ||
            (t->map != AS_VEC_MAP_ONE && put8(ctx, 0x0fu) != 0) ||
            (t->map == AS_VEC_MAP_0F38 && put8(ctx, 0x38u) != 0) ||
            (t->map == AS_VEC_MAP_0F3A && put8(ctx, 0x3au) != 0) || put8(ctx, t->opcode) != 0 ||
            (t->disp8_n != 0 && put8(ctx, t->disp8_n) != 0)) {
            return -1;
        }
        /* And a number after that, if the row has one (`xabort $1`). */
        {
            const as_x86_operand_t *imm = role_op(t, rq, 'i');

            if (imm != NULL && imm->imm_symbolic) {
                vec_err(ctx, "'%s' takes a number that is known here", t->name);
                return -1;
            }
            return imm != NULL ? put8(ctx, (unsigned)imm->u.imm & 0xffu) : 0;
        }
    }
    if (rm == NULL && (t->flags & AS_VEC_F_MODRM) == 0) {
        /* No operand, and so no ModRM: vzeroupper and vzeroall. */
        return put8(ctx, 0xc5u) != 0 || put8(ctx, 0xf8u | (len << 2) | t->pp) != 0 || put8(ctx, t->opcode) != 0 ? -1
                                                                                                             : 0;
    }
    if ((t->flags & AS_VEC_F_NO64) != 0 && rq->mode != 64u) {
        w = 0;
    }
    /* The registers of 16- and 32-bit code are the first eight, and an
     * address is not of 64-bit registers; nor in 64-bit code of 16-bit
     * ones. */
    for (i = 0; i < rq->op_count; ++i) {
        const as_x86_operand_t *op = &rq->ops[i];

        if (op->kind == AS_X86_OP_MEM && op->u.mem.addr_bits != 0 &&
            (op->u.mem.addr_bits == 64u) != (rq->mode == 64u) &&
            !(op->u.mem.addr_bits == 32u || (op->u.mem.addr_bits == 16u && rq->mode != 64u))) {
            vec_err(ctx, "an address of %u-bit registers in %u-bit code", op->u.mem.addr_bits, rq->mode);
            return -1;
        }
    }
    if (rq->mode != 64u) {
        for (i = 0; i < rq->op_count; ++i) {
            const as_x86_operand_t *op = &rq->ops[i];

            if ((op->kind == AS_X86_OP_REG && op->reg_class != AS_X86_REGCLASS_MASK && (unsigned)op->u.reg > 7u) ||
                (op->kind == AS_X86_OP_REG && op->reg_class == AS_X86_REGCLASS_GENERAL && op->size_bits == 64) ||
                (op->kind == AS_X86_OP_MEM && ((op->u.mem.has_base && (unsigned)op->u.mem.base > 7u) ||
                                               (op->u.mem.has_index && (unsigned)op->u.mem.index > 7u)))) {
                vec_err(ctx, "a register of 64-bit code");
                return -1;
            }
        }
    }

    tc.out = tail;
    tc.cap = sizeof(tail);
    tc.at = 0;
    {
        unsigned n = 1;

        if (t->enc == AS_VEC_EVEX) {
            n = rq->broadcast != 0 ? t->bcst : t->disp8_n;
        }
        if (put8(&tc, t->opcode) != 0) {
            return -1;
        }
        if (rm == NULL) {
            /* (AS_VEC_F_MODRM: the register, if there is one, in reg.) */
            if (put8(&tc, 0xc0u | ((reg & 7u) << 3)) != 0) {
                return -1;
            }
        } else if (put_modrm(&tc, rq, reg, rm, n, (t->flags & AS_VEC_F_SIB) != 0, &x, &b, &x4, &v4) != 0) {
            return -1;
        }
    }
    if ((t->flags & AS_VEC_F_SUFFIX) != 0) {
        if (put8(&tc, t->disp8_n) != 0) {
            return -1;
        }
    } else if (is4 != NULL) {
        /* The register in the high four bits; the low four hold a
         * number where the row has one too (XOP's vpermil2), and else
         * nothing. */
        unsigned low = 0;

        if (imm != NULL) {
            if (imm->imm_symbolic || imm->u.imm < 0 || imm->u.imm > 15) {
                vec_err(ctx, "'%s' takes a number of four bits", t->name);
                return -1;
            }
            low = (unsigned)imm->u.imm;
        }
        if (put8(&tc, (((unsigned)is4->u.reg & 0xfu) << 4) | low) != 0) {
            return -1;
        }
    } else if (imm != NULL && (t->flags & AS_VEC_F_IMM16) != 0 && imm->imm_symbolic) {
        /* (A symbol: two bytes for the linker.) */
        if (put8(&tc, 0) != 0 || put8(&tc, 0) != 0) {
            return -1;
        }
    } else if (imm != NULL && (t->flags & AS_VEC_F_IMM16) != 0) {
        if (imm->u.imm < -32768 || imm->u.imm > 65535) {
            vec_err(ctx, "the number %lld is not of two bytes", (long long)imm->u.imm);
            return -1;
        }
        if (put8(&tc, (unsigned)imm->u.imm & 0xffu) != 0 || put8(&tc, ((unsigned)imm->u.imm >> 8) & 0xffu) != 0) {
            return -1;
        }
    } else if (imm != NULL && (t->flags & AS_VEC_F_IMM32) != 0 && imm->imm_symbolic) {
        /* (A symbol: four bytes for the linker, which are the
         * instruction's last.  Of APX's rows; the others take a number
         * that is known here.) */
        if ((t->flags & AS_VEC_F_APX) == 0) {
            vec_err(ctx, "'%s' takes a number that is known here", t->name);
            return -1;
        }
        if (put32(&tc, 0) != 0) {
            return -1;
        }
    } else if (imm != NULL && (t->flags & AS_VEC_F_IMM32) != 0) {
        /* (Of 64 bits the four bytes are extended by their sign.) */
        if (imm->u.imm < -2147483648ll || imm->u.imm > (t->w == AS_VEC_W1 ? 2147483647ll : 4294967295ll)) {
            vec_err(ctx, "the number %lld is not of four bytes", (long long)imm->u.imm);
            return -1;
        }
        if (put32(&tc, (uint32_t)imm->u.imm) != 0) {
            return -1;
        }
    } else if (imm != NULL) {
        if (!imm->imm_symbolic && (imm->u.imm < -128 || imm->u.imm > 255)) {
            vec_err(ctx, "the number %lld is not of one byte", (long long)imm->u.imm);
            return -1;
        }
        if (put8(&tc, (unsigned)imm->u.imm & 0xffu) != 0) {
            return -1;
        }
    }

    /*
     * APX's general registers, %r16 to %r31: the fifth bit of one in
     * r/m or of a base is B4, of an index X4, and of one in ModRM.reg
     * R4 -- which an EVEX prefix has places for that are not where a
     * vector register's are, and an instruction with no prefix of that
     * kind says in a REX2 one.
     */
    {
        const as_x86_operand_t *rop = strchr(t->roles, 'r') != NULL ? role_op(t, rq, 'r') : NULL;

        if (rm != NULL && rm->kind == AS_X86_OP_REG && rm->reg_class == AS_X86_REGCLASS_GENERAL) {
            b4 = x4;
            x4 = 0;
        } else if (rm != NULL && rm->kind == AS_X86_OP_MEM && rm->u.mem.addr_bits != 16u && !rm->u.mem.rip_relative) {
            b4 = rm->u.mem.has_base ? ((unsigned)rm->u.mem.base >> 4) & 1u : 0u;
            xg4 = rm->u.mem.has_index && rm->u.mem.index_class == 0 ? ((unsigned)rm->u.mem.index >> 4) & 1u : 0u;
        }
        high = b4 != 0 || xg4 != 0 ||
               (rop != NULL && rop->kind == AS_X86_OP_REG && rop->reg_class == AS_X86_REGCLASS_GENERAL &&
                (unsigned)rop->u.reg > 15u) ||
               (vop != NULL && vop->kind == AS_X86_OP_REG && vop->reg_class == AS_X86_REGCLASS_GENERAL &&
                (unsigned)vop->u.reg > 15u);
    }
    if (high && rq->mode != 64u) {
        vec_err(ctx, "'%s': %%r16 to %%r31 are registers of 64-bit code", t->name);
        return -1;
    }

    if (t->enc == AS_VEC_LEGACY) {
        /* No VEX: the prefix that is part of the opcode, REX if a bit
         * of it is wanted, the escape bytes.  Before them the operand
         * size, where the row's is not the mode's. */
        static const uint8_t pp_byte[] = {0, 0x66, 0xf3, 0xf2};
        unsigned r = (reg >> 3) & 1u;
        unsigned rex = (w << 3) | (r << 2) | (x << 1) | b | (rq->explicit_rex ? 0x40u | rq->rex_bits : 0u);
        int size16 = (t->flags & AS_VEC_F_OP16) != 0;
        int size32 = (t->flags & AS_VEC_F_OP32) != 0;

        if ((size16 && rq->mode != 16u) || (size32 && rq->mode == 16u)) {
            if (put8(ctx, 0x66u) != 0) {
                return -1;
            }
        }
        if (t->pp != AS_VEC_PP_NONE && put8(ctx, pp_byte[t->pp]) != 0) {
            return -1;
        }
        if (rex != 0 && rq->mode != 64u) {
            vec_err(ctx, "a REX prefix outside 64-bit code");
            return -1;
        }
        if (high) {
            /* REX2: the bits of REX, the fifth bits above them, and
             * whether the opcode is of the two-byte map -- which it
             * says instead of the escape byte, and has no way to say
             * of the others. */
            /* (Nor have the instructions that save and restore the
             * state of the processor, by its makers' say.) */
            if ((t->map != AS_VEC_MAP_ONE && t->map != AS_VEC_MAP_0F) || strncmp(t->name, "xsave", 5) == 0 ||
                strncmp(t->name, "xrstor", 6) == 0) {
                vec_err(ctx, "'%s' has no prefix that says %%r16 to %%r31", t->name);
                return -1;
            }
            if (put8(ctx, 0xd5u) != 0 ||
                put8(ctx, ((t->map == AS_VEC_MAP_0F ? 1u : 0u) << 7) | (((reg >> 4) & 1u) << 6) | (xg4 << 5) |
                                  (b4 << 4) | (rex & 0xfu)) != 0) {
                return -1;
            }
        } else {
            if (rex != 0 && put8(ctx, 0x40u | (rex & 0xfu)) != 0) {
                return -1;
            }
            if ((t->map != AS_VEC_MAP_ONE && put8(ctx, 0x0fu) != 0) ||
                (t->map == AS_VEC_MAP_0F38 && put8(ctx, 0x38u) != 0) ||
                (t->map == AS_VEC_MAP_0F3A && put8(ctx, 0x3au) != 0)) {
                return -1;
            }
        }
    } else if (t->enc == AS_VEC_VEX || t->enc == AS_VEC_XOP) {
        unsigned r = (reg >> 3) & 1u;

        if (high) {
            vec_err(ctx, "'%s' has no prefix that says %%r16 to %%r31", t->name);
            return -1;
        }
        int two = t->enc == AS_VEC_VEX && t->map == AS_VEC_MAP_0F && !w && !x && !b &&
                  (rq->pseudo & AS_PSEUDO_VEX3) == 0 && (t->flags & AS_VEC_F_VEX3) == 0;

        if (two) {
            if (put8(ctx, 0xc5u) != 0 ||
                put8(ctx, ((r ^ 1u) << 7) | ((~vvvv & 0xfu) << 3) | (len << 2) | t->pp) != 0) {
                return -1;
            }
        } else if (put8(ctx, t->enc == AS_VEC_XOP ? 0x8fu : 0xc4u) != 0 ||
                   put8(ctx, ((r ^ 1u) << 7) | ((x ^ 1u) << 6) | ((b ^ 1u) << 5) | t->map) != 0 ||
                   put8(ctx, (w << 7) | ((~vvvv & 0xfu) << 3) | (len << 2) | t->pp) != 0) {
            return -1;
        }
    } else {
        unsigned r = (reg >> 3) & 1u;
        unsigned r4 = (reg >> 4) & 1u;
        unsigned bbit = 0;
        unsigned ll = len;
        unsigned vhi;
        unsigned low;

        /* A register in r/m has its fifth bit where an index has its
         * fourth. */
        if (rm->kind == AS_X86_OP_REG && rm->reg_class != AS_X86_REGCLASS_GENERAL) {
            x = x4;
        }
        /* V' is the fifth bit of vvvv, or of a vector index. */
        vhi = rm->kind == AS_X86_OP_MEM && rm->u.mem.index_class != 0 ? v4 : (vvvv >> 4) & 1u;
        if ((t->flags & AS_VEC_F_ND) != 0) {
            /* (APX: the bit says vvvv is where the result goes.) */
            bbit = 1;
            ll = 0;
        } else if (rq->broadcast != 0) {
            bbit = 1;
        } else if (rq->rounding >= 0) {
            bbit = 1;
            ll = (unsigned)rq->rounding & 3u;
        } else if (rq->sae) {
            /* (The length is 512 bits or a scalar's, and the bits that
             * would say so are written 0, as GNU as writes them.) */
            bbit = 1;
            ll = 0;
        }
        low = ((vhi ^ 1u) << 3) | (rq->opmask & 7u);
        if ((t->flags & AS_VEC_F_NF) != 0) {
            /* (APX: no flags are written, said where a mask would be.) */
            low |= 4u;
        }
        if ((t->flags & AS_VEC_F_SCC) != 0) {
            /* (APX's conditional compare: the condition where a mask
             * and V' would be, and the flags to leave if it does not
             * hold where vvvv would -- none, which is written 0.) */
            low = t->bcst & 0xfu;
            vvvv = ~(unsigned)rq->dfv & 0xfu;
        }
        if (put8(ctx, 0x62u) != 0 ||
            put8(ctx, ((r ^ 1u) << 7) | ((x ^ 1u) << 6) | ((b ^ 1u) << 5) | ((r4 ^ 1u) << 4) | (b4 << 3) | t->map) !=
                0 ||
            put8(ctx, (w << 7) | ((~vvvv & 0xfu) << 3) | ((xg4 ^ 1u) << 2) | t->pp) != 0 ||
            put8(ctx, ((rq->zeroing ? 1u : 0u) << 7) | (ll << 5) | (bbit << 4) | low) != 0) {
            return -1;
        }
    }
    prefix = ctx->at;
    for (i = 0; i < tc.at; ++i) {
        if (put8(ctx, tail[i]) != 0) {
            return -1;
        }
    }
    (void)prefix;
    return 0;
}

/* The elements a row reads from memory, for a broadcast's count: what
 * it reads there whole, over one element. */
static unsigned row_elements(const as_x86_vec_tmpl_t *t) {
    return t->bcst != 0 ? t->disp8_n / t->bcst : 0u;
}

static const char needs_v3[] = "requires -march=x86-64-v3 or higher";

/*
 * Whether the row may be the statement's, its operands being the
 * row's: the prefix it has, and what is written in braces.  0 if it
 * may, and else why not, in *why -- the reason kept is that of the
 * last row refused, which is the one that came nearest.
 */
static int row_refuses(const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq, int evex, const char **why) {
    const as_x86_operand_t *rm = role_op(t, rq, 'm');
    int mem = rm != NULL && rm->kind == AS_X86_OP_MEM;

    if (t->enc == AS_VEC_LEGACY) {
        if (evex) {
            *why = "takes no mask, broadcast or rounding, and no register past the fifteenth";
            return 1;
        }
        /* (REX2 says %r16 to %r31 for an opcode of one byte or two:
         * one of the maps past them is left to the EVEX row of the
         * name, where there is one.) */
        if (t->map != AS_VEC_MAP_ONE && t->map != AS_VEC_MAP_0F && has_high_general(rq)) {
            *why = "has no prefix that says %r16 to %r31 with these operands";
            return 1;
        }
        if ((rq->pseudo & (AS_PSEUDO_EVEX | AS_PSEUDO_NF)) != 0) {
            *why = "has no EVEX form with these operands";
            return 1;
        }
        if ((rq->pseudo & (AS_PSEUDO_VEX | AS_PSEUDO_VEX3)) != 0) {
            *why = "has no VEX form: the instruction with one has another name";
            return 1;
        }
        return 0;
    }
    if (rq->explicit_rex) {
        *why = "has a VEX or EVEX prefix, and takes no REX one";
        return 1;
    }
    if (t->enc == AS_VEC_VEX || t->enc == AS_VEC_XOP) {
        if ((rq->pseudo & AS_PSEUDO_EVEX) != 0) {
            *why = "has no EVEX form with these operands";
            return 1;
        }
        if (evex) {
            *why = "has no AVX-512 form with these operands";
            return 1;
        }
        if (has_high_general(rq)) {
            *why = "has no prefix that says %r16 to %r31 with these operands";
            return 1;
        }
        if (rq->mode == 64u && rq->level < 3u) {
            *why = needs_v3;
            return 1;
        }
        return 0;
    }
    if ((rq->pseudo & (AS_PSEUDO_VEX | AS_PSEUDO_VEX3)) != 0) {
        *why = "has no VEX form with these operands";
        return 1;
    }
    if (rq->mode == 64u && rq->level < 4u) {
        *why = "requires -march=x86-64-v4 or higher";
        return 1;
    }
    if (rq->have_opmask && rq->opmask == 0) {
        *why = "has %k0 for a write mask, which is none";
        return 1;
    }
    if ((t->flags & AS_VEC_F_BCST) != 0 && mem && rq->broadcast == 0) {
        *why = "does not say how much of memory it reads: the name wants a suffix";
        return 1;
    }
    if (rq->have_opmask && (t->flags & (AS_VEC_F_MASK | AS_VEC_F_MASKREQ)) == 0) {
        *why = "takes no write mask";
        return 1;
    }
    if (rq->zeroing && (t->flags & AS_VEC_F_ZERO) == 0) {
        *why = "takes no {z}";
        return 1;
    }
    if (rq->zeroing && (!rq->have_opmask || rq->opmask == 0)) {
        *why = "has {z} and no write mask";
        return 1;
    }
    if ((t->flags & AS_VEC_F_MASKREQ) != 0 && (!rq->have_opmask || rq->opmask == 0)) {
        *why = "must have a write mask, and not %k0";
        return 1;
    }
    if (rq->broadcast != 0) {
        if (t->bcst == 0 || !mem) {
            *why = "takes no broadcast";
            return 1;
        }
        if (rq->broadcast != row_elements(t)) {
            *why = "has a broadcast of another count than its vector's";
            return 1;
        }
    }
    if (rq->rounding >= 0 && ((t->flags & AS_VEC_F_ER) == 0 || mem)) {
        *why = "takes no rounding here";
        return 1;
    }
    if (rq->sae && rq->rounding < 0 && ((t->flags & AS_VEC_F_SAE) == 0 || mem)) {
        *why = "takes no {sae} here";
        return 1;
    }
    return 0;
}

int as_x86_vec_takes_data16(const char *mnemonic) {
    size_t i;

    if (mnemonic == NULL || find_rows(mnemonic, &i) != 0) {
        return 0;
    }
    for (; i < as_x86_vec_table_count; i = next_row[i]) {
        /* (The forms APX adds to the name are not what a prefix is
         * written before.) */
        if ((as_x86_vec_table[i].flags & (AS_VEC_F_PROMO | AS_VEC_F_PROMOHI | AS_VEC_F_APX)) != 0) {
            continue;
        }
        if (as_x86_vec_table[i].enc != AS_VEC_LEGACY || strpbrk(as_x86_vec_table[i].ops, "xyzXYZnN0123") != NULL) {
            return 0;
        }
    }
    return 1;
}

int as_x86_vec_takes_lock(const char *mnemonic) {
    size_t i;

    return mnemonic != NULL && find_rows(mnemonic, &i) == 0 && (as_x86_vec_table[i].flags & AS_VEC_F_LOCK) != 0;
}

int as_x86_vec_encode(const as_x86_vec_req_t *rq, uint8_t *out, size_t out_cap, size_t *out_len, char *errbuf,
                      size_t errbuf_sz) {
    const char *why = "does not take these operands";
    const char *lower = NULL;
    as_x86_operand_t with_imm[6];
    as_x86_vec_req_t aliased;
    char base[24];
    vec_ctx_t ctx;
    size_t first;
    size_t i;
    int evex;
    int imm;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (rq == NULL || rq->mnemonic == NULL || out == NULL || out_len == NULL) {
        return 0;
    }
    if (find_rows(rq->mnemonic, &first) != 0) {
        if (alias_of(rq->mnemonic, base, sizeof(base), &imm) != 0 || find_rows(base, &first) != 0) {
            return 0;
        }
        if (rq->op_count + 1 > sizeof(with_imm) / sizeof(with_imm[0])) {
            if (errbuf != NULL && errbuf_sz > 0) {
                snprintf(errbuf, errbuf_sz, "'%s' does not take these operands", rq->mnemonic);
            }
            return -1;
        }
        memset(&with_imm[0], 0, sizeof(with_imm[0]));
        with_imm[0].kind = AS_X86_OP_IMM;
        with_imm[0].u.imm = imm;
        memcpy(&with_imm[1], rq->ops, rq->op_count * sizeof(with_imm[0]));
        aliased = *rq;
        aliased.ops = with_imm;
        aliased.op_count = rq->op_count + 1;
        aliased.mnemonic = base;
        /* (The message names what was written.) */
        i = (size_t)as_x86_vec_encode(&aliased, out, out_cap, out_len, errbuf, errbuf_sz);
        if ((int)i < 0 && errbuf != NULL && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "'%s' does not take these operands", rq->mnemonic);
        }
        return (int)i;
    }
    if ((as_x86_vec_table[first].flags & AS_VEC_F_WEAK) != 0) {
        /* `movsd`, `cmpsd` and `movq` are also instructions of the
         * general registers, and are these only with a vector one. */
        int vector = 0;

        for (i = 0; i < rq->op_count; ++i) {
            if (rq->ops[i].kind == AS_X86_OP_REG &&
                (is_vector_class(rq->ops[i].reg_class) || rq->ops[i].reg_class == AS_X86_REGCLASS_MMX)) {
                vector = 1;
            }
        }
        if (!vector) {
            return 0;
        }
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.cap = out_cap;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    evex = needs_evex(rq);

    for (i = first; i < as_x86_vec_table_count; i = next_row[i]) {
        const as_x86_vec_tmpl_t *t = &as_x86_vec_table[i];

        if (!row_takes(t, rq)) {
            continue;
        }
        /*
         * APX's EVEX form of an instruction that has another: it is
         * the one asked for by `{evex}` or `{nf}`, or -- of the
         * instructions with no REX2 form -- the one %r16 to %r31 need.
         * `{nf}` asks for a form that writes no flags, and no other.
         */
        if ((t->flags & (AS_VEC_F_PROMO | AS_VEC_F_PROMOHI)) != 0 &&
            (rq->pseudo & (AS_PSEUDO_EVEX | AS_PSEUDO_NF)) == 0 &&
            !((t->flags & AS_VEC_F_PROMOHI) != 0 && has_high_general(rq))) {
            continue;
        }
        /* (`{dfv=}` is of the conditional compare and test.) */
        if (rq->have_dfv && (t->flags & AS_VEC_F_SCC) == 0) {
            why = "takes no {dfv=}";
            continue;
        }
        if (((t->flags & AS_VEC_F_NF) != 0 && (t->flags & (AS_VEC_F_PROMO | AS_VEC_F_PROMOHI)) != 0) !=
            ((rq->pseudo & AS_PSEUDO_NF) != 0)) {
            if ((rq->pseudo & AS_PSEUDO_NF) != 0) {
                why = "has no form that writes no flags with these operands";
            }
            continue;
        }
        if ((t->flags & AS_VEC_F_64) != 0 && rq->mode != 64u) {
            why = "is an instruction of 64-bit code with these operands";
            continue;
        }
        if ((t->flags & AS_VEC_F_X32) != 0 && rq->mode == 64u) {
            why = "is not an instruction of 64-bit code with these operands";
            continue;
        }
        if (row_refuses(t, rq, evex, &why)) {
            /* What AVX can say needs no more than AVX, whatever the
             * AVX-512 row after it goes on to ask. */
            if (why == needs_v3) {
                lower = needs_v3;
            }
            continue;
        }
        if (t->enc == AS_VEC_VEX && (rq->pseudo & AS_PSEUDO_VEX3) == 0 && t->map == AS_VEC_MAP_0F &&
            t->w != AS_VEC_W1) {
            const as_x86_operand_t *rm = role_op(t, rq, 'm');
            int rm_high = rm != NULL && rm->kind == AS_X86_OP_REG && (unsigned)rm->u.reg > 7u;

            /* The two-byte prefix has no bit for r/m's fourth: the row
             * after has the registers the other way about.  (Two
             * sources that could change places are left as written,
             * as GNU as leaves them.) */
            if ((t->flags & AS_VEC_F_SWAP) != 0 && rm_high && role_reg(t, rq, 'r') <= 7u &&
                (rq->pseudo & (AS_PSEUDO_LOAD | AS_PSEUDO_STORE)) == 0) {
                continue;
            }
        }
        if ((t->flags & AS_VEC_F_SWAP) != 0 && (rq->pseudo & AS_PSEUDO_STORE) != 0) {
            const as_x86_operand_t *rm = role_op(t, rq, 'm');

            if (rm != NULL && rm->kind == AS_X86_OP_REG) {
                continue;
            }
        }
        if (emit_row(&ctx, t, rq) != 0) {
            return -1;
        }
        *out_len = ctx.at;
        return 1;
    }
    /* (APX's forms of an instruction, and these operands are of none
     * of them: the instruction is assembled where it always was.) */
    if ((as_x86_vec_table[first].flags & AS_VEC_F_APX) != 0) {
        return 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        snprintf(errbuf, errbuf_sz, "'%s' %s", rq->mnemonic, lower != NULL ? lower : why);
    }
    return -1;
}
