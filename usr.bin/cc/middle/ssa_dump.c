/*
 * ssa_dump.c: write cc's SSA module as text, in the IR form the ir/
 * tools read (ir-verifier, ir-normalize, ir-diff).
 *
 * The form is LLVM-like: `%v12 = add i32 %v3, %v4;`, blocks named by a
 * label line, one terminator closing each.  Lowering's IR differs from
 * strict SSA in three ways, which the dump spells out so that the
 * verifier can check the rest:
 *
 *  - A value written more than once, by MOV, by inline asm, or never
 *    written but read, is a variable: it is defined `undef` at the entry
 *    and each write is a `set` (an instruction writing it computes a
 *    temporary first).
 *  - A block lowering falls through from gets an explicit `br`.
 *  - Code no branch reaches is left out: lowering emits it, nothing runs
 *    it, and the verifier rejects unreachable blocks.
 *
 * Types are what the IR records today: an integer of its byte width, a
 * floating value of its width (f32, f64, f80), and agg<N> for a value
 * that is a structure of N bytes.  Strings are written with every byte
 * that is not a letter or digit as \xx, since the reader takes `#` for a
 * comment and `:` for a label.
 */

#include "cc_ssa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** One block of a function as the dump sees it: instructions [first, end). */
typedef struct {
    size_t first;     /**< its first instruction (a LABEL, or the function's first) */
    size_t end;       /**< one past its last */
    int label;        /**< the LABEL that starts it, or -1 for the entry or code after a terminator */
    int reachable;    /**< a path from the entry reaches it */
} dump_block_t;

/** Per-function state of the dump. */
typedef struct {
    const cc_ssa_function_t *f;
    dump_block_t *blocks;
    size_t block_count;
    int *label_block;        /**< the block each label starts, or -1; label_count entries */
    unsigned char *is_var;   /**< per value: a variable, defined undef at the entry */
    unsigned temp;           /**< the next temporary's number */
} dump_func_t;

/**
 * The text type of a value: its width and kind.
 *
 * @param f    the function
 * @param v    the value
 * @param buf  receives the type
 * @param cap  its size
 * @return     @p buf
 */
static const char *value_type_text(const cc_ssa_function_t *f, int v, char *buf, size_t cap) {
    unsigned size = (v >= 0 && v < f->value_count && f->value_sizes != NULL) ? f->value_sizes[v] : 8;

    if (v < 0) {
        snprintf(buf, cap, "void");
    } else if (v < f->value_count && f->value_types[v] == CC_VAL_F64) {
        snprintf(buf, cap, "%s", size == 4 ? "f32" : size > 8 ? "f80" : "f64");
    } else if (size == 1 || size == 2 || size == 4 || size == 8) {
        snprintf(buf, cap, "i%u", size * 8);
    } else {
        snprintf(buf, cap, "agg%u", size);
    }
    return buf;
}

/**
 * Whether @p in ends its block in the text: a terminator, or an asm goto,
 * which is written as one (its fall-through is its first target).
 */
static int ends_block(const cc_ssa_instr_t *in) {
    return cc_ssa_opcode_is_terminator(in->op) || (in->op == CC_SSA_ASM && in->asm_is_goto);
}

