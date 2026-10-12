#include "as_elf_emit.h"

#include "as_arm.h"

#include "as_x86_encode.h"

#include "as_x86_regs.h"

#include "as_reloc_op.h"

#include "as_cfi.h"

#include "as_dwline.h"

#include "as_x86_vec.h"

#include "as_x86_vex.h"

#include "as_x86_sse3.h"

#include "as_data.h"

#include "as_strmap.h"

#include "as_util.h"

#include "elfobj.h"

#include <ctype.h>

#include <errno.h>

#include <limits.h>

#include <stdarg.h>

#include <stdint.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <strings.h>

#include <sys/stat.h>

#include <unistd.h>

#include "as_emit_int.h"

/* The table: the run's, and not an emitter's alone -- an instruction is
 * encoded as it is read (as_x86_precode()), before there is an emitter. */
static struct {
    struct text_code *slots;
    size_t cap;
    size_t count;
} text_codes;

static const char *first_symbol_in_expr(const as_expr_t *e);

int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out);

static int stmt_is_place_free(const as_stmt_t *st);

static size_t text_code_key(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits, char *key, size_t key_sz);

static int text_code_find(const char *key, size_t n, unsigned char *code, size_t code_cap, size_t *code_len);

static void text_code_keep(const char *key, size_t n, const unsigned char *code, size_t code_len);

static void text_code_free(void);

static int stmt_code_find(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits, int anywhere,
                          unsigned char *code, size_t code_cap, size_t *code_len);

static void stmt_code_keep(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                           const unsigned char *code, size_t code_len);

static int encode_x86_stmt_by_text(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, unsigned x86_code_bits,
                                   const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                                   char *encerr, size_t encerr_sz);

int append_incbin_to_bytebuf(emit_ctx_t *ctx, const as_stmt_t *st, bytebuf_t *buf, const as_directive_t *d);

static int section_name_is_executable(emit_ctx_t *ctx, const char *name);

static int is_numeric_local_label_name(const char *name);

static int numeric_local_label_number(const char *name, int *out);

static int append_directive_data_ctx(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name,
                                     const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                                     const as_directive_t *d);

int encode_x86_stmt_for_layout(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                                      const as_stmt_t *st, unsigned char *code, size_t code_cap,
                                      size_t *code_len, char *encerr, size_t encerr_sz);

static int append_virtual_instruction_bytes(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name,
                                            const as_stmt_t *st, unsigned x86_code_bits);

static int parsed_stmt_index(emit_ctx_t *ctx, const as_stmt_t *needle, size_t *idx_out);

static int label_stmt_index(emit_ctx_t *ctx, const char *name, size_t *idx_out);

static int stmt_range_size_in_section(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                      uint64_t sec_off, unsigned x86_code_bits, uint64_t *size_out);

static int linux_alt_original_range_size(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                         unsigned x86_code_bits, uint64_t *size_out);

static int stmt_declared_label_section(emit_ctx_t *ctx, const as_stmt_t *target_st, const char **section_out);

static const char *section_name_keep(emit_ctx_t *ctx, const char *name);

static int eval_direct_local_branch_target(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st,
                                           uint64_t base_off, unsigned x86_code_bits, const as_expr_t *rel_expr,
                                           size_t branch_len, long long *abs_target_out);

static int x86_branch_may_be_short(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                   const as_expr_t *e, unsigned x86_code_bits);

static int build_stmt_section_at(emit_ctx_t *ctx);

static const as_stmt_t *stmt_fold_label_diffs(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits,
                                              as_stmt_t *tmp, as_operand_t ops[4], as_expr_t consts[4]);

static const uint32_t *get_section_prefix_sums(emit_ctx_t *ctx, const char *section_name,
                                               unsigned x86_code_bits);

static int parse_ymm_reg(const char *name, unsigned *out);

static int parse_zmm_reg(const char *name, unsigned *out);

static int is_local_temp_symbol_name(const char *name);

static int is_local_temp_symbol_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return name[0] == '.' && name[1] == 'L';
}

/*
 * The labels of the source that are its own: defined in it, and named
 * by no .globl, .global or .weak.  A branch to one in the branch's own
 * section goes a distance that is known here, as a branch to a .L label
 * does -- `jmp l` / `l: ret` is EB 00 and has no relocation -- where it
 * had been the long form with a relocation for the linker to work out
 * what the assembler knew.  Sorted; made by as_elf_mark_far_branches().
 */
static char **near_names;

static size_t near_name_count;

static int near_name_cmp(const void *a, const void *b) {
    return as_strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int name_is_near_label(const char *name) {
    return name != NULL && near_name_count != 0 &&
           bsearch(&name, near_names, near_name_count, sizeof(near_names[0]), near_name_cmp) != NULL;
}

static void near_names_free(void) {
    size_t i;

    for (i = 0; i < near_name_count; ++i) {
        free(near_names[i]);
    }
    free(near_names);
    near_names = NULL;
    near_name_count = 0;
}

/* Whether what a branch names is a label whose place the assembler
 * knows: a compiler's .L label, or one of the source's own. */
int expr_is_local_temp_symbol(const as_expr_t *e) {
    /* (With a relocation operator on it, it is the linker's to find,
     * wherever it is.) */
    return e != NULL && e->kind == AS_EXPR_SYMBOL && e->reloc_op == NULL &&
           (is_local_temp_symbol_name(e->symbol) || name_is_near_label(e->symbol));
}

static void asm_var_reset(emit_ctx_t *ctx) {
    size_t i;

    if (ctx == NULL) {
        return;
    }
    for (i = 0; i < ctx->asm_var_count; ++i) {
        free(ctx->asm_vars[i].name);
    }
    free(ctx->asm_vars);
    ctx->asm_vars = NULL;
    ctx->asm_var_count = 0;
    ctx->asm_var_cap = 0;
    for (i = 0; i < ctx->asm_reg_alias_count; ++i) {
        free(ctx->asm_reg_aliases[i].name);
        free(ctx->asm_reg_aliases[i].reg);
    }
    free(ctx->asm_reg_aliases);
    ctx->asm_reg_aliases = NULL;
    ctx->asm_reg_alias_count = 0;
    ctx->asm_reg_alias_cap = 0;
}

static int asm_var_lookup(const emit_ctx_t *ctx, const char *name, long long *out) {
    size_t i;

    if (ctx == NULL || name == NULL || out == NULL) {
        return -1;
    }
    for (i = ctx->asm_var_count; i > 0; --i) {
        const asm_var_t *v = &ctx->asm_vars[i - 1];
        if (v->name != NULL && as_strcmp(v->name, name) == 0) {
            *out = v->value;
            return 0;
        }
    }
    return -1;
}

static int asm_var_set(emit_ctx_t *ctx, const char *name, long long value) {
    size_t i;
    asm_var_t *next;

    if (ctx == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    for (i = 0; i < ctx->asm_var_count; ++i) {
        if (ctx->asm_vars[i].name != NULL && as_strcmp(ctx->asm_vars[i].name, name) == 0) {
            ctx->asm_vars[i].value = value;
            return 0;
        }
    }
    if (ctx->asm_var_count == ctx->asm_var_cap) {
        size_t ncap = ctx->asm_var_cap == 0 ? 32 : ctx->asm_var_cap * 2;
        next = (asm_var_t *)realloc(ctx->asm_vars, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->asm_vars = next;
        ctx->asm_var_cap = ncap;
    }
    ctx->asm_vars[ctx->asm_var_count].name = xstrdup(name);
    if (ctx->asm_vars[ctx->asm_var_count].name == NULL) {
        return -1;
    }
    ctx->asm_vars[ctx->asm_var_count].value = value;
    ctx->asm_var_count++;
    return 0;
}

static const char *asm_reg_alias_lookup(const emit_ctx_t *ctx, const char *name) {
    size_t i;

    if (ctx == NULL || name == NULL) {
        return NULL;
    }
    for (i = ctx->asm_reg_alias_count; i > 0; --i) {
        const asm_reg_alias_t *a = &ctx->asm_reg_aliases[i - 1];
        if (a->name != NULL && as_strcmp(a->name, name) == 0) {
            return a->reg;
        }
    }
    return NULL;
}

static int asm_reg_alias_set(emit_ctx_t *ctx, const char *name, const char *reg) {
    size_t i;
    asm_reg_alias_t *next;

    if (ctx == NULL || name == NULL || name[0] == '\0' || reg == NULL || reg[0] == '\0') {
        return -1;
    }
    for (i = 0; i < ctx->asm_reg_alias_count; ++i) {
        if (ctx->asm_reg_aliases[i].name != NULL && as_strcmp(ctx->asm_reg_aliases[i].name, name) == 0) {
            char *r = xstrdup(reg);
            if (r == NULL) {
                return -1;
            }
            free(ctx->asm_reg_aliases[i].reg);
            ctx->asm_reg_aliases[i].reg = r;
            return 0;
        }
    }
    if (ctx->asm_reg_alias_count == ctx->asm_reg_alias_cap) {
        size_t ncap = ctx->asm_reg_alias_cap == 0 ? 32 : ctx->asm_reg_alias_cap * 2;
        next = (asm_reg_alias_t *)realloc(ctx->asm_reg_aliases, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->asm_reg_aliases = next;
        ctx->asm_reg_alias_cap = ncap;
    }
    ctx->asm_reg_aliases[ctx->asm_reg_alias_count].name = xstrdup(name);
    ctx->asm_reg_aliases[ctx->asm_reg_alias_count].reg = xstrdup(reg);
    if (ctx->asm_reg_aliases[ctx->asm_reg_alias_count].name == NULL ||
        ctx->asm_reg_aliases[ctx->asm_reg_alias_count].reg == NULL) {
        return -1;
    }
    ctx->asm_reg_alias_count++;
    return 0;
}

static const char *asm_reg_alias_for_operand(const emit_ctx_t *ctx, const as_operand_t *op) {
    if (ctx == NULL || op == NULL) {
        return NULL;
    }
    if ((op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
        op->u.expr != NULL && op->u.expr->kind == AS_EXPR_SYMBOL) {
        return asm_reg_alias_lookup(ctx, op->u.expr->symbol);
    }
    if (op->raw != NULL) {
        return asm_reg_alias_lookup(ctx, op->raw);
    }
    return NULL;
}

const as_operand_t *x86_operand_with_reg_alias(const emit_ctx_t *ctx, const as_operand_t *op, as_operand_t *tmp) {
    const char *reg = asm_reg_alias_for_operand(ctx, op);
    const char *base_reg;
    const char *index_reg;

    if (reg == NULL || tmp == NULL) {
        if (op == NULL || op->kind != AS_OPERAND_MEMORY || tmp == NULL) {
            return op;
        }
        base_reg = asm_reg_alias_lookup(ctx, op->u.mem.base_reg);
        index_reg = asm_reg_alias_lookup(ctx, op->u.mem.index_reg);
        if (base_reg == NULL && index_reg == NULL) {
            return op;
        }
        *tmp = *op;
        tmp->u.mem = op->u.mem;
        if (base_reg != NULL) {
            tmp->u.mem.base_reg = (char *)base_reg;
        }
        if (index_reg != NULL) {
            tmp->u.mem.index_reg = (char *)index_reg;
        }
        return tmp;
    }
    *tmp = *op;
    tmp->kind = AS_OPERAND_REGISTER;
    tmp->u.reg = (char *)reg;
    return tmp;
}

static size_t hash_name(const char *name) {
    size_t h;
    size_t prime;

#if UINTPTR_MAX > 0xFFFFFFFFu
    h = 1469598103934665603ull;
    prime = 1099511628211ull;
#else
    h = 2166136261u;
    prime = 16777619u;
#endif

    while (name != NULL && *name != '\0') {
        h ^= (unsigned char)*name++;
        h *= prime;
    }
    return h;
}

static uint32_t section_group_ordinal(const as_section_state_t *sections, size_t index) {
    uint32_t ordinal = 0;
    size_t i;

    if (sections == NULL || index >= sections->count || sections->items[index].group == NULL) {
        return 0;
    }
    /* A section that is not the first of its group has the number the
     * first has: it had none, and was left out of the group. */
    for (i = 0; i < index; ++i) {
        if (sections->items[i].group != NULL &&
            as_strcmp(sections->items[i].group, sections->items[index].group) == 0) {
            index = i;
            break;
        }
    }
    for (i = 0; i <= index; ++i) {
        const char *group = sections->items[i].group;
        size_t j;
        int first = 1;

        if (group == NULL) {
            continue;
        }
        for (j = 0; j < i; ++j) {
            if (sections->items[j].group != NULL && as_strcmp(sections->items[j].group, group) == 0) {
                first = 0;
                break;
            }
        }
        if (!first) {
            continue;
        }
        ordinal++;
        if (i == index) {
            return ordinal;
        }
    }
    return 0;
}

static int bytebuf_reserve(bytebuf_t *b, size_t extra) {
    unsigned char *next;

    /* Reject before either compare uses b->len + extra: if the sum
     * wraps a small value, both the early-return below ("already
     * fits") and the doubling loop ("ncap < need") silently succeed
     * without growing the buffer, and the next memcpy writes past
     * the allocation.  Reachable on 32-bit hosts via large .fill /
     * .skip / sized data directives where the size came from a
     * parsed expression. */
    if (extra > SIZE_MAX - b->len) {
        return -1;
    }
    if (b->len + extra <= b->cap) {
        return 0;
    }
    {
        size_t ncap = b->cap == 0 ? 256 : b->cap;
        while (ncap < b->len + extra) {
            if (ncap > SIZE_MAX / 2) {
                return -1;
            }
            ncap *= 2;
        }
        next = (unsigned char *)realloc(b->data, ncap);
        if (next == NULL) {
            return -1;
        }
        b->data = next;
        b->cap = ncap;
    }
    return 0;
}

int bytebuf_append(bytebuf_t *b, const void *p, size_t n) {
    if (bytebuf_reserve(b, n) != 0) {
        return -1;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    return 0;
}

int bytebuf_append_zeros(bytebuf_t *b, size_t n) {
    if (bytebuf_reserve(b, n) != 0) {
        return -1;
    }
    memset(b->data + b->len, 0, n);
    b->len += n;
    return 0;
}

int bytebuf_append_u64_le(bytebuf_t *b, uint64_t v, unsigned width) {
    unsigned i;

    if (width < 1 || width > 8) {
        return -1;
    }
    if (bytebuf_reserve(b, width) != 0) {
        return -1;
    }
    for (i = 0; i < width; ++i) {
        b->data[b->len + i] = (unsigned char)((v >> (i * 8)) & 0xffu);
    }
    b->len += width;
    return 0;
}

static void write_u64_le_at(unsigned char *p, uint64_t v, unsigned width) {
    unsigned i;

    for (i = 0; i < width; ++i) {
        p[i] = (unsigned char)((v >> (i * 8)) & 0xffu);
    }
}

/* Whether a directive has an argument at this place that is not empty
 * (`.p2align 4,,10` has an empty second). */
int directive_has_arg(const as_directive_t *d, size_t i) {
    return d != NULL && i < d->arg_count && d->args[i] != NULL && d->args[i][0] != '\0';
}

/*
 * The third argument of .align, .balign and .p2align: the most padding
 * that is wanted.  If reaching the alignment takes more, there is to be
 * none.  Returns 1 if `need` bytes are too many, 0 if not or if there is
 * no limit, -1 if the limit is not a number.
 */
int align_exceeds_max_skip(const as_directive_t *d, size_t need) {
    long long max_skip;

    if (!directive_has_arg(d, 2)) {
        return 0;
    }
    if (as_expr_eval_string(d->args[2], NULL, NULL, &max_skip) != AS_EXPR_EVAL_OK || max_skip < 0) {
        return -1;
    }
    return (unsigned long long)need > (unsigned long long)max_skip ? 1 : 0;
}

/*
 * The padding of an alignment: `need` bytes of `fill`.  .balignw and
 * .balignl, and the two of .p2align, pad with a pattern of two bytes or
 * four where one is written, and then the padding must be a whole number
 * of patterns: GNU as refuses "alignment padding (7 bytes) not a multiple
 * of 2", and so does this (-2).  -1 for no memory.
 */
int append_align_fill(bytebuf_t *buf, const as_directive_t *d, size_t need, uint64_t fill) {
    size_t width = d->fill_width > 1 && directive_has_arg(d, 1) ? d->fill_width : 1;

    if (need % width != 0) {
        return -2;
    }
    for (; need > 0; need -= width) {
        if (bytebuf_append_u64_le(buf, fill, width) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * Expressions are as_expr.c's.  These give its three answers the 0 or -1
 * the rest of this file tests for.
 */


/* One integer literal. */
int parse_int64(const char *s, long long *out) {
    return as_expr_parse_number(s, out);
}

/* Text that is an expression of numbers alone. */
int parse_const_expr_string(const char *s, long long *out) {
    if (s == NULL || out == NULL) {
        return -1;
    }
    return as_expr_eval_string(s, NULL, NULL, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

/* An expression of numbers alone. */
int eval_expr_const(const as_expr_t *e, long long *out) {
    return as_expr_eval(e, NULL, NULL, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

/* What .set and = have given a value to; the location counter is not
 * among them. */
static int asm_var_expr_lookup(void *cookie, const char *name, long long *out) {
    if (as_strcmp(name, ".") == 0) {
        return 0;
    }
    return asm_var_lookup((const emit_ctx_t *)cookie, name, out) == 0;
}

/* An expression of numbers and assembly-time variables. */
static int eval_expr_asm_vars(emit_ctx_t *ctx, const as_expr_t *e, long long *out) {
    if (ctx == NULL) {
        return -1;
    }
    return as_expr_eval(e, asm_var_expr_lookup, ctx, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

int eval_arg_asm_vars(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, long long *out) {
    as_expr_t *expr;
    int rc;

    if (ctx == NULL || arg == NULL || out == NULL) {
        return -1;
    }
    if (parse_int64(arg, out) == 0 || parse_const_expr_string(arg, out) == 0) {
        return 0;
    }
    expr = as_parse_expr_string(arg, st != NULL ? st->file : NULL, st != NULL ? st->line : 0);
    if (expr == NULL) {
        return -1;
    }
    rc = eval_expr_asm_vars(ctx, expr, out);
    as_expr_free(expr);
    return rc;
}

static int apply_asm_var_directive(emit_ctx_t *ctx, const as_stmt_t *st, const as_directive_t *d) {
    long long value;
    as_x86_reg_t gr;
    unsigned vr;

    if (ctx == NULL || d == NULL || d->name == NULL ||
        (as_strcmp(d->name, ".set") != 0 && as_strcmp(d->name, ".equ") != 0 && as_strcmp(d->name, ".equiv") != 0) ||
        d->arg_count < 2 || d->args[0] == NULL || d->args[1] == NULL) {
        return 0;
    }
    if (parse_x86_reg(d->args[1], &gr) == 0 ||
        parse_xmm_reg(d->args[1], &vr) == 0 ||
        parse_ymm_reg(d->args[1], &vr) == 0 ||
        parse_zmm_reg(d->args[1], &vr) == 0 ||
        parse_mmx_reg(d->args[1], &vr) == 0) {
        return asm_reg_alias_set(ctx, d->args[0], d->args[1]);
    }
    if (eval_arg_asm_vars(ctx, st, d->args[1], &value) != 0) {
        return 0;
    }
    return asm_var_set(ctx, d->args[0], value);
}

/* Every symbol in `e` that is an absolute one becomes its value. */
static void fold_absolute_symbols(const emit_ctx_t *ctx, as_expr_t *e) {
    long long value;

    if (e == NULL) {
        return;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && e->reloc_op == NULL &&
        asm_var_lookup(ctx, e->symbol, &value) == 0) {
        free(e->symbol);
        e->symbol = NULL;
        e->kind = AS_EXPR_CONST;
        e->value = value;
        return;
    }
    fold_absolute_symbols(ctx, e->lhs);
    fold_absolute_symbols(ctx, e->rhs);
}

/*
 * A symbol that `.set` or `.equ` gave a number is that number where an
 * instruction names it: `mov $K, %eax` is the move of 7, with no
 * relocation, and `add $K, %ebx` has the one-byte immediate 7 fits in.
 *
 * Done once, before anything is measured, and in the order of the source
 * -- a symbol may be set again, and an instruction has the value of the
 * `.set` above it.  Were it done as each instruction is encoded, one
 * measured ahead of its `.set`, as those a branch jumps over are, would
 * be one size then and another when written.
 *
 * (An instruction above the only `.set` of its symbol is not folded, and
 * keeps a relocation against the absolute symbol.  The linker makes the
 * same number of it, in a field as wide as could be needed.)
 */
int fold_absolute_operands(emit_ctx_t *ctx) {
    size_t i;
    size_t j;

    if (ctx == NULL || ctx->parsed == NULL) {
        return -1;
    }
    asm_var_reset(ctx);
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE) {
            if (apply_asm_var_directive(ctx, st, &st->u.directive) != 0) {
                return -1;
            }
            continue;
        }
        if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }
        for (j = 0; j < st->u.instr.operand_count; ++j) {
            as_operand_t *op = &st->u.instr.operands[j];

            if (op->kind == AS_OPERAND_IMMEDIATE) {
                fold_absolute_symbols(ctx, op->u.expr);
            } else if (op->kind == AS_OPERAND_MEMORY) {
                fold_absolute_symbols(ctx, op->u.mem.disp);
            } else if (op->kind == AS_OPERAND_LABEL_REF && !mnemonic_takes_a_target(st->u.instr.mnemonic)) {
                /* `mov K, %eax`: the address K.  A branch to K is left
                 * to the linker, which knows where the branch is. */
                fold_absolute_symbols(ctx, op->u.expr);
                if (op->u.expr != NULL && op->u.expr->kind == AS_EXPR_CONST) {
                    /* What the parser makes of a bare number. */
                    op->kind = AS_OPERAND_IMMEDIATE;
                }
            }
        }
    }
    asm_var_reset(ctx);
    return 0;
}

int expr_has_symbol(const as_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_SYMBOL || e->kind == AS_EXPR_LOCAL_REF) {
        return 1;
    }
    if (expr_has_symbol(e->lhs) || expr_has_symbol(e->rhs)) {
        return 1;
    }
    return 0;
}

int expr_has_local_ref(const as_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_LOCAL_REF) {
        return 1;
    }
    return expr_has_local_ref(e->lhs) || expr_has_local_ref(e->rhs);
}

int raw_is_numeric_local_ref(const char *raw) {
    size_t i = 0;

    if (raw == NULL || raw[0] == '\0') {
        return 0;
    }
    while (raw[i] >= '0' && raw[i] <= '9') {
        i++;
    }
    return i > 0 && (raw[i] == 'f' || raw[i] == 'b') && raw[i + 1] == '\0';
}

/* How long one is: two bytes, and the prefix where the mode wants it. */
static size_t fixed_short_rel_size(const char *mn, unsigned code_bits) {
    int need67 = 0;

    return x86_fixed_short_branch(mn, code_bits, &need67, NULL) == 0 && need67 ? 3u : 2u;
}

static int parse_ymm_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_YMM, 15, out);
}

static int parse_zmm_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_ZMM, 31, out);
}

/*
 * The strings: .ascii, .asciz and .string.  1 with the bytes in `buf`, 0
 * if the directive is none of them, -1 if a literal cannot be read.
 *
 * (This was a second emitter of every data directive -- the numbers,
 * the space, .fill, the alignments, .org and the floats -- behind the
 * one that has the context, append_directive_data_ctx(), which has
 * each of those itself and came here for the strings alone; the floats
 * are numbers by the time anything is emitted.)
 */
static int append_string_directive(bytebuf_t *buf, const as_directive_t *d) {
    size_t i;

    if (d == NULL || d->name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".ascii") == 0 || as_strcmp(d->name, ".asciz") == 0 || as_strcmp(d->name, ".string") == 0) {
        int nul = (as_strcmp(d->name, ".ascii") == 0) ? 0 : 1;
        for (i = 0; i < d->arg_count; ++i) {
            const char *s = d->args[i] != NULL ? d->args[i] : "";
            char *bytes = NULL;
            size_t len = 0;
            if (as_decode_string_literal(s, &bytes, &len) != 0) {
                return -1;
            }
            if (bytebuf_append(buf, bytes, len) != 0) {
                free(bytes);
                return -1;
            }
            free(bytes);
            if (nul && bytebuf_append_zeros(buf, 1) != 0) {
                return -1;
            }
        }
        return 1;
    }
    return 0;
}

const char *section_from_directive(const as_directive_t *d) {
    const char *arg;
    /* The name out of its quotes, good until the next is asked for; as
     * long as the name is, where it had been 255 bytes of it. */
    static char *secbuf;
    static size_t secbuf_cap;
    size_t i;
    size_t n = 0;

    if (d == NULL || d->name == NULL) {
        return NULL;
    }
    if (as_strcmp(d->name, ".text") == 0) return ".text";
    if (as_strcmp(d->name, ".data") == 0) return ".data";
    if (as_strcmp(d->name, ".rodata") == 0) return ".rodata";
    if (as_strcmp(d->name, ".bss") == 0) return ".bss";
    if ((as_strcmp(d->name, ".section") == 0 || as_strcmp(d->name, ".pushsection") == 0) &&
        d->arg_count > 0 && d->args[0] != NULL && d->args[0][0] != '\0') {
        arg = d->args[0];
        while (*arg != '\0' && isspace((unsigned char)*arg)) {
            arg++;
        }
        if (strlen(arg) + 1 > secbuf_cap) {
            char *grown = (char *)realloc(secbuf, strlen(arg) + 1);

            if (grown == NULL) {
                return NULL;
            }
            secbuf = grown;
            secbuf_cap = strlen(arg) + 1;
        }
        if (*arg == '"' || *arg == '\'') {
            char q = *arg++;
            while (*arg != '\0' && *arg != q && n + 1 < secbuf_cap) {
                secbuf[n++] = *arg++;
            }
            secbuf[n] = '\0';
            return secbuf;
        }
        for (i = 0; arg[i] != '\0' && n + 1 < secbuf_cap; ++i) {
            if (isspace((unsigned char)arg[i]) || arg[i] == ',') {
                break;
            }
            secbuf[n++] = arg[i];
        }
        while (n > 0 && isspace((unsigned char)secbuf[n - 1])) {
            n--;
        }
        secbuf[n] = '\0';
        return n > 0 ? secbuf : NULL;
    }
    return NULL;
}

typedef struct {
    char *name;
    bytebuf_t buf;
    /* Whether the last bytes put in it were data and not an
     * instruction: what padding between instructions begins with. */
    int after_data;
} sec_buf_t;

typedef struct {
    sec_buf_t *items;
    size_t count;
    size_t cap;
    as_strmap_t by_name; /* a section's name to its place in items */
} sec_buf_vec_t;

typedef struct {
    char *current;
    char *previous;
    unsigned x86_code_bits;
    char **stack;
    size_t stack_count;
    size_t stack_cap;
} section_track_t;

static void sec_buf_vec_free(sec_buf_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
        free(v->items[i].buf.data);
    }
    free(v->items);
    as_strmap_free(&v->by_name);
    memset(v, 0, sizeof(*v));
}

static sec_buf_t *sec_buf_find(sec_buf_vec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return NULL;
    }
    return as_strmap_get(&v->by_name, name, &i) ? &v->items[i] : NULL;
}

static sec_buf_t *sec_buf_get_or_add(sec_buf_vec_t *v, const char *name) {
    sec_buf_t *next;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    next = sec_buf_find(v, name);
    if (next != NULL) {
        return next;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        sec_buf_t *grow = (sec_buf_t *)realloc(v->items, ncap * sizeof(*grow));
        if (grow == NULL) {
            return NULL;
        }
        v->items = grow;
        v->cap = ncap;
    }
    memset(&v->items[v->count], 0, sizeof(v->items[v->count]));
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return NULL;
    }
    if (as_strmap_put(&v->by_name, v->items[v->count].name, v->count) != 0) {
        free(v->items[v->count].name);
        return NULL;
    }
    v->count++;
    return &v->items[v->count - 1];
}

static int section_track_init(section_track_t *st, unsigned initial_code_bits) {
    if (st == NULL) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->current = xstrdup(".text");
    st->previous = xstrdup(".text");
    st->x86_code_bits = initial_code_bits == 64u ? 64u : (initial_code_bits == 16u ? 16u : 32u);
    if (st->current == NULL || st->previous == NULL) {
        free(st->current);
        free(st->previous);
        memset(st, 0, sizeof(*st));
        return -1;
    }
    return 0;
}

static void section_track_free(section_track_t *st) {
    size_t i;

    if (st == NULL) {
        return;
    }
    for (i = 0; i < st->stack_count; ++i) {
        free(st->stack[i]);
    }
    free(st->stack);
    free(st->current);
    free(st->previous);
    memset(st, 0, sizeof(*st));
}

static int section_track_switch(section_track_t *st, const char *name) {
    char *next;
    char *old;

    if (st == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    next = xstrdup(name);
    if (next == NULL) {
        return -1;
    }
    old = st->current;
    st->current = next;
    free(st->previous);
    st->previous = old;
    return 0;
}

static int section_track_push(section_track_t *st) {
    char **next;

    if (st == NULL || st->current == NULL) {
        return -1;
    }
    /* Two of each push: the section it was written in, and the one
     * before that, which a .popsection brings back as well. */
    if (st->stack_count + 2 > st->stack_cap) {
        size_t ncap = st->stack_cap == 0 ? 8 : st->stack_cap * 2;
        next = (char **)realloc(st->stack, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        st->stack = next;
        st->stack_cap = ncap;
    }
    st->stack[st->stack_count] = xstrdup(st->current);
    if (st->stack[st->stack_count] == NULL) {
        return -1;
    }
    st->stack[st->stack_count + 1] = st->previous != NULL ? xstrdup(st->previous) : NULL;
    if (st->previous != NULL && st->stack[st->stack_count + 1] == NULL) {
        free(st->stack[st->stack_count]);
        return -1;
    }
    st->stack_count += 2;
    return 0;
}

static int section_track_apply_directive(section_track_t *st, const as_directive_t *d) {
    const char *sec_name;
    char *tmp;

    if (st == NULL || d == NULL || d->name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".previous") == 0) {
        if (st->previous == NULL) {
            return 1;
        }
        tmp = st->current;
        st->current = st->previous;
        st->previous = tmp;
        return 1;
    }
    if (as_strcmp(d->name, ".popsection") == 0) {
        if (st->stack_count < 2) {
            return 1;
        }
        st->stack_count -= 2;
        free(st->current);
        free(st->previous);
        st->current = st->stack[st->stack_count];
        st->previous = st->stack[st->stack_count + 1];
        return 1;
    }
    if (as_strcmp(d->name, ".code16") == 0) {
        st->x86_code_bits = 16u;
        return 1;
    }
    if (as_strcmp(d->name, ".code32") == 0) {
        st->x86_code_bits = 32u;
        return 1;
    }
    if (as_strcmp(d->name, ".code64") == 0) {
        st->x86_code_bits = 64u;
        return 1;
    }
    sec_name = section_from_directive(d);
    if (sec_name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".pushsection") == 0 && section_track_push(st) != 0) {
        return -1;
    }
    if (section_track_switch(st, sec_name) != 0) {
        return -1;
    }
    return 1;
}

/*
 * A section of the object, kept by its name for section_for_name(): as
 * it is made, so that it is never looked for in the object, whose
 * search goes through every section -- for a section to a function,
 * every function for every function.  (If it cannot be kept it is
 * looked for.)
 */
static void section_remember(emit_ctx_t *ctx, const char *name, elf_section_t *sec) {
    const char *kept;
    size_t at;

    if (ctx == NULL || name == NULL || sec == NULL || as_strmap_get(&ctx->sec_by_name, name, &at)) {
        return;
    }
    kept = section_name_keep(ctx, name);
    if (ctx->sec_found_count == ctx->sec_found_cap) {
        size_t cap = ctx->sec_found_cap != 0 ? ctx->sec_found_cap * 2 : 16;
        elf_section_t **grown = (elf_section_t **)realloc(ctx->sec_found, cap * sizeof(*grown));

        if (grown == NULL) {
            return;
        }
        ctx->sec_found = grown;
        ctx->sec_found_cap = cap;
    }
    if (kept[0] != '\0' && as_strmap_put(&ctx->sec_by_name, kept, ctx->sec_found_count) == 0) {
        ctx->sec_found[ctx->sec_found_count++] = sec;
    }
}

static elf_section_t *section_for_name(emit_ctx_t *ctx, const char *name) {
    if (ctx == NULL || name == NULL) {
        return NULL;
    }
    if (as_strcmp(name, ".text") == 0) {
        return ctx->text_sec;
    }
    if (as_strcmp(name, ".data") == 0) {
        return ctx->data_sec;
    }
    /* Looked for in the object once, and after that by its name's
     * number: the object's own search goes through every section. */
    {
        elf_section_t *sec;
        size_t at;

        if (as_strmap_get(&ctx->sec_by_name, name, &at)) {
            return ctx->sec_found[at];
        }
        sec = elf_find_section(ctx->obj, name);
        if (sec != NULL) {
            section_remember(ctx, name, sec);
        }
        return sec;
    }
}

/*
 * Give a section its bytes.  A section of no bits -- .bss, .tbss, one
 * declared @nobits -- has a size and no contents, so what is assembled
 * into it can only be zeros; anything else was dropped, and the object
 * said the section was that long and nothing more.  `.long 5` in .bss is
 * refused, as GNU as refuses it.
 */
static int set_section_bytes(emit_ctx_t *ctx, elf_section_t *sec, const char *name, const unsigned char *data,
                             size_t len) {
    const as_section_t *declared = ctx->sections != NULL && name != NULL ? as_sections_find(ctx->sections, name, 0)
                                                                         : NULL;

    if (declared != NULL && declared->type == SHT_NOBITS) {
        size_t i;

        for (i = 0; i < len; ++i) {
            if (data[i] != 0) {
                set_err(ctx, "section %s holds no bits, and a value other than zero is stored in it at offset %lu",
                        name, (unsigned long)i);
                return -1;
            }
        }
    }
    return elf_section_set_data(sec, data, len) == ELF_OK ? 0 : -1;
}

/* Whether the section is flagged as code: it is padded with NOPs. */
static int section_name_has_exec_flag(emit_ctx_t *ctx, const char *name) {
    elf_section_t *sec = section_for_name(ctx, name);
    if (sec == NULL) {
        return 0;
    }
    return (elf_section_flags(sec) & SHF_EXECINSTR) != 0;
}

/*
 * The names of the sections an instruction is written in, whatever their
 * flags: found once, by following the source.
 */
static int find_sections_with_instructions(emit_ctx_t *ctx) {
    section_track_t track;
    size_t i;

    ctx->code_sections_found = 1;
    if (section_track_init(&track, 32u) != 0) {
        return -1;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        size_t k;

        if (st->kind == AS_STMT_DIRECTIVE) {
            if (section_track_apply_directive(&track, &st->u.directive) < 0) {
                section_track_free(&track);
                return -1;
            }
            continue;
        }
        if (st->kind != AS_STMT_INSTRUCTION || track.current == NULL) {
            continue;
        }
        for (k = 0; k < ctx->code_section_count; ++k) {
            if (as_strcmp(ctx->code_sections[k], track.current) == 0) {
                break;
            }
        }
        if (k == ctx->code_section_count) {
            char **next = (char **)realloc(ctx->code_sections, (k + 1) * sizeof(*next));

            if (next == NULL) {
                section_track_free(&track);
                return -1;
            }
            ctx->code_sections = next;
            ctx->code_sections[k] = xstrdup(track.current);
            if (ctx->code_sections[k] == NULL) {
                section_track_free(&track);
                return -1;
            }
            ctx->code_section_count++;
        }
    }
    section_track_free(&track);
    return 0;
}

/*
 * Whether the section's statements are assembled as a section of code
 * is: its instructions encoded, its data and its labels laid out among
 * them.  One flagged as code is, and so is any other an instruction is
 * written in -- .init and .fini opened without flags, a section "a", or
 * .data.  It went by the flag alone, and an instruction anywhere else
 * was left out of the object without a word: `call f` in .init was not
 * there.
 */
static int section_name_is_executable(emit_ctx_t *ctx, const char *name) {
    size_t k;

    if (ctx == NULL || name == NULL) {
        return 0;
    }
    if (section_name_has_exec_flag(ctx, name)) {
        return 1;
    }
    if (!ctx->code_sections_found && ctx->parsed != NULL) {
        (void)find_sections_with_instructions(ctx);
    }
    for (k = 0; k < ctx->code_section_count; ++k) {
        if (as_strcmp(ctx->code_sections[k], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int instruction_has_symbolic_reloc(const as_instruction_t *in) {
    size_t i;

    if (in == NULL) {
        return 0;
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        const as_expr_t *e = NULL;
        if (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) {
            e = op->u.expr;
        } else if (op->kind == AS_OPERAND_MEMORY) {
            e = op->u.mem.disp;
        } else {
            continue;
        }
        if (first_symbol_in_expr(e) != NULL) {
            return 1;
        }
    }
    return 0;
}

void as_elf_warnings_free(as_elf_warnings_t *w) {
    if (w == NULL) {
        return;
    }
    free(w->seen);
    w->seen = NULL;
    w->seen_count = 0;
    w->seen_cap = 0;
}

/*
 * Warn about a statement: once, however many times it is encoded, and
 * not at all where warnings are suppressed.
 */
void warn_stmt(const as_elf_cfg_t *cfg, const as_stmt_t *st, const char *text) {
    as_elf_warnings_t *w = cfg->warnings;

    if (w != NULL) {
        size_t i;

        w->asked++;
        if (w->probe) {
            return;
        }
        if (w->suppress) {
            return;
        }
        for (i = 0; i < w->seen_count; ++i) {
            if (w->seen[i] == (const void *)st) {
                return;
            }
        }
        if (w->seen_count == w->seen_cap) {
            size_t ncap = w->seen_cap == 0 ? 16 : w->seen_cap * 2;
            const void **next = (const void **)realloc(w->seen, ncap * sizeof(*next));

            if (next != NULL) {
                w->seen = next;
                w->seen_cap = ncap;
            }
        }
        if (w->seen_count < w->seen_cap) {
            w->seen[w->seen_count++] = st;
        }
        w->count++;
    }
    {
        /* Where the user wrote it, not where it stands in the expanded
         * source; and the macro it is of the expansion of. */
        char written[1024];
        char shown[1024];

        snprintf(written, sizeof(written), "%s:%u: Warning: %s\n", st->file != NULL ? st->file : "<input>", st->line,
                 text);
        as_source_positions_in_text(written, shown, sizeof(shown));
        fputs(shown, stderr);
    }
}

/* Whether an expression is a symbol with the @TLSCALL operator on it. */
int expr_is_tlscall(const as_expr_t *e) {
    return e != NULL && e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && e->reloc_op != NULL &&
           strcasecmp(e->reloc_op, "TLSCALL") == 0;
}

int eval_local_rel_expr_virtual(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st, uint64_t base_off,
                                       unsigned x86_code_bits, const as_expr_t *e, long long *out);

static unsigned directive_fixed_scalar_width(const as_directive_t *d) {
    if (d == NULL || d->name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".byte") == 0) return 1;
    if (as_strcmp(d->name, ".word") == 0 || as_strcmp(d->name, ".short") == 0 ||
        as_strcmp(d->name, ".hword") == 0 || as_strcmp(d->name, ".2byte") == 0) return 2;
    if (as_strcmp(d->name, ".long") == 0 || as_strcmp(d->name, ".4byte") == 0) return 4;
    if (as_strcmp(d->name, ".quad") == 0 || as_strcmp(d->name, ".8byte") == 0) return 8;
    return 0;
}

/*
 * A section's name, kept: the same pointer for the same name, good until
 * the object is made.  What is found out about where a label is is
 * handed about as one of these, where it had been copied into arrays of
 * 128 bytes -- and a section whose name was longer was another section,
 * or none.
 */
static const char *section_name_keep(emit_ctx_t *ctx, const char *name) {
    size_t i;
    char *copy;

    if (name == NULL || name[0] == '\0' || ctx == NULL) {
        return "";
    }
    if (as_strmap_get(&ctx->kept_by_name, name, &i)) {
        return ctx->kept_names[i];
    }
    if (ctx->kept_name_count == ctx->kept_name_cap) {
        size_t cap = ctx->kept_name_cap != 0 ? ctx->kept_name_cap * 2 : 16;
        char **grown = (char **)realloc(ctx->kept_names, cap * sizeof(*grown));

        if (grown == NULL) {
            return "";
        }
        ctx->kept_names = grown;
        ctx->kept_name_cap = cap;
    }
    copy = xstrdup(name);
    if (copy == NULL || as_strmap_put(&ctx->kept_by_name, copy, ctx->kept_name_count) != 0) {
        free(copy);
        return "";
    }
    ctx->kept_names[ctx->kept_name_count++] = copy;
    return copy;
}

/* The keys the cache of labels is found by: a place in the source, and
 * a statement.  0, or -1 for a file's name too long to make one of. */
#define VLABEL_KEY_MAX 1100

static int vlabel_place_key(char *key, size_t key_sz, const char *file, unsigned line, int digit) {
    int n = snprintf(key, key_sz, "%u %d %s", line, digit, file);

    return n < 0 || (size_t)n >= key_sz ? -1 : 0;
}

static void vlabel_stmt_key(char *key, size_t key_sz, const as_stmt_t *st) {
    snprintf(key, key_sz, "%p", (const void *)st);
}

static int virtual_label_cache_lookup(emit_ctx_t *ctx, const char *file, unsigned line, int digit,
                                      const char *sym_name, const char **section_out, uint64_t *off_out) {
    size_t i;

    if (ctx == NULL || section_out == NULL || off_out == NULL) {
        return -1;
    }
    /* sym_name == NULL && digit >= 0 used to short-circuit here, which
     * forced every numbered local-label query (e.g. `1f`, `2b`) to redo
     * the full virtual-layout walk even after the first call had already
     * populated the cache. The push path stores (file, line, digit) for
     * numbered labels, so the loop below can find them. */
    if (sym_name == NULL && digit < 0) {
        return -1;
    }
    {
        virtual_label_cache_t *c;
        char key[VLABEL_KEY_MAX];

        if (sym_name != NULL) {
            if (!as_strmap_get(&ctx->vlabel_by_name, sym_name, &i)) {
                return -1;
            }
        } else if (file == NULL || vlabel_place_key(key, sizeof(key), file, line, digit) != 0 ||
                   !as_strmap_get(&ctx->vlabel_by_place, key, &i)) {
            return -1;
        }
        c = &ctx->vlabel_cache[i];
        if (c->off == UINT64_MAX) {
            return -1;
        }
        *section_out = section_name_keep(ctx, c->section);
        *off_out = c->off;
        return 0;
    }
    return -1;
}

static int virtual_label_cache_lookup_stmt(emit_ctx_t *ctx, const as_stmt_t *stmt, const char **section_out,
                                           uint64_t *off_out) {
    size_t i;

    if (ctx == NULL || stmt == NULL || section_out == NULL || off_out == NULL) {
        return -1;
    }
    {
        virtual_label_cache_t *c;
        char key[32];

        vlabel_stmt_key(key, sizeof(key), stmt);
        if (!as_strmap_get(&ctx->vlabel_by_stmt, key, &i)) {
            return -1;
        }
        c = &ctx->vlabel_cache[i];
        if (c->off == UINT64_MAX) {
            return -1;
        }
        *section_out = section_name_keep(ctx, c->section);
        *off_out = c->off;
        return 0;
    }
}

static int virtual_label_cache_push(emit_ctx_t *ctx, const as_stmt_t *st, const as_label_def_t *label,
                                    const char *section, uint64_t off) {
    virtual_label_cache_t *next;
    virtual_label_cache_t *c;
    int digit = -1;

    if (ctx == NULL || label == NULL || label->name == NULL || section == NULL) {
        return -1;
    }
    (void)numeric_local_label_number(label->name, &digit);
    if (digit < 0 && virtual_label_cache_lookup(ctx, label->file, label->line, digit, label->name,
                                                &(const char *){NULL}, &(uint64_t){0}) == 0) {
        return 0;
    }
    if (ctx->vlabel_count == ctx->vlabel_cap) {
        size_t ncap = ctx->vlabel_cap == 0 ? 256 : ctx->vlabel_cap * 2;
        next = (virtual_label_cache_t *)realloc(ctx->vlabel_cache, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->vlabel_cache = next;
        ctx->vlabel_cap = ncap;
    }
    c = &ctx->vlabel_cache[ctx->vlabel_count++];
    memset(c, 0, sizeof(*c));
    c->name = xstrdup(label->name);
    c->file = xstrdup(label->file != NULL ? label->file : "");
    c->section = xstrdup(section);
    c->line = label->line;
    c->digit = digit;
    c->off = off;
    c->stmt = st;
    if (c->name == NULL || c->file == NULL || c->section == NULL) {
        return -1;
    }
    {
        char key[VLABEL_KEY_MAX];
        size_t at = ctx->vlabel_count - 1;
        size_t was;

        /* By name the first of the name is found, by place the first
         * at the place, by statement the last. */
        if (!as_strmap_get(&ctx->vlabel_by_name, c->name, &was) &&
            as_strmap_put(&ctx->vlabel_by_name, c->name, at) != 0) {
            return -1;
        }
        if (vlabel_place_key(key, sizeof(key), c->file, c->line, c->digit) == 0 &&
            !as_strmap_get(&ctx->vlabel_by_place, key, &was)) {
            c->place_key = xstrdup(key);
            if (c->place_key == NULL || as_strmap_put(&ctx->vlabel_by_place, c->place_key, at) != 0) {
                return -1;
            }
        }
        vlabel_stmt_key(key, sizeof(key), st);
        c->stmt_key = xstrdup(key);
        if (c->stmt_key == NULL || as_strmap_put(&ctx->vlabel_by_stmt, c->stmt_key, at) != 0) {
            return -1;
        }
    }
    return 0;
}

static void virtual_label_cache_free(emit_ctx_t *ctx) {
    size_t i;

    if (ctx == NULL) {
        return;
    }
    /* (Where the rows of the line table stand is of the layout too.) */
    ctx->line_rows_valid = 0;
    for (i = 0; i < ctx->vlabel_count; ++i) {
        free(ctx->vlabel_cache[i].name);
        free(ctx->vlabel_cache[i].file);
        free(ctx->vlabel_cache[i].section);
        free(ctx->vlabel_cache[i].place_key);
        free(ctx->vlabel_cache[i].stmt_key);
    }
    as_strmap_free(&ctx->vlabel_by_name);
    as_strmap_free(&ctx->vlabel_by_place);
    as_strmap_free(&ctx->vlabel_by_stmt);
    free(ctx->vlabel_cache);
    ctx->vlabel_cache = NULL;
    ctx->vlabel_count = 0;
    ctx->vlabel_cap = 0;
}

static int find_label_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                       const char *file, unsigned line, int digit, const char *sym_name,
                                       const char **section_out, uint64_t *off_out) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || section_out == NULL || off_out == NULL || (sym_name == NULL && file == NULL)) {
        return -1;
    }
    *section_out = "";
    *off_out = 0;
    if (virtual_label_cache_lookup(ctx, file, line, digit, sym_name, section_out, off_out) == 0) {
        return 0;
    }
    /* Fast path: look up the target stmt by label name (O(N) once via the
     * parsed array, then re-used) and read its virtual offset from the
     * per-section prefix-sum table that get_section_prefix_sums builds
     * on demand. This avoids the expensive section_track + encoder walk
     * below for the common case of resolving a named or numbered local
     * label that exists in the parse. */
    if (build_stmt_section_at(ctx) == 0 && ctx->parsed != NULL) {
        const as_stmt_t *target_st = NULL;
        size_t target_idx = 0;
        /* A name is looked up; a numeric label is found by going
         * through them. */
        if (sym_name != NULL && label_stmt_index(ctx, sym_name, &target_idx) == 0) {
            target_st = &ctx->parsed->items[target_idx];
        }
        for (i = 0; sym_name == NULL && i < ctx->parsed->count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];
            size_t j;
            for (j = 0; j < st->label_count; ++j) {
                int matches;
                if (sym_name != NULL) {
                    matches = (st->labels[j].name != NULL && as_strcmp(st->labels[j].name, sym_name) == 0);
                } else {
                    int label_digit = -1;
                    matches = (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 &&
                               label_digit == digit && st->labels[j].line == line &&
                               st->labels[j].file != NULL && file != NULL &&
                               as_strcmp(st->labels[j].file, file) == 0);
                }
                if (matches) {
                    target_st = st;
                    target_idx = i;
                    break;
                }
            }
            if (target_st != NULL) {
                break;
            }
        }
        if (target_st != NULL && target_idx < ctx->stmt_section_at_count &&
            ctx->stmt_section_at[target_idx] != NULL) {
            const char *target_section = ctx->stmt_section_at[target_idx];
            const uint32_t *pfx;
            unsigned bits = ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                            (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u);
            pfx = get_section_prefix_sums(ctx, target_section, bits);
            if (pfx != NULL && target_idx < ctx->section_prefix_count + ctx->parsed->count) {
                *section_out = section_name_keep(ctx, target_section);
                *off_out = pfx[target_idx];
                /* Also push into vlabel_cache so future lookups hit the
                 * existing fast path. */
                {
                    size_t k;
                    for (k = 0; k < target_st->label_count; ++k) {
                        (void)virtual_label_cache_push(ctx, target_st, &target_st->labels[k],
                                                       target_section, pfx[target_idx]);
                    }
                }
                return 0;
            }
        }
        /* The label doesn't exist in this translation unit. Don't walk
         * the entire parse just to confirm an external symbol can't be
         * resolved virtually — return failure so the caller emits a
         * relocation instead. This is critical for files with thousands
         * of `.long external_sym` directives (Linux's syscall tables),
         * each of which used to drive a full O(N) encoder walk. */
        if (target_st == NULL) {
            return -1;
        }
    }
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        size_t j;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        for (j = 0; j < st->label_count; ++j) {
            int matches = 0;
            if (virtual_label_cache_push(ctx, st, &st->labels[j], track.current != NULL ? track.current : "",
                                         (uint64_t)sb->buf.len) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }

            if (sym_name != NULL) {
                matches = (st->labels[j].name != NULL && as_strcmp(st->labels[j].name, sym_name) == 0);
            } else {
                int label_digit = -1;
                matches = (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 &&
                       label_digit == digit &&
                       st->labels[j].line == line &&
                       st->labels[j].file != NULL &&
                       as_strcmp(st->labels[j].file, file) == 0);
            }
            if (matches) {
                *section_out = section_name_keep(ctx, track.current);
                *off_out = (uint64_t)sb->buf.len;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return 0;
            }
        }
        if (st == base_st && st->kind == AS_STMT_DIRECTIVE) {
            continue;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            {
                unsigned scalar_width = directive_fixed_scalar_width(&st->u.directive);
                if (scalar_width != 0) {
                    if (bytebuf_append_zeros(&sb->buf, st->u.directive.arg_count * (size_t)scalar_width) != 0) {
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    continue;
                }
            }
            ctx->virtual_scanning++;
            {
                int arc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                                    track.x86_code_bits, &st->u.directive);
                ctx->virtual_scanning--;
                if (arc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
                }
            }
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            if (append_virtual_instruction_bytes(ctx, &sb->buf, track.current, st, track.x86_code_bits) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return -1;
}

static int resolve_local_ref_target_line(emit_ctx_t *ctx, const char *file, unsigned ref_line,
                                         int digit, int forward, unsigned *line_out) {
    size_t i;
    int found = 0;
    unsigned best = 0;

    if (ctx == NULL || file == NULL || line_out == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        size_t j;
        for (j = 0; j < st->label_count; ++j) {
            int label_digit = -1;
            if (numeric_local_label_number(st->labels[j].name, &label_digit) != 0 ||
                label_digit != digit || st->labels[j].file == NULL ||
                as_strcmp(st->labels[j].file, file) != 0) {
                continue;
            }
            if (forward) {
                if (st->labels[j].line > ref_line && (!found || st->labels[j].line < best)) {
                    best = st->labels[j].line;
                    found = 1;
                }
            } else {
                if (st->labels[j].line <= ref_line && (!found || st->labels[j].line > best)) {
                    best = st->labels[j].line;
                    found = 1;
                }
            }
        }
    }
    if (!found) {
        return -1;
    }
    *line_out = best;
    return 0;
}

static const as_stmt_t *resolve_local_ref_target_stmt(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                                      const char *file, int digit, int forward) {
    size_t i;
    size_t base_idx = 0;
    int have_base = 0;

    if (ctx == NULL || base_st == NULL || file == NULL) {
        return NULL;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        if (&ctx->parsed->items[i] == base_st) {
            base_idx = i;
            have_base = 1;
            break;
        }
    }
    if (!have_base) {
        return NULL;
    }
    if (base_st->source_seq != 0) {
        /* The statements were reordered: nearest in the source, not in
         * the order they are assembled in. */
        const as_stmt_t *best = NULL;

        for (i = 0; i < ctx->parsed->count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];
            size_t j;

            if (st->source_seq == 0 ||
                (forward ? st->source_seq <= base_st->source_seq : st->source_seq > base_st->source_seq) ||
                (best != NULL &&
                 (forward ? st->source_seq > best->source_seq : st->source_seq < best->source_seq))) {
                continue;
            }
            for (j = 0; j < st->label_count; ++j) {
                int label_digit = -1;

                if (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 && label_digit == digit &&
                    st->labels[j].file != NULL && as_strcmp(st->labels[j].file, file) == 0) {
                    best = st;
                    break;
                }
            }
        }
        return best;
    }
    if (forward) {
        for (i = base_idx + 1; i < ctx->parsed->count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];
            size_t j;
            for (j = 0; j < st->label_count; ++j) {
                int label_digit = -1;
                if (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 &&
                    label_digit == digit &&
                    st->labels[j].file != NULL &&
                    as_strcmp(st->labels[j].file, file) == 0) {
                    return st;
                }
            }
        }
    } else {
        for (i = base_idx + 1; i > 0; --i) {
            const as_stmt_t *st = &ctx->parsed->items[i - 1];
            size_t j;
            for (j = st->label_count; j > 0; --j) {
                int label_digit = -1;
                if (numeric_local_label_number(st->labels[j - 1].name, &label_digit) == 0 &&
                    label_digit == digit &&
                    st->labels[j - 1].file != NULL &&
                    as_strcmp(st->labels[j - 1].file, file) == 0) {
                    return st;
                }
            }
        }
    }
    return NULL;
}

static int find_stmt_virtual_location(emit_ctx_t *ctx, const as_stmt_t *target_st,
                                      const char **section_out, uint64_t *off_out) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;
    int found = 0;

    if (ctx == NULL || target_st == NULL || section_out == NULL || off_out == NULL) {
        return -1;
    }
    *section_out = "";
    *off_out = 0;
    if (virtual_label_cache_lookup_stmt(ctx, target_st, section_out, off_out) == 0) {
        return 0;
    }
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        size_t j;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        for (j = 0; j < st->label_count; ++j) {
            if (virtual_label_cache_push(ctx, st, &st->labels[j], track.current != NULL ? track.current : "",
                                         (uint64_t)sb->buf.len) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
        /* A statement without a label has a place too: a .uleb128 asks
         * for its own, to take `.` from. */
        if (st == target_st && !found) {
            *section_out = section_name_keep(ctx, track.current);
            *off_out = (uint64_t)sb->buf.len;
            found = 1;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            {
                unsigned scalar_width = directive_fixed_scalar_width(&st->u.directive);
                if (scalar_width != 0) {
                    if (bytebuf_append_zeros(&sb->buf, st->u.directive.arg_count * (size_t)scalar_width) != 0) {
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    continue;
                }
            }
            ctx->virtual_scanning++;
            {
                int arc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                                    track.x86_code_bits, &st->u.directive);
                ctx->virtual_scanning--;
                if (arc < 0) {
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
            }
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            if (append_virtual_instruction_bytes(ctx, &sb->buf, track.current, st, track.x86_code_bits) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return found ? 0 : -1;
}

int eval_local_rel_expr_virtual(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st, uint64_t base_off,
                                       unsigned x86_code_bits, const as_expr_t *e, long long *out) {
    long long l;
    long long r;
    uint64_t target_off;
    virtual_addr_value_t target_loc;

    if (ctx == NULL || section_name == NULL || e == NULL || out == NULL) {
        return -1;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        *out = e->value;
        return 0;
    case AS_EXPR_SYMBOL:
        if (e->symbol != NULL && as_strcmp(e->symbol, ".") == 0) {
            *out = 0;
            return 0;
        }
        if (e->symbol == NULL || e->reloc_op != NULL ||
            find_label_virtual_location(ctx, base_st, NULL, 0, 0, e->symbol,
                                        &target_loc.section,
                                        &target_off) != 0 ||
            as_strcmp(target_loc.section, section_name) != 0) {
            return -1;
        }
        *out = (long long)target_off - (long long)base_off;
        return 0;
    case AS_EXPR_LOCAL_REF:
        if (e->src_file == NULL) {
            return -1;
        }
        {
            const as_stmt_t *target_st = resolve_local_ref_target_stmt(ctx, base_st, e->src_file,
                                                                       e->local_digit, e->local_forward);
            if (target_st != NULL &&
                find_stmt_virtual_location(ctx, target_st, &target_loc.section,
                                           &target_off) == 0 &&
                as_strcmp(target_loc.section, section_name) == 0) {
                *out = (long long)target_off - (long long)base_off;
                return 0;
            }
        }
        {
            unsigned target_line = 0;
            if (e->local_resolved) {
                target_line = e->local_target_line;
            } else if (resolve_local_ref_target_line(ctx, e->src_file, e->src_line,
                                                     e->local_digit, e->local_forward,
                                                     &target_line) != 0) {
                return -1;
            }
            if (find_label_virtual_location(ctx, base_st,
                                            e->src_file, target_line, e->local_digit,
                                            NULL, &target_loc.section,
                                            &target_off) != 0 ||
                as_strcmp(target_loc.section, section_name) != 0) {
                return -1;
            }
        }
        *out = (long long)target_off - (long long)base_off;
        return 0;
    case AS_EXPR_UNARY:
        if (eval_local_rel_expr_virtual(ctx, section_name, base_st, base_off, x86_code_bits, e->lhs, &l) != 0) {
            return -1;
        }
        if (e->op == AS_EXPR_OP_NEG) {
            *out = -l;
            return 0;
        }
        if (e->op == AS_EXPR_OP_BNOT) {
            *out = ~l;
            return 0;
        }
        return -1;
    case AS_EXPR_BINARY:
        if (eval_local_rel_expr_virtual(ctx, section_name, base_st, base_off, x86_code_bits, e->lhs, &l) != 0 ||
            eval_local_rel_expr_virtual(ctx, section_name, base_st, base_off, x86_code_bits, e->rhs, &r) != 0) {
            return -1;
        }
        switch (e->op) {
        case AS_EXPR_OP_ADD: *out = l + r; return 0;
        case AS_EXPR_OP_SUB: *out = l - r; return 0;
        case AS_EXPR_OP_MUL: *out = l * r; return 0;
        case AS_EXPR_OP_DIV: if (r == 0) return -1; *out = l / r; return 0;
        case AS_EXPR_OP_MOD: if (r == 0) return -1; *out = l % r; return 0;
        case AS_EXPR_OP_OR: *out = l | r; return 0;
        case AS_EXPR_OP_AND: *out = l & r; return 0;
        case AS_EXPR_OP_XOR: *out = l ^ r; return 0;
        case AS_EXPR_OP_SHL: *out = l << (r & 63); return 0;
        case AS_EXPR_OP_SHR: *out = l >> (r & 63); return 0;
        case AS_EXPR_OP_EQ: *out = (l == r) ? -1 : 0; return 0;
        case AS_EXPR_OP_NE: *out = (l != r) ? -1 : 0; return 0;
        case AS_EXPR_OP_LT: *out = (l < r) ? -1 : 0; return 0;
        case AS_EXPR_OP_LE: *out = (l <= r) ? -1 : 0; return 0;
        case AS_EXPR_OP_GT: *out = (l > r) ? -1 : 0; return 0;
        case AS_EXPR_OP_GE: *out = (l >= r) ? -1 : 0; return 0;
        default: return -1;
        }
    default:
        return -1;
    }
}

static int local_ref_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                      const as_expr_t *e, virtual_addr_value_t *out) {
    unsigned target_line = 0;
    uint64_t off = 0;

    if (ctx == NULL || e == NULL || e->kind != AS_EXPR_LOCAL_REF || out == NULL || e->src_file == NULL) {
        return -1;
    }
    {
        const as_stmt_t *target_st = resolve_local_ref_target_stmt(ctx, base_st, e->src_file,
                                                                   e->local_digit, e->local_forward);
        if (target_st != NULL) {
            memset(out, 0, sizeof(*out));
            if (find_stmt_virtual_location(ctx, target_st, &out->section, &off) != 0) {
                return -1;
            }
            out->has_section = 1;
            out->value = (long long)off;
            return 0;
        }
    }
    if (e->local_resolved) {
        target_line = e->local_target_line;
    } else if (resolve_local_ref_target_line(ctx, e->src_file, e->src_line,
                                             e->local_digit, e->local_forward, &target_line) != 0) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (find_label_virtual_location(ctx, base_st, e->src_file, target_line, e->local_digit, NULL,
                                    &out->section, &off) != 0) {
        return -1;
    }
    out->has_section = 1;
    out->value = (long long)off;
    return 0;
}

static int eval_virtual_addr_expr(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                  const as_expr_t *e, virtual_addr_value_t *out) {
    virtual_addr_value_t l;
    virtual_addr_value_t r;

    if (ctx == NULL || e == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    switch (e->kind) {
    case AS_EXPR_CONST:
        out->value = e->value;
        return 0;
    case AS_EXPR_SYMBOL: {
        uint64_t off = 0;

        if (e->symbol == NULL || e->reloc_op != NULL || as_strcmp(e->symbol, ".") == 0) {
            return -1;
        }
        if (find_label_virtual_location(ctx, base_st, NULL, 0, 0, e->symbol,
                                        &out->section, &off) != 0) {
            return -1;
        }
        out->has_section = 1;
        out->value = (long long)off;
        return 0;
    }
    case AS_EXPR_LOCAL_REF:
        return local_ref_virtual_location(ctx, base_st, e, out);
    case AS_EXPR_UNARY:
        if (eval_virtual_addr_expr(ctx, base_st, e->lhs, &l) != 0 || l.has_section) {
            return -1;
        }
        if (e->op == AS_EXPR_OP_NEG) {
            out->value = -l.value;
            return 0;
        }
        if (e->op == AS_EXPR_OP_BNOT) {
            out->value = ~l.value;
            return 0;
        }
        return -1;
    case AS_EXPR_BINARY:
        if (eval_virtual_addr_expr(ctx, base_st, e->lhs, &l) != 0 ||
            eval_virtual_addr_expr(ctx, base_st, e->rhs, &r) != 0) {
            return -1;
        }
        switch (e->op) {
        case AS_EXPR_OP_ADD:
            if (l.has_section && r.has_section) {
                return -1;
            }
            *out = l.has_section ? l : r;
            out->value = l.value + r.value;
            return 0;
        case AS_EXPR_OP_SUB:
            if (l.has_section && r.has_section) {
                if (as_strcmp(l.section, r.section) != 0) {
                    return -1;
                }
                out->has_section = 0;
                out->value = l.value - r.value;
                return 0;
            }
            if (!l.has_section && !r.has_section) {
                out->value = l.value - r.value;
                return 0;
            }
            if (l.has_section) {
                *out = l;
                out->value = l.value - r.value;
                return 0;
            }
            return -1;
        case AS_EXPR_OP_MUL:
        case AS_EXPR_OP_DIV:
        case AS_EXPR_OP_MOD:
        case AS_EXPR_OP_OR:
        case AS_EXPR_OP_AND:
        case AS_EXPR_OP_XOR:
        case AS_EXPR_OP_SHL:
        case AS_EXPR_OP_SHR:
        case AS_EXPR_OP_EQ:
        case AS_EXPR_OP_NE:
        case AS_EXPR_OP_LT:
        case AS_EXPR_OP_LE:
        case AS_EXPR_OP_GT:
        case AS_EXPR_OP_GE:
            if (l.has_section || r.has_section) {
                return -1;
            }
            switch (e->op) {
            case AS_EXPR_OP_MUL: out->value = l.value * r.value; return 0;
            case AS_EXPR_OP_DIV: if (r.value == 0) return -1; out->value = l.value / r.value; return 0;
            case AS_EXPR_OP_MOD: if (r.value == 0) return -1; out->value = l.value % r.value; return 0;
            case AS_EXPR_OP_OR: out->value = l.value | r.value; return 0;
            case AS_EXPR_OP_AND: out->value = l.value & r.value; return 0;
            case AS_EXPR_OP_XOR: out->value = l.value ^ r.value; return 0;
            case AS_EXPR_OP_SHL: out->value = l.value << (r.value & 63); return 0;
            case AS_EXPR_OP_SHR: out->value = l.value >> (r.value & 63); return 0;
            case AS_EXPR_OP_EQ: out->value = (l.value == r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_NE: out->value = (l.value != r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_LT: out->value = (l.value < r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_LE: out->value = (l.value <= r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_GT: out->value = (l.value > r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_GE: out->value = (l.value >= r.value) ? -1 : 0; return 0;
            default: return -1;
            }
        default:
            return -1;
        }
    default:
        return -1;
    }
}

int eval_abs_local_diff_expr_virtual(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                            const as_expr_t *e, long long *out) {
    virtual_addr_value_t v;

    if (out == NULL || eval_virtual_addr_expr(ctx, base_st, e, &v) != 0 || v.has_section) {
        return -1;
    }
    *out = v.value;
    return 0;
}

static int numeric_local_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                          int digit, int forward, virtual_addr_value_t *out) {
    unsigned target_line = 0;
    uint64_t off = 0;

    if (ctx == NULL || base_st == NULL || base_st->file == NULL || out == NULL) {
        return -1;
    }
    {
        const as_stmt_t *target_st = resolve_local_ref_target_stmt(ctx, base_st, base_st->file, digit, forward);
        if (target_st != NULL) {
            memset(out, 0, sizeof(*out));
            if (find_stmt_virtual_location(ctx, target_st, &out->section, &off) != 0) {
                return -1;
            }
            out->has_section = 1;
            out->value = (long long)off;
            return 0;
        }
    }
    if (resolve_local_ref_target_line(ctx, base_st->file, base_st->line, digit, forward, &target_line) != 0) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (find_label_virtual_location(ctx, base_st, base_st->file, target_line, digit, NULL,
                                    &out->section, &off) != 0) {
        return -1;
    }
    out->has_section = 1;
    out->value = (long long)off;
    return 0;
}

int eval_linux_alt_pad_expr(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                   uint64_t sec_off, unsigned x86_code_bits, const char *arg, long long *out) {
    virtual_addr_value_t v744f;
    virtual_addr_value_t v743f;
    virtual_addr_value_t v741b;
    virtual_addr_value_t v740b;
    long long repl_len;
    long long orig_len;
    long long delta;

    if (ctx == NULL || st == NULL || arg == NULL || out == NULL ||
        strstr(arg, "744f-743f") == NULL || strstr(arg, "741b-740b") == NULL) {
        return -1;
    }
    {
        const as_stmt_t *s744 = resolve_local_ref_target_stmt(ctx, st, st->file, 744, 1);
        const as_stmt_t *s743 = resolve_local_ref_target_stmt(ctx, st, st->file, 743, 1);
        const as_stmt_t *s740 = resolve_local_ref_target_stmt(ctx, st, st->file, 740, 0);
        size_t i744;
        size_t i743;
        size_t i740;
        size_t ibase;
        uint64_t repl = 0;
        uint64_t orig = 0;

        if (s744 != NULL && s743 != NULL && s740 != NULL &&
            parsed_stmt_index(ctx, s744, &i744) == 0 &&
            parsed_stmt_index(ctx, s743, &i743) == 0 &&
            parsed_stmt_index(ctx, s740, &i740) == 0 &&
            parsed_stmt_index(ctx, st, &ibase) == 0 &&
            i743 <= i744 && i740 <= ibase &&
            stmt_range_size_in_section(ctx, ".altinstr_replacement", i743, i744,
                                       0, x86_code_bits, &repl) == 0 &&
            section_name != NULL &&
            linux_alt_original_range_size(ctx, section_name, i740, ibase,
                                          x86_code_bits, &orig) == 0) {
            *out = repl > orig ? (long long)(repl - orig) : 0;
            return 0;
        }
    }
    if (numeric_local_virtual_location(ctx, st, 744, 1, &v744f) != 0 ||
        numeric_local_virtual_location(ctx, st, 743, 1, &v743f) != 0 ||
        numeric_local_virtual_location(ctx, st, 740, 0, &v740b) != 0 ||
        !v744f.has_section || !v743f.has_section || !v740b.has_section ||
        as_strcmp(v744f.section, v743f.section) != 0 ||
        section_name == NULL || as_strcmp(section_name, v740b.section) != 0) {
        return -1;
    }
    memset(&v741b, 0, sizeof(v741b));
    v741b.has_section = 1;
    v741b.section = section_name_keep(ctx, section_name);
    v741b.value = (long long)sec_off;
    repl_len = v744f.value - v743f.value;
    orig_len = v741b.value - v740b.value;
    delta = repl_len - orig_len;
    *out = delta > 0 ? delta : 0;
    return 0;
}

int eval_linux_alt_len_expr(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, long long *out) {
    virtual_addr_value_t a;
    virtual_addr_value_t b;

    if (ctx == NULL || st == NULL || arg == NULL || out == NULL) {
        return -1;
    }
    if (as_strcmp(arg, "742b-740b") == 0) {
        const as_stmt_t *s742 = resolve_local_ref_target_stmt(ctx, st, st->file, 742, 0);
        const as_stmt_t *s740 = resolve_local_ref_target_stmt(ctx, st, st->file, 740, 0);
        size_t i742;
        size_t i740;
        uint64_t orig = 0;
        const char *section = "";

        if (s742 != NULL && s740 != NULL &&
            parsed_stmt_index(ctx, s742, &i742) == 0 &&
            parsed_stmt_index(ctx, s740, &i740) == 0 &&
            i740 <= i742 &&
            stmt_declared_label_section(ctx, s740, &section) == 0 &&
            linux_alt_original_range_size(ctx, section, i740, i742,
                                          ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u,
                                          &orig) == 0) {
            *out = (long long)orig;
            return 0;
        }
        if (numeric_local_virtual_location(ctx, st, 742, 0, &a) != 0 ||
            numeric_local_virtual_location(ctx, st, 740, 0, &b) != 0 ||
            !a.has_section || !b.has_section || as_strcmp(a.section, b.section) != 0) {
            return -1;
        }
        *out = a.value - b.value;
        return 0;
    }
    if (as_strcmp(arg, "744f-743f") == 0) {
        if (numeric_local_virtual_location(ctx, st, 744, 1, &a) != 0 ||
            numeric_local_virtual_location(ctx, st, 743, 1, &b) != 0 ||
            !a.has_section || !b.has_section || as_strcmp(a.section, b.section) != 0) {
            return -1;
        }
        *out = a.value - b.value;
        return 0;
    }
    return -1;
}

static int eval_local_align_fill_expr(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                      uint64_t sec_off, unsigned x86_code_bits, const char *arg,
                                      long long *out) {
    const char *plus;
    const char *minus_dot;
    char *align_text;
    long long align = 0;
    long long rel = 0;
    as_expr_t *local;
    int rc = -1;

    if (arg == NULL || strstr(arg, "0b") == NULL || (minus_dot = strstr(arg, "- .")) == NULL ||
        (plus = strchr(arg, '+')) == NULL || plus >= minus_dot) {
        return -1;
    }
    align_text = (char *)malloc((size_t)(minus_dot - plus));
    if (align_text == NULL) {
        return -1;
    }
    memcpy(align_text, plus + 1, (size_t)(minus_dot - plus - 1));
    align_text[minus_dot - plus - 1] = '\0';
    if (parse_const_expr_string(align_text, &align) != 0 && parse_int64(align_text, &align) != 0) {
        goto out;
    }
    local = as_parse_expr_string("0b", st != NULL ? st->file : NULL, st != NULL ? st->line : 0);
    if (local == NULL) {
        goto out;
    }
    if (eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, local, &rel) == 0) {
        *out = align + rel;
        if (*out < 0) {
            *out = 0;
        }
        rc = 0;
    }
    as_expr_free(local);

out:
    free(align_text);
    return rc;
}

static int parsed_stmt_index(emit_ctx_t *ctx, const as_stmt_t *needle, size_t *idx_out) {
    if (ctx == NULL || needle == NULL || idx_out == NULL || ctx->parsed == NULL ||
        ctx->parsed->items == NULL) {
        return -1;
    }
    /* Stmts live in a contiguous array, so index is direct pointer math. */
    if (needle < ctx->parsed->items || needle >= ctx->parsed->items + ctx->parsed->count) {
        return -1;
    }
    *idx_out = (size_t)(needle - ctx->parsed->items);
    return 0;
}

static int local_temp_branch_target_within(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                           const as_expr_t *rel_expr, size_t max_stmt_distance) {
    const as_stmt_t *target_st = NULL;
    size_t base_idx;
    size_t target_idx;
    size_t i;

    if (ctx == NULL || base_st == NULL || rel_expr == NULL ||
        rel_expr->kind != AS_EXPR_SYMBOL ||
        !expr_is_local_temp_symbol(rel_expr) ||
        parsed_stmt_index(ctx, base_st, &base_idx) != 0) {
        return 0;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        size_t j;

        for (j = 0; j < st->label_count; ++j) {
            if (st->labels[j].name != NULL && as_strcmp(st->labels[j].name, rel_expr->symbol) == 0) {
                target_st = st;
                break;
            }
        }
        if (target_st != NULL) {
            break;
        }
    }
    if (target_st == NULL || parsed_stmt_index(ctx, target_st, &target_idx) != 0) {
        return 0;
    }
    if (target_idx > base_idx) {
        return target_idx - base_idx <= max_stmt_distance;
    }
    return base_idx - target_idx <= max_stmt_distance;
}

/* The width in bytes of each value a data directive lays down, or 0 if
 * the directive is not one of those. */
static unsigned data_directive_width(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (as_strcmp(name, ".byte") == 0) {
        return 1;
    }
    if (as_strcmp(name, ".word") == 0 || as_strcmp(name, ".short") == 0 ||
        as_strcmp(name, ".hword") == 0 || as_strcmp(name, ".2byte") == 0) {
        return 2;
    }
    if (as_strcmp(name, ".long") == 0 || as_strcmp(name, ".4byte") == 0) {
        return 4;
    }
    if (as_strcmp(name, ".quad") == 0 || as_strcmp(name, ".8byte") == 0) {
        return 8;
    }
    return 0;
}

/* An alignment or an .org is as long as it takes to get from where it is
 * to where it leads: its size is not the statement's alone and must not
 * be remembered as if it were. */
static int stmt_size_depends_on_offset(const as_stmt_t *st) {
    const char *name;

    if (st == NULL || st->kind != AS_STMT_DIRECTIVE || st->u.directive.name == NULL) {
        return 0;
    }
    name = st->u.directive.name;
    return as_strcmp(name, ".align") == 0 || as_strcmp(name, ".balign") == 0 ||
           as_strcmp(name, ".p2align") == 0 || as_strcmp(name, ".org") == 0;
}

static int stmt_virtual_size_in_section(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                        uint64_t sec_off, unsigned x86_code_bits, size_t *size_out) {
    bytebuf_t tmp;
    size_t cache_idx = (size_t)-1;
    int cache_eligible = 0;

    if (ctx == NULL || section_name == NULL || st == NULL || size_out == NULL) {
        return -1;
    }
    *size_out = 0;
    memset(&tmp, 0, sizeof(tmp));
    /* Cache hit path: when called below the top-level scan (virtual_scanning > 0),
     * branches degrade to a conservative size that does not depend on `sec_off`,
     * so the value is fully determined by the statement. Memoise. */
    if (ctx->virtual_scanning > 0 && ctx->parsed != NULL && parsed_stmt_index(ctx, st, &cache_idx) == 0 &&
        !stmt_size_depends_on_offset(st)) {
        cache_eligible = 1;
        if (cache_idx < ctx->stmt_size_cache_count && ctx->stmt_size_cached != NULL &&
            ctx->stmt_size_cached[cache_idx]) {
            *size_out = ctx->stmt_size_cache[cache_idx];
            return 0;
        }
    }
    if (st->kind == AS_STMT_INSTRUCTION) {
        /* Branch sizing is recursive: computing one branch's exact size may
         * require sizing a range that contains other branches. Two branches
         * straddling each other can drive the recursion to stack overflow on
         * large inputs (the Linux kernel hits >25k frames). Past the top
         * level we fall back to the conservative virtual-byte path, which
         * produces a safe upper bound (rel32) and never recurses further.
         * The top-level call still gets the precise size; only nested
         * sizing of *other* branches is conservatized. */
        if (st->u.instr.operand_count == 1 && is_rel_mnemonic(st->u.instr.mnemonic) &&
            ctx->virtual_scanning == 0) {
            const as_operand_t *op = &st->u.instr.operands[0];
            const as_expr_t *e = op->u.expr;
            if ((ctx->virtual_scanning == 0 ||
                 (ctx->virtual_scanning == 1 && local_temp_branch_target_within(ctx, st, e, 64))) &&
                (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
                e != NULL && e->kind == AS_EXPR_SYMBOL && expr_is_local_temp_symbol(e)) {
                as_elf_cfg_t local_cfg = *ctx->cfg;
                long long abs_target;
                int may_short = x86_branch_may_be_short(ctx, section_name, st, e, x86_code_bits);
                unsigned char code[32];
                size_t code_len = 0;
                char encerr[128];

                local_cfg.x86_code_bits = x86_code_bits;
                local_cfg.is_64 = x86_code_bits == 64u;
                if (eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, e,
                                                    2, &abs_target) == 0 &&
                    emit_resolved_x86_branch_ex(&local_cfg, st, sec_off, abs_target, may_short, code, sizeof(code),
                                             &code_len, encerr, sizeof(encerr)) == 0) {
                    if (code_len != 2 &&
                        eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, e,
                                                        code_len, &abs_target) == 0 &&
                        emit_resolved_x86_branch_ex(&local_cfg, st, sec_off, abs_target, may_short, code, sizeof(code),
                                                 &code_len, encerr, sizeof(encerr)) != 0) {
                        return -1;
                    }
                    *size_out = code_len;
                    return 0;
                }
            }
        }
        if (!section_name_is_executable(ctx, section_name) ||
            append_virtual_instruction_bytes(ctx, &tmp, section_name, st, x86_code_bits) != 0) {
            free(tmp.data);
            return -1;
        }
        *size_out = tmp.len;
        free(tmp.data);
        if (cache_eligible && ctx->parsed != NULL) {
            if (ctx->stmt_size_cache_count < ctx->parsed->count) {
                size_t want = ctx->parsed->count;
                size_t *new_cache = (size_t *)realloc(ctx->stmt_size_cache, want * sizeof(*new_cache));
                unsigned char *new_flags = (unsigned char *)realloc(ctx->stmt_size_cached, want * sizeof(*new_flags));
                if (new_cache != NULL && new_flags != NULL) {
                    if (ctx->stmt_size_cache_count < want) {
                        memset(new_cache + ctx->stmt_size_cache_count, 0,
                               (want - ctx->stmt_size_cache_count) * sizeof(*new_cache));
                        memset(new_flags + ctx->stmt_size_cache_count, 0,
                               (want - ctx->stmt_size_cache_count) * sizeof(*new_flags));
                    }
                    ctx->stmt_size_cache = new_cache;
                    ctx->stmt_size_cached = new_flags;
                    ctx->stmt_size_cache_count = want;
                }
            }
            if (cache_idx < ctx->stmt_size_cache_count && ctx->stmt_size_cached != NULL) {
                ctx->stmt_size_cache[cache_idx] = *size_out;
                ctx->stmt_size_cached[cache_idx] = 1u;
            }
        }
        return 0;
    }
    if (st->kind == AS_STMT_DIRECTIVE) {
        section_track_t track;
        int trc;
        unsigned data_width = data_directive_width(st->u.directive.name);

        /*
         * A directive that lays down values of a fixed width is that
         * width times their number, whatever the values are.  Measuring
         * it by assembling it evaluated them: a jump table's
         * `.long .L15` asked where .L15 is, which measured the branches
         * before it, which measured what lay between each and its label
         * -- this directive among it -- without end.
         */
        if (data_width != 0) {
            *size_out = (size_t)data_width * st->u.directive.arg_count;
            return 0;
        }

        if (section_track_init(&track, x86_code_bits) != 0) {
            return -1;
        }
        free(track.current);
        track.current = xstrdup(section_name);
        if (track.current == NULL) {
            section_track_free(&track);
            return -1;
        }
        trc = section_track_apply_directive(&track, &st->u.directive);
        if (trc < 0) {
            section_track_free(&track);
            return -1;
        }
        if (trc > 0) {
            section_track_free(&track);
            return 0;
        }
        if (append_directive_data_ctx(ctx, &tmp, section_name, st, sec_off, x86_code_bits, &st->u.directive) < 0) {
            free(tmp.data);
            section_track_free(&track);
            return -1;
        }
        *size_out = tmp.len;
        free(tmp.data);
        section_track_free(&track);
        if (cache_eligible && ctx->parsed != NULL) {
            if (ctx->stmt_size_cache_count < ctx->parsed->count) {
                size_t want = ctx->parsed->count;
                size_t *new_cache = (size_t *)realloc(ctx->stmt_size_cache, want * sizeof(*new_cache));
                unsigned char *new_flags = (unsigned char *)realloc(ctx->stmt_size_cached, want * sizeof(*new_flags));
                if (new_cache != NULL && new_flags != NULL) {
                    if (ctx->stmt_size_cache_count < want) {
                        memset(new_cache + ctx->stmt_size_cache_count, 0,
                               (want - ctx->stmt_size_cache_count) * sizeof(*new_cache));
                        memset(new_flags + ctx->stmt_size_cache_count, 0,
                               (want - ctx->stmt_size_cache_count) * sizeof(*new_flags));
                    }
                    ctx->stmt_size_cache = new_cache;
                    ctx->stmt_size_cached = new_flags;
                    ctx->stmt_size_cache_count = want;
                }
            }
            if (cache_idx < ctx->stmt_size_cache_count && ctx->stmt_size_cached != NULL) {
                ctx->stmt_size_cache[cache_idx] = *size_out;
                ctx->stmt_size_cached[cache_idx] = 1u;
            }
        }
        return 0;
    }
    return 0;
}

/* Find or build the prefix-sum array for (section_name, x86_code_bits).
 * Returns NULL on allocation failure. The returned array has parsed->count + 1
 * entries; prefix[i] is the cumulative size of stmts j < i in the named
 * section (under the conservative virtual sizing, i.e. with virtual_scanning
 * incremented so branches degrade to a fixed length). */
static const uint32_t *get_section_prefix_sums(emit_ctx_t *ctx, const char *section_name,
                                               unsigned x86_code_bits) {
    size_t i;
    section_track_t track;
    uint32_t *prefix;
    uint64_t total;
    if (ctx == NULL || ctx->parsed == NULL || section_name == NULL) {
        return NULL;
    }
    for (i = 0; i < ctx->section_prefix_count; ++i) {
        if (ctx->section_prefixes[i].name != NULL &&
            ctx->section_prefixes[i].x86_code_bits == x86_code_bits &&
            as_strcmp(ctx->section_prefixes[i].name, section_name) == 0) {
            return ctx->section_prefixes[i].prefix;
        }
    }
    /* Avoid re-entering during build: the size computation calls back into
     * stmt_virtual_size_in_section which calls back here. The build pass
     * sets section_prefix_building so the recursive call falls through to
     * the per-stmt cache + linear walk and does not try to build another
     * prefix table for a different section. */
    if (ctx->section_prefix_building) {
        return NULL;
    }
    if (ctx->section_prefix_count == ctx->section_prefix_cap) {
        size_t ncap = ctx->section_prefix_cap == 0 ? 4 : ctx->section_prefix_cap * 2;
        void *next = realloc(ctx->section_prefixes, ncap * sizeof(*ctx->section_prefixes));
        if (next == NULL) {
            return NULL;
        }
        ctx->section_prefixes = next;
        ctx->section_prefix_cap = ncap;
    }
    prefix = (uint32_t *)calloc(ctx->parsed->count + 1, sizeof(*prefix));
    if (prefix == NULL) {
        return NULL;
    }
    if (section_track_init(&track, x86_code_bits) != 0) {
        free(prefix);
        return NULL;
    }
    free(track.current);
    track.current = xstrdup(".text");
    if (track.current == NULL) {
        section_track_free(&track);
        free(prefix);
        return NULL;
    }
    total = 0;
    ctx->virtual_scanning++;
    ctx->section_prefix_building = 1;
    prefix[0] = 0;
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                ctx->section_prefix_building = 0;
                ctx->virtual_scanning--;
                section_track_free(&track);
                free(prefix);
                return NULL;
            }
            if (trc > 0) {
                prefix[i + 1] = (uint32_t)total;
                continue;
            }
        }
        if (track.current != NULL && as_strcmp(track.current, section_name) == 0) {
            size_t n = 0;
            /* (In the mode the statement is in, which a .code16 before
             * it may have made another than the file began in.) */
            if (stmt_virtual_size_in_section(ctx, section_name, st, total, track.x86_code_bits, &n) == 0) {
                total += (uint64_t)n;
            }
        }
        /* (A section of more than 4 GiB has no table: it is walked.) */
        if (total > UINT32_MAX) {
            ctx->section_prefix_building = 0;
            ctx->virtual_scanning--;
            section_track_free(&track);
            free(prefix);
            return NULL;
        }
        prefix[i + 1] = (uint32_t)total;
    }
    ctx->section_prefix_building = 0;
    ctx->virtual_scanning--;
    section_track_free(&track);
    {
        struct as_section_prefix_s *slot = &ctx->section_prefixes[ctx->section_prefix_count];
        slot->name = xstrdup(section_name);
        slot->x86_code_bits = x86_code_bits;
        slot->prefix = prefix;
        slot->prefix_count = ctx->parsed->count + 1;
        if (slot->name == NULL) {
            free(prefix);
            return NULL;
        }
        ctx->section_prefix_count++;
        return prefix;
    }
}

static int stmt_range_size_in_section(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                      uint64_t sec_off, unsigned x86_code_bits, uint64_t *size_out) {
    section_track_t track;
    uint64_t total = 0;
    size_t i;
    const uint32_t *prefix;

    (void)sec_off;
    if (ctx == NULL || section_name == NULL || size_out == NULL || start_idx > end_idx ||
        end_idx > ctx->parsed->count) {
        return -1;
    }
    /* Fast path: prefix-sum O(1) lookup. Only safe when not currently
     * building the table itself (which calls us recursively per stmt). */
    if (!ctx->section_prefix_building) {
        prefix = get_section_prefix_sums(ctx, section_name, x86_code_bits);
        if (prefix != NULL) {
            *size_out = prefix[end_idx] - prefix[start_idx];
            return 0;
        }
    }
    if (section_track_init(&track, x86_code_bits) != 0) {
        return -1;
    }
    free(track.current);
    track.current = xstrdup(section_name);
    if (track.current == NULL) {
        section_track_free(&track);
        return -1;
    }
    ctx->virtual_scanning++;
    for (i = start_idx; i < end_idx; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
        }
        if (track.current == NULL || as_strcmp(track.current, section_name) != 0) {
            continue;
        }
        {
            size_t n = 0;
            if (stmt_virtual_size_in_section(ctx, section_name, st, total, x86_code_bits, &n) != 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            total += (uint64_t)n;
        }
    }
    ctx->virtual_scanning--;
    section_track_free(&track);
    *size_out = total;
    return 0;
}

static int linux_alt_original_range_size(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                         unsigned x86_code_bits, uint64_t *size_out) {
    section_track_t track;
    uint64_t total = 0;
    size_t i;

    if (ctx == NULL || section_name == NULL || size_out == NULL || start_idx > end_idx ||
        end_idx > ctx->parsed->count) {
        return -1;
    }
    if (section_track_init(&track, x86_code_bits) != 0) {
        return -1;
    }
    free(track.current);
    track.current = xstrdup(section_name);
    if (track.current == NULL) {
        section_track_free(&track);
        return -1;
    }
    ctx->virtual_scanning++;
    for (i = start_idx; i < end_idx; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
        }
        if (track.current == NULL || as_strcmp(track.current, section_name) != 0) {
            continue;
        }
        if (st->kind == AS_STMT_DIRECTIVE &&
            st->u.directive.name != NULL &&
            (as_strcmp(st->u.directive.name, ".skip") == 0 ||
             as_strcmp(st->u.directive.name, ".space") == 0) &&
            st->u.directive.arg_count >= 1 &&
            st->u.directive.args[0] != NULL &&
            strstr(st->u.directive.args[0], "744f-743f") != NULL &&
            strstr(st->u.directive.args[0], "741b-740b") != NULL) {
            const as_stmt_t *s744 = resolve_local_ref_target_stmt(ctx, st, st->file, 744, 1);
            const as_stmt_t *s743 = resolve_local_ref_target_stmt(ctx, st, st->file, 743, 1);
            size_t i744;
            size_t i743;
            uint64_t repl = 0;

            if (s744 == NULL || s743 == NULL ||
                parsed_stmt_index(ctx, s744, &i744) != 0 ||
                parsed_stmt_index(ctx, s743, &i743) != 0 ||
                i743 > i744 ||
                stmt_range_size_in_section(ctx, ".altinstr_replacement", i743, i744,
                                           0, x86_code_bits, &repl) != 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            if (repl > total) {
                total += repl - total;
            }
            continue;
        }
        {
            size_t n = 0;

            if (stmt_virtual_size_in_section(ctx, section_name, st, total, x86_code_bits, &n) != 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            total += (uint64_t)n;
        }
    }
    ctx->virtual_scanning--;
    section_track_free(&track);
    *size_out = total;
    return 0;
}

/* Lazy build of the per-stmt section table. Walks parsed once and
 * records track.current at each stmt position. Subsequent
 * stmt_declared_label_section calls are O(1). */
static int build_stmt_section_at(emit_ctx_t *ctx) {
    section_track_t track;
    size_t i;
    char **table;
    if (ctx == NULL || ctx->parsed == NULL) {
        return -1;
    }
    if (ctx->stmt_section_at != NULL && ctx->stmt_section_at_count == ctx->parsed->count) {
        return 0;
    }
    table = (char **)calloc(ctx->parsed->count, sizeof(*table));
    if (table == NULL) {
        return -1;
    }
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        free(table);
        return -1;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        table[i] = xstrdup(track.current != NULL ? track.current : "");
        if (table[i] == NULL) {
            section_track_free(&track);
            for (i = 0; i < ctx->parsed->count; ++i) {
                free(table[i]);
            }
            free(table);
            return -1;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                section_track_free(&track);
                for (i = 0; i < ctx->parsed->count; ++i) {
                    free(table[i]);
                }
                free(table);
                return -1;
            }
        }
    }
    section_track_free(&track);
    ctx->stmt_section_at = table;
    ctx->stmt_section_at_count = ctx->parsed->count;
    return 0;
}

static int stmt_declared_label_section(emit_ctx_t *ctx, const as_stmt_t *target_st, const char **section_out) {
    size_t idx;

    if (ctx == NULL || target_st == NULL || section_out == NULL) {
        return -1;
    }
    *section_out = "";
    if (build_stmt_section_at(ctx) != 0) {
        return -1;
    }
    if (parsed_stmt_index(ctx, target_st, &idx) != 0 || idx >= ctx->stmt_section_at_count) {
        return -1;
    }
    if (ctx->stmt_section_at[idx] != NULL) {
        /* The table's own copy: it lasts as long as the context. */
        *section_out = ctx->stmt_section_at[idx];
    }
    return 0;
}

static int label_index_cmp(const void *a, const void *b) {
    const label_index_t *x = (const label_index_t *)a;
    const label_index_t *y = (const label_index_t *)b;
    int c = as_strcmp(x->name, y->name);

    /* The first of a name that is defined twice. */
    return c != 0 ? c : (x->stmt < y->stmt ? -1 : (x->stmt > y->stmt ? 1 : 0));
}

/*
 * The statement a label is on: the first, in the order of the
 * statements, that has it.  Asked for every branch on every pass, by
 * going through every label of the source until this kept them sorted.
 */
static int label_stmt_index(emit_ctx_t *ctx, const char *name, size_t *idx_out) {
    size_t lo, hi;

    if (ctx == NULL || ctx->parsed == NULL || name == NULL) {
        return -1;
    }
    if (!ctx->label_index_made) {
        size_t n = 0;
        size_t i, j;

        for (i = 0; i < ctx->parsed->count; ++i) {
            n += ctx->parsed->items[i].label_count;
        }
        ctx->label_index = n != 0 ? (label_index_t *)malloc(n * sizeof(*ctx->label_index)) : NULL;
        ctx->label_index_count = 0;
        for (i = 0; ctx->label_index != NULL && i < ctx->parsed->count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];

            for (j = 0; j < st->label_count; ++j) {
                if (st->labels[j].name != NULL) {
                    ctx->label_index[ctx->label_index_count].name = st->labels[j].name;
                    ctx->label_index[ctx->label_index_count].stmt = i;
                    ctx->label_index_count++;
                }
            }
        }
        if (ctx->label_index_count != 0) {
            qsort(ctx->label_index, ctx->label_index_count, sizeof(*ctx->label_index), label_index_cmp);
        }
        ctx->label_index_made = 1;
    }
    /* The first entry whose name is not less than `name`. */
    lo = 0;
    hi = ctx->label_index_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (as_strcmp(ctx->label_index[mid].name, name) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == ctx->label_index_count || as_strcmp(ctx->label_index[lo].name, name) != 0) {
        return -1;
    }
    *idx_out = ctx->label_index[lo].stmt;
    return 0;
}

static int eval_direct_local_branch_target(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st,
                                           uint64_t base_off, unsigned x86_code_bits, const as_expr_t *rel_expr,
                                           size_t branch_len, long long *abs_target_out) {
    const as_stmt_t *target_st;
    size_t base_idx;
    size_t target_idx;
    size_t i;
    uint64_t cur;

    if (ctx == NULL || section_name == NULL || base_st == NULL || rel_expr == NULL ||
        abs_target_out == NULL ||
        parsed_stmt_index(ctx, base_st, &base_idx) != 0) {
        return -1;
    }
    if (rel_expr->kind == AS_EXPR_LOCAL_REF) {
        target_st = resolve_local_ref_target_stmt(ctx, base_st, rel_expr->src_file,
                                                  rel_expr->local_digit, rel_expr->local_forward);
    } else if (rel_expr->kind == AS_EXPR_SYMBOL && rel_expr->symbol != NULL) {
        target_st = label_stmt_index(ctx, rel_expr->symbol, &i) == 0 ? &ctx->parsed->items[i] : NULL;
    } else {
        return -1;
    }
    if (target_st == NULL || parsed_stmt_index(ctx, target_st, &target_idx) != 0 || target_idx == base_idx) {
        return -1;
    }
    {
        const char *target_section = "";

        if (stmt_declared_label_section(ctx, target_st, &target_section) != 0 ||
            as_strcmp(target_section, section_name) != 0) {
            return -1;
        }
    }
    (void)i;
    if (target_idx > base_idx) {
        if (stmt_range_size_in_section(ctx, section_name, base_idx + 1, target_idx,
                                       base_off + (uint64_t)branch_len, x86_code_bits, &cur) != 0) {
            return -1;
        }
        *abs_target_out = (long long)(base_off + (uint64_t)branch_len + cur);
        return 0;
    }
    if (stmt_range_size_in_section(ctx, section_name, target_idx, base_idx, base_off,
                                   x86_code_bits, &cur) != 0) {
        return -1;
    }
    *abs_target_out = (long long)base_off - (long long)cur;
    return 0;
}

/*
 * What a jump names, where that is one place and a number of bytes from
 * it: a label, numbered or not, or `.`, alone or with a number added or
 * taken away.  0 with the place's node and the number, -1 for anything
 * else.
 */
static int relax_branch_parts(const as_expr_t *e, const as_expr_t **place, long long *addend) {
    const as_expr_t *other = NULL;

    *addend = 0;
    if (e == NULL) {
        return -1;
    }
    if (e->kind == AS_EXPR_BINARY && e->lhs != NULL && e->rhs != NULL &&
        (e->op == AS_EXPR_OP_ADD || e->op == AS_EXPR_OP_SUB)) {
        if (e->rhs->kind == AS_EXPR_CONST) {
            other = e->lhs;
            *addend = e->op == AS_EXPR_OP_ADD ? e->rhs->value : (long long)(0ull - (unsigned long long)e->rhs->value);
        } else if (e->lhs->kind == AS_EXPR_CONST && e->op == AS_EXPR_OP_ADD) {
            other = e->rhs;
            *addend = e->lhs->value;
        } else {
            return -1;
        }
        e = other;
    }
    if (e->kind != AS_EXPR_LOCAL_REF && !(e->kind == AS_EXPR_SYMBOL && e->symbol != NULL)) {
        return -1;
    }
    *place = e;
    return 0;
}

/*
 * The statement a branch that is sized here goes to, or -1 if it is not
 * one: a `jmp` or a conditional jump of 32- or 64-bit code, to one label
 * of the source -- a numbered one, a compiler's, or any that no other
 * file sees -- in the section the branch is in.
 */
static int relax_branch_target(emit_ctx_t *ctx, size_t idx, const char *section, unsigned code_bits,
                               size_t *target_out) {
    const as_stmt_t *st = &ctx->parsed->items[idx];
    const as_stmt_t *target_st = NULL;
    const as_operand_t *op;
    const as_expr_t *e;
    long long addend;
    char mnbuf[32];
    char suffix = '\0';
    unsigned cc;
    size_t target;

    (void)code_bits;
    if (st->kind != AS_STMT_INSTRUCTION || st->u.instr.operand_count != 1 ||
        st->u.instr.target_in_other_section || !is_rel_mnemonic(st->u.instr.mnemonic) ||
        is_call_mnemonic(st->u.instr.mnemonic) || is_fixed_short_rel_mnemonic(st->u.instr.mnemonic) ||
        normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0 ||
        (!streq_ci(mnbuf, "jmp") && x86_cond_code(mnbuf, &cc) != 0)) {
        return -1;
    }
    op = &st->u.instr.operands[0];
    if ((op->kind != AS_OPERAND_LABEL_REF && op->kind != AS_OPERAND_IMMEDIATE) ||
        (op->raw != NULL && op->raw[0] == '*') || relax_branch_parts(op->u.expr, &e, &addend) != 0) {
        return -1;
    }
    /* `.`: the jump itself. */
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && as_strcmp(e->symbol, ".") == 0) {
        *target_out = idx;
        return 0;
    }
    if (e->kind == AS_EXPR_LOCAL_REF) {
        target_st = resolve_local_ref_target_stmt(ctx, st, e->src_file, e->local_digit, e->local_forward);
    } else if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && expr_is_local_temp_symbol(e) &&
               label_stmt_index(ctx, e->symbol, &target) == 0) {
        target_st = &ctx->parsed->items[target];
    }
    if (target_st == NULL || parsed_stmt_index(ctx, target_st, &target) != 0 ||
        target >= ctx->stmt_section_at_count || ctx->stmt_section_at[target] == NULL ||
        as_strcmp(ctx->stmt_section_at[target], section) != 0) {
        return -1;
    }
    *target_out = target;
    return 0;
}

/* A jump that does not reach in two bytes: `jmp` is five, a conditional
 * one six. */
/* The long form of a jump: one opcode byte or two, a distance of 32
 * bits or -- in 16-bit code, or with a `w` -- of 16, and the operand-
 * size prefix where that is not the code's. */
static size_t relax_long_size(const as_stmt_t *st, unsigned code_bits) {
    char mnbuf[32];
    char suffix = '\0';
    int is_jmp = normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) == 0 &&
                 streq_ci(mnbuf, "jmp");
    int code16 = code_bits == 16u;
    int wide16;

    suffix = x86_branch_size_letter(st, suffix, code_bits);
    wide16 = suffix == 'w' || (code16 && suffix != 'l');
    return (is_jmp ? 1u : 2u) + (wide16 ? 2u : 4u) + (wide16 != code16 ? 1u : 0u);
}

/* What is remembered of where statements are, forgotten: it was worked
 * out with branches of other sizes. */
static void relax_forget_layout(emit_ctx_t *ctx) {
    size_t i;

    ctx->layout_epoch++;
    virtual_label_cache_free(ctx);
    if (ctx->stmt_size_cached != NULL) {
        memset(ctx->stmt_size_cached, 0, ctx->stmt_size_cache_count);
    }
    for (i = 0; i < ctx->section_prefix_count; ++i) {
        free(ctx->section_prefixes[i].name);
        free(ctx->section_prefixes[i].prefix);
    }
    ctx->section_prefix_count = 0;
}

/*
 * Size the branches.
 *
 * A jump to a label of its own section is two bytes if the label is
 * within 127 bytes ahead or 128 behind, and five or six if not; and how
 * far the label is depends on the sizes of the jumps between.  So every
 * one starts short, the statements are laid out, each jump that does not
 * reach is made long, and that is done again until none changes -- which
 * it does, a jump only ever growing.  Then each is as short as it can
 * be, which is what GNU as makes it.
 *
 * Before this there were two guesses.  A jump to a compiler's label was
 * short if the statements between came to 127 bytes with every jump
 * among them counted long, and none were more than 64: right, but long
 * where it need not be.  A jump to a numbered label was counted as two
 * bytes whatever it was written as: after `jmp 9f` over 200 bytes, which
 * is five, every label was taken to be three bytes before its place by
 * whatever worked a distance out -- `jmp a` further down went three
 * bytes short of `a`, and `.long b - a` was three less than it is.
 *
 * 0, or -1 for no memory.  While it runs (relax_state 1) what asks is
 * given the sizes as they stand.
 */
static int relax_branches(emit_ctx_t *ctx) {
    size_t count = ctx->parsed != NULL ? ctx->parsed->count : 0;
    size_t *target = NULL;
    size_t *fixed = NULL;
    size_t *sid = NULL;
    unsigned *region = NULL;
    uint64_t *off = NULL;
    struct relax_at {
        const char *name;
        uint64_t off;
        unsigned region;
    } *at = NULL;
    size_t at_count = 0, at_cap = 0;
    section_track_t track;
    int tracking = 0;
    int changed = 0;
    int moved = 0;
    unsigned round = 0;
    int rc = -1;
    size_t i;

    if (ctx->relax_state != 0) {
        return 0;
    }
    if (count == 0 || ctx->cfg == NULL || ctx->cfg->target != 0 ||
        (ctx->cfg->machine != EM_386 && ctx->cfg->machine != EM_X86_64)) {
        ctx->relax_state = 2;
        return 0;
    }
    if (ctx->branch_short_count < count) {
        unsigned char *next = (unsigned char *)realloc(ctx->branch_short, count);

        if (next == NULL) {
            return -1;
        }
        ctx->branch_short = next;
        ctx->branch_short_count = count;
    }
    memset(ctx->branch_short, 0, ctx->branch_short_count);
    target = (size_t *)malloc(count * sizeof(*target));
    fixed = (size_t *)malloc(count * sizeof(*fixed));
    off = (uint64_t *)malloc(count * sizeof(*off));
    if (target == NULL || fixed == NULL || off == NULL || build_stmt_section_at(ctx) != 0) {
        goto out;
    }
    /* Which statements are the jumps, each of them short to begin with. */
    if (section_track_init(&track, ctx->cfg->is_64 ? 64u : (ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        goto out;
    }
    tracking = 1;
    for (i = 0; i < count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        fixed[i] = (size_t)-1;
        target[i] = (size_t)-1;
        if (st->kind == AS_STMT_DIRECTIVE) {
            if (section_track_apply_directive(&track, &st->u.directive) < 0) {
                goto out;
            }
        } else if (relax_branch_target(ctx, i, track.current != NULL ? track.current : "", track.x86_code_bits,
                                       &target[i]) == 0) {
            ctx->branch_short[i] = 1;
        }
    }
    /*
     * Which section each statement is in, by number: the sections are
     * numbered as they are met, through a table of their names.  (The
     * name was looked for among all the sections at every statement of
     * every round, which for a section to a function is every function
     * for every function.)
     */
    {
        size_t cap = 64;
        size_t *slots;

        while (cap < count * 2) {
            cap *= 2;
        }
        slots = (size_t *)calloc(cap, sizeof(*slots));
        sid = (size_t *)malloc(count * sizeof(*sid));
        region = (unsigned *)malloc(count * sizeof(*region));
        if (slots == NULL || sid == NULL || region == NULL) {
            free(slots);
            goto out;
        }
        for (i = 0; i < count; ++i) {
            const char *name = ctx->stmt_section_at[i];
            size_t h = 2166136261u;
            const char *p;
            size_t s;

            for (p = name; *p != '\0'; ++p) {
                h = (h ^ (unsigned char)*p) * 16777619u;
            }
            for (s = h & (cap - 1); slots[s] != 0 && as_strcmp(at[slots[s] - 1].name, name) != 0; s = (s + 1) & (cap - 1)) {
            }
            if (slots[s] == 0) {
                if (at_count == at_cap) {
                    size_t ncap = at_cap != 0 ? at_cap * 2 : 16;
                    struct relax_at *grown = (struct relax_at *)realloc(at, ncap * sizeof(*grown));

                    if (grown == NULL) {
                        free(slots);
                        goto out;
                    }
                    at = grown;
                    at_cap = ncap;
                }
                at[at_count].name = name;
                at[at_count].off = 0;
                at[at_count].region = 0;
                slots[s] = ++at_count;
            }
            sid[i] = slots[s] - 1;
            /* (And which stretch of its section between alignments,
             * for the rounds below: an alignment is the last of one.) */
            region[i] = at[sid[i]].region;
            {
                const as_stmt_t *st = &ctx->parsed->items[i];

                if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL &&
                    (as_strcmp(st->u.directive.name, ".p2align") == 0 || as_strcmp(st->u.directive.name, ".balign") == 0 ||
                     as_strcmp(st->u.directive.name, ".align") == 0 || as_strcmp(st->u.directive.name, ".org") == 0)) {
                    at[sid[i]].region++;
                }
            }
        }
        free(slots);
    }
    ctx->relax_state = 1;
    ctx->virtual_scanning++;
    /*
     * A round goes down the source once.  Each statement is where the
     * ones before it now put it; a short jump is asked whether it still
     * reaches -- a label behind it is where this round put it, and one
     * ahead where the last round did, moved by as much as the jump
     * itself has been -- and if it does not it is long from there on,
     * in the round that found it.  The rounds end when one changes no
     * jump and moves no statement, so that every distance asked about
     * in it was the true one.
     *
     * (Every jump of a round was asked against the layout of the round
     * before, all at once: a jump that did not reach by the width of
     * some padding was made long though another, made long in the same
     * round, had taken the padding away.  GNU as walks as this does, and
     * what a compiler makes at -O2 came out some bytes different from
     * it in one function in eight.)
     */
    /* (The first round lays the statements out with every jump short,
     * and asks nothing; the second is the first that asks.) */
    while (changed || moved || round < 2) {
        changed = 0;
        moved = 0;
        relax_forget_layout(ctx);
        if (tracking) {
            section_track_free(&track);
        }
        if (section_track_init(&track, ctx->cfg->is_64 ? 64u : (ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
            tracking = 0;
            goto unwind;
        }
        tracking = 1;
        for (i = 0; i < at_count; ++i) {
            at[i].off = 0;
        }
        for (i = 0; i < count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];
            const char *section = track.current != NULL ? track.current : "";
            size_t k = sid[i];
            size_t n = 0;
            long long stretch = round == 0 ? 0 : (long long)at[k].off - (long long)off[i];

            moved |= stretch != 0;
            off[i] = at[k].off;
            if (round != 0 && ctx->branch_short[i] == 1) {
                const as_expr_t *place;
                long long addend;
                long long there;
                long long disp;
                long long pre = (long long)x86_branch_flip16(st) + (long long)x86_branch_hint(st);

                (void)relax_branch_parts(st->u.instr.operands[0].u.expr, &place, &addend);
                there = (long long)off[target[i]];
                /* A label ahead has yet to be moved in this round.  It
                 * will move as this jump has, unless an alignment lies
                 * between, whose padding takes up what came before it:
                 * then it is taken to stay, and if that puts it behind
                 * the jump the question is left for the next round. */
                if (target[i] > i && (stretch < 0 || region[target[i]] == region[i])) {
                    there += stretch;
                }
                disp = there + addend - ((long long)off[i] + 2 + pre);
                if (target[i] > i && there < (long long)off[i]) {
                    disp = 0;
                }
                if (disp < -128 || disp > 127) {
                    ctx->branch_short[i] = 2;
                    changed = 1;
                    /* (What has been remembered of sizes and places in
                     * this round was worked out with the jump short.) */
                    relax_forget_layout(ctx);
                }
            }
            if (st->kind == AS_STMT_DIRECTIVE) {
                int trc = section_track_apply_directive(&track, &st->u.directive);

                if (trc < 0) {
                    goto unwind;
                }
                if (trc > 0) {
                    continue;
                }
                (void)stmt_virtual_size_in_section(ctx, section, st, off[i], track.x86_code_bits, &n);
            } else if (st->kind == AS_STMT_INSTRUCTION) {
                if (ctx->branch_short[i] == 1) {
                    n = 2u + (size_t)x86_branch_flip16(st) + x86_branch_hint(st);
                } else if (ctx->branch_short[i] == 2) {
                    n = relax_long_size(st, track.x86_code_bits) + x86_branch_hint(st);
                } else {
                    /* Any other instruction is as long as it is,
                     * wherever it is: found once. */
                    if (fixed[i] == (size_t)-1 &&
                        stmt_virtual_size_in_section(ctx, section, st, off[i], track.x86_code_bits, &fixed[i]) != 0) {
                        fixed[i] = 0;
                    }
                    n = fixed[i];
                }
            }
            at[k].off += n;
        }
        round++;
    }
    rc = 0;
unwind:
    ctx->virtual_scanning--;
    relax_forget_layout(ctx);
    if (rc != 0) {
        memset(ctx->branch_short, 0, ctx->branch_short_count);
    }
    ctx->relax_state = rc == 0 ? 2 : 0;
out:
    if (tracking) {
        section_track_free(&track);
    }
    free(target);
    free(fixed);
    free(sid);
    free(region);
    free(off);
    free(at);
    return rc;
}

/*
 * Whether the branch `st` to the compiler's local label `e` takes the
 * short form, two bytes with a displacement of one.
 *
 * This is asked in two places that must be given the same answer: where
 * the branch is encoded, and where it is one of the statements another
 * branch measures its own distance across.  They used to be answered
 * differently.  Measured across, a branch within 64 statements of its
 * label was counted as short; encoded, it was short only if the distance
 * came to no more than 127 bytes, which 64 statements easily exceed.  A
 * loop compiled by the system's own cc has a jump back to its top of
 * about 170 bytes and a jump forward over that one to its end: the
 * forward jump counted the backward one as two bytes, it was written as
 * five, and the forward jump landed three bytes short of its label, in
 * the middle of the other.  Every program with a loop died of an illegal
 * instruction.
 *
 * So it is decided once, by relax_branches(), and both are told what it
 * decided.
 */
static int local_temp_branch_is_short(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                      const as_expr_t *e, unsigned x86_code_bits) {
    size_t base_idx;

    (void)section_name;
    (void)e;
    (void)x86_code_bits;
    if (ctx == NULL || ctx->parsed == NULL || st == NULL || parsed_stmt_index(ctx, st, &base_idx) != 0 ||
        relax_branches(ctx) != 0) {
        return 0;
    }
    return base_idx < ctx->branch_short_count && ctx->branch_short[base_idx] == 1;
}

/* Whether the statement is a branch relax_branches() has sized, and
 * then *is_short for which form it has. */
static int branch_is_relaxed(emit_ctx_t *ctx, const as_stmt_t *st, int *is_short) {
    size_t idx;

    if (ctx == NULL || ctx->parsed == NULL || st == NULL || parsed_stmt_index(ctx, st, &idx) != 0 ||
        relax_branches(ctx) != 0 || idx >= ctx->branch_short_count || ctx->branch_short[idx] == 0) {
        return 0;
    }
    *is_short = ctx->branch_short[idx] == 1;
    return 1;
}

/*
 * Where a sized jump goes that names `.`, or a label of the source that
 * is not a numbered one, with a number added: 0 with the place in its
 * section and which form the jump has, -1 if `st` is not such a jump.
 * (A label alone, and a numbered one with a number, were resolved
 * before there was this, and still are where they were.)
 */
/*
 * A call of where it stands, or so many bytes from there: `call .`,
 * `call .+5`.  It is as far as it says, like a jump of the same, and
 * was given a relocation against the section -- with the distance of
 * the wrong end of the instruction in it.
 */
static int call_to_place_target(const as_stmt_t *st, uint64_t sec_off, long long *abs_target) {
    const as_expr_t *place = NULL;
    long long addend = 0;
    const as_operand_t *op;

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION || st->u.instr.operand_count != 1 ||
        !is_call_mnemonic(st->u.instr.mnemonic) || strncasecmp(st->u.instr.mnemonic, "lcall", 5) == 0) {
        return -1;
    }
    op = &st->u.instr.operands[0];
    if ((op->kind != AS_OPERAND_LABEL_REF && op->kind != AS_OPERAND_IMMEDIATE) ||
        (op->raw != NULL && op->raw[0] == '*') || relax_branch_parts(op->u.expr, &place, &addend) != 0 ||
        place->kind != AS_EXPR_SYMBOL || as_strcmp(place->symbol, ".") != 0) {
        return -1;
    }
    *abs_target = (long long)sec_off + addend;
    return 0;
}

static int relaxed_place_branch_target(emit_ctx_t *ctx, const as_stmt_t *st, const char *section_name,
                                       uint64_t sec_off, unsigned x86_code_bits, long long *abs_target,
                                       int *is_short) {
    const as_expr_t *place = NULL;
    long long addend = 0;

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION || st->u.instr.operand_count != 1 ||
        !branch_is_relaxed(ctx, st, is_short) ||
        relax_branch_parts(st->u.instr.operands[0].u.expr, &place, &addend) != 0 || place->kind != AS_EXPR_SYMBOL) {
        return -1;
    }
    if (as_strcmp(place->symbol, ".") == 0) {
        *abs_target = (long long)sec_off + addend;
        return 0;
    }
    if (addend == 0 ||
        eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, place,
                                        (*is_short ? 2 : relax_long_size(st, x86_code_bits)) + x86_branch_hint(st),
                                        abs_target) != 0) {
        return -1;
    }
    *abs_target += addend;
    return 0;
}

/*
 * Whether a branch may be encoded short if its displacement fits.  For
 * one to a compiler's local label that is the decision above; for the
 * others it is as it was.  So is 16-bit code, where the long forms are
 * shorter and the sizes assumed elsewhere are not these.
 */
static int x86_branch_may_be_short(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                   const as_expr_t *e, unsigned x86_code_bits) {
    /* To another section: how far is not known here. */
    int is_short;

    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.target_in_other_section) {
        return 0;
    }
    /* One that was sized is the size it was given. */
    if (branch_is_relaxed(ctx, st, &is_short)) {
        return is_short;
    }
    if (e == NULL || e->kind != AS_EXPR_SYMBOL || !expr_is_local_temp_symbol(e) || x86_code_bits == 16u) {
        return 1;
    }
    return local_temp_branch_is_short(ctx, section_name, st, e, x86_code_bits);
}

static unsigned stmt_local_rel_virtual_len(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                           unsigned x86_code_bits) {
    const as_operand_t *op;
    const as_expr_t *e;
    char mnbuf[32];
    char suffix = '\0';
    unsigned cc;
    int is_local;

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION ||
        st->u.instr.operand_count != 1 ||
        !is_rel_mnemonic(st->u.instr.mnemonic) ||
        normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        return 0;
    }
    op = &st->u.instr.operands[0];
    if (op->kind != AS_OPERAND_LABEL_REF && op->kind != AS_OPERAND_IMMEDIATE) {
        return 0;
    }
    /* A jump that was sized is the size it was given. */
    if (branch_is_relaxed(ctx, st, &is_local)) {
        return is_local ? 2u + (unsigned)x86_branch_flip16(st) : (unsigned)relax_long_size(st, x86_code_bits);
    }
    e = op->u.expr;
    is_local = ((e != NULL && expr_has_local_ref(e)) ||
                (section_name != NULL && as_strcmp(section_name, ".altinstr_replacement") != 0 &&
                 e != NULL && expr_is_local_temp_symbol(e) &&
                 (x86_code_bits == 16u ? local_temp_branch_target_within(ctx, st, e, 64)
                                       : (e->kind == AS_EXPR_SYMBOL &&
                                          local_temp_branch_is_short(ctx, section_name, st, e, x86_code_bits)))) ||
                raw_is_numeric_local_ref(op->raw));
    /*
     * The distance is of 16 bits in 16-bit code and with a `w`, of 32
     * otherwise, and has the operand-size prefix where it is not the
     * code's.  A call was five bytes and a jump five or six whatever
     * the code: in 16-bit code every label behind a call or a long
     * jump was thought two bytes further on than it is, and a branch
     * over one went two bytes past where it was sent.
     */
    {
        int code16 = x86_code_bits == 16u;
        int wide16 = (suffix = x86_branch_size_letter(st, suffix, x86_code_bits)) == 'w' || (code16 && suffix != 'l');
        unsigned field = wide16 ? 2u : 4u;
        unsigned prefix = (wide16 != code16) ? 1u : 0u;

        if (is_call_mnemonic(st->u.instr.mnemonic)) {
            return 1u + field + prefix;
        }
        if (is_fixed_short_rel_mnemonic(st->u.instr.mnemonic)) {
            /* (And the address-size prefix, written: `addr16 loop`
             * counts in %cx.) */
            return (unsigned)fixed_short_rel_size(st->u.instr.mnemonic, x86_code_bits) +
                   ((st->u.instr.prefixes & AS_PREFIX_ADDR16) != 0 ? 1u : 0u);
        }
        if (is_local) {
            return 2u + (unsigned)x86_branch_flip16(st);
        }
        if (streq_ci(mnbuf, "jmp")) {
            return 1u + field + prefix;
        }
        if (x86_cond_code(mnbuf, &cc) == 0) {
            return 2u + field + prefix;
        }
    }
    return 0;
}

/*
 * ARM and AArch64.
 *
 * An instruction of theirs is as long as it is wherever it stands, so
 * there is nothing to relax: it is measured by encoding it with no
 * labels known, and encoded again once they are.
 */
typedef struct {
    emit_ctx_t *ctx;
    const char *section;
} arm_label_ctx_t;

static int arm_label(void *cookie, const char *name, uint64_t *offset) {
    arm_label_ctx_t *lc = (arm_label_ctx_t *)cookie;
    const char *sec = "";
    uint64_t off = 0;

    if (find_label_virtual_location(lc->ctx, NULL, NULL, 0, 0, name, &sec, &off) != 0 || sec == NULL ||
        lc->section == NULL || as_strcmp(sec, lc->section) != 0) {
        return 1;
    }
    *offset = off;
    return 0;
}

static int arm_encode_stmt(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, int placed,
                           unsigned code_bits, const as_stmt_t *st, unsigned char *code, size_t code_cap,
                           size_t *code_len, char *encerr, size_t encerr_sz) {
    const char *ops[16];
    arm_label_ctx_t lc;
    as_arm_req_t rq;
    size_t i;
    int rc;

    if (st->u.instr.operand_count > sizeof(ops) / sizeof(ops[0])) {
        snprintf(encerr, encerr_sz, "'%s' has too many operands", st->u.instr.mnemonic);
        return -1;
    }
    for (i = 0; i < st->u.instr.operand_count; ++i) {
        ops[i] = st->u.instr.operands[i].raw != NULL ? st->u.instr.operands[i].raw : "";
    }
    lc.ctx = ctx;
    lc.section = section_name;
    memset(&rq, 0, sizeof(rq));
    rq.mnemonic = st->u.instr.mnemonic;
    rq.ops = ops;
    rq.op_count = st->u.instr.operand_count;
    rq.pc = sec_off;
    rq.thumb = code_bits == 16u;
    rq.in_it = (int)st->u.instr.arm_in_it;
    rq.label = placed ? arm_label : NULL;
    rq.cookie = &lc;
    rc = ctx->cfg->target == EM_AARCH64 ? as_a64_encode(&rq, &ctx->arm_out, encerr, encerr_sz)
                                        : as_arm_encode(&rq, &ctx->arm_out, encerr, encerr_sz);
    if (rc != 0) {
        return -1;
    }
    if (ctx->arm_out.len > code_cap) {
        return -1;
    }
    memcpy(code, ctx->arm_out.bytes, ctx->arm_out.len);
    *code_len = ctx->arm_out.len;
    return 0;
}

/* Padding where instructions go: the instruction that does nothing,
 * behind as many zeros as it takes to stand where one may. */
int append_arm_nops(bytebuf_t *buf, size_t need, unsigned target, unsigned code_bits) {
    static const unsigned char arm_nop[4] = {0x00, 0xf0, 0x20, 0xe3};
    static const unsigned char thumb_nop[2] = {0x00, 0xbf};
    static const unsigned char a64_nop[4] = {0x1f, 0x20, 0x03, 0xd5};
    const unsigned char *nop = target == EM_AARCH64 ? a64_nop : (code_bits == 16u ? thumb_nop : arm_nop);
    size_t unit = target != EM_AARCH64 && code_bits == 16u ? 2 : 4;

    if (need % unit != 0 && bytebuf_append_zeros(buf, need % unit) != 0) {
        return -1;
    }
    for (need -= need % unit; need != 0; need -= unit) {
        if (bytebuf_append(buf, nop, unit) != 0) {
            return -1;
        }
    }
    return 0;
}

/* A statement between two labels that may be one size or another, so
 * that how far apart the labels are is not known until all are laid
 * out: a jump that is sized, an alignment, a change of code. */
static int stmt_is_of_variable_size(const as_stmt_t *st) {
    if (st->kind == AS_STMT_INSTRUCTION) {
        const as_instruction_t *in = &st->u.instr;

        return in->mnemonic != NULL && in->operand_count == 1 && is_rel_mnemonic(in->mnemonic) &&
               !is_call_mnemonic(in->mnemonic) && !is_fixed_short_rel_mnemonic(in->mnemonic) &&
               (in->operands[0].kind == AS_OPERAND_LABEL_REF || in->operands[0].kind == AS_OPERAND_IMMEDIATE) &&
               !(in->operands[0].raw != NULL && in->operands[0].raw[0] == '*');
    }
    if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL) {
        static const char *const always[] = {".p2align", ".balign", ".align",  ".org",    ".uleb128",
                                             ".sleb128", ".incbin", ".code16", ".code32", ".code64"};
        static const char *const counted[] = {".skip", ".space", ".zero", ".fill"};
        const as_directive_t *d = &st->u.directive;
        size_t i;

        for (i = 0; i < sizeof(always) / sizeof(always[0]); ++i) {
            if (as_strcmp(d->name, always[i]) == 0) {
                return 1;
            }
        }
        for (i = 0; i < sizeof(counted) / sizeof(counted[0]); ++i) {
            if (as_strcmp(d->name, counted[i]) == 0) {
                long long n;

                return d->arg_count == 0 || d->args[0] == NULL || parse_int64(d->args[0], &n) != 0;
            }
        }
    }
    return 0;
}

/*
 * A label less a label, where both stand before the statement, in one
 * section, with nothing of variable size between: how far apart they
 * are is known where the statement is read, and it is a number like any
 * other.  0 and the number (with whatever else the expression adds), or
 * -1 if the expression is not such a one.
 */
static int fixed_label_diff(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits, const as_expr_t *e,
                            long long *out) {
    as_expr_linear_t lin;
    const char *sec;
    uint64_t size = 0;
    size_t is, ia, ib, lo, hi, k;

    if (e == NULL || e->kind == AS_EXPR_CONST || first_symbol_op_in_expr(e) != NULL ||
        as_expr_eval_linear(e, NULL, NULL, &lin) != AS_EXPR_EVAL_OK || lin.add_symbol == NULL ||
        lin.sub_symbol == NULL || lin.add_local != NULL || lin.sub_local != NULL ||
        parsed_stmt_index(ctx, st, &is) != 0 || label_stmt_index(ctx, lin.add_symbol, &ib) != 0 ||
        label_stmt_index(ctx, lin.sub_symbol, &ia) != 0 || ia > is || ib > is || build_stmt_section_at(ctx) != 0 ||
        ia >= ctx->stmt_section_at_count || ib >= ctx->stmt_section_at_count) {
        return -1;
    }
    sec = ctx->stmt_section_at[ia];
    if (sec == NULL || ctx->stmt_section_at[ib] == NULL || as_strcmp(sec, ctx->stmt_section_at[ib]) != 0) {
        return -1;
    }
    lo = ia < ib ? ia : ib;
    hi = ia < ib ? ib : ia;
    for (k = lo; k < hi; ++k) {
        const as_stmt_t *between = &ctx->parsed->items[k];

        if (stmt_is_of_variable_size(between) &&
            (between->kind == AS_STMT_DIRECTIVE ||
             (ctx->stmt_section_at[k] != NULL && as_strcmp(ctx->stmt_section_at[k], sec) == 0))) {
            return -1;
        }
    }
    if (stmt_range_size_in_section(ctx, sec, lo, hi, 0, x86_code_bits, &size) != 0) {
        return -1;
    }
    *out = lin.value + (ib >= ia ? (long long)size : -(long long)size);
    return 0;
}

/*
 * The statement with each such difference in its operands made the
 * number it is: `st` itself if it has none, else `tmp`, which has `ops`
 * for its operands and `consts` for the numbers.
 *
 * `addl $b-a, %eax` was encoded as an instruction with a symbol in it,
 * in the long form whatever the distance -- 81 and four bytes where GNU
 * as has 83 and one -- and `ret $b-a`, `shl $b-a, %eax` and `bt $b-a,
 * %eax`, which have no field a symbol could go in, were refused.  A
 * label ahead of the statement, or a jump or an alignment between the
 * two, leaves it the long form, as it does GNU as's.
 */
static const as_stmt_t *stmt_fold_label_diffs(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits,
                                              as_stmt_t *tmp, as_operand_t ops[4], as_expr_t consts[4]) {
    const as_instruction_t *in;
    int any = 0;
    size_t i;

    if (ctx == NULL || ctx->cfg == NULL || ctx->cfg->target != 0 || st == NULL || st->kind != AS_STMT_INSTRUCTION) {
        return st;
    }
    in = &st->u.instr;
    if (in->operand_count == 0 || in->operand_count > 4 || in->mnemonic == NULL || is_rel_mnemonic(in->mnemonic)) {
        return st;
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        const as_expr_t *e = op->kind == AS_OPERAND_MEMORY                                         ? op->u.mem.disp
                             : (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) ? op->u.expr
                                                                                                    : NULL;
        long long v;

        ops[i] = *op;
        if (e == NULL || fixed_label_diff(ctx, st, x86_code_bits, e, &v) != 0) {
            continue;
        }
        memset(&consts[i], 0, sizeof(consts[i]));
        consts[i].kind = AS_EXPR_CONST;
        consts[i].value = v;
        if (op->kind == AS_OPERAND_MEMORY) {
            ops[i].u.mem.disp = &consts[i];
        } else {
            ops[i].u.expr = &consts[i];
        }
        any = 1;
    }
    if (!any) {
        return st;
    }
    *tmp = *st;
    tmp->u.instr.operands = ops;
    return tmp;
}

static int append_virtual_instruction_bytes(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name,
                                            const as_stmt_t *st, unsigned x86_code_bits) {
    unsigned char code[32];
    size_t code_len = 0;
    char encerr[256];
    as_elf_cfg_t stmt_cfg;

    if (ctx == NULL || ctx->cfg == NULL || buf == NULL || st == NULL) {
        return -1;
    }
    if (ctx->cfg->target != 0 && st->kind == AS_STMT_INSTRUCTION) {
        if (arm_encode_stmt(ctx, section_name, (uint64_t)buf->len, 0, x86_code_bits, st, code, sizeof(code), &code_len,
                            encerr, sizeof(encerr)) != 0) {
            return -1;
        }
        return bytebuf_append(buf, code, code_len);
    }
    /* See note in stmt_virtual_size_in_section: precise branch sizing recurses
     * through this function, so cap recursion at the top level. */
    if (st->kind == AS_STMT_INSTRUCTION &&
        st->u.instr.operand_count == 1 &&
        is_rel_mnemonic(st->u.instr.mnemonic) &&
        ctx->virtual_scanning == 0) {
        const as_operand_t *op = &st->u.instr.operands[0];
        const as_expr_t *e = op->u.expr;
        if ((ctx->virtual_scanning == 0 ||
             (ctx->virtual_scanning == 1 && local_temp_branch_target_within(ctx, st, e, 64))) &&
            (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            e != NULL && e->kind == AS_EXPR_SYMBOL && expr_is_local_temp_symbol(e)) {
            as_elf_cfg_t local_cfg = *ctx->cfg;
            long long abs_target;
            int may_short = x86_branch_may_be_short(ctx, section_name, st, e, x86_code_bits);

            local_cfg.x86_code_bits = x86_code_bits;
            local_cfg.is_64 = x86_code_bits == 64u;
            if (eval_direct_local_branch_target(ctx, section_name, st, (uint64_t)buf->len, x86_code_bits, e,
                                                2, &abs_target) == 0 &&
                emit_resolved_x86_branch_ex(&local_cfg, st, (uint64_t)buf->len, abs_target, may_short, code, sizeof(code),
                                         &code_len, encerr, sizeof(encerr)) == 0) {
                if (code_len != 2 &&
                    eval_direct_local_branch_target(ctx, section_name, st, (uint64_t)buf->len, x86_code_bits, e,
                                                    code_len, &abs_target) == 0 &&
                    emit_resolved_x86_branch_ex(&local_cfg, st, (uint64_t)buf->len, abs_target, may_short, code, sizeof(code),
                                             &code_len, encerr, sizeof(encerr)) != 0) {
                    return -1;
                }
                return bytebuf_append(buf, code, code_len);
            }
        }
    }
    {
        unsigned local_rel_len = stmt_local_rel_virtual_len(ctx, section_name, st, x86_code_bits);
        if (local_rel_len != 0) {
            return bytebuf_append_zeros(buf, local_rel_len + x86_branch_hint(st));
        }
    }
    /* The mode is what .code16, .code32 or .code64 last said, whatever
     * the object is: is_64 here is the code's, and ctx->cfg's the
     * object's. */
    stmt_cfg = *ctx->cfg;
    stmt_cfg.x86_code_bits = x86_code_bits;
    stmt_cfg.is_64 = x86_code_bits == 64u;
    stmt_cfg.have_current_text_offset = 1u;
    stmt_cfg.current_text_offset = (uint64_t)buf->len;
    if (stmt_code_find(ctx, st, 0, x86_code_bits, 1, code, sizeof(code), &code_len)) {
        return bytebuf_append(buf, code, code_len);
    }
    {
        as_stmt_t folded;
        as_operand_t fold_ops[4];
        as_expr_t fold_consts[4];
        const as_stmt_t *as = stmt_fold_label_diffs(ctx, st, x86_code_bits, &folded, fold_ops, fold_consts);
        int rc = as == st ? encode_x86_stmt_by_text(ctx, &stmt_cfg, x86_code_bits, st, code, sizeof(code), &code_len,
                                                    encerr, sizeof(encerr))
                          : 1;

        if (rc < 0 ||
            (rc == 1 && encode_x86_stmt(ctx, &stmt_cfg, as, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0)) {
            return -1;
        }
    }
    if (stmt_is_place_free(st)) {
        stmt_code_keep(ctx, st, (uint64_t)buf->len, x86_code_bits, code, code_len);
    }
    return bytebuf_append(buf, code, code_len);
}

/*
 * .uleb128 and .sleb128 of the distance between two labels.
 *
 * (Of numbers they are .byte by now: as_directives_lower.)  How many
 * bytes one takes depends on a distance, and the distance on how many
 * bytes this and its like take, if any lies between the labels -- as in
 * a compiler's exception table, whose length is such a number and whose
 * entries are too.  So the sizes are settled before anything is laid
 * out for good: settle_leb128_sizes() starts each at one byte and grows
 * it until none wants more.  Wherever the layout is being worked out the
 * directive is that many bytes and nothing is evaluated; where the bytes
 * are written for the object, the value is, and is padded to the size.
 */
static struct leb_stmt *find_leb_stmt(emit_ctx_t *ctx, const as_stmt_t *st) {
    size_t lo = 0;
    size_t hi = ctx != NULL ? ctx->leb_count : 0;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (ctx->leb[mid].st == st) {
            return &ctx->leb[mid];
        }
        if (ctx->leb[mid].st < st) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

/*
 * Where every statement stands in its section, as the layout now is:
 * one walk of the source for all of them.  (To ask it of each statement
 * is a walk of its section for each, and the rows of a line table and
 * the directives of call frame information are many: a compiler's
 * 2,400 lines of C++ with debugging information took 81 seconds.)
 * The offsets are the caller's to free; a statement's section is
 * ctx->stmt_section_at[] of it.  0, or -1.
 */
static int layout_offsets(emit_ctx_t *ctx, uint64_t **off_out) {
    size_t count = ctx->parsed->count;
    uint64_t *off = (uint64_t *)malloc((count != 0 ? count : 1) * sizeof(*off));
    uint64_t *sizes = NULL;
    size_t size_count = 0;
    size_t size_cap = 0;
    as_strmap_t by_name;
    section_track_t track;
    size_t i;
    int rc = -1;

    memset(&by_name, 0, sizeof(by_name));
    if (off == NULL || build_stmt_section_at(ctx) != 0 ||
        section_track_init(&track, ctx->cfg->is_64 ? 64u : (ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        free(off);
        return -1;
    }
    ctx->virtual_scanning++;
    for (i = 0; i < count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        const char *section = track.current != NULL ? track.current : "";
        const char *in = i < ctx->stmt_section_at_count && ctx->stmt_section_at[i] != NULL ? ctx->stmt_section_at[i]
                                                                                           : "";
        size_t k;
        size_t n = 0;

        if (!as_strmap_get(&by_name, in, &k)) {
            if (size_count == size_cap) {
                size_t cap = size_cap != 0 ? size_cap * 2 : 16;
                uint64_t *grown = (uint64_t *)realloc(sizes, cap * sizeof(*grown));

                if (grown == NULL) {
                    goto out;
                }
                sizes = grown;
                size_cap = cap;
            }
            k = size_count++;
            sizes[k] = 0;
            if (as_strmap_put(&by_name, in, k) != 0) {
                goto out;
            }
        }
        off[i] = sizes[k];
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);

            if (trc < 0) {
                goto out;
            }
            if (trc > 0) {
                continue;
            }
        } else if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }
        (void)stmt_virtual_size_in_section(ctx, section, st, off[i], track.x86_code_bits, &n);
        sizes[k] += n;
    }
    *off_out = off;
    off = NULL;
    rc = 0;
out:
    ctx->virtual_scanning--;
    section_track_free(&track);
    as_strmap_free(&by_name);
    free(sizes);
    free(off);
    return rc;
}

/* The words of a directive, which may have come as one argument or as
 * several: the next one from *at, in word[0 .. word_sz).  0 at the end. */
static int next_word(const as_directive_t *d, size_t *arg, const char **at, char *word, size_t word_sz) {
    for (;;) {
        const char *p = *at;
        size_t n = 0;

        if (p == NULL) {
            if (*arg >= d->arg_count) {
                return 0;
            }
            p = d->args[*arg] != NULL ? d->args[*arg] : "";
        }
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            (*arg)++;
            *at = NULL;
            continue;
        }
        if (*p == '"') {
            /* A string is one word, quotes and all. */
            word[n++] = *p++;
            while (*p != '\0' && *p != '"' && n + 2 < word_sz) {
                word[n++] = *p++;
            }
            if (*p == '"') {
                word[n++] = *p++;
            }
        } else {
            while (*p != '\0' && !isspace((unsigned char)*p) && n + 1 < word_sz) {
                word[n++] = *p++;
            }
        }
        word[n] = '\0';
        *at = p;
        return 1;
    }
}

/*
 * What a `.loc` says, into *r: the file and the line, a column, and
 * then by name what is so of the place.  `is_stmt` and `isa` stay as
 * they are set until set again -- *sticky_stmt, *sticky_isa -- and the
 * rest are of this row alone.  0, or -1 with *why.
 */
static int parse_loc(const as_directive_t *d, struct line_row *r, unsigned *sticky_stmt, unsigned *sticky_isa,
                     const char **why) {
    char word[512];
    size_t arg = 0;
    const char *at = NULL;
    unsigned long numbers[3] = {0, 0, 0};
    size_t have = 0;
    char *end;

    memset(r, 0, sizeof(*r));
    *why = "a file's number and a line";
    while (next_word(d, &arg, &at, word, sizeof(word))) {
        if (isdigit((unsigned char)word[0]) && have < 3) {
            numbers[have++] = strtoul(word, &end, 0);
            if (*end != '\0') {
                return -1;
            }
            continue;
        }
        if (have < 2) {
            return -1;
        }
        if (as_strcmp(word, "basic_block") == 0) {
            r->row.flags |= AS_DWLINE_BASIC_BLOCK;
        } else if (as_strcmp(word, "prologue_end") == 0) {
            r->row.flags |= AS_DWLINE_PROLOGUE_END;
        } else if (as_strcmp(word, "epilogue_begin") == 0) {
            r->row.flags |= AS_DWLINE_EPILOGUE_BEGIN;
        } else if (as_strcmp(word, "is_stmt") == 0 || as_strcmp(word, "isa") == 0 || as_strcmp(word, "discriminator") == 0) {
            char value[64];
            unsigned long n;

            *why = "a number after is_stmt, isa or discriminator";
            if (!next_word(d, &arg, &at, value, sizeof(value))) {
                return -1;
            }
            n = strtoul(value, &end, 0);
            if (*end != '\0' || (word[1] == 's' && word[2] == '_' && n > 1)) {
                return -1;
            }
            if (word[0] == 'd') {
                r->row.discriminator = (unsigned)n;
            } else if (word[2] == 'a') {
                *sticky_isa = (unsigned)n;
            } else {
                *sticky_stmt = (unsigned)n;
            }
        } else if (as_strcmp(word, "view") == 0) {
            *why = "a name after view";
            if (!next_word(d, &arg, &at, word, sizeof(word)) || strlen(word) >= sizeof(r->view)) {
                return -1;
            }
            memcpy(r->view, word, strlen(word) + 1);
            if (as_strcmp(word, "-0") == 0) {
                r->row.flags |= AS_DWLINE_VIEW_RESET;
            }
        } else {
            *why = "what is not a thing .loc says of a place";
            return -1;
        }
    }
    if (have < 2) {
        return -1;
    }
    r->row.file = (unsigned)numbers[0];
    r->row.line = (unsigned)numbers[1];
    r->row.column = (unsigned)numbers[2];
    r->row.isa = *sticky_isa;
    if (*sticky_stmt) {
        r->row.flags |= AS_DWLINE_IS_STMT;
    }
    return 0;
}

/*
 * The rows of the line table, as the layout now is.
 *
 * A `.loc` is a row where it takes effect, and that is where GNU as
 * has it: at the instruction after it, wherever padding or data has
 * put that, or -- two with nothing between -- at the `.loc` after it;
 * one with a `view` at once, where it stands; and one that nothing
 * follows is no row.  With `own_source` there are no `.loc` and each
 * instruction is a row, of its own line in the source (-g).
 */
static int line_rows_from(emit_ctx_t *ctx, const uint64_t *offsets);

static int line_rows(emit_ctx_t *ctx) {
    uint64_t *offsets = NULL;
    int rc;

    if (ctx->line_rows_valid) {
        return 0;
    }
    /* (Laying the source out may ask after a view, which asks for
     * this: it is told there is none yet.) */
    if (ctx->line_rows_busy) {
        return -1;
    }
    ctx->line_rows_busy = 1;
    rc = layout_offsets(ctx, &offsets);
    ctx->line_rows_busy = 0;
    if (rc != 0) {
        set_err(ctx, "out of memory");
        return -1;
    }
    rc = line_rows_from(ctx, offsets);
    free(offsets);
    return rc;
}

static int line_rows_from(emit_ctx_t *ctx, const uint64_t *offsets) {
    struct line_row pending;
    int have_pending = 0;
    unsigned stmt = 1;
    unsigned isa = 0;
    size_t cap = 0;
    size_t i;

    ctx->line_row_count = 0;
    as_strmap_free(&ctx->line_views);
    memset(&ctx->line_views, 0, sizeof(ctx->line_views));
    cap = 0;
    free(ctx->line_rows);
    ctx->line_rows = NULL;
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        int is_loc = st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL &&
                     as_strcmp(st->u.directive.name, ".loc") == 0;
        struct line_row now;
        const char *why = "";
        const char *section = NULL;
        uint64_t off = 0;
        int emit = 0;

        if (!is_loc && !(st->kind == AS_STMT_INSTRUCTION && (have_pending || ctx->line_own))) {
            continue;
        }
        if (is_loc && parse_loc(&st->u.directive, &now, &stmt, &isa, &why) != 0) {
            set_err(ctx, "%s:%u: .loc: %s", st->file != NULL ? st->file : "<input>", st->line, why);
            return -1;
        }
        if (i >= ctx->stmt_section_at_count || ctx->stmt_section_at[i] == NULL) {
            continue;
        }
        section = ctx->stmt_section_at[i];
        off = offsets[i];
        if (!is_loc && ctx->line_own) {
            memset(&pending, 0, sizeof(pending));
            pending.row.file = 1;
            pending.row.line = st->line;
            pending.row.flags = AS_DWLINE_IS_STMT;
            have_pending = 1;
        }
        /* The row before takes effect here, if it has not yet. */
        if (have_pending) {
            pending.row.section = section;
            pending.row.offset = off;
            emit = 1;
        }
        for (;;) {
            if (emit) {
                if (ctx->line_row_count == cap) {
                    size_t ncap = cap != 0 ? cap * 2 : 256;
                    struct line_row *grown = (struct line_row *)realloc(ctx->line_rows, ncap * sizeof(*grown));

                    if (grown == NULL) {
                        set_err(ctx, "out of memory");
                        return -1;
                    }
                    ctx->line_rows = grown;
                    cap = ncap;
                }
                ctx->line_rows[ctx->line_row_count] = pending;
                ctx->line_row_count++;
                have_pending = 0;
                emit = 0;
            }
            if (!is_loc) {
                break;
            }
            /* And this one waits for what follows it, unless it has a
             * view, which is of where it stands. */
            pending = now;
            have_pending = 1;
            is_loc = 0;
            if (now.view[0] != '\0') {
                pending.row.section = section;
                pending.row.offset = off;
                emit = 1;
                continue;
            }
            break;
        }
    }
    /* The rows that have names, by them. */
    for (i = 0; i < ctx->line_row_count; ++i) {
        if (ctx->line_rows[i].view[0] != '\0' && as_strcmp(ctx->line_rows[i].view, "-0") != 0 &&
            as_strmap_put(&ctx->line_views, ctx->line_rows[i].view, i) != 0) {
            set_err(ctx, "out of memory");
            return -1;
        }
    }
    ctx->line_rows_valid = 1;
    return 0;
}

/* The names `.loc ... view NAME` gives, each with the statement that
 * gives it: found once. */
static int line_view_names(emit_ctx_t *ctx) {
    size_t i;

    if (ctx->line_view_names_made) {
        return 0;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        const as_directive_t *d = &st->u.directive;
        char word[512];
        size_t arg = 0;
        const char *at = NULL;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL || as_strcmp(d->name, ".loc") != 0) {
            continue;
        }
        while (next_word(d, &arg, &at, word, sizeof(word))) {
            if (as_strcmp(word, "view") == 0 && next_word(d, &arg, &at, word, sizeof(word)) &&
                as_strcmp(word, "-0") != 0) {
                char *kept = xstrdup(word);
                void *grown = kept != NULL
                                  ? realloc(ctx->line_view_name_list,
                                            (ctx->line_view_name_count + 1) * sizeof(ctx->line_view_name_list[0]))
                                  : NULL;

                if (grown == NULL) {
                    free(kept);
                    return -1;
                }
                ctx->line_view_name_list = (char **)grown;
                ctx->line_view_name_list[ctx->line_view_name_count++] = kept;
                if (as_strmap_put(&ctx->line_view_names, kept, i) != 0) {
                    return -1;
                }
            }
        }
    }
    ctx->line_view_names_made = 1;
    return 0;
}

/*
 * The number of a view that `.loc ... view NAME` named: how many rows
 * stand before it at the same address of the same section, with none
 * between at another.  0 and the number, or -1 if NAME is no view's.
 */
static int line_view_value(emit_ctx_t *ctx, const char *name, long long *out) {
    size_t at = 0;
    size_t i;
    long long n = 0;

    while (isspace((unsigned char)*name)) {
        name++;
    }
    if (name[0] != '.' && !isalpha((unsigned char)name[0]) && name[0] != '_') {
        return -1;
    }
    /* (The names there are do not change, and most that are asked of
     * are none of them: the rows are worked out only for one that is.) */
    if (line_view_names(ctx) != 0 || !as_strmap_get(&ctx->line_view_names, name, &at)) {
        return -1;
    }
    if (line_rows(ctx) != 0 || !as_strmap_get(&ctx->line_views, name, &at)) {
        return -1;
    }
    for (i = at; i > 0; --i) {
        const struct line_row *before = &ctx->line_rows[i - 1];

        if (as_strcmp(before->row.section, ctx->line_rows[at].row.section) != 0) {
            continue;
        }
        if (before->row.offset != ctx->line_rows[at].row.offset) {
            break;
        }
        n++;
        /* (`view -0` is where the count begins again.) */
        if ((before->row.flags & AS_DWLINE_VIEW_RESET) != 0) {
            break;
        }
    }
    *out = n;
    return 0;
}

static int eval_leb128_arg(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st, uint64_t sec_off,
                           unsigned x86_code_bits, const char *arg, long long *out) {
    as_expr_t *expr;
    int rc;

    if (parse_int64(arg, out) == 0 || parse_const_expr_string(arg, out) == 0) {
        return 0;
    }
    /* A view of the line table, by its name. */
    if (ctx != NULL && line_view_value(ctx, arg, out) == 0) {
        return 0;
    }
    expr = as_parse_expr_string(arg, st != NULL ? st->file : NULL, st != NULL ? st->line : 0);
    if (expr == NULL) {
        return -1;
    }
    /* Two labels of another section, or of this one. */
    rc = eval_abs_local_diff_expr_virtual(ctx, st, expr, out);
    if (rc != 0) {
        rc = eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, out);
    }
    as_expr_free(expr);
    return rc;
}

static int append_leb128_directive(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name, const as_stmt_t *st,
                                   uint64_t sec_off, unsigned x86_code_bits, const as_directive_t *d) {
    struct leb_stmt *leb = find_leb_stmt(ctx, st);
    int is_signed = d->name[1] == 's';
    uint64_t done = 0;
    size_t i;

    if (ctx == NULL || d->arg_count == 0) {
        return -1;
    }
    for (i = 0; i < d->arg_count; ++i) {
        unsigned char bytes[LEB128_MAX_BYTES];
        size_t want = leb != NULL ? leb->arg_bytes[i] : 1;
        size_t n;
        long long v = 0;

        if (ctx->virtual_scanning) {
            if (bytebuf_append_zeros(buf, want) != 0) {
                return -1;
            }
            continue;
        }
        /* `.` is where this argument's bytes begin. */
        if (d->args[i] == NULL ||
            eval_leb128_arg(ctx, section_name, st, sec_off + done, x86_code_bits, d->args[i], &v) != 0) {
            set_err(ctx, "%s:%u: %s: '%s' is neither a number nor the distance between two labels",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0, d->name,
                    d->args[i] != NULL ? d->args[i] : "");
            return -1;
        }
        n = as_leb128_encode(v, is_signed, bytes);
        if (n > want) {
            set_err(ctx, "%s:%u: %s: the size of '%s' did not settle",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0, d->name,
                    d->args[i]);
            return -1;
        }
        /* Padded: every byte but the last says that more follow, and
         * those added carry the sign and nothing else. */
        for (; n < want; ++n) {
            bytes[n - 1] |= 0x80;
            bytes[n] = is_signed && v < 0 ? 0x7f : 0x00;
        }
        if (bytebuf_append(buf, bytes, n) != 0) {
            return -1;
        }
        done += n;
    }
    return 1;
}

static int append_directive_data_ctx(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name,
                                     const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                                     const as_directive_t *d) {
    size_t i;
    unsigned width = 0;

    (void)section_name;
    (void)st;
    (void)sec_off;
    (void)x86_code_bits;
    if (d == NULL || d->name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".uleb128") == 0 || as_strcmp(d->name, ".sleb128") == 0) {
        return append_leb128_directive(ctx, buf, section_name, st, sec_off, x86_code_bits, d);
    }
    if (ctx != NULL && ctx->virtual_scanning &&
        (as_strcmp(d->name, ".skip") == 0 || as_strcmp(d->name, ".space") == 0)) {
        long long tmp = 0;
        if (ctx->virtual_scanning > 1) {
            return 1;
        }
        if (d->arg_count >= 1 && d->args[0] != NULL &&
            parse_int64(d->args[0], &tmp) != 0 &&
            parse_const_expr_string(d->args[0], &tmp) != 0 &&
            eval_linux_alt_pad_expr(ctx, section_name, st, sec_off, x86_code_bits, d->args[0], &tmp) != 0) {
            return 1;
        }
    }
    if (as_strcmp(d->name, ".incbin") == 0) {
        return append_incbin_to_bytebuf(ctx, st, buf, d);
    }
    if (as_strcmp(d->name, ".org") == 0) {
        long long target = 0;
        as_expr_t *expr;

        if (d->arg_count < 1 || d->args[0] == NULL) {
            return -1;
        }
        if (parse_int64(d->args[0], &target) != 0) {
            expr = as_parse_expr_string(d->args[0], st != NULL ? st->file : NULL,
                                        st != NULL ? st->line : 0);
            if (expr == NULL) {
                return -1;
            }
            /* The distance from here, where `.` is here; it was measured
             * from the section's start, and `.org .+4` went to 4. */
            if (eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, &target) != 0) {
                as_expr_free(expr);
                return -1;
            }
            target += (long long)sec_off;
            as_expr_free(expr);
        }
        if (target < 0 || (uint64_t)target < sec_off) {
            return -1;
        }
        if (d->arg_count >= 2 && d->args[1] != NULL && d->args[1][0] != '\0') {
            long long fill = 0;
            uint64_t n;

            /* `.org place, fill`: the byte the gap is filled with. */
            if (parse_int64(d->args[1], &fill) != 0 && parse_const_expr_string(d->args[1], &fill) != 0) {
                return -1;
            }
            for (n = sec_off; n < (uint64_t)target; ++n) {
                if (bytebuf_append_u64_le(buf, (uint64_t)fill, 1) != 0) {
                    return -1;
                }
            }
            return 1;
        }
        if ((uint64_t)target > sec_off &&
            bytebuf_append_zeros(buf, (size_t)((uint64_t)target - sec_off)) != 0) {
            return -1;
        }
        return 1;
    }
    if (as_strcmp(d->name, ".align") == 0 || as_strcmp(d->name, ".balign") == 0 || as_strcmp(d->name, ".p2align") == 0) {
        long long raw = 0;
        long long fill = section_name_has_exec_flag(ctx, section_name) ? 0x90 : 0;
        size_t align;
        size_t need;

        if (d->arg_count < 1 || d->args[0] == NULL ||
            (parse_int64(d->args[0], &raw) != 0 && parse_const_expr_string(d->args[0], &raw) != 0) ||
            raw < 0) {
            return -1;
        }
        if (d->arg_count >= 2 && d->args[1] != NULL && d->args[1][0] != '\0' &&
            (parse_int64(d->args[1], &fill) != 0 && parse_const_expr_string(d->args[1], &fill) != 0)) {
            return -1;
        }
        if (as_strcmp(d->name, ".p2align") == 0) {
            if (raw > 31) {
                return -1;
            }
            align = (size_t)1u << (unsigned)raw;
        } else {
            align = (size_t)raw;
        }
        if (align == 0 || (align & (align - 1)) != 0) {
            return -1;
        }
        /*
         * From where the directive is in its section, which is sec_off,
         * and not from the length of the buffer written to: when a
         * statement is being measured that buffer is a scratch one and
         * empty, so every alignment measured 0 bytes, and a branch across
         * one was aimed short by its padding -- into the padding.
         */
        need = (align - ((size_t)sec_off & (align - 1))) & (align - 1);
        switch (align_exceeds_max_skip(d, need)) {
        case 1: need = 0; break;
        case 0: break;
        default: return -1;
        }
        /* Where instructions go, padding that is NOPs -- none named, or
         * 0x90 named -- is the long NOPs GNU as pads with. */
        if (ctx != NULL && ctx->cfg != NULL && ctx->cfg->target != 0 && !directive_has_arg(d, 1) &&
            section_name_has_exec_flag(ctx, section_name)) {
            return append_arm_nops(buf, need, ctx->cfg->target, x86_code_bits) == 0 ? 1 : -1;
        }
        if (fill == 0x90 && !(d->fill_width > 1 && directive_has_arg(d, 1)) && ctx != NULL && ctx->cfg != NULL &&
            ctx->cfg->target == 0 && (ctx->cfg->machine == EM_386 || ctx->cfg->machine == EM_X86_64) &&
            section_name_has_exec_flag(ctx, section_name)) {
            /* Behind data GNU as writes one NOP of a byte, and pads
             * what is left. */
            if (ctx->pad_after_data && need != 0) {
                if (bytebuf_append_u64_le(buf, 0x90, 1) != 0) {
                    return -1;
                }
                need--;
            }
            return append_x86_nops(buf, need, 0, x86_code_bits, ctx->cfg->machine == EM_X86_64) == 0 ? 1 : -1;
        }
        if (append_align_fill(buf, d, need, (uint64_t)fill) != 0) {
            set_err(ctx, "%s:%u: alignment padding (%lu bytes) is not a multiple of %u",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0,
                    (unsigned long)need, d->fill_width);
            return -1;
        }
        return 1;
    }
    if (as_strcmp(d->name, ".fill") == 0) {
        long long repeat = 0;
        long long size = 1;
        long long value = 0;

        if (d->arg_count < 1 || d->args[0] == NULL) {
            return -1;
        }
        if (parse_int64(d->args[0], &repeat) != 0 && parse_const_expr_string(d->args[0], &repeat) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->file : NULL,
                                                  st != NULL ? st->line : 0);
            if (expr == NULL ||
                (eval_abs_local_diff_expr_virtual(ctx, st, expr, &repeat) != 0 &&
                 eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, &repeat) != 0 &&
                 eval_local_align_fill_expr(ctx, section_name, st, sec_off, x86_code_bits, d->args[0], &repeat) != 0)) {
                as_expr_free(expr);
                return -1;
            }
            as_expr_free(expr);
        }
        if (d->arg_count >= 2 &&
            ((parse_int64(d->args[1], &size) != 0 && parse_const_expr_string(d->args[1], &size) != 0) ||
             size <= 0 || size > 8)) {
            return -1;
        }
        if (d->arg_count >= 3 &&
            (parse_int64(d->args[2], &value) != 0 && parse_const_expr_string(d->args[2], &value) != 0)) {
            return -1;
        }
        if (repeat < 0 || repeat > AS_ELF_FILL_MAX / size) {
            return -1;
        }
        /* The value is of 32 bits, as GNU as has it, whatever the size
         * of what it fills. */
        if (size > 4) {
            value &= 0xffffffffll;
        }
        for (i = 0; i < (size_t)repeat; ++i) {
            if (bytebuf_append_u64_le(buf, (uint64_t)value, (unsigned)size) != 0) {
                return -1;
            }
        }
        return 1;
    }
    if (as_strcmp(d->name, ".byte") == 0) {
        width = 1;
    } else if (as_strcmp(d->name, ".word") == 0 || as_strcmp(d->name, ".short") == 0 ||
               as_strcmp(d->name, ".hword") == 0 || as_strcmp(d->name, ".2byte") == 0) {
        width = 2;
    } else if (as_strcmp(d->name, ".long") == 0 || as_strcmp(d->name, ".4byte") == 0) {
        width = 4;
    } else if (as_strcmp(d->name, ".quad") == 0 || as_strcmp(d->name, ".8byte") == 0) {
        width = 8;
    }
    if (width != 0) {
        for (i = 0; i < d->arg_count; ++i) {
            long long v;
            char *sym = NULL;
            int64_t add = 0;
            as_expr_t *expr;

            if (eval_linux_alt_len_expr(ctx, st, d->args[i], &v) == 0 ||
                parse_int64(d->args[i], &v) == 0 || parse_const_expr_string(d->args[i], &v) == 0 ||
                eval_arg_asm_vars(ctx, st, d->args[i], &v) == 0) {
                if (bytebuf_append_u64_le(buf, (uint64_t)v, width) != 0) {
                    return -1;
                }
                continue;
            }
            /* For x86 what is not a number as it stands is worked out
             * where the relocations are made (emit_data_field), which
             * writes the field: here it is its width and no more. */
            if (ctx != NULL && ctx->cfg != NULL &&
                (ctx->cfg->machine == EM_386 || ctx->cfg->machine == EM_X86_64)) {
                if (bytebuf_append_zeros(buf, width) != 0) {
                    return -1;
                }
                continue;
            }
            expr = as_parse_expr_string(d->args[i], st != NULL ? st->file : NULL,
                                        st != NULL ? st->line : 0);
            if (expr != NULL) {
                long long ev = 0;
                if (eval_abs_local_diff_expr_virtual(ctx, st, expr, &ev) == 0 ||
                    eval_local_rel_expr_virtual(ctx, section_name, st, sec_off + (uint64_t)(i * width),
                                                x86_code_bits, expr, &ev) == 0) {
                    as_expr_free(expr);
                    if (bytebuf_append_u64_le(buf, (uint64_t)ev, width) != 0) {
                        return -1;
                    }
                    continue;
                }
                as_expr_free(expr);
                expr = NULL;
            }
            if (parse_symbol_addend_arg(d->args[i], &sym, &add) == 0 && sym != NULL) {
                free(sym);
                if (bytebuf_append_zeros(buf, width) != 0) {
                    return -1;
                }
                continue;
            }
            free(sym);
            expr = as_parse_expr_string(d->args[i], st != NULL ? st->file : NULL,
                                        st != NULL ? st->line : 0);
            if (expr != NULL && expr_has_symbol(expr)) {
                as_expr_free(expr);
                if (bytebuf_append_zeros(buf, width) != 0) {
                    return -1;
                }
                continue;
            }
            as_expr_free(expr);
            return -1;
        }
        return 1;
    }
    if (as_strcmp(d->name, ".zero") == 0 || as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0) {
        long long count = 0;
        long long fill = 0;
        size_t j;
        if (d->arg_count < 1 || d->args[0] == NULL) {
            return -1;
        }
        if (eval_linux_alt_pad_expr(ctx, section_name, st, sec_off, x86_code_bits, d->args[0], &count) == 0) {
            /* Linux alternative macros use label-difference expressions that must be
             * evaluated before generic constant parsing can treat comparisons as
             * ordinary arithmetic. */
        } else if (parse_int64(d->args[0], &count) != 0 && parse_const_expr_string(d->args[0], &count) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->file : NULL,
                                                  st != NULL ? st->line : 0);
            if (expr == NULL ||
                (eval_abs_local_diff_expr_virtual(ctx, st, expr, &count) != 0 &&
                 eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, &count) != 0)) {
                /* While places are still being found a count may not be
                 * known yet; once they are, one that is no number was
                 * taken for none, and is refused as that of .zero is. */
                if ((as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0) && ctx != NULL &&
                    ctx->virtual_scanning) {
                    as_expr_free(expr);
                    expr = NULL;
                    count = 0;
                } else {
                    set_err(ctx, "%s:%u: cannot evaluate %s count expression '%s'",
                            st != NULL && st->file != NULL ? st->file : "<input>",
                            st != NULL ? st->line : 0,
                            d->name,
                            d->args[0]);
                    as_expr_free(expr);
                    return -1;
                }
            }
            as_expr_free(expr);
        }
        if (count < 0 && (as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0)) {
            count = 0;
        }
        if (count < 0) {
            return -1;
        }
        if (count > AS_ELF_FILL_MAX) {
            set_err(ctx, "%s:%u: %s count is unreasonably large: %lld",
                    st != NULL && st->file != NULL ? st->file : "<input>",
                    st != NULL ? st->line : 0,
                    d->name,
                    count);
            return -1;
        }
        if ((as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0) && d->arg_count >= 2) {
            if (parse_int64(d->args[1], &fill) != 0 && parse_const_expr_string(d->args[1], &fill) != 0) {
                return -1;
            }
            for (j = 0; j < (size_t)count; ++j) {
                if (bytebuf_append_u64_le(buf, (uint64_t)fill, 1) != 0) {
                    return -1;
                }
            }
            return 1;
        }
        if (bytebuf_append_zeros(buf, (size_t)count) != 0) {
            return -1;
        }
        return 1;
    }
    return append_string_directive(buf, d);
}

static int encode_x86_stmt_at(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                              const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                              char *encerr, size_t encerr_sz);

/* Is there no name in the expression: nothing but numbers? */
static int expr_is_numbers(const as_expr_t *e) {
    if (e == NULL) {
        return 1;
    }
    if (e->kind == AS_EXPR_CONST) {
        return 1;
    }
    if (e->kind == AS_EXPR_SYMBOL || e->kind == AS_EXPR_LOCAL_REF) {
        return 0;
    }
    return expr_is_numbers(e->lhs) && expr_is_numbers(e->rhs);
}

/*
 * An instruction of registers and numbers is the same bytes wherever
 * it stands and however the rest is laid out; one with a name in it, or
 * that goes somewhere, is not.
 */
static int stmt_is_place_free(const as_stmt_t *st) {
    size_t i;

    if (st->u.instr.mnemonic == NULL || is_rel_mnemonic(st->u.instr.mnemonic)) {
        return 0;
    }
    for (i = 0; i < st->u.instr.operand_count; ++i) {
        const as_operand_t *op = &st->u.instr.operands[i];

        if (op->kind == AS_OPERAND_REGISTER) {
            continue;
        }
        if (op->kind == AS_OPERAND_IMMEDIATE) {
            if (!expr_is_numbers(op->u.expr)) {
                return 0;
            }
        } else if (op->kind == AS_OPERAND_MEMORY) {
            if (!expr_is_numbers(op->u.mem.disp) || x86_base_is_ip(op->u.mem.base_reg)) {
                return 0;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

/*
 * Encode an instruction as it is read, if it is one of registers and
 * numbers, and let its operands go: the statement is its bytes from
 * here on (as_instruction_t.code).  Such an instruction needs nothing
 * the rest of the source could tell -- no label, no symbol, no layout --
 * and what was kept of it until the object was written was three times
 * the size of everything else kept for it.
 *
 * `code_bits` is the size of the code it is in.  1 if it was encoded, 0
 * if it is left as it was: it is not such an instruction, or it cannot
 * be encoded, which is said when its turn comes in the ordinary way.
 * AS_NO_STMT_CODE in the environment leaves every instruction as it was.
 */
int as_x86_precode(const as_elf_cfg_t *cfg, unsigned code_bits, as_stmt_t *st) {
    static int off = -1;
    as_elf_cfg_t stmt_cfg;
    as_elf_warnings_t probe;
    unsigned char code[32];
    char encerr[256];
    char key[192];
    size_t key_len;
    size_t code_len = 0;

    if (off < 0) {
        off = getenv("AS_NO_STMT_CODE") != NULL;
    }
    if (off || cfg == NULL || st == NULL || cfg->target != 0 || st->kind != AS_STMT_INSTRUCTION ||
        st->u.instr.code_len != 0 || (cfg->machine != EM_386 && cfg->machine != EM_X86_64) ||
        !stmt_is_place_free(st)) {
        return 0;
    }
    /* (A segment written before it may yet be taken away, by what goes
     * through the source for the jumps: as_elf_mark_far_branches().) */
    if ((st->u.instr.prefixes & AS_PREFIX_SEG_OVERRIDE) != 0) {
        return 0;
    }
    stmt_cfg = *cfg;
    stmt_cfg.x86_code_bits = code_bits;
    stmt_cfg.is_64 = code_bits == 64u;
    /* (One that has a warning to make is left to make it in its turn,
     * among the others'.) */
    memset(&probe, 0, sizeof(probe));
    probe.probe = 1;
    stmt_cfg.warnings = &probe;
    encerr[0] = '\0';
    /* By what is written, if one written the same has been encoded:
     * a compiler writes a few hundred instructions many thousands of
     * times, and each was encoded anew as it was read. */
    key_len = text_code_key(NULL, st, code_bits, key, sizeof(key));
    if (key_len == 0 || !text_code_find(key, key_len, code, sizeof(code), &code_len)) {
        if (encode_x86_stmt(NULL, &stmt_cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0 ||
            probe.asked != 0 || code_len == 0 || code_len > sizeof(st->u.instr.code)) {
            return 0;
        }
        if (key_len != 0) {
            text_code_keep(key, key_len, code, code_len);
        }
    }
    if (code_len == 0 || code_len > sizeof(st->u.instr.code)) {
        return 0;
    }
    as_instruction_drop_operands(&st->u.instr);
    memcpy(st->u.instr.code, code, code_len);
    st->u.instr.code_len = (unsigned char)code_len;
    st->u.instr.code_bits = (unsigned char)code_bits;
    return 1;
}

void as_x86_precode_free(void) {
    text_code_free();
    mn_facts_free();
}

static struct stmt_code *stmt_code_slot(emit_ctx_t *ctx, const as_stmt_t *st) {
    static int off = -1;
    size_t idx = 0;

    if (off < 0) {
        off = getenv("AS_NO_STMT_CODE") != NULL;
    }
    if (off || ctx == NULL || ctx->cfg == NULL || ctx->cfg->target != 0 || st == NULL ||
        st->kind != AS_STMT_INSTRUCTION || parsed_stmt_index(ctx, st, &idx) != 0) {
        return NULL;
    }
    if (ctx->stmt_code == NULL) {
        ctx->stmt_code = (struct stmt_code *)calloc(ctx->parsed->count, sizeof(*ctx->stmt_code));
        ctx->stmt_code_count = ctx->stmt_code != NULL ? ctx->parsed->count : 0;
    }
    return idx < ctx->stmt_code_count ? &ctx->stmt_code[idx] : NULL;
}

/* The statement's bytes, if they are kept for here (or, `anywhere`
 * asked, for anywhere): 1, and the bytes. */
static int stmt_code_find(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits, int anywhere,
                          unsigned char *code, size_t code_cap, size_t *code_len) {
    const struct stmt_code *kept = stmt_code_slot(ctx, st);

    if (kept == NULL || kept->len == 0 || kept->bits != (unsigned char)x86_code_bits || kept->len > code_cap) {
        return 0;
    }
    if (!kept->anywhere && (anywhere || kept->off != sec_off || kept->epoch != ctx->layout_epoch)) {
        return 0;
    }
    memcpy(code, kept->bytes, kept->len);
    *code_len = kept->len;
    return 1;
}

/*
 * The key of a statement in the table of texts: every field of it that
 * an encoder reads, with the code's size before them.  0 if it has no
 * key -- it is not of registers and numbers, an operand has no text, a
 * register has been given another name (which a later statement may
 * give again), or the key is too long to be worth keeping.
 */
/* What a key is put together of: a string, and a number.  Each is ended
 * by a byte no name has, so that no two keys of different things are
 * the same.  0, or -1 where the key has no room for it. */
static int key_str(char *key, size_t key_sz, size_t *n, const char *s) {
    size_t len = strlen(s);

    if (*n + len + 2 > key_sz) {
        return -1;
    }
    memcpy(key + *n, s, len);
    *n += len;
    key[(*n)++] = '\x1f';
    return 0;
}

static int key_num(char *key, size_t key_sz, size_t *n, unsigned long long v) {
    if (*n + 18 > key_sz) {
        return -1;
    }
    do {
        key[(*n)++] = "0123456789abcdef"[v & 15u];
        v >>= 4;
    } while (v != 0);
    key[(*n)++] = '\x1e';
    return 0;
}

static size_t text_code_key(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits, char *key, size_t key_sz) {
    const as_instruction_t *in = &st->u.instr;
    size_t n;
    size_t i;
    int ok;

    /* (One that is its bytes already has no operands to be the key.) */
    if ((ctx != NULL && ctx->asm_reg_alias_count != 0) || in->code_len != 0 || !stmt_is_place_free(st)) {
        return 0;
    }
    /* (Put together by hand: it is made for every instruction of
     * registers and numbers that is read, and snprintf() was a fifth of
     * the time such a source took.) */
    n = 0;
    ok = key_num(key, key_sz, &n, x86_code_bits) == 0 && key_num(key, key_sz, &n, in->prefixes) == 0 &&
         key_num(key, key_sz, &n, in->pseudo) == 0 && key_num(key, key_sz, &n, in->rex_bits) == 0 &&
         key_num(key, key_sz, &n, in->opmask) == 0 &&
         key_num(key, key_sz, &n,
                 (unsigned long long)in->have_opmask | ((unsigned long long)in->zeroing << 1) |
                     ((unsigned long long)in->sae << 2) | ((unsigned long long)in->syntax_intel << 3) |
                         ((unsigned long long)in->from_intel << 4) | ((unsigned long long)in->have_dfv << 5) |
                         ((unsigned long long)in->dfv << 6)) == 0 &&
         key_num(key, key_sz, &n, in->broadcast) == 0 &&
         key_num(key, key_sz, &n, (unsigned long long)(long long)in->rounding_mode) == 0 &&
         key_str(key, key_sz, &n, in->segment_override != NULL ? in->segment_override : "-") == 0 &&
         key_str(key, key_sz, &n, in->mnemonic) == 0;
    /* What each operand is, not how it was written: a name that is a
     * number is written the same when the number has been set again. */
    for (i = 0; ok && i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        char mark[3];
        long long v = 0;

        mark[0] = '\t';
        mark[1] = op->raw != NULL && (op->raw[0] == '$' || op->raw[0] == '*') ? op->raw[0] : '.';
        mark[2] = '\0';
        ok = key_str(key, key_sz, &n, mark) == 0;
        if (op->kind == AS_OPERAND_REGISTER) {
            ok = ok && key_str(key, key_sz, &n, "%") == 0 && key_str(key, key_sz, &n, op->u.reg) == 0;
        } else if (op->kind == AS_OPERAND_IMMEDIATE) {
            if (eval_expr_const(op->u.expr, &v) != 0) {
                return 0;
            }
            ok = ok && key_str(key, key_sz, &n, "$") == 0 && key_num(key, key_sz, &n, (unsigned long long)v) == 0;
        } else {
            const as_mem_operand_t *m = &op->u.mem;

            if (m->disp != NULL && eval_expr_const(m->disp, &v) != 0) {
                return 0;
            }
            ok = ok && key_str(key, key_sz, &n, m->segment_reg != NULL ? m->segment_reg : "") == 0 &&
                 key_num(key, key_sz, &n, m->disp != NULL) == 0 && key_num(key, key_sz, &n, (unsigned long long)v) == 0 &&
                 key_str(key, key_sz, &n, m->base_reg != NULL ? m->base_reg : "") == 0 &&
                 key_str(key, key_sz, &n, m->index_reg != NULL ? m->index_reg : "") == 0 &&
                 key_num(key, key_sz, &n, (unsigned long long)m->scale) == 0 &&
                 key_num(key, key_sz, &n, (unsigned long long)m->size_bits) == 0;
        }
    }
    if (!ok || n >= key_sz) {
        return 0;
    }
    key[n] = '\0';
    return n;
}

static uint32_t text_code_hash(const char *key, size_t n) {
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 0; i < n; ++i) {
        h = (h ^ (unsigned char)key[i]) * 16777619u;
    }
    return h;
}

static int text_code_find(const char *key, size_t n, unsigned char *code, size_t code_cap, size_t *code_len) {
    uint32_t h = text_code_hash(key, n);
    size_t at;

    if (text_codes.cap == 0) {
        return 0;
    }
    for (at = h & (text_codes.cap - 1); text_codes.slots[at].key != NULL; at = (at + 1) & (text_codes.cap - 1)) {
        const struct text_code *t = &text_codes.slots[at];

        if (t->hash == h && as_strcmp(t->key, key) == 0) {
            if (t->len > code_cap) {
                return 0;
            }
            memcpy(code, t->bytes, t->len);
            *code_len = t->len;
            return 1;
        }
    }
    return 0;
}

/* (For want of memory the statement is not kept, and is encoded again
 * when it is written again.) */
static void text_code_keep(const char *key, size_t n, const unsigned char *code, size_t code_len) {
    uint32_t h = text_code_hash(key, n);
    struct text_code *t;
    size_t at;

    if (code_len == 0 || code_len > sizeof(t->bytes)) {
        return;
    }
    if ((text_codes.count + 1) * 2 > text_codes.cap) {
        size_t ncap = text_codes.cap == 0 ? 1024 : text_codes.cap * 2;
        struct text_code *next = (struct text_code *)calloc(ncap, sizeof(*next));
        size_t i;

        if (next == NULL) {
            return;
        }
        for (i = 0; i < text_codes.cap; ++i) {
            if (text_codes.slots[i].key != NULL) {
                for (at = text_codes.slots[i].hash & (ncap - 1); next[at].key != NULL; at = (at + 1) & (ncap - 1)) {
                }
                next[at] = text_codes.slots[i];
            }
        }
        free(text_codes.slots);
        text_codes.slots = next;
        text_codes.cap = ncap;
    }
    for (at = h & (text_codes.cap - 1); text_codes.slots[at].key != NULL; at = (at + 1) & (text_codes.cap - 1)) {
    }
    t = &text_codes.slots[at];
    t->key = xstrdup(key);
    if (t->key == NULL) {
        return;
    }
    t->hash = h;
    t->len = (unsigned char)code_len;
    memcpy(t->bytes, code, code_len);
    text_codes.count++;
}

static void text_code_free(void) {
    size_t i;

    for (i = 0; i < text_codes.cap; ++i) {
        free(text_codes.slots[i].key);
    }
    free(text_codes.slots);
    text_codes.slots = NULL;
    text_codes.cap = 0;
    text_codes.count = 0;
}

/*
 * An instruction of registers and numbers, encoded -- once for each way
 * it is written.  1 if the statement is not one (and nothing is done),
 * else what encode_x86_stmt() returns.
 */
static int encode_x86_stmt_by_text(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, unsigned x86_code_bits,
                                   const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                                   char *encerr, size_t encerr_sz) {
    char key[192];
    unsigned asked = cfg != NULL && cfg->warnings != NULL ? cfg->warnings->asked : 0;
    size_t n;
    int rc;

    if (stmt_code_slot(ctx, st) == NULL) {
        return 1;
    }
    n = text_code_key(ctx, st, x86_code_bits, key, sizeof(key));
    if (n == 0) {
        return 1;
    }
    if (text_code_find(key, n, code, code_cap, code_len)) {
        return 0;
    }
    rc = encode_x86_stmt(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);
    /* (One that has a warning to make is not kept: the next that is
     * written the same has the warning to make too.) */
    if (rc == 0 && (cfg == NULL || cfg->warnings == NULL || cfg->warnings->asked == asked)) {
        text_code_keep(key, n, code, *code_len);
    }
    return rc;
}

static void stmt_code_keep(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                           const unsigned char *code, size_t code_len) {
    struct stmt_code *kept = stmt_code_slot(ctx, st);

    if (kept == NULL || code_len == 0 || code_len > sizeof(kept->bytes)) {
        return;
    }
    kept->off = sec_off;
    kept->bits = (unsigned char)x86_code_bits;
    kept->epoch = ctx->layout_epoch;
    kept->anywhere = (unsigned char)stmt_is_place_free(st);
    kept->len = (unsigned char)code_len;
    memcpy(kept->bytes, code, code_len);
}

/*
 * A statement's bytes where it stands.  They are worked out once for a
 * layout and kept (struct stmt_code); AS_NO_STMT_CODE in the
 * environment has them worked out every time, for a test to hold the
 * two to the same object.
 */
int encode_x86_stmt_for_layout(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                                      const as_stmt_t *st, unsigned char *code, size_t code_cap,
                                      size_t *code_len, char *encerr, size_t encerr_sz) {
    int rc;

    if (code != NULL && code_len != NULL && stmt_code_find(ctx, st, sec_off, x86_code_bits, 0, code, code_cap, code_len)) {
        return 0;
    }
    rc = encode_x86_stmt_at(ctx, section_name, sec_off, x86_code_bits, st, code, code_cap, code_len, encerr, encerr_sz);
    if (rc == 0 && code != NULL && code_len != NULL) {
        stmt_code_keep(ctx, st, sec_off, x86_code_bits, code, *code_len);
    }
    return rc;
}

static int encode_x86_stmt_at(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                              const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                              char *encerr, size_t encerr_sz) {
    as_elf_cfg_t stmt_cfg;
    as_stmt_t folded;
    as_operand_t fold_ops[4];
    as_expr_t fold_consts[4];

    if (ctx == NULL || ctx->cfg == NULL || st == NULL) {
        return -1;
    }
    if (ctx->cfg->target != 0 && st->kind == AS_STMT_INSTRUCTION) {
        return arm_encode_stmt(ctx, section_name, sec_off, 1, x86_code_bits, st, code, code_cap, code_len, encerr,
                               encerr_sz);
    }
    st = stmt_fold_label_diffs(ctx, st, x86_code_bits, &folded, fold_ops, fold_consts);
    stmt_cfg = *ctx->cfg;
    stmt_cfg.x86_code_bits = x86_code_bits;
    stmt_cfg.is_64 = x86_code_bits == 64u;
    stmt_cfg.have_current_text_offset = 1u;
    stmt_cfg.current_text_offset = sec_off;

    if ((stmt_cfg.machine == EM_386 || stmt_cfg.machine == EM_X86_64) &&
        st->kind == AS_STMT_INSTRUCTION &&
        st->u.instr.operand_count == 1 &&
        is_rel_mnemonic(st->u.instr.mnemonic)) {
        const as_operand_t *op = &st->u.instr.operands[0];
        as_expr_t *raw_local_expr = NULL;
        const as_expr_t *rel_expr = NULL;
        long long disp;
        /* A local label of another section is no nearer than a symbol
         * of another file: the long form, and a relocation. */
        int far_label = st->u.instr.target_in_other_section && !is_fixed_short_rel_mnemonic(st->u.instr.mnemonic);

        if ((op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            op->u.expr != NULL &&
            op->kind == AS_OPERAND_LABEL_REF &&
            !is_fixed_short_rel_mnemonic(st->u.instr.mnemonic) &&
            (far_label || (!expr_has_local_ref(op->u.expr) &&
                           !expr_is_local_temp_symbol(op->u.expr) &&
                           !raw_is_numeric_local_ref(op->raw)))) {
            stmt_cfg.have_current_text_offset = 0u;
            stmt_cfg.current_text_offset = 0u;
        }
        if (far_label) {
            goto unresolved_rel_target;
        }
        /* `jmp .`, `jmp .+2`, `jmp label+1`: a place and a number, which
         * is as far as it is and needs no relocation. */
        {
            long long abs_target;
            int is_short;

            if (relaxed_place_branch_target(ctx, st, section_name, sec_off, x86_code_bits, &abs_target, &is_short) ==
                0) {
                as_stmt_t tmp = *st;
                as_instruction_t tin = st->u.instr;
                as_operand_t tops[3];
                as_expr_t texpr;
                as_elf_cfg_t local_cfg = stmt_cfg;

                memcpy(tops, st->u.instr.operands, st->u.instr.operand_count * sizeof(*tops));
                memset(&texpr, 0, sizeof(texpr));
                texpr.kind = AS_EXPR_CONST;
                texpr.value = abs_target;
                tops[0].kind = AS_OPERAND_LABEL_REF;
                tops[0].u.expr = &texpr;
                tin.operands = tops;
                tmp.u.instr = tin;
                local_cfg.x86_rel_is_disp = 0u;
                local_cfg.have_current_text_offset = 1u;
                local_cfg.current_text_offset = sec_off;
                return emit_resolved_x86_branch_ex(&local_cfg, &tmp, sec_off, abs_target, is_short, code, code_cap,
                                                   code_len, encerr, encerr_sz);
            }
            if (call_to_place_target(st, sec_off, &abs_target) == 0) {
                return emit_resolved_x86_branch_ex(&stmt_cfg, st, sec_off, abs_target, 0, code, code_cap, code_len,
                                                   encerr, encerr_sz);
            }
        }

        if (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) {
            rel_expr = op->u.expr;
            if ((rel_expr == NULL || (!expr_has_local_ref(rel_expr) && !expr_is_local_temp_symbol(rel_expr))) &&
                raw_is_numeric_local_ref(op->raw)) {
                raw_local_expr = as_parse_expr_string(op->raw, st->file, st->line);
                if (raw_local_expr != NULL) {
                    rel_expr = raw_local_expr;
                }
            }
        }
        if ((op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            rel_expr != NULL &&
            (expr_has_local_ref(rel_expr) || expr_is_local_temp_symbol(rel_expr) ||
             is_fixed_short_rel_mnemonic(st->u.instr.mnemonic))) {
            as_stmt_t tmp = *st;
            as_instruction_t tin = st->u.instr;
            as_operand_t tops[3];
            as_expr_t texpr;
            as_elf_cfg_t local_cfg = stmt_cfg;
            long long abs_target;
            int may_short = x86_branch_may_be_short(ctx, section_name, st, rel_expr, x86_code_bits);
            virtual_addr_value_t target;

            if ((rel_expr->kind == AS_EXPR_LOCAL_REF || expr_is_local_temp_symbol(rel_expr)) &&
                eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, rel_expr,
                                                2, &abs_target) == 0) {
                size_t tmp_len = 0;
                if (emit_resolved_x86_branch_ex(&local_cfg, st, sec_off, abs_target, may_short, code, code_cap,
                                             &tmp_len, encerr, encerr_sz) != 0) {
                    as_expr_free(raw_local_expr);
                    return -1;
                }
                if (tmp_len != 2 &&
                    eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, rel_expr,
                                                    tmp_len, &abs_target) != 0) {
                    as_expr_free(raw_local_expr);
                    return -1;
                }
            } else if (eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, rel_expr, &disp) == 0) {
                abs_target = (long long)sec_off + disp;
            } else if ((expr_has_local_ref(rel_expr) || expr_is_local_temp_symbol(rel_expr)) &&
                       eval_virtual_addr_expr(ctx, st, rel_expr, &target) == 0 &&
                       target.has_section &&
                       as_strcmp(target.section, section_name) == 0) {
                abs_target = target.value;
            } else {
                as_expr_free(raw_local_expr);
                goto unresolved_rel_target;
            }

            memcpy(tops, st->u.instr.operands, st->u.instr.operand_count * sizeof(*tops));
            memset(&texpr, 0, sizeof(texpr));
            texpr.kind = AS_EXPR_CONST;
            texpr.value = abs_target;
            tops[0].kind = AS_OPERAND_LABEL_REF;
            tops[0].u.expr = &texpr;
            tin.operands = tops;
            tmp.u.instr = tin;
            local_cfg.x86_rel_is_disp = 0u;
            local_cfg.have_current_text_offset = 1u;
            local_cfg.current_text_offset = sec_off;
            {
                int rc = emit_resolved_x86_branch_ex(&local_cfg, &tmp, sec_off, abs_target, may_short, code, code_cap,
                                                  code_len, encerr, encerr_sz);
                as_expr_free(raw_local_expr);
                return rc;
            }
        }
        as_expr_free(raw_local_expr);
unresolved_rel_target:
        ;
    }
    if (stmt_cfg.machine == EM_X86_64 &&
        st->kind == AS_STMT_INSTRUCTION &&
        st->u.instr.operand_count > 0) {
        size_t oi;

        for (oi = 0; oi < st->u.instr.operand_count; ++oi) {
            const as_operand_t *op = &st->u.instr.operands[oi];
            long long rel;

            if (op->kind != AS_OPERAND_MEMORY ||
                op->u.mem.base_reg == NULL ||
                !x86_base_is_ip(op->u.mem.base_reg) ||
                op->u.mem.disp == NULL ||
                !expr_has_local_ref(op->u.mem.disp) ||
                eval_local_rel_expr_virtual(ctx, section_name, NULL, 0,
                                            x86_code_bits, op->u.mem.disp, &rel) != 0) {
                continue;
            }
            {
                size_t probe_len = 0;
                as_stmt_t tmp = *st;
                as_instruction_t tin = st->u.instr;
                as_operand_t tops[8];
                as_expr_t texpr;
                long long disp;

                if (st->u.instr.operand_count > sizeof(tops) / sizeof(tops[0])) {
                    return -1;
                }
                if (encode_x86_stmt(ctx, &stmt_cfg, st, code, code_cap, &probe_len, encerr, encerr_sz) != 0) {
                    return -1;
                }
                memcpy(tops, st->u.instr.operands, st->u.instr.operand_count * sizeof(*tops));
                memset(&texpr, 0, sizeof(texpr));
                texpr.kind = AS_EXPR_CONST;
                disp = rel - ((long long)sec_off + (long long)probe_len);
                texpr.value = disp;
                tops[oi].u.mem.disp = &texpr;
                tin.operands = tops;
                tmp.u.instr = tin;
                return encode_x86_stmt(ctx, &stmt_cfg, &tmp, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
    }

    {
        int rc = encode_x86_stmt_by_text(ctx, &stmt_cfg, x86_code_bits, st, code, code_cap, code_len, encerr, encerr_sz);

        if (rc != 1) {
            return rc;
        }
    }
    return encode_x86_stmt(ctx, &stmt_cfg, st, code, code_cap, code_len, encerr, encerr_sz);
}

static int emit_text_program(emit_ctx_t *ctx) {
    sec_buf_vec_t secbufs;
    size_t i;
    int trace_emit_phase = getenv("AS_DEBUG_EMIT_PHASE") != NULL;
    unsigned char code[32];
    size_t code_len;
    char encerr[256];
    section_track_t track;
    int trc;

    asm_var_reset(ctx);
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        int in_exec;
        int drc;

        if (trace_emit_phase && ((i % 500) == 0 || (i >= 150 && i < 260))) {
            fprintf(stderr, "as: emit-phase text index=%zu line=%u\n", i, st->line);
        }

        in_exec = section_name_is_executable(ctx, track.current);
        sb = NULL;
        if (in_exec) {
            sb = sec_buf_get_or_add(&secbufs, track.current);
            if (sb == NULL) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            if (apply_asm_var_directive(ctx, st, &st->u.directive) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            if (!section_name_is_executable(ctx, track.current)) {
                continue;
            }
            sb = sec_buf_get_or_add(&secbufs, track.current);
            if (sb == NULL) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            drc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                            track.x86_code_bits, &st->u.directive);
            if (drc < 0) {
                if (ctx->errbuf == NULL || ctx->errbuf[0] == '\0') {
                    set_err(ctx, "%s:%u: malformed directive data", st->file != NULL ? st->file : "<input>", st->line);
                }
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            continue;
        }
        if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }
        if (!section_name_is_executable(ctx, track.current)) {
            continue;
        }

        {
            if (encode_x86_stmt_for_layout(ctx, track.current, (uint64_t)sb->buf.len, track.x86_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
                set_err(ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, encerr);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (bytebuf_append(&sb->buf, code, code_len) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    for (i = 0; i < secbufs.count; ++i) {
        elf_section_t *sec = section_for_name(ctx, secbufs.items[i].name);
        if (sec == NULL) {
            continue;
        }
        if (set_section_bytes(ctx, sec, secbufs.items[i].name, secbufs.items[i].buf.data,
                              secbufs.items[i].buf.len) != 0) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return 0;
}

static uint8_t map_bind(as_sym_bind_t bind) {
    switch (bind) {
    case AS_SYM_BIND_GLOBAL:
        return STB_GLOBAL;
    case AS_SYM_BIND_WEAK:
        return STB_WEAK;
    default:
        return STB_LOCAL;
    }
}

static uint8_t map_type(as_sym_type_t type) {
    switch (type) {
    case AS_SYM_TYPE_FUNCTION:
        return STT_FUNC;
    case AS_SYM_TYPE_OBJECT:
    case AS_SYM_TYPE_COMMON:
    case AS_SYM_TYPE_UNIQUE_OBJECT:
        return STT_OBJECT;
    case AS_SYM_TYPE_TLS_OBJECT:
        return STT_TLS;
    case AS_SYM_TYPE_IFUNC:
        return STT_GNU_IFUNC;
    default:
        return STT_NOTYPE;
    }
}

static uint8_t map_vis(as_sym_visibility_t vis) {
    switch (vis) {
    case AS_SYM_VIS_INTERNAL:
        return STV_INTERNAL;
    case AS_SYM_VIS_HIDDEN:
        return STV_HIDDEN;
    case AS_SYM_VIS_PROTECTED:
        return STV_PROTECTED;
    default:
        return STV_DEFAULT;
    }
}

static uint32_t reloc_type_for_machine(unsigned machine) {
    switch (machine) {
    case EM_386:
        return R_386_32;
    case EM_X86_64:
        return R_X86_64_64;
    case EM_ARM:
        return R_ARM_ABS32;
    case EM_AARCH64:
        return R_AARCH64_ABS64;
    default:
        return R_386_32;
    }
}

/* Whether a relocation type is one of thread-local storage's. */
static int reloc_type_is_tls(unsigned machine, uint32_t t) {
    if (machine == EM_X86_64) {
        return (t >= R_X86_64_DTPMOD64 && t <= R_X86_64_TPOFF32) || t == R_X86_64_GOTPC32_TLSDESC ||
               t == R_X86_64_TLSDESC_CALL || t == R_X86_64_TLSDESC;
    }
    if (machine == EM_386) {
        return (t >= R_386_TLS_TPOFF && t <= R_386_TLS_LDM) || (t >= R_386_TLS_LDO_32 && t <= R_386_TLS_TPOFF32) ||
               t == R_386_TLS_GOTDESC || t == R_386_TLS_DESC_CALL || t == R_386_TLS_DESC;
    }
    return 0;
}

/* `.` for an expression that is being made a relocation's addend: the
 * offset in its section of the statement, which the cookie points at. */
static int reloc_dot_lookup(void *cookie, const char *name, long long *out) {
    if (as_strcmp(name, ".") != 0) {
        return 0;
    }
    *out = *(const long long *)cookie;
    return 1;
}

/* What reloc_type_for_symbol() gives for an operator there is not. */
#define AS_RELOC_TYPE_BAD 0xffffffffu

/*
 * Whether the instruction a relocation is being made for is one the
 * psABIs let a linker rewrite when the symbol turns out to be local --
 * a load by mov, a test, one of the eight binary operations reading
 * memory, or a call or jump through memory: 0 if it is not (or the
 * relocation is a data directive's), 1 if it is, 2 if it is and has a
 * REX prefix.  Set around the making of an instruction's relocation.
 */
static int reloc_got_load;

/*
 * Set while a relocation of an ARM or AArch64 instruction is added: the
 * type is the target's own already, and for ARM, whose addends are in
 * the instruction, the symbol stays the symbol -- a compiler's local
 * label is not made its section and a distance into it, which the
 * instruction's field would have to hold.
 */
static int reloc_type_exact;

/* The relocation of a data directive, chosen as x86's of the same ELF
 * class, as the target names it; AS_RELOC_TYPE_BAD if it has none. */
static uint32_t reloc_type_for_target(unsigned target, uint32_t type) {
    static const uint32_t arm[][2] = {
        {R_386_NONE, R_ARM_NONE}, {R_386_32, R_ARM_ABS32}, {R_386_PC32, R_ARM_REL32},
        {R_386_16, R_ARM_ABS16},  {R_386_8, R_ARM_ABS8},
    };
    static const uint32_t a64[][2] = {
        {R_X86_64_NONE, R_AARCH64_NONE},   {R_X86_64_64, R_AARCH64_ABS64},     {R_X86_64_32, R_AARCH64_ABS32},
        {R_X86_64_32S, R_AARCH64_ABS32},   {R_X86_64_16, R_AARCH64_ABS16},     {R_X86_64_PC64, R_AARCH64_PREL64},
        {R_X86_64_PC32, R_AARCH64_PREL32}, {R_X86_64_PLT32, R_AARCH64_PREL32}, {R_X86_64_PC16, R_AARCH64_PREL16},
    };
    const uint32_t (*map)[2] = target == EM_AARCH64 ? a64 : arm;
    size_t count = target == EM_AARCH64 ? sizeof(a64) / sizeof(a64[0]) : sizeof(arm) / sizeof(arm[0]);
    size_t i;

    for (i = 0; i < count; ++i) {
        if (map[i][0] == type) {
            return map[i][1];
        }
    }
    return AS_RELOC_TYPE_BAD;
}

static uint32_t reloc_type_for_symbol(unsigned machine, const char *name, const char *reloc_op, uint32_t fallback) {
    if (name == NULL) {
        return fallback;
    }
    if (machine == EM_386 || machine == EM_X86_64) {
        uint32_t t;

        /*
         * The operator on the symbol, if it has one, names the
         * relocation.  Only @PLT was known for i386, so sym@GOTOFF and
         * sym@GOT -- how position-independent code reaches everything --
         * were an R_386_32 against a symbol whose name ended in @GOTOFF.
         */
        int op = as_reloc_op_type(machine, reloc_op, &t);

        if (op == 1) {
            /* In a field of 64 bits, the type of that width. */
            if (fallback == R_X86_64_64) {
                t = as_reloc_op_widen(machine, t);
            }
            /* In an instruction the linker may rewrite, the type that
             * says so: see x86_got_load_kind(). */
            if (machine == EM_X86_64 && t == R_X86_64_GOTPCREL && reloc_got_load != 0) {
                t = reloc_got_load == 2 ? R_X86_64_REX_GOTPCRELX : R_X86_64_GOTPCRELX;
            } else if (machine == EM_386 && t == R_386_GOT32 && reloc_got_load != 0) {
                t = R_386_GOT32X;
            }
            return t;
        }
        /* What follows the @ is no operator, or none this machine has:
         * `foo@BOGUS`, `foo@GOTPCREL` in i386 code.  It was left on the
         * symbol's name, and the object named a symbol `foo@BOGUS`. */
        if (op < 0) {
            return AS_RELOC_TYPE_BAD;
        }
        /* The table itself, by name, in i386 code: relative to here. */
        if (machine == EM_386 && as_strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0 &&
            (fallback == R_386_32 || fallback == R_386_PC32)) {
            return R_386_GOTPC;
        }
        /* And in x86-64 code, where it was the table's address itself:
         * `movabs $_GLOBAL_OFFSET_TABLE_, %rax` was R_X86_64_64. */
        if (machine == EM_X86_64 && as_strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0) {
            if (fallback == R_X86_64_64 || fallback == R_X86_64_PC64) {
                return R_X86_64_GOTPC64;
            }
            if (fallback == R_X86_64_32 || fallback == R_X86_64_32S || fallback == R_X86_64_PC32) {
                return R_X86_64_GOTPC32;
            }
        }
    }
    return fallback;
}

static elf_symbol_t *find_emit_symbol(emit_ctx_t *ctx, const char *name) {
    size_t pos;

    if (ctx == NULL || name == NULL || ctx->sym_index == NULL || ctx->sym_index_cap == 0) {
        return NULL;
    }
    pos = hash_name(name) & (ctx->sym_index_cap - 1);
    while (ctx->sym_index[pos] != 0) {
        size_t idx = ctx->sym_index[pos] - 1;
        if (as_strcmp(ctx->sym_map[idx].name, name) == 0) {
            return ctx->sym_map[idx].sym;
        }
        pos = (pos + 1) & (ctx->sym_index_cap - 1);
    }
    return NULL;
}

static int append_emit_symbol(emit_ctx_t *ctx, const char *name, elf_symbol_t *sym);

static int ensure_emit_symbol_index(emit_ctx_t *ctx) {
    size_t cap;
    size_t *slots;
    size_t i;

    if (ctx == NULL) {
        return -1;
    }
    if (ctx->sym_index_cap != 0 && (ctx->sym_count + 1) * 2 < ctx->sym_index_cap) {
        return 0;
    }
    cap = 64;
    while (cap <= (ctx->sym_count + 1) * 2) {
        cap <<= 1;
    }
    slots = (size_t *)calloc(cap, sizeof(*slots));
    if (slots == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->sym_count; ++i) {
        size_t pos = hash_name(ctx->sym_map[i].name) & (cap - 1);
        while (slots[pos] != 0) {
            pos = (pos + 1) & (cap - 1);
        }
        slots[pos] = i + 1;
    }
    free(ctx->sym_index);
    ctx->sym_index = slots;
    ctx->sym_index_cap = cap;
    return 0;
}

static const as_symbol_t *find_as_symbol_const(emit_ctx_t *ctx, const as_symtab_t *symtab, const char *name);

static elf_symbol_t *ensure_emit_symbol(emit_ctx_t *ctx, const char *name) {
    elf_symbol_t *sym;
    elf_section_t *sec;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    sym = find_emit_symbol(ctx, name);
    if (sym != NULL) {
        return sym;
    }

    sec = section_for_name(ctx, name);
    if (sec != NULL) {
        sym = elf_add_symbol(ctx->obj, name, 0, 0, STB_LOCAL, STT_SECTION);
    } else {
        /* (Weak if the source said so of it, though it was not
         * written with the source's symbols: see
         * symbol_is_not_emitted().) */
        const as_symbol_t *said = find_as_symbol_const(ctx, ctx->symtab, name);

        sym = elf_add_symbol(ctx->obj, name, 0, 0,
                             said != NULL && said->bind == AS_SYM_BIND_WEAK ? STB_WEAK : STB_GLOBAL, STT_NOTYPE);
    }
    if (sym == NULL) {
        return NULL;
    }
    if (sec != NULL && elf_symbol_define(sym, sec, 0) != ELF_OK) {
        return NULL;
    }
    if (append_emit_symbol(ctx, name, sym) != 0) {
        return NULL;
    }
    return sym;
}

/* A symbol of the source by its name: the first of that name, found by
 * a table made the first time one is asked for. */
static const as_symbol_t *find_as_symbol_const(emit_ctx_t *ctx, const as_symtab_t *symtab, const char *name) {
    size_t i;

    if (symtab == NULL || name == NULL) {
        return NULL;
    }
    if (!ctx->symtab_indexed) {
        for (i = symtab->count; i > 0; --i) {
            if (symtab->items[i - 1].name != NULL &&
                as_strmap_put(&ctx->symtab_by_name, symtab->items[i - 1].name, i - 1) != 0) {
                as_strmap_free(&ctx->symtab_by_name);
                return NULL;
            }
        }
        ctx->symtab_indexed = 1;
    }
    return as_strmap_get(&ctx->symtab_by_name, name, &i) ? &symtab->items[i] : NULL;
}

/*
 * `.symver name, name@version`.
 *
 * A version of a symbol is a symbol whose name says so, which the
 * linker reads: `name@version` for a version that is not the one a
 * plain reference gets, `name@@version` for the one that is.  (This
 * assembler had made version sections of its own instead, which are the
 * linker's to make of these names.)  `name@@@version` is the two-at
 * form of a symbol defined here and the one-at form of one that is not,
 * and is the only name the symbol then has; a symbol of another file
 * has its versioned name alone whichever way it is written.
 *
 * The one versioned name a symbol is written under in place of its
 * own, to be freed; NULL if it keeps its own.
 */
static int symbol_is_not_emitted(emit_ctx_t *ctx, const as_symtab_t *symtab, const as_symbol_t *s);

static char *symver_spelled(const char *version, size_t len, int defined) {
    const char *three = NULL;
    char *out = (char *)malloc(len + 1);
    size_t i;

    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i + 2 < len; ++i) {
        if (version[i] == '@' && version[i + 1] == '@' && version[i + 2] == '@') {
            three = version + i;
            break;
        }
    }
    if (three == NULL) {
        memcpy(out, version, len);
        out[len] = '\0';
        return out;
    }
    i = (size_t)(three - version);
    memcpy(out, version, i);
    out[i++] = '@';
    if (defined) {
        out[i++] = '@';
    }
    memcpy(out + i, three + 3, len - (size_t)(three - version) - 3);
    out[i + len - (size_t)(three - version) - 3] = '\0';
    return out;
}

static char *symver_sole_name(const as_symbol_t *s) {
    int defined = s->defined || s->is_common;

    if (s->version == NULL || strchr(s->version, '\n') != NULL ||
        (defined && strstr(s->version, "@@@") == NULL)) {
        return NULL;
    }
    return symver_spelled(s->version, strlen(s->version), defined);
}

/* The versions of the symbols defined here that keep their own names
 * too: each a symbol beside the one it is a version of, and like it.
 * To be called when every symbol has its place. */
static int emit_symbol_versions(emit_ctx_t *ctx, const as_symtab_t *symtab) {
    size_t i;

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        const char *v = s->version;
        elf_symbol_t *base;

        if (v == NULL || symbol_is_not_emitted(ctx, symtab, s)) {
            continue;
        }
        {
            char *sole = symver_sole_name(s);

            if (sole != NULL) {
                free(sole);
                continue;
            }
        }
        base = find_emit_symbol(ctx, s->name);
        if (base == NULL) {
            continue;
        }
        while (*v != '\0') {
            const char *end = strchr(v, '\n');
            size_t len = end != NULL ? (size_t)(end - v) : strlen(v);
            char *name = symver_spelled(v, len, 1);
            elf_symbol_t *sym;

            if (name == NULL) {
                return -1;
            }
            sym = elf_add_symbol(ctx->obj, name, elf_symbol_value(base), elf_symbol_size(base), elf_symbol_bind(base),
                                 elf_symbol_type(base));
            free(name);
            if (sym == NULL || elf_symbol_set_shndx(sym, elf_symbol_shndx(base)) != ELF_OK ||
                elf_symbol_set_value(sym, elf_symbol_value(base)) != ELF_OK ||
                elf_symbol_set_visibility(sym, elf_symbol_visibility(base)) != ELF_OK) {
                return -1;
            }
            v += len + (end != NULL ? 1 : 0);
        }
    }
    return 0;
}

/*
 * The symbols of the source that are not symbols of the object: a
 * compiler's .L labels; a name set to itself; and a name for a symbol
 * of another file, which is that symbol wherever it is used and has
 * nothing of its own to say -- it had been written as a local symbol
 * that no file defines.
 */
static int symbol_is_not_emitted(emit_ctx_t *ctx, const as_symtab_t *symtab, const as_symbol_t *s) {
    const as_symbol_t *target;

    /* (A .L label that is made global or weak is a symbol after all.) */
    if (is_local_temp_symbol_name(s->name) && s->bind == AS_SYM_BIND_LOCAL) {
        return 1;
    }
    /* A view of the line table is a number and no symbol. */
    {
        size_t at;

        if (s->name != NULL && !s->defined && line_view_names(ctx) == 0 &&
            as_strmap_get(&ctx->line_view_names, s->name, &at)) {
            return 1;
        }
    }
    /* A weak symbol that is neither here nor referred to says nothing:
     * GNU as leaves it out.  (One that is referred to is made when the
     * reference is.) */
    if (s->bind == AS_SYM_BIND_WEAK && !s->defined && !s->is_common && s->alias_target == NULL &&
        s->reference_count == 0) {
        return 1;
    }
    if (s->alias_target == NULL || s->name == NULL) {
        return 0;
    }
    if (as_strcmp(s->alias_target, s->name) == 0) {
        return 1;
    }
    if (s->alias_minus != NULL || s->alias_from_dot || is_local_temp_symbol_name(s->alias_target)) {
        return 0;
    }
    target = find_as_symbol_const(ctx, symtab, s->alias_target);
    return target == NULL || (!target->defined && !target->is_common);
}

static int append_emit_symbol(emit_ctx_t *ctx, const char *name, elf_symbol_t *sym) {
    emit_sym_t *next;
    size_t pos;

    if (ctx->sym_count == ctx->sym_cap) {
        size_t ncap = ctx->sym_cap == 0 ? 64 : ctx->sym_cap * 2;

        next = (emit_sym_t *)realloc(ctx->sym_map, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->sym_map = next;
        ctx->sym_cap = ncap;
    }
    ctx->sym_map[ctx->sym_count].name = xstrdup(name);
    ctx->sym_map[ctx->sym_count].sym = sym;
    if (ctx->sym_map[ctx->sym_count].name == NULL) {
        return -1;
    }
    if (ensure_emit_symbol_index(ctx) != 0) {
        free(ctx->sym_map[ctx->sym_count].name);
        ctx->sym_map[ctx->sym_count].name = NULL;
        return -1;
    }
    pos = hash_name(name) & (ctx->sym_index_cap - 1);
    while (ctx->sym_index[pos] != 0) {
        pos = (pos + 1) & (ctx->sym_index_cap - 1);
    }
    ctx->sym_index[pos] = ctx->sym_count + 1;
    ctx->sym_count++;
    return 0;
}

/*
 * What the data directives of the source say, into the sections they
 * are in: from the statements, as everything else is.  (There was a
 * list of them made beforehand, as_data.c's, that this was handed and
 * did not read.)
 */
static int emit_data_sections(emit_ctx_t *ctx) {
    sec_buf_vec_t secbufs;
    section_track_t track;
    size_t i;
    int trc;

    asm_var_reset(ctx);
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        int drc;
        elf_section_t *sec;

        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        if (apply_asm_var_directive(ctx, st, &st->u.directive) != 0) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        trc = section_track_apply_directive(&track, &st->u.directive);
        if (trc < 0) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        if (trc > 0 || section_name_is_executable(ctx, track.current)) {
            continue;
        }
        sec = section_for_name(ctx, track.current);
        if (sec == NULL) {
            set_err(ctx, "%s:%u: unknown section %s", st->file != NULL ? st->file : "<input>", st->line, track.current);
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        drc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                        track.x86_code_bits, &st->u.directive);
        if (drc < 0) {
            set_err(ctx, "%s:%u: malformed %s directive data", st->file != NULL ? st->file : "<input>", st->line, track.current);
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        /* `.byte 1,,2`: the empty argument was made a zero where the
         * line was parsed, and here it is said. */
        if (st->u.directive.zero_assumed != 0 && ctx->cfg != NULL) {
            warn_stmt(ctx->cfg, st, "zero assumed for missing expression");
        }
        /*
         * A number too wide for its directive is stored cut down, and
         * said to be: `.byte 256` is 00 and a warning.  The test is GNU
         * as's -- bits lost from the value and from its negative both,
         * so that -129, which is 7f, passes as 255 does.
         */
        if (ctx->cfg != NULL) {
            unsigned width = directive_fixed_scalar_width(&st->u.directive);
            size_t k;

            for (k = 0; (width == 1 || width == 2 || width == 4) && k < st->u.directive.arg_count; ++k) {
                as_expr_t *e = as_parse_expr_string(st->u.directive.args[k], st->file, st->line);
                long long v;

                if (e != NULL && eval_expr_const(e, &v) == 0) {
                    unsigned long long high = ~0ULL << (8u * width);
                    unsigned long long u = (unsigned long long)v;

                    if ((u & high) != 0 && ((0ULL - u) & high) != 0) {
                        char text[96];

                        snprintf(text, sizeof(text), "value 0x%llx truncated to 0x%llx", u, u & ~high);
                        warn_stmt(ctx->cfg, st, text);
                    }
                }
                as_expr_free(e);
            }
        }
    }

    for (i = 0; i < secbufs.count; ++i) {
        elf_section_t *sec = section_for_name(ctx, secbufs.items[i].name);
        if (sec == NULL) {
            continue;
        }
        if (set_section_bytes(ctx, sec, secbufs.items[i].name, secbufs.items[i].buf.data,
                              secbufs.items[i].buf.len) != 0) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
    }
    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return 0;
}

/*
 * `.file N "name"`: a number names one file.  Given to a second it was
 * taken without a word -- which a compiler's configure script tries,
 * to learn whether the assembler keeps the numbers it is given, and
 * concluded that this one does not.
 */
static int check_file_numbers(emit_ctx_t *ctx, const as_parse_result_t *parsed) {
    struct numbered {
        unsigned long number;
        const char *name;
    } *seen = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t i;
    size_t k;
    int rc = 0;

    for (i = 0; i < parsed->count && rc == 0; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        const char *arg;
        char *end = NULL;
        unsigned long number;

        if (st->kind != AS_STMT_DIRECTIVE || as_strcmp(st->u.directive.name, ".file") != 0 ||
            st->u.directive.arg_count == 0 || st->u.directive.args[0] == NULL) {
            continue;
        }
        arg = st->u.directive.args[0];
        while (isspace((unsigned char)*arg)) {
            arg++;
        }
        if (!isdigit((unsigned char)*arg)) {
            continue;
        }
        number = strtoul(arg, &end, 10);
        while (isspace((unsigned char)*end)) {
            end++;
        }
        for (k = 0; k < count; ++k) {
            if (seen[k].number == number) {
                break;
            }
        }
        if (k < count) {
            if (as_strcmp(seen[k].name, end) != 0) {
                set_err(ctx, "%s:%u: file number %lu is %s already, and cannot be %s",
                        st->file != NULL ? st->file : "<input>", st->line, number, seen[k].name, end);
                rc = -1;
            }
            continue;
        }
        if (count == cap) {
            size_t ncap = cap != 0 ? cap * 2 : 16;
            struct numbered *next = (struct numbered *)realloc(seen, ncap * sizeof(*next));

            if (next == NULL) {
                set_err(ctx, "out of memory");
                rc = -1;
                break;
            }
            seen = next;
            cap = ncap;
        }
        seen[count].number = number;
        seen[count].name = end;
        count++;
    }
    free(seen);
    return rc;
}

static int collect_directive_presence(const as_parse_result_t *parsed, int *has_file_loc, int *has_cfi) {
    size_t i;

    *has_file_loc = 0;
    *has_cfi = 0;
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        if (as_strcmp(st->u.directive.name, ".file") == 0 || as_strcmp(st->u.directive.name, ".loc") == 0) {
            *has_file_loc = 1;
        }
        if (strncmp(st->u.directive.name, ".cfi_", 5) == 0) {
            *has_cfi = 1;
        }
    }
    return 0;
}

static int ensure_section_exists(emit_ctx_t *ctx, const char *name, uint32_t type, uint64_t flags,
                                 uint64_t align, const void *data, size_t data_sz) {
    elf_section_t *sec = elf_find_section(ctx->obj, name);
    if (sec == NULL) {
        sec = elf_add_section(ctx->obj, name, type, flags);
        if (sec == NULL) {
            return -1;
        }
    }
    if (elf_section_set_align(sec, align) != ELF_OK) {
        return -1;
    }
    if (data != NULL && data_sz > 0 && elf_section_set_data(sec, data, data_sz) != ELF_OK) {
        return -1;
    }
    return 0;
}

static int machine_relocation_addend_is_in_place(unsigned machine);

static int64_t local_label_addend(emit_ctx_t *ctx, const char *name);

static int add_reloc_for_symbol_ex(emit_ctx_t *ctx, elf_section_t *sec, const char *name, uint64_t offset,
                                   uint32_t fallback_type, int64_t addend);

static int add_reloc_for_symbol_op(emit_ctx_t *ctx, elf_section_t *sec, const char *name, const char *reloc_op,
                                   uint64_t offset, uint32_t fallback_type, int64_t addend);

/*
 * One section of call frame information, `.eh_frame` or `.debug_frame`,
 * from the `.cfi_*` directives and where each stands: its bytes, and a
 * relocation for each address in them.
 */
static int emit_frame_section(emit_ctx_t *ctx, const as_cfi_event_t *events, size_t count, int debug_frame) {
    const char *name = debug_frame ? ".debug_frame" : ".eh_frame";
    int is_64 = ctx->cfg->is_64 != 0;
    unsigned machine = ctx->cfg->machine;
    as_cfi_frames_t frames;
    elf_section_t *sec;
    char why[256];
    size_t i;
    int rc = -1;

    if (as_cfi_build(events, count, is_64, debug_frame, &frames, why, sizeof(why)) != 0) {
        set_err(ctx, "%s", why);
        return -1;
    }
    /* (No .eh_frame_hdr: that is the linker's to make, of the frames
     * of every object, and there is none in an object.) */
    if (ensure_section_exists(ctx, name, SHT_PROGBITS, debug_frame ? 0 : SHF_ALLOC, is_64 ? 8 : 4, NULL, 0) != 0 ||
        (sec = elf_find_section(ctx->obj, name)) == NULL) {
        set_err(ctx, "failed to emit %s", name);
        goto out;
    }
    for (i = 0; i < frames.fixup_count; ++i) {
        const as_cfi_fixup_t *f = &frames.fixups[i];
        uint32_t type;

        if (machine == EM_X86_64) {
            type = f->pcrel ? (f->width == 8 ? R_X86_64_PC64 : f->width == 2 ? R_X86_64_PC16 : R_X86_64_PC32)
                            : (f->width == 8 ? R_X86_64_64 : f->width == 2 ? R_X86_64_16 : R_X86_64_32);
        } else {
            if (f->width == 8) {
                set_err(ctx, "%s: a pointer of eight bytes, which an ELF32 object has no relocation for", name);
                goto out;
            }
            type = f->pcrel ? (f->width == 2 ? R_386_PC16 : R_386_PC32) : (f->width == 2 ? R_386_16 : R_386_32);
        }
        if (machine_relocation_addend_is_in_place(machine)) {
            write_u64_le_at(frames.bytes + f->offset, (uint64_t)(f->addend + local_label_addend(ctx, f->symbol)),
                            f->width);
        }
        if (add_reloc_for_symbol_ex(ctx, sec, f->symbol, f->offset, type, f->addend) != 0) {
            set_err(ctx, "%s: no relocation could be made against '%s'", name, f->symbol);
            goto out;
        }
    }
    if (frames.len != 0 && elf_section_set_data(sec, frames.bytes, frames.len) != ELF_OK) {
        set_err(ctx, "failed to emit %s", name);
        goto out;
    }
    rc = 0;
out:
    as_cfi_frames_free(&frames);
    return rc;
}

/* What is between the quotes of a word that is a string: a copy, or
 * NULL for a word that is none or for no memory. */
static char *unquoted(const char *word) {
    size_t n = strlen(word);
    char *copy;

    if (n == 0) {
        return NULL;
    }
    /* (The parser has taken the quotes off some, and they come bare.) */
    if (n < 2 || word[0] != '"' || word[n - 1] != '"') {
        copy = (char *)malloc(n + 1);
        if (copy != NULL) {
            memcpy(copy, word, n + 1);
        }
        return copy;
    }
    copy = (char *)malloc(n - 1);
    if (copy != NULL) {
        memcpy(copy, word + 1, n - 2);
        copy[n - 2] = '\0';
    }
    return copy;
}

static int source_has_loc(const as_parse_result_t *parsed) {
    size_t i;

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        const as_directive_t *d = &st->u.directive;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL) {
            continue;
        }
        if (as_strcmp(d->name, ".loc") == 0) {
            return 1;
        }
        /* A file with a number is of a line table the source means to
         * write, whether it has got to a row of it or not: a compiler's
         * output for a function with no line, and what GCC's configure
         * asks -g of. */
        if (as_strcmp(d->name, ".file") == 0 && d->arg_count != 0 && d->args[0] != NULL &&
            isdigit((unsigned char)d->args[0][0])) {
            return 1;
        }
    }
    return 0;
}

/* Whether the source writes its own debugging information: a section
 * `.debug_info` or `.debug_line`, as a compiler's output has. */
static int source_has_debug_sections(const as_parse_result_t *parsed) {
    size_t i;

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        const as_directive_t *d = &st->u.directive;

        if (st->kind == AS_STMT_DIRECTIVE && d->name != NULL &&
            (as_strcmp(d->name, ".section") == 0 || as_strcmp(d->name, ".pushsection") == 0) && d->arg_count != 0 &&
            d->args[0] != NULL &&
            (as_strcmp(d->args[0], ".debug_line") == 0 || as_strcmp(d->args[0], ".debug_info") == 0)) {
            return 1;
        }
    }
    return 0;
}

/*
 * The compile unit of an assembly source assembled with -g: one entry
 * in `.debug_info` that names the source and its line table -- and,
 * where its code is all in one section, where that begins and how long
 * it is -- and the abbreviation it is written by.  A debugger finds
 * the line table by it.
 */
static int emit_own_compile_unit(emit_ctx_t *ctx, const char *name, const as_dwline_end_t *ends, size_t end_count) {
    /* DW_TAG_compile_unit, no children: DW_AT_stmt_list as an offset
     * into a section, DW_AT_name and DW_AT_producer as strings,
     * DW_AT_language in two bytes -- and then, with `ranged`,
     * DW_AT_low_pc as an address and DW_AT_high_pc as a length. */
    static const unsigned char abbrev_head[] = {0x01, 0x11, 0x00, 0x10, 0x17, 0x03, 0x08, 0x25, 0x08, 0x13, 0x05};
    static const char producer[] = "Substrate as";
    unsigned machine = ctx->cfg->machine;
    unsigned ptr = ctx->cfg->is_64 ? 8u : 4u;
    uint32_t abs32 = machine == EM_X86_64 ? R_X86_64_32 : R_386_32;
    int ranged = end_count == 1;
    elf_section_t *info;
    elf_section_t *abbrev;
    bytebuf_t a;
    bytebuf_t d;
    unsigned char word[8];
    size_t low_at = 0;
    size_t k;
    int rc = -1;

    memset(&a, 0, sizeof(a));
    memset(&d, 0, sizeof(d));
    if (ensure_section_exists(ctx, ".debug_abbrev", SHT_PROGBITS, 0, 1, NULL, 0) != 0 ||
        ensure_section_exists(ctx, ".debug_info", SHT_PROGBITS, 0, 1, NULL, 0) != 0 ||
        (abbrev = elf_find_section(ctx->obj, ".debug_abbrev")) == NULL ||
        (info = elf_find_section(ctx->obj, ".debug_info")) == NULL) {
        set_err(ctx, "failed to emit .debug_info");
        return -1;
    }
    if (elf_section_size(abbrev) != 0 || elf_section_size(info) != 0) {
        /* The source has debugging information of its own, and wants
         * none made for it. */
        return 0;
    }
    if (bytebuf_append(&a, abbrev_head, sizeof(abbrev_head)) != 0 ||
        (ranged && bytebuf_append(&a, ptr == 8 ? "\x11\x01\x12\x07" : "\x11\x01\x12\x06", 4) != 0) ||
        bytebuf_append_zeros(&a, 3) != 0) {
        goto oom;
    }
    /* The unit: its length, DWARF 4, where its abbreviations are, the
     * size of an address; then the one entry. */
    memset(word, 0, sizeof(word));
    word[0] = 4;
    if (bytebuf_append_zeros(&d, 4) != 0 || bytebuf_append(&d, word, 2) != 0 || bytebuf_append_zeros(&d, 4) != 0) {
        goto oom;
    }
    word[0] = (unsigned char)ptr;
    if (bytebuf_append(&d, word, 1) != 0 || bytebuf_append(&d, "\x01", 1) != 0 || bytebuf_append_zeros(&d, 4) != 0 ||
        bytebuf_append(&d, name, strlen(name) + 1) != 0 || bytebuf_append(&d, producer, sizeof(producer)) != 0 ||
        bytebuf_append(&d, "\x01\x80", 2) != 0) {
        goto oom;
    }
    if (ranged) {
        low_at = d.len;
        if (bytebuf_append_zeros(&d, ptr) != 0) {
            goto oom;
        }
        for (k = 0; k < ptr; ++k) {
            word[k] = (unsigned char)(ends[0].size >> (8 * k));
        }
        if (bytebuf_append(&d, word, ptr) != 0) {
            goto oom;
        }
    }
    {
        uint32_t len = (uint32_t)(d.len - 4);

        for (k = 0; k < 4; ++k) {
            d.data[k] = (unsigned char)(len >> (8 * k));
        }
    }
    /* (The fields a relocation fills are 0, which is the addend an
     * object with them in place has.) */
    if (add_reloc_for_symbol_ex(ctx, info, ".debug_abbrev", 6, abs32, 0) != 0 ||
        add_reloc_for_symbol_ex(ctx, info, ".debug_line", 12, abs32, 0) != 0 ||
        (ranged && add_reloc_for_symbol_ex(ctx, info, ends[0].section, low_at,
                                           machine == EM_X86_64 ? R_X86_64_64 : R_386_32, 0) != 0)) {
        set_err(ctx, ".debug_info: no relocation could be made");
        goto out;
    }
    if (elf_section_set_data(abbrev, a.data, a.len) != ELF_OK || elf_section_set_data(info, d.data, d.len) != ELF_OK) {
        set_err(ctx, "failed to emit .debug_info");
        goto out;
    }
    rc = 0;
    goto out;
oom:
    set_err(ctx, "out of memory");
out:
    free(a.data);
    free(d.data);
    return rc;
}

/*
 * `.debug_line`: the files `.file N` named and the rows of `.loc`,
 * made a line table (as_dwline.h).  There was none: the directives
 * were taken and nothing made of them, and a compiler that leaves the
 * table to the assembler had no line numbers in what it built.
 */
static int emit_line_table(emit_ctx_t *ctx, const as_parse_result_t *parsed) {
    unsigned machine = ctx->cfg->machine;
    as_dwline_file_t *files = NULL;
    char **owned = NULL;
    size_t owned_count = 0;
    size_t file_count = 0;
    as_dwline_row_t *rows = NULL;
    as_dwline_end_t *ends = NULL;
    size_t end_count = 0;
    as_dwline_table_t table;
    elf_section_t *sec;
    char why[256];
    size_t i;
    size_t k;
    int rc = -1;

    memset(&table, 0, sizeof(table));
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        const as_directive_t *d = &st->u.directive;
        char word[1024];
        size_t arg = 0;
        const char *at = NULL;
        char *first;
        char *second = NULL;
        unsigned long number;
        char *end;
        void *grown;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL || as_strcmp(d->name, ".file") != 0 ||
            !next_word(d, &arg, &at, word, sizeof(word)) || !isdigit((unsigned char)word[0])) {
            continue;
        }
        number = strtoul(word, &end, 10);
        if (*end != '\0' || number > 0xffffu || !next_word(d, &arg, &at, word, sizeof(word)) ||
            (first = unquoted(word)) == NULL) {
            set_err(ctx, "%s:%u: .file: a number and a name in quotes", st->file != NULL ? st->file : "<input>",
                    st->line);
            goto out;
        }
        /* A second string is the name, and the first its directory. */
        if (next_word(d, &arg, &at, word, sizeof(word)) && as_strcmp(word, "md5") != 0) {
            second = unquoted(word);
        }
        for (k = 0; k < file_count; ++k) {
            if (files[k].number == (unsigned)number) {
                break;
            }
        }
        if (k < file_count) {
            /* (Said again, and the same: check_file_numbers.) */
            free(first);
            free(second);
            continue;
        }
        grown = realloc(owned, (owned_count + 2) * sizeof(owned[0]));
        if (grown == NULL) {
            free(first);
            free(second);
            set_err(ctx, "out of memory");
            goto out;
        }
        owned = (char **)grown;
        owned[owned_count++] = first;
        if (second != NULL) {
            owned[owned_count++] = second;
        }
        grown = realloc(files, (file_count + 1) * sizeof(files[0]));
        if (grown == NULL) {
            set_err(ctx, "out of memory");
            goto out;
        }
        files = (as_dwline_file_t *)grown;
        files[file_count].number = (unsigned)number;
        files[file_count].dir = second != NULL ? first : NULL;
        files[file_count].name = second != NULL ? second : first;
        file_count++;
    }
    if (ctx->line_own) {
        /* -g, and a source that says nothing of lines: the one file is
         * the source. */
        free(files);
        files = (as_dwline_file_t *)malloc(sizeof(files[0]));
        if (files == NULL) {
            file_count = 0;
            set_err(ctx, "out of memory");
            goto out;
        }
        files[0].number = 1;
        files[0].dir = NULL;
        files[0].name = ctx->cfg->source_name != NULL ? ctx->cfg->source_name : "<input>";
        file_count = 1;
    }
    ctx->line_rows_valid = 0;
    if (line_rows(ctx) != 0) {
        goto out;
    }
    if (ctx->line_row_count == 0) {
        rc = 0;
        goto out;
    }
    rows = (as_dwline_row_t *)malloc(ctx->line_row_count * sizeof(rows[0]));
    ends = (as_dwline_end_t *)malloc(ctx->line_row_count * sizeof(ends[0]));
    if (rows == NULL || ends == NULL) {
        set_err(ctx, "out of memory");
        goto out;
    }
    for (i = 0; i < ctx->line_row_count; ++i) {
        rows[i] = ctx->line_rows[i].row;
        for (k = 0; k < end_count; ++k) {
            if (as_strcmp(ends[k].section, rows[i].section) == 0) {
                break;
            }
        }
        if (k == end_count) {
            elf_section_t *code = elf_find_section(ctx->obj, rows[i].section);

            ends[end_count].section = rows[i].section;
            ends[end_count].size = code != NULL ? elf_section_size(code) : rows[i].offset;
            end_count++;
        }
    }
    if (as_dwline_build(files, file_count, rows, ctx->line_row_count, ends, end_count, ctx->cfg->is_64 != 0, &table,
                        why, sizeof(why)) != 0) {
        set_err(ctx, "%s", why);
        goto out;
    }
    if (ensure_section_exists(ctx, ".debug_line", SHT_PROGBITS, 0, 1, NULL, 0) != 0 ||
        (sec = elf_find_section(ctx->obj, ".debug_line")) == NULL) {
        set_err(ctx, "failed to emit .debug_line");
        goto out;
    }
    if (elf_section_size(sec) != 0) {
        set_err(ctx, "the source has .loc, and writes .debug_line itself as well");
        goto out;
    }
    for (i = 0; i < table.fixup_count; ++i) {
        const as_dwline_fixup_t *f = &table.fixups[i];
        uint32_t type = machine == EM_X86_64 ? (f->width == 8 ? R_X86_64_64 : R_X86_64_32) : R_386_32;

        if (machine_relocation_addend_is_in_place(machine)) {
            write_u64_le_at(table.bytes + f->offset, (uint64_t)f->addend, f->width);
        }
        if (add_reloc_for_symbol_ex(ctx, sec, f->section, f->offset, type, f->addend) != 0) {
            set_err(ctx, ".debug_line: no relocation could be made against '%s'", f->section);
            goto out;
        }
    }
    if (elf_section_set_data(sec, table.bytes, table.len) != ELF_OK) {
        set_err(ctx, "failed to emit .debug_line");
        goto out;
    }
    if (ctx->line_own && emit_own_compile_unit(ctx, files[0].name, ends, end_count) != 0) {
        goto out;
    }
    rc = 0;
out:
    as_dwline_table_free(&table);
    for (i = 0; i < owned_count; ++i) {
        free(owned[i]);
    }
    free(owned);
    free(files);
    free(rows);
    free(ends);
    return rc;
}

/* The call frame information of the source: every `.cfi_*` directive
 * with the place it stands at, and the sections made of them. */
static int emit_call_frames(emit_ctx_t *ctx, const as_parse_result_t *parsed) {
    as_cfi_event_t *events = NULL;
    size_t count = 0;
    size_t cap = 0;
    int eh_frame = 0;
    int debug_frame = 0;
    uint64_t *offsets = NULL;
    size_t i;
    int rc = -1;

    if (layout_offsets(ctx, &offsets) != 0) {
        set_err(ctx, "out of memory");
        return -1;
    }
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        const char *section = NULL;
        uint64_t off = 0;

        if (st->kind != AS_STMT_DIRECTIVE || st->u.directive.name == NULL ||
            strncmp(st->u.directive.name, ".cfi_", 5) != 0) {
            continue;
        }
        if (as_strcmp(st->u.directive.name, ".cfi_sections") != 0) {
            if (i >= ctx->stmt_section_at_count || ctx->stmt_section_at[i] == NULL) {
                set_err(ctx, "%s:%u: %s: where it stands could not be found",
                        st->file != NULL ? st->file : "<input>", st->line, st->u.directive.name);
                goto out;
            }
            section = ctx->stmt_section_at[i];
            off = offsets[i];
        }
        if (count == cap) {
            size_t ncap = cap != 0 ? cap * 2 : 64;
            as_cfi_event_t *grown = (as_cfi_event_t *)realloc(events, ncap * sizeof(events[0]));

            if (grown == NULL) {
                set_err(ctx, "out of memory");
                goto out;
            }
            events = grown;
            cap = ncap;
        }
        events[count].d = &st->u.directive;
        events[count].section = section != NULL ? section : "";
        events[count].offset = off;
        events[count].file = st->file;
        events[count].line = st->line;
        count++;
    }
    as_cfi_sections(events, count, &eh_frame, &debug_frame);
    if ((eh_frame && emit_frame_section(ctx, events, count, 0) != 0) ||
        (debug_frame && emit_frame_section(ctx, events, count, 1) != 0)) {
        goto out;
    }
    rc = 0;
out:
    free(events);
    free(offsets);
    return rc;
}

/* The text of a directive's argument that is a string in quotes: its
 * bytes, with the escapes worked out.  NULL if it is not one. */
static char *directive_string(const as_directive_t *d, size_t i, size_t *len_out) {
    const char *arg = i < d->arg_count ? d->args[i] : NULL;
    char *bytes = NULL;

    if (arg == NULL) {
        return NULL;
    }
    if (as_decode_string_literal(arg, &bytes, len_out) != 0) {
        return NULL;
    }
    return bytes;
}

/*
 * `.file "name"` and `.ident "text"`.
 *
 * Each `.file` with a name and no number is a symbol of type FILE, the
 * first in the symbol table, by which a linker's map and a debugger say
 * which source a local symbol is of.  Each `.ident` is a string in
 * .comment, behind the empty one the section begins with.  Both were
 * taken and nothing made of them.  This is to be called before any
 * other symbol is made.
 */
static int emit_file_symbols_and_comment(emit_ctx_t *ctx) {
    bytebuf_t comment;
    size_t i;
    int rc = -1;

    memset(&comment, 0, sizeof(comment));
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        const as_directive_t *d = &st->u.directive;
        char *text;
        size_t len = 0;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL) {
            continue;
        }
        if (as_strcmp(d->name, ".file") == 0 && d->arg_count == 1) {
            const char *arg = d->args[0] != NULL ? d->args[0] : "";

            /* (With a number before the name it is a file of the line
             * table, and no symbol.) */
            while (isspace((unsigned char)*arg)) {
                arg++;
            }
            if (isdigit((unsigned char)*arg)) {
                continue;
            }
            text = directive_string(d, 0, &len);
            if (text == NULL) {
                continue;
            }
            /* (A name is what is before the first NUL of it.) */
            {
                elf_symbol_t *sym = elf_add_symbol(ctx->obj, text, 0, 0, STB_LOCAL, STT_FILE);

                free(text);
                if (sym == NULL || elf_symbol_set_shndx(sym, SHN_ABS) != ELF_OK) {
                    goto out;
                }
            }
        } else if (as_strcmp(d->name, ".ident") == 0) {
            text = directive_string(d, 0, &len);
            if (text == NULL) {
                set_err(ctx, "%s:%u: .ident: a string in quotes is wanted", st->file != NULL ? st->file : "<input>",
                        st->line);
                goto out;
            }
            if ((comment.len == 0 && bytebuf_append_zeros(&comment, 1) != 0) ||
                bytebuf_append(&comment, text, len) != 0 || bytebuf_append_zeros(&comment, 1) != 0) {
                free(text);
                goto out;
            }
            free(text);
        }
    }
    if (comment.len != 0) {
        elf_section_t *sec = elf_find_section(ctx->obj, ".comment");

        if (sec == NULL) {
            sec = elf_add_section(ctx->obj, ".comment", SHT_PROGBITS, 0);
        }
        if (sec == NULL || elf_section_set_align(sec, 1) != ELF_OK || elf_section_set_merge(sec, 1, 1) != ELF_OK ||
            elf_section_set_data(sec, comment.data, comment.len) != ELF_OK) {
            goto out;
        }
    }
    rc = 0;
out:
    free(comment.data);
    return rc;
}

typedef struct {
    char *name;
    elf_section_t *sec;
    uint64_t off;
} sym_loc_t;

typedef struct {
    size_t *slots;
    size_t cap;
} name_index_t;

typedef struct {
    const char *file;
    unsigned line;
    elf_section_t *sec;
    uint64_t off;
} dot_loc_t;

static void free_dot_locs(dot_loc_t *locs) {
    free(locs);
}

static const dot_loc_t *find_dot_loc(const dot_loc_t *locs, size_t count, const char *file, unsigned line) {
    size_t i;

    if (locs == NULL || file == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        if (locs[i].file != NULL && locs[i].line == line && as_strcmp(locs[i].file, file) == 0) {
            return &locs[i];
        }
    }
    return NULL;
}

static void free_sym_locs(sym_loc_t *locs, size_t count) {
    size_t i;

    if (locs == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        free(locs[i].name);
    }
    free(locs);
}

static void free_name_index(name_index_t *idx) {
    if (idx == NULL) {
        return;
    }
    free(idx->slots);
    idx->slots = NULL;
    idx->cap = 0;
}

static int ensure_name_index(name_index_t *idx, const sym_loc_t *locs, size_t count) {
    size_t cap;
    size_t *slots;
    size_t i;

    if (idx == NULL) {
        return -1;
    }
    if (idx->cap != 0 && (count + 1) * 2 < idx->cap) {
        return 0;
    }
    cap = 64;
    while (cap <= (count + 1) * 2) {
        cap <<= 1;
    }
    slots = (size_t *)calloc(cap, sizeof(*slots));
    if (slots == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        size_t pos = hash_name(locs[i].name) & (cap - 1);
        while (slots[pos] != 0) {
            pos = (pos + 1) & (cap - 1);
        }
        slots[pos] = i + 1;
    }
    free(idx->slots);
    idx->slots = slots;
    idx->cap = cap;
    return 0;
}

static int numeric_local_label_number(const char *name, int *out) {
    size_t i;
    long value = 0;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    for (i = 0; name[i] != '\0'; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return -1;
        }
        value = value * 10 + (long)(name[i] - '0');
        if (value > INT_MAX) {
            return -1;
        }
    }
    if (out != NULL) {
        *out = (int)value;
    }
    return 0;
}

static int is_numeric_local_label_name(const char *name) {
    return numeric_local_label_number(name, NULL) == 0;
}

static const sym_loc_t *find_sym_loc(const sym_loc_t *locs, const name_index_t *idx, const char *name) {
    size_t pos;

    if (locs == NULL || idx == NULL || idx->slots == NULL || idx->cap == 0 || name == NULL) {
        return NULL;
    }
    pos = hash_name(name) & (idx->cap - 1);
    while (idx->slots[pos] != 0) {
        size_t hit = idx->slots[pos] - 1;
        if (as_strcmp(locs[hit].name, name) == 0) {
            return &locs[hit];
        }
        pos = (pos + 1) & (idx->cap - 1);
    }
    return NULL;
}

static int upsert_sym_loc(sym_loc_t **locs, size_t *count, size_t *cap, name_index_t *idx,
                          const char *name, elf_section_t *sec, uint64_t off) {
    sym_loc_t *next;
    size_t pos;

    if (locs == NULL || count == NULL || cap == NULL || idx == NULL || name == NULL) {
        return -1;
    }
    if (ensure_name_index(idx, *locs, *count) != 0) {
        return -1;
    }
    pos = hash_name(name) & (idx->cap - 1);
    while (idx->slots[pos] != 0) {
        size_t hit = idx->slots[pos] - 1;
        if (as_strcmp((*locs)[hit].name, name) == 0) {
            (*locs)[hit].sec = sec;
            (*locs)[hit].off = off;
            return 0;
        }
        pos = (pos + 1) & (idx->cap - 1);
    }

    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 16 : *cap * 2;
        next = (sym_loc_t *)realloc(*locs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *locs = next;
        *cap = ncap;
    }

    (*locs)[*count].name = xstrdup(name);
    if ((*locs)[*count].name == NULL) {
        return -1;
    }
    (*locs)[*count].sec = sec;
    (*locs)[*count].off = off;
    idx->slots[pos] = *count + 1;
    (*count)++;
    return 0;
}

static int collect_symbol_locations(emit_ctx_t *ctx, sym_loc_t **locs, size_t *loc_count, size_t *loc_cap,
                                    name_index_t *loc_index) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || locs == NULL || loc_count == NULL || loc_cap == NULL || loc_index == NULL) {
        return -1;
    }
    *locs = NULL;
    *loc_count = 0;
    *loc_cap = 0;
    loc_index->slots = NULL;
    loc_index->cap = 0;
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        uint64_t cur_off;
        elf_section_t *cur_sec;
        sec_buf_t *sb;
        size_t j;
        int trc;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            free_sym_locs(*locs, *loc_count);
            *locs = NULL;
            *loc_count = 0;
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        cur_off = (uint64_t)sb->buf.len;
        cur_sec = section_for_name(ctx, track.current);

        for (j = 0; j < st->label_count; ++j) {
            if (is_numeric_local_label_name(st->labels[j].name)) {
                continue;
            }
            if (upsert_sym_loc(locs, loc_count, loc_cap, loc_index, st->labels[j].name, cur_sec, cur_off) != 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
                *loc_cap = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            int drc;

            trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return -1;
}
            if (trc > 0) {
                continue;
            }
            drc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                            track.x86_code_bits, &st->u.directive);
            if (drc < 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                set_err(ctx, "%s:%u: malformed directive data", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            continue;
        }

        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            unsigned char code[32];
            size_t code_len = 0;
            char encerr[256];
            if (encode_x86_stmt_for_layout(ctx, track.current, cur_off, track.x86_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                set_err(ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, encerr);
                return -1;
            }
            if (bytebuf_append(&sb->buf, code, code_len) != 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return 0;
}

static int collect_dot_locations(emit_ctx_t *ctx, dot_loc_t **out, size_t *count_out) {
    size_t i;
    size_t count = 0;
    size_t cap = 0;
    dot_loc_t *items = NULL;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || out == NULL || count_out == NULL) {
        return -1;
    }
    *out = NULL;
    *count_out = 0;
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb = sec_buf_get_or_add(&secbufs, track.current);
        uint64_t cur_off;

        if (sb == NULL) {
            free(items);
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        cur_off = (uint64_t)sb->buf.len;
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc;
            int drc;

            if (count == cap) {
                size_t ncap = cap == 0 ? 128 : cap * 2;
                dot_loc_t *next = (dot_loc_t *)realloc(items, ncap * sizeof(*next));
                if (next == NULL) {
                    free(items);
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                items = next;
                cap = ncap;
            }
            items[count].file = st->file;
            items[count].line = st->line;
            items[count].sec = section_for_name(ctx, track.current);
            items[count].off = cur_off;
            count++;

            trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                free(items);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            drc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                            track.x86_code_bits, &st->u.directive);
            if (drc < 0) {
                free(items);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            unsigned char code[32];
            size_t code_len = 0;
            char encerr[256];
            if (encode_x86_stmt_for_layout(ctx, track.current, cur_off, track.x86_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0 ||
                bytebuf_append(&sb->buf, code, code_len) != 0) {
                free(items);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    *out = items;
    *count_out = count;
    return 0;
}

static int emit_symbols(emit_ctx_t *ctx, const as_symtab_t *symtab) {
    size_t i;
    sym_loc_t *locs = NULL;
    size_t loc_count = 0;
    size_t loc_cap = 0;
    dot_loc_t *dot_locs = NULL;
    size_t dot_count = 0;
    name_index_t loc_index;
    int trace_emit_phase = getenv("AS_DEBUG_EMIT_PHASE") != NULL;

    memset(&loc_index, 0, sizeof(loc_index));
    if (collect_symbol_locations(ctx, &locs, &loc_count, &loc_cap, &loc_index) != 0) {
        return -1;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase symbol locations done count=%zu\n", loc_count);
    if (collect_dot_locations(ctx, &dot_locs, &dot_count) != 0) {
        free_sym_locs(locs, loc_count);
        free_dot_locs(dot_locs);
        free_name_index(&loc_index);
        return -1;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase dot locations done count=%zu\n", dot_count);

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;
        unsigned long long sym_size = s->size;

        if (symbol_is_not_emitted(ctx, symtab, s)) {
            continue;
        }
        if (s->size_target_symbol != NULL) {
            const sym_loc_t *target_loc = find_sym_loc(locs, &loc_index, s->size_target_symbol);
            elf_section_t *base_sec = NULL;
            uint64_t base_off = 0;

            if (target_loc == NULL || target_loc->sec == NULL) {
                set_err(ctx, "cannot resolve size target for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            if (s->size_base_from_dot) {
                const dot_loc_t *dot = find_dot_loc(dot_locs, dot_count, s->size_anchor_file, s->size_anchor_line);
                if (dot == NULL || dot->sec == NULL) {
                    set_err(ctx, "cannot resolve symbolic size for %s", s->name);
                    free_sym_locs(locs, loc_count);
                    free_dot_locs(dot_locs);
                    free_name_index(&loc_index);
                    return -1;
                }
                base_sec = dot->sec;
                base_off = dot->off;
            } else {
                const as_symbol_t *base_sym = find_as_symbol_const(ctx, symtab, s->size_base_symbol);
                const sym_loc_t *base_loc = NULL;

                if (base_sym != NULL && base_sym->alias_from_dot && base_sym->alias_target != NULL &&
                    as_strcmp(base_sym->alias_target, s->size_target_symbol) == 0) {
                    const dot_loc_t *dot = find_dot_loc(dot_locs, dot_count, base_sym->def_file, base_sym->def_line);
                    if (dot == NULL || dot->sec == NULL || dot->sec != target_loc->sec ||
                        dot->off < target_loc->off) {
                        set_err(ctx, "cannot resolve symbolic size alias for %s", s->name);
                        free_sym_locs(locs, loc_count);
                        free_dot_locs(dot_locs);
                        free_name_index(&loc_index);
                        return -1;
                    }
                    sym_size = (unsigned long long)((int64_t)dot->off - (int64_t)target_loc->off +
                                                    base_sym->alias_addend);
                    base_sec = target_loc->sec;
                    base_off = target_loc->off + sym_size;
                } else {
                    base_loc = find_sym_loc(locs, &loc_index, s->size_base_symbol);
                    if (base_loc == NULL || base_loc->sec == NULL) {
                        set_err(ctx, "cannot resolve size base for %s", s->name);
                        free_sym_locs(locs, loc_count);
                        free_dot_locs(dot_locs);
                        free_name_index(&loc_index);
                        return -1;
                    }
                    base_sec = base_loc->sec;
                    base_off = base_loc->off;
                }
            }
            if (base_sec != target_loc->sec) {
                set_err(ctx, "%s:%u: the size of '%s' is the distance between two labels that are not of one section",
                        s->def_file != NULL ? s->def_file : "<input>", s->def_line, s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            /* (A distance that is backwards is written as it comes, as
             * GNU as writes it; it was refused.) */
            sym_size = (unsigned long long)((long long)base_off - (long long)target_loc->off + s->size_addend);
        }

        {
            char *versioned = symver_sole_name(s);

            esym = elf_add_symbol(ctx->obj, versioned != NULL ? versioned : s->name, 0, sym_size, map_bind(s->bind),
                                  map_type(s->type));
            free(versioned);
        }
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        /* (An indirect function and a unique object are GNU's, and an
         * object that has one says it is of that ABI, or a reader does
         * not know what the numbers mean.) */
        if ((s->type == AS_SYM_TYPE_UNIQUE_OBJECT || s->type == AS_SYM_TYPE_IFUNC) &&
            elf_set_osabi(ctx->obj, ELFOSABI_LINUX) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (elf_symbol_set_visibility(esym, map_vis(s->visibility)) != ELF_OK ||
            (s->type == AS_SYM_TYPE_UNIQUE_OBJECT && elf_symbol_set_binding(esym, STB_GNU_UNIQUE) != ELF_OK)) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (append_emit_symbol(ctx, s->name, esym) != 0) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase primary symbols done count=%zu\n", symtab->count);

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;
        const sym_loc_t *loc;

        if (symbol_is_not_emitted(ctx, symtab, s)) {
            continue;
        }
        esym = ensure_emit_symbol(ctx, s->name);
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (s->is_common) {
            if (elf_symbol_set_shndx(esym, SHN_COMMON) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            /*
             * A common symbol's value is its alignment and its size is
             * its size: the size was put for the value and nothing for
             * the size, so the linker gave `.comm buf,64,32` no room
             * at an alignment of 64.  With no alignment written it is
             * the size taken up to a power of two, and 16 at the most,
             * as GNU as has it.
             */
            {
                unsigned long long align = s->common_align;

                if (align == 0) {
                    for (align = 1; align < s->common_size && align < 16; align <<= 1) {
                    }
                }
                if (elf_symbol_set_value(esym, align) != ELF_OK ||
                    elf_symbol_set_size(esym, s->common_size) != ELF_OK) {
                    free_sym_locs(locs, loc_count);
                    free_dot_locs(dot_locs);
                    free_name_index(&loc_index);
                    return -1;
                }
            }
        } else if (s->is_absolute) {
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK ||
                elf_symbol_set_value(esym, s->absolute_value) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
        } else if (s->defined) {
            if (s->alias_target != NULL) {
                continue;
            }
            loc = find_sym_loc(locs, &loc_index, s->name);
            if (loc != NULL && loc->sec != NULL) {
                uint64_t thumb_bit = 0;
                size_t tf;

                for (tf = 0; tf < ctx->cfg->thumb_func_count; ++tf) {
                    if (as_strcmp(ctx->cfg->thumb_funcs[tf], s->name) == 0) {
                        thumb_bit = 1;
                        break;
                    }
                }
                if ((thumb_bit != 0 && elf_symbol_set_type(esym, STT_FUNC) != ELF_OK) ||
                    elf_symbol_define(esym, loc->sec, loc->off | thumb_bit) != ELF_OK) {
                    free_sym_locs(locs, loc_count);
                    free_dot_locs(dot_locs);
                    free_name_index(&loc_index);
                    return -1;
                }
                /*
                 * What is defined in a thread-local section is a
                 * thread-local symbol, whatever .type called it: a
                 * compiler writes `.type x, @object` for a __thread
                 * variable.  Left an object, the linker took its value
                 * for an address and not an offset in the thread's
                 * block, and the program read another variable.
                 */
                if ((elf_section_flags(loc->sec) & SHF_TLS) != 0 && elf_symbol_set_type(esym, STT_TLS) != ELF_OK) {
                    free_sym_locs(locs, loc_count);
                    free_dot_locs(dot_locs);
                    free_name_index(&loc_index);
                    return -1;
                }
            } else {
                /* Defined, and nowhere: it was put at the start of
                 * .text, where it was not.  There is no such symbol
                 * that this assembler should make. */
                set_err(ctx, "%s:%u: internal error: the symbol '%s' is defined and has no place",
                        s->def_file != NULL ? s->def_file : "<input>", s->def_line, s->name != NULL ? s->name : "");
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
        }
    }

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;
        elf_symbol_t *target;
        uint16_t shndx;
        int64_t v;

        if (symbol_is_not_emitted(ctx, symtab, s)) {
            continue;
        }
        if (s->alias_target == NULL) {
            continue;
        }
        esym = ensure_emit_symbol(ctx, s->name);
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (s->alias_minus != NULL) {
            /* The distance between two labels: a number, if they are
             * of one section. */
            const sym_loc_t *to = find_sym_loc(locs, &loc_index, s->alias_target);
            const sym_loc_t *from = find_sym_loc(locs, &loc_index, s->alias_minus);

            if (to == NULL || from == NULL || to->sec == NULL || to->sec != from->sec) {
                set_err(ctx, "%s:%u: '%s' is set to '%s' less '%s', which are not two labels of one section",
                        s->def_file != NULL ? s->def_file : "<input>", s->def_line, s->name, s->alias_target,
                        s->alias_minus);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            v = (int64_t)to->off - (int64_t)from->off + s->alias_addend;
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK || elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            continue;
        }
        if (s->alias_from_dot) {
            const sym_loc_t *target_loc = find_sym_loc(locs, &loc_index, s->alias_target);
            elf_section_t *dot_sec = NULL;
            uint64_t dot_off = 0;

            if (target_loc == NULL || target_loc->sec == NULL) {
                set_err(ctx, "cannot resolve .-expression target for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            {
                const dot_loc_t *dot = find_dot_loc(dot_locs, dot_count, s->def_file, s->def_line);
                if (dot != NULL) {
                    dot_sec = dot->sec;
                    dot_off = dot->off;
                }
            }
            if (dot_sec == NULL) {
                set_err(ctx, "cannot resolve current location for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            if (dot_sec != target_loc->sec) {
                set_err(ctx, "cross-section .-symbol assignment is not supported for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            v = (int64_t)dot_off - (int64_t)target_loc->off + s->alias_addend;
            if (v < 0) {
                set_err(ctx, "negative absolute value for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK || elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            continue;
        }
        /* A name for a place that has no symbol of its own -- a
         * compiler's label, or the `.` of a `.set` -- is at the place. */
        if (is_local_temp_symbol_name(s->alias_target)) {
            const sym_loc_t *at = find_sym_loc(locs, &loc_index, s->alias_target);

            if (at == NULL || at->sec == NULL ||
                elf_symbol_define(esym, at->sec, (uint64_t)((int64_t)at->off + s->alias_addend)) != ELF_OK) {
                set_err(ctx, "%s:%u: '%s' is set to '%s', which is no place in this source",
                        s->def_file != NULL ? s->def_file : "<input>", s->def_line, s->name, s->alias_target);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            continue;
        }
        target = ensure_emit_symbol(ctx, s->alias_target);
        if (target == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        shndx = elf_symbol_shndx(target);
        if (elf_symbol_set_shndx(esym, shndx) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        v = (int64_t)elf_symbol_value(target) + s->alias_addend;
        if (elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        /* Another name for a function or an object is one, and as
         * large, unless it was said to be something else. */
        if (elf_symbol_type(esym) == STT_NOTYPE &&
            (elf_symbol_set_type(esym, elf_symbol_type(target)) != ELF_OK ||
             (elf_symbol_size(esym) == 0 && s->alias_addend == 0 &&
              elf_symbol_set_size(esym, elf_symbol_size(target)) != ELF_OK))) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
    }

    free_sym_locs(locs, loc_count);
    free_dot_locs(dot_locs);
    free_name_index(&loc_index);
    return emit_symbol_versions(ctx, symtab);
}

/*
 * What add_reloc_for_symbol_ex() below will add to the addend of a
 * relocation against `name`: a local label is rewritten to its section's
 * symbol plus the label's offset in the section, and that offset is part
 * of the addend.
 *
 * Where the addend is kept in the relocation record that is the end of it.
 * Where it is kept in the bytes being relocated (i386: SHT_REL) it has to
 * be in what is written there, and it was not -- the bytes were written
 * first, with the addend as it stood, and the offset then went into a
 * record field nothing reads.  Every reference to a local label came out
 * as a reference to the start of its section: a program's first string
 * constant was whatever its .rodata began with.
 */
static int64_t local_label_addend(emit_ctx_t *ctx, const char *name) {
    const char *target_section = "";
    uint64_t target_off = 0;
    char *sym_name = xstrdup(name);
    int64_t off = 0;

    if (sym_name == NULL) {
        return 0;
    }
    if (sym_name[0] != '\0' && is_local_temp_symbol_name(sym_name) &&
        find_label_virtual_location(ctx, NULL, NULL, 0, 0, sym_name,
                                    &target_section,
                                    &target_off) == 0) {
        elf_section_t *target_sec = section_for_name(ctx, target_section);

        /*
         * This is what is written into the field where the addend lives
         * there (i386): the label's offset, for a relocation that is
         * against the label's section.  A label in a merged section keeps
         * its own symbol (add_reloc_for_symbol_ex), which already stands
         * for that offset; added here as well it was counted twice, and
         * each string a program named was the one that far beyond it.
         */
        if (target_sec == NULL || (elf_section_flags(target_sec) & SHF_MERGE) == 0) {
            off = (int64_t)target_off;
        }
    }
    free(sym_name);
    return off;
}

static int add_reloc_for_symbol_ex(emit_ctx_t *ctx, elf_section_t *sec, const char *name,
                                   uint64_t offset, uint32_t fallback_type, int64_t addend) {
    return add_reloc_for_symbol_op(ctx, sec, name, NULL, offset, fallback_type, addend);
}

/* The same, for a symbol with a relocation operator written on it --
 * `reloc_op` is the GOTOFF of sym@GOTOFF, or NULL -- which then names
 * the relocation. */
static int add_reloc_for_symbol_op(emit_ctx_t *ctx, elf_section_t *sec, const char *name, const char *reloc_op,
                                   uint64_t offset, uint32_t fallback_type, int64_t addend) {
    char *sym_name;
    elf_symbol_t *sym;
    uint32_t rtype;

    sym_name = xstrdup(name);
    if (sym_name == NULL) {
        return -1;
    }
    if (sym_name[0] == '\0') {
        free(sym_name);
        return -1;
    }
    if (is_local_temp_symbol_name(sym_name)) {
        const char *target_section = "";
        uint64_t target_off = 0;

        if (find_label_virtual_location(ctx, NULL, NULL, 0, 0, sym_name,
                                        &target_section, &target_off) == 0) {
            elf_section_t *target_sec = section_for_name(ctx, target_section);
            char *section_name;

            /*
             * A label in a section the linker merges -- string constants
             * are kept so -- stays a symbol.  The linker moves each string
             * of such a section where it will; a relocation against the
             * section with an offset, which is what a .L label otherwise
             * becomes, means "this far into the section" and is taken
             * for a place in whichever string that falls in.  With the
             * -4 of a PC-relative field the first string's fell before
             * the section altogether: ld warned of an access beyond the
             * end of a merged section and the program printed the wrong
             * strings.
             */
            if (target_sec != NULL &&
                ((elf_section_flags(target_sec) & SHF_MERGE) != 0 ||
                 (reloc_type_exact && ctx->cfg != NULL && ctx->cfg->target == EM_ARM))) {
                sym = find_emit_symbol(ctx, sym_name);
                if (sym == NULL) {
                    sym = elf_add_symbol(ctx->obj, sym_name, 0, 0, STB_LOCAL, STT_NOTYPE);
                    if (sym == NULL || elf_symbol_define(sym, target_sec, target_off) != ELF_OK ||
                        append_emit_symbol(ctx, sym_name, sym) != 0) {
                        free(sym_name);
                        return -1;
                    }
                }
                if (sec == NULL) {
                    free(sym_name);
                    return 0;
                }
                rtype = reloc_type_exact ? fallback_type
                                         : reloc_type_for_symbol(ctx->cfg != NULL ? ctx->cfg->machine : EM_386, name,
                                                                 reloc_op, fallback_type);
                if (rtype != AS_RELOC_TYPE_BAD && !reloc_type_exact && ctx->cfg != NULL && ctx->cfg->target != 0) {
                    rtype = reloc_type_for_target(ctx->cfg->target, rtype);
                }
                if (rtype == AS_RELOC_TYPE_BAD || elf_add_relocation(sec, offset, sym, rtype, addend) != ELF_OK) {
                    free(sym_name);
                    return -1;
                }
                free(sym_name);
                return 0;
            }
            section_name = xstrdup(target_section);
            if (section_name == NULL) {
                free(sym_name);
                return -1;
            }
            free(sym_name);
            sym_name = section_name;
            addend += (int64_t)target_off;
        }
    }
    sym = ensure_emit_symbol(ctx, sym_name);
    if (sym == NULL) {
        free(sym_name);
        return -1;
    }
    if (sec == NULL) {
        free(sym_name);
        return 0;
    }
    rtype = reloc_type_exact ? fallback_type
                             : reloc_type_for_symbol(ctx->cfg != NULL ? ctx->cfg->machine : EM_386, name, reloc_op,
                                                     fallback_type);
    if (rtype == AS_RELOC_TYPE_BAD) {
        set_err(ctx, "'%s@%s': what follows the @ is not a relocation operator this target has", name,
                reloc_op != NULL ? reloc_op : "");
        free(sym_name);
        return -1;
    }
    if (!reloc_type_exact && ctx->cfg != NULL && ctx->cfg->target != 0) {
        rtype = reloc_type_for_target(ctx->cfg->target, rtype);
        if (rtype == AS_RELOC_TYPE_BAD) {
            set_err(ctx, "'%s': a relocation this target has none for", name);
            free(sym_name);
            return -1;
        }
    }
    /* A symbol reached by a thread-local relocation is a thread-local
     * symbol, and one that another file defines is known to be one by
     * this alone. */
    if (reloc_type_is_tls(ctx->cfg != NULL ? ctx->cfg->machine : EM_386, rtype) &&
        elf_symbol_type(sym) == STT_NOTYPE) {
        (void)elf_symbol_set_type(sym, STT_TLS);
    }
    if (elf_add_relocation(sec, offset, sym, rtype, addend) != ELF_OK) {
        free(sym_name);
        return -1;
    }
    free(sym_name);
    return 0;
}

/* The relocation operator written on that symbol -- the first, as
 * first_symbol_in_expr() finds it -- or NULL. */
const char *first_symbol_op_in_expr(const as_expr_t *e) {
    if (e == NULL) {
        return NULL;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL) {
        return e->reloc_op;
    }
    if (e->lhs != NULL && first_symbol_in_expr(e->lhs) != NULL) {
        return first_symbol_op_in_expr(e->lhs);
    }
    return first_symbol_op_in_expr(e->rhs);
}

static const char *first_symbol_in_expr(const as_expr_t *e) {
    if (e == NULL) {
        return NULL;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL) {
        return e->symbol;
    }
    if (e->lhs != NULL) {
        const char *s = first_symbol_in_expr(e->lhs);
        if (s != NULL) {
            return s;
        }
    }
    if (e->rhs != NULL) {
        return first_symbol_in_expr(e->rhs);
    }
    return NULL;
}

char *trim_copy_arg(const char *s) {
    const char *p;
    const char *q;
    size_t n;
    char *out;

    if (s == NULL) {
        return NULL;
    }
    p = s;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    q = p + strlen(p);
    while (q > p && isspace((unsigned char)q[-1])) {
        --q;
    }
    n = (size_t)(q - p);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return out;
}

static int parse_symbol_addend_op_arg(const char *arg, char **sym_out, char **op_out, int64_t *add_out);

int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out) {
    return parse_symbol_addend_op_arg(arg, sym_out, NULL, add_out);
}

/* The same, and where op_out is not NULL the relocation operator
 * written on the symbol, or NULL for none: the caller frees it. */
static int parse_symbol_addend_op_arg(const char *arg, char **sym_out, char **op_out, int64_t *add_out) {
    as_expr_t *e;
    as_expr_linear_t lin;
    int rc = -1;

    /*
     * A data directive's argument as as_expr.c reduces it.  One symbol
     * and a number give the symbol's name and the addend; a number alone
     * gives no name, and the caller has the number another way; a
     * difference of symbols is not this function's.
     */
    if (sym_out == NULL || add_out == NULL) {
        return -1;
    }
    *sym_out = NULL;
    *add_out = 0;
    if (op_out != NULL) {
        *op_out = NULL;
    }
    e = as_parse_expr_string(arg, NULL, 0);
    if (e == NULL) {
        return -1;
    }
    if (as_expr_eval_linear(e, NULL, NULL, &lin) == AS_EXPR_EVAL_OK &&
        lin.sub_symbol == NULL && lin.sub_local == NULL) {
        if (lin.add_symbol != NULL) {
            const char *rop = as_expr_reloc_op(e);

            if (op_out != NULL && rop != NULL) {
                *op_out = xstrdup(rop);
                if (*op_out == NULL) {
                    as_expr_free(e);
                    return -1;
                }
            }
            *sym_out = xstrdup(lin.add_symbol);
            rc = *sym_out != NULL ? 0 : -1;
        } else if (lin.add_local != NULL) {
            /* 1f and 1b by those names, which is how they are looked up. */
            char name[16];

            snprintf(name, sizeof(name), "%d%c", lin.add_local->local_digit,
                     lin.add_local->local_forward ? 'f' : 'b');
            *sym_out = xstrdup(name);
            rc = *sym_out != NULL ? 0 : -1;
        } else {
            rc = 0;
        }
        if (rc == 0 && *sym_out != NULL) {
            *add_out = (int64_t)lin.value;
        }
    }
    as_expr_free(e);
    return rc;
}

static char *dirname_dup2(const char *path) {
    const char *slash;
    size_t n;
    char *out;

    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return xstrdup(".");
    }
    if (slash == path) {
        return xstrdup("/");
    }
    n = (size_t)(slash - path);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *join_path2(const char *a, const char *b) {
    size_t an;
    size_t bn;
    char *out;

    if (a == NULL || b == NULL) {
        return NULL;
    }
    an = strlen(a);
    bn = strlen(b);
    out = (char *)malloc(an + 1 + bn + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, a, an);
    out[an] = '/';
    memcpy(out + an + 1, b, bn);
    out[an + 1 + bn] = '\0';
    return out;
}

static char *decode_path_arg(const char *arg) {
    char *bytes = NULL;
    size_t len = 0;
    char *out;

    if (arg == NULL) {
        return NULL;
    }
    if (arg[0] != '"') {
        return xstrdup(arg);
    }
    if (as_decode_string_literal(arg, &bytes, &len) != 0) {
        return NULL;
    }
    if (memchr(bytes, '\0', len) != NULL) {
        free(bytes);
        return NULL;
    }
    out = (char *)malloc(len + 1);
    if (out == NULL) {
        free(bytes);
        return NULL;
    }
    memcpy(out, bytes, len);
    out[len] = '\0';
    free(bytes);
    return out;
}

static FILE *open_incbin_file(const as_stmt_t *st, const char *path, char **opened_path) {
    FILE *fp;

    if (opened_path != NULL) {
        *opened_path = NULL;
    }
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    fp = fopen(path, "rb");
    if (fp != NULL) {
        if (opened_path != NULL) {
            *opened_path = xstrdup(path);
        }
        return fp;
    }
    if (path[0] != '/' && st != NULL && st->file != NULL) {
        char *dir = dirname_dup2(st->file);
        char *joined = NULL;
        if (dir != NULL) {
            joined = join_path2(dir, path);
        }
        free(dir);
        if (joined != NULL) {
            fp = fopen(joined, "rb");
            if (fp != NULL) {
                if (opened_path != NULL) {
                    *opened_path = joined;
                } else {
                    free(joined);
                }
                return fp;
            }
            free(joined);
        }
    }
    return NULL;
}

int append_incbin_to_bytebuf(emit_ctx_t *ctx, const as_stmt_t *st, bytebuf_t *buf, const as_directive_t *d) {
    char *path = NULL;
    char *opened = NULL;
    unsigned long long skip = 0;
    unsigned long long count = 0;
    int has_count = 0;
    FILE *fp;
    unsigned char tmp[4096];

    if (d->arg_count < 1) {
        set_err(ctx, "%s:%u: .incbin requires path argument", st != NULL && st->file != NULL ? st->file : "<input>",
                st != NULL ? st->line : 0);
        return -1;
    }
    path = decode_path_arg(d->args[0]);
    if (path == NULL || path[0] == '\0') {
        free(path);
        set_err(ctx, "%s:%u: malformed .incbin path", st != NULL && st->file != NULL ? st->file : "<input>",
                st != NULL ? st->line : 0);
        return -1;
    }
    if (d->arg_count >= 2) {
        long long v;
        if (parse_int64(d->args[1], &v) != 0 || v < 0) {
            free(path);
            set_err(ctx, "%s:%u: malformed .incbin skip", st != NULL && st->file != NULL ? st->file : "<input>",
                    st != NULL ? st->line : 0);
            return -1;
        }
        skip = (unsigned long long)v;
    }
    if (d->arg_count >= 3) {
        long long v;
        if (parse_int64(d->args[2], &v) != 0 || v < 0) {
            free(path);
            set_err(ctx, "%s:%u: malformed .incbin count", st != NULL && st->file != NULL ? st->file : "<input>",
                    st != NULL ? st->line : 0);
            return -1;
        }
        count = (unsigned long long)v;
    }
    has_count = d->arg_count >= 3;

    fp = open_incbin_file(st, path, &opened);
    if (fp == NULL) {
        set_err(ctx, "%s:%u: failed to open .incbin file %s", st != NULL && st->file != NULL ? st->file : "<input>",
                st != NULL ? st->line : 0, path);
        free(path);
        return -1;
    }
    free(path);
    free(opened);

    while (skip > 0) {
        size_t want = skip > sizeof(tmp) ? sizeof(tmp) : (size_t)skip;
        size_t nread = fread(tmp, 1, want, fp);
        if (nread == 0) {
            break;
        }
        skip -= nread;
    }
    /* What is asked for is in the file, or it is refused: a skip past
     * the end gave nothing, and a count past it gave what there was. */
    if (skip > 0) {
        fclose(fp);
        set_err(ctx, "%s:%u: .incbin: the file is shorter than what is to be skipped",
                st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0);
        return -1;
    }
    while (!has_count || count > 0) {
        size_t want = sizeof(tmp);
        size_t nread;
        if (has_count && count < want) {
            want = (size_t)count;
        }
        nread = fread(tmp, 1, want, fp);
        if (nread == 0) {
            break;
        }
        if (bytebuf_append(buf, tmp, nread) != 0) {
            fclose(fp);
            set_err(ctx, "%s:%u: out of memory while appending .incbin",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0);
            return -1;
        }
        if (has_count) {
            count -= nread;
        }
    }
    fclose(fp);
    if (has_count && count > 0) {
        set_err(ctx, "%s:%u: .incbin: the file has fewer bytes after the skip than are asked for",
                st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0);
        return -1;
    }
    return 1;
}

static uint32_t default_text_reloc_type(unsigned machine, const as_instruction_t *in, const as_operand_t *op) {
    if (machine == EM_386) {
        if (op != NULL && (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            in != NULL && is_rel_mnemonic(in->mnemonic)) {
            if (is_fixed_short_rel_mnemonic(in->mnemonic)) {
                return R_386_PC8;
            }
            if (streq_ci(in->mnemonic, "call")) {
                return R_386_PLT32;
            }
            return R_386_PC32;
        }
        return R_386_32;
    }
    if (machine == EM_X86_64) {
        if (op != NULL && (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            in != NULL && is_rel_mnemonic(in->mnemonic)) {
            if (is_fixed_short_rel_mnemonic(in->mnemonic)) {
                return R_X86_64_PC8;
            }
            if (streq_ci(in->mnemonic, "call")) {
                return R_X86_64_PLT32;
            }
            return R_X86_64_PC32;
        }
        if (op != NULL && op->kind == AS_OPERAND_MEMORY && op->u.mem.base_reg != NULL &&
            x86_base_is_ip(op->u.mem.base_reg)) {
            return R_X86_64_PC32;
        }
        if (op != NULL && op->kind == AS_OPERAND_MEMORY) {
            return R_X86_64_32S;
        }
        return R_X86_64_64;
    }
    return reloc_type_for_machine(machine);
}

static int machine_relocation_addend_is_in_place(unsigned machine) {
    return machine == EM_386;
}

typedef struct {
    int digit;
    const char *name;
    const char *file;
    unsigned line;
    const char *section; /* kept: section_name_keep() */
    uint64_t off;
} local_emit_label_t;

static int find_seen_local_label(const local_emit_label_t *labels, size_t count,
                                 const as_expr_t *ref, const local_emit_label_t **out) {
    size_t i;
    const local_emit_label_t *best = NULL;
    unsigned target_line = 0;

    if (labels == NULL || ref == NULL || ref->kind != AS_EXPR_LOCAL_REF ||
        ref->src_file == NULL || out == NULL) {
        return -1;
    }
    if (ref->local_resolved) {
        target_line = ref->local_target_line;
    }
    for (i = 0; i < count; ++i) {
        const local_emit_label_t *cur = &labels[i];
        if (cur->digit != ref->local_digit || cur->file == NULL ||
            as_strcmp(cur->file, ref->src_file) != 0) {
            continue;
        }
        if (ref->local_resolved) {
            if (cur->line == target_line) {
                *out = cur;
                return 0;
            }
            continue;
        }
        if (ref->local_forward) {
            if (cur->line > ref->src_line && (best == NULL || cur->line < best->line)) {
                best = cur;
            }
        } else {
            if (cur->line <= ref->src_line && (best == NULL || cur->line > best->line)) {
                best = cur;
            }
        }
    }
    if (best == NULL) {
        return -1;
    }
    *out = best;
    return 0;
}

static int find_seen_temp_label(const local_emit_label_t *labels, size_t count,
                                const char *name, const local_emit_label_t **out);

/*
 * `x - label` in a data directive, where label is in the section the
 * directive is in and x is not: the entries of a position-independent
 * jump table, `.long .L17 - .L19`, are these.  The value is x's address
 * less the label's, which is x's address less the field's own plus the
 * distance from the label to the field: a PC-relative relocation against
 * x with that distance for its addend.  Such a directive was assembled
 * as zeros with no relocation, and every switch gcc compiled this way
 * jumped to the table.
 *
 * On success *minuend_out is x as a symbol node (its name is the
 * expression's, and lives as long) and *extra_out the addend to add.
 */
static int label_difference_as_pcrel(emit_ctx_t *ctx, const as_stmt_t *st, const as_expr_t *expr,
                                     const char *cur_section, uint64_t field_off,
                                     const local_emit_label_t *labels, size_t label_count,
                                     as_expr_t *minuend_out, int64_t *extra_out) {
    as_expr_linear_t lin;
    const local_emit_label_t *seen = NULL;
    const char *section = "";
    uint64_t off = 0;

    if (ctx == NULL || expr == NULL || cur_section == NULL || minuend_out == NULL || extra_out == NULL) {
        return -1;
    }
    if (as_expr_eval_linear(expr, NULL, NULL, &lin) != AS_EXPR_EVAL_OK ||
        lin.add_symbol == NULL || lin.sub_symbol == NULL ||
        as_strcmp(lin.add_symbol, ".") == 0 || as_strcmp(lin.sub_symbol, ".") == 0) {
        return -1;
    }

    /* Where the subtracted label is: it must be here. */
    if (find_seen_temp_label(labels, label_count, lin.sub_symbol, &seen) == 0) {
        if (as_strcmp(seen->section, cur_section) != 0) {
            return -1;
        }
        off = seen->off;
    } else if (find_label_virtual_location(ctx, st, NULL, 0, 0, lin.sub_symbol,
                                           &section, &off) != 0 ||
               as_strcmp(section, cur_section) != 0) {
        return -1;
    }

    /* And x must not be: two labels of one section differ by a number,
     * which the directive has already been given. */
    seen = NULL;
    if (find_seen_temp_label(labels, label_count, lin.add_symbol, &seen) == 0) {
        if (as_strcmp(seen->section, cur_section) == 0) {
            return -1;
        }
    } else {
        uint64_t add_off = 0;

        if (find_label_virtual_location(ctx, st, NULL, 0, 0, lin.add_symbol,
                                        &section, &add_off) == 0 &&
            as_strcmp(section, cur_section) == 0) {
            return -1;
        }
    }

    memset(minuend_out, 0, sizeof(*minuend_out));
    minuend_out->kind = AS_EXPR_SYMBOL;
    minuend_out->symbol = (char *)lin.add_symbol;
    *extra_out = (int64_t)field_off - (int64_t)off + (int64_t)lin.value;
    return 0;
}

/*
 * The labels seen so far, by name: the last of each.  The list grows as
 * a pass goes down the source and is asked at every name on the way, so
 * the index is of the list it was last asked about and takes in what
 * has been added since.  (Each name was looked for from the end of the
 * list back, which for a source of labels is every label for every
 * label.)
 */
static struct {
    const local_emit_label_t *labels;
    size_t indexed;
    size_t *slots; /* an index into labels, and one; 0 for empty */
    size_t cap;
    size_t used;
} seen_labels;

static void seen_labels_free(void) {
    free(seen_labels.slots);
    memset(&seen_labels, 0, sizeof(seen_labels));
}

static size_t seen_label_hash(const char *name) {
    size_t h = 2166136261u;

    for (; *name != '\0'; ++name) {
        h = (h ^ (unsigned char)*name) * 16777619u;
    }
    return h;
}

static void seen_label_put(const local_emit_label_t *labels, size_t *slots, size_t cap, size_t i) {
    size_t at = seen_label_hash(labels[i].name) & (cap - 1);

    while (slots[at] != 0 && as_strcmp(labels[slots[at] - 1].name, labels[i].name) != 0) {
        at = (at + 1) & (cap - 1);
    }
    if (slots[at] == 0) {
        seen_labels.used++;
    }
    slots[at] = i + 1;
}

/* Take the labels from `indexed` to `count` into the index; -1 for want
 * of memory, and the caller looks the slow way. */
static int seen_labels_take(const local_emit_label_t *labels, size_t count) {
    size_t i;

    if (seen_labels.labels != labels || count < seen_labels.indexed) {
        seen_labels_free();
        seen_labels.labels = labels;
    }
    for (i = seen_labels.indexed; i < count; ++i) {
        if (labels[i].name == NULL) {
            continue;
        }
        if ((seen_labels.used + 1) * 2 > seen_labels.cap) {
            size_t ncap = seen_labels.cap == 0 ? 256 : seen_labels.cap * 2;
            size_t *next = (size_t *)calloc(ncap, sizeof(*next));
            size_t k;

            if (next == NULL) {
                return -1;
            }
            seen_labels.used = 0;
            for (k = 0; k < seen_labels.cap; ++k) {
                if (seen_labels.slots[k] != 0) {
                    seen_label_put(labels, next, ncap, seen_labels.slots[k] - 1);
                }
            }
            free(seen_labels.slots);
            seen_labels.slots = next;
            seen_labels.cap = ncap;
        }
        seen_label_put(labels, seen_labels.slots, seen_labels.cap, i);
    }
    seen_labels.indexed = count;
    return 0;
}

static int find_seen_temp_label(const local_emit_label_t *labels, size_t count,
                                const char *name, const local_emit_label_t **out) {
    size_t i;

    if (labels == NULL || name == NULL || out == NULL) {
        return -1;
    }
    if (seen_labels_take(labels, count) == 0) {
        size_t at;

        if (seen_labels.cap == 0) {
            return -1;
        }
        for (at = seen_label_hash(name) & (seen_labels.cap - 1); seen_labels.slots[at] != 0;
             at = (at + 1) & (seen_labels.cap - 1)) {
            const local_emit_label_t *cur = &labels[seen_labels.slots[at] - 1];

            if (as_strcmp(cur->name, name) == 0) {
                *out = cur;
                return 0;
            }
        }
        return -1;
    }
    for (i = count; i > 0; --i) {
        const local_emit_label_t *cur = &labels[i - 1];
        if (cur->name != NULL && as_strcmp(cur->name, name) == 0) {
            *out = cur;
            return 0;
        }
    }
    return -1;
}

/*
 * What an expression in a data directive comes to.
 *
 * Each name in it is a number (a symbol that was set to one), or a
 * place: so far into a section, for a label of this source and for `.`,
 * or the address of a symbol some other file has.  The whole is then a
 * number and so many times each section and each such symbol, and an
 * object file can hold three shapes of that: a number alone -- two
 * labels of one section, however they are spelt, differ by one; a number
 * and one place, which is a relocation; and a number, a place, less the
 * section the directive is in, which is a relocation relative to the
 * field.  Anything else has no relocation that says it and is refused.
 */
#define DATA_TERMS_MAX 6u

typedef struct {
    const char *base;      /* a section's name, or a symbol's */
    const char *reloc_op;  /* the operator written on the symbol, or NULL */
    int is_section;
    long long k;
    /* The one label the term is of, while it is of one, and how far
     * into the section it is. */
    const char *label;
    long long label_off;
} data_term_t;

typedef struct {
    long long c;
    data_term_t t[DATA_TERMS_MAX];
    size_t n;
} data_value_t;

typedef struct {
    emit_ctx_t *ctx;
    const as_stmt_t *st;
    const char *section;
    uint64_t off;
    const local_emit_label_t *labels;
    size_t label_count;
    /* Set where a place was multiplied, divided, shifted or the like. */
    int not_linear;
} data_eval_t;

static const char *data_eval_section(data_eval_t *ev, const char *name) {
    const char *kept = section_name_keep(ev->ctx, name);

    return kept[0] != '\0' || name == NULL || name[0] == '\0' ? kept : NULL;
}

static int data_value_add(data_value_t *v, const data_term_t *term) {
    size_t i;

    if (term->k == 0) {
        return 0;
    }
    for (i = 0; i < v->n; ++i) {
        /* (A symbol with an operator on it is another thing than the
         * symbol, and than itself with another operator.) */
        if (v->t[i].is_section == term->is_section && as_strcmp(v->t[i].base, term->base) == 0 &&
            (v->t[i].reloc_op == NULL) == (term->reloc_op == NULL) &&
            (term->reloc_op == NULL || strcasecmp(v->t[i].reloc_op, term->reloc_op) == 0)) {
            v->t[i].k += term->k;
            v->t[i].label = NULL;
            if (v->t[i].k == 0) {
                v->t[i] = v->t[--v->n];
            }
            return 0;
        }
    }
    if (v->n == DATA_TERMS_MAX) {
        return -1;
    }
    v->t[v->n++] = *term;
    return 0;
}

static int data_eval_place(data_eval_t *ev, const char *section, long long off, const char *label,
                           data_value_t *out) {
    data_term_t term;

    memset(&term, 0, sizeof(term));
    term.base = data_eval_section(ev, section);
    if (term.base == NULL) {
        return -1;
    }
    term.is_section = 1;
    term.k = 1;
    term.label = label;
    term.label_off = off;
    out->c = off;
    return data_value_add(out, &term);
}

static int data_eval(data_eval_t *ev, const as_expr_t *e, data_value_t *out) {
    data_value_t l;
    data_value_t r;
    size_t i;

    memset(out, 0, sizeof(*out));
    if (e == NULL) {
        return -1;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        out->c = e->value;
        return 0;
    case AS_EXPR_SYMBOL: {
        const local_emit_label_t *seen = NULL;
        const char *section = "";
        uint64_t off = 0;
        data_term_t term;

        if (e->symbol == NULL) {
            return -1;
        }
        if (as_strcmp(e->symbol, ".") == 0) {
            return data_eval_place(ev, ev->section, (long long)ev->off, NULL, out);
        }
        if (e->reloc_op == NULL && asm_var_lookup(ev->ctx, e->symbol, &out->c) == 0) {
            return 0;
        }
        /* A view of the line table is a number. */
        if (e->reloc_op == NULL && line_view_value(ev->ctx, e->symbol, &out->c) == 0) {
            return 0;
        }
        if (e->reloc_op == NULL) {
            if (is_local_temp_symbol_name(e->symbol) &&
                find_seen_temp_label(ev->labels, ev->label_count, e->symbol, &seen) == 0) {
                return data_eval_place(ev, seen->section, (long long)seen->off, e->symbol, out);
            }
            if (find_label_virtual_location(ev->ctx, ev->st, NULL, 0, 0, e->symbol, &section, &off) == 0) {
                return data_eval_place(ev, section, (long long)off, e->symbol, out);
            }
        }
        memset(&term, 0, sizeof(term));
        term.base = e->symbol;
        term.reloc_op = e->reloc_op;
        term.k = 1;
        return data_value_add(out, &term);
    }
    case AS_EXPR_LOCAL_REF: {
        const local_emit_label_t *seen = NULL;
        virtual_addr_value_t target;

        if (find_seen_local_label(ev->labels, ev->label_count, e, &seen) == 0) {
            return data_eval_place(ev, seen->section, (long long)seen->off, NULL, out);
        }
        if (local_ref_virtual_location(ev->ctx, ev->st, e, &target) == 0 && target.has_section) {
            return data_eval_place(ev, target.section, target.value, NULL, out);
        }
        return -1;
    }
    case AS_EXPR_UNARY:
        if (data_eval(ev, e->lhs != NULL ? e->lhs : e->rhs, &l) != 0) {
            return -1;
        }
        if (e->op == AS_EXPR_OP_NEG) {
            *out = l;
            out->c = (long long)(0ull - (unsigned long long)l.c);
            for (i = 0; i < out->n; ++i) {
                out->t[i].k = -out->t[i].k;
            }
            return 0;
        }
        if (l.n != 0) {
            ev->not_linear = 1;
            return -1;
        }
        break;
    case AS_EXPR_BINARY:
        if (data_eval(ev, e->lhs, &l) != 0 || data_eval(ev, e->rhs, &r) != 0) {
            return -1;
        }
        if (e->op == AS_EXPR_OP_ADD || e->op == AS_EXPR_OP_SUB) {
            int sign = e->op == AS_EXPR_OP_ADD ? 1 : -1;

            *out = l;
            out->c = (long long)((unsigned long long)l.c + (unsigned long long)sign * (unsigned long long)r.c);
            for (i = 0; i < r.n; ++i) {
                r.t[i].k *= sign;
                if (data_value_add(out, &r.t[i]) != 0) {
                    return -1;
                }
            }
            return 0;
        }
        if (e->op == AS_EXPR_OP_MUL && (l.n == 0) != (r.n == 0)) {
            const data_value_t *places = l.n != 0 ? &l : &r;
            long long by = l.n != 0 ? r.c : l.c;

            out->c = (long long)((unsigned long long)places->c * (unsigned long long)by);
            for (i = 0; i < places->n; ++i) {
                data_term_t term = places->t[i];

                term.k *= by;
                if (data_value_add(out, &term) != 0) {
                    return -1;
                }
            }
            return 0;
        }
        if (l.n != 0 || r.n != 0) {
            ev->not_linear = 1;
            return -1;
        }
        break;
    default:
        return -1;
    }
    /* What is left is an operation on numbers. */
    {
        as_expr_t node = *e;
        as_expr_t ln;
        as_expr_t rn;

        memset(&ln, 0, sizeof(ln));
        memset(&rn, 0, sizeof(rn));
        ln.kind = AS_EXPR_CONST;
        ln.value = l.c;
        rn.kind = AS_EXPR_CONST;
        rn.value = e->kind == AS_EXPR_BINARY ? r.c : l.c;
        node.lhs = e->lhs != NULL ? &ln : NULL;
        node.rhs = e->rhs != NULL ? &rn : NULL;
        return as_expr_eval(&node, NULL, NULL, &out->c) == AS_EXPR_EVAL_OK ? 0 : -1;
    }
}

/* The one place of a value that is not a number alone, and whether it is
 * less the section the statement is in; NULL where the value is not of
 * either shape. */
static const data_term_t *data_value_place(const data_eval_t *ev, const data_value_t *v, int *pcrel) {
    const data_term_t *place = NULL;
    size_t i;

    *pcrel = 0;
    for (i = 0; i < v->n; ++i) {
        if (v->t[i].k == 1 && place == NULL) {
            place = &v->t[i];
        } else if (v->t[i].k == -1 && v->t[i].is_section && as_strcmp(v->t[i].base, ev->section) == 0 && !*pcrel) {
            *pcrel = 1;
        } else {
            return NULL;
        }
    }
    return place;
}

/*
 * Whether an expression in an operand is one an instruction can hold: a
 * number, or one of the two shapes a relocation has.  `x+y` and `2*x`
 * of two labels had the relocation of the first name in them and the
 * rest dropped.
 */
static int operand_expr_is_expressible(emit_ctx_t *ctx, const as_stmt_t *st, const as_expr_t *e,
                                       const char *section, uint64_t off, const local_emit_label_t *labels,
                                       size_t label_count) {
    data_eval_t ev;
    data_value_t v;
    int pcrel;

    memset(&ev, 0, sizeof(ev));
    ev.ctx = ctx;
    ev.st = st;
    ev.section = section != NULL ? section : "";
    ev.off = off;
    ev.labels = labels;
    ev.label_count = label_count;
    if (data_eval(&ev, e, &v) != 0) {
        /* What could not be read at all is left to what reads operands. */
        return !ev.not_linear;
    }
    return v.n == 0 || data_value_place(&ev, &v, &pcrel) != NULL;
}

/* The relocation of a field of `width` bytes, or AS_RELOC_TYPE_BAD where
 * the object format has none of that width. */
static uint32_t data_reloc_type(unsigned machine, unsigned width, int pcrel) {
    if (machine == EM_X86_64) {
        switch (width) {
        case 1: return pcrel ? R_X86_64_PC8 : R_X86_64_8;
        case 2: return pcrel ? R_X86_64_PC16 : R_X86_64_16;
        case 4: return pcrel ? R_X86_64_PC32 : R_X86_64_32;
        case 8: return pcrel ? R_X86_64_PC64 : R_X86_64_64;
        default: return AS_RELOC_TYPE_BAD;
        }
    }
    switch (width) {
    case 1: return pcrel ? R_386_PC8 : R_386_8;
    case 2: return pcrel ? R_386_PC16 : R_386_16;
    case 4: return pcrel ? R_386_PC32 : R_386_32;
    default: return AS_RELOC_TYPE_BAD;
    }
}

/*
 * One argument of a data directive of x86, which is not a number as it
 * is written: its value into the field, or its relocation.  -1 with the
 * reason said where it is neither.
 */
static int emit_data_field(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, const char *section,
                           elf_section_t *sec, unsigned char *field, uint64_t field_off, unsigned width,
                           const local_emit_label_t *labels, size_t label_count) {
    unsigned machine = ctx->cfg != NULL ? ctx->cfg->machine : EM_386;
    const char *file = st->file != NULL ? st->file : "<input>";
    as_expr_t *expr = as_parse_expr_string(arg, st->file, st->line);
    data_eval_t ev;
    data_value_t v;
    const data_term_t *place = NULL;
    const char *name;
    int64_t addend;
    uint32_t type;
    int pcrel = 0;
    int rc = -1;

    if (expr == NULL) {
        set_err(ctx, "%s:%u: '%s' is not an expression", file, st->line, arg);
        return -1;
    }
    memset(&ev, 0, sizeof(ev));
    ev.ctx = ctx;
    ev.st = st;
    ev.section = section != NULL ? section : "";
    ev.off = field_off;
    ev.labels = labels;
    ev.label_count = label_count;
    if (data_eval(&ev, expr, &v) != 0) {
        set_err(ctx, "%s:%u: '%s' is neither a number nor a place a relocation can name", file, st->line, arg);
        goto out;
    }
    if (v.n == 0) {
        write_u64_le_at(field, (uint64_t)v.c, width);
        rc = 0;
        goto out;
    }
    place = data_value_place(&ev, &v, &pcrel);
    if (place == NULL) {
        set_err(ctx, "%s:%u: '%s' cannot be a number or one relocation: it is not a place, or one less a place "
                     "of this section",
                file, st->line, arg);
        goto out;
    }
    addend = (int64_t)v.c;
    if (pcrel) {
        addend += (int64_t)field_off;
    }
    name = place->base;
    if (place->is_section && place->label != NULL) {
        /* Against the label, as a reference to one in an instruction is. */
        name = place->label;
        addend -= (int64_t)place->label_off;
    }
    /*
     * (With an operator the relocation is the operator's, and this is
     * only what says how wide the field is: of eight bytes it names the
     * wide one of the operator's two, of four the other.  It was the
     * eight-byte type whatever the field, so `.long ext@GOT` in 64-bit
     * code had R_X86_64_GOT64 on four bytes, and `.long tv@DTPOFF`,
     * which a compiler writes in debugging information, was refused.)
     */
    type = data_reloc_type(machine, width, pcrel);
    if (type == AS_RELOC_TYPE_BAD && place->reloc_op != NULL && !pcrel) {
        type = reloc_type_for_machine(machine);
    }
    if (place->reloc_op != NULL) {
        /* And the operator's relocation is of the field's width, or
         * there is none to make: `.quad ext@PLT`, and on i386 `.quad`
         * with any operator, had a relocation of four bytes on eight. */
        uint32_t named = reloc_type_for_symbol(machine, name, place->reloc_op, type);
        int bytes = named != AS_RELOC_TYPE_BAD ? elf_reloc_size_for_machine((uint16_t)machine, named) : 0;

        if (bytes > 0 && (unsigned)bytes != width) {
            set_err(ctx, "%s:%u: '%s': the relocation @%s names is of %d bytes, and the field is of %u", file,
                    st->line, arg, place->reloc_op, bytes, width);
            goto out;
        }
    }
    if (type == AS_RELOC_TYPE_BAD) {
        set_err(ctx, "%s:%u: '%s': there is no relocation of %u bytes for this target", file, st->line, arg, width);
        goto out;
    }
    write_u64_le_at(field,
                    machine_relocation_addend_is_in_place(machine)
                        ? (uint64_t)(addend + local_label_addend(ctx, name))
                        : 0,
                    width);
    if (add_reloc_for_symbol_op(ctx, sec, name, place->reloc_op, field_off, type, addend) != 0) {
        if (ctx->errbuf == NULL || ctx->errbuf[0] == '\0') {
            set_err(ctx, "%s:%u: '%s': no relocation could be made", file, st->line, arg);
        }
        goto out;
    }
    rc = 0;
out:
    as_expr_free(expr);
    return rc;
}

/*
 * A relocation chosen for code of one machine, named as an object of
 * the other names it: .code32 or .code16 in an ELF64 object, .code64 in
 * an ELF32 one.  0, or -1 where the object has none such -- a field
 * sign-extended to 64 bits or of 64 has none in ELF32, and what goes
 * by a table has none across.
 */
static int x86_reloc_for_object(unsigned code_machine, unsigned obj_machine, uint32_t *type) {
    static const uint32_t pairs[][2] = {
        /* i386's, x86-64's */
        {R_386_32, R_X86_64_32},     {R_386_16, R_X86_64_16},     {R_386_8, R_X86_64_8},
        {R_386_PC32, R_X86_64_PC32}, {R_386_PC16, R_X86_64_PC16}, {R_386_PC8, R_X86_64_PC8},
    };
    size_t i;

    if (code_machine == obj_machine) {
        return 0;
    }
    if (code_machine == EM_X86_64 && *type == R_X86_64_PLT32) {
        *type = R_386_PC32;
        return 0;
    }
    for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        if (*type == pairs[i][code_machine == EM_386 ? 0 : 1]) {
            *type = pairs[i][code_machine == EM_386 ? 1 : 0];
            return 0;
        }
    }
    return -1;
}

static int emit_relocations(emit_ctx_t *ctx) {
    size_t i;
    unsigned machine = ctx->cfg != NULL ? ctx->cfg->machine : EM_386;
    sec_buf_vec_t secbufs;
    section_track_t track;
    local_emit_label_t *local_labels = NULL;
    size_t local_label_cap = 0;
    size_t local_label_count = 0;
    size_t *list_sec = NULL;
    size_t *list_off = NULL;
    static int trace_env = -1;

    if (trace_env < 0) {
        const char *v = getenv("AS_DEBUG_RELOC_TRACE");
        trace_env = (v != NULL && v[0] != '\0') ? 1 : 0;
    }
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }
    /* For a listing: where each statement's bytes begin, and in which
     * section.  Without them the listing is not made; the object is. */
    if (ctx->cfg != NULL && ctx->cfg->list_stmt != NULL && ctx->parsed->count != 0) {
        list_sec = (size_t *)calloc(ctx->parsed->count, sizeof(*list_sec));
        list_off = (size_t *)calloc(ctx->parsed->count, sizeof(*list_off));
        if (list_sec == NULL || list_off == NULL) {
            free(list_sec);
            free(list_off);
            list_sec = NULL;
            list_off = NULL;
        }
    }
    ctx->list_sec = list_sec;
    ctx->list_off = list_off;
    /* (The index of labels seen is of this pass's list, which is new.) */
    seen_labels_free();

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        elf_section_t *cur_sec;
        uint64_t cur_off;
        int trc;
        size_t j;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        cur_sec = section_for_name(ctx, track.current);
        cur_off = (uint64_t)sb->buf.len;
        if (list_sec != NULL) {
            list_sec[i] = (size_t)(sb - secbufs.items);
            list_off[i] = sb->buf.len;
        }

        for (j = 0; j < st->label_count; ++j) {
            int label_digit = -1;
            if (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 ||
                is_local_temp_symbol_name(st->labels[j].name)) {
                local_emit_label_t *dst;

                /* As many as the source has: there was room for 8192,
                 * and one past that was found by laying the source out
                 * again from its top, each time. */
                if (local_label_count == local_label_cap) {
                    size_t cap = local_label_cap != 0 ? local_label_cap * 2 : 256;
                    local_emit_label_t *grown =
                        (local_emit_label_t *)realloc(local_labels, cap * sizeof(*grown));

                    if (grown == NULL) {
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    local_labels = grown;
                    local_label_cap = cap;
                    ctx->emit_labels = grown;
                }
                dst = &local_labels[local_label_count++];
                dst->digit = label_digit;
                dst->name = st->labels[j].name;
                dst->file = st->labels[j].file;
                dst->line = st->labels[j].line;
                dst->off = cur_off;
                dst->section = section_name_keep(ctx, track.current);
            }
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            const as_directive_t *d = &st->u.directive;
            int drc;
            unsigned width = 0;

            if (apply_asm_var_directive(ctx, st, d) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            trc = section_track_apply_directive(&track, d);
            if (trc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            if (as_strcmp(d->name, ".byte") == 0) width = 1;
            else if (as_strcmp(d->name, ".word") == 0 || as_strcmp(d->name, ".short") == 0 ||
                     as_strcmp(d->name, ".hword") == 0 || as_strcmp(d->name, ".2byte") == 0) width = 2;
            else if (as_strcmp(d->name, ".long") == 0 || as_strcmp(d->name, ".4byte") == 0) width = 4;
            else if (as_strcmp(d->name, ".quad") == 0 || as_strcmp(d->name, ".8byte") == 0) width = 8;

            ctx->pad_after_data = sb->after_data;
            drc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                            track.x86_code_bits, d);
            ctx->pad_after_data = 0;
            if (drc < 0) {
                set_err(ctx, "%s:%u: malformed directive data", st->file != NULL ? st->file : "<input>", st->line);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            /* Padding leaves it as it was; `.org` is not data. */
            if ((uint64_t)sb->buf.len > cur_off && as_strcmp(d->name, ".align") != 0 &&
                as_strcmp(d->name, ".balign") != 0 && as_strcmp(d->name, ".p2align") != 0) {
                sb->after_data = as_strcmp(d->name, ".org") != 0;
            }
            if (width != 0) {
                for (j = 0; j < d->arg_count; ++j) {
                    char *sym = NULL;
                    char *sym_op = NULL;
                    int64_t addend = 0;
                    uint64_t reloc_off;
                    long long const_value = 0;
                    if (parse_int64(d->args[j], &const_value) == 0 ||
                        parse_const_expr_string(d->args[j], &const_value) == 0 ||
                        eval_arg_asm_vars(ctx, st, d->args[j], &const_value) == 0) {
                        continue;
                    }
                    if (machine == EM_386 || machine == EM_X86_64) {
                        reloc_off = cur_off + (uint64_t)(j * width);
                        if (eval_linux_alt_len_expr(ctx, st, d->args[j], &const_value) == 0) {
                            continue;
                        }
                        if (reloc_off + width > (uint64_t)sb->buf.len ||
                            emit_data_field(ctx, st, d->args[j], track.current, cur_sec, sb->buf.data + reloc_off,
                                            reloc_off, width, local_labels, local_label_count) != 0) {
                            section_track_free(&track);
                            sec_buf_vec_free(&secbufs);
                            return -1;
                        }
                        continue;
                    }
                    if (parse_symbol_addend_op_arg(d->args[j], &sym, &sym_op, &addend) == 0 && sym != NULL) {
                        uint32_t t = reloc_type_for_machine(machine);
                        long long asm_value;
                        if (sym_op == NULL && asm_var_lookup(ctx, sym, &asm_value) == 0) {
                            free(sym);
                            continue;
                        }
                        reloc_off = cur_off + (uint64_t)(j * width);
                        if (machine_relocation_addend_is_in_place(machine) &&
                            reloc_off + width <= (uint64_t)sb->buf.len) {
                            write_u64_le_at(sb->buf.data + reloc_off,
                                            (uint64_t)(addend + local_label_addend(ctx, sym)), width);
                        }
                        if (add_reloc_for_symbol_op(ctx, cur_sec, sym, sym_op, reloc_off, t, addend) != 0) {
                            free(sym);
                            free(sym_op);
                            section_track_free(&track);
                            sec_buf_vec_free(&secbufs);
                            return -1;
                        }
                        free(sym);
                        free(sym_op);
                        continue;
                    }
                    free(sym_op);
                    sym_op = NULL;
                    {
                        as_expr_t *expr = as_parse_expr_string(d->args[j], st->file, st->line);
                        as_expr_t minuend_node;
                        const as_expr_t *minuend = NULL;
                        int64_t pcrel_extra = 0;

                        /* `x - .`, or `x - label` with the label in this
                         * section: a PC-relative relocation against x. */
                        if (expr != NULL && width == 4) {
                            if (expr->kind == AS_EXPR_BINARY &&
                                expr->op == AS_EXPR_OP_SUB &&
                                expr->lhs != NULL &&
                                expr->rhs != NULL &&
                                expr->rhs->kind == AS_EXPR_SYMBOL &&
                                expr->rhs->symbol != NULL &&
                                as_strcmp(expr->rhs->symbol, ".") == 0) {
                                minuend = expr->lhs;
                            } else if (label_difference_as_pcrel(ctx, st, expr, track.current,
                                                                 cur_off + (uint64_t)(j * width),
                                                                 local_labels, local_label_count,
                                                                 &minuend_node, &pcrel_extra) == 0) {
                                minuend = &minuend_node;
                            }
                        }
                        if (minuend != NULL) {
                            uint32_t t = (machine == EM_X86_64) ? R_X86_64_PC32 : R_386_PC32;
                            char *target_name = NULL;
                            int handled = 0;

                            if (minuend->kind == AS_EXPR_LOCAL_REF) {
                                const local_emit_label_t *seen = NULL;
                                virtual_addr_value_t target;
                                if (find_seen_local_label(local_labels, local_label_count, minuend, &seen) == 0) {
                                    target_name = xstrdup(seen->section);
                                    addend = (int64_t)seen->off;
                                    handled = (target_name != NULL);
                                } else if (local_ref_virtual_location(ctx, st, minuend, &target) == 0 &&
                                           target.has_section) {
                                    target_name = xstrdup(target.section);
                                    addend = (int64_t)target.value;
                                    handled = (target_name != NULL);
                                }
                            } else if (minuend->kind == AS_EXPR_SYMBOL &&
                                       minuend->symbol != NULL &&
                                       as_strcmp(minuend->symbol, ".") != 0) {
                                const char *target_section = "";
                                uint64_t target_off = 0;
                                if (is_local_temp_symbol_name(minuend->symbol)) {
                                    const local_emit_label_t *seen = NULL;
                                    if (find_seen_temp_label(local_labels, local_label_count, minuend->symbol,
                                                             &seen) == 0) {
                                        target_name = xstrdup(seen->section);
                                        addend = (int64_t)seen->off;
                                        handled = (target_name != NULL);
                                    } else if (find_label_virtual_location(ctx, st, NULL, 0, 0, minuend->symbol,
                                                                           &target_section,
                                                                           &target_off) == 0) {
                                        target_name = xstrdup(target_section);
                                        addend = (int64_t)target_off;
                                        handled = (target_name != NULL);
                                    }
                                } else {
                                    target_name = xstrdup(minuend->symbol);
                                    addend = 0;
                                    handled = (target_name != NULL);
                                }
                            }
                            if (handled) {
                                addend += pcrel_extra;
                                reloc_off = cur_off + (uint64_t)(j * width);
                                if (machine_relocation_addend_is_in_place(machine) &&
                                    reloc_off + width <= (uint64_t)sb->buf.len) {
                                    write_u64_le_at(sb->buf.data + reloc_off,
                                                    (uint64_t)(addend + local_label_addend(ctx, target_name)), width);
                                }
                                if (add_reloc_for_symbol_ex(ctx, cur_sec, target_name, reloc_off, t, addend) != 0) {
                                    free(target_name);
                                    as_expr_free(expr);
                                    section_track_free(&track);
                                    sec_buf_vec_free(&secbufs);
                                    return -1;
                                }
                                free(target_name);
                                as_expr_free(expr);
                                continue;
                            }
                            free(target_name);
                        }
                        as_expr_free(expr);
                    }
                }
            }
            continue;
        }
        if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }
        if (!section_name_is_executable(ctx, track.current)) {
            continue;
        }
        {
            unsigned char code[32];
            size_t code_len = 0;
            char encerr[256];
            size_t rel_count = 0;
            int tls_in_stmt = 0;
            /*
             * The object's machine, and the one whose instructions
             * these are: .code32 in an ELF64 object is i386's code, and
             * its fields are found and its relocations chosen as
             * i386's are, then named as the object names them.  From
             * here `machine` is the code's.
             */
            const unsigned obj_machine = machine;
            unsigned machine = (obj_machine == EM_386 || obj_machine == EM_X86_64)
                                   ? (track.x86_code_bits == 64u ? (unsigned)EM_X86_64 : (unsigned)EM_386)
                                   : obj_machine;

            if (encode_x86_stmt_for_layout(ctx, track.current, cur_off, track.x86_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
                set_err(ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, encerr);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (ctx->cfg->target != 0) {
                /* The fields a relocation fills are told by the
                 * encoder, with the relocation's own number. */
                int bad = 0;

                for (j = 0; j < ctx->arm_out.fixup_count && !bad; ++j) {
                    const as_arm_fixup_t *f = &ctx->arm_out.fixups[j];

                    reloc_type_exact = 1;
                    bad = add_reloc_for_symbol_ex(ctx, cur_sec, f->symbol, cur_off + f->offset, f->type, f->addend);
                    reloc_type_exact = 0;
                }
                if (bad) {
                    if (ctx->errbuf == NULL || ctx->errbuf[0] == '\0') {
                        set_err(ctx, "%s:%u: a relocation for '%s' could not be made",
                                st->file != NULL ? st->file : "<input>", st->line, st->u.instr.mnemonic);
                    }
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                if (bytebuf_append(&sb->buf, code, code_len) != 0) {
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                sb->after_data = 0;
                continue;
            }
            if (trace_env) {
                fprintf(stderr, "as: reloc-trace insn %s:%u mnem=%s text_off=0x%llx len=%llu\n",
                        st->file != NULL ? st->file : "<input>",
                        st->line,
                        st->u.instr.mnemonic != NULL ? st->u.instr.mnemonic : "<null>",
                        (unsigned long long)cur_off,
                        (unsigned long long)code_len);
            }
            for (j = 0; j < st->u.instr.operand_count; ++j) {
                const as_operand_t *op = &st->u.instr.operands[j];
                const as_expr_t *e = NULL;
                const char *sym;
                const char *rop; /* the operator written on it, or NULL */
                uint32_t t;
                uint64_t reloc_off;
                uint64_t reloc_width = 4;
                int64_t addend = 0;
                int have_disp_field = 0;
                size_t disp_field_off = 0;
                int is_label_difference = 0;
                long long label_difference = 0;
                const char *taken_from = NULL;
                long long taken_value = 0;
                int dot_relative = 0;
                int dot_alone = 0;

                if (asm_reg_alias_for_operand(ctx, op) != NULL) {
                    continue;
                }
                /* A jump that was written with its distance in it. */
                {
                    long long there;
                    int is_short;

                    if (relaxed_place_branch_target(ctx, st, track.current, cur_off, track.x86_code_bits, &there,
                                                    &is_short) == 0 ||
                        call_to_place_target(st, cur_off, &there) == 0) {
                        continue;
                    }
                }
                if (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) {
                    e = op->u.expr;
                } else if (op->kind == AS_OPERAND_MEMORY) {
                    e = op->u.mem.disp;
                }
                if (e != NULL && (machine == EM_386 || machine == EM_X86_64) &&
                    !operand_expr_is_expressible(ctx, st, e, track.current, cur_off, local_labels,
                                                 local_label_count)) {
                    set_err(ctx, "%s:%u: '%s' cannot be a number or one relocation",
                            st->file != NULL ? st->file : "<input>", st->line, op->raw != NULL ? op->raw : "");
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                if (e != NULL) {
                    long long resolved_disp;
                    int skip_local_resolve = 0;
                    /*
                     * A reference to a label of this section needs no
                     * relocation where it is a distance: the target of a
                     * branch.  Where it is the label's address -- an
                     * immediate, `pushl $1f`; an absolute operand, `movl
                     * .L5, %eax`; a displacement, `movl 1f(%ebx), %eax`
                     * -- the address is not known until the section is
                     * placed, and it was taken for a distance all the
                     * same: no relocation, and a zero in the instruction.
                     */
                    int is_distance = mnemonic_takes_a_target(st->u.instr.mnemonic) &&
                                      op->kind != AS_OPERAND_MEMORY && !(op->raw != NULL && op->raw[0] == '*');
                    int can_resolve_without_reloc =
                        is_distance && !st->u.instr.target_in_other_section &&
                        (e->kind == AS_EXPR_LOCAL_REF || expr_is_local_temp_symbol(e) ||
                         is_fixed_short_rel_mnemonic(st->u.instr.mnemonic));
                    if (machine == EM_X86_64 &&
                        op->kind == AS_OPERAND_MEMORY &&
                        op->u.mem.base_reg != NULL &&
                        x86_base_is_ip(op->u.mem.base_reg)) {
                        skip_local_resolve = 1;
                    }
                    /* A call of a symbol is the linker's to resolve --
                     * but not of a label that is the source's own and in
                     * this section, which is a distance like any other. */
                    if (e->kind == AS_EXPR_SYMBOL &&
                        (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
                        is_call_mnemonic(st->u.instr.mnemonic) && !expr_is_local_temp_symbol(e)) {
                        skip_local_resolve = 1;
                    }
                    if (can_resolve_without_reloc && !skip_local_resolve &&
                        eval_local_rel_expr_virtual(ctx, track.current, st, cur_off, track.x86_code_bits, e,
                                                    &resolved_disp) == 0) {
                        continue;
                    }
                }
                sym = first_symbol_in_expr(e);
                if (sym == NULL) {
                    continue;
                }
                rop = first_symbol_op_in_expr(e);
                /* (A difference that was a number when the instruction
                 * was encoded is in its bytes already, in whatever form
                 * the number chose: stmt_fold_label_diffs().) */
                {
                    long long known;

                    if (st->u.instr.mnemonic != NULL && !is_rel_mnemonic(st->u.instr.mnemonic) &&
                        fixed_label_diff(ctx, st, track.x86_code_bits, e, &known) == 0) {
                        continue;
                    }
                }
                /*
                 * One label of a section less another of it is a number,
                 * and is written as one.  It was given a relocation to
                 * the first and the second was forgotten: `movl
                 * $end-start, %eax` loaded the address of `end`.
                 */
                {
                    as_expr_linear_t lin;

                    if (as_expr_eval_linear(e, NULL, NULL, &lin) == AS_EXPR_EVAL_OK &&
                        (lin.add_symbol != NULL || lin.add_local != NULL) &&
                        (lin.sub_symbol != NULL || lin.sub_local != NULL) &&
                        !(op->kind == AS_OPERAND_MEMORY && op->u.mem.base_reg != NULL &&
                          x86_base_is_ip(op->u.mem.base_reg)) &&
                        eval_abs_local_diff_expr_virtual(ctx, st, e, &label_difference) == 0) {
                        is_label_difference = 1;
                    } else if (as_expr_eval_linear(e, NULL, NULL, &lin) == AS_EXPR_EVAL_OK && lin.add_symbol != NULL &&
                               lin.sub_symbol != NULL && lin.add_local == NULL && lin.sub_local == NULL &&
                               (as_strcmp(lin.add_symbol, ".") == 0 || as_strcmp(lin.sub_symbol, ".") == 0)) {
                        /*
                         * With `.`, which is where the instruction
                         * begins: `. - label` and `label - .` are
                         * numbers, and `ext - .` is a distance.
                         */
                        long long here = (long long)cur_off;
                        const char *other = as_strcmp(lin.add_symbol, ".") == 0 ? lin.sub_symbol : lin.add_symbol;
                        int dot_is_taken = as_strcmp(lin.sub_symbol, ".") == 0;
                        as_expr_linear_t at;
                        const char *label_section = "";
                        uint64_t label_off = 0;

                        if (as_strcmp(other, ".") == 0) {
                            is_label_difference = 1;
                            label_difference = lin.value;
                        } else if (as_expr_eval_linear(e, reloc_dot_lookup, &here, &at) != AS_EXPR_EVAL_OK) {
                            taken_from = lin.sub_symbol;
                        } else if (find_label_virtual_location(ctx, NULL, NULL, 0, 0, other, &label_section,
                                                               &label_off) == 0 &&
                                   track.current != NULL && as_strcmp(label_section, track.current) == 0) {
                            is_label_difference = 1;
                            label_difference = at.value + (dot_is_taken ? (long long)label_off : -(long long)label_off);
                        } else if (dot_is_taken) {
                            dot_relative = 1;
                            taken_value = at.value;
                            sym = other;
                        } else {
                            taken_from = lin.sub_symbol;
                        }
                    } else if (as_expr_eval_linear(e, NULL, NULL, &lin) == AS_EXPR_EVAL_OK && lin.add_symbol != NULL &&
                               lin.sub_symbol != NULL && lin.add_local == NULL && lin.sub_local == NULL) {
                        /* Less a label, from a symbol of elsewhere. */
                        taken_from = lin.sub_symbol;
                        taken_value = lin.value;
                    } else if (as_expr_eval_linear(e, NULL, NULL, &lin) == AS_EXPR_EVAL_OK && lin.add_symbol != NULL &&
                               as_strcmp(lin.add_symbol, ".") == 0 && lin.sub_symbol == NULL && lin.add_local == NULL &&
                               lin.sub_local == NULL && track.current != NULL) {
                        /* `.` alone is a place in this section, and was
                         * a relocation to a symbol named `.`. */
                        dot_alone = 1;
                        taken_value = lin.value;
                    }
                }
                /* @TLSCALL has no field: its relocation is on the
                 * instruction, at its first byte. */
                if (expr_is_tlscall(e)) {
                    if (add_reloc_for_symbol_op(ctx, cur_sec, sym, rop, cur_off,
                                                machine == EM_X86_64 ? R_X86_64_NONE : R_386_NONE, 0) != 0) {
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    rel_count++;
                    continue;
                }
                {
                    /* The addend, where the expression is this symbol and
                     * a number; a local label taken from it is a number
                     * too, once laid out. */
                    as_expr_linear_t lin;
                    long long local_off = 0;

                    if (as_expr_eval_linear(e, NULL, NULL, &lin) == AS_EXPR_EVAL_OK &&
                        lin.add_symbol != NULL && as_strcmp(lin.add_symbol, sym) == 0 &&
                        lin.sub_symbol == NULL &&
                        (lin.sub_local == NULL ||
                         eval_local_rel_expr_virtual(ctx, track.current, st, 0, track.x86_code_bits,
                                                     lin.sub_local, &local_off) == 0)) {
                        addend = (int64_t)(lin.value - local_off);
                    } else {
                        /* `sym + [. - .L1]`: the distance from a label of
                         * this section to here is a number. */
                        long long here = (long long)cur_off;
                        const char *label_section = "";
                        uint64_t label_off = 0;

                        if (as_expr_eval_linear(e, reloc_dot_lookup, &here, &lin) == AS_EXPR_EVAL_OK &&
                            lin.add_symbol != NULL && as_strcmp(lin.add_symbol, sym) == 0 && lin.sub_symbol != NULL &&
                            lin.sub_local == NULL && lin.add_local == NULL &&
                            find_label_virtual_location(ctx, NULL, NULL, 0, 0, lin.sub_symbol, &label_section,
                                                        &label_off) == 0 &&
                            track.current != NULL && as_strcmp(label_section, track.current) == 0) {
                            addend = (int64_t)(lin.value - (long long)label_off);
                        }
                    }
                }
                if (dot_alone) {
                    sym = track.current;
                    addend = (int64_t)taken_value + (int64_t)cur_off;
                }
                t = default_text_reloc_type(machine, &st->u.instr, op);
                if (t == R_386_PC8 || t == R_X86_64_PC8) {
                    reloc_width = 1;
                } else if (t == R_386_PC16 || t == R_X86_64_PC16) {
                    reloc_width = 2;
                } else if (machine == EM_X86_64 && t == R_X86_64_64) {
                    reloc_width = 8;
                    /*
                     * Most x86-64 ALU/immediate encodings use imm32 (sign-extended),
                     * not full imm64. If the encoded instruction cannot carry an
                     * 8-byte immediate, fall back to a 32-bit signed relocation.
                     */
                    if ((op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) && code_len < reloc_width) {
                        t = R_X86_64_32S;
                        reloc_width = 4;
                    } else if (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) {
                        /* (Nor one that is eight bytes long by its
                         * prefix: `movq $sym@GOTPCREL, %r16` is C7 and
                         * four bytes behind REX2.) */
                        unsigned field = 0;
                        int field_rex_w = 0;
                        unsigned char field_opcode = 0;

                        if (x86_imm_field_width(code, code_len, track.x86_code_bits, &field, &field_rex_w,
                                                &field_opcode) == 0 &&
                            field == 4) {
                            t = R_X86_64_32S;
                            reloc_width = 4;
                        }
                    }
                }
                adjust_x86_rel_reloc_to_encoding(machine, &st->u.instr, op, code, code_len, &t, &reloc_width);
                /*
                 * An immediate that is a symbol: the relocation is the
                 * field's width and kind.  In 64-bit code a four-byte
                 * field is sign-extended by a 64-bit operation and by a
                 * push (R_X86_64_32S) and not by a 32-bit one
                 * (R_X86_64_32).
                 */
                if (op->kind == AS_OPERAND_IMMEDIATE && op->raw != NULL && op->raw[0] == '$' &&
                    rop == NULL &&
                    (t == R_386_32 || t == R_X86_64_64 || t == R_X86_64_32S || t == R_X86_64_32)) {
                    unsigned imm_width;
                    int imm_rex_w;
                    unsigned char imm_opcode;

                    if (x86_imm_field_width(code, code_len, track.x86_code_bits, &imm_width, &imm_rex_w,
                                            &imm_opcode) == 0) {
                        reloc_width = imm_width;
                        if (machine == EM_386) {
                            t = imm_width == 1 ? R_386_8 : (imm_width == 2 ? R_386_16 : R_386_32);
                        } else if (imm_width == 1) {
                            t = R_X86_64_8;
                        } else if (imm_width == 2) {
                            t = R_X86_64_16;
                        } else if (imm_width == 8) {
                            t = R_X86_64_64;
                        } else {
                            t = (imm_rex_w || imm_opcode == 0x68) ? R_X86_64_32S : R_X86_64_32;
                        }
                    }
                }
                /*
                 * An address that is a symbol: the relocation goes on the
                 * displacement, wherever in the instruction that is, and
                 * in 64-bit code a displacement is four bytes sign-
                 * extended whatever the operation's size.
                 */
                if (machine == EM_386 && track.x86_code_bits == 16u &&
                    (op->kind == AS_OPERAND_MEMORY ||
                     (operand_is_bare_address(op) && !is_rel_mnemonic(st->u.instr.mnemonic))) &&
                    x86_disp16_offset(code, code_len, 1, &disp_field_off) == 0) {
                    /* In 16-bit code an address is of 16 bits: `movw
                     * sym, %bx` is two bytes of it and R_386_16.  Taken
                     * for one of 32, it had four bytes of relocation
                     * and no room for them, and the instruction was
                     * zeros from its first byte. */
                    reloc_width = 2;
                    t = R_386_16;
                    have_disp_field = 1;
                } else if ((op->kind == AS_OPERAND_MEMORY ||
                            (operand_is_bare_address(op) && !is_rel_mnemonic(st->u.instr.mnemonic))) &&
                           reloc_width != 1 && reloc_width != 2 &&
                           (track.x86_code_bits == 16u && machine == EM_386
                                ? x86_disp32_offset_code16(code, code_len, &disp_field_off)
                                : x86_disp32_offset(code, code_len, machine == EM_X86_64, &disp_field_off)) == 0) {
                    have_disp_field = 1;
                    reloc_width = 4;
                    if (machine == EM_X86_64 && t == R_X86_64_64) {
                        t = R_X86_64_32S;
                    }
                } else if (machine == EM_386 &&
                           (op->kind == AS_OPERAND_MEMORY ||
                            (operand_is_bare_address(op) && !is_rel_mnemonic(st->u.instr.mnemonic))) &&
                           x86_disp16_offset(code, code_len, track.x86_code_bits == 16u, &disp_field_off) == 0) {
                    /* `movl sym(%bx), %ecx`: two bytes, and their own
                     * relocation.  Four were written over the end of the
                     * instruction, the opcode among them. */
                    reloc_width = 2;
                    t = R_386_16;
                    have_disp_field = 1;
                } else if (machine == EM_386 && st->u.instr.operand_count == 2 && op->kind != AS_OPERAND_MEMORY &&
                           op == &st->u.instr.operands[st->u.instr.syntax_intel ? 0 : 1] &&
                           x86_far_pointer_offset(code, code_len, &disp_field_off)) {
                    /*
                     * A far jump or call to `$segment, $symbol`: the
                     * offset, of two bytes or four, is followed by the
                     * two of the segment.  The relocation was put on the
                     * last four bytes, over the segment, which was lost.
                     * (In Intel's `segment:symbol` the offset is the
                     * first of the two as they are kept, and for that
                     * one this was still so.)
                     */
                    reloc_width = code_len - 2 - disp_field_off;
                    t = reloc_width == 2 ? R_386_16 : R_386_32;
                    have_disp_field = 1;
                } else if (code_len == 4 && code[0] == 0xc8 && op == &st->u.instr.operands[0] &&
                           op->kind == AS_OPERAND_IMMEDIATE) {
                    /* `enter $sym, $1`: the frame's size is the two
                     * bytes behind the opcode, with one more behind
                     * them.  The relocation was of four, from the
                     * opcode on. */
                    disp_field_off = 1;
                    reloc_width = 2;
                    t = machine == EM_X86_64 ? R_X86_64_16 : R_386_16;
                    have_disp_field = 1;
                }
                /*
                 * A displacement is sign-extended to 64 bits, and its
                 * relocation says so -- but where the address is of 32
                 * bits it is not extended at all, and where lea leaves
                 * its result in less than 64 the top is thrown away:
                 * for both GNU as gives the relocation that takes any
                 * 32-bit value, signed or not.
                 */
                if (machine == EM_X86_64 && have_disp_field && t == R_X86_64_32S &&
                    x86_64_disp_reloc_is_unsigned(code, code_len, st->u.instr.mnemonic)) {
                    t = R_X86_64_32;
                }
                /*
                 * A field is behind an opcode: one that is as long as
                 * its instruction, or longer, is not a field of it.
                 * Such a relocation was cut to the instruction's length
                 * and written from its first byte -- `callw sym` was
                 * four zeros, the prefix and the opcode gone -- and is
                 * refused, on its line.
                 */
                if (!have_disp_field && code_len <= reloc_width + x86_prefix_count(code, code_len, machine == EM_X86_64)) {
                    set_err(ctx, "%s:%u: '%s': the instruction has no field of %u bytes for the symbol",
                            st->file != NULL ? st->file : "<input>", st->line,
                            st->u.instr.mnemonic != NULL ? st->u.instr.mnemonic : "", (unsigned)reloc_width);
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                if (t == R_386_PC8 || t == R_X86_64_PC8) {
                    addend += -1;
                } else if (t == R_386_PC16 || t == R_X86_64_PC16) {
                    addend += -2;
                } else if ((t == R_386_PC32 || t == R_386_PLT32 ||
                            t == R_X86_64_PC32 || t == R_X86_64_PLT32) &&
                           (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
                           is_rel_mnemonic(st->u.instr.mnemonic)) {
                    addend += -4;
                } else if (machine == EM_X86_64 &&
                           op->kind == AS_OPERAND_MEMORY &&
                           op->u.mem.base_reg != NULL &&
                           x86_base_is_ip(op->u.mem.base_reg)) {
                    addend += -4;
                    /* Less what follows the field as well: the address
                     * is counted from the end of the instruction. */
                    if (have_disp_field) {
                        addend -= (int64_t)(code_len - (disp_field_off + 4));
                    }
                }
                /*
                 * `addl $_GLOBAL_OFFSET_TABLE_, %ebx` is R_386_GOTPC: the
                 * table's address less the field's own.  But %ebx holds
                 * the address of the instruction, the thunk before it
                 * having put it there, so what must be added is the table
                 * less the instruction -- more by the field's place in
                 * the instruction, which is the addend GNU as gives it.
                 */
                if (((machine == EM_386 && t == R_386_32) ||
                     (machine == EM_X86_64 && !have_disp_field &&
                      (t == R_X86_64_64 || t == R_X86_64_32 || t == R_X86_64_32S))) &&
                    as_strcmp(sym, "_GLOBAL_OFFSET_TABLE_") == 0 && code_len >= reloc_width) {
                    addend += (int64_t)(code_len - reloc_width);
                }
                /* (An instruction may have two: an address and a number,
                 * `movl $a, b`.  Each has its own field, the address's
                 * found by where displacements go and the number's at
                 * the instruction's end.) */
                reloc_off = have_disp_field ? cur_off + (uint64_t)disp_field_off
                                            : cur_off + (uint64_t)code_len - reloc_width;
                if (is_label_difference) {
                    uint64_t code_rel_off = reloc_off - cur_off;
                    long long low = reloc_width >= 8 ? INT64_MIN : -(1ll << (reloc_width * 8 - 1));
                    long long high = reloc_width >= 8 ? INT64_MAX : (1ll << (reloc_width * 8)) - 1;

                    if (code_rel_off + reloc_width > (uint64_t)code_len || label_difference < low ||
                        label_difference > high) {
                        set_err(ctx, "%s:%u: the difference of labels, %lld, does not fit the %u bytes it has in '%s'",
                                st->file != NULL ? st->file : "<input>", st->line, label_difference,
                                (unsigned)reloc_width, st->u.instr.mnemonic);
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    write_u64_le_at(code + code_rel_off, (uint64_t)label_difference, (unsigned)reloc_width);
                    continue;
                }
                /*
                 * A symbol of elsewhere less a label of this section is
                 * the symbol's distance from the field, and the field's
                 * from the label, which is a number.  Less anything
                 * else there is no relocation for it.  The label was
                 * forgotten here too: `movl $ext-start, %eax` loaded
                 * the address of `ext`.
                 */
                if (taken_from != NULL) {
                    const char *label_section = "";
                    uint64_t label_off = 0;

                    /* (In four bytes, or in the eight of `movabs`, which
                     * is how code of the large model finds its table:
                     * `movabs $_GLOBAL_OFFSET_TABLE_-1b, %r11`.) */
                    if ((reloc_width != 4 && !(reloc_width == 8 && machine == EM_X86_64)) ||
                        find_label_virtual_location(ctx, NULL, NULL, 0, 0, taken_from, &label_section,
                                                    &label_off) != 0 ||
                        track.current == NULL || as_strcmp(label_section, track.current) != 0) {
                        set_err(ctx, "%s:%u: '%s' is taken from '%s' and is not a label of this section",
                                st->file != NULL ? st->file : "<input>", st->line, taken_from, sym);
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    t = machine == EM_X86_64 ? (reloc_width == 8 ? R_X86_64_PC64 : R_X86_64_PC32) : R_386_PC32;
                    addend = (int64_t)taken_value + (int64_t)(reloc_off - label_off);
                }
                if (dot_relative) {
                    if (reloc_width != 4 && !(reloc_width == 8 && machine == EM_X86_64)) {
                        set_err(ctx, "%s:%u: '%s' less '.' does not fit the %u bytes it has in '%s'",
                                st->file != NULL ? st->file : "<input>", st->line, sym, (unsigned)reloc_width,
                                st->u.instr.mnemonic);
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    /* (`.` was taken as the section's offset of the
                     * instruction, and the field is further on.) */
                    t = machine == EM_X86_64 ? (reloc_width == 8 ? R_X86_64_PC64 : R_X86_64_PC32) : R_386_PC32;
                    addend = (int64_t)taken_value + (int64_t)reloc_off;
                }
                if (x86_reloc_for_object(machine, obj_machine, &t) != 0) {
                    set_err(ctx, "%s:%u: '%s' needs a relocation that an %s object has not",
                            st->file != NULL ? st->file : "<input>", st->line,
                            st->u.instr.mnemonic != NULL ? st->u.instr.mnemonic : "",
                            obj_machine == EM_X86_64 ? "ELF64" : "ELF32");
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                if (machine_relocation_addend_is_in_place(obj_machine)) {
                    uint64_t code_rel_off = reloc_off - cur_off;
                    if (code_rel_off + reloc_width <= (uint64_t)code_len) {
                        write_u64_le_at(code + code_rel_off,
                                        (uint64_t)(addend + local_label_addend(ctx, sym)), (unsigned)reloc_width);
                    }
                } else {
                    uint64_t code_rel_off = reloc_off - cur_off;
                    if (code_rel_off + reloc_width <= (uint64_t)code_len) {
                        write_u64_le_at(code + code_rel_off, 0, (unsigned)reloc_width);
                    }
                }
                if (trace_env) {
                    fprintf(stderr, "as: reloc-trace   -> sym=%s type=%u off=0x%llx addend=%lld\n",
                            sym, (unsigned)t, (unsigned long long)reloc_off, (long long)addend);
                }
                /* An operator on what a branch goes to is @PLT or it is
                 * a mistake: `jmp foo@GOTOFF` was assembled. */
                if (is_rel_mnemonic(st->u.instr.mnemonic) && op->kind != AS_OPERAND_MEMORY &&
                    !(op->raw != NULL && op->raw[0] == '*')) {
                    uint32_t op_type = 0;

                    if (as_reloc_op_type(machine, rop, &op_type) == 1 && op_type != R_386_PLT32 &&
                        op_type != R_X86_64_PLT32) {
                        set_err(ctx, "%s:%u: '%s@%s' is not what a branch can go to",
                                st->file != NULL ? st->file : "<input>", st->line, sym, rop);
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                }
                /* An operator's relocation is of four bytes or eight,
                 * and an address of 16 bits has two for it: `movl
                 * sym@GOT(%bx), %eax` failed with no line named, and
                 * with a number in the instruction was assembled. */
                if (rop != NULL && reloc_width < 4) {
                    set_err(ctx, "%s:%u: '%s@%s' is a relocation of 4 bytes, and the field it is in has %u",
                            st->file != NULL ? st->file : "<input>", st->line, sym, rop, (unsigned)reloc_width);
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                /* The operators of thread-local storage belong to the
                 * few instructions a linker knows how to rewrite, none
                 * of which has a second symbol in it. */
                {
                    uint32_t op_type = 0;
                    int tls = as_reloc_op_type(machine, rop, &op_type) == 1 &&
                              (machine == EM_X86_64
                                   ? (op_type == R_X86_64_TLSGD || op_type == R_X86_64_TLSLD ||
                                      op_type == R_X86_64_GOTTPOFF || op_type == R_X86_64_GOTPC32_TLSDESC)
                                   : (op_type == R_386_TLS_GD || op_type == R_386_TLS_LDM ||
                                      op_type == R_386_TLS_IE_32 || op_type == R_386_TLS_GOTIE ||
                                      op_type == R_386_TLS_IE || op_type == R_386_TLS_GOTDESC));

                    if ((tls && rel_count > 0) || (tls_in_stmt && rel_count > 0)) {
                        set_err(ctx, "%s:%u: '%s%s%s' is in an instruction with another symbol, which none of its forms has",
                                st->file != NULL ? st->file : "<input>", st->line, sym, rop != NULL ? "@" : "",
                                rop != NULL ? rop : "");
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    tls_in_stmt |= tls;
                }
                reloc_got_load = x86_got_load_kind(code, code_len);
                if (add_reloc_for_symbol_op(ctx, cur_sec, sym, rop, reloc_off, t, addend) != 0) {
                    reloc_got_load = 0;
                    if (ctx->errbuf != NULL && ctx->errbuf[0] != '\0' && strchr(ctx->errbuf, ':') == NULL) {
                        char why[256];

                        snprintf(why, sizeof(why), "%s", ctx->errbuf);
                        set_err(ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, why);
                    }
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                reloc_got_load = 0;
                rel_count++;
            }
            /*
             * A branch to a number, `jmp 0x1234`: the number is an
             * address, and where this section will be is not known, so
             * the distance is the linker's to work out -- a PC-relative
             * relocation with no symbol.  It was encoded as the distance
             * from here with the section at address 0.
             */
            if (rel_count == 0 && (machine == EM_386 || machine == EM_X86_64) && track.x86_code_bits != 16u &&
                as_parse_is_direct_branch(st) && is_rel_mnemonic(st->u.instr.mnemonic) &&
                st->u.instr.operands[0].u.expr != NULL) {
                long long target;
                size_t width = 0;

                if (code_len == 2 && (code[0] == 0xeb || (code[0] >= 0x70 && code[0] <= 0x7f) ||
                                      (code[0] >= 0xe0 && code[0] <= 0xe3))) {
                    width = 1;
                } else if (code_len == 3 && code[0] == 0x67 && code[1] >= 0xe0 && code[1] <= 0xe3) {
                    /* jcxz in 32-bit code, jecxz in 64-bit: the count
                     * register of the other size. */
                    width = 1;
                } else if ((code_len == 5 && (code[0] == 0xe8 || code[0] == 0xe9)) ||
                           (code_len == 6 && code[0] == 0x0f && (code[1] & 0xf0) == 0x80)) {
                    width = 4;
                }
                if (width != 0 && eval_expr_const(st->u.instr.operands[0].u.expr, &target) == 0) {
                    int64_t addend = (int64_t)target - (int64_t)width;
                    uint32_t type = machine == EM_X86_64 ? (width == 1 ? R_X86_64_PC8 : R_X86_64_PC32)
                                                         : (width == 1 ? R_386_PC8 : R_386_PC32);

                    write_u64_le_at(code + code_len - width,
                                    machine_relocation_addend_is_in_place(machine) ? (uint64_t)addend : 0,
                                    (unsigned)width);
                    if (cur_sec == NULL ||
                        elf_add_relocation(cur_sec, cur_off + code_len - width, NULL, type, addend) != ELF_OK) {
                        set_err(ctx, "%s:%u: a relocation for a branch to an address could not be made",
                                st->file != NULL ? st->file : "<input>", st->line);
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                }
            }
            if (bytebuf_append(&sb->buf, code, code_len) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            sb->after_data = 0;
        }
    }

    /* The listing's lines: a statement's bytes run to where the next
     * statement of its section begins, or to the section's end. */
    if (list_sec != NULL) {
        size_t *next_off = (size_t *)malloc((secbufs.count + 1) * sizeof(*next_off));
        size_t *list_len = (size_t *)malloc(ctx->parsed->count * sizeof(*list_len));

        if (next_off != NULL && list_len != NULL) {
            for (i = 0; i < secbufs.count; ++i) {
                next_off[i] = secbufs.items[i].buf.len;
            }
            /* Backwards, for each to find its end; then told forwards. */
            for (i = ctx->parsed->count; i-- > 0;) {
                size_t end = next_off[list_sec[i]];

                list_len[i] = end > list_off[i] ? end - list_off[i] : 0;
                next_off[list_sec[i]] = list_off[i];
            }
            for (i = 0; i < ctx->parsed->count; ++i) {
                const sec_buf_t *b = &secbufs.items[list_sec[i]];
                const as_stmt_t *st = &ctx->parsed->items[i];

                ctx->cfg->list_stmt(ctx->cfg->list_cookie, st->file, st->line, (uint64_t)list_off[i],
                                    list_len[i] != 0 ? b->buf.data + list_off[i] : NULL, list_len[i]);
            }
        }
        free(next_off);
        free(list_len);
        free(list_sec);
        free(list_off);
        ctx->list_sec = NULL;
        ctx->list_off = NULL;
    }

    for (i = 0; i < secbufs.count; ++i) {
        elf_section_t *sec = section_for_name(ctx, secbufs.items[i].name);
        if (sec == NULL) {
            continue;
        }
        if (set_section_bytes(ctx, sec, secbufs.items[i].name, secbufs.items[i].buf.data,
                              secbufs.items[i].buf.len) != 0) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return 0;
}

/* A local label and the section it is defined in, to be looked up. */
typedef struct {
    const char *name; /* NULL for a numeric label, which is found by its line */
    unsigned line;
    int digit;
    size_t section;
    size_t stmt; /* the statement it is on */
} far_label_t;

/* The symbol a jump goes to, where it is written as a name and no
 * operator: the node of it, or NULL. */
static as_expr_t *jump_symbol_node(as_expr_t *e) {
    as_expr_t *found;

    if (e == NULL) {
        return NULL;
    }
    if (e->kind == AS_EXPR_SYMBOL) {
        return e->symbol != NULL && e->reloc_op == NULL ? e : NULL;
    }
    found = jump_symbol_node(e->lhs);
    return found != NULL ? found : jump_symbol_node(e->rhs);
}

static int far_label_cmp(const void *a, const void *b) {
    const far_label_t *x = (const far_label_t *)a;
    const far_label_t *y = (const far_label_t *)b;

    if ((x->name == NULL) != (y->name == NULL)) {
        return x->name == NULL ? -1 : 1;
    }
    if (x->name != NULL) {
        return as_strcmp(x->name, y->name);
    }
    if (x->line != y->line) {
        return x->line < y->line ? -1 : 1;
    }
    return x->digit - y->digit;
}

/* The local label a branch's operand names, alone or with something
 * added to it: `1b`, `.L5`, `1b+4`. */
static const as_expr_t *far_branch_label(const as_expr_t *e) {
    const as_expr_t *found;

    if (e == NULL) {
        return NULL;
    }
    if ((e->kind == AS_EXPR_LOCAL_REF && e->local_resolved) || expr_is_local_temp_symbol(e)) {
        return e;
    }
    found = far_branch_label(e->lhs);
    return found != NULL ? found : far_branch_label(e->rhs);
}

int as_elf_mark_far_branches(as_parse_result_t *parsed, const as_elf_cfg_t *cfg) {
    section_track_t track;
    sec_buf_vec_t names; /* for the sections' numbers only */
    far_label_t *labels = NULL;
    size_t label_count = 0, label_cap = 0;
    const char **exported = NULL;
    size_t exported_count = 0, exported_cap = 0;
    const char **weak = NULL;
    size_t weak_count = 0, weak_cap = 0;
    size_t *stmt_section = NULL;
    size_t i;
    int rc = -1;

    near_names_free();
    if (parsed == NULL || parsed->count == 0) {
        return 0;
    }
    memset(&names, 0, sizeof(names));
    if (section_track_init(&track, cfg != NULL && cfg->is_64 ? 64u
                                                             : (cfg != NULL && cfg->x86_code_bits == 16u ? 16u : 32u)) !=
        0) {
        return -1;
    }
    stmt_section = (size_t *)calloc(parsed->count, sizeof(*stmt_section));
    if (stmt_section == NULL) {
        goto out;
    }
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        sec_buf_t *sb = sec_buf_get_or_add(&names, track.current);
        size_t j;

        if (sb == NULL) {
            goto out;
        }
        stmt_section[i] = (size_t)(sb - names.items);
        for (j = 0; j < st->label_count; ++j) {
            const char *name = st->labels[j].name;
            int digit = -1;
            int numeric = numeric_local_label_number(name, &digit) == 0;

            if (label_count == label_cap) {
                size_t cap = label_cap != 0 ? label_cap * 2 : 64;
                far_label_t *grown = (far_label_t *)realloc(labels, cap * sizeof(*grown));

                if (grown == NULL) {
                    goto out;
                }
                labels = grown;
                label_cap = cap;
            }
            labels[label_count].name = numeric ? NULL : name;
            labels[label_count].line = numeric ? st->labels[j].line : 0;
            labels[label_count].digit = numeric ? digit : 0;
            labels[label_count].section = stmt_section[i];
            labels[label_count].stmt = i;
            label_count++;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            const as_directive_t *d = &st->u.directive;

            /* What another file may see, or replace, is not the
             * source's own. */
            if (as_strcmp(d->name, ".globl") == 0 || as_strcmp(d->name, ".global") == 0 || as_strcmp(d->name, ".weak") == 0) {
                for (j = 0; j < d->arg_count; ++j) {
                    /* (And those that may be replaced, apart.) */
                    if (as_strcmp(d->name, ".weak") == 0) {
                        if (weak_count == weak_cap) {
                            size_t cap = weak_cap != 0 ? weak_cap * 2 : 16;
                            const char **grown = (const char **)realloc(weak, cap * sizeof(*grown));

                            if (grown == NULL) {
                                goto out;
                            }
                            weak = grown;
                            weak_cap = cap;
                        }
                        weak[weak_count++] = d->args[j];
                    }
                    if (exported_count == exported_cap) {
                        size_t cap = exported_cap != 0 ? exported_cap * 2 : 64;
                        const char **grown = (const char **)realloc(exported, cap * sizeof(*grown));

                        if (grown == NULL) {
                            goto out;
                        }
                        exported = grown;
                        exported_cap = cap;
                    }
                    exported[exported_count++] = d->args[j];
                }
            }
            (void)section_track_apply_directive(&track, d);
        }
    }
    if (label_count != 0) {
        qsort(labels, label_count, sizeof(*labels), far_label_cmp);
    }
    /* The source's own labels, by name. */
    near_names_free();
    if (exported_count != 0) {
        qsort(exported, exported_count, sizeof(*exported), near_name_cmp);
    }
    if (weak_count != 0) {
        qsort(weak, weak_count, sizeof(*weak), near_name_cmp);
    }
    near_names = (char **)calloc(label_count != 0 ? label_count : 1, sizeof(*near_names));
    if (near_names == NULL) {
        goto out;
    }
    for (i = 0; i < label_count; ++i) {
        const char *name = labels[i].name;

        /* (Sorted by name, so one defined twice is twice together.) */
        if (name == NULL || is_local_temp_symbol_name(name) ||
            (near_name_count != 0 && as_strcmp(near_names[near_name_count - 1], name) == 0) ||
            (exported_count != 0 &&
             bsearch(&name, exported, exported_count, sizeof(*exported), near_name_cmp) != NULL)) {
            continue;
        }
        near_names[near_name_count] = xstrdup(name);
        if (near_names[near_name_count] == NULL) {
            goto out;
        }
        near_name_count++;
    }
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        const as_expr_t *e;
        far_label_t key;
        const far_label_t *found;

        /* (Nor does one before a far jump say anything.) */
        if (st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL && (cfg == NULL || cfg->target == 0) &&
            (strncasecmp(st->u.instr.mnemonic, "ljmp", 4) == 0 || strncasecmp(st->u.instr.mnemonic, "lcall", 5) == 0) &&
            (st->u.instr.prefixes & AS_PREFIX_SEG_OVERRIDE) != 0 && st->u.instr.operand_count != 0 &&
            st->u.instr.operands[0].kind != AS_OPERAND_MEMORY && !(cfg != NULL && cfg->is_64)) {
            free(st->u.instr.segment_override);
            st->u.instr.segment_override = NULL;
            st->u.instr.prefixes &= ~AS_PREFIX_SEG_OVERRIDE;
        }
        if (!as_parse_is_direct_branch(st)) {
            continue;
        }
        /*
         * A segment written before a jump or a call to a label says
         * nothing of where it goes.  %cs and %ds before a jump are the
         * hints of which way it will go, and are kept; any other, and
         * any before a call, GNU as leaves out, and so does this -- it
         * wrote them all.  (%es and %ss in 64-bit code are left for the
         * encoder to refuse.)
         */
        if ((st->u.instr.prefixes & AS_PREFIX_SEG_OVERRIDE) != 0 && st->u.instr.segment_override != NULL &&
            (cfg == NULL || cfg->target == 0)) {
            const char *seg = st->u.instr.segment_override;
            int hint = streq_ci(seg, "cs") || streq_ci(seg, "ds");
            int gone64 = cfg != NULL && cfg->is_64 && (streq_ci(seg, "es") || streq_ci(seg, "ss"));

            if (!gone64 && (is_call_mnemonic(st->u.instr.mnemonic) || !hint)) {
                free(st->u.instr.segment_override);
                st->u.instr.segment_override = NULL;
                st->u.instr.prefixes &= ~AS_PREFIX_SEG_OVERRIDE;
            }
        }
        /*
         * A jump to a label of its own section that the source exports
         * goes where the label is: another file may call the name, and
         * may not put something else where this one's code jumps to.
         * (A weak one it may; and a call or a `loop` of the name is the
         * linker's, as GNU as leaves them.)  The jump is made to go to
         * a name of the source's own, put on the label here: it was the
         * long form with a relocation, where the label was two bytes
         * away.
         */
        {
            as_expr_t *node = jump_symbol_node(st->u.instr.operands[0].u.expr);
            unsigned cc;
            char mnbuf[32];
            char suffix = '\0';

            if (node != NULL && !expr_is_local_temp_symbol(node) && as_strcmp(node->symbol, ".") != 0 &&
                normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) == 0 &&
                (streq_ci(mnbuf, "jmp") || x86_cond_code(mnbuf, &cc) == 0) &&
                (cfg == NULL || cfg->target == 0) && exported_count != 0 &&
                bsearch(&node->symbol, exported, exported_count, sizeof(*exported), near_name_cmp) != NULL &&
                (weak_count == 0 ||
                 bsearch(&node->symbol, weak, weak_count, sizeof(*weak), near_name_cmp) == NULL)) {
                far_label_t want;
                const far_label_t *at;

                memset(&want, 0, sizeof(want));
                want.name = node->symbol;
                at = label_count != 0
                         ? (const far_label_t *)bsearch(&want, labels, label_count, sizeof(*labels), far_label_cmp)
                         : NULL;
                if (at != NULL && at->section == stmt_section[i]) {
                    size_t len = strlen(node->symbol) + sizeof(".L.jump.");
                    char *alias = (char *)malloc(len);

                    if (alias == NULL) {
                        goto out;
                    }
                    snprintf(alias, len, ".L.jump.%s", node->symbol);
                    if (as_stmt_add_label(&parsed->items[at->stmt], alias) != 0) {
                        free(alias);
                        goto out;
                    }
                    free(node->symbol);
                    node->symbol = alias;
                    /* (Its section is the jump's, which is all the rest
                     * of this asks.) */
                    continue;
                }
            }
        }
        e = far_branch_label(st->u.instr.operands[0].u.expr);
        if (e == NULL) {
            continue;
        }
        memset(&key, 0, sizeof(key));
        if (e->kind == AS_EXPR_LOCAL_REF && e->local_resolved) {
            key.line = e->local_target_line;
            key.digit = e->local_digit;
        } else if (expr_is_local_temp_symbol(e)) {
            key.name = e->symbol;
        } else {
            continue;
        }
        found = label_count != 0
                    ? (const far_label_t *)bsearch(&key, labels, label_count, sizeof(*labels), far_label_cmp)
                    : NULL;
        if (found == NULL) {
            /* A .L label that the source does not define: no nearer
             * than a symbol of another file, which is what it is. */
            if (key.name != NULL) {
                st->u.instr.target_in_other_section = AS_TARGET_UNDEFINED;
            }
            continue;
        }
        if (found->section == stmt_section[i]) {
            continue;
        }
        st->u.instr.target_in_other_section = 1;
        if (as_parse_name_branch_label(st) != 0) {
            goto out;
        }
    }
    rc = 0;

out:
    free(labels);
    free(exported);
    free(weak);
    free(stmt_section);
    sec_buf_vec_free(&names);
    section_track_free(&track);
    return rc;
}

/*
 * Every instruction of the source that cannot be encoded, reported: the
 * first ended the run, and a source with three mistakes was assembled
 * three times to find them.  How many there are.
 */
static unsigned report_instruction_errors(emit_ctx_t *ctx) {
    section_track_t track;
    bytebuf_t scratch;
    unsigned count = 0;
    size_t i;

    if (ctx->cfg == NULL || ctx->cfg->report_error == NULL) {
        return 0;
    }
    memset(&scratch, 0, sizeof(scratch));
    if (section_track_init(&track, ctx->cfg->is_64 ? 64u : (ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return 0;
    }
    for (i = 0; i < ctx->parsed->count && count < AS_PARSE_ERRORS_MAX; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE) {
            (void)section_track_apply_directive(&track, &st->u.directive);
            continue;
        }
        if (st->kind != AS_STMT_INSTRUCTION || track.current == NULL) {
            continue;
        }
        scratch.len = 0;
        if (ctx->errbuf != NULL && ctx->errbuf_sz != 0) {
            ctx->errbuf[0] = '\0';
        }
        /*
         * A jump or a call is measured without being encoded, so what
         * is wrong with one that is no instruction at all -- `loop $1`,
         * `jmpw there`, `jcxz` in 64-bit code -- was found only where
         * it was written out, and the first such ended the run: of ten
         * of them one was reported.
         */
        if (ctx->cfg->target == 0 && st->u.instr.mnemonic != NULL && is_rel_mnemonic(st->u.instr.mnemonic)) {
            as_elf_cfg_t stmt_cfg = *ctx->cfg;
            char why[256];
            int need67 = 0;

            stmt_cfg.x86_code_bits = track.x86_code_bits;
            stmt_cfg.is_64 = track.x86_code_bits == 64u;
            why[0] = '\0';
            if (x86_fixed_short_branch(st->u.instr.mnemonic, track.x86_code_bits, &need67, NULL) == -2) {
                snprintf(why, sizeof(why), "'%s' is not an instruction of %u-bit code", st->u.instr.mnemonic,
                         track.x86_code_bits);
            } else if (!x86_stmt_refused(&stmt_cfg, st, why, sizeof(why)) &&
                       !x86_operand_classes_refused(&st->u.instr, st->u.instr.syntax_intel != 0, stmt_cfg.is_64, why,
                                                    sizeof(why))) {
                why[0] = '\0';
            }
            if (why[0] != '\0') {
                char said[384];

                snprintf(said, sizeof(said), "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, why);
                ctx->cfg->report_error(ctx->cfg->report_cookie, said);
                count++;
                continue;
            }
        }
        if (append_virtual_instruction_bytes(ctx, &scratch, track.current, st, track.x86_code_bits) != 0) {
            char generic[384];
            unsigned char code[32];
            size_t code_len = 0;
            char why[256];

            /* Why: the encoder says, asked for the instruction alone. */
            why[0] = '\0';
            (void)encode_x86_stmt_for_layout(ctx, track.current, 0, track.x86_code_bits, st, code, sizeof(code),
                                             &code_len, why, sizeof(why));
            if (why[0] != '\0') {
                snprintf(generic, sizeof(generic), "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line,
                         why);
            } else {
                snprintf(generic, sizeof(generic), "%s:%u: '%s' cannot be assembled",
                         st->file != NULL ? st->file : "<input>", st->line,
                         st->u.instr.mnemonic != NULL ? st->u.instr.mnemonic : "?");
            }
            ctx->cfg->report_error(ctx->cfg->report_cookie,
                                   ctx->errbuf != NULL && ctx->errbuf[0] != '\0' ? ctx->errbuf : generic);
            count++;
        }
    }
    if (ctx->errbuf != NULL && ctx->errbuf_sz != 0) {
        ctx->errbuf[0] = '\0';
    }
    free(scratch.data);
    section_track_free(&track);
    return count;
}

static void free_leb128_sizes(emit_ctx_t *ctx) {
    size_t i;

    for (i = 0; i < ctx->leb_count; ++i) {
        free(ctx->leb[i].arg_bytes);
    }
    free(ctx->leb);
    ctx->leb = NULL;
    ctx->leb_count = 0;
    for (i = 0; i < ctx->code_section_count; ++i) {
        free(ctx->code_sections[i]);
    }
    free(ctx->code_sections);
    ctx->code_sections = NULL;
    ctx->code_section_count = 0;
}

/*
 * Give each .uleb128 and .sleb128 that is left the bytes its arguments
 * take: one each to begin with, and more wherever a value does not fit,
 * until none grows.  A size only grows, so it ends.  What had been
 * worked out of the layout stood on the sizes before, and is forgotten
 * each time one changes.
 */
static int settle_leb128_sizes(emit_ctx_t *ctx) {
    size_t count = 0;
    size_t i;
    int pass;

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL && st->u.directive.arg_count != 0 &&
            (as_strcmp(st->u.directive.name, ".uleb128") == 0 || as_strcmp(st->u.directive.name, ".sleb128") == 0)) {
            count++;
        }
    }
    if (count == 0) {
        return 0;
    }
    ctx->leb = (struct leb_stmt *)calloc(count, sizeof(*ctx->leb));
    if (ctx->leb == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL && st->u.directive.arg_count != 0 &&
            (as_strcmp(st->u.directive.name, ".uleb128") == 0 || as_strcmp(st->u.directive.name, ".sleb128") == 0)) {
            struct leb_stmt *leb = &ctx->leb[ctx->leb_count];

            leb->arg_bytes = (unsigned char *)malloc(st->u.directive.arg_count);
            if (leb->arg_bytes == NULL) {
                return -1;
            }
            memset(leb->arg_bytes, 1, st->u.directive.arg_count);
            leb->st = st;
            ctx->leb_count++;
        }
    }
    for (pass = 0; pass < LEB128_MAX_BYTES * 8; ++pass) {
        int grew = 0;

        for (i = 0; i < ctx->leb_count; ++i) {
            struct leb_stmt *leb = &ctx->leb[i];
            const as_directive_t *d = &leb->st->u.directive;
            unsigned bits = ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 32u;
            const char *section = "";
            uint64_t off = 0;
            int placed = 0;
            uint64_t done = 0;
            size_t j;

            for (j = 0; j < d->arg_count; done += leb->arg_bytes[j], ++j) {
                unsigned char bytes[LEB128_MAX_BYTES];
                long long v = 0;
                size_t n;

                if (d->args[j] == NULL) {
                    continue;
                }
                if (eval_leb128_arg(ctx, NULL, leb->st, 0, bits, d->args[j], &v) != 0) {
                    /* Not two labels: something of `.`, which wants the
                     * place of the directive.  One that cannot be worked
                     * out at all is said so where the bytes are written. */
                    if (!placed) {
                        section = "";
                        (void)find_stmt_virtual_location(ctx, leb->st, &section, &off);
                        placed = 1;
                    }
                    if (section[0] == '\0' ||
                        eval_leb128_arg(ctx, section, leb->st, off + done, bits, d->args[j], &v) != 0) {
                        continue;
                    }
                }
                n = as_leb128_encode(v, d->name[1] == 's', bytes);
                if (n > leb->arg_bytes[j]) {
                    leb->arg_bytes[j] = (unsigned char)n;
                    grew = 1;
                }
            }
        }
        if (!grew) {
            break;
        }
        virtual_label_cache_free(ctx);
        if (ctx->stmt_size_cached != NULL) {
            memset(ctx->stmt_size_cached, 0, ctx->stmt_size_cache_count);
        }
        if (ctx->branch_short != NULL) {
            memset(ctx->branch_short, 0, ctx->branch_short_count);
        }
        ctx->relax_state = 0;
        for (i = 0; i < ctx->section_prefix_count; ++i) {
            free(ctx->section_prefixes[i].name);
            free(ctx->section_prefixes[i].prefix);
        }
        ctx->section_prefix_count = 0;
    }
    return 0;
}

int as_elf_emit_file(const as_parse_result_t *parsed,
                     const as_section_state_t *sections,
                     const as_symtab_t *symtab,
                     const as_elf_cfg_t *cfg,
                     const char *out_path,
                     char *errbuf,
                     size_t errbuf_sz) {
    emit_ctx_t ctx;
    elfobj_class_t cls;
    size_t i;
    int has_file_loc = 0;
    int has_cfi = 0;
    int trace_emit_phase = getenv("AS_DEBUG_EMIT_PHASE") != NULL;
    int emitted = -1; /* what is returned: 0 once the object is written */

    if (parsed == NULL || sections == NULL || symtab == NULL || cfg == NULL || out_path == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.parsed = parsed;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (fold_absolute_operands(&ctx) != 0) {
        set_err(&ctx, "out of memory");
        return -1;
    }

    ctx.sections = sections;
    ctx.symtab = symtab;
    cls = cfg->is_64 ? ELFOBJ_CLASS_64 : ELFOBJ_CLASS_32;
    ctx.obj = elf_create(ET_REL, (uint16_t)(cfg->target != 0 ? cfg->target : cfg->machine), cls, ELFOBJ_ENDIAN_LE);
    if (ctx.obj == NULL) {
        set_err(&ctx, "elf_create failed");
        return -1;
    }
    /* An ARM object says which ABI it is of, and a linker asks. */
    if (cfg->target == EM_ARM) {
        (void)elf_set_flags(ctx.obj, EF_ARM_ABI_VER5);
    }

    for (i = 0; i < sections->count; ++i) {
        const as_section_t *s = &sections->items[i];
        elf_section_t *es = elf_add_section(ctx.obj, s->name, s->type, s->flags);
        if (es == NULL) {
            set_err(&ctx, "failed to add section %s", s->name);
            goto fail;
        }
        section_remember(&ctx, s->name, es);
        {
            unsigned long long align = s->align > 0 ? s->align : 1;

            /* The instructions of ARM and AArch64 are words, and stand
             * where a word may wherever the linker puts their section. */
            if (cfg->target != 0 && (s->flags & SHF_EXECINSTR) != 0 && align < 4) {
                align = 4;
            }
            if (elf_section_set_align(es, align) != ELF_OK) {
                set_err(&ctx, "failed to set align on %s", s->name);
                goto fail;
            }
        }
        if ((s->flags & SHF_MERGE) != 0 && s->entsize != 0 &&
            elf_section_set_merge(es, s->entsize, (s->flags & SHF_STRINGS) != 0) != ELF_OK) {
            set_err(&ctx, "failed to set merge metadata on %s", s->name);
            goto fail;
        }
        if (s->group != NULL) {
            if (elf_section_set_group(es, section_group_ordinal(sections, i), s->comdat) != ELF_OK ||
                elf_section_set_group_signature(es, s->group) != ELF_OK) {
                set_err(&ctx, "failed to set group on %s", s->name);
                goto fail;
            }
        }

        if (as_strcmp(s->name, ".text") == 0 && s->subsection == 0) {
            ctx.text_sec = es;
        }
        if (as_strcmp(s->name, ".data") == 0 && s->subsection == 0) {
            ctx.data_sec = es;
        }
    }

    if (ctx.data_sec == NULL) {
        ctx.data_sec = elf_add_section(ctx.obj, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
        if (ctx.data_sec == NULL || elf_section_set_align(ctx.data_sec, 4) != ELF_OK) {
            set_err(&ctx, "failed to create .data");
            goto fail;
        }
    }
    if (ctx.text_sec == NULL) {
        ctx.text_sec = elf_add_section(ctx.obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
        if (ctx.text_sec == NULL || elf_section_set_align(ctx.text_sec, 16) != ELF_OK) {
            set_err(&ctx, "failed to create .text");
            goto fail;
        }
    }

    if (settle_leb128_sizes(&ctx) != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory");
        goto fail;
    }
    if (report_instruction_errors(&ctx) != 0 || cfg->check_only) {
        if (errbuf != NULL && errbuf_sz != 0) {
            errbuf[0] = '\0';
        }
        goto fail;
    }
    if (emit_text_program(&ctx) != 0) {
        if (ctx.errbuf == NULL || ctx.errbuf[0] == '\0') {
            set_err(&ctx, "failed to emit text instructions");
        }
        goto fail;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase text done\n");

    if (emit_data_sections(&ctx) != 0) {
        if (ctx.errbuf == NULL || ctx.errbuf[0] == '\0') {
            set_err(&ctx, "failed to emit data program");
        }
        goto fail;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase data done\n");

    if (emit_file_symbols_and_comment(&ctx) != 0) {
        if (ctx.errbuf == NULL || ctx.errbuf[0] == '\0') {
            set_err(&ctx, "failed to emit the file symbols and .comment");
        }
        goto fail;
    }
    if (emit_symbols(&ctx, symtab) != 0) {
        if (ctx.errbuf == NULL || ctx.errbuf[0] == '\0') {
            set_err(&ctx, "failed to emit symbols");
        }
        goto fail;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase symbols done\n");

    if (emit_relocations(&ctx) != 0) {
        if (ctx.errbuf == NULL || ctx.errbuf[0] == '\0') {
            set_err(&ctx, "failed to emit relocations");
        }
        goto fail;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase relocations done\n");

    if (ensure_section_exists(&ctx, ".note.GNU-stack", SHT_PROGBITS, 0, 1, NULL, 0) != 0) {
        set_err(&ctx, "failed to emit .note.GNU-stack");
        goto fail;
    }

    collect_directive_presence(parsed, &has_file_loc, &has_cfi);
    /* `.file N` and `.loc` are made the line table of `.debug_line`. */
    if (has_file_loc && check_file_numbers(&ctx, parsed) != 0) {
        goto fail;
    }
    /* With -g the source's own instructions are the rows -- unless it
     * has rows of its own, or is a compiler's output with its debugging
     * information written out, which -g then adds nothing to: it was
     * refused, as writing `.debug_line` twice. */
    ctx.line_own = cfg->line_own_source && !source_has_loc(parsed) && !source_has_debug_sections(parsed);
    ctx.line_rows_valid = 0;
    if ((has_file_loc || ctx.line_own) && emit_line_table(&ctx, parsed) != 0) {
        goto fail;
    }
    if (has_cfi && emit_call_frames(&ctx, parsed) != 0) {
        goto fail;
    }

    if (elf_finalize(ctx.obj) != ELF_OK) {
        set_err(&ctx, "elf_finalize failed");
        goto fail;
    }
    /*
     * As GNU as: what the name leads to is removed if it is a plain
     * file, and the name then written.  So a link to a file that is
     * there is replaced by the object -- the file left as it was -- and
     * one to a file that is not there yet has the file made.
     */
    {
        struct stat sb;

        if (stat(out_path, &sb) == 0 && S_ISREG(sb.st_mode)) {
            (void)unlink(out_path);
        }
    }
    errno = 0;
    if (elf_write_file(ctx.obj, out_path) != ELF_OK) {
        /* The path, and what the system said of it. */
        set_err(&ctx, "can't create %s: %s", out_path, errno != 0 ? strerror(errno) : "the object could not be written");
        goto fail;
    }

    emitted = 0;

fail:
    /* What the context holds is given back here, whichever way the
     * function ends. */
    for (i = 0; i < ctx.sym_count; ++i) {
        free(ctx.sym_map[i].name);
    }
    free(ctx.sym_map);
    free(ctx.sym_index);
    asm_var_reset(&ctx);
    virtual_label_cache_free(&ctx);
    free(ctx.list_sec);
    free(ctx.list_off);
    free(ctx.label_index);
    free(ctx.stmt_size_cache);
    free(ctx.stmt_size_cached);
    free(ctx.stmt_code);
    text_code_free();
    seen_labels_free();
    free(ctx.branch_short);
    free(ctx.emit_labels);
    free_leb128_sizes(&ctx);
    free(ctx.line_rows);
    as_strmap_free(&ctx.line_views);
    as_strmap_free(&ctx.line_view_names);
    for (i = 0; i < ctx.line_view_name_count; ++i) {
        free(ctx.line_view_name_list[i]);
    }
    free(ctx.line_view_name_list);
    {
        size_t _i;
        for (_i = 0; _i < ctx.kept_name_count; ++_i) {
            free(ctx.kept_names[_i]);
        }
        free(ctx.kept_names);
        as_strmap_free(&ctx.kept_by_name);
        as_strmap_free(&ctx.sec_by_name);
        free(ctx.sec_found);
        as_strmap_free(&ctx.symtab_by_name);
        for (_i = 0; _i < ctx.section_prefix_count; ++_i) {
            free(ctx.section_prefixes[_i].name);
            free(ctx.section_prefixes[_i].prefix);
        }
        free(ctx.section_prefixes);
        for (_i = 0; _i < ctx.stmt_section_at_count; ++_i) {
            free(ctx.stmt_section_at[_i]);
        }
        free(ctx.stmt_section_at);
    }
    elf_close(ctx.obj);
    return emitted;
}
