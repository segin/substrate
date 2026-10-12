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

static int parse_ymm_reg(const char *name, unsigned *out);

static int parse_zmm_reg(const char *name, unsigned *out);

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

static void write_u64_le_at(unsigned char *p, uint64_t v, unsigned width) {
    unsigned i;

    for (i = 0; i < width; ++i) {
        p[i] = (unsigned char)((v >> (i * 8)) & 0xffu);
    }
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

static int parse_ymm_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_YMM, 15, out);
}

static int parse_zmm_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_ZMM, 31, out);
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
            width = directive_fixed_scalar_width(d);

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