/** The successors of block @p b, each a block index: its branch targets, or the next block it falls into. */
static size_t block_successors(const dump_func_t *d, size_t b, size_t *out, size_t cap) {
    const cc_ssa_function_t *f = d->f;
    const dump_block_t *blk = &d->blocks[b];
    size_t n = 0;
    const cc_ssa_instr_t *last = blk->end > blk->first ? &f->instrs[blk->end - 1] : NULL;

#define ADD_LABEL(l)                                                                                               \
    do {                                                                                                           \
        int l_ = (l);                                                                                              \
        if (l_ >= 0 && l_ < f->label_count && d->label_block[l_] >= 0 && n < cap) {                                \
            out[n++] = (size_t)d->label_block[l_];                                                                 \
        }                                                                                                          \
    } while (0)

    if (last != NULL && last->op == CC_SSA_BR) {
        ADD_LABEL(last->label);
    } else if (last != NULL && last->op == CC_SSA_BR_COND) {
        ADD_LABEL(last->true_label);
        ADD_LABEL(last->false_label);
    } else if (last == NULL || !cc_ssa_opcode_is_terminator(last->op)) {
        /* it falls through into the next block; an asm goto may also jump */
        if (b + 1 < d->block_count && n < cap) {
            out[n++] = b + 1;
        }
        if (last != NULL && last->op == CC_SSA_ASM && last->asm_is_goto) {
            size_t k;

            for (k = 0; k < last->asm_goto_count; ++k) {
                ADD_LABEL(last->asm_goto_labels[k]);
            }
        }
    }
#undef ADD_LABEL
    return n;
}

/**
 * Split the function into blocks -- at each LABEL, and after each
 * terminator or asm goto -- and find the ones the entry reaches.
 *
 * @return 0, or -1 when out of memory
 */
static int dump_find_blocks(dump_func_t *d) {
    const cc_ssa_function_t *f = d->f;
    size_t i;
    size_t cap = 16;
    size_t *stack;
    size_t sp = 0;
    int l;

    d->blocks = (dump_block_t *)calloc(cap, sizeof(d->blocks[0]));
    d->label_block = (int *)malloc((size_t)(f->label_count > 0 ? f->label_count : 1) * sizeof(int));
    if (d->blocks == NULL || d->label_block == NULL) {
        return -1;
    }
    for (l = 0; l < f->label_count; ++l) {
        d->label_block[l] = -1;
    }
    d->block_count = 1;
    d->blocks[0].first = 0;
    d->blocks[0].label = -1;
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int starts = in->op == CC_SSA_LABEL || (i > 0 && ends_block(&f->instrs[i - 1]));

        if (starts && i > d->blocks[d->block_count - 1].first) {
            if (d->block_count == cap) {
                dump_block_t *next = (dump_block_t *)realloc(d->blocks, cap * 2 * sizeof(d->blocks[0]));
                if (next == NULL) {
                    return -1;
                }
                d->blocks = next;
                cap *= 2;
            }
            d->blocks[d->block_count - 1].end = i;
            memset(&d->blocks[d->block_count], 0, sizeof(d->blocks[0]));
            d->blocks[d->block_count].first = i;
            d->blocks[d->block_count].label = -1;
            d->block_count++;
        }
        if (in->op == CC_SSA_LABEL && in->label >= 0 && in->label < f->label_count) {
            d->blocks[d->block_count - 1].label = in->label;
            d->label_block[in->label] = (int)(d->block_count - 1);
        }
    }
    d->blocks[d->block_count - 1].end = f->instr_count;

    stack = (size_t *)malloc(d->block_count * sizeof(stack[0]));
    if (stack == NULL) {
        return -1;
    }
    d->blocks[0].reachable = 1;
    stack[sp++] = 0;
    while (sp > 0) {
        size_t succ[64];
        size_t n = block_successors(d, stack[--sp], succ, sizeof(succ) / sizeof(succ[0]));
        size_t k;

        for (k = 0; k < n; ++k) {
            if (!d->blocks[succ[k]].reachable) {
                d->blocks[succ[k]].reachable = 1;
                stack[sp++] = succ[k];
            }
        }
    }
    free(stack);
    return 0;
}

/** The value an instruction defines, or -1 (asm outputs are counted apart). */
static int instr_def(const cc_ssa_instr_t *in) {
    switch (in->op) {
    case CC_SSA_STORE:
    case CC_SSA_LABEL:
    case CC_SSA_BR:
    case CC_SSA_BR_COND:
    case CC_SSA_VA_START:
    case CC_SSA_ASM:
    case CC_SSA_TRAP:
    case CC_SSA_RET:
        return -1;
    default:
        return in->dst;
    }
}

