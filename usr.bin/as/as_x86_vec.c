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
    /* (Nor one whose rows are the forms that have come here so far of
     * an instruction that is still assembled elsewhere.) */
    if (find_rows(mnemonic, &first) == 0) {
        return (as_x86_vec_table[first].flags & (AS_VEC_F_APX | AS_VEC_F_BASE)) == 0;
    }
    return alias_of(mnemonic, base, sizeof(base), &imm) == 0 && find_rows(base, &first) == 0;
}

int as_x86_vec_has_rows(const char *mnemonic) {
    size_t first;

    return mnemonic != NULL && find_rows(mnemonic, &first) == 0;
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

int as_x86_vec_has_base(const char *mnemonic, size_t op_count) {
    size_t i;

    if (mnemonic == NULL || find_rows(mnemonic, &i) != 0) {
        return 0;
    }
    for (; i < as_x86_vec_table_count; i = next_row[i]) {
        if ((as_x86_vec_table[i].flags & AS_VEC_F_BASE) != 0 && strlen(as_x86_vec_table[i].ops) == op_count) {
            return 1;
        }
    }
    return 0;
}

int as_x86_vec_base_is_whole(const char *mnemonic) {
    size_t i;

    if (mnemonic == NULL || find_rows(mnemonic, &i) != 0) {
        return 0;
    }
    for (; i < as_x86_vec_table_count; i = next_row[i]) {
        const as_x86_vec_tmpl_t *t = &as_x86_vec_table[i];

        if (t->enc == AS_VEC_LEGACY && (t->flags & AS_VEC_F_BASE) != 0 && t->bcst == AS_VEC_LEGACY_WHOLE) {
            return 1;
        }
    }
    return 0;
}

static int is_vector_class(unsigned cls) {
    return cls == AS_X86_REGCLASS_XMM || cls == AS_X86_REGCLASS_YMM || cls == AS_X86_REGCLASS_ZMM;
}

/*
 * How many bytes the number after a row's instruction is of, where the
 * row says: two, four -- or, where it says both, the code's own size,
 * which is two in 16-bit code and four in the others (`add $0x1234,
 * (%bx)` with no suffix).  0 where it says neither: one byte.
 */
static unsigned row_imm_bytes(const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq) {
    unsigned both = AS_VEC_F_IMM16 | AS_VEC_F_IMM32;

    if ((t->flags & both) == both) {
        return rq->mode == 16u ? 2u : 4u;
    }
    return (t->flags & AS_VEC_F_IMM16) != 0 ? 2u : (t->flags & AS_VEC_F_IMM32) != 0 ? 4u : 0u;
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
    /* Memory at an address that is a number alone, no register in
     * it: what the accumulator is moved to and from by a form of its
     * own in 16- and 32-bit code, with the address after the opcode. */
    case 'O':
        return plain_mem && !op->u.mem.has_base && !op->u.mem.has_index && !op->u.mem.rip_relative;
    /* And such an address to be loaded from, which is not a symbol's
     * place in the global offset table: that load keeps its ModRM,
     * for the linker to make another instruction of (a store there is
     * the short form like any other). */
    case 'P':
        return plain_mem && !op->u.mem.has_base && !op->u.mem.has_index && !op->u.mem.rip_relative &&
               !op->u.mem.disp_has_op;
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
    case 'M':
        /* And the number three, which int has one for. */
        return op->kind == AS_X86_OP_IMM && !op->imm_symbolic && op->u.imm == 3;
    case 's':
        /* A byte that the instruction extends by its sign. */
        return op->kind == AS_X86_OP_IMM && !op->imm_symbolic && op->u.imm >= -128 && op->u.imm <= 127;
    /* And one that is such a byte once it is cut to the 16 or the 32
     * bits it is added to: `addl $0xffffffff, %ecx` is 83 C1 FF. */
    case 'S':
        return op->kind == AS_X86_OP_IMM && !op->imm_symbolic &&
               ((op->u.imm >= -128 && op->u.imm <= 127) || (op->u.imm >= 0xff80 && op->u.imm <= 0xffff) ||
                (op->u.imm >= 0xffffff80ll && op->u.imm <= 0xffffffffll));
    case 'T':
        return op->kind == AS_X86_OP_IMM && !op->imm_symbolic &&
               ((op->u.imm >= -128 && op->u.imm <= 127) || (op->u.imm >= 0xffffff80ll && op->u.imm <= 0xffffffffll));
    /* A general register of 16, of 32, of 64 bits that is not %ax,
     * %eax, %rax: those have a form of their own with a number. */
    /* A general register of 8 bits, any of them -- %ah to %bh, which
     * are written with no REX prefix, and %spl to %dil, which are
     * written with one (emit_row_bytes()); and one that is not %al. */
    /* A control register, a debug register, a segment register: what
     * mov has forms of its own for. */
    case '5':
        return cls == AS_X86_REGCLASS_CONTROL;
    /* The place a jump or a call goes to. */
    case '&':
        return op->kind == AS_X86_OP_REL;
    case '6':
        return cls == AS_X86_REGCLASS_DEBUG;
    /* A test register, %tr0 to %tr7, which a 486 had. */
    case 'r':
        return cls == AS_X86_REGCLASS_TEST;
    /* A number that four bytes hold once they are extended by their
     * sign to 64 bits, or a symbol, which is given that room: what
     * `mov $n, %rax` is C7 for, and above which it is the move of
     * eight bytes. */
    case 'p':
        return op->kind == AS_X86_OP_IMM &&
               (op->imm_symbolic || (op->u.imm >= -2147483648ll && op->u.imm <= 2147483647ll));
    case '7':
        return cls == AS_X86_REGCLASS_SEGMENT;
    /* The segment registers an instruction of one byte pushes -- %es,
     * %cs, %ss, %ds -- and pops, which are those but %cs; and the two
     * that came later, %fs and %gs, whose instructions are of two. */
    case '8':
        return cls == AS_X86_REGCLASS_SEGMENT && (unsigned)op->u.reg <= 3u;
    case '4':
        return cls == AS_X86_REGCLASS_SEGMENT && (unsigned)op->u.reg <= 3u && (unsigned)op->u.reg != 1u;
    case '9':
        return cls == AS_X86_REGCLASS_SEGMENT && ((unsigned)op->u.reg == 4u || (unsigned)op->u.reg == 5u);
    /* The port `in` and `out` go through: %dx, written as the register
     * or in brackets as an address is. */
    case 'C':
        return (cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 16 && (unsigned)op->u.reg == 2u) ||
               (plain_mem && op->u.mem.has_base && (unsigned)op->u.mem.base == 2u && op->u.mem.addr_bits == 16u &&
                !op->u.mem.has_index && !op->u.mem.has_disp && !op->u.mem.rip_relative);
    /* A register of the x87's stack, %st(0) to %st(7); and its top,
     * %st, and no other. */
    case 'U':
        return op->kind == AS_X86_OP_FPU;
    case 'I':
        return op->kind == AS_X86_OP_FPU && op->u.fpu == 0;
    case 'H':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 8;
    case 'v':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 8 && (unsigned)op->u.reg != 0u;
    /* And the ones those are not: %al, %ax, %eax, %rax, in the forms
     * that are theirs alone. */
    case 'V':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 8 && (unsigned)op->u.reg == 0u;
    case 'F':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 16 && (unsigned)op->u.reg == 0u;
    case 'G':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 32 && (unsigned)op->u.reg == 0u;
    case 'J':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 64 && (unsigned)op->u.reg == 0u;
    case 'f':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 16 && (unsigned)op->u.reg != 0u;
    case 'g':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 32 && (unsigned)op->u.reg != 0u;
    case 'j':
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 64 && (unsigned)op->u.reg != 0u;
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
        return cls == AS_X86_REGCLASS_GENERAL && op->size_bits == 8 && !op->high8 &&
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
    /* What a string instruction reads and what it writes: memory
     * through the source index and through the destination index, %si
     * and %di at whichever size, with nothing added. */
    case '<':
    case '>':
        return mem && op->u.mem.has_base && !op->u.mem.has_index && !op->u.mem.rip_relative &&
               (!op->u.mem.has_disp || (op->u.mem.disp == 0 && !op->u.mem.force_disp32)) &&
               (unsigned)op->u.mem.base == (want == '<' ? 6u : 7u);
    /* And the table xlat looks in, written as memory through a
     * register: whichever and with whatever added, for the size of
     * the address is all that is read of it. */
    case '=':
        return mem && op->u.mem.has_base && !op->u.mem.rip_relative;
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
        char want = t->ops[i];

        /* (`L`: a byte extended by its sign once it is cut to the
         * code's own size -- what `S` is of 16 bits and `T` of 32, for
         * a form whose size nothing but the code says.) */
        if (want == 'L') {
            want = rq->mode == 16u ? 'S' : 'T';
        }
        if (!operand_is(&rq->ops[i], want)) {
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

        /* (%ah to %bh have numbers above fifteen here too, and are not
         * of those: `crc32 %ah, %ecx` was looked for among APX's forms.) */
        if (op->kind == AS_X86_OP_REG && op->reg_class == AS_X86_REGCLASS_GENERAL && !op->high8 &&
            (unsigned)op->u.reg > 15u) {
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

        /* (An address of 16-bit registers has a displacement of one
         * byte or of two, and `{disp32}` asks for what it has not:
         * GNU as refuses it, and this took it for two.) */
        if ((rq->pseudo & AS_PSEUDO_DISP32) != 0) {
            vec_err(ctx, "{disp32} before an address of 16-bit registers, which has no displacement of four bytes");
            return -1;
        }
        /* (A one-byte displacement only where it is the bytes it says.) */
        if (as_x86_modrm16(m, reg & 7u, disp8_n == 1u && (rq->pseudo & AS_PSEUDO_DISP32) == 0, bytes, &n) != 0) {
            vec_err(ctx, "not an address of 16-bit registers");
            return -1;
        }
        /* (`{disp8}` before an address with no displacement asks for
         * one of nothing: `{disp8} add %cx, (%bx)` is 01 4F 00.) */
        if ((rq->pseudo & AS_PSEUDO_DISP8) != 0 && n == 1 && (bytes[0] & 0xc0u) == 0 && !m->disp_only) {
            bytes[0] |= 0x40u;
            bytes[n++] = 0;
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
static int emit_row(vec_ctx_t *ctx, const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq);

/*
 * A row with registers of 8 bits of any kind (class H or v).
 *
 * %ah, %ch, %dh and %bh are numbers 4 to 7 in an instruction with no
 * REX prefix, and cannot be named in one that has it; %spl, %bpl, %sil
 * and %dil are those numbers in one that has it, and so make it have
 * one, of no bits if no other is wanted.  The row is emitted with the
 * registers numbered so and with that said of the prefix.
 */
static int emit_row_bytes(vec_ctx_t *ctx, const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq) {
    as_x86_operand_t ops[8];
    as_x86_vec_req_t as = *rq;
    size_t i;

    if (strpbrk(t->ops, "HvUI") == NULL || rq->op_count > sizeof(ops) / sizeof(ops[0])) {
        return emit_row(ctx, t, rq);
    }
    for (i = 0; i < rq->op_count; ++i) {
        ops[i] = rq->ops[i];
        /* (A register of the x87's stack is a register in r/m by its
         * place in the stack: `fadd %st(3), %st` is D8 C3.) */
        if ((t->ops[i] == 'U' || t->ops[i] == 'I') && ops[i].kind == AS_X86_OP_FPU) {
            unsigned place = (unsigned)ops[i].u.fpu;

            ops[i].kind = AS_X86_OP_REG;
            ops[i].u.reg = (as_x86_reg_t)place;
            continue;
        }
        /* (Or the row of memory into a register that `{load}` takes
         * for two registers: its first is a byte register then.) */
        if ((t->ops[i] != 'H' && t->ops[i] != 'v' &&
             !(t->ops[i] == 'm' && ops[i].reg_class == AS_X86_REGCLASS_GENERAL && ops[i].size_bits == 8)) ||
            ops[i].kind != AS_X86_OP_REG) {
            continue;
        }
        if (ops[i].high8) {
            ops[i].u.reg = (as_x86_reg_t)((unsigned)ops[i].u.reg - (unsigned)AS_X86_REG_AH + 4u);
            as.no_rex = 1;
            /* (`{rex}` asks for a prefix where there may be one, and
             * GNU as leaves it out where there may not: `{rex} movb
             * %ah, %al` is 88 E0.  A `rex` written as a prefix is not
             * left out, and is refused.) */
            if ((rq->pseudo & AS_PSEUDO_REX) != 0 && rq->rex_bits == 0 && rq->mode == 64u) {
                as.explicit_rex = 0;
            }
        } else if ((unsigned)ops[i].u.reg >= 4u && (unsigned)ops[i].u.reg <= 7u) {
            if (rq->mode != 64u) {
                vec_err(ctx, "'%s': %%spl, %%bpl, %%sil and %%dil are registers of 64-bit code", t->name);
                return -1;
            }
            as.explicit_rex = 1;
        }
    }
    as.ops = ops;
    return emit_row(ctx, t, &as);
}

/*
 * A jump or a call to a place (roles `j` and `J`).
 *
 * The row's opcode is the form with the full displacement, of two
 * bytes or of four; where it has another with one byte, that opcode is
 * in the row's `disp8_n`, and is written where the distance is known
 * and a byte holds it and the near form was not asked for.  Role `J`
 * is a form with a byte and no other -- loop and the jumps on the
 * counter -- whose rows say by OP16 and OP32 which counter they test,
 * and so whether the address-size prefix is before them.
 *
 * The distance is from the end of the instruction, and the lengths
 * here are what the encoders this came from took them for: two for the
 * short form, and for the near one the opcode's bytes and the
 * displacement's, with no prefix counted.
 */
static int emit_branch(vec_ctx_t *ctx, const as_x86_vec_tmpl_t *t, const as_x86_vec_req_t *rq,
                       const as_x86_operand_t *to, int short_only) {
    int known = rq->rel_is_disp || rq->has_offset;
    unsigned opcode_len = t->map == AS_VEC_MAP_ONE ? 1u : 2u;
    long long target = (long long)to->u.rel;
    long long disp;
    unsigned near_bytes;
    unsigned k;

    if (short_only) {
        int other = ((t->flags & AS_VEC_F_OP16) != 0 && rq->mode != 16u) ||
                    ((t->flags & AS_VEC_F_OP32) != 0 && rq->mode != 32u);
        unsigned len = other ? 3u : 2u;

        disp = rq->rel_is_disp || !rq->has_offset ? target : target - ((long long)rq->offset + (long long)len);
        if (disp < -128 || disp > 127) {
            vec_err(ctx, "'%s' goes no further than a byte says, and its target is not so near", t->name);
            return -1;
        }
        if ((other && put8(ctx, 0x67u) != 0) || put8(ctx, t->opcode) != 0 || put8(ctx, (unsigned)disp & 0xffu) != 0) {
            return -1;
        }
        return 0;
    }
    if ((((t->flags & AS_VEC_F_OP16) != 0 && rq->mode != 16u) ||
         ((t->flags & AS_VEC_F_OP32) != 0 && rq->mode == 16u)) &&
        put8(ctx, 0x66u) != 0) {
        return -1;
    }
    /* (A REX prefix that is written is written, where there is one.) */
    if (rq->explicit_rex) {
        if (rq->mode != 64u) {
            vec_err(ctx, "'%s': a REX prefix outside 64-bit code", t->name);
            return -1;
        }
        if (put8(ctx, 0x40u | (rq->rex_bits & 0xfu)) != 0) {
            return -1;
        }
    }
    if (t->disp8_n != 0 && known && !rq->force_near) {
        disp = rq->rel_is_disp ? target : target - ((long long)rq->offset + 2);
        if (disp >= -128 && disp <= 127) {
            return put8(ctx, t->disp8_n) != 0 || put8(ctx, (unsigned)disp & 0xffu) != 0 ? -1 : 0;
        }
    }
    near_bytes = (t->flags & AS_VEC_F_OP16) != 0 || (rq->mode == 16u && (t->flags & AS_VEC_F_OP32) == 0) ? 2u : 4u;
    disp = rq->rel_is_disp || !rq->has_offset
               ? target
               : target - ((long long)rq->offset + (long long)(opcode_len + near_bytes));
    if (near_bytes == 2u && opcode_len == 2u && known && (disp < -32768 || disp > 32767)) {
        vec_err(ctx, "conditional branch target out of range");
        return -1;
    }
    if ((opcode_len == 2u && put8(ctx, 0x0fu) != 0) || put8(ctx, t->opcode) != 0) {
        return -1;
    }
    for (k = 0; k < near_bytes; ++k) {
        if (put8(ctx, (unsigned)(((unsigned long long)disp >> (k * 8u)) & 0xffu)) != 0) {
            return -1;
        }
    }
    return 0;
}

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
    if (rm == NULL && t->enc == AS_VEC_LEGACY && (t->flags & AS_VEC_F_BASE) != 0 &&
        (role_op(t, rq, 'j') != NULL || role_op(t, rq, 'J') != NULL)) {
        const as_x86_operand_t *near_or_short = role_op(t, rq, 'j');

        return emit_branch(ctx, t, rq, near_or_short != NULL ? near_or_short : role_op(t, rq, 'J'),
                           near_or_short == NULL);
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
        /* (Or in the index registers of a string instruction, which
         * are of one size where both are written.) */
        if (addr == NULL && (t->flags & AS_VEC_F_BASE) != 0 && strpbrk(t->ops, "<>=") != NULL) {
            unsigned bits = 0;
            unsigned other = rq->mode == 64u ? 32u : rq->mode == 32u ? 16u : 32u;

            for (k = 0; k < rq->op_count; ++k) {
                if (t->ops[k] == '<' || t->ops[k] == '>' || t->ops[k] == '=') {
                    unsigned b = rq->ops[k].u.mem.addr_bits != 0 ? rq->ops[k].u.mem.addr_bits : rq->mode;

                    if (bits != 0 && bits != b) {
                        vec_err(ctx, "'%s': the two index registers are of different sizes", t->name);
                        return -1;
                    }
                    bits = b;
                }
            }
            if (bits != rq->mode && bits != other) {
                vec_err(ctx, "an address in a %u-bit register in %u-bit code", bits, rq->mode);
                return -1;
            }
            if (bits == other && put8(ctx, 0x67u) != 0) {
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
        /* (The operand's size where it is not the mode's, and W, as
         * for a row with a ModRM: `add $0x1234, %ax` is 66 05 34 12
         * in 32-bit code, `add $1, %rax` with four bytes 48 05.) */
        if ((((t->flags & AS_VEC_F_OP16) != 0 && rq->mode != 16u) ||
             ((t->flags & AS_VEC_F_OP32) != 0 && rq->mode == 16u)) &&
            put8(ctx, 0x66u) != 0) {
            return -1;
        }
        if (t->pp != AS_VEC_PP_NONE && put8(ctx, pp_byte[t->pp]) != 0) {
            return -1;
        }
        /*
         * A register in the opcode itself (role `+`): its low three
         * bits are added to the opcode, the fourth is REX's B, and the
         * fifth -- of %r16 to %r31 -- REX2's.  `push %rbx` is 53,
         * `push %r9` 41 51, `push %r17` D5 10 51.
         */
        {
            const as_x86_operand_t *plus = (t->flags & AS_VEC_F_BASE) != 0 ? role_op(t, rq, '+') : NULL;
            unsigned preg = plus != NULL ? (unsigned)plus->u.reg : 0u;
            unsigned rex = (t->flags & AS_VEC_F_BASE) == 0
                               ? 0u
                               : ((t->w == AS_VEC_W1 ? 8u : 0u) | ((preg >> 3) & 1u) |
                                  (rq->explicit_rex ? 0x40u | rq->rex_bits : 0u));
            int far = preg > 15u;

            if ((rex != 0 || far) && rq->mode != 64u) {
                vec_err(ctx, far ? "'%s': %%r16 to %%r31 are registers of 64-bit code"
                                 : "'%s': a REX prefix outside 64-bit code",
                        t->name);
                return -1;
            }
            if (rq->no_rex && (rex != 0 || far)) {
                vec_err(ctx, "'%s': %%ah, %%ch, %%dh and %%bh cannot be named in an instruction with a REX prefix",
                        t->name);
                return -1;
            }
            if (far) {
                if (t->map != AS_VEC_MAP_ONE && t->map != AS_VEC_MAP_0F) {
                    vec_err(ctx, "'%s' has no prefix that says %%r16 to %%r31", t->name);
                    return -1;
                }
                if (put8(ctx, 0xd5u) != 0 ||
                    put8(ctx, ((t->map == AS_VEC_MAP_0F ? 1u : 0u) << 7) | (1u << 4) | (rex & 0xfu)) != 0) {
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
            /* (Or a segment register in it, role `g`: the low two bits
             * of its number in the opcode's bits 3 and 4.  `push %ds`
             * is 1E, `pop %gs` 0F A9.) */
            {
                const as_x86_operand_t *seg = (t->flags & AS_VEC_F_BASE) != 0 ? role_op(t, rq, 'g') : NULL;

                if (seg != NULL) {
                    preg = ((unsigned)seg->u.reg & 3u) << 3;
                } else {
                    preg &= 7u;
                }
            }
            if (put8(ctx, t->opcode + preg) != 0 || (t->disp8_n != 0 && put8(ctx, t->disp8_n) != 0)) {
                return -1;
            }
        }
        /*
         * Two numbers after the opcode, which enter has (the frame's
         * size in two bytes and the level in one) and the far jump and
         * call to a place written as a segment and an offset (the
         * offset, of the operand's size, and then the segment in two).
         * Role `z` is a number of the operand's size, `2` one of two
         * bytes and `1` one of a byte, and they are written in that
         * order whatever order they are operands in.  A symbol is room
         * for the linker.
         */
        if ((t->flags & AS_VEC_F_BASE) != 0 &&
            (role_op(t, rq, 'z') != NULL || role_op(t, rq, '2') != NULL || role_op(t, rq, '1') != NULL)) {
            static const char order[] = "z21";
            size_t r;

            for (r = 0; order[r] != '\0'; ++r) {
                const as_x86_operand_t *num = role_op(t, rq, order[r]);
                unsigned bytes = order[r] == '1' ? 1u
                                 : order[r] == '2' ? 2u
                                 : (t->flags & AS_VEC_F_OP16) != 0 || (rq->mode == 16u && (t->flags & AS_VEC_F_OP32) == 0)
                                       ? 2u
                                       : 4u;
                unsigned b;

                if (num == NULL) {
                    continue;
                }
                if (!num->imm_symbolic &&
                    (bytes == 1u ? (num->u.imm < -128 || num->u.imm > 255)
                     /* (An offset of four bytes keeps its low four,
                      * as GNU as has it: `ljmp $8, $0x100000000`.) */
                     : bytes == 2u && (num->u.imm < -32768 || num->u.imm > 65535))) {
                    vec_err(ctx, "the number %lld is not of %u byte%s", (long long)num->u.imm, bytes,
                            bytes == 1u ? "" : "s");
                    return -1;
                }
                for (b = 0; b < bytes; ++b) {
                    if (put8(ctx, num->imm_symbolic ? 0u : (unsigned)(((uint64_t)num->u.imm >> (b * 8u)) & 0xffu)) != 0) {
                        return -1;
                    }
                }
            }
            return 0;
        }
        /* An address after the opcode (role `o`): of the code's own
         * size, two bytes in 16-bit code and four in 32-bit.  A symbol
         * in it is room for the linker. */
        {
            const as_x86_operand_t *at = (t->flags & AS_VEC_F_BASE) != 0 ? role_op(t, rq, 'o') : NULL;

            if (at != NULL) {
                uint32_t where = at->u.mem.has_disp ? (uint32_t)at->u.mem.disp : 0u;

                if (rq->mode == 64u || (at->u.mem.addr_bits != 0 && at->u.mem.addr_bits != rq->mode)) {
                    vec_err(ctx, "'%s': an address of another size than the code's", t->name);
                    return -1;
                }
                if (rq->mode == 16u ? (put8(ctx, where & 0xffu) != 0 || put8(ctx, (where >> 8) & 0xffu) != 0)
                                    : put32(ctx, where) != 0) {
                    return -1;
                }
            }
        }
        /* And a number after that, if the row has one (`xabort $1`):
         * of one byte, or of the two or four the row says, which may
         * be a symbol's room in a form an instruction always had. */
        {
            const as_x86_operand_t *imm = role_op(t, rq, 'i');
            unsigned wide = row_imm_bytes(t, rq);
            unsigned k2;
            int cut;

            /* (Of eight bytes, which one instruction has: movabs.  A
             * row with no VEX has no use for `len` but to say so.) */
            if (t->len == AS_VEC_LEGACY_IMM64) {
                if (!imm->imm_symbolic || (t->flags & AS_VEC_F_BASE) != 0) {
                    for (k2 = 0; k2 < 8u; ++k2) {
                        if (put8(ctx, imm->imm_symbolic ? 0u
                                                        : (unsigned)(((uint64_t)imm->u.imm >> (k2 * 8u)) & 0xffu)) != 0) {
                            return -1;
                        }
                    }
                    return 0;
                }
            }

            if (imm == NULL) {
                return 0;
            }
            /*
             * A number with the accumulator -- `add $n, %al` and its
             * kind, whose rows are of a number and a register: one too
             * large keeps its low bytes, as GNU as has it with a
             * warning, and the byte may be a symbol's.  (Not a number
             * alone: `int $0x1234` is refused.)
             */
            cut = (t->flags & AS_VEC_F_BASE) != 0 && rq->op_count == 2 && strchr(t->ops, 'i') != NULL &&
                  t->w != AS_VEC_W1;
            /* (And a number pushed, but as 64-bit code pushes it:
             * `pushw $0x7fffffff` pushes 0xffff, and the byte that a
             * number is once cut to the size is that byte.) */
            if ((t->flags & AS_VEC_F_BASE) != 0 && rq->op_count == 1 && strncmp(t->name, "push", 4) == 0 &&
                (strchr("STL", t->ops[0]) != NULL || wide == 2 || (wide == 4 && rq->mode != 64u))) {
                cut = 1;
            }
            /* (And the count of a return of 16 bits, near or far:
             * `retw $0x10000` is 66 C2 00 00.  One of 32 or of 64 bits
             * with such a count is refused.) */
            if ((t->flags & AS_VEC_F_BASE) != 0 && rq->op_count == 1 && wide == 2 &&
                (strncmp(t->name, "ret", 3) == 0 || strncmp(t->name, "lret", 4) == 0) &&
                ((t->flags & AS_VEC_F_OP16) != 0 ||
                 (rq->data16 && (t->flags & (AS_VEC_F_64 | AS_VEC_F_OP32)) == 0) ||
                 (rq->mode == 16u && (t->flags & AS_VEC_F_OP32) == 0))) {
                cut = 1;
            }
            /* (A byte read from a port or written to one, through
             * %al or of the instruction that says a byte, is cut as
             * the accumulator's are; a port for a word is not.) */
            if ((t->flags & AS_VEC_F_BASE) != 0 && rq->op_count == 1 && wide == 0 &&
                (strcmp(t->name, "inb") == 0 || strcmp(t->name, "outb") == 0)) {
                cut = 1;
            }
            /* (A symbol may be the byte of a form an instruction
             * always had: `int $x`, `in $x, %al`.) */
            if (imm->imm_symbolic && (t->flags & AS_VEC_F_BASE) == 0) {
                vec_err(ctx, "'%s' takes a number that is known here", t->name);
                return -1;
            }
            if (wide == 0) {
                if (!imm->imm_symbolic && !cut && (t->flags & AS_VEC_F_BASE) != 0 &&
                    (imm->u.imm < -128 || imm->u.imm > 255)) {
                    vec_err(ctx, "the number %lld is not of one byte", (long long)imm->u.imm);
                    return -1;
                }
                return put8(ctx, imm->imm_symbolic ? 0u : (unsigned)imm->u.imm & 0xffu);
            }
            /* (Four bytes that 64 bits are made of by their sign must
             * be the number: of a row with W, and of what 64-bit code
             * pushes, which has no W and is of 64 bits all the same.) */
            if (!imm->imm_symbolic && !cut &&
                (wide == 2 ? (imm->u.imm < -32768 || imm->u.imm > 65535)
                           : (imm->u.imm < -2147483648ll ||
                              imm->u.imm > (t->w == AS_VEC_W1 || (rq->mode == 64u && strncmp(t->name, "push", 4) == 0)
                                                ? 2147483647ll
                                                : 4294967295ll)))) {
                vec_err(ctx, "the number %lld is not of %u bytes", (long long)imm->u.imm, wide);
                return -1;
            }
            for (k2 = 0; k2 < wide; ++k2) {
                if (put8(ctx, imm->imm_symbolic ? 0u : (unsigned)(((uint64_t)imm->u.imm >> (k2 * 8u)) & 0xffu)) != 0) {
                    return -1;
                }
            }
            return 0;
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
    } else if (imm != NULL && row_imm_bytes(t, rq) == 2u && imm->imm_symbolic) {
        /* (A symbol: two bytes for the linker.) */
        if (put8(&tc, 0) != 0 || put8(&tc, 0) != 0) {
            return -1;
        }
    } else if (imm != NULL && row_imm_bytes(t, rq) == 2u) {
        /* (A form an instruction always had takes a number too large
         * and keeps its low bytes, as GNU as does with a warning: `addw
         * $0x12345678, %cx` adds 0x5678.) */
        if ((t->flags & AS_VEC_F_BASE) == 0 && (imm->u.imm < -32768 || imm->u.imm > 65535)) {
            vec_err(ctx, "the number %lld is not of two bytes", (long long)imm->u.imm);
            return -1;
        }
        if (put8(&tc, (unsigned)imm->u.imm & 0xffu) != 0 || put8(&tc, ((unsigned)imm->u.imm >> 8) & 0xffu) != 0) {
            return -1;
        }
    } else if (imm != NULL && row_imm_bytes(t, rq) == 4u && imm->imm_symbolic) {
        /* (A symbol: four bytes for the linker, which are the
         * instruction's last.  Of APX's rows; the others take a number
         * that is known here.) */
        if ((t->flags & (AS_VEC_F_APX | AS_VEC_F_BASE)) == 0) {
            vec_err(ctx, "'%s' takes a number that is known here", t->name);
            return -1;
        }
        if (put32(&tc, 0) != 0) {
            return -1;
        }
    } else if (imm != NULL && row_imm_bytes(t, rq) == 4u) {
        /* (Of 64 bits the four bytes are extended by their sign, and
         * the number must be what they extend to.) */
        if (((t->flags & AS_VEC_F_BASE) == 0 || t->w == AS_VEC_W1) &&
            (imm->u.imm < -2147483648ll || imm->u.imm > (t->w == AS_VEC_W1 ? 2147483647ll : 4294967295ll))) {
            vec_err(ctx, "the number %lld is not of four bytes", (long long)imm->u.imm);
            return -1;
        }
        if (put32(&tc, (uint32_t)imm->u.imm) != 0) {
            return -1;
        }
    } else if (imm != NULL) {
        /* (One that is a byte once cut to 16 or to 32 bits is its low
         * byte, which is what the row's class has found it to be.  And
         * a form an instruction always had keeps the low byte of any
         * number -- `addb $0x1234, %cl` adds 0x34 -- but for the
         * shifts and rotations, whose count GNU as will not cut.) */
        const char *at = strchr(t->roles, 'i');
        char cls = at != NULL ? t->ops[at - t->roles] : '\0';

        if (!imm->imm_symbolic && cls != 'S' && cls != 'T' && cls != 'L' &&
            !((t->flags & AS_VEC_F_BASE) != 0 && cls == 'i' &&
              !(t->map == AS_VEC_MAP_ONE && (t->opcode == 0xc0u || t->opcode == 0xc1u))) &&
            (imm->u.imm < -128 || imm->u.imm > 255)) {
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
        if (rq->no_rex && (rex != 0 || high)) {
            vec_err(ctx, "'%s': %%ah, %%ch, %%dh and %%bh cannot be named in an instruction with a REX prefix",
                    t->name);
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
        if ((as_x86_vec_table[i].flags & (AS_VEC_F_PROMO | AS_VEC_F_PROMOHI | AS_VEC_F_APX | AS_VEC_F_BASE)) != 0) {
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

/* The same of the forms the name always had: any row of AS_VEC_F_BASE
 * may say so.  (A name's first rows are APX's where it has them, and
 * say it of APX's forms.) */
int as_x86_vec_base_takes_lock(const char *mnemonic) {
    size_t i;

    if (mnemonic == NULL || find_rows(mnemonic, &i) != 0) {
        return 0;
    }
    for (; i < as_x86_vec_table_count; i = next_row[i]) {
        if ((as_x86_vec_table[i].flags & (AS_VEC_F_LOCK | AS_VEC_F_BASE)) == (AS_VEC_F_LOCK | AS_VEC_F_BASE)) {
            return 1;
        }
    }
    return 0;
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
        /* (Unless the instruction of general registers has come to
         * the table too, as movq has: its rows are looked through
         * with the rest.) */
        if (!vector && !as_x86_vec_has_base(rq->mnemonic, rq->op_count)) {
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
        /* (xrelease before a move: the form with ModRM.) */
        if (rq->no_moffs && (t->flags & AS_VEC_F_BASE) != 0 && strchr(t->roles, 'o') != NULL) {
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
        /* (`{load}` and `{store}` ask for one of the two ways an
         * instruction of two registers can be written; the rows that
         * have come from the older encoders have the one written
         * unasked, and the other is still where it was.) */
        if ((t->flags & AS_VEC_F_BASE) != 0 && (rq->pseudo & (AS_PSEUDO_LOAD | AS_PSEUDO_STORE)) != 0) {
            /* (The name's, though this row may be one with no operand,
             * which does not say: `{load} aad`.) */
            if (t->bcst != AS_VEC_LEGACY_WHOLE && !as_x86_vec_base_is_whole(t->name)) {
                continue;
            }
            /* (A segment register pushed is stored and one popped is
             * loaded: GNU as will not have the other asked of it.) */
            if (strchr(t->roles, 'g') != NULL &&
                (rq->pseudo & (strncmp(t->name, "push", 4) == 0 ? AS_PSEUDO_LOAD : AS_PSEUDO_STORE)) != 0) {
                why = "has one way to be written with a segment register";
                continue;
            }
            /* (Nor will it have `{load}` before the status word stored
             * in %ax.) */
            if ((rq->pseudo & AS_PSEUDO_LOAD) != 0 && t->ops[0] == 'F' && strcmp(t->name, "fnstsw") == 0) {
                why = "stores, and is not written the other way";
                continue;
            }
            /*
             * A name whose rows are all its forms.  Its row of two
             * registers is the way that stores, which is what GNU as
             * writes unasked and for `{store}`; `{load}` asks for the
             * other where there is one -- the row of memory into a
             * register, whose opcode is this one's with the bit of the
             * direction, and whose r/m is then the first register.
             * (An instruction with one way only has it whatever is
             * asked: `{load} bt %eax, %ebx` is bt.)
             */
            if ((rq->pseudo & AS_PSEUDO_LOAD) != 0 && rq->op_count == 2 && strcmp(t->roles, "rm") == 0 &&
                rq->ops[0].kind == AS_X86_OP_REG && rq->ops[1].kind == AS_X86_OP_REG) {
                size_t j;

                for (j = first; j < as_x86_vec_table_count; j = next_row[j]) {
                    const as_x86_vec_tmpl_t *u = &as_x86_vec_table[j];

                    if ((u->flags & AS_VEC_F_BASE) != 0 && strcmp(u->roles, "mr") == 0 && u->ops[0] == 'm' &&
                        u->ops[1] == t->ops[1] && u->opcode == (t->opcode | 2u) && (t->opcode & 2u) == 0 &&
                        u->map == t->map && u->w == t->w &&
                        ((u->flags ^ t->flags) & (AS_VEC_F_OP16 | AS_VEC_F_OP32 | AS_VEC_F_64)) == 0) {
                        t = u;
                        break;
                    }
                }
            }
        }
        /* (Three tiles are three different ones.) */
        if (strcmp(t->ops, "ttt") == 0 &&
            (rq->ops[0].u.reg == rq->ops[1].u.reg || rq->ops[0].u.reg == rq->ops[2].u.reg ||
             rq->ops[1].u.reg == rq->ops[2].u.reg)) {
            why = "takes three tile registers, and no one of them twice";
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
        if (emit_row_bytes(&ctx, t, rq) != 0) {
            return -1;
        }
        *out_len = ctx.at;
        return 1;
    }
    /* (APX's forms of an instruction, and these operands are of none
     * of them: the instruction is assembled where it always was.) */
    if ((as_x86_vec_table[first].flags & (AS_VEC_F_APX | AS_VEC_F_BASE)) != 0) {
        return 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        snprintf(errbuf, errbuf_sz, "'%s' %s", rq->mnemonic, lower != NULL ? lower : why);
    }
    return -1;
}