/** The value an asm operand names: the value itself, or the address of a memory operand; -1 for none. */
static int asm_operand_value(int v) {
    return CC_SSA_ASM_MEM_INDIRECT_P(v) ? CC_SSA_ASM_MEM_INDIRECT_DECODE(v) : v;
}

/**
 * The values an instruction reads.
 *
 * @param in   the instruction
 * @param out  receives them; may be NULL to count only
 * @param cap  the room in @p out
 * @return     how many there are (more than @p cap when they did not fit)
 */
static size_t instr_uses(const cc_ssa_instr_t *in, int *out, size_t cap) {
    size_t n = 0;
    size_t k;

#define USE(v)                                                                                                     \
    do {                                                                                                           \
        int v_ = (v);                                                                                              \
        if (v_ >= 0) {                                                                                             \
            if (out != NULL && n < cap) {                                                                          \
                out[n] = v_;                                                                                       \
            }                                                                                                      \
            n++;                                                                                                   \
        }                                                                                                          \
    } while (0)

    switch (in->op) {
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
    case CC_SSA_CMP:
    case CC_SSA_STORE:
    case CC_SSA_DYNALLOC:
        USE(in->lhs);
        USE(in->rhs);
        break;
    case CC_SSA_LOAD:
    case CC_SSA_ADDR:
    case CC_SSA_MOV:
    case CC_SSA_I2F:
    case CC_SSA_F2I:
    case CC_SSA_FROUND32:
    case CC_SSA_FROUND64:
    case CC_SSA_BR_COND:
    case CC_SSA_RET:
    case CC_SSA_VA_START:
        USE(in->lhs);
        break;
    case CC_SSA_CALLI:
        USE(in->lhs);
        /* fall through */
    case CC_SSA_CALL:
        for (k = 0; k < in->arg_count; ++k) {
            USE(in->args[k]);
        }
        if (in->imm > 0) {
            USE(in->rhs);
        }
        if (in->call_chain > 0) {
            USE(in->call_chain - 1);
        }
        break;
    case CC_SSA_ASM:
        for (k = 0; k < in->asm_in_count; ++k) {
            USE(asm_operand_value(in->asm_in_values[k]));
        }
        for (k = 0; k < in->asm_out_count; ++k) {
            if (CC_SSA_ASM_MEM_INDIRECT_P(in->asm_out_values[k])) {
                USE(asm_operand_value(in->asm_out_values[k]));
            }
        }
        break;
    default:
        break;
    }
#undef USE
    return n;
}

/**
 * Find the variables: values written more than once, by MOV or inline
 * asm, or read without being written, in the reachable code.
 *
 * @return 0, or -1 when out of memory
 */
static int dump_find_vars(dump_func_t *d) {
    const cc_ssa_function_t *f = d->f;
    int count = f->value_count;
    unsigned *defs = (unsigned *)calloc((size_t)(count > 0 ? count : 1), sizeof(unsigned));
    unsigned char *used = (unsigned char *)calloc((size_t)(count > 0 ? count : 1), 1);
    size_t b;
    int v;

    d->is_var = (unsigned char *)calloc((size_t)(count > 0 ? count : 1), 1);
    if (defs == NULL || used == NULL || d->is_var == NULL) {
        free(defs);
        free(used);
        return -1;
    }
    for (b = 0; b < d->block_count; ++b) {
        size_t i;

        if (!d->blocks[b].reachable) {
            continue;
        }
        for (i = d->blocks[b].first; i < d->blocks[b].end; ++i) {
            const cc_ssa_instr_t *in = &f->instrs[i];
            int def = instr_def(in);
            size_t k;

            if (def >= 0 && def < count) {
                defs[def]++;
                if (in->op == CC_SSA_MOV) {
                    d->is_var[def] = 1;
                }
            }
            for (k = 0; k < in->asm_out_count; ++k) {
                int o = in->asm_out_values[k];
                if (o >= 0 && o < count) {
                    d->is_var[o] = 1;
                    defs[o]++;
                }
            }
            {
                size_t nu = instr_uses(in, NULL, 0);
                int *uses = (int *)malloc((nu > 0 ? nu : 1) * sizeof(int));

                if (uses == NULL) {
                    free(defs);
                    free(used);
                    return -1;
                }
                instr_uses(in, uses, nu);
                for (k = 0; k < nu; ++k) {
                    if (uses[k] < count) {
                        used[uses[k]] = 1;
                    }
                }
                free(uses);
            }
        }
    }
    for (v = 0; v < count; ++v) {
        if (defs[v] > 1 || (used[v] && defs[v] == 0)) {
            d->is_var[v] = 1;
        }
    }
    free(defs);
    free(used);
    return 0;
}

/** Write a block's name: `entry`, `L<label>`, or `B<index>` for one no label starts. */
static void block_name(const dump_func_t *d, size_t b, char *buf, size_t cap) {
    if (b == 0) {
        snprintf(buf, cap, "entry");
    } else if (d->blocks[b].label >= 0) {
        snprintf(buf, cap, "L%d", d->blocks[b].label);
    } else {
        snprintf(buf, cap, "B%zu", b);
    }
}

/**
 * Write the name of the block a branch to @p label reaches; a label no
 * LABEL places is written as `L<label>` too, which the verifier reports
 * as an unknown block.
 */
static void label_name(const dump_func_t *d, int label, char *buf, size_t cap) {
    if (label >= 0 && label < d->f->label_count && d->label_block[label] >= 0) {
        block_name(d, (size_t)d->label_block[label], buf, cap);
    } else {
        snprintf(buf, cap, "L%d", label);
    }
}

/** Write an operand: a value, or `void` for none. */
static void put_value(FILE *out, int v) {
    if (v >= 0) {
        fprintf(out, "%%v%d", v);
    } else {
        fputs("void", out);
    }
}

/** Write @p s quoted, with every byte but a letter, digit, space or one of `_./-` as \xx. */
static void put_escaped(FILE *out, const char *s) {
    fputc('"', out);
    for (; s != NULL && *s != '\0'; ++s) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
            c == '.' || c == '/' || c == '-') {
            fputc(c, out);
        } else {
            fprintf(out, "\\%02x", c);
        }
    }
    fputc('"', out);
}

/** The text of an opcode that combines two values. */
static const char *binop_name(const cc_ssa_instr_t *in, int is_float) {
    switch (in->op) {
    case CC_SSA_ADD:
        return is_float ? "fadd" : "add";
    case CC_SSA_SUB:
        return is_float ? "fsub" : "sub";
    case CC_SSA_MUL:
        return is_float ? "fmul" : "mul";
    case CC_SSA_DIV:
        return is_float ? "fdiv" : in->is_unsigned ? "udiv" : "sdiv";
    case CC_SSA_AND:
        return "and";
    case CC_SSA_OR:
        return "or";
    case CC_SSA_XOR:
        return "xor";
    case CC_SSA_SHL:
        return "shl";
    case CC_SSA_SHR:
        return in->is_unsigned ? "lshr" : "ashr";
    default:
        return NULL;
    }
}

/** The predicate of a comparison. */
static const char *cmp_name(const cc_ssa_instr_t *in, int is_float) {
    static const char *const sint[] = {"eq", "ne", "slt", "sle", "sgt", "sge"};
    static const char *const uint[] = {"eq", "ne", "ult", "ule", "ugt", "uge"};
    static const char *const flt[] = {"oeq", "une", "olt", "ole", "ogt", "oge"};
    unsigned k = (unsigned)in->cmp_kind < 6 ? (unsigned)in->cmp_kind : 0;

    return is_float ? flt[k] : in->is_unsigned ? uint[k] : sint[k];
}

/**
 * Write the right-hand side of an instruction that defines a value: the
 * opcode, the result type and the operands.
 */
static void put_rhs(const dump_func_t *d, const cc_ssa_instr_t *in, FILE *out) {
    const cc_ssa_function_t *f = d->f;
    char t[16];
    char lt[16];
    int lf = in->lhs >= 0 && in->lhs < f->value_count && f->value_types[in->lhs] == CC_VAL_F64;
    const char *bn;
    size_t k;

    value_type_text(f, in->dst, t, sizeof(t));
    value_type_text(f, in->lhs, lt, sizeof(lt));
    if ((bn = binop_name(in, lf)) != NULL) {
        fprintf(out, "%s %s ", bn, t);
        put_value(out, in->lhs);
        if ((in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) && in->rhs < 0) {
            /* a shift by a constant count carries it in imm */
            fprintf(out, ", %lld", in->imm);
        } else {
            fputs(", ", out);
            put_value(out, in->rhs);
        }
        return;
    }
    switch (in->op) {
    case CC_SSA_PARAM:
        fprintf(out, "param %s %d", t, in->param_index);
        return;
    case CC_SSA_CONST:
        if (f->value_types[in->dst] == CC_VAL_F64) {
            fprintf(out, "fconst %s %.17g", t, in->fimm);
        } else {
            fprintf(out, "const %s %lld", t, in->imm);
        }
        return;
    case CC_SSA_STR:
        fprintf(out, "str %s ", t);
        put_escaped(out, in->sym);
        return;
    case CC_SSA_GADDR:
        fprintf(out, "gaddr %s @%s", t, in->sym != NULL ? in->sym : "");
        return;
    case CC_SSA_LADDR:
        fprintf(out, "laddr %s %lld", t, in->imm);
        return;
    case CC_SSA_ADDR:
        fprintf(out, "addr %s ", t);
        put_value(out, in->lhs);
        return;
    case CC_SSA_LOAD:
        /* rhs is not an operand of a load (some leave it 0) */
        fprintf(out, "%s %s %lld, ", in->is_unsigned ? "zload" : "load", t, in->imm);
        put_value(out, in->lhs);
        return;
    case CC_SSA_MOV:
        fprintf(out, "mov %s ", t);
        put_value(out, in->lhs);
        return;
    case CC_SSA_CMP:
        fprintf(out, "%s %s %s ", lf ? "fcmp" : "icmp", cmp_name(in, lf), lt);
        put_value(out, in->lhs);
        fputs(", ", out);
        put_value(out, in->rhs);
        return;
    case CC_SSA_I2F:
        fprintf(out, "%s %s ", in->is_unsigned ? "uitofp" : "sitofp", t);
        put_value(out, in->lhs);
        return;
    case CC_SSA_F2I:
        fprintf(out, "%s %s ", in->is_unsigned ? "fptoui" : "fptosi", t);
        put_value(out, in->lhs);
        return;
    case CC_SSA_FROUND32:
        fprintf(out, "fround32 %s ", t);
        put_value(out, in->lhs);
        return;
    case CC_SSA_FROUND64:
        fprintf(out, "fround64 %s ", t);
        put_value(out, in->lhs);
        return;
    case CC_SSA_STACKALLOC:
        fprintf(out, "stackalloc %s %lld", t, in->imm);
        return;
    case CC_SSA_DYNALLOC:
        fprintf(out, "dynalloc %s ", t);
        put_value(out, in->lhs);
        if (in->rhs >= 0) {
            fputs(", save ", out);
            put_value(out, in->rhs);
        }
        return;
    case CC_SSA_CHAIN:
        fprintf(out, "chain %s", t);
        return;
    case CC_SSA_CALL:
    case CC_SSA_CALLI:
        fprintf(out, "%s %s ", in->call_is_variadic ? "call.va" : "call", t);
        if (in->op == CC_SSA_CALL) {
            fprintf(out, "@%s(", in->sym != NULL ? in->sym : "");
        } else {
            put_value(out, in->lhs);
            fputc('(', out);
        }
        for (k = 0; k < in->arg_count; ++k) {
            if (k > 0) {
                fputs(", ", out);
            }
            put_value(out, in->args[k]);
        }
        fputc(')', out);
        if (in->rhs >= 0 && in->imm > 0) {
            /* an aggregate result, written to the memory rhs points at */
            fprintf(out, " sret %lld ", in->imm);
            put_value(out, in->rhs);
        }
        if (in->call_chain > 0) {
            fputs(" chain ", out);
            put_value(out, in->call_chain - 1);
        }
        return;
    default:
        fprintf(out, "op%d %s", (int)in->op, t);
        return;
    }
}

/** Write one instruction of a reachable block. */
static void put_instr(dump_func_t *d, size_t b, const cc_ssa_instr_t *in, FILE *out) {
    const cc_ssa_function_t *f = d->f;
    char t[16];
    char name[32];
    int def = instr_def(in);
    size_t k;

    switch (in->op) {
    case CC_SSA_LABEL:
        return;
    case CC_SSA_BR:
        label_name(d, in->label, name, sizeof(name));
        fprintf(out, "    br %s;\n", name);
        return;
    case CC_SSA_BR_COND: {
        char other[32];

        label_name(d, in->true_label, name, sizeof(name));
        label_name(d, in->false_label, other, sizeof(other));
        fprintf(out, "    br_cond %s ", value_type_text(f, in->lhs, t, sizeof(t)));
        put_value(out, in->lhs);
        fprintf(out, ", %s, %s;\n", name, other);
        return;
    }
    case CC_SSA_RET:
        if (in->lhs >= 0) {
            fprintf(out, "    ret %s ", value_type_text(f, in->lhs, t, sizeof(t)));
            put_value(out, in->lhs);
            fputs(";\n", out);
        } else {
            fputs("    ret void;\n", out);
        }
        return;
    case CC_SSA_TRAP:
        fputs("    unreachable;\n", out);
        return;
    case CC_SSA_STORE:
        fprintf(out, "    store %s %lld, ", value_type_text(f, in->rhs, t, sizeof(t)), in->imm);
        put_value(out, in->rhs);
        fputs(", ", out);
        put_value(out, in->lhs);
        fputs(";\n", out);
        return;
    case CC_SSA_VA_START:
        fputs("    va_start ", out);
        put_value(out, in->lhs);
        fputs(";\n", out);
        return;
    case CC_SSA_ASM:
        /* outputs are variables the asm writes; a memory operand is its address, in brackets */
        fputs(in->asm_is_goto ? "    asm_goto " : "    asm ", out);
        fputs(in->asm_volatile ? "volatile" : "pure", out);
        for (k = 0; k < in->asm_out_count; ++k) {
            int v = in->asm_out_values[k];

            fputs(k == 0 ? " out " : ", ", out);
            if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                fputc('[', out);
                put_value(out, asm_operand_value(v));
                fputc(']', out);
            } else {
                put_value(out, v);
            }
        }
        for (k = 0; k < in->asm_in_count; ++k) {
            int v = in->asm_in_values[k];

            fputs(k == 0 ? " in " : ", ", out);
            if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                fputc('[', out);
                put_value(out, asm_operand_value(v));
                fputc(']', out);
            } else {
                put_value(out, v);
            }
        }
        if (in->asm_is_goto) {
            /* a terminator in the text: it falls through to the next block or jumps to a label */
            if (b + 1 < d->block_count) {
                block_name(d, b + 1, name, sizeof(name));
                fprintf(out, " to %s", name);
            }
            for (k = 0; k < in->asm_goto_count; ++k) {
                label_name(d, in->asm_goto_labels[k], name, sizeof(name));
                fprintf(out, ", %s", name);
            }
        }
        fputs(";\n", out);
        return;
    default:
        break;
    }
    if (def < 0) {
        /* a call whose result is not used */
        fputs("    ", out);
        put_rhs(d, in, out);
        fputs(";\n", out);
        return;
    }
    if (def < f->value_count && d->is_var[def]) {
        /* a write to a variable: a MOV writes it directly; anything else computes a temporary first */
        value_type_text(f, def, t, sizeof(t));
        if (in->op == CC_SSA_MOV) {
            fprintf(out, "    set %s %%v%d, ", t, def);
            put_value(out, in->lhs);
            fputs(";\n", out);
        } else {
            unsigned tmp = d->temp++;

            fprintf(out, "    %%t%u = ", tmp);
            put_rhs(d, in, out);
            fprintf(out, ";\n    set %s %%v%d, %%t%u;\n", t, def, tmp);
        }
        return;
    }
    fprintf(out, "    %%v%d = ", def);
    put_rhs(d, in, out);
    fputs(";\n", out);
}

/**
 * Write one function.
 *
 * @return 0, or -1 when out of memory
 */
static int dump_function(const cc_ssa_function_t *f, FILE *out) {
    dump_func_t d;
    char t[16];
    char name[32];
    size_t b;
    size_t i;
    int v;

    memset(&d, 0, sizeof(d));
    d.f = f;
    if (dump_find_blocks(&d) != 0 || dump_find_vars(&d) != 0) {
        free(d.blocks);
        free(d.label_block);
        free(d.is_var);
        return -1;
    }
    fprintf(out, "\n  func @%s : fn(", f->name != NULL ? f->name : "");
    for (i = 0; i < f->param_count; ++i) {
        fprintf(out, "%s%s", i > 0 ? ", " : "", value_type_text(f, f->param_values[i], t, sizeof(t)));
    }
    if (f->is_variadic) {
        fputs(f->param_count > 0 ? ", ..." : "...", out);
    }
    /* the function records only its return value's register kind */
    fprintf(out, ") -> %s {\n", f->ret_type == CC_VAL_F64 ? "f64" : "i64");
    for (b = 0; b < d.block_count; ++b) {
        if (!d.blocks[b].reachable) {
            continue;
        }
        block_name(&d, b, name, sizeof(name));
        fprintf(out, "  %s:\n", name);
        if (b == 0) {
            for (v = 0; v < f->value_count; ++v) {
                if (d.is_var[v]) {
                    fprintf(out, "    %%v%d = undef %s;\n", v, value_type_text(f, v, t, sizeof(t)));
                }
            }
        }
        for (i = d.blocks[b].first; i < d.blocks[b].end; ++i) {
            put_instr(&d, b, &f->instrs[i], out);
        }
        if (d.blocks[b].end == d.blocks[b].first || !ends_block(&f->instrs[d.blocks[b].end - 1])) {
            /* lowering falls through here; the text says where to */
            if (b + 1 < d.block_count) {
                block_name(&d, b + 1, name, sizeof(name));
                fprintf(out, "    br %s;\n", name);
            } else {
                fputs("    unreachable;\n", out);
            }
        }
    }
    fputs("  }\n", out);
    free(d.blocks);
    free(d.label_block);
    free(d.is_var);
    return 0;
}

int cc_ssa_dump_module(const cc_ssa_module_t *m, const char *name, int pointer_size, FILE *out) {
    size_t i;

    fputs("module ", out);
    put_escaped(out, name);
    fprintf(out, " {\n  target %s;\n", pointer_size >= 8 ? "lp64" : "ilp32");
    for (i = 0; i < m->global_count; ++i) {
        const cc_ssa_global_t *g = &m->globals[i];

        /* a scalar's size is its element size; an array or structure has size_bytes */
        fprintf(out, "  global @%s size %ld%s;\n", g->name != NULL ? g->name : "",
                g->size_bytes > 0 ? g->size_bytes : g->elem_size, g->has_init ? " init" : "");
    }
    for (i = 0; i < m->func_count; ++i) {
        if (dump_function(&m->funcs[i], out) != 0) {
            return -1;
        }
    }
    fputs("}\n", out);
    return ferror(out) ? -1 : 0;
}
