#include "as_elf_emit.h"
#include "as_arm.h"
#include "as_x86_encode.h"
#include "as_x86_regs.h"
#include "as_reloc_op.h"
#include "as_cfi.h"
#include "as_dwline.h"
#include "as_x86_vec.h"
#include "as_x86_vex.h"
#include "as_x86_avx.h"
#include "as_x86_avx2.h"
#include "as_x86_bmi1.h"
#include "as_x86_bmi2.h"
#include "as_x86_fma.h"
#include "as_x86_avx512f.h"
#include "as_x86_avx512bw.h"
#include "as_x86_avx512dq.h"
#include "as_x86_sse3.h"
#include "as_x86_ssse3.h"
#include "as_x86_sse41.h"
#include "as_x86_sse42.h"
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

typedef struct {
    char *name;
    elf_symbol_t *sym;
} emit_sym_t;

typedef struct {
    char *name;
    char *file;
    unsigned line;
    int digit;
    char *section;
    uint64_t off;
    const as_stmt_t *stmt;
    /* What it is found by besides its name: where it is written, for a
     * numbered label, and the statement it is. */
    char *place_key;
    char *stmt_key;
} virtual_label_cache_t;

typedef struct {
    char *name;
    long long value;
} asm_var_t;

/* The longest LEB128 of 64 bits. */
#define LEB128_MAX_BYTES 10

struct leb_stmt {
    const as_stmt_t *st;
    unsigned char *arg_bytes; /* for each argument, the bytes it takes */
};

/* A row of the line table: what a `.loc` said, where it took effect,
 * and the name it gave its view, if it gave one. */
struct line_row {
    as_dwline_row_t row;
    char view[48];
};

typedef struct {
    char *name;
    char *reg;
} asm_reg_alias_t;

/* A label's name, which is the parse's, and the statement it is on. */
typedef struct {
    const char *name;
    size_t stmt;
} label_index_t;

/*
 * An instruction's bytes, kept: the statement at this offset of its
 * section, in this code and this layout, is these.  The passes after
 * the first -- the labels' places, the text, the relocations -- each
 * encoded every statement again, five times in all.
 */
struct stmt_code {
    uint64_t off;
    unsigned epoch;
    unsigned char bits;
    unsigned char len;      /* 0: not kept */
    unsigned char anywhere; /* the same wherever it stands */
    unsigned char bytes[17];
};

/*
 * And by what was written: an instruction of registers and numbers is
 * the same bytes each time it is written the same way, and a compiler
 * writes a few hundred of them many thousands of times.  One slot of a
 * table that is open-addressed by the hash of the key, which is the
 * statement's text and what else it is assembled by.
 */
struct text_code {
    char *key; /* NULL: an empty slot */
    uint32_t hash;
    unsigned char len;
    unsigned char bytes[17];
};

typedef struct {
    const as_elf_cfg_t *cfg;
    const as_parse_result_t *parsed;
    /* What each section was declared to be; NULL where it is not known. */
    const as_section_state_t *sections;
    const as_symtab_t *symtab; /* NULL where no object is written */
    elfobj_t *obj;
    elf_section_t *text_sec;
    elf_section_t *data_sec;
    emit_sym_t *sym_map;
    size_t sym_count;
    size_t sym_cap;
    size_t *sym_index;
    size_t sym_index_cap;
    virtual_label_cache_t *vlabel_cache;
    size_t vlabel_count;
    size_t vlabel_cap;
    /* The same by name (the first of a name), by place and by
     * statement (the last): it was searched from end to end for each,
     * and a compiler's debugging sections ask a hundred thousand times. */
    as_strmap_t vlabel_by_name;
    as_strmap_t vlabel_by_place;
    as_strmap_t vlabel_by_stmt;
    asm_var_t *asm_vars;
    size_t asm_var_count;
    size_t asm_var_cap;
    asm_reg_alias_t *asm_reg_aliases;
    size_t asm_reg_alias_count;
    size_t asm_reg_alias_cap;
    int virtual_scanning;
    /* ARM and AArch64: what the encoder last gave, for the fields of it
     * that relocations fill. */
    as_arm_out_t arm_out;
    /* The sections that are not flagged as code and have instructions
     * written in them.  See section_name_is_executable(). */
    char **code_sections;
    size_t code_section_count;
    int code_sections_found;
    /* The .uleb128 and .sleb128 statements whose arguments are distances
     * between labels, in the order of the source, and the bytes each
     * argument takes.  See settle_leb128_sizes(). */
    struct leb_stmt *leb;
    size_t leb_count;
    /* The rows of the line table, as the layout now is, and the rows
     * that `.loc ... view NAME` gave a name to.  See line_rows(). */
    struct line_row *line_rows;
    size_t line_row_count;
    int line_rows_valid;
    int line_rows_busy;
    /* -g was given and the source has no `.loc`: each instruction is a
     * row of the line table, of its own line. */
    int line_own;
    as_strmap_t line_views;
    as_strmap_t line_view_names;
    char **line_view_name_list;
    size_t line_view_name_count;
    int line_view_names_made;
    /* Per-statement size cache for the conservative virtual-size path.
     * Linux's syscall_32.s drove ~13k stmts × thousands of branch range
     * queries, each redoing the full encode_x86_stmt for every entry —
     * an O(N^2) blow-up that wedged the build for minutes. Sizes
     * computed below the precise (top-level) sizing path are
     * offset-independent, so we memoise them by parsed-stmt index. */
    /* For a listing, while the bytes are written: freed with the rest. */
    size_t *list_sec;
    size_t *list_off;
    /* Every label and the statement it is on, by name: label_stmt_index(). */
    label_index_t *label_index;
    size_t label_index_count;
    int label_index_made;
    size_t *stmt_size_cache;
    unsigned char *stmt_size_cached;
    size_t stmt_size_cache_count;
    /* What each statement was encoded to, where and in which layout:
     * encode_x86_stmt_for_layout(). */
    struct stmt_code *stmt_code;
    size_t stmt_code_count;
    unsigned layout_epoch;
    /* The bytes of each instruction of registers and numbers that has
     * been written, by its text: text_code_find(). */
    struct text_code *text_code;
    size_t text_code_cap;
    size_t text_code_count;
    /* Whether each branch to a label of its own section is the short
     * form: 0 not one that is sized here, 1 short, 2 long.  Made by
     * relax_branches(), which relax_state says the state of: 0 not yet
     * run, 1 running, 2 done. */
    unsigned char *branch_short;
    size_t branch_short_count;
    int relax_state;
    /* Set while a directive is given its bytes for good, if the last
     * bytes of its section were data: see append_x86_nops(). */
    int pad_after_data;
    /* The local labels emit_relocations() has passed, which it frees
     * here so that each way out of it need not. */
    void *emit_labels;
    /* Section names, kept: see section_name_keep(). */
    char **kept_names;
    size_t kept_name_count;
    size_t kept_name_cap;
    as_strmap_t kept_by_name;
    /* The object's sections that have been asked for by name
     * (section_for_name), and the symbols of the source by theirs. */
    as_strmap_t sec_by_name;
    elf_section_t **sec_found;
    size_t sec_found_count;
    size_t sec_found_cap;
    as_strmap_t symtab_by_name;
    int symtab_indexed;
    /* Per-section prefix-sum table for O(1) range-size queries during
     * branch sizing. range_layout[s].prefix[i] is the sum of all
     * conservative stmt sizes in section `name` for stmts j < i. The
     * builder runs once per (section, code_bits) tuple and walks the
     * full parse, so subsequent stmt_range_size_in_section calls reduce
     * to prefix[end] - prefix[start]. Without this Linux's branch-heavy
     * .s files (~13k stmts, ~1k branches) drive O(B*N) range walks. */
    struct as_section_prefix_s {
        char *name;
        unsigned x86_code_bits;
        uint64_t *prefix;
        size_t prefix_count;
    } *section_prefixes;
    size_t section_prefix_count;
    size_t section_prefix_cap;
    int section_prefix_building;
    /* Per-stmt effective section name. Computed once via the same
     * section_track linear walk so stmt_declared_label_section is O(1)
     * instead of O(N). Indexed by parsed-stmt index. NULL string means
     * "no .text/.data found" (very rare). */
    char **stmt_section_at;
    size_t stmt_section_at_count;
    char *errbuf;
    size_t errbuf_sz;
} emit_ctx_t;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} bytebuf_t;

/* `section` is a name kept by section_name_keep(): of any length, and
 * good for as long as the object is being made. */
typedef struct {
    int has_section;
    const char *section;
    long long value;
} virtual_addr_value_t;

static const char *first_symbol_in_expr(const as_expr_t *e);
static int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out);
static as_x86_seg_t map_seg(const char *s);
static int emit_seg_override_byte(unsigned char *out, size_t out_cap, size_t *pos_io, const char *segment_reg);
static int x86_cond_code(const char *mnemonic, unsigned *cc_out);
static int x86_base_is_ip(const char *base_reg);
static int stmt_is_place_free(const as_stmt_t *st);
static int stmt_code_find(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits, int anywhere,
                          unsigned char *code, size_t code_cap, size_t *code_len);
static void stmt_code_keep(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                           const unsigned char *code, size_t code_len);
static int encode_x86_stmt_by_text(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, unsigned x86_code_bits,
                                   const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                                   char *encerr, size_t encerr_sz);
static const char *first_symbol_op_in_expr(const as_expr_t *e);
static int x86_disp16_offset(const unsigned char *code, size_t len, int code16, size_t *off_out);
static int x86_disp32_offset(const unsigned char *code, size_t len, int is_64, size_t *off_out);
static int append_incbin_to_bytebuf(emit_ctx_t *ctx, const as_stmt_t *st, bytebuf_t *buf, const as_directive_t *d);
static int section_name_is_executable(emit_ctx_t *ctx, const char *name);
static int is_numeric_local_label_name(const char *name);
static int numeric_local_label_number(const char *name, int *out);
static int append_directive_data_ctx(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name,
                                     const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                                     const as_directive_t *d);
static int encode_x86_stmt_for_layout(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
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
static int parse_x86_reg(const char *name, as_x86_reg_t *out);
static int build_stmt_section_at(emit_ctx_t *ctx);
static const uint64_t *get_section_prefix_sums(emit_ctx_t *ctx, const char *section_name,
                                               unsigned x86_code_bits);
static int parse_xmm_reg(const char *name, unsigned *out);
static int parse_ymm_reg(const char *name, unsigned *out);
static int parse_zmm_reg(const char *name, unsigned *out);
static int parse_mmx_reg(const char *name, unsigned *out);
static int convert_operand_x86(const as_operand_t *op, const char *mnemonic, as_x86_operand_t *dst, int is64,
                               int intel_syntax, char *errbuf, size_t errbuf_sz);

/*
 * The pseudo-prefixes of the statement being encoded, for
 * convert_operand_x86(), which is given an operand and not its
 * statement: {disp8} and {disp32} are about the operand.  Set by
 * encode_x86_stmt() as it begins.
 */
static unsigned x86_stmt_pseudo;
static int is_local_temp_symbol_name(const char *name);
static int expr_is_local_temp_symbol(const as_expr_t *e);

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
    return strcmp(*(const char *const *)a, *(const char *const *)b);
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
static int expr_is_local_temp_symbol(const as_expr_t *e) {
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
        if (v->name != NULL && strcmp(v->name, name) == 0) {
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
        if (ctx->asm_vars[i].name != NULL && strcmp(ctx->asm_vars[i].name, name) == 0) {
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
        if (a->name != NULL && strcmp(a->name, name) == 0) {
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
        if (ctx->asm_reg_aliases[i].name != NULL && strcmp(ctx->asm_reg_aliases[i].name, name) == 0) {
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

static const as_operand_t *x86_operand_with_reg_alias(const emit_ctx_t *ctx, const as_operand_t *op, as_operand_t *tmp) {
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
            strcmp(sections->items[i].group, sections->items[index].group) == 0) {
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
            if (sections->items[j].group != NULL && strcmp(sections->items[j].group, group) == 0) {
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

static int bytebuf_append(bytebuf_t *b, const void *p, size_t n) {
    if (bytebuf_reserve(b, n) != 0) {
        return -1;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    return 0;
}

static int bytebuf_append_zeros(bytebuf_t *b, size_t n) {
    if (bytebuf_reserve(b, n) != 0) {
        return -1;
    }
    memset(b->data + b->len, 0, n);
    b->len += n;
    return 0;
}

static int bytebuf_append_u64_le(bytebuf_t *b, uint64_t v, unsigned width) {
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

static int x86_mnemonic_keeps_trailing_size_letter(const char *mnemonic) {
    static const char *const exact[] = {
        "movaps", "movups", "movapd", "movupd", "movdqa", "movdqu",
        "paddb", "paddw", "paddd", "paddq",
        "psubb", "psubw", "psubd", "psubq",
        "psllw", "pslld", "psllq", "psrlw", "psrld", "psrlq",
        "psraw", "psrad", "pxor", "por", "pand", "pandn",
        "pshufd", "pmullw", "pmulhw", "pmulhuw", "pmuludq",
        "paddsb", "paddsw", "paddusb", "paddusw",
        "psubsb", "psubsw", "psubusb", "psubusw",
        "pminub", "pminsw", "pmaxub", "pmaxsw",
        "pavgb", "pavgw", "pmaddwd", "psadbw"
    };
    size_t i;

    if (mnemonic == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(exact) / sizeof(exact[0]); ++i) {
        if (strcmp(mnemonic, exact[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Whether a directive has an argument at this place that is not empty
 * (`.p2align 4,,10` has an empty second). */
static int directive_has_arg(const as_directive_t *d, size_t i) {
    return d != NULL && i < d->arg_count && d->args[i] != NULL && d->args[i][0] != '\0';
}

/*
 * The third argument of .align, .balign and .p2align: the most padding
 * that is wanted.  If reaching the alignment takes more, there is to be
 * none.  Returns 1 if `need` bytes are too many, 0 if not or if there is
 * no limit, -1 if the limit is not a number.
 */
static int align_exceeds_max_skip(const as_directive_t *d, size_t need) {
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
static int append_align_fill(bytebuf_t *buf, const as_directive_t *d, size_t need, uint64_t fill) {
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
 * Padding between instructions: `need` bytes that do nothing, as GNU as
 * 2.46 writes them.  One long NOP runs faster than as many one-byte
 * ones, so the padding is as few instructions as there are: the odd
 * length first, then the longest NOP the mode has, over and over.  Past
 * a number of those -- two, or seven with the NOPs of a 64-bit object --
 * a jump over the padding comes first, so that none of it is run.
 *
 * The NOPs are of the object, not of the mode: `.code32` in a 64-bit
 * object pads with the 64-bit ones.  16-bit code has its own.
 */
static int append_x86_nops(bytebuf_t *buf, size_t need, unsigned code_bits, int is_64_object) {
    static const unsigned char of_16[][5] = {
        {0x90}, {0x89, 0xf6}, {0x8d, 0x74, 0x00}, {0x8d, 0xb4, 0x00, 0x00}, {0x2e, 0x8d, 0xb4, 0x00, 0x00}};
    static const unsigned char of_32[][8] = {
        {0x90},
        {0x66, 0x90},
        {0x8d, 0x76, 0x00},
        {0x8d, 0x74, 0x26, 0x00},
        {0x2e, 0x8d, 0x74, 0x26, 0x00},
        {0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00},
        {0x8d, 0xb4, 0x26, 0x00, 0x00, 0x00, 0x00},
        {0x2e, 0x8d, 0xb4, 0x26, 0x00, 0x00, 0x00, 0x00}};
    static const unsigned char of_64[][11] = {
        {0x90},
        {0x66, 0x90},
        {0x0f, 0x1f, 0x00},
        {0x0f, 0x1f, 0x40, 0x00},
        {0x0f, 0x1f, 0x44, 0x00, 0x00},
        {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00},
        {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00},
        {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}};
    const unsigned char *patterns;
    size_t longest;
    size_t stride;
    size_t most;
    size_t odd;

    if (code_bits == 16) {
        patterns = &of_16[0][0];
        longest = sizeof(of_16) / sizeof(of_16[0]);
        stride = sizeof(of_16[0]);
        most = 2;
    } else if (is_64_object) {
        patterns = &of_64[0][0];
        longest = sizeof(of_64) / sizeof(of_64[0]);
        stride = sizeof(of_64[0]);
        most = 7;
    } else {
        patterns = &of_32[0][0];
        longest = sizeof(of_32) / sizeof(of_32[0]);
        stride = sizeof(of_32[0]);
        most = 2;
    }
    if (need / longest > most) {
        if (need - 2 <= 127) {
            need -= 2;
            if (bytebuf_append_u64_le(buf, 0xeb, 1) != 0 || bytebuf_append_u64_le(buf, (uint64_t)need, 1) != 0) {
                return -1;
            }
        } else {
            if (code_bits == 16) {
                if (bytebuf_append_u64_le(buf, 0x66, 1) != 0) {
                    return -1;
                }
                need -= 1;
            }
            need -= 5;
            if (bytebuf_append_u64_le(buf, 0xe9, 1) != 0 || bytebuf_append_u64_le(buf, (uint64_t)need, 4) != 0) {
                return -1;
            }
        }
    }
    odd = need % longest;
    if (odd != 0 && bytebuf_append(buf, patterns + (odd - 1) * stride, odd) != 0) {
        return -1;
    }
    for (need -= odd; need != 0; need -= longest) {
        if (bytebuf_append(buf, patterns + (longest - 1) * stride, longest) != 0) {
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
static int parse_int64(const char *s, long long *out) {
    return as_expr_parse_number(s, out);
}

/* Text that is an expression of numbers alone. */
static int parse_const_expr_string(const char *s, long long *out) {
    if (s == NULL || out == NULL) {
        return -1;
    }
    return as_expr_eval_string(s, NULL, NULL, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

/* An expression of numbers alone. */
static int eval_expr_const(const as_expr_t *e, long long *out) {
    return as_expr_eval(e, NULL, NULL, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

/* What .set and = have given a value to; the location counter is not
 * among them. */
static int asm_var_expr_lookup(void *cookie, const char *name, long long *out) {
    if (strcmp(name, ".") == 0) {
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

static int eval_arg_asm_vars(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, long long *out) {
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
        (strcmp(d->name, ".set") != 0 && strcmp(d->name, ".equ") != 0 && strcmp(d->name, ".equiv") != 0) ||
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

/* Is the operand of this instruction where it goes to, not what it reads? */
static int mnemonic_takes_a_target(const char *mn) {
    if (mn == NULL) {
        return 0;
    }
    return mn[0] == 'j' || mn[0] == 'J' || strncasecmp(mn, "call", 4) == 0 || strncasecmp(mn, "loop", 4) == 0 ||
           strncasecmp(mn, "xbegin", 6) == 0;
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
static int fold_absolute_operands(emit_ctx_t *ctx) {
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

static int expr_has_symbol(const as_expr_t *e) {
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

static int expr_has_local_ref(const as_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_LOCAL_REF) {
        return 1;
    }
    return expr_has_local_ref(e->lhs) || expr_has_local_ref(e->rhs);
}

static int raw_is_numeric_local_ref(const char *raw) {
    size_t i = 0;

    if (raw == NULL || raw[0] == '\0') {
        return 0;
    }
    while (raw[i] >= '0' && raw[i] <= '9') {
        i++;
    }
    return i > 0 && (raw[i] == 'f' || raw[i] == 'b') && raw[i + 1] == '\0';
}

/*
 * The questions the encoders have of a register's name.  Each is asked
 * of the one table (as_x86_regs.h); each had a list of its own.
 */

/* A general register, %al to %r15, or a vector or MMX register that an
 * instruction's register field can hold: its number, %ah to %bh having
 * numbers of their own. */
static int parse_x86_reg(const char *name, as_x86_reg_t *out) {
    static const as_x86_reg_t high8[] = {AS_X86_REG_AH, AS_X86_REG_CH, AS_X86_REG_DH, AS_X86_REG_BH};
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    if (r == NULL || out == NULL) {
        return -1;
    }
    switch (r->cls) {
    case AS_X86_REGCLASS_GENERAL:
        if (r->number > 15) {
            return -1;
        }
        *out = (r->flags & AS_X86_REGF_HIGH8) != 0 ? high8[r->number - 4] : (as_x86_reg_t)r->number;
        return 0;
    case AS_X86_REGCLASS_XMM:
    case AS_X86_REGCLASS_YMM:
    case AS_X86_REGCLASS_ZMM:
        if (r->number > 15) {
            return -1;
        }
        *out = (as_x86_reg_t)r->number;
        return 0;
    case AS_X86_REGCLASS_MMX:
        *out = (as_x86_reg_t)r->number;
        return 0;
    default:
        return -1;
    }
}

/* How wide a register is, for the address it is the base or index of
 * and for the operand it is; 0 for one that has no place in either. */
static unsigned parse_x86_reg_bits(const char *name) {
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    if (r == NULL || r->cls == AS_X86_REGCLASS_X87 || r->cls == AS_X86_REGCLASS_NO_INDEX) {
        return 0;
    }
    return r->width;
}

/* How wide an address an index register makes: its own width, but for a
 * vector register, which is the index of a gather and makes none -- the
 * address is as wide as its base. */
static unsigned x86_index_addr_bits(const char *name) {
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    if (r != NULL && (r->cls == AS_X86_REGCLASS_XMM || r->cls == AS_X86_REGCLASS_YMM || r->cls == AS_X86_REGCLASS_ZMM)) {
        return 0;
    }
    return parse_x86_reg_bits(name);
}

static unsigned infer_x86_mem_addr_bits(const as_mem_operand_t *mem) {
    unsigned base_bits = 0;
    unsigned index_bits = 0;

    if (mem == NULL) {
        return 0;
    }
    if (mem->base_reg != NULL) {
        base_bits = parse_x86_reg_bits(mem->base_reg);
    }
    if (mem->index_reg != NULL) {
        index_bits = x86_index_addr_bits(mem->index_reg);
    }
    if (base_bits != 0 && index_bits != 0 && base_bits != index_bits) {
        return 0;
    }
    if (base_bits != 0) {
        return base_bits;
    }
    return index_bits;
}

/* The number of a register of one class, numbered no more than `max`. */
static int reg_of_class(const char *name, as_x86_reg_class_t cls, unsigned max, unsigned *out) {
    const as_x86_reg_info_t *r = as_x86_reg_find_class(name, cls, max);

    if (r == NULL || out == NULL) {
        return -1;
    }
    *out = r->number;
    return 0;
}

static int parse_bnd_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_BOUND, 3, out);
}

/* A control, debug or test register -- `kind` is "cr", "dr", "db" or
 * "tr", the letters its name begins with -- numbered no more than
 * max_reg. */
static int parse_x86_sysreg(const char *name, const char *kind, unsigned max_reg, unsigned *out) {
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    if (r == NULL || kind == NULL || out == NULL || r->number > max_reg ||
        (r->cls != AS_X86_REGCLASS_CONTROL && r->cls != AS_X86_REGCLASS_DEBUG && r->cls != AS_X86_REGCLASS_TEST) ||
        strncmp(r->name, kind, strlen(kind)) != 0) {
        return -1;
    }
    *out = r->number;
    return 0;
}

static int parse_i386_modrm_reg(const char *name, unsigned *out) {
    as_x86_reg_t reg;
    unsigned bnd;

    if (parse_x86_reg(name, &reg) == 0 && ((unsigned)reg & 8u) == 0u) {
        switch (reg) {
        case AS_X86_REG_AH:
            *out = 4u;
            return 0;
        case AS_X86_REG_CH:
            *out = 5u;
            return 0;
        case AS_X86_REG_DH:
            *out = 6u;
            return 0;
        case AS_X86_REG_BH:
            *out = 7u;
            return 0;
        default:
            *out = (unsigned)reg & 7u;
            return 0;
        }
    }
    if (parse_bnd_reg(name, &bnd) == 0) {
        *out = bnd;
        return 0;
    }
    return -1;
}

static int parse_i386_gp_reg(const char *name, unsigned *out) {
    as_x86_reg_t reg;

    if (parse_x86_reg(name, &reg) != 0 || ((unsigned)reg & 8u) != 0u) {
        return -1;
    }
    switch (reg) {
    case AS_X86_REG_AH:
        *out = 4u;
        return 0;
    case AS_X86_REG_CH:
        *out = 5u;
        return 0;
    case AS_X86_REG_DH:
        *out = 6u;
        return 0;
    case AS_X86_REG_BH:
        *out = 7u;
        return 0;
    default:
        *out = (unsigned)reg & 7u;
        return 0;
    }
}

static int parse_seg_reg_text(const char *name, as_x86_seg_t *out) {
    as_x86_seg_t seg;

    if (out == NULL) {
        return -1;
    }
    seg = map_seg(name);
    if (seg == AS_X86_SEG_NONE) {
        return -1;
    }
    *out = seg;
    return 0;
}

static int x86_reg_width_bits(const char *name) {
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    /* (A mask register has no width that an operand's size is told
     * from, nor has the x87 stack.) */
    if (r == NULL || r->cls == AS_X86_REGCLASS_MASK || r->cls == AS_X86_REGCLASS_X87) {
        return 0;
    }
    return (int)r->width;
}

static int operand_is_x86_seg_reg(const as_operand_t *op) {
    as_x86_seg_t seg;

    if (op == NULL || op->kind != AS_OPERAND_REGISTER || op->u.reg == NULL) {
        return 0;
    }
    return parse_seg_reg_text(op->u.reg, &seg) == 0;
}

static int operand_is_x86_dx_port_reg(const as_operand_t *op) {
    if (op == NULL || op->kind != AS_OPERAND_REGISTER || op->u.reg == NULL) {
        return 0;
    }
    return streq_ci(op->u.reg, "dx") || streq_ci(op->u.reg, "%dx") ||
           streq_ci(op->u.reg, "edx") || streq_ci(op->u.reg, "%edx");
}

/* The width of a general register, 8 to 64; 0 for a register of any
 * other kind, and for what is not a register. */
static int x86_gpr_operand_width_bits(const as_operand_t *op) {
    const char *p;

    if (op == NULL || op->kind != AS_OPERAND_REGISTER || op->u.reg == NULL || operand_is_x86_seg_reg(op)) {
        return 0;
    }
    p = op->u.reg;
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    /* %mm0 is 64 bits wide and is not a general register. */
    if ((p[0] == 'm' || p[0] == 'M') && (p[1] == 'm' || p[1] == 'M')) {
        return 0;
    }
    switch (x86_reg_width_bits(p)) {
    case 8:
        return 8;
    case 16:
        return 16;
    case 32:
        return 32;
    case 64:
        return 64;
    default:
        return 0;
    }
}

/*
 * The operand size an AT&T instruction with no suffix has from the
 * general registers among its operands: 8, 16, 32 or 64, or 0 if they
 * do not say.  The register that says is the destination, which is
 * written last, or failing a register there the one before it:
 * `mov %ax, (%ebx)` is a 16-bit store, `movzx %al, %eax` a 32-bit
 * instruction.  Not every register is an operand of the instruction's
 * size: the port of in and out is %dx whatever is moved, a move to a
 * segment register is 16 bits with nothing to say so, and the
 * instructions listed take a register of one width and are not
 * instructions of that width -- setcc and its byte, the system
 * instructions and their 16-bit selectors, crc32 whose width is its
 * source's and is settled where it is encoded.
 */
static int att_register_operand_size(const as_instruction_t *insn, const char *mn) {
    static const char *const fixed[] = {
        "fnstsw", "fstsw", "arpl", "lldt", "ltr", "lmsw", "verr", "verw", "enter", "ret", "lret", "crc32",
    };
    static const char *const shifts[] = {
        "sal", "shl", "shr", "sar", "rol", "ror", "rcl", "rcr", "shld", "shrd",
    };
    int is_port_io = streq_ci(mn, "in") || streq_ci(mn, "out");
    int is_shift = 0;
    size_t i;

    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        if (streq_ci(mn, fixed[i])) {
            return 0;
        }
    }
    if (strncasecmp(mn, "set", 3) == 0) {
        return 0;
    }
    for (i = 0; i < sizeof(shifts) / sizeof(shifts[0]); ++i) {
        if (streq_ci(mn, shifts[i])) {
            is_shift = 1;
        }
    }
    if (insn->operand_count > 0 && operand_is_x86_seg_reg(&insn->operands[insn->operand_count - 1])) {
        return 0;
    }
    for (i = insn->operand_count; i > 0; --i) {
        const as_operand_t *op = &insn->operands[i - 1];
        int bits;

        if (is_port_io && operand_is_x86_dx_port_reg(op)) {
            continue;
        }
        /* The count of a shift is %cl whatever is shifted. */
        if (is_shift && i == 1 && insn->operand_count > 1) {
            continue;
        }
        bits = x86_gpr_operand_width_bits(op);
        if (bits != 0) {
            return bits;
        }
    }
    return 0;
}

static int infer_explicit_string_width_bits(const as_instruction_t *insn, const char *mnemonic) {
    size_t i;
    int bits = 0;

    if (insn == NULL || mnemonic == NULL) {
        return 0;
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];
        int cur = 0;

        if ((streq_ci(mnemonic, "in") || streq_ci(mnemonic, "outs") || streq_ci(mnemonic, "ins")) &&
            operand_is_x86_dx_port_reg(op)) {
            continue;
        }
        if (op->kind == AS_OPERAND_REGISTER && op->u.reg != NULL) {
            if (operand_is_x86_seg_reg(op)) {
                continue;
            }
            cur = x86_reg_width_bits(op->u.reg);
        } else if (op->kind == AS_OPERAND_MEMORY && op->u.mem.size_bits > 0) {
            cur = op->u.mem.size_bits;
        }
        if (cur == 0) {
            continue;
        }
        if (bits == 0) {
            bits = cur;
        } else if (bits != cur) {
            return 0;
        }
    }
    return bits;
}

static int eval_abs_mem_disp(const as_expr_t *expr, long long *disp_out) {
    if (expr == NULL || disp_out == NULL) {
        return -1;
    }
    if (eval_expr_const(expr, disp_out) == 0) {
        return 0;
    }
    if (expr_has_symbol(expr)) {
        *disp_out = 0;
        return 0;
    }
    return -1;
}

/*
 * ModRM and what follows it for an address of 16-bit registers, which
 * has a table of its own: %bx or %bp with %si or %di, or one of the
 * four alone, and a displacement of one byte or two.  The x87, SSE and
 * VEX emitters had no such table: `movaps (%bx), %xmm0` and `fldl
 * (%bx)` were refused.  (The prefix that says the address is of 16 bits
 * is put on by the statement's encoder.)
 */
static int emit_i386_modrm16(unsigned reg_field, const as_mem_operand_t *mem, unsigned char *out, size_t out_cap,
                             size_t *pos_io) {
    const as_x86_reg_info_t *base = mem->base_reg != NULL ? as_x86_reg_find(mem->base_reg) : NULL;
    const as_x86_reg_info_t *index = mem->index_reg != NULL ? as_x86_reg_find(mem->index_reg) : NULL;
    size_t pos = *pos_io;
    unsigned rm;
    unsigned mod;
    long long disp = 0;
    int symbolic = 0;

    if (base == NULL || base->cls != AS_X86_REGCLASS_GENERAL || base->width != 16 ||
        (mem->index_reg != NULL &&
         (index == NULL || index->cls != AS_X86_REGCLASS_GENERAL || index->width != 16 || mem->scale > 1))) {
        return -1;
    }
    /* %bx is 3, %bp 5, %si 6, %di 7. */
    if (index != NULL) {
        if ((base->number != 3 && base->number != 5) || (index->number != 6 && index->number != 7)) {
            return -1;
        }
        rm = (base->number == 5 ? 2u : 0u) + (index->number == 7 ? 1u : 0u);
    } else {
        switch (base->number) {
        case 6: rm = 4; break;
        case 7: rm = 5; break;
        case 5: rm = 6; break;
        case 3: rm = 7; break;
        default: return -1;
        }
    }
    if (mem->disp != NULL && eval_expr_const(mem->disp, &disp) != 0) {
        if (!expr_has_symbol(mem->disp)) {
            return -1;
        }
        symbolic = 1;
        disp = 0;
    }
    if (symbolic) {
        mod = 2;
    } else if (disp == 0 && rm != 6) {
        mod = 0;
    } else if (disp >= -128 && disp <= 127) {
        /* (%bp alone with no displacement is an address with no
         * register, so it is given one of nothing.) */
        mod = 1;
    } else if (disp >= -32768 && disp <= 65535) {
        mod = 2;
    } else {
        return -1;
    }
    if (pos + 3 > out_cap) {
        return -1;
    }
    out[pos++] = (unsigned char)((mod << 6) | ((reg_field & 7u) << 3) | rm);
    if (mod == 1) {
        out[pos++] = (unsigned char)(disp & 0xff);
    } else if (mod == 2) {
        out[pos++] = (unsigned char)(disp & 0xff);
        out[pos++] = (unsigned char)((disp >> 8) & 0xff);
    }
    *pos_io = pos;
    return 0;
}

/*
 * The statement being encoded is of 16-bit code.  An address that is
 * a symbol or a number and no register is two bytes there, behind
 * ModRM 00 reg 110; the emitters that are handed operands and not the
 * mode wrote the four bytes of 32-bit code behind r/m 101, which in
 * 16-bit code is (%di) and four bytes of something else: `fldl sym`,
 * `addps sym, %xmm1`, `bt $1, sym`.
 */
static int x86_emit_code16;

static int emit_i386_modrm_rm_operand(unsigned reg_field, const as_operand_t *rm_op,
                                      unsigned char *out, size_t out_cap, size_t *pos_io) {
    unsigned rm_reg;
    size_t pos;

    if (rm_op == NULL || out == NULL || pos_io == NULL || reg_field > 7u) {
        return -1;
    }
    /*
     * A number written with its `$` is a number and not the address of
     * anything.  It was read as the address: `pxor $3, %mm1`, `addps
     * $1, %xmm0` and `bsfl $1, %eax` were assembled, each with what was
     * in memory at that address for its operand.
     */
    if (rm_op->kind == AS_OPERAND_IMMEDIATE && rm_op->raw != NULL && rm_op->raw[0] == '$') {
        return -1;
    }
    pos = *pos_io;
    if (pos >= out_cap) {
        return -1;
    }
    if (rm_op->kind == AS_OPERAND_REGISTER) {
        if (parse_i386_modrm_reg(rm_op->u.reg, &rm_reg) != 0) {
            return -1;
        }
        if (pos + 1 > out_cap) {
            return -1;
        }
        out[pos++] = (unsigned char)(0xc0u | ((reg_field & 7u) << 3) | (rm_reg & 7u));
        *pos_io = pos;
        return 0;
    }

    if (rm_op->kind == AS_OPERAND_IMMEDIATE || rm_op->kind == AS_OPERAND_LABEL_REF) {
        long long disp = 0;

        if (eval_abs_mem_disp(rm_op->u.expr, &disp) != 0 || pos + 5 > out_cap) {
            return -1;
        }
        if (x86_emit_code16) {
            out[pos++] = (unsigned char)(((reg_field & 7u) << 3) | 6u);
            out[pos++] = (unsigned char)(disp & 0xff);
            out[pos++] = (unsigned char)((disp >> 8) & 0xff);
            *pos_io = pos;
            return 0;
        }
        out[pos++] = (unsigned char)(((reg_field & 7u) << 3) | 5u);
        out[pos++] = (unsigned char)(disp & 0xff);
        out[pos++] = (unsigned char)((disp >> 8) & 0xff);
        out[pos++] = (unsigned char)((disp >> 16) & 0xff);
        out[pos++] = (unsigned char)((disp >> 24) & 0xff);
        *pos_io = pos;
        return 0;
    }

    if (rm_op->kind == AS_OPERAND_MEMORY) {
        const as_mem_operand_t *mem = &rm_op->u.mem;
        as_x86_reg_t base;
        as_x86_reg_t index;
        unsigned base_reg = 0;
        unsigned index_reg = 4;
        unsigned scale_bits = 0;
        unsigned mod;
        unsigned rm;
        unsigned char modrm;
        unsigned char sib = 0;
        int need_sib = 0;
        int has_base = 0;
        int has_index = 0;
        int disp_only = 0;
        int has_disp = 0;
        long long disp = 0;

        if ((mem->base_reg != NULL && x86_reg_width_bits(mem->base_reg) == 16) ||
            (mem->index_reg != NULL && x86_reg_width_bits(mem->index_reg) == 16)) {
            return emit_i386_modrm16(reg_field, mem, out, out_cap, pos_io);
        }
        if (mem->base_reg != NULL) {
            if (parse_x86_reg(mem->base_reg, &base) != 0 || ((unsigned)base & 8u) != 0u) {
                return -1;
            }
            base_reg = (unsigned)base & 7u;
            has_base = 1;
        }
        if (mem->index_reg != NULL) {
            if (parse_x86_reg(mem->index_reg, &index) != 0 || ((unsigned)index & 8u) != 0u || index == AS_X86_REG_ESP) {
                return -1;
            }
            index_reg = (unsigned)index & 7u;
            has_index = 1;
        }
        {
            unsigned scale = (unsigned)(mem->scale > 0 ? mem->scale : 1);
            if (scale == 8u) {
                scale_bits = 3u;
            } else if (scale == 4u) {
                scale_bits = 2u;
            } else if (scale == 2u) {
                scale_bits = 1u;
            } else {
                scale_bits = 0u;
            }
        }
        if (mem->disp != NULL) {
            if (eval_abs_mem_disp(mem->disp, &disp) != 0) {
                return -1;
            }
            has_disp = 1;
        }
        if (!has_base && !has_index) {
            disp_only = 1;
            has_disp = 1;
        }

        if (pos + 6 > out_cap) {
            return -1;
        }
        if (disp_only) {
            out[pos++] = (unsigned char)(((reg_field & 7u) << 3) | 5u);
            out[pos++] = (unsigned char)(disp & 0xff);
            out[pos++] = (unsigned char)((disp >> 8) & 0xff);
            out[pos++] = (unsigned char)((disp >> 16) & 0xff);
            out[pos++] = (unsigned char)((disp >> 24) & 0xff);
            *pos_io = pos;
            return 0;
        }

        if (has_index && !has_base) {
            mod = 0;
            rm = 4;
            need_sib = 1;
            sib = (unsigned char)((scale_bits << 6) | (index_reg << 3) | 5u);
            if (!has_disp) {
                disp = 0;
            }
            has_disp = 1;
        } else {
            /*
             * A displacement with a symbol in it is 32 bits wide: its
             * value is the linker's to give, and the relocation is four
             * bytes.  It was sized by the 0 that stands for a symbol
             * here, so one byte was kept for it -- and the relocation
             * then written over the instruction, or refused: `flds
             * sym(%ebx)` failed, `movaps sym(%ebx), %xmm0` assembled to
             * four zero bytes.
             *
             * A displacement that is the number 0 is none, but under
             * %ebp, whose code without one means an absolute address:
             * there it is one byte of zero.
             */
            int symbolic = has_disp && expr_has_symbol(mem->disp);

            if (!symbolic && (!has_disp || disp == 0)) {
                mod = (base_reg != 5u) ? 0 : 1;
                disp = 0;
                has_disp = (mod == 1);
            } else if (!symbolic && disp >= -128 && disp <= 127) {
                mod = 1;
            } else {
                mod = 2;
            }

            rm = base_reg;
            if (has_index || base_reg == 4u) {
                need_sib = 1;
                rm = 4;
                sib = (unsigned char)((scale_bits << 6) | (index_reg << 3) | (base_reg & 7u));
            }

            if (mod == 0 && base_reg == 5u) {
                has_disp = 1;
                disp = 0;
            }
        }

        modrm = (unsigned char)((mod << 6) | ((reg_field & 7u) << 3) | (rm & 7u));
        out[pos++] = modrm;
        if (need_sib) {
            out[pos++] = sib;
        }
        if (mod == 1) {
            out[pos++] = (unsigned char)((signed char)disp);
        } else if (mod == 2 || (mod == 0 && has_disp)) {
            out[pos++] = (unsigned char)(disp & 0xff);
            out[pos++] = (unsigned char)((disp >> 8) & 0xff);
            out[pos++] = (unsigned char)((disp >> 16) & 0xff);
            out[pos++] = (unsigned char)((disp >> 24) & 0xff);
        }
        *pos_io = pos;
        return 0;
    }

    return -1;
}

static int emit_i386_modrm_any_rm_operand(unsigned reg_field, const as_operand_t *rm_op,
                                          unsigned char *out, size_t out_cap, size_t *pos_io) {
    unsigned rm_reg;

    if (rm_op == NULL || out == NULL || pos_io == NULL || reg_field > 7u) {
        return -1;
    }
    if (rm_op->kind == AS_OPERAND_REGISTER) {
        if (parse_i386_modrm_reg(rm_op->u.reg, &rm_reg) != 0 &&
            parse_mmx_reg(rm_op->u.reg, &rm_reg) != 0 &&
            parse_xmm_reg(rm_op->u.reg, &rm_reg) != 0) {
            return -1;
        }
        if (*pos_io + 1 > out_cap) {
            return -1;
        }
        out[(*pos_io)++] = (unsigned char)(0xc0u | ((reg_field & 7u) << 3) | (rm_reg & 7u));
        return 0;
    }
    return emit_i386_modrm_rm_operand(reg_field, rm_op, out, out_cap, pos_io);
}

static int emit_i386_prefixed_0f_rm(unsigned char prefix, unsigned char opcode2, unsigned reg_field,
                                    const as_operand_t *rm_op, unsigned char *out,
                                    size_t out_cap, size_t *out_len) {
    size_t pos = 0;
    const char *seg_override = NULL;

    if (out == NULL || out_len == NULL || rm_op == NULL || out_cap < 8) {
        return -1;
    }
    if (rm_op->kind == AS_OPERAND_MEMORY) {
        seg_override = rm_op->u.mem.segment_reg;
    }
    if (seg_override != NULL) {
        if (emit_seg_override_byte(out, out_cap, &pos, seg_override) != 0) {
            return -1;
        }
    }
    if (prefix != 0) {
        out[pos++] = prefix;
    }
    out[pos++] = 0x0f;
    out[pos++] = opcode2;
    if (emit_i386_modrm_any_rm_operand(reg_field, rm_op, out, out_cap, &pos) != 0) {
        return -1;
    }
    *out_len = pos;
    return 0;
}

static int emit_i386_prefixed_0f_map_rm(unsigned char prefix, unsigned char map2, unsigned char opcode3,
                                        unsigned reg_field, const as_operand_t *rm_op, unsigned char *out,
                                        size_t out_cap, size_t *out_len) {
    size_t pos = 0;
    const char *seg_override = NULL;

    if (out == NULL || out_len == NULL || rm_op == NULL || out_cap < 8) {
        return -1;
    }
    if (rm_op->kind == AS_OPERAND_MEMORY) {
        seg_override = rm_op->u.mem.segment_reg;
    }
    if (seg_override != NULL) {
        if (emit_seg_override_byte(out, out_cap, &pos, seg_override) != 0) {
            return -1;
        }
    }
    if (prefix != 0) {
        out[pos++] = prefix;
    }
    out[pos++] = 0x0f;
    out[pos++] = map2;
    out[pos++] = opcode3;
    if (emit_i386_modrm_any_rm_operand(reg_field, rm_op, out, out_cap, &pos) != 0) {
        return -1;
    }
    *out_len = pos;
    return 0;
}

static int emit_i386_prefixed_0f_map_rm_imm8(unsigned char prefix, unsigned char map2, unsigned char opcode3,
                                             unsigned reg_field, const as_operand_t *rm_op, unsigned char imm8,
                                             unsigned char *out, size_t out_cap, size_t *out_len) {
    size_t pos = 0;
    const char *seg_override = NULL;

    if (out == NULL || out_len == NULL || rm_op == NULL || out_cap < 9) {
        return -1;
    }
    if (rm_op->kind == AS_OPERAND_MEMORY) {
        seg_override = rm_op->u.mem.segment_reg;
    }
    if (seg_override != NULL) {
        if (emit_seg_override_byte(out, out_cap, &pos, seg_override) != 0) {
            return -1;
        }
    }
    if (prefix != 0) {
        out[pos++] = prefix;
    }
    out[pos++] = 0x0f;
    out[pos++] = map2;
    out[pos++] = opcode3;
    if (emit_i386_modrm_any_rm_operand(reg_field, rm_op, out, out_cap, &pos) != 0) {
        return -1;
    }
    if (pos >= out_cap) {
        return -1;
    }
    out[pos++] = imm8;
    *out_len = pos;
    return 0;
}

static int emit_i386_0f_sysreg_mov(unsigned char opcode2, unsigned sysreg, unsigned gpreg,
                                   unsigned char *out, size_t out_cap, size_t *out_len) {
    if (out == NULL || out_len == NULL || out_cap < 4 || sysreg > 7u || gpreg > 7u) {
        return -1;
    }
    out[0] = 0x0f;
    out[1] = opcode2;
    out[2] = (unsigned char)(0xc0u | ((sysreg & 7u) << 3) | (gpreg & 7u));
    *out_len = 3;
    return 0;
}

static int emit_i386_legacy_simd_rm(unsigned char prefix, unsigned char opcode2, unsigned reg_field,
                                    const as_operand_t *src, unsigned char *out, size_t out_cap, size_t *out_len) {
    return emit_i386_prefixed_0f_rm(prefix, opcode2, reg_field, src, out, out_cap, out_len);
}

static int emit_i386_legacy_simd_rm_imm8(unsigned char prefix, unsigned char opcode2, unsigned reg_field,
                                         const as_operand_t *src, unsigned char imm8, unsigned char *out,
                                         size_t out_cap, size_t *out_len) {
    size_t pos = 0;
    const char *seg_override = NULL;

    if (src == NULL || out == NULL || out_len == NULL || out_cap < 9) {
        return -1;
    }
    if (src->kind == AS_OPERAND_MEMORY) {
        seg_override = src->u.mem.segment_reg;
    }
    if (seg_override != NULL) {
        if (emit_seg_override_byte(out, out_cap, &pos, seg_override) != 0) {
            return -1;
        }
    }
    if (prefix != 0) {
        out[pos++] = prefix;
    }
    out[pos++] = 0x0f;
    out[pos++] = opcode2;
    if (emit_i386_modrm_rm_operand(reg_field, src, out, out_cap, &pos) != 0) {
        return -1;
    }
    if (pos >= out_cap) {
        return -1;
    }
    out[pos++] = imm8;
    *out_len = pos;
    return 0;
}

static int emit_i386_prefixed_xmm_srcdst_rm(unsigned char prefix, unsigned char opcode2,
                                            const as_operand_t *src, const as_operand_t *dst,
                                            unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xr;
    unsigned xm;

    if (src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
        return -1;
    }
    return emit_i386_prefixed_0f_rm(prefix, opcode2, xr, src, out, out_cap, out_len);
}

static int emit_i386_xmm_move_rm(unsigned char prefix, unsigned char load_opcode2,
                                 unsigned char store_opcode2, const as_operand_t *src,
                                 const as_operand_t *dst, unsigned char *out,
                                 size_t out_cap, size_t *out_len) {
    unsigned xr;
    unsigned xm;

    if (src == NULL || dst == NULL) {
        return -1;
    }
    if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, load_opcode2, xr, src, out, out_cap, out_len);
    }
    if (src->kind == AS_OPERAND_REGISTER &&
        (dst->kind == AS_OPERAND_MEMORY || dst->kind == AS_OPERAND_IMMEDIATE || dst->kind == AS_OPERAND_LABEL_REF) &&
        parse_xmm_reg(src->u.reg, &xr) == 0) {
        return emit_i386_prefixed_0f_rm(prefix, store_opcode2, xr, dst, out, out_cap, out_len);
    }
    return -1;
}

static int emit_i386_xmm_partial_move_rm(unsigned char prefix, unsigned char load_opcode2,
                                         unsigned char store_opcode2, const as_operand_t *src,
                                         const as_operand_t *dst, unsigned char *out,
                                         size_t out_cap, size_t *out_len) {
    unsigned xr;

    if (src == NULL || dst == NULL) {
        return -1;
    }
    if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
        if (src->kind == AS_OPERAND_REGISTER) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, load_opcode2, xr, src, out, out_cap, out_len);
    }
    if (src->kind == AS_OPERAND_REGISTER &&
        (dst->kind == AS_OPERAND_MEMORY || dst->kind == AS_OPERAND_IMMEDIATE || dst->kind == AS_OPERAND_LABEL_REF) &&
        parse_xmm_reg(src->u.reg, &xr) == 0) {
        return emit_i386_prefixed_0f_rm(prefix, store_opcode2, xr, dst, out, out_cap, out_len);
    }
    return -1;
}

static int lookup_i386_packed_rm_opcode(const char *mnemonic, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
    } map[] = {
        {"punpcklbw", 0x60}, {"punpcklwd", 0x61}, {"punpckldq", 0x62}, {"packsswb", 0x63},
        {"pcmpgtb", 0x64},   {"pcmpgtw", 0x65},   {"pcmpgtd", 0x66},   {"packuswb", 0x67},
        {"punpckhbw", 0x68}, {"punpckhwd", 0x69}, {"punpckhdq", 0x6a}, {"packssdw", 0x6b},
        {"punpcklqdq", 0x6c}, {"punpckhqdq", 0x6d},
        {"pcmpeqb", 0x74},   {"pcmpeqw", 0x75},   {"pcmpeqd", 0x76},
        {"psrlw", 0xd1},     {"psrld", 0xd2},     {"psrlq", 0xd3},     {"paddq", 0xd4},
        {"pmullw", 0xd5},    {"psubusb", 0xd8},   {"psubusw", 0xd9},   {"pminub", 0xda},
        {"pand", 0xdb},      {"paddusb", 0xdc},   {"paddusw", 0xdd},   {"pmaxub", 0xde},
        {"pandn", 0xdf},     {"pavgb", 0xe0},     {"psraw", 0xe1},     {"psrad", 0xe2},
        {"pavgw", 0xe3},     {"pmulhuw", 0xe4},   {"pmulhw", 0xe5},    {"psubsb", 0xe8},
        {"psubsw", 0xe9},    {"pminsw", 0xea},    {"por", 0xeb},       {"paddsb", 0xec},
        {"paddsw", 0xed},    {"pmaxsw", 0xee},    {"psllw", 0xf1},     {"pslld", 0xf2},
        {"psllq", 0xf3},     {"pmuludq", 0xf4},   {"pmaddwd", 0xf5},   {"psadbw", 0xf6},
        {"psubb", 0xf8},     {"psubw", 0xf9},     {"psubd", 0xfa},     {"psubq", 0xfb},
        {"paddb", 0xfc},     {"paddw", 0xfd},     {"paddd", 0xfe},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_xmm_unprefixed_opcode(const char *mnemonic, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
    } map[] = {
        {"unpcklps", 0x14}, {"unpckhps", 0x15},
        {"sqrtps", 0x51}, {"rsqrtps", 0x52}, {"rcpps", 0x53}, {"andps", 0x54},
        {"andnps", 0x55}, {"orps", 0x56},    {"xorps", 0x57}, {"addps", 0x58},
        {"mulps", 0x59},  {"cvtps2pd", 0x5a},{"cvtdq2ps", 0x5b},{"subps", 0x5c},
        {"minps", 0x5d},  {"divps", 0x5e},   {"maxps", 0x5f}, {"ucomiss", 0x2e},
        {"comiss", 0x2f},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_xmm_66_opcode(const char *mnemonic, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
    } map[] = {
        {"unpcklpd", 0x14}, {"unpckhpd", 0x15},
        {"sqrtpd", 0x51}, {"andpd", 0x54},   {"andnpd", 0x55}, {"orpd", 0x56},
        {"xorpd", 0x57},  {"addpd", 0x58},   {"mulpd", 0x59},  {"cvtpd2ps", 0x5a},
        {"cvtps2dq", 0x5b},{"subpd", 0x5c},  {"minpd", 0x5d},  {"divpd", 0x5e},
        {"maxpd", 0x5f},  {"ucomisd", 0x2e}, {"comisd", 0x2f},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_0fc7_group(const char *mnemonic, unsigned char *prefix, unsigned *reg_field) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned reg_field;
    } map[] = {
        {"cmpxchg8b", 0x00, 1u},
        {"xsavec", 0x00, 4u},
        {"xsaves", 0x00, 5u},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || reg_field == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *reg_field = map[i].reg_field;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_0f00_group(const char *mnemonic, unsigned *reg_field) {
    static const struct {
        const char *mnemonic;
        unsigned reg_field;
    } map[] = {
        {"sldt", 0u},
        {"str", 1u},
        {"lldt", 2u},
        {"ltr", 3u},
        {"verr", 4u},
        {"verw", 5u},
    };
    size_t i;

    if (mnemonic == NULL || reg_field == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *reg_field = map[i].reg_field;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_f3_0fae_group(const char *mnemonic, unsigned *reg_field) {
    static const struct {
        const char *mnemonic;
        unsigned reg_field;
    } map[] = {
        {"rdfsbase", 0u},
        {"rdgsbase", 1u},
        {"wrfsbase", 2u},
        {"wrgsbase", 3u},
    };
    size_t i;

    if (mnemonic == NULL || reg_field == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *reg_field = map[i].reg_field;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_0fae_rm_group(const char *mnemonic, unsigned char *prefix, unsigned *reg_field) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned reg_field;
    } map[] = {
        {"fxsave", 0x00, 0u},
        {"fxrstor", 0x00, 1u},
        {"ldmxcsr", 0x00, 2u},
        {"stmxcsr", 0x00, 3u},
        {"xsave", 0x00, 4u},
        {"xrstor", 0x00, 5u},
        {"xsaveopt", 0x00, 6u},
        {"clflush", 0x00, 7u},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || reg_field == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *reg_field = map[i].reg_field;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_fence_tail(const char *mnemonic, unsigned char *tail) {
    static const struct {
        const char *mnemonic;
        unsigned char tail;
    } map[] = {
        {"lfence", 0xe8},
        {"mfence", 0xf0},
        {"sfence", 0xf8},
    };
    size_t i;

    if (mnemonic == NULL || tail == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *tail = map[i].tail;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_bt_group(const char *mnemonic, unsigned *reg_field, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned reg_field;
        unsigned char opcode2;
    } map[] = {
        {"bt", 4u, 0xa3},
        {"bts", 5u, 0xab},
        {"btr", 6u, 0xb3},
        {"btc", 7u, 0xbb},
    };
    size_t i;

    if (mnemonic == NULL || reg_field == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *reg_field = map[i].reg_field;
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_scanbit_group(const char *mnemonic, unsigned char *prefix, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode2;
    } map[] = {
        {"bsf", 0x00, 0xbc},
        {"bsr", 0x00, 0xbd},
        {"popcnt", 0xf3, 0xb8},
        {"tzcnt", 0xf3, 0xbc},
        {"lzcnt", 0xf3, 0xbd},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_f2_scalar_xmm_opcode(const char *mnemonic, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
    } map[] = {
        {"cvttsd2si", 0x2c},
        {"cvtsd2si", 0x2d},
        {"sqrtsd", 0x51},
        {"addsd", 0x58},
        {"mulsd", 0x59},
        {"cvtsd2ss", 0x5a},
        {"subsd", 0x5c},
        {"minsd", 0x5d},
        {"divsd", 0x5e},
        {"maxsd", 0x5f},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_f3_scalar_xmm_opcode(const char *mnemonic, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
    } map[] = {
        {"cvttss2si", 0x2c},
        {"cvtss2si", 0x2d},
        {"sqrtss", 0x51},
        {"rsqrtss", 0x52},
        {"rcpss", 0x53},
        {"addss", 0x58},
        {"mulss", 0x59},
        {"cvtss2sd", 0x5a},
        {"cvttps2dq", 0x5b},
        {"subss", 0x5c},
        {"minss", 0x5d},
        {"divss", 0x5e},
        {"maxss", 0x5f},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_fixed_0f_opcode(const char *mnemonic, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
    } map[] = {
        {"cpuid", 0xa2},
        {"rsm", 0xaa},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_shiftd_opcode(const char *mnemonic, unsigned char *reg_opcode, unsigned char *imm_opcode) {
    static const struct {
        const char *mnemonic;
        unsigned char reg_opcode;
        unsigned char imm_opcode;
    } map[] = {
        {"shld", 0xa5, 0xa4},
        {"shrd", 0xad, 0xac},
    };
    size_t i;

    if (mnemonic == NULL || reg_opcode == NULL || imm_opcode == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *reg_opcode = map[i].reg_opcode;
            *imm_opcode = map[i].imm_opcode;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_sha_0f38_opcode(const char *mnemonic, unsigned char *opcode3) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode3;
    } map[] = {
        {"sha1nexte", 0xc8},
        {"sha1msg1", 0xc9},
        {"sha1msg2", 0xca},
        {"sha256msg1", 0xcc},
        {"sha256msg2", 0xcd},
    };
    size_t i;

    if (mnemonic == NULL || opcode3 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode3 = map[i].opcode3;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_crypto_0f3a_imm8_opcode(const char *mnemonic, unsigned char *opcode3) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode3;
    } map[] = {
        {"pclmulqdq", 0x44},
        {"gf2p8affineqb", 0xce},
        {"gf2p8affineinvqb", 0xcf},
        {"aeskeygenassist", 0xdf},
    };
    size_t i;

    if (mnemonic == NULL || opcode3 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode3 = map[i].opcode3;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_movbe_opcode(int intel_syntax, int first_is_reg, unsigned char *opcode3) {
    if (opcode3 == NULL) {
        return -1;
    }
    if (intel_syntax) {
        *opcode3 = first_is_reg ? 0xf0 : 0xf1;
    } else {
        *opcode3 = first_is_reg ? 0xf1 : 0xf0;
    }
    return 0;
}

static int lookup_i386_aes_0f38_opcode(const char *mnemonic, unsigned char *opcode3) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode3;
    } map[] = {
        {"gf2p8mulb", 0xcf},
        {"aesimc", 0xdb},
        {"aesenc", 0xdc},
        {"aesenclast", 0xdd},
        {"aesdec", 0xde},
        {"aesdeclast", 0xdf},
    };
    size_t i;

    if (mnemonic == NULL || opcode3 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode3 = map[i].opcode3;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_keylocker_opcode(const char *mnemonic, unsigned char *opcode3) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode3;
    } map[] = {
        {"loadiwkey", 0xdc},
        {"aesenc128kl", 0xdc},
        {"aesdec128kl", 0xdd},
        {"aesenc256kl", 0xde},
        {"aesdec256kl", 0xdf},
    };
    size_t i;

    if (mnemonic == NULL || opcode3 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode3 = map[i].opcode3;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_memorder_opcode(const char *mnemonic, unsigned char *prefix, unsigned char *opcode3,
                                       int *reg_first) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode3;
        int reg_first;
    } map[] = {
        {"enqcmd", 0xf2, 0xf8, 1},
        {"enqcmds", 0xf3, 0xf8, 1},
        {"aadd", 0x00, 0xfc, 0},
        {"aand", 0x66, 0xfc, 0},
        {"aor", 0xf2, 0xfc, 0},
        {"axor", 0xf3, 0xfc, 0},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || opcode3 == NULL || reg_first == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *opcode3 = map[i].opcode3;
            *reg_first = map[i].reg_first;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_x87_nooperand_opcode(const char *mnemonic, unsigned char *op1, unsigned char *op2) {
    static const struct {
        const char *mnemonic;
        unsigned char op1;
        unsigned char op2;
    } map[] = {
        {"fnop", 0xd9, 0xd0},
        {"fchs", 0xd9, 0xe0},
        {"f2xm1", 0xd9, 0xf0},
        {"fprem", 0xd9, 0xf8},
    };
    size_t i;

    if (mnemonic == NULL || op1 == NULL || op2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *op1 = map[i].op1;
            *op2 = map[i].op2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_x87_fcmov_opcode(const char *mnemonic, unsigned char *op1, unsigned char *base) {
    static const struct {
        const char *mnemonic;
        unsigned char op1;
        unsigned char base;
    } map[] = {
        {"fcmovb", 0xda, 0xc0},
        {"fcmove", 0xda, 0xc8},
        {"fcmovbe", 0xda, 0xd0},
        {"fcmovu", 0xda, 0xd8},
        {"fcmovnb", 0xdb, 0xc0},
        {"fcmovne", 0xdb, 0xc8},
        {"fcmovnbe", 0xdb, 0xd0},
        {"fcmovnu", 0xdb, 0xd8},
        {"fucomi", 0xdb, 0xe8},
        {"fcomi", 0xdb, 0xf0},
    };
    size_t i;

    if (mnemonic == NULL || op1 == NULL || base == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *op1 = map[i].op1;
            *base = map[i].base;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_x87_stack_unary_opcode(const char *mnemonic, unsigned char *base) {
    static const struct {
        const char *mnemonic;
        unsigned char base;
    } map[] = {
        {"ffree", 0xc0},
        {"fst", 0xd0},
        {"fucom", 0xe0},
        {"fucomp", 0xe8},
    };
    size_t i;

    if (mnemonic == NULL || base == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *base = map[i].base;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_x87_pop2_opcode(const char *mnemonic, unsigned char *base) {
    static const struct {
        const char *mnemonic;
        unsigned char base;
    } map[] = {
        {"faddp", 0xc0},
        {"fmulp", 0xc8},
        {"fsubp", 0xe0},
        {"fsubrp", 0xe8},
        {"fdivp", 0xf0},
        {"fdivrp", 0xf8},
    };
    size_t i;

    if (mnemonic == NULL || base == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *base = map[i].base;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_x87_ipcompare_opcode(const char *mnemonic, unsigned char *base) {
    static const struct {
        const char *mnemonic;
        unsigned char base;
    } map[] = {
        {"fucomip", 0xe8},
        {"fcomip", 0xf0},
    };
    size_t i;

    if (mnemonic == NULL || base == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *base = map[i].base;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_x87_arith_opcode(const char *mnemonic, unsigned char *mem_d8_regf,
                                        unsigned char *mem_dc_regf, int *stack_single_base,
                                        unsigned char *stack_pair_base) {
    static const struct {
        const char *mnemonic;
        unsigned char mem_d8_regf;
        unsigned char mem_dc_regf;
        int stack_single_base;
        unsigned char stack_pair_base;
    } map[] = {
        {"fadd", 0u, 0u, -1, 0xc0},
        {"fmul", 1u, 1u, -1, 0xc8},
        {"fcom", 2u, 2u, 0xd0, 0xd0},
        {"fcomp", 3u, 3u, 0xd8, 0xd8},
        {"fsub", 4u, 4u, -1, 0xe0},
        {"fsubr", 5u, 5u, -1, 0xe8},
        {"fdiv", 6u, 6u, -1, 0xf0},
        {"fdivr", 7u, 7u, -1, 0xf8},
    };
    size_t i;

    if (mnemonic == NULL || mem_d8_regf == NULL || mem_dc_regf == NULL ||
        stack_single_base == NULL || stack_pair_base == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *mem_d8_regf = map[i].mem_d8_regf;
            *mem_dc_regf = map[i].mem_dc_regf;
            *stack_single_base = map[i].stack_single_base;
            *stack_pair_base = map[i].stack_pair_base;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_adcxo_prefix(const char *mnemonic, unsigned char *prefix) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
    } map[] = {
        {"adcx", 0x66},
        {"adox", 0xf3},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_encodekey_opcode(const char *mnemonic, unsigned char *opcode3) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode3;
    } map[] = {
        {"encodekey128", 0xfa},
        {"encodekey256", 0xfb},
    };
    size_t i;

    if (mnemonic == NULL || opcode3 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode3 = map[i].opcode3;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_bndcmp_opcode(const char *mnemonic, unsigned char *prefix, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode2;
    } map[] = {
        {"bndcl", 0xf3, 0x1a},
        {"bndcu", 0xf2, 0x1a},
        {"bndcn", 0xf2, 0x1b},
        {"bndmk", 0xf3, 0x1b},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_x86_xmm_move_family(const char *mnemonic, unsigned char *prefix,
                                      unsigned char *load_opcode2, unsigned char *store_opcode2,
                                      int *is_partial) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char load_opcode2;
        unsigned char store_opcode2;
        int is_partial;
    } map[] = {
        {"movups", 0x00, 0x10, 0x11, 0},
        {"movupd", 0x66, 0x10, 0x11, 0},
        {"movlpd", 0x66, 0x12, 0x13, 1},
        {"movhpd", 0x66, 0x16, 0x17, 1},
        {"movlps", 0x00, 0x12, 0x13, 1},
        {"movhps", 0x00, 0x16, 0x17, 1},
        {"movaps", 0x00, 0x28, 0x29, 0},
        {"movapd", 0x66, 0x28, 0x29, 0},
        {"movsd", 0xf2, 0x10, 0x11, 0},
        {"movss", 0xf3, 0x10, 0x11, 0},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || load_opcode2 == NULL || store_opcode2 == NULL || is_partial == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *load_opcode2 = map[i].load_opcode2;
            *store_opcode2 = map[i].store_opcode2;
            *is_partial = map[i].is_partial;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_movmsk_prefix(const char *mnemonic, unsigned char *prefix) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
    } map[] = {
        {"movmskps", 0x00},
        {"movmskpd", 0x66},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_mmx_xmm_bridge(const char *mnemonic, unsigned char *prefix, int *dst_is_xmm) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        int dst_is_xmm;
    } map[] = {
        {"movdq2q", 0xf2, 0},
        {"movq2dq", 0xf3, 1},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || dst_is_xmm == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *dst_is_xmm = map[i].dst_is_xmm;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_mmx_xmm_convert_to_xmm(const char *mnemonic, unsigned char *prefix, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode2;
    } map[] = {
        {"cvtpi2pd", 0x66, 0x2a},
        {"cvtpi2ps", 0x00, 0x2a},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_mmx_xmm_convert_from_xmm(const char *mnemonic, unsigned char *prefix, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode2;
    } map[] = {
        {"cvttpd2pi", 0x66, 0x2c},
        {"cvtpd2pi", 0x66, 0x2d},
        {"cvttps2pi", 0x00, 0x2c},
        {"cvtps2pi", 0x00, 0x2d},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_movnt_store_family(const char *mnemonic, unsigned char *prefix, unsigned char *opcode2,
                                          int *use_xmm) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode2;
        int use_xmm;
    } map[] = {
        {"movntq", 0x00, 0xe7, 0},
        {"movntdq", 0x66, 0xe7, 1},
        {"movntps", 0x00, 0x2b, 1},
        {"movntpd", 0x66, 0x2b, 1},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || opcode2 == NULL || use_xmm == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *opcode2 = map[i].opcode2;
            *use_xmm = map[i].use_xmm;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_xmm_shiftdq_imm8(const char *mnemonic, unsigned char *reg_field) {
    static const struct {
        const char *mnemonic;
        unsigned char reg_field;
    } map[] = {
        {"psrldq", 3u},
        {"pslldq", 7u},
    };
    size_t i;

    if (mnemonic == NULL || reg_field == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *reg_field = map[i].reg_field;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_xmm_shuffle_tail(const char *mnemonic, unsigned char *prefix, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode2;
    } map[] = {
        {"pshuflw", 0xf2, 0x70},
        {"pshufhw", 0xf3, 0x70},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_xmm_regpair_opcode(const char *mnemonic, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
    } map[] = {
        {"movhlps", 0x12},
        {"movlhps", 0x16},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_comisd_opcode(const char *mnemonic, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
    } map[] = {
        {"ucomisd", 0x2e},
        {"comisd", 0x2f},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_cachehint_regf(const char *mnemonic, unsigned char *opcode2, unsigned char *regf) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode2;
        unsigned char regf;
    } map[] = {
        {"prefetch", 0x0d, 0u},
        {"prefetchwt1", 0x0d, 2u},
        {"prefetchnta", 0x18, 0u},
        {"prefetcht0", 0x18, 1u},
        {"prefetcht1", 0x18, 2u},
        {"prefetcht2", 0x18, 3u},
    };
    size_t i;

    if (mnemonic == NULL || opcode2 == NULL || regf == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode2 = map[i].opcode2;
            *regf = map[i].regf;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_extrq_insertq_prefix(const char *mnemonic, unsigned char *prefix) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
    } map[] = {
        {"extrq", 0x66},
        {"insertq", 0xf2},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            return 0;
        }
    }
    return -1;
}

static int select_x86_dstsrc_tail_operand(const as_instruction_t *insn, int intel_syntax,
                                          const as_operand_t **dst_op, const as_operand_t **src_op,
                                          const as_operand_t **tail_op) {
    if (insn == NULL || dst_op == NULL || src_op == NULL || tail_op == NULL || insn->operand_count != 3) {
        return -1;
    }
    if (intel_syntax) {
        *dst_op = &insn->operands[0];
        *src_op = &insn->operands[1];
        *tail_op = &insn->operands[2];
    } else {
        *tail_op = &insn->operands[0];
        *src_op = &insn->operands[1];
        *dst_op = &insn->operands[2];
    }
    return 0;
}

static int select_x86_vmread_vmwrite_operands(const as_instruction_t *insn, int intel_syntax,
                                              const char *mnemonic,
                                              const as_operand_t **rm_op,
                                              const as_operand_t **reg_op) {
    int is_vmread;

    if (insn == NULL || mnemonic == NULL || rm_op == NULL || reg_op == NULL || insn->operand_count != 2) {
        return -1;
    }
    is_vmread = (strcmp(mnemonic, "vmread") == 0);
    if (is_vmread == intel_syntax) {
        *rm_op = &insn->operands[0];
        *reg_op = &insn->operands[1];
    } else {
        *reg_op = &insn->operands[0];
        *rm_op = &insn->operands[1];
    }
    return 0;
}

static int select_i386_actual_reg_rm_operands(const as_instruction_t *insn,
                                              const as_operand_t **reg_op,
                                              const as_operand_t **rm_op) {
    if (insn == NULL || reg_op == NULL || rm_op == NULL || insn->operand_count != 2) {
        return -1;
    }
    if (insn->operands[0].kind == AS_OPERAND_REGISTER) {
        *reg_op = &insn->operands[0];
        *rm_op = &insn->operands[1];
        return 0;
    }
    if (insn->operands[1].kind == AS_OPERAND_REGISTER) {
        *rm_op = &insn->operands[0];
        *reg_op = &insn->operands[1];
        return 0;
    }
    return -1;
}

static int select_i386_syntax_reg_rm_operands(const as_instruction_t *insn, int intel_syntax, int intel_reg_first,
                                              const as_operand_t **reg_op,
                                              const as_operand_t **rm_op) {
    if (insn == NULL || reg_op == NULL || rm_op == NULL || insn->operand_count != 2) {
        return -1;
    }
    if (intel_reg_first) {
        if (intel_syntax) {
            *reg_op = &insn->operands[0];
            *rm_op = &insn->operands[1];
        } else {
            *rm_op = &insn->operands[0];
            *reg_op = &insn->operands[1];
        }
    } else if (intel_syntax) {
        *rm_op = &insn->operands[0];
        *reg_op = &insn->operands[1];
    } else {
        *reg_op = &insn->operands[0];
        *rm_op = &insn->operands[1];
    }
    return 0;
}

static int lookup_i386_xmm_imm8_family(const char *mnemonic, unsigned char *prefix, unsigned char *opcode2) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode2;
    } map[] = {
        {"cmpps", 0x00, 0xc2},
        {"cmppd", 0x66, 0xc2},
        {"cmpss", 0xf3, 0xc2},
        {"cmpsd", 0xf2, 0xc2},
        {"shufps", 0x00, 0xc6},
        {"shufpd", 0x66, 0xc6},
        {"pshufd", 0x66, 0x70},
    };
    size_t i;

    if (mnemonic == NULL || prefix == NULL || opcode2 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *prefix = map[i].prefix;
            *opcode2 = map[i].opcode2;
            return 0;
        }
    }
    return -1;
}

static int emit_i386_3dnow_rm(unsigned char reg_field, const as_operand_t *src, unsigned char imm8,
                              unsigned char *out, size_t out_cap, size_t *out_len) {
    size_t pos = 0;

    if (src == NULL || out == NULL || out_len == NULL || out_cap < 5) {
        return -1;
    }
    out[pos++] = 0x0f;
    out[pos++] = 0x0f;
    if (emit_i386_modrm_rm_operand(reg_field, src, out, out_cap, &pos) != 0) {
        return -1;
    }
    out[pos++] = imm8;
    *out_len = pos;
    return 0;
}

static int mnemonic_needs_uniform_width(const char *mn) {
    return streq_ci(mn, "mov") || streq_ci(mn, "add") || streq_ci(mn, "sub") || streq_ci(mn, "and") ||
           streq_ci(mn, "or") || streq_ci(mn, "xor") || streq_ci(mn, "cmp") || streq_ci(mn, "test") ||
           streq_ci(mn, "xchg");
}

static int infer_uniform_operand_width_bits(const as_instruction_t *insn, const size_t *op_index, size_t op_count,
                                            int *out_bits, char *errbuf, size_t errbuf_sz) {
    size_t j;
    int bits = 0;

    if (insn == NULL || op_index == NULL || out_bits == NULL) {
        return -1;
    }
    for (j = 0; j < op_count; ++j) {
        const as_operand_t *op = &insn->operands[op_index[j]];
        int cur = 0;

        if (op->kind == AS_OPERAND_REGISTER && op->u.reg != NULL) {
            cur = x86_reg_width_bits(op->u.reg);
        } else if (op->kind == AS_OPERAND_MEMORY && op->u.mem.size_bits > 0) {
            cur = op->u.mem.size_bits;
        }
        if (cur == 0) {
            continue;
        }
        if (bits == 0) {
            bits = cur;
            continue;
        }
        if (bits != cur) {
            if (errbuf != NULL && errbuf_sz > 0) {
                snprintf(errbuf, errbuf_sz, "conflicting operand sizes");
            }
            return -1;
        }
    }
    *out_bits = bits;
    return 0;
}

static int has_unsized_memory_and_immediate(const as_instruction_t *insn, const size_t *op_index, size_t op_count) {
    size_t j;
    int have_unsized_mem = 0;
    int have_imm = 0;

    if (insn == NULL || op_index == NULL) {
        return 0;
    }
    for (j = 0; j < op_count; ++j) {
        const as_operand_t *op = &insn->operands[op_index[j]];

        if (op->kind == AS_OPERAND_MEMORY && op->u.mem.size_bits == 0) {
            have_unsized_mem = 1;
        } else if (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) {
            have_imm = 1;
        }
    }
    return have_unsized_mem && have_imm;
}

static as_x86_seg_t map_seg(const char *s) {
    /* By the segment's number in the table: es, cs, ss, ds, fs, gs. */
    static const as_x86_seg_t segs[] = {AS_X86_SEG_ES, AS_X86_SEG_CS, AS_X86_SEG_SS,
                                        AS_X86_SEG_DS, AS_X86_SEG_FS, AS_X86_SEG_GS};
    const as_x86_reg_info_t *r;

    /* (Without its percent sign: this is asked of names that have had
     * it taken off, and one that has it still is not one.) */
    if (s == NULL || s[0] == '%' || isspace((unsigned char)s[0])) {
        return AS_X86_SEG_NONE;
    }
    r = as_x86_reg_find_class(s, AS_X86_REGCLASS_SEGMENT, 5);
    return r != NULL ? segs[r->number] : AS_X86_SEG_NONE;
}

/* movs, cmps, lods, stos, scas, ins and outs, with a size suffix or not. */
static int is_string_instruction(const char *mn) {
    static const char *const stems[] = { "movs", "cmps", "lods", "stos", "scas", "ins", "outs" };
    size_t i;

    if (mn == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(stems) / sizeof(stems[0]); ++i) {
        size_t n = strlen(stems[i]);

        if (strncasecmp(mn, stems[i], n) == 0 &&
            (mn[n] == '\0' || (mn[n + 1] == '\0' && strchr("bwlqdBWLQD", mn[n]) != NULL))) {
            return 1;
        }
    }
    return 0;
}

/* 's' for %si, %esi or %rsi, 'd' for %di, %edi or %rdi, 0 for another. */
static char string_index_register(const char *reg) {
    if (reg == NULL) {
        return 0;
    }
    if (reg[0] == '%') {
        reg++;
    }
    if (strcasecmp(reg, "si") == 0 || strcasecmp(reg, "esi") == 0 || strcasecmp(reg, "rsi") == 0) {
        return 's';
    }
    if (strcasecmp(reg, "di") == 0 || strcasecmp(reg, "edi") == 0 || strcasecmp(reg, "rdi") == 0) {
        return 'd';
    }
    return 0;
}

static int emit_seg_override_byte(unsigned char *out, size_t out_cap, size_t *pos_io, const char *segment_reg) {
    as_x86_seg_t seg;

    if (segment_reg == NULL) {
        return 0;
    }
    if (out == NULL || pos_io == NULL || *pos_io >= out_cap) {
        return -1;
    }
    seg = map_seg(segment_reg);
    switch (seg) {
    case AS_X86_SEG_CS:
        out[(*pos_io)++] = 0x2e;
        return 0;
    case AS_X86_SEG_DS:
        out[(*pos_io)++] = 0x3e;
        return 0;
    case AS_X86_SEG_ES:
        out[(*pos_io)++] = 0x26;
        return 0;
    case AS_X86_SEG_FS:
        out[(*pos_io)++] = 0x64;
        return 0;
    case AS_X86_SEG_GS:
        out[(*pos_io)++] = 0x65;
        return 0;
    case AS_X86_SEG_SS:
        out[(*pos_io)++] = 0x36;
        return 0;
    case AS_X86_SEG_NONE:
    default:
        return -1;
    }
}

static int seg_reg_field(as_x86_seg_t seg, unsigned *out) {
    if (out == NULL) {
        return -1;
    }
    switch (seg) {
    case AS_X86_SEG_ES:
        *out = 0u;
        return 0;
    case AS_X86_SEG_CS:
        *out = 1u;
        return 0;
    case AS_X86_SEG_SS:
        *out = 2u;
        return 0;
    case AS_X86_SEG_DS:
        *out = 3u;
        return 0;
    case AS_X86_SEG_FS:
        *out = 4u;
        return 0;
    case AS_X86_SEG_GS:
        *out = 5u;
        return 0;
    default:
        return -1;
    }
}

/*
 * The branches that have only a one-byte displacement: loop, loope
 * (loopz), loopne (loopnz), each with an optional w, l or q for the
 * width of the counter, and jcxz, jecxz, jrcxz, whose name is the
 * counter's.  The opcode, and whether the counter is not the one the
 * code's address size gives -- cx in 32-bit code, ecx in 16- or 64-bit
 * -- so that the address-size prefix 67 is wanted.
 *
 * 0 for one of them that the mode has; -1 for a mnemonic that is not
 * one; -2 for one the mode has not: a 64-bit counter outside 64-bit
 * code, a 16-bit one inside it.
 */
static int x86_fixed_short_branch(const char *mn, unsigned code_bits, int *need67, unsigned char *opcode) {
    static const struct {
        const char *name;
        unsigned char opcode;
    } loops[] = {
        { "loopne", 0xe0 }, { "loopnz", 0xe0 }, { "loope", 0xe1 }, { "loopz", 0xe1 }, { "loop", 0xe2 },
    };
    unsigned counter = 0;
    unsigned char op = 0;
    size_t i;

    if (mn == NULL) {
        return -1;
    }
    if (streq_ci(mn, "jcxz")) {
        counter = 16u;
        op = 0xe3;
    } else if (streq_ci(mn, "jecxz")) {
        counter = 32u;
        op = 0xe3;
    } else if (streq_ci(mn, "jrcxz")) {
        counter = 64u;
        op = 0xe3;
    } else {
        for (i = 0; i < sizeof(loops) / sizeof(loops[0]); ++i) {
            size_t n = strlen(loops[i].name);

            if (strncasecmp(mn, loops[i].name, n) != 0) {
                continue;
            }
            if (mn[n] == '\0') {
                counter = code_bits;
            } else if (mn[n + 1] != '\0') {
                continue;
            } else if (mn[n] == 'w' || mn[n] == 'W') {
                counter = 16u;
            } else if (mn[n] == 'l' || mn[n] == 'L') {
                counter = 32u;
            } else if (mn[n] == 'q' || mn[n] == 'Q') {
                counter = 64u;
            } else {
                continue;
            }
            op = loops[i].opcode;
            break;
        }
        if (op == 0) {
            return -1;
        }
    }
    if ((counter == 64u && code_bits != 64u) || (counter == 16u && code_bits == 64u)) {
        return -2;
    }
    if (need67 != NULL) {
        *need67 = counter != code_bits;
    }
    if (opcode != NULL) {
        *opcode = op;
    }
    return 0;
}

static int is_rel_mnemonic(const char *mn) {
    if (mn == NULL || mn[0] == '\0') {
        return 0;
    }
    if (streq_ci(mn, "call") || streq_ci(mn, "callw") || streq_ci(mn, "calll") || streq_ci(mn, "callq") ||
        streq_ci(mn, "jmp") || streq_ci(mn, "xbegin") || x86_fixed_short_branch(mn, 32u, NULL, NULL) != -1) {
        return 1;
    }
    if ((mn[0] == 'j' || mn[0] == 'J') && mn[1] != '\0') {
        return 1;
    }
    return 0;
}

/* (`callw` and `calll` are calls too: taken for some other instruction
 * that names a symbol, they had the relocation of an address and not
 * of a distance.) */
static int is_call_mnemonic(const char *mn) {
    return streq_ci(mn, "call") || streq_ci(mn, "lcall") || streq_ci(mn, "callw") || streq_ci(mn, "calll") ||
           streq_ci(mn, "callq");
}

static int is_fixed_short_rel_mnemonic(const char *mn) {
    if (mn == NULL || mn[0] == '\0') {
        return 0;
    }
    return x86_fixed_short_branch(mn, 32u, NULL, NULL) != -1;
}

/*
 * `data16` before a call or a jump to a label: the distance is of the
 * size the code's is not, behind the prefix -- and the prefix is there
 * though the jump be a short one.  (Before `loop` and its like it says
 * nothing and is not written, as GNU as has it.)  With a label the
 * assembler knew, the prefix was left out and nothing said; with one
 * it did not, the distance had four bytes behind a prefix that said
 * two.
 */
static int x86_branch_flip16(const as_stmt_t *st) {
    return st != NULL && st->kind == AS_STMT_INSTRUCTION && (st->u.instr.prefixes & AS_PREFIX_DATA16) != 0 &&
           x86_fixed_short_branch(st->u.instr.mnemonic, 32u, NULL, NULL) == -1;
}

/*
 * A size prefix by its name: a mode has the one that names the size it
 * is not -- `data32` and `addr32` in 16-bit code, `data16` and `addr16`
 * in 32-bit, `data16` and `addr32` in 64-bit -- and the other says
 * nothing a processor would hear.  `addr32 nop` in 32-bit code was
 * assembled with the byte, which there means 16.  NULL, or the name
 * that is wrong.
 */
static const char *x86_size_word_wrong(const as_elf_cfg_t *cfg, const as_stmt_t *st) {
    unsigned p = st->u.instr.prefixes;
    int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;

    if ((p & AS_PREFIX_DATA16) != 0 && ((p & AS_PREFIX_DATA_32) != 0) != code16) {
        return code16 ? "data16" : "data32";
    }
    if ((p & AS_PREFIX_ADDR16) != 0 && ((p & AS_PREFIX_ADDR_32) != 0) != (code16 || cfg->is_64)) {
        return (p & AS_PREFIX_ADDR_32) != 0 ? "addr32" : "addr16";
    }
    return NULL;
}

/* The size letter such a branch has: its own, or the prefix's. */
static char x86_branch_size_letter(const as_stmt_t *st, char suffix, unsigned code_bits) {
    return x86_branch_flip16(st) ? (code_bits == 16u ? 'l' : 'w') : suffix;
}

/* How long one is: two bytes, and the prefix where the mode wants it. */
static size_t fixed_short_rel_size(const char *mn, unsigned code_bits) {
    int need67 = 0;

    return x86_fixed_short_branch(mn, code_bits, &need67, NULL) == 0 && need67 ? 3u : 2u;
}

static int is_size_suffixable_base(const char *mn) {
    static const char *const names[] = {
        "add", "adc", "sbb", "sub", "and", "or", "xor", "cmp", "mov", "lea", "imul", "shl",
        "shr", "sar", "ror", "rol", "rcl", "rcr", "bt", "bts", "btr", "btc", "bsf", "bsr",
        "movsx", "movzx", "test", "push", "pushf", "popf", "xadd", "xchg", "cmpxchg",
        "inc", "dec", "not", "neg", "mul", "div", "idiv", "enter", "call", "jmp", "lcall", "ljmp",
        "sldt", "str", "lldt", "ltr", "verr", "verw", "sgdt", "sidt", "lgdt", "lidt", "smsw", "lmsw",
        "pop", "ret", "leave",
        "movs", "lods", "stos", "scas", "cmps", "ins", "outs",
        "cmovo", "cmovno", "cmovb", "cmovae", "cmovc", "cmovnc", "cmovnae", "cmovnb",
        "cmove", "cmovne", "cmovz", "cmovnz",
        "cmovbe", "cmova", "cmovna", "cmovnbe",
        "cmovs", "cmovns",
        "cmovp", "cmovnp", "cmovpe", "cmovpo",
        "cmovl", "cmovge", "cmovnge", "cmovnl",
        "cmovle", "cmovg", "cmovng", "cmovnle",
        "popcnt", "lzcnt", "tzcnt", "bswap",
        "in", "out", "nop",
    };
    size_t i;

    if (mn == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(mn, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int has_x86_size_suffix(const char *mn, char *suffix_out) {
    size_t n;
    char c;
    char tmp[32];

    if (mn == NULL) {
        return 0;
    }
    n = strlen(mn);
    if (n < 2) {
        return 0;
    }
    c = mn[n - 1];
    if (c != 'b' && c != 'w' && c != 'l' && c != 'q') {
        return 0;
    }
    if (n >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, mn, n - 1);
    tmp[n - 1] = '\0';
    if (!is_size_suffixable_base(tmp)) {
        /* Instructions that take some of the suffixes and not all:
         * none of them has a form of a byte. */
        static const struct {
            const char *base;
            const char *suffixes;
        } some[] = {
            {"shld", "wlq"},  {"shrd", "wlq"}, {"lar", "wlq"},    {"lsl", "wlq"},  {"movnti", "lq"}, {"bound", "wl"},
            {"lss", "wl"},    {"lfs", "wl"},   {"lgs", "wl"},     {"lds", "wl"},   {"les", "wl"},    {"iret", "wlq"},
            {"lret", "wlq"},  {"movbe", "wlq"}, {"adcx", "lq"},   {"adox", "lq"},
        };
        size_t i;

        for (i = 0; i < sizeof(some) / sizeof(some[0]); ++i) {
            if (strcmp(tmp, some[i].base) == 0 && strchr(some[i].suffixes, c) != NULL) {
                break;
            }
        }
        if (i == sizeof(some) / sizeof(some[0])) {
            return 0;
        }
    }
    if (suffix_out != NULL) {
        *suffix_out = c;
    }
    return 1;
}

static int normalize_x86_mnemonic(const char *src, char *dst, size_t dst_sz, char *suffix_out) {
    size_t i;
    size_t n;
    char suffix = '\0';

    if (src == NULL || dst == NULL || dst_sz == 0) {
        return -1;
    }
    n = strlen(src);
    if (n + 1 > dst_sz) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[n] = '\0';

    /* sal is shl by its other name: the same instruction, and the name a
     * compiler writes for a signed operand.  The suffix, if there is one,
     * is taken off below as shl's is. */
    if (strncmp(dst, "sal", 3) == 0 &&
        (n == 3 || (n == 4 && (dst[3] == 'b' || dst[3] == 'w' || dst[3] == 'l' || dst[3] == 'q')))) {
        dst[1] = 'h';
    }

    if (strcmp(dst, "movslq") == 0) {
        if (dst_sz < sizeof("movsxd")) {
            return -1;
        }
        memcpy(dst, "movsxd", sizeof("movsxd"));
        suffix = 'q';
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strcmp(dst, "movsbq") == 0 || strcmp(dst, "movsbl") == 0 || strcmp(dst, "movsbw") == 0) {
        if (dst_sz < sizeof("movsxb")) {
            return -1;
        }
        memcpy(dst, "movsxb", sizeof("movsxb"));
        if (strcmp(dst, "movsxb") == 0 && n >= 1) {
            char last = (char)tolower((unsigned char)src[n - 1]);
            suffix = (last == 'q' || last == 'l' || last == 'w') ? last : 'l';
        }
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strcmp(dst, "movswq") == 0 || strcmp(dst, "movswl") == 0) {
        if (dst_sz < sizeof("movsxw")) {
            return -1;
        }
        memcpy(dst, "movsxw", sizeof("movsxw"));
        suffix = (char)tolower((unsigned char)src[n - 1]) == 'q' ? 'q' : 'l';
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strcmp(dst, "movzbq") == 0 || strcmp(dst, "movzbl") == 0 || strcmp(dst, "movzbw") == 0) {
        if (dst_sz < sizeof("movzxb")) {
            return -1;
        }
        memcpy(dst, "movzxb", sizeof("movzxb"));
        {
            char last = (char)tolower((unsigned char)src[n - 1]);
            suffix = (last == 'q' || last == 'l' || last == 'w') ? last : 'l';
        }
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strcmp(dst, "movzwq") == 0 || strcmp(dst, "movzwl") == 0) {
        if (dst_sz < sizeof("movzxw")) {
            return -1;
        }
        memcpy(dst, "movzxw", sizeof("movzxw"));
        suffix = (char)tolower((unsigned char)src[n - 1]) == 'q' ? 'q' : 'l';
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    /* movabs and its four suffixes; with none it is as wide as its
     * register, which the emitter reads, and q stands for that. */
    if (strncmp(dst, "movabs", 6) == 0 &&
        (n == 6 || (n == 7 && (dst[6] == 'b' || dst[6] == 'w' || dst[6] == 'l' || dst[6] == 'q')))) {
        if (dst_sz < sizeof("movabs")) {
            return -1;
        }
        suffix = (n == 7 && dst[6] != 'q') ? dst[6] : 'q';
        memcpy(dst, "movabs", sizeof("movabs"));
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strcmp(dst, "movabsq") == 0 || strcmp(dst, "movabs") == 0) {
        if (dst_sz < sizeof("movabs")) {
            return -1;
        }
        memcpy(dst, "movabs", sizeof("movabs"));
        suffix = 'q';
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strcmp(dst, "sysretd") == 0) {
        if (dst_sz < sizeof("sysretl")) {
            return -1;
        }
        memcpy(dst, "sysretl", sizeof("sysretl"));
        return 0;
    }
    if (strcmp(dst, "sysexitd") == 0) {
        if (dst_sz < sizeof("sysexitl")) {
            return -1;
        }
        memcpy(dst, "sysexitl", sizeof("sysexitl"));
        return 0;
    }
    if (strcmp(dst, "retfq") == 0) {
        if (dst_sz < sizeof("lretq")) {
            return -1;
        }
        memcpy(dst, "lretq", sizeof("lretq"));
        return 0;
    }
    if (strcmp(dst, "retfw") == 0) {
        if (dst_sz < sizeof("lretw")) {
            return -1;
        }
        memcpy(dst, "lretw", sizeof("lretw"));
        return 0;
    }
    if (strcmp(dst, "retfd") == 0) {
        if (dst_sz < sizeof("lret")) {
            return -1;
        }
        memcpy(dst, "lret", sizeof("lret"));
        return 0;
    }
    if (strncmp(dst, "cvtsi2sd", 8) == 0 && n == 9 &&
        (dst[8] == 'q' || dst[8] == 'l' || dst[8] == 'w')) {
        dst[8] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "cvtsi2ss", 8) == 0 && n == 9 &&
        (dst[8] == 'q' || dst[8] == 'l' || dst[8] == 'w')) {
        dst[8] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "cvttsd2si", 9) == 0 && n == 10 &&
        (dst[9] == 'q' || dst[9] == 'l' || dst[9] == 'w')) {
        dst[9] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "cvttss2si", 9) == 0 && n == 10 &&
        (dst[9] == 'q' || dst[9] == 'l' || dst[9] == 'w')) {
        dst[9] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvtsi2sd", 9) == 0 && n == 10 &&
        (dst[9] == 'q' || dst[9] == 'l' || dst[9] == 'w')) {
        dst[9] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvtsi2ss", 9) == 0 && n == 10 &&
        (dst[9] == 'q' || dst[9] == 'l' || dst[9] == 'w')) {
        dst[9] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvttsd2si", 10) == 0 && n == 11 &&
        (dst[10] == 'q' || dst[10] == 'l' || dst[10] == 'w')) {
        dst[10] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvttss2si", 10) == 0 && n == 11 &&
        (dst[10] == 'q' || dst[10] == 'l' || dst[10] == 'w')) {
        dst[10] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvtusi2sd", 10) == 0 && n == 11 &&
        (dst[10] == 'q' || dst[10] == 'l' || dst[10] == 'w')) {
        dst[10] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvtusi2ss", 10) == 0 && n == 11 &&
        (dst[10] == 'q' || dst[10] == 'l' || dst[10] == 'w')) {
        dst[10] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "crc32", 5) == 0 && n == 6 &&
        (dst[5] == 'b' || dst[5] == 'w' || dst[5] == 'l' || dst[5] == 'q')) {
        dst[5] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (x86_mnemonic_keeps_trailing_size_letter(dst)) {
        if (suffix_out != NULL) {
            *suffix_out = '\0';
        }
        return 0;
    }

    if (has_x86_size_suffix(dst, &suffix)) {
        dst[n - 1] = '\0';
    } else {
        suffix = '\0';
    }
    if (suffix_out != NULL) {
        *suffix_out = suffix;
    }
    return 0;
}

static int parse_xmm_reg(const char *name, unsigned *out) {
    /* (To %xmm15 and %ymm15: the encoders that take these are the ones
     * without a prefix that reaches further.) */
    return reg_of_class(name, AS_X86_REGCLASS_XMM, 15, out);
}

static int parse_ymm_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_YMM, 15, out);
}

static int parse_zmm_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_ZMM, 31, out);
}

static int parse_mmx_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_MMX, 7, out);
}

static int parse_k_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_MASK, 7, out);
}

static int parse_st_index(const char *s, unsigned *out) {
    const char *p;
    unsigned v = 0;

    if (s == NULL || out == NULL) {
        return -1;
    }
    p = s;
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    if (!(p[0] == 's' || p[0] == 'S') || !(p[1] == 't' || p[1] == 'T')) {
        return -1;
    }
    if (p[2] == '\0') {
        *out = 0;
        return 0;
    }
    if (p[2] != '(') {
        return -1;
    }
    p += 3;
    if (!isdigit((unsigned char)*p)) {
        return -1;
    }
    while (isdigit((unsigned char)*p)) {
        v = (v * 10u) + (unsigned)(*p - '0');
        ++p;
    }
    if (*p != ')' || p[1] != '\0' || v > 7u) {
        return -1;
    }
    *out = v;
    return 0;
}

static int is_x86_low8_reg(const char *name) {
    const as_x86_reg_info_t *r = as_x86_reg_find_class(name, AS_X86_REGCLASS_GENERAL, 15);

    return r != NULL && r->width == 8 && (r->flags & AS_X86_REGF_HIGH8) == 0;
}

static int emit_x86_64_xmm_memop(unsigned char prefix, unsigned char opcode, unsigned xr, const as_mem_operand_t *mem,
                                 unsigned char *out, size_t out_cap, size_t *out_len) {
    as_x86_reg_t base;
    unsigned breg;
    long long disp = 0;
    int has_disp = 0;
    int rip_relative = 0;
    unsigned char rex;
    unsigned char modrm;
    unsigned char mod;
    int need_sib;
    size_t pos = 0;

    if (mem == NULL || out == NULL || out_len == NULL || out_cap < 10) {
        return -1;
    }
    if (mem->segment_reg != NULL) {
        if (emit_seg_override_byte(out, out_cap, &pos, mem->segment_reg) != 0) {
            return -1;
        }
    }
    if (mem->base_reg == NULL) {
        return -1;
    }
    if (streq_ci(mem->base_reg, "rip") || streq_ci(mem->base_reg, "%rip")) {
        rip_relative = 1;
        base = AS_X86_REG_RBP; /* placeholder for r/m=101 */
    } else if (parse_x86_reg(mem->base_reg, &base) != 0) {
        return -1;
    }
    if (mem->index_reg != NULL) {
        return -1;
    }
    if (mem->disp != NULL) {
        if (eval_expr_const(mem->disp, &disp) != 0) {
            if (expr_has_symbol(mem->disp)) {
                disp = 0;
            } else {
                return -1;
            }
        }
        has_disp = 1;
    }

    breg = (unsigned)base & 15u;
    if (rip_relative) {
        breg = 5u;
    }
    need_sib = ((breg & 7u) == 4u) ? 1 : 0;
    if (!has_disp && ((breg & 7u) == 5u)) {
        has_disp = 1;
        disp = 0;
    }
    if (rip_relative) {
        mod = 0;
        has_disp = 1;
    } else if (!has_disp) {
        mod = 0;
    } else if (!expr_has_symbol(mem->disp) && disp >= -128 && disp <= 127) {
        /* (A symbol's displacement is 32 bits: see the 32-bit emitter.) */
        mod = 1;
    } else {
        mod = 2;
    }

    if (prefix != 0u) {
        out[pos++] = prefix;
    }
    rex = (unsigned char)(0x40u | ((xr & 8u) ? 0x04u : 0u) | ((breg & 8u) ? 0x01u : 0u));
    if (rex != 0x40u) {
        out[pos++] = rex;
    }
    out[pos++] = 0x0f;
    out[pos++] = opcode;
    modrm = (unsigned char)((mod << 6) | ((xr & 7u) << 3) | (breg & 7u));
    out[pos++] = modrm;
    if (need_sib) {
        out[pos++] = (unsigned char)(0x20u | (breg & 7u)); /* scale=1,index=none,base=breg */
    }
    if (rip_relative || mod == 2) {
        out[pos++] = (unsigned char)(disp & 0xff);
        out[pos++] = (unsigned char)((disp >> 8) & 0xff);
        out[pos++] = (unsigned char)((disp >> 16) & 0xff);
        out[pos++] = (unsigned char)((disp >> 24) & 0xff);
    } else if (mod == 1) {
        out[pos++] = (unsigned char)((int8_t)disp);
    }
    *out_len = pos;
    return 0;
}

static int emit_x86_64_regfield_memop(unsigned char prefix, unsigned char opcode, unsigned reg_field, int rex_w,
                                      const as_mem_operand_t *mem, unsigned char *out, size_t out_cap, size_t *out_len) {
    as_x86_reg_t base;
    unsigned breg;
    long long disp = 0;
    int has_disp = 0;
    int rip_relative = 0;
    unsigned char rex;
    unsigned char modrm;
    unsigned char mod;
    int need_sib;
    size_t pos = 0;

    if (mem == NULL || out == NULL || out_len == NULL || out_cap < 10) {
        return -1;
    }
    if (mem->segment_reg != NULL) {
        if (emit_seg_override_byte(out, out_cap, &pos, mem->segment_reg) != 0) {
            return -1;
        }
    }
    if (mem->base_reg == NULL) {
        return -1;
    }
    if (streq_ci(mem->base_reg, "rip") || streq_ci(mem->base_reg, "%rip")) {
        rip_relative = 1;
        base = AS_X86_REG_RBP; /* placeholder for r/m=101 */
    } else if (parse_x86_reg(mem->base_reg, &base) != 0) {
        return -1;
    }
    if (mem->index_reg != NULL) {
        return -1;
    }
    if (mem->disp != NULL) {
        if (eval_expr_const(mem->disp, &disp) != 0) {
            if (expr_has_symbol(mem->disp)) {
                disp = 0;
            } else {
                return -1;
            }
        }
        has_disp = 1;
    }

    breg = (unsigned)base & 15u;
    if (rip_relative) {
        breg = 5u;
    }
    need_sib = ((breg & 7u) == 4u) ? 1 : 0;
    if (!has_disp && ((breg & 7u) == 5u)) {
        has_disp = 1;
        disp = 0;
    }
    if (rip_relative) {
        mod = 0;
        has_disp = 1;
    } else if (!has_disp) {
        mod = 0;
    } else if (!expr_has_symbol(mem->disp) && disp >= -128 && disp <= 127) {
        /* (A symbol's displacement is 32 bits: see the 32-bit emitter.) */
        mod = 1;
    } else {
        mod = 2;
    }

    if (prefix != 0u) {
        out[pos++] = prefix;
    }
    rex = (unsigned char)(0x40u | (rex_w ? 0x08u : 0u) | ((reg_field & 8u) ? 0x04u : 0u) | ((breg & 8u) ? 0x01u : 0u));
    if (rex != 0x40u) {
        out[pos++] = rex;
    }
    out[pos++] = 0x0f;
    out[pos++] = opcode;
    modrm = (unsigned char)((mod << 6) | ((reg_field & 7u) << 3) | (breg & 7u));
    out[pos++] = modrm;
    if (need_sib) {
        out[pos++] = (unsigned char)(0x20u | (breg & 7u)); /* scale=1,index=none,base=breg */
    }
    if (rip_relative || mod == 2) {
        out[pos++] = (unsigned char)(disp & 0xff);
        out[pos++] = (unsigned char)((disp >> 8) & 0xff);
        out[pos++] = (unsigned char)((disp >> 16) & 0xff);
        out[pos++] = (unsigned char)((disp >> 24) & 0xff);
    } else if (mod == 1) {
        out[pos++] = (unsigned char)((int8_t)disp);
    }
    *out_len = pos;
    return 0;
}

static int emit_x86_64_1byte_regfield_memop(unsigned char opcode, unsigned reg_field, const as_mem_operand_t *mem,
                                            unsigned char *out, size_t out_cap, size_t *out_len) {
    as_x86_reg_t base;
    unsigned breg;
    long long disp = 0;
    int has_disp = 0;
    int rip_relative = 0;
    int no_base = 0;
    int has_index = 0;
    unsigned ireg = 0;
    unsigned scale_bits = 0;
    unsigned char rex;
    unsigned char modrm;
    unsigned char mod;
    int need_sib;
    size_t pos = 0;

    if (mem == NULL || out == NULL || out_len == NULL || out_cap < 12) {
        return -1;
    }
    if (mem->segment_reg != NULL) {
        if (emit_seg_override_byte(out, out_cap, &pos, mem->segment_reg) != 0) {
            return -1;
        }
    }
    /* An address with no register: in 64-bit code that is a SIB byte
     * that names none, and four bytes. */
    if (mem->base_reg == NULL && mem->index_reg == NULL && mem->disp != NULL) {
        if (eval_expr_const(mem->disp, &disp) != 0) {
            if (!expr_has_symbol(mem->disp)) {
                return -1;
            }
            disp = 0;
        }
        if (pos + 7 > out_cap) {
            return -1;
        }
        out[pos++] = opcode;
        out[pos++] = (unsigned char)(((reg_field & 7u) << 3) | 4u);
        out[pos++] = 0x25;
        out[pos++] = (unsigned char)(disp & 0xff);
        out[pos++] = (unsigned char)((disp >> 8) & 0xff);
        out[pos++] = (unsigned char)((disp >> 16) & 0xff);
        out[pos++] = (unsigned char)((disp >> 24) & 0xff);
        *out_len = pos;
        return 0;
    }
    if (mem->base_reg == NULL && mem->index_reg == NULL) {
        return -1;
    }
    if (mem->base_reg == NULL) {
        /* An index and no base: the SIB byte names no base, and the
         * displacement is of four bytes whatever it is. */
        no_base = 1;
        base = AS_X86_REG_RBP;
    } else if (streq_ci(mem->base_reg, "rip") || streq_ci(mem->base_reg, "%rip")) {
        rip_relative = 1;
        base = AS_X86_REG_RBP;
    } else if (parse_x86_reg(mem->base_reg, &base) != 0) {
        return -1;
    }
    /*
     * An index.  There was none: an x87 instruction or a move of a
     * segment register with `(%rax,%rbx,8)` was "unsupported".
     */
    if (mem->index_reg != NULL) {
        as_x86_reg_t index;

        int iw = x86_reg_width_bits(mem->index_reg);

        if (rip_relative || parse_x86_reg(mem->index_reg, &index) != 0 || ((unsigned)index & 15u) == 4u) {
            return -1;
        }
        /* Of 32 bits or 64, and of the width the base is. */
        if ((iw != 32 && iw != 64) || (!no_base && x86_reg_width_bits(mem->base_reg) != iw)) {
            return -1;
        }
        switch (mem->scale) {
        case 0:
        case 1: scale_bits = 0; break;
        case 2: scale_bits = 1; break;
        case 4: scale_bits = 2; break;
        case 8: scale_bits = 3; break;
        default: return -1;
        }
        ireg = (unsigned)index & 15u;
        has_index = 1;
    }
    if (mem->disp != NULL) {
        if (eval_expr_const(mem->disp, &disp) != 0) {
            if (expr_has_symbol(mem->disp)) {
                disp = 0;
            } else {
                return -1;
            }
        }
        /* A displacement of nothing is none: `fldl 0(%r8)` had a byte
         * of zero for it.  (The frame pointer and %r13 have no form
         * without one, and are given it below.) */
        has_disp = disp != 0 || expr_has_symbol(mem->disp);
    }

    breg = (unsigned)base & 15u;
    if (rip_relative || no_base) {
        breg = 5u;
    }
    need_sib = (has_index || (breg & 7u) == 4u) ? 1 : 0;
    if (!has_disp && ((breg & 7u) == 5u)) {
        has_disp = 1;
        disp = 0;
    }
    if (rip_relative || no_base) {
        mod = 0;
        has_disp = 1;
    } else if (!has_disp) {
        mod = 0;
    } else if (!expr_has_symbol(mem->disp) && disp >= -128 && disp <= 127) {
        /* (A symbol's displacement is 32 bits: see the 32-bit emitter.) */
        mod = 1;
    } else {
        mod = 2;
    }

    rex = (unsigned char)(0x40u | ((breg & 8u) && !no_base ? 0x01u : 0u) | ((has_index && (ireg & 8u)) ? 0x02u : 0u));
    if (rex != 0x40u) {
        out[pos++] = rex;
    }
    out[pos++] = opcode;
    modrm = (unsigned char)((mod << 6) | ((reg_field & 7u) << 3) | (need_sib ? 4u : (breg & 7u)));
    out[pos++] = modrm;
    if (need_sib) {
        out[pos++] = (unsigned char)((scale_bits << 6) | ((has_index ? (ireg & 7u) : 4u) << 3) | (breg & 7u));
    }
    if (rip_relative || no_base || mod == 2) {
        out[pos++] = (unsigned char)(disp & 0xff);
        out[pos++] = (unsigned char)((disp >> 8) & 0xff);
        out[pos++] = (unsigned char)((disp >> 16) & 0xff);
        out[pos++] = (unsigned char)((disp >> 24) & 0xff);
    } else if (mod == 1) {
        out[pos++] = (unsigned char)((int8_t)disp);
    }
    *out_len = pos;
    return 0;
}

static int emit_x86_64_x87_mem_by_size(const as_operand_t *op,
                                       int opcode32, unsigned reg32,
                                       int opcode64, unsigned reg64,
                                       int opcode80, unsigned reg80,
                                       unsigned char *out, size_t out_cap, size_t *out_len) {
    int bits;

    if (op == NULL || op->kind != AS_OPERAND_MEMORY) {
        return -1;
    }
    bits = op->u.mem.size_bits;
    if (bits == 32 && opcode32 >= 0) {
        return emit_x86_64_1byte_regfield_memop((unsigned char)opcode32, reg32, &op->u.mem, out, out_cap, out_len);
    }
    if (bits == 64 && opcode64 >= 0) {
        return emit_x86_64_1byte_regfield_memop((unsigned char)opcode64, reg64, &op->u.mem, out, out_cap, out_len);
    }
    if (bits == 80 && opcode80 >= 0) {
        return emit_x86_64_1byte_regfield_memop((unsigned char)opcode80, reg80, &op->u.mem, out, out_cap, out_len);
    }
    return -1;
}

static int emit_x86_64_x87_mem16_32(const as_operand_t *op,
                                    int opcode16, unsigned reg16,
                                    int opcode32, unsigned reg32,
                                    unsigned char *out, size_t out_cap, size_t *out_len) {
    int bits;

    if (op == NULL || op->kind != AS_OPERAND_MEMORY) {
        return -1;
    }
    bits = op->u.mem.size_bits;
    if (bits == 16 && opcode16 >= 0) {
        return emit_x86_64_1byte_regfield_memop((unsigned char)opcode16, reg16, &op->u.mem, out, out_cap, out_len);
    }
    if (bits == 32 && opcode32 >= 0) {
        return emit_x86_64_1byte_regfield_memop((unsigned char)opcode32, reg32, &op->u.mem, out, out_cap, out_len);
    }
    return -1;
}

static int emit_x86_64_x87_mem16_32_64(const as_operand_t *op,
                                       int opcode16, unsigned reg16,
                                       int opcode32, unsigned reg32,
                                       int opcode64, unsigned reg64,
                                       unsigned char *out, size_t out_cap, size_t *out_len) {
    int bits;

    if (op == NULL || op->kind != AS_OPERAND_MEMORY) {
        return -1;
    }
    bits = op->u.mem.size_bits;
    if (bits == 16 && opcode16 >= 0) {
        return emit_x86_64_1byte_regfield_memop((unsigned char)opcode16, reg16, &op->u.mem, out, out_cap, out_len);
    }
    if (bits == 32 && opcode32 >= 0) {
        return emit_x86_64_1byte_regfield_memop((unsigned char)opcode32, reg32, &op->u.mem, out, out_cap, out_len);
    }
    if (bits == 64 && opcode64 >= 0) {
        return emit_x86_64_1byte_regfield_memop((unsigned char)opcode64, reg64, &op->u.mem, out, out_cap, out_len);
    }
    return -1;
}

static int lookup_x86_64_x87_mem_exact(const char *mnemonic, unsigned char *opcode, unsigned *reg_field) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode;
        unsigned reg_field;
    } map[] = {
        {"fldt", 0xdb, 5u},
        {"fcoms", 0xd8, 2u},
        {"fcomps", 0xd8, 3u},
        {"fcoml", 0xdc, 2u},
        {"fcompl", 0xdc, 3u},
        {"fstpl", 0xdd, 3u},
        {"fbld", 0xdf, 4u},
        {"fbstp", 0xdf, 6u},
    };
    size_t i;

    if (mnemonic == NULL || opcode == NULL || reg_field == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode = map[i].opcode;
            *reg_field = map[i].reg_field;
            return 0;
        }
    }
    return -1;
}

static int lookup_x86_64_x87_mem16_32(const char *mnemonic, unsigned char *opcode16, unsigned *reg16,
                                      unsigned char *opcode32, unsigned *reg32) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode16;
        unsigned reg16;
        unsigned char opcode32;
        unsigned reg32;
    } map[] = {
        {"fiadd", 0xde, 0u, 0xda, 0u},
        {"fimul", 0xde, 1u, 0xda, 1u},
        {"ficom", 0xde, 2u, 0xda, 2u},
        {"ficomp", 0xde, 3u, 0xda, 3u},
        {"fisub", 0xde, 4u, 0xda, 4u},
        {"fisubr", 0xde, 5u, 0xda, 5u},
        {"fidiv", 0xde, 6u, 0xda, 6u},
        {"fidivr", 0xde, 7u, 0xda, 7u},
        {"fist", 0xdf, 2u, 0xdb, 2u},
    };
    size_t i;

    if (mnemonic == NULL || opcode16 == NULL || reg16 == NULL || opcode32 == NULL || reg32 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode16 = map[i].opcode16;
            *reg16 = map[i].reg16;
            *opcode32 = map[i].opcode32;
            *reg32 = map[i].reg32;
            return 0;
        }
    }
    return -1;
}

static int lookup_x86_64_x87_mem16_32_64(const char *mnemonic, unsigned char *opcode16, unsigned *reg16,
                                         unsigned char *opcode32, unsigned *reg32,
                                         unsigned char *opcode64, unsigned *reg64) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode16;
        unsigned reg16;
        unsigned char opcode32;
        unsigned reg32;
        unsigned char opcode64;
        unsigned reg64;
    } map[] = {
        {"fild", 0xdf, 0u, 0xdb, 0u, 0xdf, 5u},
        {"fistp", 0xdf, 3u, 0xdb, 3u, 0xdf, 7u},
        {"fisttp", 0xdf, 1u, 0xdb, 1u, 0xdd, 1u},
    };
    size_t i;

    if (mnemonic == NULL || opcode16 == NULL || reg16 == NULL || opcode32 == NULL || reg32 == NULL ||
        opcode64 == NULL || reg64 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode16 = map[i].opcode16;
            *reg16 = map[i].reg16;
            *opcode32 = map[i].opcode32;
            *reg32 = map[i].reg32;
            *opcode64 = map[i].opcode64;
            *reg64 = map[i].reg64;
            return 0;
        }
    }
    return -1;
}

/* An address written bare: a symbol, or a number with no `$` (which the
 * parser gives the kind of an immediate, there being no other). */
static int operand_is_bare_address(const as_operand_t *op) {
    if (op->kind == AS_OPERAND_LABEL_REF) {
        return 1;
    }
    return op->kind == AS_OPERAND_IMMEDIATE && op->raw != NULL && op->raw[0] != '$';
}

/*
 * The x87 instructions on the register stack, in AT&T syntax, and the
 * memory forms that had no entry anywhere.
 *
 * What was here took the arithmetic instructions only as `op %st(i),
 * %st`.  A compiler writes `faddp %st, %st(1)`; a person writes `faddp`,
 * `fxch`, `fadd %st(2)`.  None of those assembled, so no 32-bit code
 * doing arithmetic on doubles did.  The encodings are GNU as's, which
 * for the register forms of fsub, fsubr, fdiv and fdivr are the ones
 * AT&T syntax has always had.
 *
 * Returns 0 with the instruction in `out`, or -1 if it is not one of
 * these, for the encoders after this to try.
 */
static int operand_st_index(const as_operand_t *op, unsigned *out);

static int emit_x87_forms(const as_instruction_t *insn, int intel_syntax, int is_64,
                          unsigned char *out, size_t out_cap, size_t *out_len) {
    /* fadd, fmul, ...: D8 base+i to %st, DC base+i from %st, DE base+1
     * with no operand; and with p, DE base+i. */
    static const struct { const char *name; unsigned char base; } arith[] = {
        { "fadd", 0xc0 }, { "fmul", 0xc8 }, { "fsub", 0xe0 },
        { "fsubr", 0xe8 }, { "fdiv", 0xf0 }, { "fdivr", 0xf8 },
    };
    /* One register, and for the first five %st(1) if none is written. */
    static const struct { const char *name; unsigned char op, base; int may_omit; } single[] = {
        { "fxch", 0xd9, 0xc8, 1 }, { "fcom", 0xd8, 0xd0, 1 }, { "fcomp", 0xd8, 0xd8, 1 },
        { "fucom", 0xdd, 0xe0, 1 }, { "fucomp", 0xdd, 0xe8, 1 },
        { "ffree", 0xdd, 0xc0, 0 }, { "fld", 0xd9, 0xc0, 0 },
        { "fst", 0xdd, 0xd0, 0 }, { "fstp", 0xdd, 0xd8, 0 },
    };
    /* `op %st(i), %st`. */
    static const struct { const char *name; unsigned char op, base; int one_operand; } to_top[] = {
        { "fcomi", 0xdb, 0xf0, 1 }, { "fcomip", 0xdf, 0xf0, 1 },
        { "fucomi", 0xdb, 0xe8, 1 }, { "fucomip", 0xdf, 0xe8, 1 },
        { "fcmovb", 0xda, 0xc0, 0 }, { "fcmove", 0xda, 0xc8, 0 },
        { "fcmovbe", 0xda, 0xd0, 0 }, { "fcmovu", 0xda, 0xd8, 0 },
        { "fcmovnb", 0xdb, 0xc0, 0 }, { "fcmovne", 0xdb, 0xc8, 0 },
        { "fcmovnbe", 0xdb, 0xd0, 0 }, { "fcmovnu", 0xdb, 0xd8, 0 },
    };
    /*
     * Memory operands: the opcode and its /digit, and whether a wait goes
     * first.  s is 32 bits for a float and 16 for an integer, l is 64
     * for a float and 32 for an integer, q and ll are a 64-bit integer,
     * t is ten bytes; an integer instruction with no suffix is 16 bits,
     * as GNU as takes it.
     */
    static const struct { const char *name; unsigned char wait, op, digit; } memory[] = {
        { "fadds", 0, 0xd8, 0 }, { "faddl", 0, 0xdc, 0 }, { "fmuls", 0, 0xd8, 1 }, { "fmull", 0, 0xdc, 1 },
        { "fcoms", 0, 0xd8, 2 }, { "fcoml", 0, 0xdc, 2 }, { "fcomps", 0, 0xd8, 3 }, { "fcompl", 0, 0xdc, 3 },
        { "fadd", 0, 0xd8, 0 }, { "fmul", 0, 0xd8, 1 }, { "fcom", 0, 0xd8, 2 }, { "fcomp", 0, 0xd8, 3 },
        { "fsub", 0, 0xd8, 4 }, { "fsubr", 0, 0xd8, 5 }, { "fdiv", 0, 0xd8, 6 }, { "fdivr", 0, 0xd8, 7 },
        { "fsubs", 0, 0xd8, 4 }, { "fsubl", 0, 0xdc, 4 }, { "fsubrs", 0, 0xd8, 5 }, { "fsubrl", 0, 0xdc, 5 },
        { "fdivs", 0, 0xd8, 6 }, { "fdivl", 0, 0xdc, 6 }, { "fdivrs", 0, 0xd8, 7 }, { "fdivrl", 0, 0xdc, 7 },
        { "fiaddl", 0, 0xda, 0 }, { "fiadds", 0, 0xde, 0 }, { "fiadd", 0, 0xde, 0 },
        { "fimull", 0, 0xda, 1 }, { "fimuls", 0, 0xde, 1 }, { "fimul", 0, 0xde, 1 },
        { "ficoml", 0, 0xda, 2 }, { "ficoms", 0, 0xde, 2 }, { "ficom", 0, 0xde, 2 },
        { "ficompl", 0, 0xda, 3 }, { "ficomps", 0, 0xde, 3 }, { "ficomp", 0, 0xde, 3 },
        { "fisubl", 0, 0xda, 4 }, { "fisubs", 0, 0xde, 4 }, { "fisub", 0, 0xde, 4 },
        { "fisubrl", 0, 0xda, 5 }, { "fisubrs", 0, 0xde, 5 }, { "fisubr", 0, 0xde, 5 },
        { "fidivl", 0, 0xda, 6 }, { "fidivs", 0, 0xde, 6 }, { "fidiv", 0, 0xde, 6 },
        { "fidivrl", 0, 0xda, 7 }, { "fidivrs", 0, 0xde, 7 }, { "fidivr", 0, 0xde, 7 },
        { "flds", 0, 0xd9, 0 }, { "fldl", 0, 0xdd, 0 }, { "fldt", 0, 0xdb, 5 }, { "fld", 0, 0xd9, 0 },
        { "fsts", 0, 0xd9, 2 }, { "fstl", 0, 0xdd, 2 }, { "fst", 0, 0xd9, 2 }, { "fstp", 0, 0xd9, 3 },
        { "fstps", 0, 0xd9, 3 }, { "fstpl", 0, 0xdd, 3 }, { "fstpt", 0, 0xdb, 7 },
        { "filds", 0, 0xdf, 0 }, { "fildl", 0, 0xdb, 0 }, { "fild", 0, 0xdf, 0 },
        { "fildq", 0, 0xdf, 5 }, { "fildll", 0, 0xdf, 5 },
        { "fists", 0, 0xdf, 2 }, { "fistl", 0, 0xdb, 2 }, { "fist", 0, 0xdf, 2 },
        { "fistps", 0, 0xdf, 3 }, { "fistpl", 0, 0xdb, 3 }, { "fistp", 0, 0xdf, 3 },
        { "fistpq", 0, 0xdf, 7 }, { "fistpll", 0, 0xdf, 7 },
        { "fisttps", 0, 0xdf, 1 }, { "fisttpl", 0, 0xdb, 1 }, { "fisttp", 0, 0xdf, 1 },
        { "fisttpq", 0, 0xdd, 1 }, { "fisttpll", 0, 0xdd, 1 },
        { "fldcw", 0, 0xd9, 5 }, { "fnstcw", 0, 0xd9, 7 }, { "fstcw", 1, 0xd9, 7 },
        { "fnstsw", 0, 0xdd, 7 }, { "fstsw", 1, 0xdd, 7 },
        { "fldenv", 0, 0xd9, 4 }, { "fnstenv", 0, 0xd9, 6 }, { "fstenv", 1, 0xd9, 6 },
        { "fnsave", 0, 0xdd, 6 }, { "fsave", 1, 0xdd, 6 }, { "frstor", 0, 0xdd, 4 },
        { "fbld", 0, 0xdf, 4 }, { "fbstp", 0, 0xdf, 6 },
    };
    const as_operand_t *a;
    const as_operand_t *b;
    const char *mn;
    unsigned ia = 0;
    unsigned ib = 0;
    int a_is_st;
    int b_is_st;
    size_t i;
    size_t n;

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_len == NULL || out_cap < 8 ||
        intel_syntax || insn->operand_count > 2 ||
        (insn->mnemonic[0] != 'f' && insn->mnemonic[0] != 'F')) {
        return -1;
    }
    mn = insn->mnemonic;
    a = insn->operand_count > 0 ? &insn->operands[0] : NULL;
    b = insn->operand_count > 1 ? &insn->operands[1] : NULL;
    a_is_st = a != NULL && operand_st_index(a, &ia) == 0 && ia <= 7u;
    b_is_st = b != NULL && operand_st_index(b, &ib) == 0 && ib <= 7u;

    /* fnstsw and fstsw to %ax, written or understood. */
    if ((streq_ci(mn, "fnstsw") || streq_ci(mn, "fstsw")) &&
        (insn->operand_count == 0 ||
         (insn->operand_count == 1 && a->kind == AS_OPERAND_REGISTER && a->u.reg != NULL &&
          (streq_ci(a->u.reg, "ax") || streq_ci(a->u.reg, "%ax"))))) {
        n = 0;
        if (streq_ci(mn, "fstsw")) {
            out[n++] = 0x9b;
        }
        out[n++] = 0xdf;
        out[n++] = 0xe0;
        *out_len = n;
        return 0;
    }

    /* The register forms: every operand that is written is of the stack. */
    if ((a == NULL || a_is_st) && (b == NULL || b_is_st)) {
        for (i = 0; i < sizeof(arith) / sizeof(arith[0]); ++i) {
            size_t len = strlen(arith[i].name);
            int pop;

            if (strncasecmp(mn, arith[i].name, len) != 0) {
                continue;
            }
            if (mn[len] == '\0') {
                pop = 0;
            } else if ((mn[len] == 'p' || mn[len] == 'P') && mn[len + 1] == '\0') {
                pop = 1;
            } else {
                continue;
            }
            if (insn->operand_count == 0) {
                out[0] = 0xde;
                out[1] = (unsigned char)(arith[i].base + 1u);
            } else if (insn->operand_count == 1) {
                out[0] = pop ? 0xde : 0xd8;
                out[1] = (unsigned char)(arith[i].base + ia);
            } else if (!pop && ib == 0u) {
                /* op %st(i), %st */
                out[0] = 0xd8;
                out[1] = (unsigned char)(arith[i].base + ia);
            } else if (ia == 0u) {
                /* op %st, %st(i), and opp %st, %st(i) */
                out[0] = pop ? 0xde : 0xdc;
                out[1] = (unsigned char)(arith[i].base + ib);
            } else if (pop && ib == 0u && (i == 0 || i == 1)) {
                /* faddp and fmulp, which commute, are taken either way. */
                out[0] = 0xde;
                out[1] = (unsigned char)(arith[i].base + ia);
            } else {
                return -1;
            }
            *out_len = 2;
            return 0;
        }
        for (i = 0; i < sizeof(single) / sizeof(single[0]); ++i) {
            if (!streq_ci(mn, single[i].name)) {
                continue;
            }
            if (insn->operand_count == 1) {
                out[1] = (unsigned char)(single[i].base + ia);
            } else if (insn->operand_count == 0 && single[i].may_omit) {
                out[1] = (unsigned char)(single[i].base + 1u);
            } else {
                return -1;
            }
            out[0] = single[i].op;
            *out_len = 2;
            return 0;
        }
        for (i = 0; i < sizeof(to_top) / sizeof(to_top[0]); ++i) {
            if (!streq_ci(mn, to_top[i].name)) {
                continue;
            }
            if (insn->operand_count == 2 && ib == 0u) {
                out[1] = (unsigned char)(to_top[i].base + ia);
            } else if (insn->operand_count == 1 && to_top[i].one_operand) {
                out[1] = (unsigned char)(to_top[i].base + ia);
            } else {
                return -1;
            }
            out[0] = to_top[i].op;
            *out_len = 2;
            return 0;
        }
        return -1;
    }

    /* One memory operand: an address, or a symbol that is one. */
    if (insn->operand_count == 1 && (a->kind == AS_OPERAND_MEMORY || operand_is_bare_address(a))) {
        for (i = 0; i < sizeof(memory) / sizeof(memory[0]); ++i) {
            if (!streq_ci(mn, memory[i].name)) {
                continue;
            }
            n = 0;
            if (memory[i].wait) {
                out[n++] = 0x9b;
            }
            if (is_64) {
                /* The 64-bit emitter of ModRM writes the segment override
                 * and REX itself, and starts at the byte it is given. */
                as_mem_operand_t at;
                size_t len = 0;

                if (a->kind == AS_OPERAND_MEMORY) {
                    at = a->u.mem;
                } else {
                    memset(&at, 0, sizeof(at));
                    at.disp = a->u.expr;
                }
                if (emit_x86_64_1byte_regfield_memop(memory[i].op, memory[i].digit, &at,
                                                     out + n, out_cap - n, &len) != 0) {
                    return -1;
                }
                *out_len = n + len;
                return 0;
            }
            if (a->kind == AS_OPERAND_MEMORY && a->u.mem.segment_reg != NULL &&
                emit_seg_override_byte(out, out_cap, &n, a->u.mem.segment_reg) != 0) {
                return -1;
            }
            out[n++] = memory[i].op;
            if (emit_i386_modrm_rm_operand(memory[i].digit, a, out, out_cap, &n) != 0) {
                return -1;
            }
            *out_len = n;
            return 0;
        }
    }
    return -1;
}

/*
 * movq with an MMX or XMM register.
 *
 * The q of movq was taken for a size suffix and the instruction for mov,
 * and the SIMD register's number for a general register's: in 32-bit
 * code `movq %xmm0, (%esp)` -- how a compiler stores a 64-bit integer it
 * has in an XMM register -- was `movl %eax, (%esp)`.  In 64-bit code the
 * register forms named a general register for the vector one.
 *
 *      xmm/m64 to xmm      F3 0F 7E /r
 *      xmm to m64          66 0F D6 /r
 *      mm/m64 to mm           0F 6F /r
 *      mm to m64              0F 7F /r
 *      r64 to mm, mm to r64   REX.W 0F 6E /r, REX.W 0F 7E /r
 *
 * (Between a 64-bit general register and an XMM register the 64-bit
 * encoder has it right and is left to it.)  0 with the instruction in
 * `out`; -1 if this is not one of those forms.
 */
static int emit_x86_64_mmx_memop(unsigned char opcode, unsigned dst_mmx, const as_mem_operand_t *mem,
                                 unsigned char *out, size_t out_cap, size_t *out_len);


static int emit_simd_movq(const as_instruction_t *insn, int intel_syntax, int is_64,
                          unsigned char *out, size_t out_cap, size_t *out_len) {
    const as_operand_t *src;
    const as_operand_t *dst;
    unsigned sx = 0, dx = 0, sm = 0, dm = 0;
    int src_xmm, dst_xmm, src_mm, dst_mm;
    int src_mem, dst_mem;
    as_x86_reg_t gr;
    size_t n = 0;

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_len == NULL || out_cap < 8 ||
        insn->operand_count != 2 || !streq_ci(insn->mnemonic, "movq")) {
        return -1;
    }
    /* Intel's order is the destination first. */
    src = &insn->operands[intel_syntax ? 1 : 0];
    dst = &insn->operands[intel_syntax ? 0 : 1];
    src_xmm = src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &sx) == 0;
    dst_xmm = dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &dx) == 0;
    src_mm = src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &sm) == 0;
    dst_mm = dst->kind == AS_OPERAND_REGISTER && parse_mmx_reg(dst->u.reg, &dm) == 0;
    /* A bare symbol is memory too; the 64-bit emitters of ModRM do not
     * take one, and it is left to what was there. */
    src_mem = src->kind == AS_OPERAND_MEMORY || (!is_64 && operand_is_bare_address(src));
    dst_mem = dst->kind == AS_OPERAND_MEMORY || (!is_64 && operand_is_bare_address(dst));
    if (!is_64 && (sx > 7u || dx > 7u)) {
        return -1;
    }

    if (src_xmm && dst_xmm) {
        out[n++] = 0xf3;
        if (sx > 7u || dx > 7u) {
            out[n++] = (unsigned char)(0x40u | (dx > 7u ? 0x04u : 0u) | (sx > 7u ? 0x01u : 0u));
        }
        out[n++] = 0x0f;
        out[n++] = 0x7e;
        out[n++] = (unsigned char)(0xc0u | ((dx & 7u) << 3) | (sx & 7u));
        *out_len = n;
        return 0;
    }
    if (src_mem && dst_xmm) {
        return is_64 ? emit_x86_64_xmm_memop(0xf3, 0x7e, dx, &src->u.mem, out, out_cap, out_len)
                     : emit_i386_prefixed_0f_rm(0xf3, 0x7e, dx, src, out, out_cap, out_len);
    }
    if (src_xmm && dst_mem) {
        return is_64 ? emit_x86_64_xmm_memop(0x66, 0xd6, sx, &dst->u.mem, out, out_cap, out_len)
                     : emit_i386_prefixed_0f_rm(0x66, 0xd6, sx, dst, out, out_cap, out_len);
    }
    if (src_mm && dst_mm) {
        out[n++] = 0x0f;
        out[n++] = 0x6f;
        out[n++] = (unsigned char)(0xc0u | ((dm & 7u) << 3) | (sm & 7u));
        *out_len = n;
        return 0;
    }
    if (src_mem && dst_mm) {
        return is_64 ? emit_x86_64_mmx_memop(0x6f, dm, &src->u.mem, out, out_cap, out_len)
                     : emit_i386_prefixed_0f_rm(0x00, 0x6f, dm, src, out, out_cap, out_len);
    }
    if (src_mm && dst_mem) {
        return is_64 ? emit_x86_64_mmx_memop(0x7f, sm, &dst->u.mem, out, out_cap, out_len)
                     : emit_i386_prefixed_0f_rm(0x00, 0x7f, sm, dst, out, out_cap, out_len);
    }
    if (is_64 && ((src_mm && dst->kind == AS_OPERAND_REGISTER) || (dst_mm && src->kind == AS_OPERAND_REGISTER))) {
        const as_operand_t *gp = src_mm ? dst : src;
        unsigned mm = src_mm ? sm : dm;

        if (parse_x86_reg(gp->u.reg, &gr) != 0 || x86_reg_width_bits(gp->u.reg) != 64) {
            return -1;
        }
        out[n++] = (unsigned char)(0x48u | ((((unsigned)gr) & 8u) ? 0x01u : 0u));
        out[n++] = 0x0f;
        out[n++] = src_mm ? 0x7e : 0x6e;
        out[n++] = (unsigned char)(0xc0u | ((mm & 7u) << 3) | (((unsigned)gr) & 7u));
        *out_len = n;
        return 0;
    }
    return -1;
}

/*
 * Put F0 before an encoded instruction unless it is already among its
 * legacy prefixes -- the bytes before the opcode that are segment
 * overrides, 66, 67, F2 or F3.
 */
/*
 * VIA's PadLock instructions and the user-mode wait instructions: an
 * opcode in the 0F map and a ModRM byte that is fixed, for PadLock, or
 * has one register in ModRM.rm.
 *
 * PadLock's take no operand.  Each but xstore is defined with a rep
 * prefix, which GNU as writes whether it is in the source or not, and
 * once.  (montmul and xstore-rng were the two bytes 0F A6 and 0F A7 in
 * 32-bit code, with neither prefix nor ModRM, and unknown in 64-bit;
 * the rest were unknown in both.)
 *
 * tpause and umwait take a 32-bit register, and may have %edx and %eax
 * written after it.  umonitor takes an address in a register, whose
 * width is the address size: 67 where that is not the mode's.
 *
 * 0 if it wrote the instruction, 1 if the mnemonic is none of these,
 * -1 if it is one and its operands are not.
 */
static int emit_x86_padlock_waitpkg(const char *mn, const as_instruction_t *insn, int intel_syntax, int is_64,
                                    unsigned char *out, size_t out_cap, size_t *out_len) {
    static const struct {
        const char *mnemonic;
        unsigned char prefix;
        unsigned char opcode2;
        unsigned char modrm;
    } padlock[] = {
        {"xstore", 0x00, 0xa7, 0xc0},     {"xstore-rng", 0x00, 0xa7, 0xc0}, {"xstorerng", 0x00, 0xa7, 0xc0},
        {"xcrypt-ecb", 0xf3, 0xa7, 0xc8}, {"xcryptecb", 0xf3, 0xa7, 0xc8},
        {"xcrypt-cbc", 0xf3, 0xa7, 0xd0}, {"xcryptcbc", 0xf3, 0xa7, 0xd0},
        {"xcrypt-ctr", 0xf3, 0xa7, 0xd8}, {"xcryptctr", 0xf3, 0xa7, 0xd8},
        {"xcrypt-cfb", 0xf3, 0xa7, 0xe0}, {"xcryptcfb", 0xf3, 0xa7, 0xe0},
        {"xcrypt-ofb", 0xf3, 0xa7, 0xe8}, {"xcryptofb", 0xf3, 0xa7, 0xe8},
        {"montmul", 0xf3, 0xa6, 0xc0},    {"xsha1", 0xf3, 0xa6, 0xc8},      {"xsha256", 0xf3, 0xa6, 0xd0},
    };
    const unsigned rep_prefixes = AS_PREFIX_REP | AS_PREFIX_REPE;
    const as_operand_t *op;
    unsigned char prefix;
    as_x86_reg_t reg;
    int width;
    int need67 = 0;
    size_t pos = 0;
    size_t i;

    if (mn == NULL || insn == NULL || out == NULL || out_len == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(padlock) / sizeof(padlock[0]); ++i) {
        if (strcmp(mn, padlock[i].mnemonic) != 0) {
            continue;
        }
        if (insn->operand_count != 0 || (insn->prefixes & ~rep_prefixes) != 0 || out_cap < 4) {
            return -1;
        }
        if (padlock[i].prefix != 0 || (insn->prefixes & rep_prefixes) != 0) {
            out[pos++] = 0xf3;
        }
        out[pos++] = 0x0f;
        out[pos++] = padlock[i].opcode2;
        out[pos++] = padlock[i].modrm;
        *out_len = pos;
        return 0;
    }

    if (strcmp(mn, "tpause") == 0) {
        prefix = 0x66;
    } else if (strcmp(mn, "umwait") == 0) {
        prefix = 0xf2;
    } else if (strcmp(mn, "umonitor") == 0) {
        prefix = 0xf3;
    } else {
        return 1;
    }
    if (insn->prefixes != 0 || insn->segment_override != NULL || out_cap < 6) {
        return -1;
    }
    if (prefix == 0xf3) {
        if (insn->operand_count != 1) {
            return -1;
        }
        op = &insn->operands[0];
    } else if (insn->operand_count == 1) {
        op = &insn->operands[0];
    } else if (insn->operand_count == 3) {
        /* The register, then %edx and %eax, which are where the time is
         * and are not encoded; Intel's order is the reverse. */
        const as_operand_t *dx = &insn->operands[1];
        const as_operand_t *ax = &insn->operands[intel_syntax ? 0 : 2];

        op = &insn->operands[intel_syntax ? 2 : 0];
        if (dx->kind != AS_OPERAND_REGISTER || ax->kind != AS_OPERAND_REGISTER ||
            x86_reg_width_bits(dx->u.reg) != 32 || x86_reg_width_bits(ax->u.reg) != 32 ||
            parse_x86_reg(dx->u.reg, &reg) != 0 || reg != AS_X86_REG_EDX ||
            parse_x86_reg(ax->u.reg, &reg) != 0 || reg != AS_X86_REG_EAX) {
            return -1;
        }
    } else {
        return -1;
    }
    if (op->kind != AS_OPERAND_REGISTER || parse_x86_reg(op->u.reg, &reg) != 0) {
        return -1;
    }
    width = x86_reg_width_bits(op->u.reg);
    if (reg >= AS_X86_REG_AH || (!is_64 && reg >= AS_X86_REG_R8)) {
        return -1;
    }
    if (prefix == 0xf3) {
        if (width == (is_64 ? 32 : 16)) {
            need67 = 1;
        } else if (width != (is_64 ? 64 : 32)) {
            return -1;
        }
    } else if (width != 32) {
        return -1;
    }
    if (need67) {
        out[pos++] = 0x67;
    }
    out[pos++] = prefix;
    if (reg >= AS_X86_REG_R8) {
        out[pos++] = 0x41;
    }
    out[pos++] = 0x0f;
    out[pos++] = 0xae;
    out[pos++] = (unsigned char)(0xf0u | ((unsigned)reg & 7u));
    *out_len = pos;
    return 0;
}

/*
 * The instructions that were AVX-512 first and were given a VEX form
 * later (AVX-VNNI, AVX-IFMA): GNU as writes the EVEX form of these
 * unless `{vex}` asks for the other, the reverse of every other
 * instruction that has both.
 */
static int x86_mnemonic_is_evex_first(const char *mn) {
    return strcmp(mn, "vpdpbusd") == 0 || strcmp(mn, "vpdpbusds") == 0 ||
           strcmp(mn, "vpdpwssd") == 0 || strcmp(mn, "vpdpwssds") == 0 ||
           strcmp(mn, "vpmadd52huq") == 0 || strcmp(mn, "vpmadd52luq") == 0;
}

/*
 * Whether the statement has a decorator -- a mask `{%k1}`, zeroing `{z}`, a
 * broadcast `{1to8}`, a rounding mode.  Only EVEX has a place for one,
 * and the VEX encoders do not look: they would write the instruction
 * without it.
 */
static int x86_stmt_has_decorator(const as_instruction_t *insn) {
    return insn->have_opmask || insn->opmask != 0 || insn->zeroing || insn->broadcast || insn->sae ||
           insn->rounding_mode >= 0;
}

/*
 * The segment override of an instruction that one of the VEX or EVEX
 * encoders wrote.  They are given the operands and not the prefix, and
 * `vmovdqa %fs:(%rbx), %ymm0` read from %ds.  The override goes before
 * the VEX prefix, which is where the encoding begins.
 */
static int x86_prepend_segment(const as_instruction_t *insn, unsigned char *code, size_t code_cap,
                               size_t *code_len) {
    const char *name;
    unsigned char seg = 0;
    size_t i;

    if (insn == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    name = insn->segment_override;
    for (i = 0; name == NULL && i < insn->operand_count; ++i) {
        if (insn->operands[i].kind == AS_OPERAND_MEMORY) {
            name = insn->operands[i].u.mem.segment_reg;
        }
    }
    if (name == NULL) {
        return 0;
    }
    switch (map_seg(name)) {
    case AS_X86_SEG_CS: seg = 0x2e; break;
    case AS_X86_SEG_DS: seg = 0x3e; break;
    case AS_X86_SEG_ES: seg = 0x26; break;
    case AS_X86_SEG_FS: seg = 0x64; break;
    case AS_X86_SEG_GS: seg = 0x65; break;
    case AS_X86_SEG_SS: seg = 0x36; break;
    default: return 0;
    }
    /* Some emitters write the override of a memory operand themselves;
     * not twice. */
    for (i = 0; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                  code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                  code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
        if (code[i] == seg) {
            return 0;
        }
    }
    if (*code_len >= code_cap) {
        return -1;
    }
    memmove(code + 1, code, *code_len);
    code[0] = seg;
    (*code_len)++;
    return 0;
}

/*
 * The instructions with a segment register that are of 16 bits: a move
 * from one to a 16-bit register, and pushw and popw of one.  The
 * emitters of those write no operand-size prefix, so `movw %ds, %si`
 * was `movl %ds, %esi`, which clears the top of %esi, and `pushw %fs`
 * pushed four bytes for two.
 */
static int x86_segment_form_is_16bit(const as_instruction_t *insn, const char *mn, char suffix, int intel_syntax) {
    as_x86_seg_t seg;

    if (strcmp(mn, "mov") == 0 && insn->operand_count == 2) {
        const as_operand_t *src = &insn->operands[intel_syntax ? 1 : 0];
        const as_operand_t *dst = &insn->operands[intel_syntax ? 0 : 1];

        return src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER &&
               parse_seg_reg_text(src->u.reg, &seg) == 0 && x86_reg_width_bits(dst->u.reg) == 16 &&
               parse_seg_reg_text(dst->u.reg, &seg) != 0;
    }
    if ((strcmp(mn, "push") == 0 || strcmp(mn, "pop") == 0) && insn->operand_count == 1 && suffix == 'w') {
        return insn->operands[0].kind == AS_OPERAND_REGISTER &&
               parse_seg_reg_text(insn->operands[0].u.reg, &seg) == 0;
    }
    return 0;
}

/*
 * The address-size prefix of a 64-bit instruction whose address is
 * through 32-bit registers, for the emitters that are handed the
 * statement and write none: x87, the moves to and from %xmm and the
 * segment registers, VEX and EVEX.  Without it `fldl (%eax)` loaded
 * through all of %rax.  It goes after a segment override, where GNU as
 * has it.  In 32-bit code the same prefix is of an address through
 * 16-bit registers; `width` is the one that wants it.
 */
static int x86_prepend_addr_size(const as_instruction_t *insn, int width, unsigned char *code, size_t code_cap,
                                 size_t *code_len) {
    size_t i;
    size_t at = 0;
    int need = 0;

    if (insn == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];

        if (op->kind != AS_OPERAND_MEMORY) {
            continue;
        }
        /* (`(%dx)` is how a port is written, and no address.) */
        if (width == 16 && op->u.mem.base_reg != NULL && op->u.mem.index_reg == NULL &&
            (streq_ci(op->u.mem.base_reg, "dx") || streq_ci(op->u.mem.base_reg, "%dx"))) {
            continue;
        }
        if ((op->u.mem.base_reg != NULL && x86_reg_width_bits(op->u.mem.base_reg) == width) ||
            (op->u.mem.index_reg != NULL && x86_reg_width_bits(op->u.mem.index_reg) == width)) {
            need = 1;
        }
    }
    if (!need) {
        return 0;
    }
    while (at < *code_len && (code[at] == 0x26 || code[at] == 0x2e || code[at] == 0x36 || code[at] == 0x3e ||
                              code[at] == 0x64 || code[at] == 0x65)) {
        ++at;
    }
    if (at < *code_len && code[at] == 0x67) {
        return 0;
    }
    if (*code_len >= code_cap) {
        return -1;
    }
    memmove(code + at + 1, code + at, *code_len - at);
    code[at] = 0x67;
    (*code_len)++;
    return 0;
}

/*
 * The instructions whose operand is 64 bits in 64-bit code with no
 * REX.W to say so: those of the stack and of control transfer.
 */
static int x86_mnemonic_is_64bit_by_default(const char *mn) {
    static const char *const names[] = {
        "push", "pop", "call", "jmp", "lcall", "ljmp", "ret", "lret", "enter", "leave",
        "pushf", "popf", "lgdt", "lidt", "sgdt", "sidt", "lldt", "ltr", "lmsw",
        /* And those where a 64-bit register is written and the
         * instruction is not of 64 bits for it, or sees to it itself. */
        "sldt", "str", "lsl", "lar", "movmskps", "movmskpd", "pmovmskb", "pextrw",
        "rdfsbase", "rdgsbase", "wrfsbase", "wrgsbase",
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(mn, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Whether a statement is one that only the encoders of as_x86_encode.c
 * can do right, and the emitters handed the statement itself must leave:
 * a statement of 32-bit code that addresses through 16-bit registers
 * (the port of in and out, `(%dx)`, is no address), and one with a
 * pseudo-prefix that asks for a displacement's width, a direction or a
 * REX, of which those emitters know nothing.  The VEX and EVEX ones
 * convert their operands as the encoders do, and so honour the two that
 * are about a displacement; `pseudo_mask` is the pseudo-prefixes that
 * count for the caller.
 */
#define X86_PSEUDO_DISP (AS_PSEUDO_DISP8 | AS_PSEUDO_DISP32)
#define X86_PSEUDO_FORMS (X86_PSEUDO_DISP | AS_PSEUDO_LOAD | AS_PSEUDO_STORE | AS_PSEUDO_REX)

/*
 * Set while a statement with an address of 16-bit registers that the
 * encoders had no instruction for is given to the emitters after all:
 * x87, SSE and the rest, which write that address by
 * emit_i386_modrm16().  See encode_x86_stmt_either().
 */
static int x86_addr16_by_emitters;
static int x86_stmt_is_the_encoders(const as_elf_cfg_t *cfg, const as_instruction_t *insn, unsigned pseudo_mask) {
    size_t i;

    if ((insn->pseudo & pseudo_mask) != 0) {
        return 1;
    }
    if (cfg->is_64 || cfg->x86_code_bits == 16u || x86_addr16_by_emitters) {
        return 0;
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];

        if (op->kind != AS_OPERAND_MEMORY) {
            continue;
        }
        if (op->u.mem.base_reg != NULL && op->u.mem.index_reg == NULL && op->u.mem.disp == NULL &&
            (streq_ci(op->u.mem.base_reg, "%dx") || streq_ci(op->u.mem.base_reg, "dx"))) {
            continue;
        }
        if ((op->u.mem.base_reg != NULL && x86_reg_width_bits(op->u.mem.base_reg) == 16) ||
            (op->u.mem.index_reg != NULL && x86_reg_width_bits(op->u.mem.index_reg) == 16)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Whether a 16-bit statement that the emitter of special forms wrote is
 * to have the operand-size prefix.  Not a move, whose forms with a
 * segment, control or debug register are judged by themselves; and not
 * the instructions on a 16-bit selector in memory -- `sldtw (%eax)`,
 * `ltrw sym` -- which are 16 bits by nature and take the prefix only
 * for a register.
 */
static int x86_special_takes_opsize(const as_instruction_t *insn, const char *mn) {
    static const char *const selector[] = {"sldt", "str", "lldt", "ltr", "lmsw", "smsw", "verr", "verw"};
    size_t i;

    /* (adcx and adox have no form of 16 bits: 66 is adcx's own, and
     * part of its opcode.) */
    if (strcmp(mn, "mov") == 0 || strcmp(mn, "adcx") == 0 || strcmp(mn, "adox") == 0) {
        return 0;
    }
    for (i = 0; i < sizeof(selector) / sizeof(selector[0]); ++i) {
        if (strcmp(mn, selector[i]) == 0) {
            return insn->operand_count == 1 && insn->operands[0].kind == AS_OPERAND_REGISTER;
        }
    }
    return 1;
}

/*
 * {vex3}: the three-byte VEX prefix where the two-byte one would do.
 * C5 [R vvvv L pp] is C4 [R 1 1 00001] [0 vvvv L pp].
 */
static int x86_vex_make_3byte(unsigned char *code, size_t code_cap, size_t *code_len) {
    size_t at = 0;
    unsigned char b;

    while (at < *code_len && (code[at] == 0x26 || code[at] == 0x2e || code[at] == 0x36 || code[at] == 0x3e ||
                              code[at] == 0x64 || code[at] == 0x65 || code[at] == 0x67)) {
        ++at;
    }
    if (at + 1 >= *code_len || code[at] != 0xc5) {
        return 0;
    }
    if (*code_len >= code_cap) {
        return -1;
    }
    b = code[at + 1];
    memmove(code + at + 2, code + at + 1, *code_len - at - 1);
    code[at] = 0xc4;
    code[at + 1] = (unsigned char)((b & 0x80u) | 0x61u);
    code[at + 2] = (unsigned char)(b & 0x7fu);
    (*code_len)++;
    return 0;
}

static int x86_stmt_has_gpr_of_width(const as_instruction_t *insn, int bits) {
    size_t i;

    for (i = 0; i < insn->operand_count; ++i) {
        if (insn->operands[i].kind == AS_OPERAND_REGISTER && x86_reg_width_bits(insn->operands[i].u.reg) == bits) {
            return 1;
        }
    }
    return 0;
}

/*
 * What goes before the bytes of an emitter that was handed the statement
 * and writes no prefixes of its own: a segment override -- written as a
 * prefix, `fs flds (%eax)`, or on the operand -- and then the address
 * size of 64-bit code.  The first was dropped: `gs movaps (%eax), %xmm0`
 * and `fs cmpxchg %ecx, (%eax)` read from %ds.
 */
static int x86_stmt_outer_prefixes(const as_elf_cfg_t *cfg, const as_instruction_t *insn, unsigned char *code,
                                   size_t code_cap, size_t *code_len) {
    /*
     * `fsave`, `fstcw` and the others that wait first: the wait is an
     * instruction of its own, and the prefixes are of the one after it.
     * `fsave %fs:(%rax)` had the override twice, once on each side of
     * the wait, and `fsave (%eax)` had the address size before it.
     */
    if (*code_len > 1 && code[0] == 0x9b) {
        size_t len = *code_len - 1;

        if (x86_prepend_segment(insn, code + 1, code_cap - 1, &len) != 0 ||
            (cfg->is_64 && x86_prepend_addr_size(insn, 32, code + 1, code_cap - 1, &len) != 0)) {
            return -1;
        }
        *code_len = len + 1;
        return 0;
    }
    if (x86_prepend_segment(insn, code, code_cap, code_len) != 0) {
        return -1;
    }
    return cfg->is_64 ? x86_prepend_addr_size(insn, 32, code, code_cap, code_len) : 0;
}

static int x86_prepend_opsize(unsigned char *code, size_t code_cap, size_t *code_len) {
    size_t at = 0;
    size_t i;

    if (code == NULL || code_len == NULL || *code_len >= code_cap) {
        return -1;
    }
    /* After a segment override and 67, before lock and rep, which is
     * GNU's order; and not a second one. */
    while (at < *code_len && (code[at] == 0x26 || code[at] == 0x2e || code[at] == 0x36 || code[at] == 0x3e ||
                              code[at] == 0x64 || code[at] == 0x65 || code[at] == 0x67)) {
        ++at;
    }
    for (i = at; i < *code_len && (code[i] == 0x66 || code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
        if (code[i] == 0x66) {
            return 0;
        }
    }
    memmove(code + at + 1, code + at, *code_len - at);
    code[at] = 0x66;
    (*code_len)++;
    return 0;
}

static int x86_prepend_lock_if_absent(unsigned char *code, size_t code_cap, size_t *code_len) {
    size_t i;

    if (code == NULL || code_len == NULL) {
        return -1;
    }
    for (i = 0; i < *code_len; ++i) {
        unsigned char c = code[i];

        if (c == 0xf0) {
            return 0;
        }
        if (c != 0x26 && c != 0x2e && c != 0x36 && c != 0x3e && c != 0x64 && c != 0x65 &&
            c != 0x66 && c != 0x67 && c != 0xf2 && c != 0xf3) {
            break;
        }
    }
    if (*code_len >= code_cap) {
        return -1;
    }
    memmove(code + 1, code, *code_len);
    code[0] = 0xf0;
    (*code_len)++;
    return 0;
}

static int emit_i386_stmt_prefixes(const as_instruction_t *insn, unsigned char *out, size_t out_cap, size_t *out_len) {
    size_t pos = 0;

    if (insn == NULL || out == NULL || out_len == NULL) {
        return -1;
    }
    if ((insn->prefixes & AS_PREFIX_LOCK) != 0) {
        if (pos >= out_cap) return -1;
        out[pos++] = 0xf0;
    }
    if ((insn->prefixes & AS_PREFIX_REPNE) != 0) {
        if (pos >= out_cap) return -1;
        out[pos++] = 0xf2;
    } else if ((insn->prefixes & (AS_PREFIX_REP | AS_PREFIX_REPE)) != 0) {
        if (pos >= out_cap) return -1;
        out[pos++] = 0xf3;
    }
    if ((insn->prefixes & AS_PREFIX_DATA16) != 0) {
        if (pos >= out_cap) return -1;
        out[pos++] = 0x66;
    }
    if ((insn->prefixes & AS_PREFIX_ADDR16) != 0) {
        if (pos >= out_cap) return -1;
        out[pos++] = 0x67;
    }
    if (emit_seg_override_byte(out, out_cap, &pos, insn->segment_override) != 0 && insn->segment_override != NULL) {
        return -1;
    }
    *out_len = pos;
    return 0;
}

static int emit_i386_prefixed_1byte_rm(const as_instruction_t *insn, unsigned char opcode, unsigned reg_field,
                                       const as_operand_t *rm_op, unsigned char *out, size_t out_cap, size_t *out_len) {
    size_t pos = 0;
    const char *seg_override = NULL;

    if (insn == NULL || rm_op == NULL || out == NULL || out_len == NULL) {
        return -1;
    }
    if (emit_i386_stmt_prefixes(insn, out, out_cap, &pos) != 0) {
        return -1;
    }
    if (rm_op->kind == AS_OPERAND_MEMORY && insn->segment_override == NULL) {
        seg_override = rm_op->u.mem.segment_reg;
    }
    if (seg_override != NULL) {
        if (emit_seg_override_byte(out, out_cap, &pos, seg_override) != 0) {
            return -1;
        }
    }
    if (pos >= out_cap) {
        return -1;
    }
    out[pos++] = opcode;
    if (emit_i386_modrm_rm_operand(reg_field, rm_op, out, out_cap, &pos) != 0) {
        return -1;
    }
    *out_len = pos;
    return 0;
}

static int emit_i386_prefixed_1byte_x87_mem(const as_instruction_t *insn, unsigned char opcode, unsigned reg_field,
                                            const as_operand_t *op, unsigned char *out, size_t out_cap, size_t *out_len) {
    if (op == NULL) {
        return -1;
    }
    if (op->kind == AS_OPERAND_REGISTER || op->kind == AS_OPERAND_COPROCESSOR) {
        return -1;
    }
    return emit_i386_prefixed_1byte_rm(insn, opcode, reg_field, op, out, out_cap, out_len);
}

static int emit_i386_x87_mem_by_size(const as_instruction_t *insn, const as_operand_t *op,
                                     int opcode32, unsigned reg32,
                                     int opcode64, unsigned reg64,
                                     int opcode80, unsigned reg80,
                                     unsigned char *out, size_t out_cap, size_t *out_len) {
    int bits;

    if (op == NULL || op->kind != AS_OPERAND_MEMORY) {
        return -1;
    }
    bits = op->u.mem.size_bits;
    if (bits == 32 && opcode32 >= 0) {
        return emit_i386_prefixed_1byte_x87_mem(insn, (unsigned char)opcode32, reg32, op, out, out_cap, out_len);
    }
    if (bits == 64 && opcode64 >= 0) {
        return emit_i386_prefixed_1byte_x87_mem(insn, (unsigned char)opcode64, reg64, op, out, out_cap, out_len);
    }
    if (bits == 80 && opcode80 >= 0) {
        return emit_i386_prefixed_1byte_x87_mem(insn, (unsigned char)opcode80, reg80, op, out, out_cap, out_len);
    }
    return -1;
}

static int emit_i386_x87_mem16_32(const as_instruction_t *insn, const as_operand_t *op,
                                  int opcode16, unsigned reg16,
                                  int opcode32, unsigned reg32,
                                  unsigned char *out, size_t out_cap, size_t *out_len) {
    int bits;

    if (op == NULL || op->kind != AS_OPERAND_MEMORY) {
        return -1;
    }
    bits = op->u.mem.size_bits;
    if (bits == 16 && opcode16 >= 0) {
        return emit_i386_prefixed_1byte_x87_mem(insn, (unsigned char)opcode16, reg16, op, out, out_cap, out_len);
    }
    if (bits == 32 && opcode32 >= 0) {
        return emit_i386_prefixed_1byte_x87_mem(insn, (unsigned char)opcode32, reg32, op, out, out_cap, out_len);
    }
    return -1;
}

static int emit_i386_x87_mem16_32_64(const as_instruction_t *insn, const as_operand_t *op,
                                     int opcode16, unsigned reg16,
                                     int opcode32, unsigned reg32,
                                     int opcode64, unsigned reg64,
                                     unsigned char *out, size_t out_cap, size_t *out_len) {
    int bits;

    if (op == NULL || op->kind != AS_OPERAND_MEMORY) {
        return -1;
    }
    bits = op->u.mem.size_bits;
    if (bits == 16 && opcode16 >= 0) {
        return emit_i386_prefixed_1byte_x87_mem(insn, (unsigned char)opcode16, reg16, op, out, out_cap, out_len);
    }
    if (bits == 32 && opcode32 >= 0) {
        return emit_i386_prefixed_1byte_x87_mem(insn, (unsigned char)opcode32, reg32, op, out, out_cap, out_len);
    }
    if (bits == 64 && opcode64 >= 0) {
        return emit_i386_prefixed_1byte_x87_mem(insn, (unsigned char)opcode64, reg64, op, out, out_cap, out_len);
    }
    return -1;
}

static int operand_st_index(const as_operand_t *op, unsigned *out);

static int lookup_i386_x87_mem_exact(const char *mnemonic, unsigned char *opcode, unsigned *reg_field) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode;
        unsigned char reg_field;
    } map[] = {
        {"fldl", 0xdd, 0u},
        {"flds", 0xd9, 0u},
        {"fldt", 0xdb, 5u},
        {"fstpl", 0xdd, 3u},
        {"fstps", 0xd9, 3u},
        {"fstpt", 0xdb, 7u},
        {"faddl", 0xdc, 0u},
        {"fadds", 0xd8, 0u},
        {"fsubl", 0xdc, 4u},
        {"fsubs", 0xd8, 4u},
        {"fsubrs", 0xd8, 5u},
        {"fsubrl", 0xdc, 5u},
        {"fmull", 0xdc, 1u},
        {"fmuls", 0xd8, 1u},
        {"fdivl", 0xdc, 6u},
        {"fdivs", 0xd8, 6u},
        {"fdivrs", 0xd8, 7u},
        {"fdivrl", 0xdc, 7u},
        {"fcoms", 0xd8, 2u},
        {"fcomps", 0xd8, 3u},
        {"fcoml", 0xdc, 2u},
        {"fcompl", 0xdc, 3u},
        {"fildl", 0xdb, 0u},
        {"filds", 0xdf, 0u},
        {"fildll", 0xdf, 5u},
        {"fistl", 0xdb, 2u},
        {"fistpl", 0xdb, 3u},
        {"fists", 0xdf, 2u},
        {"fistps", 0xdf, 3u},
        {"fistpll", 0xdf, 7u},
        {"fisttps", 0xdf, 1u},
        {"fisttpl", 0xdb, 1u},
        {"fisttpll", 0xdd, 1u},
        {"fstl", 0xdd, 2u},
        {"fbld", 0xdf, 4u},
        {"fbstp", 0xdf, 6u},
        {"fldcw", 0xd9, 5u},
        {"fnstcw", 0xd9, 7u},
        {"fsts", 0xd9, 2u},
        {"fldenv", 0xd9, 4u},
        {"fnstenv", 0xd9, 6u},
        {"frstor", 0xdd, 4u},
        {"fnsave", 0xdd, 6u},
        {"fiaddl", 0xda, 0u},
        {"fimull", 0xda, 1u},
        {"fisubl", 0xda, 4u},
        {"fisubrl", 0xda, 5u},
        {"fidivl", 0xda, 6u},
        {"fidivrl", 0xda, 7u},
        {"fiadds", 0xde, 0u},
        {"fimuls", 0xde, 1u},
        {"fisubs", 0xde, 4u},
        {"fisubrs", 0xde, 5u},
        {"fidivs", 0xde, 6u},
        {"fidivrs", 0xde, 7u},
        {"ficoml", 0xda, 2u},
        {"ficompl", 0xda, 3u},
        {"ficoms", 0xde, 2u},
        {"ficomps", 0xde, 3u},
    };
    size_t i;

    if (mnemonic == NULL || opcode == NULL || reg_field == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode = map[i].opcode;
            *reg_field = map[i].reg_field;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_x87_mem16_32_named(const char *mnemonic, unsigned char *opcode16, unsigned *reg16,
                                          unsigned char *opcode32, unsigned *reg32) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode16;
        unsigned char reg16;
        unsigned char opcode32;
        unsigned char reg32;
    } map[] = {
        {"fiadd", 0xde, 0u, 0xda, 0u},
        {"fimul", 0xde, 1u, 0xda, 1u},
        {"ficom", 0xde, 2u, 0xda, 2u},
        {"ficomp", 0xde, 3u, 0xda, 3u},
        {"fisub", 0xde, 4u, 0xda, 4u},
        {"fisubr", 0xde, 5u, 0xda, 5u},
        {"fidiv", 0xde, 6u, 0xda, 6u},
        {"fidivr", 0xde, 7u, 0xda, 7u},
        {"fist", 0xdf, 2u, 0xdb, 2u},
    };
    size_t i;

    if (mnemonic == NULL || opcode16 == NULL || reg16 == NULL || opcode32 == NULL || reg32 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode16 = map[i].opcode16;
            *reg16 = map[i].reg16;
            *opcode32 = map[i].opcode32;
            *reg32 = map[i].reg32;
            return 0;
        }
    }
    return -1;
}

static int lookup_i386_x87_mem16_32_64_named(const char *mnemonic, unsigned char *opcode16, unsigned *reg16,
                                             unsigned char *opcode32, unsigned *reg32,
                                             unsigned char *opcode64, unsigned *reg64) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode16;
        unsigned char reg16;
        unsigned char opcode32;
        unsigned char reg32;
        unsigned char opcode64;
        unsigned char reg64;
    } map[] = {
        {"fild", 0xdf, 0u, 0xdb, 0u, 0xdf, 5u},
        {"fistp", 0xdf, 3u, 0xdb, 3u, 0xdf, 7u},
        {"fisttp", 0xdf, 1u, 0xdb, 1u, 0xdd, 1u},
    };
    size_t i;

    if (mnemonic == NULL || opcode16 == NULL || reg16 == NULL || opcode32 == NULL || reg32 == NULL ||
        opcode64 == NULL || reg64 == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(mnemonic, map[i].mnemonic) == 0) {
            *opcode16 = map[i].opcode16;
            *reg16 = map[i].reg16;
            *opcode32 = map[i].opcode32;
            *reg32 = map[i].reg32;
            *opcode64 = map[i].opcode64;
            *reg64 = map[i].reg64;
            return 0;
        }
    }
    return -1;
}

static int emit_i386_x87_stack_load_exchange(const as_instruction_t *insn, const char *mnemonic, const as_operand_t *op,
                                             unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned stidx;

    if (mnemonic == NULL || insn == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(mnemonic, "fld1") == 0) {
        if (insn->operand_count != 0 || out_cap < 2) {
            return -1;
        }
        out[0] = 0xd9;
        out[1] = 0xe8;
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnemonic, "fld") == 0) {
        if (insn->operand_count != 1 || op == NULL) {
            return -1;
        }
        if (op->kind == AS_OPERAND_MEMORY) {
            return emit_i386_x87_mem_by_size(insn, op, 0xd9, 0u, 0xdd, 0u, 0xdb, 5u, out, out_cap, out_len);
        }
        if (operand_st_index(op, &stidx) != 0 || out_cap < 2) {
            return -1;
        }
        out[0] = 0xd9;
        out[1] = (unsigned char)(0xc0u + (stidx & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnemonic, "fxch") == 0) {
        if (insn->operand_count != 1 || op == NULL || operand_st_index(op, &stidx) != 0 || out_cap < 2) {
            return -1;
        }
        out[0] = 0xd9;
        out[1] = (unsigned char)(0xc8u + (stidx & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    return -1;
}

static int operand_st_index(const as_operand_t *op, unsigned *out) {
    if (op == NULL || out == NULL) {
        return -1;
    }
    if (op->kind == AS_OPERAND_COPROCESSOR) {
        return parse_st_index(op->u.coproc, out);
    }
    if (op->kind == AS_OPERAND_REGISTER) {
        return parse_st_index(op->u.reg, out);
    }
    return -1;
}

static int emit_x86_64_xmm_regop(unsigned char prefix, unsigned char opcode, unsigned dst_xmm, unsigned src_xmm,
                                 unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char rex;

    if (out == NULL || out_len == NULL || out_cap < 6) {
        return -1;
    }
    rex = (unsigned char)(0x40u | ((dst_xmm & 8u) ? 0x04u : 0u) | ((src_xmm & 8u) ? 0x01u : 0u));
    if (prefix != 0u) {
        out[0] = prefix;
        if (rex != 0x40u) {
            out[1] = rex;
            out[2] = 0x0f;
            out[3] = opcode;
            out[4] = (unsigned char)(0xc0u | ((dst_xmm & 7u) << 3) | (src_xmm & 7u));
            *out_len = 5;
        } else {
            out[1] = 0x0f;
            out[2] = opcode;
            out[3] = (unsigned char)(0xc0u | ((dst_xmm & 7u) << 3) | (src_xmm & 7u));
            *out_len = 4;
        }
    } else {
        if (rex != 0x40u) {
            out[0] = rex;
            out[1] = 0x0f;
            out[2] = opcode;
            out[3] = (unsigned char)(0xc0u | ((dst_xmm & 7u) << 3) | (src_xmm & 7u));
            *out_len = 4;
        } else {
            out[0] = 0x0f;
            out[1] = opcode;
            out[2] = (unsigned char)(0xc0u | ((dst_xmm & 7u) << 3) | (src_xmm & 7u));
            *out_len = 3;
        }
    }
    return 0;
}

static int emit_x86_64_mmx_regop(unsigned char opcode, unsigned dst_mmx, unsigned src_mmx,
                                 unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char modrm;
    size_t pos = 0;

    if (out == NULL || out_len == NULL || out_cap < 4) {
        return -1;
    }
    out[pos++] = 0x0f;
    out[pos++] = opcode;
    modrm = (unsigned char)(0xc0u | ((dst_mmx & 7u) << 3) | (src_mmx & 7u));
    out[pos++] = modrm;
    *out_len = pos;
    return 0;
}

static int emit_x86_64_mmx_memop(unsigned char opcode, unsigned dst_mmx, const as_mem_operand_t *mem,
                                 unsigned char *out, size_t out_cap, size_t *out_len) {
    return emit_x86_64_regfield_memop(0x00, opcode, dst_mmx, 0, mem, out, out_cap, out_len);
}

static int emit_x86_64_mmx_srcdst(unsigned char opcode, const as_operand_t *src,
                                  const as_operand_t *dst, unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xd;
    unsigned xs;

    if (src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER || parse_mmx_reg(dst->u.reg, &xd) != 0) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER) {
        if (parse_mmx_reg(src->u.reg, &xs) != 0) {
            return -1;
        }
        return emit_x86_64_mmx_regop(opcode, xd, xs, out, out_cap, out_len);
    }
    if (src->kind == AS_OPERAND_MEMORY) {
        return emit_x86_64_mmx_memop(opcode, xd, &src->u.mem, out, out_cap, out_len);
    }
    return -1;
}

static int emit_x86_64_0f_sysreg_mov(unsigned char opcode2, unsigned sysreg, unsigned gpreg,
                                     unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char rex;

    if (out == NULL || out_len == NULL || out_cap < 4 || sysreg > 15u || gpreg > 15u) {
        return -1;
    }
    rex = (unsigned char)(0x40u | ((sysreg & 8u) ? 0x04u : 0u) | ((gpreg & 8u) ? 0x01u : 0u));
    if (rex != 0x40u) {
        out[0] = rex;
        out[1] = 0x0f;
        out[2] = opcode2;
        out[3] = (unsigned char)(0xc0u | ((sysreg & 7u) << 3) | (gpreg & 7u));
        *out_len = 4;
    } else {
        out[0] = 0x0f;
        out[1] = opcode2;
        out[2] = (unsigned char)(0xc0u | ((sysreg & 7u) << 3) | (gpreg & 7u));
        *out_len = 3;
    }
    return 0;
}

static int emit_x86_64_xmm_srcdst(unsigned char prefix, unsigned char opcode, const as_operand_t *src,
                                  const as_operand_t *dst, unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xs;
    unsigned xd;

    if (src == NULL || dst == NULL) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER &&
        parse_xmm_reg(src->u.reg, &xs) == 0 && parse_xmm_reg(dst->u.reg, &xd) == 0) {
        return emit_x86_64_xmm_regop(prefix, opcode, xd, xs, out, out_cap, out_len);
    }
    if (src->kind == AS_OPERAND_MEMORY && dst->kind == AS_OPERAND_REGISTER &&
        parse_xmm_reg(dst->u.reg, &xd) == 0) {
        return emit_x86_64_xmm_memop(prefix, opcode, xd, &src->u.mem, out, out_cap, out_len);
    }
    return -1;
}

static int emit_x86_64_xmm_move_rm(unsigned char prefix, unsigned char reg_opcode,
                                   unsigned char load_opcode, unsigned char store_opcode,
                                   const as_operand_t *src, const as_operand_t *dst,
                                   unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xs;
    unsigned xd;

    if (src == NULL || dst == NULL) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER &&
        parse_xmm_reg(src->u.reg, &xs) == 0 && parse_xmm_reg(dst->u.reg, &xd) == 0) {
        return emit_x86_64_xmm_regop(prefix, reg_opcode, xd, xs, out, out_cap, out_len);
    }
    if (src->kind == AS_OPERAND_MEMORY && dst->kind == AS_OPERAND_REGISTER &&
        parse_xmm_reg(dst->u.reg, &xd) == 0) {
        return emit_x86_64_xmm_memop(prefix, load_opcode, xd, &src->u.mem, out, out_cap, out_len);
    }
    if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_MEMORY &&
        parse_xmm_reg(src->u.reg, &xd) == 0) {
        return emit_x86_64_xmm_memop(prefix, store_opcode, xd, &dst->u.mem, out, out_cap, out_len);
    }
    return -1;
}

static int emit_x86_64_xmm_binary_op(const as_instruction_t *insn, unsigned char prefix,
                                     unsigned char opcode, const as_operand_t *src,
                                     const as_operand_t *dst, unsigned char *out,
                                     size_t out_cap, size_t *out_len) {
    if (insn == NULL || insn->operand_count != 2) {
        return -1;
    }
    return emit_x86_64_xmm_srcdst(prefix, opcode, src, dst, out, out_cap, out_len);
}

static int emit_x86_64_gp_to_xmm_cvtsi(unsigned char prefix, const as_operand_t *src,
                                       const as_operand_t *dst, unsigned char *out,
                                       size_t out_cap, size_t *out_len) {
    as_x86_reg_t gr;
    unsigned xr;
    unsigned char rex;
    int src_bits;
    size_t pos = 0;

    if (src == NULL || dst == NULL || out == NULL || out_len == NULL || out_cap < 5 ||
        src->kind != AS_OPERAND_REGISTER || dst->kind != AS_OPERAND_REGISTER ||
        parse_x86_reg(src->u.reg, &gr) != 0 || parse_xmm_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    /*
     * REX.W says the integer is 64 bits wide, and belongs only where the
     * register is.  It was set always: `cvtsi2sd %eax, %xmm0`, which is
     * how a compiler turns an int into a double, converted all of %rax,
     * whose upper half is whatever was left there.
     */
    src_bits = x86_reg_width_bits(src->u.reg);
    if (src_bits != 32 && src_bits != 64) {
        return -1;
    }
    rex = (unsigned char)(0x40u | (src_bits == 64 ? 0x08u : 0u) | ((xr & 8u) ? 0x04u : 0u) |
                          ((((unsigned)gr) & 8u) ? 0x01u : 0u));
    out[pos++] = prefix;
    if (rex != 0x40u) {
        out[pos++] = rex;
    }
    out[pos++] = 0x0f;
    out[pos++] = 0x2a;
    out[pos++] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (((unsigned)gr) & 7u));
    *out_len = pos;
    return 0;
}

static int emit_x86_64_xmm_to_gp_cvtt(unsigned char prefix, const as_operand_t *src,
                                      const as_operand_t *dst, unsigned char *out,
                                      size_t out_cap, size_t *out_len) {
    as_x86_reg_t gr;
    unsigned src_xmm;
    int dst_bits;
    unsigned char rex;

    if (src == NULL || dst == NULL ||
        dst->kind != AS_OPERAND_REGISTER || parse_x86_reg(dst->u.reg, &gr) != 0) {
        return -1;
    }
    dst_bits = x86_reg_width_bits(dst->u.reg);
    if (dst_bits != 32 && dst_bits != 64) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &src_xmm) == 0) {
        if (out == NULL || out_len == NULL || out_cap < 5) {
            return -1;
        }
        rex = (unsigned char)(0x40u | (dst_bits == 64 ? 0x08u : 0u) | ((((unsigned)gr) & 8u) ? 0x04u : 0u) |
                              ((src_xmm & 8u) ? 0x01u : 0u));
        out[0] = prefix;
        if (rex != 0x40u) {
            out[1] = rex;
            out[2] = 0x0f;
            out[3] = 0x2c;
            out[4] = (unsigned char)(0xc0u | ((((unsigned)gr) & 7u) << 3) | (src_xmm & 7u));
            *out_len = 5;
        } else {
            out[1] = 0x0f;
            out[2] = 0x2c;
            out[3] = (unsigned char)(0xc0u | ((((unsigned)gr) & 7u) << 3) | (src_xmm & 7u));
            *out_len = 4;
        }
        return 0;
    }
    if (src->kind == AS_OPERAND_MEMORY) {
        return emit_x86_64_regfield_memop(prefix, 0x2c, (unsigned)gr, dst_bits == 64, &src->u.mem, out, out_cap,
                                          out_len);
    }
    return -1;
}

static int emit_i386_xmm_reg_srcdst_rm(unsigned char prefix, unsigned char opcode2,
                                       const as_operand_t *src, const as_operand_t *dst,
                                       unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xr;
    unsigned xm;

    if (src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
        parse_xmm_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
        return -1;
    }
    return emit_i386_legacy_simd_rm(prefix, opcode2, xr, src, out, out_cap, out_len);
}

static int emit_i386_movmsk(unsigned char prefix, const as_operand_t *src, const as_operand_t *dst,
                            unsigned char *out, size_t out_cap, size_t *out_len) {
    as_x86_reg_t gr;
    unsigned xr;
    size_t pos = 0;

    if (src == NULL || dst == NULL || out == NULL || out_len == NULL || out_cap < 4 ||
        dst->kind != AS_OPERAND_REGISTER || src->kind != AS_OPERAND_REGISTER ||
        parse_x86_reg(dst->u.reg, &gr) != 0 || parse_xmm_reg(src->u.reg, &xr) != 0) {
        return -1;
    }
    if (prefix != 0) {
        out[pos++] = prefix;
    }
    out[pos++] = 0x0f;
    out[pos++] = 0x50;
    out[pos++] = (unsigned char)(0xc0u | (((unsigned)gr & 7u) << 3) | (xr & 7u));
    *out_len = pos;
    return 0;
}

static int emit_i386_movmask_family(const char *mnemonic, const as_operand_t *src, const as_operand_t *dst,
                                    unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char prefix;
    as_x86_reg_t gr;

    if (mnemonic == NULL || src == NULL || dst == NULL) {
        return -1;
    }
    if (lookup_i386_movmsk_prefix(mnemonic, &prefix) == 0) {
        return emit_i386_movmsk(prefix, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnemonic, "pmovmskb") != 0 || out == NULL || out_len == NULL || out_cap < 4 ||
        dst->kind != AS_OPERAND_REGISTER || src->kind != AS_OPERAND_REGISTER ||
        parse_x86_reg(dst->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
        return -1;
    }
    {
        unsigned mm;
        unsigned xr;

        if (parse_mmx_reg(src->u.reg, &mm) == 0) {
            prefix = 0x00;
        } else if (parse_xmm_reg(src->u.reg, &xr) == 0) {
            prefix = 0x66;
        } else {
            return -1;
        }
    }
    return emit_i386_prefixed_0f_rm(prefix, 0xd7, (unsigned)gr & 7u, src, out, out_cap, out_len);
}

static int emit_i386_comisd_family(const char *mnemonic, const as_operand_t *src, const as_operand_t *dst,
                                   unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char opcode2;
    unsigned xr;
    unsigned xm;

    if (mnemonic == NULL || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
        parse_xmm_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
        return -1;
    }
    if (lookup_i386_comisd_opcode(mnemonic, &opcode2) != 0) {
        return -1;
    }
    return emit_i386_prefixed_0f_rm(0x66, opcode2, xr, src, out, out_cap, out_len);
}

static int emit_i386_mmx_xmm_bridge(unsigned char prefix, const as_operand_t *src,
                                    const as_operand_t *dst, int src_is_mmx,
                                    unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xr;

    if (src == NULL || dst == NULL || src->kind != AS_OPERAND_REGISTER || dst->kind != AS_OPERAND_REGISTER) {
        return -1;
    }
    if (src_is_mmx) {
        if (parse_mmx_reg(src->u.reg, &xr) != 0 || parse_xmm_reg(dst->u.reg, &xr) == 0) {
            /* fall through to separate checked path below */
        }
    }
    if (src_is_mmx) {
        unsigned mm;
        unsigned xmm;

        if (parse_mmx_reg(src->u.reg, &mm) != 0 || parse_xmm_reg(dst->u.reg, &xmm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, 0xd6, xmm, src, out, out_cap, out_len);
    } else {
        unsigned xmm;
        unsigned mm;

        if (parse_xmm_reg(src->u.reg, &xmm) != 0 || parse_mmx_reg(dst->u.reg, &mm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, 0xd6, mm, src, out, out_cap, out_len);
    }
}

static int emit_i386_maskmov(unsigned char prefix, int use_xmm, const as_operand_t *src,
                             const as_operand_t *mask, unsigned char *out,
                             size_t out_cap, size_t *out_len) {
    unsigned xr;
    unsigned xm;

    if (src == NULL || mask == NULL || src->kind != AS_OPERAND_REGISTER || mask->kind != AS_OPERAND_REGISTER) {
        return -1;
    }
    if (use_xmm) {
        if (parse_xmm_reg(src->u.reg, &xr) != 0 || parse_xmm_reg(mask->u.reg, &xm) != 0) {
            return -1;
        }
    } else {
        if (parse_mmx_reg(src->u.reg, &xr) != 0 || parse_mmx_reg(mask->u.reg, &xm) != 0) {
            return -1;
        }
    }
    return emit_i386_prefixed_0f_rm(prefix, 0xf7, xr, mask, out, out_cap, out_len);
}

static int emit_i386_movnt_store(unsigned char prefix, unsigned char opcode2, int use_xmm, int intel_syntax,
                                 const as_instruction_t *insn, unsigned char *out,
                                 size_t out_cap, size_t *out_len) {
    const as_operand_t *src_op;
    const as_operand_t *dst_op;
    unsigned xr;

    if (insn == NULL || out == NULL || out_len == NULL || insn->operand_count != 2) {
        return -1;
    }
    if (intel_syntax) {
        dst_op = &insn->operands[0];
        src_op = &insn->operands[1];
    } else {
        src_op = &insn->operands[0];
        dst_op = &insn->operands[1];
    }
    if (dst_op->kind == AS_OPERAND_REGISTER || src_op->kind != AS_OPERAND_REGISTER) {
        return -1;
    }
    if (use_xmm) {
        if (parse_xmm_reg(src_op->u.reg, &xr) != 0) {
            return -1;
        }
    } else {
        if (parse_mmx_reg(src_op->u.reg, &xr) != 0) {
            return -1;
        }
    }
    return emit_i386_prefixed_0f_rm(prefix, opcode2, xr, dst_op, out, out_cap, out_len);
}

/*
 * extrq and insertq, the SSE4a instructions on a field of bits.  In
 * Intel's order of operands:
 *
 *   extrq   xmm, xmm                 66 0F 79 /r
 *   extrq   xmm, length, index       66 0F 78 /0 ib ib
 *   insertq xmm, xmm                 F2 0F 79 /r
 *   insertq xmm, xmm, length, index  F2 0F 78 /r ib ib
 *
 * AT&T writes the same operands last to first, so the index is written
 * first there; the length is the first of the two bytes in either.  The
 * operands are registers: there is no form with memory.
 */
static int emit_x86_extrq_insertq(const char *mnemonic, const as_instruction_t *insn, int intel_syntax, int is_64,
                                  unsigned char *out, size_t out_cap, size_t *out_len) {
    const as_operand_t *op[4];
    unsigned char prefix;
    unsigned x[2] = {0u, 0u};
    long long imm[2] = {0, 0};
    unsigned reg;
    unsigned rm;
    size_t n;
    size_t nreg;
    size_t i;
    size_t pos = 0;

    if (mnemonic == NULL || insn == NULL || out == NULL || out_len == NULL ||
        lookup_i386_extrq_insertq_prefix(mnemonic, &prefix) != 0) {
        return -1;
    }
    n = insn->operand_count;
    if (n == 2) {
        nreg = 2;
    } else if (n == (strcmp(mnemonic, "insertq") == 0 ? 4u : 3u)) {
        nreg = n - 2;
    } else {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        op[i] = &insn->operands[intel_syntax ? i : n - 1 - i];
    }
    for (i = 0; i < nreg; ++i) {
        if (op[i]->kind != AS_OPERAND_REGISTER || parse_xmm_reg(op[i]->u.reg, &x[i]) != 0 ||
            (!is_64 && x[i] > 7u)) {
            return -1;
        }
    }
    for (i = nreg; i < n; ++i) {
        long long v;

        if ((op[i]->kind != AS_OPERAND_IMMEDIATE && op[i]->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(op[i]->u.expr, &v) != 0 || v < 0 || v > 255) {
            return -1;
        }
        imm[i - nreg] = v;
    }
    if (nreg == 2) {
        reg = x[0];
        rm = x[1];
    } else {
        reg = 0u;
        rm = x[0];
    }
    if (out_cap < 7) {
        return -1;
    }
    out[pos++] = prefix;
    if (reg > 7u || rm > 7u) {
        out[pos++] = (unsigned char)(0x40u | (reg > 7u ? 0x04u : 0u) | (rm > 7u ? 0x01u : 0u));
    }
    out[pos++] = 0x0f;
    out[pos++] = (n == 2) ? 0x79 : 0x78;
    out[pos++] = (unsigned char)(0xc0u | ((reg & 7u) << 3) | (rm & 7u));
    if (n != 2) {
        out[pos++] = (unsigned char)imm[0];
        out[pos++] = (unsigned char)imm[1];
    }
    *out_len = pos;
    return 0;
}

static int emit_i386_xmm_shiftdq_imm8_family(const char *mnemonic, const as_instruction_t *insn, int intel_syntax,
                                             unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char reg_field;
    const as_operand_t *imm_op;
    const as_operand_t *dst_op;
    unsigned xr;
    long long immv;

    if (mnemonic == NULL || insn == NULL || lookup_i386_xmm_shiftdq_imm8(mnemonic, &reg_field) != 0 ||
        insn->operand_count != 2) {
        return -1;
    }
    if (intel_syntax) {
        dst_op = &insn->operands[0];
        imm_op = &insn->operands[1];
    } else {
        imm_op = &insn->operands[0];
        dst_op = &insn->operands[1];
    }
    if (dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0 ||
        (imm_op->kind != AS_OPERAND_IMMEDIATE && imm_op->kind != AS_OPERAND_LABEL_REF) ||
        eval_expr_const(imm_op->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
        return -1;
    }
    return emit_i386_legacy_simd_rm_imm8(0x66, 0x73, reg_field, dst_op, (unsigned char)immv, out, out_cap, out_len);
}

/*
 * The byte of a shuffle is a pattern of bits, and GNU as takes it written
 * as a negative number: `pshufd $-1` is `pshufd $0xff`.  Of the SSE
 * instructions with an immediate byte it does so for the shuffles alone.
 */
static int x86_shuffle_imm8_fits(long long v) {
    return v >= -128 && v <= 255;
}

static int x86_is_shuffle_mnemonic(const char *mn) {
    return strcmp(mn, "pshufd") == 0 || strcmp(mn, "shufps") == 0 || strcmp(mn, "shufpd") == 0;
}

static int emit_i386_xmm_shuffle_tail_family(const char *mnemonic, const as_instruction_t *insn, int intel_syntax,
                                             unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char prefix;
    unsigned char opcode2;
    const as_operand_t *imm_op;
    const as_operand_t *src_op;
    const as_operand_t *dst_op;
    unsigned xr;
    unsigned xm;
    long long immv;

    if (mnemonic == NULL || insn == NULL || lookup_i386_xmm_shuffle_tail(mnemonic, &prefix, &opcode2) != 0) {
        return -1;
    }
    if (select_x86_dstsrc_tail_operand(insn, intel_syntax, &dst_op, &src_op, &imm_op) != 0 ||
        (imm_op->kind != AS_OPERAND_IMMEDIATE && imm_op->kind != AS_OPERAND_LABEL_REF) ||
        eval_expr_const(imm_op->u.expr, &immv) != 0 || !x86_shuffle_imm8_fits(immv) ||
        dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0) {
        return -1;
    }
    if (src_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src_op->u.reg, &xm) != 0) {
        return -1;
    }
    return emit_i386_legacy_simd_rm_imm8(prefix, opcode2, xr, src_op, (unsigned char)immv, out, out_cap, out_len);
}

static int emit_i386_cachehint(unsigned char opcode2, unsigned char regf,
                               const as_operand_t *op, unsigned char *out,
                               size_t out_cap, size_t *out_len) {
    if (op == NULL || op->kind == AS_OPERAND_REGISTER || op->kind == AS_OPERAND_COPROCESSOR) {
        return -1;
    }
    return emit_i386_prefixed_0f_rm(0x00, opcode2, regf, op, out, out_cap, out_len);
}

static int emit_i386_cachehint_family(const char *mnemonic, const as_operand_t *op,
                                      unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char opcode2;
    unsigned char regf;

    if (mnemonic == NULL || lookup_i386_cachehint_regf(mnemonic, &opcode2, &regf) != 0) {
        return -1;
    }
    return emit_i386_cachehint(opcode2, regf, op, out, out_cap, out_len);
}

static int emit_x86_seg_pushpop(const char *mnemonic, const as_operand_t *op, int allow_legacy,
                                unsigned char *out, size_t out_cap, size_t *out_len) {
    as_x86_seg_t seg;

    if (mnemonic == NULL || op == NULL || out == NULL || op->kind != AS_OPERAND_REGISTER) {
        return -1;
    }
    if (parse_seg_reg_text(op->u.reg, &seg) != 0) {
        return -1;
    }
    if (strcmp(mnemonic, "push") == 0) {
        switch (seg) {
        case AS_X86_SEG_ES:
            if (!allow_legacy || out_cap < 1) return -1;
            out[0] = 0x06;
            if (out_len != NULL) *out_len = 1;
            return 0;
        case AS_X86_SEG_CS:
            if (!allow_legacy || out_cap < 1) return -1;
            out[0] = 0x0e;
            if (out_len != NULL) *out_len = 1;
            return 0;
        case AS_X86_SEG_SS:
            if (!allow_legacy || out_cap < 1) return -1;
            out[0] = 0x16;
            if (out_len != NULL) *out_len = 1;
            return 0;
        case AS_X86_SEG_DS:
            if (!allow_legacy || out_cap < 1) return -1;
            out[0] = 0x1e;
            if (out_len != NULL) *out_len = 1;
            return 0;
        case AS_X86_SEG_FS:
            if (out_cap < 2) return -1;
            out[0] = 0x0f;
            out[1] = 0xa0;
            if (out_len != NULL) *out_len = 2;
            return 0;
        case AS_X86_SEG_GS:
            if (out_cap < 2) return -1;
            out[0] = 0x0f;
            out[1] = 0xa8;
            if (out_len != NULL) *out_len = 2;
            return 0;
        default:
            return -1;
        }
    }
    if (strcmp(mnemonic, "pop") == 0) {
        switch (seg) {
        case AS_X86_SEG_ES:
            if (!allow_legacy || out_cap < 1) return -1;
            out[0] = 0x07;
            if (out_len != NULL) *out_len = 1;
            return 0;
        case AS_X86_SEG_SS:
            if (!allow_legacy || out_cap < 1) return -1;
            out[0] = 0x17;
            if (out_len != NULL) *out_len = 1;
            return 0;
        case AS_X86_SEG_DS:
            if (!allow_legacy || out_cap < 1) return -1;
            out[0] = 0x1f;
            if (out_len != NULL) *out_len = 1;
            return 0;
        case AS_X86_SEG_FS:
            if (out_cap < 2) return -1;
            out[0] = 0x0f;
            out[1] = 0xa1;
            if (out_len != NULL) *out_len = 2;
            return 0;
        case AS_X86_SEG_GS:
            if (out_cap < 2) return -1;
            out[0] = 0x0f;
            out[1] = 0xa9;
            if (out_len != NULL) *out_len = 2;
            return 0;
        default:
            return -1;
        }
    }
    return -1;
}

static int emit_i386_bnd_binary(unsigned char prefix, unsigned char opcode2,
                                const as_operand_t *src, const as_operand_t *dst,
                                unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xr;

    if (src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
        parse_bnd_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    return emit_i386_prefixed_0f_rm(prefix, opcode2, xr, src, out, out_cap, out_len);
}

static int emit_i386_xmm_from_mmx_or_mem(unsigned char prefix, unsigned char opcode2,
                                         const as_operand_t *src, const as_operand_t *dst,
                                         unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xr;
    unsigned xm;

    if (src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
        parse_xmm_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xm) != 0) {
        return -1;
    }
    return emit_i386_prefixed_0f_rm(prefix, opcode2, xr, src, out, out_cap, out_len);
}

static int emit_i386_mmx_from_xmm_or_mem(unsigned char prefix, unsigned char opcode2,
                                         const as_operand_t *src, const as_operand_t *dst,
                                         unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xr;
    unsigned xm;

    if (src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
        parse_mmx_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
        return -1;
    }
    return emit_i386_prefixed_0f_rm(prefix, opcode2, xr, src, out, out_cap, out_len);
}

static int emit_i386_special(const as_instruction_t *insn, int intel_syntax,
                             unsigned char *out, size_t out_cap, size_t *out_len) {
    const as_operand_t *a;
    const as_operand_t *b;
    const as_operand_t *src;
    const as_operand_t *dst;
    char mnbuf[32];
    as_x86_reg_t gr;
    unsigned xr;
    unsigned xm;
    unsigned stidx;
    unsigned stsrc;
    unsigned stdst;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (insn == NULL || out == NULL || out_cap < 8) {
        return -1;
    }
    if (normalize_x86_mnemonic(insn->mnemonic, mnbuf, sizeof(mnbuf), NULL) != 0) {
        return -1;
    }
    a = insn->operand_count > 0 ? &insn->operands[0] : NULL;
    b = insn->operand_count > 1 ? &insn->operands[1] : NULL;
    src = intel_syntax ? b : a;
    dst = intel_syntax ? a : b;

    if (strcmp(mnbuf, "mov") == 0 && insn->operand_count == 2 && src != NULL && dst != NULL) {
        unsigned sr;
        unsigned gr;
        as_x86_seg_t seg;
        unsigned seg_field;

        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER) {
            if (parse_seg_reg_text(src->u.reg, &seg) == 0 && seg_reg_field(seg, &seg_field) == 0 &&
                parse_i386_gp_reg(dst->u.reg, &gr) == 0) {
                return emit_i386_prefixed_1byte_rm(insn, 0x8c, seg_field, dst, out, out_cap, out_len);
            }
            if (parse_i386_gp_reg(src->u.reg, &gr) == 0 && parse_seg_reg_text(dst->u.reg, &seg) == 0 &&
                seg_reg_field(seg, &seg_field) == 0) {
                return emit_i386_prefixed_1byte_rm(insn, 0x8e, seg_field, src, out, out_cap, out_len);
            }
            if ((parse_x86_sysreg(src->u.reg, "cr", 7u, &sr) == 0 || parse_x86_sysreg(src->u.reg, "db", 7u, &sr) == 0 ||
                 parse_x86_sysreg(src->u.reg, "dr", 7u, &sr) == 0 || parse_x86_sysreg(src->u.reg, "tr", 7u, &sr) == 0) &&
                parse_i386_gp_reg(dst->u.reg, &gr) == 0) {
                if (parse_x86_sysreg(src->u.reg, "cr", 7u, &sr) == 0) {
                    return emit_i386_0f_sysreg_mov(0x20, sr, gr, out, out_cap, out_len);
                }
                if (parse_x86_sysreg(src->u.reg, "db", 7u, &sr) == 0 || parse_x86_sysreg(src->u.reg, "dr", 7u, &sr) == 0) {
                    return emit_i386_0f_sysreg_mov(0x21, sr, gr, out, out_cap, out_len);
                }
                if (parse_x86_sysreg(src->u.reg, "tr", 7u, &sr) == 0) {
                    return emit_i386_0f_sysreg_mov(0x24, sr, gr, out, out_cap, out_len);
                }
            }
            if (parse_i386_gp_reg(src->u.reg, &gr) == 0 &&
                (parse_x86_sysreg(dst->u.reg, "cr", 7u, &sr) == 0 || parse_x86_sysreg(dst->u.reg, "db", 7u, &sr) == 0 ||
                 parse_x86_sysreg(dst->u.reg, "dr", 7u, &sr) == 0 || parse_x86_sysreg(dst->u.reg, "tr", 7u, &sr) == 0)) {
                if (parse_x86_sysreg(dst->u.reg, "cr", 7u, &sr) == 0) {
                    return emit_i386_0f_sysreg_mov(0x22, sr, gr, out, out_cap, out_len);
                }
                if (parse_x86_sysreg(dst->u.reg, "db", 7u, &sr) == 0 || parse_x86_sysreg(dst->u.reg, "dr", 7u, &sr) == 0) {
                    return emit_i386_0f_sysreg_mov(0x23, sr, gr, out, out_cap, out_len);
                }
                if (parse_x86_sysreg(dst->u.reg, "tr", 7u, &sr) == 0) {
                    return emit_i386_0f_sysreg_mov(0x26, sr, gr, out, out_cap, out_len);
                }
            }
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_seg_reg_text(src->u.reg, &seg) == 0 &&
            seg_reg_field(seg, &seg_field) == 0 &&
            (dst->kind == AS_OPERAND_REGISTER || dst->kind == AS_OPERAND_MEMORY ||
             dst->kind == AS_OPERAND_IMMEDIATE || dst->kind == AS_OPERAND_LABEL_REF)) {
            return emit_i386_prefixed_1byte_rm(insn, 0x8c, seg_field, dst, out, out_cap, out_len);
        }
        if ((src->kind == AS_OPERAND_REGISTER || src->kind == AS_OPERAND_MEMORY ||
             src->kind == AS_OPERAND_IMMEDIATE || src->kind == AS_OPERAND_LABEL_REF) &&
            dst->kind == AS_OPERAND_REGISTER && parse_seg_reg_text(dst->u.reg, &seg) == 0 &&
            seg_reg_field(seg, &seg_field) == 0) {
            return emit_i386_prefixed_1byte_rm(insn, 0x8e, seg_field, src, out, out_cap, out_len);
        }
    }
    if ((strcmp(mnbuf, "push") == 0 || strcmp(mnbuf, "pop") == 0) && insn->operand_count == 1 && a != NULL &&
        a->kind == AS_OPERAND_REGISTER) {
        return emit_x86_seg_pushpop(mnbuf, a, 1, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "in") == 0 && insn->operand_count == 2 && src != NULL && dst != NULL) {
        const char *dstreg = NULL;
        size_t pos = 0;
        if (src->kind == AS_OPERAND_MEMORY && src->u.mem.base_reg != NULL && src->u.mem.index_reg == NULL &&
            src->u.mem.segment_reg == NULL && src->u.mem.disp == NULL &&
            (streq_ci(src->u.mem.base_reg, "dx") || streq_ci(src->u.mem.base_reg, "edx")) &&
            dst->kind == AS_OPERAND_REGISTER) {
            dstreg = dst->u.reg;
            if ((insn->prefixes & AS_PREFIX_DATA16) != 0) {
                out[pos++] = 0x66;
            }
            if (dstreg != NULL && (streq_ci(dstreg, "%al") || streq_ci(dstreg, "al"))) {
                out[pos++] = 0xec;
                if (out_len != NULL) *out_len = pos;
                return 0;
            }
            if (dstreg != NULL &&
                (streq_ci(dstreg, "%ax") || streq_ci(dstreg, "ax") ||
                 streq_ci(dstreg, "%eax") || streq_ci(dstreg, "eax"))) {
                out[pos++] = 0xed;
                if (out_len != NULL) *out_len = pos;
                return 0;
            }
        }
    }
    if (strcmp(mnbuf, "out") == 0 && insn->operand_count == 2 && src != NULL && dst != NULL) {
        const char *srcreg = NULL;
        size_t pos = 0;
        if (dst->kind == AS_OPERAND_MEMORY && dst->u.mem.base_reg != NULL && dst->u.mem.index_reg == NULL &&
            dst->u.mem.segment_reg == NULL && dst->u.mem.disp == NULL &&
            (streq_ci(dst->u.mem.base_reg, "dx") || streq_ci(dst->u.mem.base_reg, "edx")) &&
            src->kind == AS_OPERAND_REGISTER) {
            srcreg = src->u.reg;
            if ((insn->prefixes & AS_PREFIX_DATA16) != 0) {
                out[pos++] = 0x66;
            }
            if (srcreg != NULL && (streq_ci(srcreg, "%al") || streq_ci(srcreg, "al"))) {
                out[pos++] = 0xee;
                if (out_len != NULL) *out_len = pos;
                return 0;
            }
            if (srcreg != NULL &&
                (streq_ci(srcreg, "%ax") || streq_ci(srcreg, "ax") ||
                 streq_ci(srcreg, "%eax") || streq_ci(srcreg, "eax"))) {
                out[pos++] = 0xef;
                if (out_len != NULL) *out_len = pos;
                return 0;
            }
        }
    }
    if (strcmp(mnbuf, "fld1") == 0 || strcmp(mnbuf, "fld") == 0 || strcmp(mnbuf, "fxch") == 0) {
        return emit_i386_x87_stack_load_exchange(insn, mnbuf, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fnop") == 0 || strcmp(mnbuf, "fchs") == 0 || strcmp(mnbuf, "f2xm1") == 0 || strcmp(mnbuf, "fprem") == 0) {
        unsigned char op1;
        unsigned char op2;

        if (insn->operand_count != 0) {
            return -1;
        }
        if (lookup_i386_x87_nooperand_opcode(mnbuf, &op1, &op2) != 0) {
            return -1;
        }
        out[0] = op1;
        out[1] = op2;
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "fcmovb") == 0 || strcmp(mnbuf, "fcmove") == 0 || strcmp(mnbuf, "fcmovbe") == 0 || strcmp(mnbuf, "fcmovu") == 0 ||
        strcmp(mnbuf, "fcmovnb") == 0 || strcmp(mnbuf, "fcmovne") == 0 || strcmp(mnbuf, "fcmovnbe") == 0 || strcmp(mnbuf, "fcmovnu") == 0 ||
        strcmp(mnbuf, "fucomi") == 0 || strcmp(mnbuf, "fcomi") == 0) {
        unsigned char op1;
        unsigned char base;
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            operand_st_index(src, &stsrc) != 0 || operand_st_index(dst, &stdst) != 0 ||
            stdst != 0) {
            return -1;
        }
        if (lookup_i386_x87_fcmov_opcode(mnbuf, &op1, &base) != 0) {
            return -1;
        }
        out[0] = op1;
        out[1] = (unsigned char)(base + (stsrc & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "fadd") == 0 || strcmp(mnbuf, "fmul") == 0 || strcmp(mnbuf, "fcom") == 0 || strcmp(mnbuf, "fcomp") == 0 ||
        strcmp(mnbuf, "fsub") == 0 || strcmp(mnbuf, "fsubr") == 0 || strcmp(mnbuf, "fdiv") == 0 || strcmp(mnbuf, "fdivr") == 0) {
        unsigned stsrc;
        unsigned stdst;
        unsigned char mem_d8_regf;
        unsigned char mem_dc_regf;
        int stack_single_base;
        unsigned char base;

        if (lookup_i386_x87_arith_opcode(mnbuf, &mem_d8_regf, &mem_dc_regf, &stack_single_base, &base) != 0) {
            return -1;
        }
        if (insn->operand_count == 1 && a != NULL) {
            if (a->kind == AS_OPERAND_MEMORY) {
                return emit_i386_x87_mem_by_size(insn, a, 0xd8, mem_d8_regf, 0xdc, mem_dc_regf, -1, 0u,
                                                 out, out_cap, out_len);
            }
            if (operand_st_index(a, &stsrc) != 0) {
                return -1;
            }
            if (stack_single_base >= 0) {
                out[0] = 0xd8;
                out[1] = (unsigned char)((unsigned char)stack_single_base + (stsrc & 7u));
                if (out_len != NULL) *out_len = 2;
                return 0;
            }
            return -1;
        }
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            operand_st_index(src, &stsrc) != 0 || operand_st_index(dst, &stdst) != 0 ||
            stdst != 0) {
            return -1;
        }
        out[0] = 0xd8;
        out[1] = (unsigned char)(base + (stsrc & 7u));
        if (out_len != NULL) {
            *out_len = 2;
        }
        return 0;
    }
    if (strcmp(mnbuf, "fstp") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        if (a->kind == AS_OPERAND_MEMORY) {
            return emit_i386_x87_mem_by_size(insn, a, 0xd9, 3u, 0xdd, 3u, 0xdb, 7u, out, out_cap, out_len);
        }
        if (a->kind != AS_OPERAND_COPROCESSOR || parse_st_index(a->u.coproc, &stidx) != 0) {
            return -1;
        }
        out[0] = 0xdd;
        out[1] = (unsigned char)(0xd8u + stidx);
        if (out_len != NULL) {
            *out_len = 2;
        }
        return 0;
    }
    if (strcmp(mnbuf, "ffree") == 0 || strcmp(mnbuf, "fst") == 0 || strcmp(mnbuf, "fucom") == 0 || strcmp(mnbuf, "fucomp") == 0) {
        unsigned char base;
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        if (strcmp(mnbuf, "fst") == 0 && a->kind == AS_OPERAND_MEMORY) {
            return emit_i386_x87_mem_by_size(insn, a, 0xd9, 2u, 0xdd, 2u, -1, 0u, out, out_cap, out_len);
        }
        if (operand_st_index(a, &stidx) != 0) {
            return -1;
        }
        if (lookup_i386_x87_stack_unary_opcode(mnbuf, &base) != 0) {
            return -1;
        }
        out[0] = 0xdd;
        out[1] = (unsigned char)(base + (stidx & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "fucompp") == 0) {
        if (insn->operand_count != 0) {
            return -1;
        }
        out[0] = 0xda;
        out[1] = 0xe9;
        if (out_len != NULL) {
            *out_len = 2;
        }
        return 0;
    }
    if (strcmp(mnbuf, "sahf") == 0) {
        if (insn->operand_count != 0) {
            return -1;
        }
        out[0] = 0x9e;
        if (out_len != NULL) {
            *out_len = 1;
        }
        return 0;
    }
    if (strcmp(mnbuf, "int1") == 0) {
        if (insn->operand_count != 0) {
            return -1;
        }
        out[0] = 0xf1;
        if (out_len != NULL) *out_len = 1;
        return 0;
    }
    if (strcmp(mnbuf, "cmc") == 0) {
        if (insn->operand_count != 0) {
            return -1;
        }
        out[0] = 0xf5;
        if (out_len != NULL) *out_len = 1;
        return 0;
    }
    if (strcmp(mnbuf, "fnstsw") == 0) {
        as_x86_reg_t gr;
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        if (a->kind == AS_OPERAND_REGISTER) {
            if (parse_x86_reg(a->u.reg, &gr) != 0 || ((unsigned)gr & 7u) != AS_X86_REG_EAX) {
                return -1;
            }
            out[0] = 0xdf;
            out[1] = 0xe0;
            if (out_len != NULL) {
                *out_len = 2;
            }
            return 0;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdd, 7u, a, out, out_cap, out_len);
    }
    {
        unsigned char opcode;
        unsigned reg_field;

        if (lookup_i386_x87_mem_exact(mnbuf, &opcode, &reg_field) == 0) {
            if (insn->operand_count != 1 || a == NULL) {
                return -1;
            }
            return emit_i386_prefixed_1byte_x87_mem(insn, opcode, reg_field, a, out, out_cap, out_len);
        }
    }
    {
        unsigned char opcode16;
        unsigned reg16;
        unsigned char opcode32;
        unsigned reg32;
        unsigned char opcode64;
        unsigned reg64;

        if (lookup_i386_x87_mem16_32_named(mnbuf, &opcode16, &reg16, &opcode32, &reg32) == 0) {
            if (insn->operand_count != 1 || a == NULL) return -1;
            return emit_i386_x87_mem16_32(insn, a, opcode16, reg16, opcode32, reg32, out, out_cap, out_len);
        }
        if (lookup_i386_x87_mem16_32_64_named(mnbuf, &opcode16, &reg16, &opcode32, &reg32, &opcode64, &reg64) == 0) {
            if (insn->operand_count != 1 || a == NULL) return -1;
            return emit_i386_x87_mem16_32_64(insn, a, opcode16, reg16, opcode32, reg32, opcode64, reg64,
                                             out, out_cap, out_len);
        }
    }
    if (strcmp(mnbuf, "fiaddl") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xda, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fimull") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xda, 1u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fisubl") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xda, 4u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fisubrl") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xda, 5u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fidivl") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xda, 6u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fidivrl") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xda, 7u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fiadds") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xde, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fimuls") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xde, 1u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fisubs") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xde, 4u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fisubrs") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xde, 5u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fidivs") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xde, 6u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fidivrs") == 0) {
        if (insn->operand_count != 1 || a == NULL) return -1;
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xde, 7u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "frstor") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdd, 4u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fnsave") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdd, 6u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "faddp") == 0 || strcmp(mnbuf, "fmulp") == 0 || strcmp(mnbuf, "fsubp") == 0 ||
        strcmp(mnbuf, "fsubrp") == 0 || strcmp(mnbuf, "fdivp") == 0 || strcmp(mnbuf, "fdivrp") == 0) {
        unsigned char base;
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            operand_st_index(src, &stsrc) != 0 || operand_st_index(dst, &stdst) != 0 ||
            stdst != 0) {
            return -1;
        }
        if (lookup_i386_x87_pop2_opcode(mnbuf, &base) != 0) {
            return -1;
        }
        out[0] = 0xde;
        out[1] = (unsigned char)(base + (stsrc & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "ffreep") == 0) {
        if (insn->operand_count != 1 || a == NULL || operand_st_index(a, &stidx) != 0) {
            return -1;
        }
        out[0] = 0xdf;
        out[1] = (unsigned char)(0xc0u + (stidx & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "fucomip") == 0 || strcmp(mnbuf, "fcomip") == 0) {
        unsigned char base;
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            operand_st_index(src, &stsrc) != 0 || operand_st_index(dst, &stdst) != 0 ||
            stdst != 0) {
            return -1;
        }
        if (lookup_i386_x87_ipcompare_opcode(mnbuf, &base) != 0) {
            return -1;
        }
        out[0] = 0xdf;
        out[1] = (unsigned char)(base + (stsrc & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "pxor") == 0) {
        unsigned char prefix;

        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER) {
            return -1;
        }
        if (parse_mmx_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            prefix = 0x00;
        } else if (parse_xmm_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            prefix = 0x66;
        } else {
            return -1;
        }
        return emit_i386_legacy_simd_rm(prefix, 0xef, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movdqa") == 0) {
        return emit_i386_xmm_move_rm(0x66, 0x6f, 0x7f, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movdqu") == 0) {
        return emit_i386_xmm_move_rm(0xf3, 0x6f, 0x7f, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtsi2sd") == 0) {
        as_x86_reg_t gr;
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER) {
            if (parse_x86_reg(src->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
                return -1;
            }
            out[0] = 0xf2;
            out[1] = 0x0f;
            out[2] = 0x2a;
            out[3] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (gr & 7u));
            if (out_len != NULL) {
                *out_len = 4;
            }
            return 0;
        }
        return emit_i386_prefixed_0f_rm(0xf2, 0x2a, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movss") == 0) {
        return emit_i386_xmm_move_rm(0xf3, 0x10, 0x11, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtsi2ss") == 0) {
        as_x86_reg_t gr;

        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER) {
            if (parse_x86_reg(src->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
                return -1;
            }
            out[0] = 0xf3;
            out[1] = 0x0f;
            out[2] = 0x2a;
            out[3] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (gr & 7u));
            if (out_len != NULL) {
                *out_len = 4;
            }
            return 0;
        }
        return emit_i386_prefixed_0f_rm(0xf3, 0x2a, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movntsd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || src->kind != AS_OPERAND_REGISTER ||
            dst->kind == AS_OPERAND_REGISTER || parse_xmm_reg(src->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0xf2, 0x2b, xr, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movntss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || src->kind != AS_OPERAND_REGISTER ||
            dst->kind == AS_OPERAND_REGISTER || parse_xmm_reg(src->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0xf3, 0x2b, xr, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttsd2si") == 0 || strcmp(mnbuf, "cvtsd2si") == 0) {
        unsigned char opcode2;

        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_x86_reg(dst->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        if (lookup_i386_f2_scalar_xmm_opcode(mnbuf, &opcode2) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0xf2, opcode2, (unsigned)gr & 7u, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttss2si") == 0 || strcmp(mnbuf, "cvtss2si") == 0) {
        unsigned char opcode2;

        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_x86_reg(dst->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        if (lookup_i386_f3_scalar_xmm_opcode(mnbuf, &opcode2) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0xf3, opcode2, (unsigned)gr & 7u, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "sqrtsd") == 0 || strcmp(mnbuf, "addsd") == 0 || strcmp(mnbuf, "mulsd") == 0 ||
        strcmp(mnbuf, "cvtsd2ss") == 0 || strcmp(mnbuf, "subsd") == 0 || strcmp(mnbuf, "minsd") == 0 ||
        strcmp(mnbuf, "divsd") == 0 || strcmp(mnbuf, "maxsd") == 0) {
        unsigned char opcode2;

        if (lookup_i386_f2_scalar_xmm_opcode(mnbuf, &opcode2) != 0) {
            return -1;
        }
        return emit_i386_prefixed_xmm_srcdst_rm(0xf2, opcode2, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "sqrtss") == 0 || strcmp(mnbuf, "rsqrtss") == 0 || strcmp(mnbuf, "rcpss") == 0 ||
        strcmp(mnbuf, "addss") == 0 || strcmp(mnbuf, "mulss") == 0 || strcmp(mnbuf, "cvtss2sd") == 0 ||
        strcmp(mnbuf, "cvttps2dq") == 0 || strcmp(mnbuf, "subss") == 0 || strcmp(mnbuf, "minss") == 0 ||
        strcmp(mnbuf, "divss") == 0 || strcmp(mnbuf, "maxss") == 0) {
        unsigned char opcode2;

        if (lookup_i386_f3_scalar_xmm_opcode(mnbuf, &opcode2) != 0) {
            return -1;
        }
        return emit_i386_prefixed_xmm_srcdst_rm(0xf3, opcode2, src, dst, out, out_cap, out_len);
    }
    {
        unsigned char opcode2;

        if (lookup_i386_packed_rm_opcode(mnbuf, &opcode2) == 0) {
            unsigned char prefix;

            if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER) {
                return -1;
            }
            if (parse_mmx_reg(dst->u.reg, &xr) == 0) {
                if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xm) != 0) {
                    return -1;
                }
                prefix = 0x00;
            } else if (parse_xmm_reg(dst->u.reg, &xr) == 0) {
                if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
                    return -1;
                }
                prefix = 0x66;
            } else {
                return -1;
            }
            /*
             * A count that is a number: the shifts have a form for it,
             * 0F 71, 72 or 73 with the register in the r/m field, and
             * nothing else here takes a number at all.  The number was
             * taken for an address, and `psllw $1, %mm0` shifted by what
             * was in memory at address 1.
             */
            if (src->kind == AS_OPERAND_IMMEDIATE || src->kind == AS_OPERAND_LABEL_REF) {
                static const struct {
                    const char *name;
                    unsigned char opcode;
                    unsigned char digit;
                } by_number[] = {
                    {"psrlw", 0x71, 2}, {"psraw", 0x71, 4}, {"psllw", 0x71, 6}, {"psrld", 0x72, 2},
                    {"psrad", 0x72, 4}, {"pslld", 0x72, 6}, {"psrlq", 0x73, 2}, {"psllq", 0x73, 6},
                };
                long long count = 0;
                size_t k;
                size_t at = 0;

                if (src->raw == NULL || src->raw[0] != '$') {
                    /* (A bare number or name is an address, as written.) */
                    return emit_i386_legacy_simd_rm(prefix, opcode2, xr, src, out, out_cap, out_len);
                }
                for (k = 0; k < sizeof(by_number) / sizeof(by_number[0]); ++k) {
                    if (strcmp(mnbuf, by_number[k].name) == 0) {
                        break;
                    }
                }
                if (k == sizeof(by_number) / sizeof(by_number[0]) || eval_expr_const(src->u.expr, &count) != 0 ||
                    count < 0 || count > 255 || out_cap < 5) {
                    return -1;
                }
                if (prefix != 0x00) {
                    out[at++] = prefix;
                }
                out[at++] = 0x0f;
                out[at++] = by_number[k].opcode;
                out[at++] = (unsigned char)(0xc0u | (by_number[k].digit << 3) | (xr & 7u));
                out[at++] = (unsigned char)count;
                *out_len = at;
                return 0;
            }
            return emit_i386_legacy_simd_rm(prefix, opcode2, xr, src, out, out_cap, out_len);
        }
    }
    if (strcmp(mnbuf, "movd") == 0) {
        as_x86_reg_t gr;
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_mmx_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_x86_reg(src->u.reg, &gr) != 0) {
                return -1;
            }
            return emit_i386_legacy_simd_rm(0x00, 0x6e, xr, src, out, out_cap, out_len);
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_x86_reg(src->u.reg, &gr) != 0) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(0x66, 0x6e, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x00, 0x7e, xr, dst, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x7e, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "pshufw") == 0) {
        const as_operand_t *imm_op;
        const as_operand_t *rm_op;
        const as_operand_t *dst_op;
        long long immv;

        if (insn->operand_count != 3) {
            return -1;
        }
        if (intel_syntax) {
            dst_op = &insn->operands[0];
            rm_op = &insn->operands[1];
            imm_op = &insn->operands[2];
        } else {
            imm_op = &insn->operands[0];
            rm_op = &insn->operands[1];
            dst_op = &insn->operands[2];
        }
        if (dst_op->kind != AS_OPERAND_REGISTER || parse_mmx_reg(dst_op->u.reg, &xr) != 0) {
            return -1;
        }
        if (rm_op->kind == AS_OPERAND_REGISTER && parse_mmx_reg(rm_op->u.reg, &xm) != 0) {
            return -1;
        }
        if ((imm_op->kind != AS_OPERAND_IMMEDIATE && imm_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(imm_op->u.expr, &immv) != 0 || !x86_shuffle_imm8_fits(immv)) {
            return -1;
        }
        return emit_i386_legacy_simd_rm_imm8(0x00, 0x70, xr, rm_op, (unsigned char)immv, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "vmread") == 0 || strcmp(mnbuf, "vmwrite") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;

        if (select_x86_vmread_vmwrite_operands(insn, intel_syntax, mnbuf, &rm_op, &reg_op) != 0) {
            return -1;
        }
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, strcmp(mnbuf, "vmread") == 0 ? 0x78 : 0x79,
                                        (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cpuid") == 0 || strcmp(mnbuf, "rsm") == 0) {
        unsigned char opcode2;

        if (insn->operand_count != 0 || out == NULL || out_len == NULL || out_cap < 2) {
            return -1;
        }
        if (lookup_i386_fixed_0f_opcode(mnbuf, &opcode2) != 0) {
            return -1;
        }
        out[0] = 0x0f;
        out[1] = opcode2;
        *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "shld") == 0 || strcmp(mnbuf, "shrd") == 0) {
        const as_operand_t *count_op;
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        as_x86_reg_t gr;
        long long immv;
        unsigned char reg_opcode;
        unsigned char imm_opcode;

        if (select_x86_dstsrc_tail_operand(insn, intel_syntax, &dst_op, &src_op, &count_op) != 0) {
            return -1;
        }
        if (src_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(src_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (lookup_i386_shiftd_opcode(mnbuf, &reg_opcode, &imm_opcode) != 0) {
            return -1;
        }
        if (count_op->kind == AS_OPERAND_REGISTER) {
            as_x86_reg_t cr;

            if (parse_x86_reg(count_op->u.reg, &cr) != 0 || cr != AS_X86_REG_ECX) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(0x00, reg_opcode, (unsigned)gr & 7u, dst_op, out, out_cap, out_len);
        }
        if ((count_op->kind != AS_OPERAND_IMMEDIATE && count_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(count_op->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        return emit_i386_legacy_simd_rm_imm8(0x00, imm_opcode, (unsigned)gr & 7u, dst_op,
                                             (unsigned char)immv, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fxsave") == 0 || strcmp(mnbuf, "fxrstor") == 0 || strcmp(mnbuf, "ldmxcsr") == 0 ||
        strcmp(mnbuf, "stmxcsr") == 0 || strcmp(mnbuf, "xsave") == 0 || strcmp(mnbuf, "xrstor") == 0 ||
        strcmp(mnbuf, "xsaveopt") == 0 || strcmp(mnbuf, "clflush") == 0) {
        unsigned reg_field;
        unsigned char prefix;

        if (insn->operand_count != 1 || a == NULL || a->kind == AS_OPERAND_REGISTER) {
            return -1;
        }
        if (lookup_i386_0fae_rm_group(mnbuf, &prefix, &reg_field) != 0) {
            return -1;
        }
        if ((insn->prefixes & AS_PREFIX_DATA16) != 0 && prefix == 0x00) {
            prefix = 0x66;
        }
        return emit_i386_prefixed_0f_rm(prefix, 0xae, reg_field, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "lfence") == 0 || strcmp(mnbuf, "mfence") == 0 || strcmp(mnbuf, "sfence") == 0) {
        unsigned char tail;
        size_t pos = 0;

        if (insn->operand_count != 0) {
            return -1;
        }
        if (lookup_i386_fence_tail(mnbuf, &tail) != 0) {
            return -1;
        }
        if ((insn->prefixes & AS_PREFIX_DATA16) != 0) {
            out[pos++] = 0x66;
        }
        out[pos++] = 0x0f;
        out[pos++] = 0xae;
        out[pos++] = tail;
        if (out_len != NULL) *out_len = pos;
        return 0;
    }
    if (strcmp(mnbuf, "rdfsbase") == 0 || strcmp(mnbuf, "rdgsbase") == 0 || strcmp(mnbuf, "wrfsbase") == 0 ||
        strcmp(mnbuf, "wrgsbase") == 0) {
        unsigned reg_field;

        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        if (lookup_i386_f3_0fae_group(mnbuf, &reg_field) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0xf3, 0xae, reg_field, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "tpause") == 0) {
        as_x86_reg_t gr;

        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_REGISTER ||
            parse_x86_reg(a->u.reg, &gr) != 0 || (gr & 7u) != AS_X86_REG_EAX) {
            return -1;
        }
        out[0] = 0x66;
        out[1] = 0x0f;
        out[2] = 0xae;
        out[3] = 0xf0;
        if (out_len != NULL) *out_len = 4;
        return 0;
    }
    if (strcmp(mnbuf, "cmpxchg") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        unsigned reg_field;
        unsigned char opcode2;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (intel_syntax) {
            rm_op = &insn->operands[0];
            reg_op = &insn->operands[1];
        } else {
            reg_op = &insn->operands[0];
            rm_op = &insn->operands[1];
        }
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_i386_modrm_reg(reg_op->u.reg, &reg_field) != 0) {
            return -1;
        }
        /* Any byte register: with %ah to %bh, which the list of the low
         * ones leaves out, this was the 32-bit instruction. */
        opcode2 = x86_reg_width_bits(reg_op->u.reg) == 8 ? 0xb0 : 0xb1;
        return emit_i386_prefixed_0f_rm(0x00, opcode2, reg_field, rm_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "sldt") == 0 || strcmp(mnbuf, "str") == 0 || strcmp(mnbuf, "lldt") == 0 ||
        strcmp(mnbuf, "ltr") == 0 || strcmp(mnbuf, "verr") == 0 || strcmp(mnbuf, "verw") == 0) {
        unsigned reg_field;

        if (insn->operand_count != 1 || a == NULL || a->kind == AS_OPERAND_COPROCESSOR) {
            return -1;
        }
        if (lookup_i386_0f00_group(mnbuf, &reg_field) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x00, reg_field, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "bt") == 0 || strcmp(mnbuf, "bts") == 0 || strcmp(mnbuf, "btr") == 0 || strcmp(mnbuf, "btc") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *src_op;
        unsigned reg_field;
        unsigned char opcode2;
        long long immv;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (intel_syntax) {
            rm_op = &insn->operands[0];
            src_op = &insn->operands[1];
        } else {
            src_op = &insn->operands[0];
            rm_op = &insn->operands[1];
        }
        if (lookup_i386_bt_group(mnbuf, &reg_field, &opcode2) != 0) {
            return -1;
        }
        if (src_op->kind == AS_OPERAND_REGISTER) {
            as_x86_reg_t gr;

            if (parse_x86_reg(src_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(0x00, opcode2, (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
        }
        if ((src_op->kind != AS_OPERAND_IMMEDIATE && src_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(src_op->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        return emit_i386_legacy_simd_rm_imm8(0x00, 0xba, reg_field, rm_op, (unsigned char)immv, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ud1") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;

        if (insn->operand_count != 2) {
            return -1;
        }
        rm_op = src;
        reg_op = dst;
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0xb9, (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "bsf") == 0 || strcmp(mnbuf, "bsr") == 0 ||
        strcmp(mnbuf, "popcnt") == 0 || strcmp(mnbuf, "tzcnt") == 0 || strcmp(mnbuf, "lzcnt") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;
        unsigned char prefix;
        unsigned char opcode2;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (intel_syntax) {
            reg_op = &insn->operands[0];
            rm_op = &insn->operands[1];
        } else {
            rm_op = &insn->operands[0];
            reg_op = &insn->operands[1];
        }
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (lookup_i386_scanbit_group(mnbuf, &prefix, &opcode2) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, opcode2, (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
    }
    /* cmpsd with three operands is SSE2's; with fewer it is the string
     * instruction, which is the encoder's. */
    if (strcmp(mnbuf, "cmpps") == 0 || strcmp(mnbuf, "cmppd") == 0 || strcmp(mnbuf, "cmpss") == 0 ||
        (strcmp(mnbuf, "cmpsd") == 0 && insn->operand_count == 3) ||
        strcmp(mnbuf, "pinsrw") == 0 || strcmp(mnbuf, "pextrw") == 0 ||
        strcmp(mnbuf, "shufps") == 0 || strcmp(mnbuf, "shufpd") == 0 ||
        strcmp(mnbuf, "pshufd") == 0) {
        const as_operand_t *imm_op;
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        long long immv;
        as_x86_reg_t gr;

        if (select_x86_dstsrc_tail_operand(insn, intel_syntax, &dst_op, &src_op, &imm_op) != 0) {
            return -1;
        }
        if ((imm_op->kind != AS_OPERAND_IMMEDIATE && imm_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(imm_op->u.expr, &immv) != 0 || immv > 255 ||
            immv < (x86_is_shuffle_mnemonic(mnbuf) ? -128 : 0)) {
            return -1;
        }
        if (strcmp(mnbuf, "cmpps") == 0 || strcmp(mnbuf, "cmppd") == 0 || strcmp(mnbuf, "cmpss") == 0 ||
            strcmp(mnbuf, "cmpsd") == 0 ||
            strcmp(mnbuf, "shufps") == 0 || strcmp(mnbuf, "shufpd") == 0 ||
            strcmp(mnbuf, "pshufd") == 0) {
            unsigned char prefix;
            unsigned char opcode2;

            if (dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0) {
                return -1;
            }
            if (src_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src_op->u.reg, &xm) != 0) {
                return -1;
            }
            if (lookup_i386_xmm_imm8_family(mnbuf, &prefix, &opcode2) != 0) {
                return -1;
            }
            return emit_i386_legacy_simd_rm_imm8(prefix, opcode2, xr, src_op, (unsigned char)immv,
                                                 out, out_cap, out_len);
        }
        if (strcmp(mnbuf, "pinsrw") == 0) {
            if (dst_op->kind != AS_OPERAND_REGISTER) {
                return -1;
            }
            if (src_op->kind == AS_OPERAND_REGISTER && parse_x86_reg(src_op->u.reg, &gr) != 0) {
                return -1;
            }
            if (parse_mmx_reg(dst_op->u.reg, &xr) == 0) {
                return emit_i386_legacy_simd_rm_imm8(0x00, 0xc4, xr, src_op, (unsigned char)immv, out, out_cap, out_len);
            }
            if (parse_xmm_reg(dst_op->u.reg, &xr) == 0) {
                return emit_i386_legacy_simd_rm_imm8(0x66, 0xc4, xr, src_op, (unsigned char)immv, out, out_cap, out_len);
            }
            return -1;
        }
        if (dst_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(dst_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (src_op->kind == AS_OPERAND_REGISTER) {
            if (parse_mmx_reg(src_op->u.reg, &xm) == 0) {
                return emit_i386_legacy_simd_rm_imm8(0x00, 0xc5, (unsigned)gr & 7u, src_op, (unsigned char)immv,
                                                     out, out_cap, out_len);
            }
            if (parse_xmm_reg(src_op->u.reg, &xm) == 0) {
                return emit_i386_legacy_simd_rm_imm8(0x66, 0xc5, (unsigned)gr & 7u, src_op, (unsigned char)immv,
                                                     out, out_cap, out_len);
            }
            return -1;
        }
        return emit_i386_legacy_simd_rm_imm8(0x66, 0xc5, (unsigned)gr & 7u, src_op, (unsigned char)immv,
                                             out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movnti") == 0) {
        const as_operand_t *mem_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (intel_syntax) {
            mem_op = &insn->operands[0];
            reg_op = &insn->operands[1];
        } else {
            reg_op = &insn->operands[0];
            mem_op = &insn->operands[1];
        }
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0xc3, (unsigned)gr & 7u, mem_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "pmovmskb") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_i386_movmask_family(mnbuf, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movntq") == 0) {
        return emit_i386_movnt_store(0x00, 0xe7, 0, intel_syntax, insn, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttpd2dq") == 0) {
        return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0xe6, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtpd2dq") == 0) {
        return emit_i386_prefixed_xmm_srcdst_rm(0xf2, 0xe6, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movntdq") == 0) {
        return emit_i386_movnt_store(0x66, 0xe7, 1, intel_syntax, insn, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "maskmovq") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_i386_maskmov(0x00, 0, dst, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "maskmovdqu") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_i386_maskmov(0x66, 1, dst, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ud0") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;

        if (insn->operand_count != 2) {
            return -1;
        }
        rm_op = src;
        reg_op = dst;
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0xff, (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
    }
    /* (The rest of this group -- rdrand, rdseed, rdpid, xrstors and
     * those of virtual machines -- are rows of as_x86_vec_table.c.) */
    if (strcmp(mnbuf, "cmpxchg8b") == 0 || strcmp(mnbuf, "xsavec") == 0 || strcmp(mnbuf, "xsaves") == 0) {
        unsigned reg_field;
        unsigned char prefix;
        const as_operand_t *op;

        if (insn->operand_count != 1) {
            return -1;
        }
        op = &insn->operands[0];
        if (lookup_i386_0fc7_group(mnbuf, &prefix, &reg_field) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, 0xc7, reg_field, op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "sha1nexte") == 0 || strcmp(mnbuf, "sha1msg1") == 0 || strcmp(mnbuf, "sha1msg2") == 0 ||
        strcmp(mnbuf, "sha256msg1") == 0 || strcmp(mnbuf, "sha256msg2") == 0) {
        unsigned char opcode3;

        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        if (lookup_i386_sha_0f38_opcode(mnbuf, &opcode3) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(0x00, 0x38, opcode3, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "sha256rnds2") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *dst_op;

        if (intel_syntax) {
            if (insn->operand_count < 2) {
                return -1;
            }
            dst_op = &insn->operands[0];
            rm_op = &insn->operands[1];
        } else {
            if (insn->operand_count != 3) {
                return -1;
            }
            if (insn->operands[0].kind != AS_OPERAND_REGISTER) {
                return -1;
            }
            rm_op = &insn->operands[1];
            dst_op = &insn->operands[2];
        }
        if (dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0) {
            return -1;
        }
        if (rm_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(rm_op->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(0x00, 0x38, 0xcb, xr, rm_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "sha1rnds4") == 0) {
        const as_operand_t *imm_op;
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        long long immv;

        if (insn->operand_count != 3) {
            return -1;
        }
        if (intel_syntax) {
            dst_op = &insn->operands[0];
            src_op = &insn->operands[1];
            imm_op = &insn->operands[2];
        } else {
            imm_op = &insn->operands[0];
            src_op = &insn->operands[1];
            dst_op = &insn->operands[2];
        }
        if ((imm_op->kind != AS_OPERAND_IMMEDIATE && imm_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(imm_op->u.expr, &immv) != 0 || immv < 0 || immv > 255 ||
            dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0) {
            return -1;
        }
        if (src_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src_op->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm_imm8(0x00, 0x3a, 0xcc, xr, src_op, (unsigned char)immv,
                                                 out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "pclmulqdq") == 0 || strcmp(mnbuf, "gf2p8affineqb") == 0 ||
        strcmp(mnbuf, "gf2p8affineinvqb") == 0 || strcmp(mnbuf, "aeskeygenassist") == 0) {
        const as_operand_t *imm_op;
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        unsigned char opcode3;
        long long immv;

        if (insn->operand_count != 3) {
            return -1;
        }
        if (intel_syntax) {
            dst_op = &insn->operands[0];
            src_op = &insn->operands[1];
            imm_op = &insn->operands[2];
        } else {
            imm_op = &insn->operands[0];
            src_op = &insn->operands[1];
            dst_op = &insn->operands[2];
        }
        if ((imm_op->kind != AS_OPERAND_IMMEDIATE && imm_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(imm_op->u.expr, &immv) != 0 || immv < 0 || immv > 255 ||
            dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0) {
            return -1;
        }
        if (src_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src_op->u.reg, &xm) != 0) {
            return -1;
        }
        if (lookup_i386_crypto_0f3a_imm8_opcode(mnbuf, &opcode3) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm_imm8(0x66, 0x3a, opcode3, xr, src_op, (unsigned char)immv,
                                                 out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "palignr") == 0) {
        const as_operand_t *imm_op;
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        unsigned char prefix;
        long long immv;

        if (insn->operand_count != 3) {
            return -1;
        }
        if (intel_syntax) {
            dst_op = &insn->operands[0];
            src_op = &insn->operands[1];
            imm_op = &insn->operands[2];
        } else {
            imm_op = &insn->operands[0];
            src_op = &insn->operands[1];
            dst_op = &insn->operands[2];
        }
        if ((imm_op->kind != AS_OPERAND_IMMEDIATE && imm_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(imm_op->u.expr, &immv) != 0 || immv < 0 || immv > 255 ||
            dst_op->kind != AS_OPERAND_REGISTER) {
            return -1;
        }
        if (parse_mmx_reg(dst_op->u.reg, &xr) == 0) {
            if (src_op->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src_op->u.reg, &xm) != 0) {
                return -1;
            }
            prefix = 0x00;
        } else if (parse_xmm_reg(dst_op->u.reg, &xr) == 0) {
            if (src_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src_op->u.reg, &xm) != 0) {
                return -1;
            }
            prefix = 0x66;
        } else {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm_imm8(prefix, 0x3a, 0x0f, xr, src_op, (unsigned char)immv,
                                                 out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movbe") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;
        unsigned char opcode3;
        int first_is_reg;

        if (insn->operand_count != 2) {
            return -1;
        }
        first_is_reg = insn->operands[0].kind == AS_OPERAND_REGISTER ? 1 : 0;
        if (select_i386_actual_reg_rm_operands(insn, &reg_op, &rm_op) != 0) {
            return -1;
        }
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (lookup_i386_movbe_opcode(intel_syntax, first_is_reg, &opcode3) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(0x00, 0x38, opcode3, (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "gf2p8mulb") == 0 || strcmp(mnbuf, "aesimc") == 0 || strcmp(mnbuf, "aesenc") == 0 ||
        strcmp(mnbuf, "aesenclast") == 0 || strcmp(mnbuf, "aesdec") == 0 || strcmp(mnbuf, "aesdeclast") == 0) {
        unsigned char opcode3;

        if (dst == NULL || src == NULL || insn->operand_count != 2 || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        if (lookup_i386_aes_0f38_opcode(mnbuf, &opcode3) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(0x66, 0x38, opcode3, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "loadiwkey") == 0 || strcmp(mnbuf, "aesenc128kl") == 0 || strcmp(mnbuf, "aesdec128kl") == 0 ||
        strcmp(mnbuf, "aesenc256kl") == 0 || strcmp(mnbuf, "aesdec256kl") == 0) {
        unsigned char opcode3;

        if (dst == NULL || src == NULL || insn->operand_count != 2 || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        if (lookup_i386_keylocker_opcode(mnbuf, &opcode3) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(0xf3, 0x38, opcode3, xr, src, out, out_cap, out_len);
    }
    /* (wrssd, wrussd, movdiri and movdir64b are rows of
     * as_x86_vec_table.c.) */
    if (strcmp(mnbuf, "enqcmd") == 0 || strcmp(mnbuf, "enqcmds") == 0 || strcmp(mnbuf, "aadd") == 0 ||
        strcmp(mnbuf, "aand") == 0 || strcmp(mnbuf, "aor") == 0 || strcmp(mnbuf, "axor") == 0) {
        const as_operand_t *mem_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;
        unsigned char opcode3;
        unsigned char prefix;
        int reg_first;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (lookup_i386_memorder_opcode(mnbuf, &prefix, &opcode3, &reg_first) != 0) {
            return -1;
        }
        if (select_i386_syntax_reg_rm_operands(insn, intel_syntax, reg_first, &reg_op, &mem_op) != 0) {
            return -1;
        }
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(prefix, 0x38, opcode3, (unsigned)gr & 7u, mem_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "adcx") == 0 || strcmp(mnbuf, "adox") == 0) {
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        as_x86_reg_t gr;
        unsigned char prefix;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (intel_syntax) {
            dst_op = &insn->operands[0];
            src_op = &insn->operands[1];
        } else {
            src_op = &insn->operands[0];
            dst_op = &insn->operands[1];
        }
        if (dst_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(dst_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (lookup_i386_adcxo_prefix(mnbuf, &prefix) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(prefix, 0x38, 0xf6, (unsigned)gr & 7u, src_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "encodekey128") == 0 || strcmp(mnbuf, "encodekey256") == 0) {
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        unsigned char opcode3;
        as_x86_reg_t gr;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (intel_syntax) {
            dst_op = &insn->operands[0];
            src_op = &insn->operands[1];
        } else {
            src_op = &insn->operands[0];
            dst_op = &insn->operands[1];
        }
        if (dst_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(dst_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (lookup_i386_encodekey_opcode(mnbuf, &opcode3) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(0xf3, 0x38, opcode3, (unsigned)gr & 7u, src_op, out, out_cap, out_len);
    }
    {
        unsigned char prefix;
        unsigned char load_opcode2;
        unsigned char store_opcode2;
        int is_partial;

        if (lookup_x86_xmm_move_family(mnbuf, &prefix, &load_opcode2, &store_opcode2, &is_partial) == 0 &&
            (strcmp(mnbuf, "movhlps") != 0 && strcmp(mnbuf, "movlhps") != 0)) {
            if (is_partial) {
                return emit_i386_xmm_partial_move_rm(prefix, load_opcode2, store_opcode2, src, dst, out, out_cap, out_len);
            }
            return emit_i386_xmm_move_rm(prefix, load_opcode2, store_opcode2, src, dst, out, out_cap, out_len);
        }
    }
    {
        unsigned char prefix;
        unsigned char opcode2;

        if (lookup_i386_mmx_xmm_convert_to_xmm(mnbuf, &prefix, &opcode2) == 0) {
            if (insn->operand_count != 2) {
                return -1;
            }
            return emit_i386_xmm_from_mmx_or_mem(prefix, opcode2, src, dst, out, out_cap, out_len);
        }
        if (lookup_i386_mmx_xmm_convert_from_xmm(mnbuf, &prefix, &opcode2) == 0) {
            if (insn->operand_count != 2) {
                return -1;
            }
            return emit_i386_mmx_from_xmm_or_mem(prefix, opcode2, src, dst, out, out_cap, out_len);
        }
    }
    if (emit_i386_comisd_family(mnbuf, src, dst, out, out_cap, out_len) == 0) {
        return 0;
    }
    if (emit_x86_extrq_insertq(mnbuf, insn, intel_syntax, 0, out, out_cap, out_len) == 0) {
        return 0;
    }
    if (emit_i386_xmm_shiftdq_imm8_family(mnbuf, insn, intel_syntax, out, out_cap, out_len) == 0) {
        return 0;
    }
    if (emit_i386_xmm_shuffle_tail_family(mnbuf, insn, intel_syntax, out, out_cap, out_len) == 0) {
        return 0;
    }
    {
        unsigned char opcode2;

        if (lookup_i386_xmm_regpair_opcode(mnbuf, &opcode2) == 0) {
            if (insn->operand_count != 2) {
                return -1;
            }
            return emit_i386_xmm_reg_srcdst_rm(0x00, opcode2, src, dst, out, out_cap, out_len);
        }
    }
    {
        unsigned char prefix;
        unsigned char opcode2;
        int use_xmm;

        if (lookup_i386_movnt_store_family(mnbuf, &prefix, &opcode2, &use_xmm) == 0) {
            if (insn->operand_count != 2) {
                return -1;
            }
            return emit_i386_movnt_store(prefix, opcode2, use_xmm, intel_syntax, insn, out, out_cap, out_len);
        }
    }
    {
        unsigned char opcode2;

        if (lookup_i386_xmm_unprefixed_opcode(mnbuf, &opcode2) == 0) {
            return emit_i386_xmm_reg_srcdst_rm(0x00, opcode2, src, dst, out, out_cap, out_len);
        }
    }
    {
        unsigned char prefix;

        if (lookup_i386_movmsk_prefix(mnbuf, &prefix) == 0) {
            return emit_i386_movmask_family(mnbuf, src, dst, out, out_cap, out_len);
        }
    }
    {
        unsigned char opcode2;

        if (lookup_i386_xmm_66_opcode(mnbuf, &opcode2) == 0) {
            return emit_i386_prefixed_xmm_srcdst_rm(0x66, opcode2, src, dst, out, out_cap, out_len);
        }
    }
    {
        unsigned char prefix;
        int dst_is_xmm;

        if (lookup_i386_mmx_xmm_bridge(mnbuf, &prefix, &dst_is_xmm) == 0) {
            if (insn->operand_count != 2) {
                return -1;
            }
            return emit_i386_mmx_xmm_bridge(prefix, src, dst, dst_is_xmm, out, out_cap, out_len);
        }
    }
    if (strcmp(mnbuf, "cvtdq2pd") == 0) {
        return emit_i386_prefixed_xmm_srcdst_rm(0xf3, 0xe6, src, dst, out, out_cap, out_len);
    }
    if (emit_i386_cachehint_family(mnbuf, a, out, out_cap, out_len) == 0) {
        return 0;
    }
    {
        static const struct {
            const char *mnemonic;
            unsigned char imm8;
        } ops[] = {
            {"pfcmpge", 0x90},
            {"pi2fw", 0x0c},
            {"pi2fd", 0x0d},
            {"pf2iw", 0x1c},
            {"pf2id", 0x1d},
            {"pfnacc", 0x8a},
            {"pfpnacc", 0x8e},
            {"pfmin", 0x94},
            {"pfrcp", 0x96},
            {"pfrsqrt", 0x97},
            {"pfsub", 0x9a},
            {"pfadd", 0x9e},
            {"pfcmpgt", 0xa0},
            {"pfmax", 0xa4},
            {"pfrcpit1", 0xa6},
            {"pfrsqit1", 0xa7},
            {"pfsubr", 0xaa},
            {"pfacc", 0xae},
            {"pfcmpeq", 0xb0},
            {"pfmul", 0xb4},
            {"pfrcpit2", 0xb6},
            {"pmulhrw", 0xb7},
            {"pswapd", 0xbb},
            {"pavgusb", 0xbf},
        };
        size_t i;

        for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
            if (strcmp(mnbuf, ops[i].mnemonic) != 0) {
                continue;
            }
            if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
                parse_mmx_reg(dst->u.reg, &xr) != 0) {
                return -1;
            }
            if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_3dnow_rm(xr, src, ops[i].imm8, out, out_cap, out_len);
        }
    }
    if (strcmp(mnbuf, "bndldx") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_i386_bnd_binary(0x00, 0x1a, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "bndstx") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || src->kind != AS_OPERAND_REGISTER ||
            parse_bnd_reg(src->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x1b, xr, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "bndmov") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_bnd_reg(dst->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x1a, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_bnd_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x1b, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "bndcl") == 0 || strcmp(mnbuf, "bndcu") == 0 || strcmp(mnbuf, "bndcn") == 0 ||
        strcmp(mnbuf, "bndmk") == 0) {
        unsigned char prefix;
        unsigned char opcode2;
        if (insn->operand_count != 2) {
            return -1;
        }
        if (lookup_i386_bndcmp_opcode(mnbuf, &prefix, &opcode2) != 0) {
            return -1;
        }
        return emit_i386_bnd_binary(prefix, opcode2, src, dst, out, out_cap, out_len);
    }

    return -1;
}

static int emit_x86_64_special(const as_instruction_t *insn, int intel_syntax, unsigned isa_level,
                               unsigned char *out, size_t out_cap, size_t *out_len) {
    const as_operand_t *a;
    const as_operand_t *b;
    const as_operand_t *src;
    const as_operand_t *dst;
    char mnbuf[32];
    as_x86_reg_t gr;
    long long imm64;
    unsigned char rex;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (insn == NULL || out == NULL || out_cap < 11) {
        return -1;
    }
    if (normalize_x86_mnemonic(insn->mnemonic, mnbuf, sizeof(mnbuf), NULL) != 0) {
        return -1;
    }
    if (insn->operand_count == 0) {
        if ((insn->prefixes & AS_PREFIX_REX) != 0 &&
            (strcmp(mnbuf, "rex") == 0 || strcmp(mnbuf, "rex64") == 0 || strncasecmp(mnbuf, "rex.", 4) == 0)) {
            unsigned char rex_only = (unsigned char)(0x40u | (insn->rex_bits & 0x0fu));

            if (out_cap < 1) {
                return -1;
            }
            out[0] = rex_only;
            if (out_len != NULL) {
                *out_len = 1;
            }
            return 0;
        }
    }
    if (insn->segment_override != NULL) {
        return -1;
    }
    if ((insn->prefixes & ~(AS_PREFIX_REX | AS_PREFIX_DATA16)) != 0) {
        return -1;
    }

    a = insn->operand_count > 0 ? &insn->operands[0] : NULL;
    b = insn->operand_count > 1 ? &insn->operands[1] : NULL;
    src = intel_syntax ? b : a;
    dst = intel_syntax ? a : b;

    if (strcmp(mnbuf, "extrq") == 0 || strcmp(mnbuf, "insertq") == 0) {
        return emit_x86_extrq_insertq(mnbuf, insn, intel_syntax, 1, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movabs") == 0) {
        size_t i;
        size_t pos = 0;
        const char *seg_override = NULL;
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (src->kind == AS_OPERAND_MEMORY && src->u.mem.base_reg == NULL && src->u.mem.index_reg == NULL &&
            src->u.mem.disp != NULL && dst->kind == AS_OPERAND_REGISTER &&
            parse_x86_reg(dst->u.reg, &gr) == 0 &&
            (streq_ci(dst->u.reg, "%al") || streq_ci(dst->u.reg, "al") ||
             streq_ci(dst->u.reg, "%ax") || streq_ci(dst->u.reg, "ax") ||
             streq_ci(dst->u.reg, "%eax") || streq_ci(dst->u.reg, "eax") ||
             streq_ci(dst->u.reg, "%rax") || streq_ci(dst->u.reg, "rax"))) {
            unsigned char opcode = (streq_ci(dst->u.reg, "%al") || streq_ci(dst->u.reg, "al")) ? 0xa0u : 0xa1u;

            if (eval_expr_const(src->u.mem.disp, &imm64) != 0) {
                if (expr_has_symbol(src->u.mem.disp)) imm64 = 0;
                else return -1;
            }
            seg_override = src->u.mem.segment_reg;
            if (seg_override != NULL && !streq_ci(seg_override, "ds")) {
                if (emit_seg_override_byte(out, out_cap, &pos, seg_override) != 0) {
                    return -1;
                }
            }
            if (streq_ci(dst->u.reg, "%ax") || streq_ci(dst->u.reg, "ax")) {
                out[pos++] = 0x66u;
            } else if (streq_ci(dst->u.reg, "%rax") || streq_ci(dst->u.reg, "rax")) {
                out[pos++] = 0x48u;
            }
            out[pos++] = opcode;
            for (i = 0; i < 8; ++i) {
                out[pos + i] = (unsigned char)(((unsigned long long)imm64 >> (i * 8u)) & 0xffu);
            }
            pos += 8;
            if (out_len != NULL) {
                *out_len = pos;
            }
            return 0;
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_MEMORY && dst->u.mem.base_reg == NULL &&
            dst->u.mem.index_reg == NULL && dst->u.mem.disp != NULL &&
            parse_x86_reg(src->u.reg, &gr) == 0 &&
            (streq_ci(src->u.reg, "%al") || streq_ci(src->u.reg, "al") ||
             streq_ci(src->u.reg, "%ax") || streq_ci(src->u.reg, "ax") ||
             streq_ci(src->u.reg, "%eax") || streq_ci(src->u.reg, "eax") ||
             streq_ci(src->u.reg, "%rax") || streq_ci(src->u.reg, "rax"))) {
            unsigned char opcode = (streq_ci(src->u.reg, "%al") || streq_ci(src->u.reg, "al")) ? 0xa2u : 0xa3u;

            if (eval_expr_const(dst->u.mem.disp, &imm64) != 0) {
                if (expr_has_symbol(dst->u.mem.disp)) imm64 = 0;
                else return -1;
            }
            seg_override = dst->u.mem.segment_reg;
            if (seg_override != NULL && !streq_ci(seg_override, "ds")) {
                if (emit_seg_override_byte(out, out_cap, &pos, seg_override) != 0) {
                    return -1;
                }
            }
            if (streq_ci(src->u.reg, "%ax") || streq_ci(src->u.reg, "ax")) {
                out[pos++] = 0x66u;
            } else if (streq_ci(src->u.reg, "%rax") || streq_ci(src->u.reg, "rax")) {
                out[pos++] = 0x48u;
            }
            out[pos++] = opcode;
            for (i = 0; i < 8; ++i) {
                out[pos + i] = (unsigned char)(((unsigned long long)imm64 >> (i * 8u)) & 0xffu);
            }
            pos += 8;
            if (out_len != NULL) {
                *out_len = pos;
            }
            return 0;
        }
        if (!intel_syntax &&
            (src->kind == AS_OPERAND_IMMEDIATE || src->kind == AS_OPERAND_LABEL_REF) &&
            dst->kind == AS_OPERAND_REGISTER &&
            src->raw != NULL && src->raw[0] != '$' && src->raw[0] != '#' &&
            parse_x86_reg(dst->u.reg, &gr) == 0 &&
            (streq_ci(dst->u.reg, "%al") || streq_ci(dst->u.reg, "al") ||
             streq_ci(dst->u.reg, "%ax") || streq_ci(dst->u.reg, "ax") ||
             streq_ci(dst->u.reg, "%eax") || streq_ci(dst->u.reg, "eax") ||
             streq_ci(dst->u.reg, "%rax") || streq_ci(dst->u.reg, "rax"))) {
            unsigned char opcode = (streq_ci(dst->u.reg, "%al") || streq_ci(dst->u.reg, "al")) ? 0xa0u : 0xa1u;

            if (eval_expr_const(src->u.expr, &imm64) != 0) {
                if (expr_has_symbol(src->u.expr)) imm64 = 0;
                else return -1;
            }
            if (streq_ci(dst->u.reg, "%rax") || streq_ci(dst->u.reg, "rax")) {
                out[0] = 0x48u;
                out[1] = opcode;
                for (i = 0; i < 8; ++i) {
                    out[2 + i] = (unsigned char)(((unsigned long long)imm64 >> (i * 8u)) & 0xffu);
                }
                if (out_len != NULL) {
                    *out_len = 10;
                }
            } else {
                out[0] = opcode;
                for (i = 0; i < 8; ++i) {
                    out[1 + i] = (unsigned char)(((unsigned long long)imm64 >> (i * 8u)) & 0xffu);
                }
                if (out_len != NULL) {
                    *out_len = 9;
                }
            }
            return 0;
        }
        if (!intel_syntax &&
            src->kind == AS_OPERAND_REGISTER &&
            (dst->kind == AS_OPERAND_IMMEDIATE || dst->kind == AS_OPERAND_LABEL_REF) &&
            dst->raw != NULL && dst->raw[0] != '$' && dst->raw[0] != '#' &&
            parse_x86_reg(src->u.reg, &gr) == 0 &&
            (streq_ci(src->u.reg, "%al") || streq_ci(src->u.reg, "al") ||
             streq_ci(src->u.reg, "%ax") || streq_ci(src->u.reg, "ax") ||
             streq_ci(src->u.reg, "%eax") || streq_ci(src->u.reg, "eax") ||
             streq_ci(src->u.reg, "%rax") || streq_ci(src->u.reg, "rax"))) {
            unsigned char opcode = (streq_ci(src->u.reg, "%al") || streq_ci(src->u.reg, "al")) ? 0xa2u : 0xa3u;

            if (eval_expr_const(dst->u.expr, &imm64) != 0) {
                if (expr_has_symbol(dst->u.expr)) imm64 = 0;
                else return -1;
            }
            if (streq_ci(src->u.reg, "%rax") || streq_ci(src->u.reg, "rax")) {
                out[0] = 0x48u;
                out[1] = opcode;
                for (i = 0; i < 8; ++i) {
                    out[2 + i] = (unsigned char)(((unsigned long long)imm64 >> (i * 8u)) & 0xffu);
                }
                if (out_len != NULL) {
                    *out_len = 10;
                }
            } else {
                out[0] = opcode;
                for (i = 0; i < 8; ++i) {
                    out[1 + i] = (unsigned char)(((unsigned long long)imm64 >> (i * 8u)) & 0xffu);
                }
                if (out_len != NULL) {
                    *out_len = 9;
                }
            }
            return 0;
        }
        if ((src->kind != AS_OPERAND_IMMEDIATE && src->kind != AS_OPERAND_LABEL_REF) ||
            dst->kind != AS_OPERAND_REGISTER ||
            parse_x86_reg(dst->u.reg, &gr) != 0) {
            return -1;
        }
        if (eval_expr_const(src->u.expr, &imm64) != 0) {
            if (expr_has_symbol(src->u.expr)) {
                imm64 = 0;
            } else {
                return -1;
            }
        }
        rex = (unsigned char)(0x48u | (((unsigned)gr >> 3) & 0x1u));
        out[0] = rex;
        out[1] = (unsigned char)(0xb8u + ((unsigned)gr & 7u));
        for (i = 0; i < 8; ++i) {
            out[2 + i] = (unsigned char)(((unsigned long long)imm64 >> (i * 8u)) & 0xffu);
        }
        if (out_len != NULL) {
            *out_len = 10;
        }
        return 0;
    }
    if (strcmp(mnbuf, "mov") == 0 && insn->operand_count == 2 && src != NULL && dst != NULL) {
        unsigned sr;
        as_x86_seg_t seg;
        unsigned seg_field;
        unsigned char rex_byte;
        unsigned char modrm;

        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER &&
            parse_x86_reg(dst->u.reg, &gr) == 0 &&
            (parse_x86_sysreg(src->u.reg, "cr", 15u, &sr) == 0 ||
             parse_x86_sysreg(src->u.reg, "db", 15u, &sr) == 0 ||
             parse_x86_sysreg(src->u.reg, "dr", 15u, &sr) == 0)) {
            if (parse_x86_sysreg(src->u.reg, "cr", 15u, &sr) == 0) {
                return emit_x86_64_0f_sysreg_mov(0x20, sr, (unsigned)gr, out, out_cap, out_len);
            }
            return emit_x86_64_0f_sysreg_mov(0x21, sr, (unsigned)gr, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER &&
            parse_x86_reg(src->u.reg, &gr) == 0 &&
            (parse_x86_sysreg(dst->u.reg, "cr", 15u, &sr) == 0 ||
             parse_x86_sysreg(dst->u.reg, "db", 15u, &sr) == 0 ||
             parse_x86_sysreg(dst->u.reg, "dr", 15u, &sr) == 0)) {
            if (parse_x86_sysreg(dst->u.reg, "cr", 15u, &sr) == 0) {
                return emit_x86_64_0f_sysreg_mov(0x22, sr, (unsigned)gr, out, out_cap, out_len);
            }
            return emit_x86_64_0f_sysreg_mov(0x23, sr, (unsigned)gr, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_seg_reg_text(src->u.reg, &seg) == 0 &&
            seg_reg_field(seg, &seg_field) == 0) {
            if (dst->kind == AS_OPERAND_REGISTER && parse_x86_reg(dst->u.reg, &gr) == 0) {
                rex_byte = (unsigned char)(0x40u | ((((unsigned)gr) & 8u) ? 0x01u : 0u));
                if (rex_byte != 0x40u) {
                    out[0] = rex_byte;
                    out[1] = 0x8c;
                    out[2] = (unsigned char)(0xc0u | ((seg_field & 7u) << 3) | (((unsigned)gr) & 7u));
                    if (out_len != NULL) {
                        *out_len = 3;
                    }
                } else {
                    out[0] = 0x8c;
                    out[1] = (unsigned char)(0xc0u | ((seg_field & 7u) << 3) | (((unsigned)gr) & 7u));
                    if (out_len != NULL) {
                        *out_len = 2;
                    }
                }
                return 0;
            }
            if (dst->kind == AS_OPERAND_MEMORY) {
                return emit_x86_64_1byte_regfield_memop(0x8c, seg_field, &dst->u.mem, out, out_cap, out_len);
            }
            /* `mov %es, sym`: a bare name or number is an address. */
            if (operand_is_bare_address(dst)) {
                as_mem_operand_t at;

                memset(&at, 0, sizeof(at));
                at.scale = 1;
                at.disp = dst->u.expr;
                return emit_x86_64_1byte_regfield_memop(0x8c, seg_field, &at, out, out_cap, out_len);
            }
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_seg_reg_text(dst->u.reg, &seg) == 0 &&
            seg_reg_field(seg, &seg_field) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_x86_reg(src->u.reg, &gr) == 0) {
                rex_byte = (unsigned char)(0x40u | ((((unsigned)gr) & 8u) ? 0x01u : 0u));
                modrm = (unsigned char)(0xc0u | ((seg_field & 7u) << 3) | (((unsigned)gr) & 7u));
                if (rex_byte != 0x40u) {
                    out[0] = rex_byte;
                    out[1] = 0x8e;
                    out[2] = modrm;
                    if (out_len != NULL) {
                        *out_len = 3;
                    }
                } else {
                    out[0] = 0x8e;
                    out[1] = modrm;
                    if (out_len != NULL) {
                        *out_len = 2;
                    }
                }
                return 0;
            }
            if (src->kind == AS_OPERAND_MEMORY) {
                return emit_x86_64_1byte_regfield_memop(0x8e, seg_field, &src->u.mem, out, out_cap, out_len);
            }
            if (operand_is_bare_address(src)) {
                as_mem_operand_t at;

                memset(&at, 0, sizeof(at));
                at.scale = 1;
                at.disp = src->u.expr;
                return emit_x86_64_1byte_regfield_memop(0x8e, seg_field, &at, out, out_cap, out_len);
            }
        }
    }
    if ((strcmp(mnbuf, "push") == 0 || strcmp(mnbuf, "pop") == 0) && insn->operand_count == 1 && a != NULL &&
        a->kind == AS_OPERAND_REGISTER) {
        return emit_x86_seg_pushpop(mnbuf, a, 0, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fldl") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_MEMORY) {
            return -1;
        }
        return emit_x86_64_1byte_regfield_memop(0xdd, 0u, &a->u.mem, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fld") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        if (a->kind == AS_OPERAND_MEMORY) {
            return emit_x86_64_x87_mem_by_size(a, 0xd9, 0u, 0xdd, 0u, 0xdb, 5u, out, out_cap, out_len);
        }
        return -1;
    }
    {
        unsigned char opcode;
        unsigned reg_field;

        if (lookup_x86_64_x87_mem_exact(mnbuf, &opcode, &reg_field) == 0) {
            if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_MEMORY) {
                return -1;
            }
            return emit_x86_64_1byte_regfield_memop(opcode, reg_field, &a->u.mem, out, out_cap, out_len);
        }
    }
    if (strcmp(mnbuf, "fst") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        if (a->kind == AS_OPERAND_MEMORY) {
            return emit_x86_64_x87_mem_by_size(a, 0xd9, 2u, 0xdd, 2u, -1, 0u, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "fstp") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        if (a->kind == AS_OPERAND_MEMORY) {
            return emit_x86_64_x87_mem_by_size(a, 0xd9, 3u, 0xdd, 3u, 0xdb, 7u, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "fstpt") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_MEMORY) {
            return -1;
        }
        return emit_x86_64_1byte_regfield_memop(0xdb, 7u, &a->u.mem, out, out_cap, out_len);
    }
    {
        unsigned char opcode16;
        unsigned reg16;
        unsigned char opcode32;
        unsigned reg32;

        if (lookup_x86_64_x87_mem16_32(mnbuf, &opcode16, &reg16, &opcode32, &reg32) == 0) {
            if (insn->operand_count != 1 || a == NULL) return -1;
            return emit_x86_64_x87_mem16_32(a, opcode16, reg16, opcode32, reg32, out, out_cap, out_len);
        }
    }
    {
        unsigned char opcode16;
        unsigned reg16;
        unsigned char opcode32;
        unsigned reg32;
        unsigned char opcode64;
        unsigned reg64;

        if (lookup_x86_64_x87_mem16_32_64(mnbuf, &opcode16, &reg16, &opcode32, &reg32, &opcode64, &reg64) == 0) {
            if (insn->operand_count != 1 || a == NULL) return -1;
            return emit_x86_64_x87_mem16_32_64(a, opcode16, reg16, opcode32, reg32, opcode64, reg64, out, out_cap,
                                                out_len);
        }
    }
    if (strcmp(mnbuf, "fucompp") == 0) {
        if (insn->operand_count != 0) {
            return -1;
        }
        if (out_cap < 2) {
            return -1;
        }
        out[0] = 0xda;
        out[1] = 0xe9;
        if (out_len != NULL) {
            *out_len = 2;
        }
        return 0;
    }
    if (strcmp(mnbuf, "movdqa") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_x86_64_xmm_move_rm(0x66, 0x6f, 0x6f, 0x7f, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movdqu") == 0) {
        return emit_x86_64_xmm_move_rm(0xf3, 0x6f, 0x6f, 0x7f, src, dst, out, out_cap, out_len);
    }
    {
        unsigned char prefix;
        unsigned char load_opcode2;
        unsigned char store_opcode2;
        int is_partial;

        if (lookup_x86_xmm_move_family(mnbuf, &prefix, &load_opcode2, &store_opcode2, &is_partial) == 0 &&
            !is_partial) {
            return emit_x86_64_xmm_move_rm(prefix, load_opcode2, load_opcode2, store_opcode2,
                                           src, dst, out, out_cap, out_len);
        }
    }
    if (strcmp(mnbuf, "prefetcht1") == 0) {
        if (insn->operand_count != 1 || src == NULL || src->kind != AS_OPERAND_MEMORY) {
            return -1;
        }
        return emit_x86_64_regfield_memop(0x00, 0x18, 2u, 0, &src->u.mem, out, out_cap, out_len);
    }
    {
        unsigned char packed_opcode2;

        if (lookup_i386_packed_rm_opcode(mnbuf, &packed_opcode2) == 0) {
            if (insn->operand_count != 2 || src == NULL || dst == NULL) {
                return -1;
            }
            return emit_x86_64_mmx_srcdst(packed_opcode2, src, dst, out, out_cap, out_len);
        }
    }
    if (strcmp(mnbuf, "cvtsd2ss") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf2, 0x5a, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtss2sd") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf3, 0x5a, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "addsd") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf2, 0x58, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "addss") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf3, 0x58, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "subsd") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf2, 0x5c, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "subss") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf3, 0x5c, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "mulsd") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf2, 0x59, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "mulss") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf3, 0x59, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "divsd") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf2, 0x5e, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "divss") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0xf3, 0x5e, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ucomisd") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0x66, 0x2e, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ucomiss") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0x00, 0x2e, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "comisd") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0x66, 0x2f, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "comiss") == 0) {
        return emit_x86_64_xmm_binary_op(insn, 0x00, 0x2f, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtsi2sd") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_x86_64_gp_to_xmm_cvtsi(0xf2, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtsi2ss") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_x86_64_gp_to_xmm_cvtsi(0xf3, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttsd2si") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_x86_64_xmm_to_gp_cvtt(0xf2, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttss2si") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_x86_64_xmm_to_gp_cvtt(0xf3, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "vpbroadcastd") == 0 || strcmp(mnbuf, "vpbroadcastq") == 0) {
        as_x86_avx2_insn_t avx2;
        unsigned yd;
        unsigned xs;
        char avxerr[128];
        unsigned char opcode;
        unsigned char vex2;
        unsigned char vex3;
        unsigned char modrm;
        if (isa_level < 3u) {
            return -2;
        }
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            src->kind != AS_OPERAND_REGISTER || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(src->u.reg, &xs) != 0 || parse_ymm_reg(dst->u.reg, &yd) != 0) {
            goto fallback_vpbroadcast;
        }
        opcode = (strcmp(mnbuf, "vpbroadcastd") == 0) ? 0x58u : 0x59u;
        vex2 = (unsigned char)((((yd & 8u) ? 0u : 1u) << 7) | (1u << 6) | (((xs & 8u) ? 0u : 1u) << 5) | 0x02u);
        vex3 = 0x7du;
        modrm = (unsigned char)(0xc0u | ((yd & 7u) << 3) | (xs & 7u));
        if (out_cap < 5) {
            return -1;
        }
        out[0] = 0xc4u;
        out[1] = vex2;
        out[2] = vex3;
        out[3] = opcode;
        out[4] = modrm;
        if (out_len != NULL) {
            *out_len = 5;
        }
        return 0;
fallback_vpbroadcast:
        memset(&avx2, 0, sizeof(avx2));
        avx2.mnemonic = mnbuf;
        avx2.op_count = 2;
        avx2.vector_bits = 256;
        if (dst == NULL || src == NULL ||
            convert_operand_x86(dst, mnbuf, &avx2.op1, 1, intel_syntax, avxerr, sizeof(avxerr)) != 0 ||
            convert_operand_x86(src, mnbuf, &avx2.op2, 1, intel_syntax, avxerr, sizeof(avxerr)) != 0) {
            return -1;
        }
        if (as_x86_encode_avx2(&avx2, out, out_cap, out_len, avxerr, sizeof(avxerr)) != 0) {
            return -1;
        }
        return 0;
    }

    return -1;
}

static int convert_operand_x86(const as_operand_t *op, const char *mnemonic, as_x86_operand_t *dst, int is64,
                               int intel_syntax, char *errbuf, size_t errbuf_sz) {
    long long v;

    memset(dst, 0, sizeof(*dst));
    if (op == NULL) {
        dst->kind = AS_X86_OP_NONE;
        return 0;
    }

    switch (op->kind) {
    case AS_OPERAND_REGISTER: {
        /*
         * The register's kind, width and number, from the one table of
         * them.  The encoders this is for take a general register, an
         * MMX one and a vector one of the first sixteen; the others --
         * a segment, control, debug or mask register, and what APX and
         * AVX-512 add to the sixteen -- have emitters of their own, and
         * are no operand of these.
         */
        const as_x86_reg_info_t *r = as_x86_reg_find(op->u.reg);

        if (r != NULL &&
            (r->cls == AS_X86_REGCLASS_MMX ||
             ((r->cls == AS_X86_REGCLASS_GENERAL || r->cls == AS_X86_REGCLASS_XMM ||
               r->cls == AS_X86_REGCLASS_YMM || r->cls == AS_X86_REGCLASS_ZMM) &&
              r->number <= 15)) &&
            as_x86_reg_operand(op->u.reg, dst) == 0) {
            return 0;
        }
        dst->kind = AS_X86_OP_REG;
        snprintf(errbuf, errbuf_sz, "unknown x86 register: %s", op->u.reg != NULL ? op->u.reg : "<null>");
        return -1;
    }
    case AS_OPERAND_COPROCESSOR:
        dst->kind = AS_X86_OP_FPU;
        dst->reg_class = AS_X86_REGCLASS_X87;
        if (parse_st_index(op->u.coproc, &dst->u.fpu) != 0) {
            snprintf(errbuf, errbuf_sz, "unknown x87 register: %s",
                     op->u.coproc != NULL ? op->u.coproc : "<null>");
            return -1;
        }
        dst->size_bits = 80;
        return 0;
    case AS_OPERAND_IMMEDIATE:
        if (is_rel_mnemonic(mnemonic)) {
            dst->kind = AS_X86_OP_REL;
            if (eval_expr_const(op->u.expr, &v) == 0) {
                dst->u.rel = (int32_t)v;
            } else {
                dst->u.rel = 0;
            }
            return 0;
        }
        if (!intel_syntax && op->raw != NULL && op->raw[0] != '$' && op->raw[0] != '#') {
            dst->kind = AS_X86_OP_MEM;
            memset(&dst->u.mem, 0, sizeof(dst->u.mem));
            dst->u.mem.scale = 1;
            dst->u.mem.disp_only = 1;
            dst->u.mem.abs_modrm = !is64;
            dst->u.mem.has_disp = 1;
            if (eval_expr_const(op->u.expr, &v) == 0) {
                dst->u.mem.disp = (int32_t)v;
            } else if (expr_has_symbol(op->u.expr)) {
                dst->u.mem.disp = 0;
            } else {
                snprintf(errbuf, errbuf_sz, "non-constant absolute memory operand in %s",
                         mnemonic != NULL ? mnemonic : "<insn>");
                return -1;
            }
            return 0;
        }
        dst->kind = AS_X86_OP_IMM;
        if (eval_expr_const(op->u.expr, &v) == 0) {
            dst->u.imm = (int64_t)v;
            return 0;
        }
        if (expr_has_symbol(op->u.expr)) {
            dst->u.imm = 0;
            dst->imm_symbolic = 1;
            return 0;
        }
        snprintf(errbuf, errbuf_sz, "non-constant immediate in %s", mnemonic != NULL ? mnemonic : "<insn>");
        return -1;
    case AS_OPERAND_LABEL_REF:
        if (is_rel_mnemonic(mnemonic)) {
            dst->kind = AS_X86_OP_REL;
            if (eval_expr_const(op->u.expr, &v) == 0) {
                dst->u.rel = (int32_t)v;
            } else {
                dst->u.rel = 0;
            }
            return 0;
        }
        dst->kind = AS_X86_OP_MEM;
        memset(&dst->u.mem, 0, sizeof(dst->u.mem));
        dst->u.mem.scale = 1;
        dst->u.mem.disp_only = 1;
        dst->u.mem.abs_modrm = !is64;
        dst->u.mem.has_disp = 1;
        dst->u.mem.disp = 0;
        return 0;
    case AS_OPERAND_MEMORY:
        {
            unsigned base_bits = op->u.mem.base_reg != NULL ? parse_x86_reg_bits(op->u.mem.base_reg) : 0;
            unsigned index_bits = op->u.mem.index_reg != NULL ? x86_index_addr_bits(op->u.mem.index_reg) : 0;
            unsigned addr_bits = infer_x86_mem_addr_bits(&op->u.mem);

            if (base_bits != 0 && index_bits != 0 && base_bits != index_bits) {
                snprintf(errbuf, errbuf_sz, "mixed x86 memory addressing widths are not supported");
                return -1;
            }
            if (addr_bits != 0 && addr_bits != 16 && addr_bits != 32 && addr_bits != 64) {
                snprintf(errbuf, errbuf_sz, "unsupported x86 memory addressing width");
                return -1;
            }
        }
        dst->kind = AS_X86_OP_MEM;
        memset(&dst->u.mem, 0, sizeof(dst->u.mem));
        dst->u.mem.addr_bits = infer_x86_mem_addr_bits(&op->u.mem);
        dst->u.mem.size_bits = (unsigned)(op->u.mem.size_bits > 0 ? op->u.mem.size_bits : 0);
        (void)is64;
        if (op->u.mem.base_reg != NULL) {
            /* (%eip is made %rip behind a prefix before it comes here,
             * in 64-bit code; elsewhere it is no register.) */
            if (streq_ci(op->u.mem.base_reg, "rip")) {
                /* `(%rip)` is x86_64-only addressing. In 32-bit mode it
                 * appears in dead-code from cc-emitted out-of-line
                 * copies of `static __always_inline` 64-bit helpers
                 * (e.g. rip_rel_ptr) that the linker drops. Treat it
                 * as RIP-relative addressing in either mode so we don't
                 * block the build; the encoded form is only meaningful
                 * in 64-bit code, but it never executes in 32-bit. */
                dst->u.mem.rip_relative = 1;
            } else if (parse_x86_reg(op->u.mem.base_reg, &dst->u.mem.base) == 0) {
                dst->u.mem.has_base = 1;
            } else {
                snprintf(errbuf, errbuf_sz, "unknown x86 base register: %s", op->u.mem.base_reg);
                return -1;
            }
        }
        if (op->u.mem.index_reg != NULL) {
            const char *iname = op->u.mem.index_reg;

            while (*iname == '%') {
                ++iname;
            }
            /* The stack pointer is no index: its code there means that
             * there is none, and the address would be without it. */
            if (streq_ci(iname, "rsp") || streq_ci(iname, "esp") || streq_ci(iname, "sp")) {
                snprintf(errbuf, errbuf_sz, "%%%s cannot be an index register", iname);
                return -1;
            }
            if (parse_x86_reg(op->u.mem.index_reg, &dst->u.mem.index) == 0) {
                dst->u.mem.has_index = 1;
            } else {
                snprintf(errbuf, errbuf_sz, "unknown x86 index register: %s", op->u.mem.index_reg);
                return -1;
            }
        }
        dst->u.mem.scale = op->u.mem.scale > 0 ? (unsigned)op->u.mem.scale : 1u;
        if (op->u.mem.disp != NULL) {
            if (eval_expr_const(op->u.mem.disp, &v) == 0) {
                dst->u.mem.has_disp = 1;
                dst->u.mem.disp = (int32_t)v;
            } else if (expr_has_symbol(op->u.mem.disp)) {
                dst->u.mem.has_disp = 1;
                dst->u.mem.force_disp32 = 1;
                dst->u.mem.disp = 0;
            } else {
                snprintf(errbuf, errbuf_sz, "unsupported memory displacement expression");
                return -1;
            }
        }
        /*
         * A displacement written as the number 0 is no displacement:
         * `0(%eax)` is `(%eax)`, one byte shorter, as GNU as has it.
         * And the reverse for the one base that cannot be without: the
         * code of (%ebp) with no displacement means something else, so
         * it has a displacement of zero, of one byte and not of four.
         * (16-bit addressing has its own table, where (%bp,%si) needs
         * none.)
         */
        /*
         * Unless a width was asked for.  {disp32} is four bytes, of zero
         * if nothing was written; {disp8} is one where the value goes in
         * one, and four where it does not, as GNU as has it.
         */
        if ((x86_stmt_pseudo & AS_PSEUDO_DISP32) != 0 && (dst->u.mem.has_base || dst->u.mem.has_index)) {
            dst->u.mem.has_disp = 1;
            dst->u.mem.force_disp32 = 1;
        } else if ((x86_stmt_pseudo & AS_PSEUDO_DISP8) != 0 && dst->u.mem.has_base) {
            dst->u.mem.has_disp = 1;
        } else if (dst->u.mem.has_base && !dst->u.mem.force_disp32) {
            int is_bp =((unsigned)dst->u.mem.base & 7u) == 5u && dst->u.mem.base < AS_X86_REG_AH;

            if (dst->u.mem.addr_bits == 16u && dst->u.mem.has_index) {
                is_bp = 0;
            }
            if (dst->u.mem.has_disp && dst->u.mem.disp == 0 && !is_bp) {
                dst->u.mem.has_disp = 0;
            } else if (!dst->u.mem.has_disp && is_bp && dst->u.mem.addr_bits != 16u) {
                dst->u.mem.has_disp = 1;
                dst->u.mem.disp = 0;
            }
        }
        if (!dst->u.mem.has_base && !dst->u.mem.has_index) {
            dst->u.mem.disp_only = 1;
            dst->u.mem.abs_modrm = !is64 && !dst->u.mem.rip_relative;
            if (!dst->u.mem.has_disp) {
                dst->u.mem.has_disp = 1;
                dst->u.mem.disp = 0;
            }
        }
        dst->size_bits = dst->u.mem.size_bits;
        return 0;
    default:
        snprintf(errbuf, errbuf_sz, "unsupported operand kind for x86 encode");
        return -1;
    }
}

static int append_directive_data(bytebuf_t *buf, const as_directive_t *d) {
    size_t i;
    unsigned width = 0;
    long long v;

    if (d == NULL || d->name == NULL) {
        return 0;
    }
    if (strcmp(d->name, ".byte") == 0) width = 1;
    else if (strcmp(d->name, ".word") == 0 || strcmp(d->name, ".short") == 0 ||
             strcmp(d->name, ".hword") == 0 || strcmp(d->name, ".2byte") == 0) width = 2;
    else if (strcmp(d->name, ".long") == 0 || strcmp(d->name, ".4byte") == 0) width = 4;
    else if (strcmp(d->name, ".quad") == 0 || strcmp(d->name, ".8byte") == 0) width = 8;

    if (width != 0) {
        for (i = 0; i < d->arg_count; ++i) {
            if (parse_int64(d->args[i], &v) == 0 || parse_const_expr_string(d->args[i], &v) == 0) {
                if (bytebuf_append_u64_le(buf, (uint64_t)v, width) != 0) {
                    return -1;
                }
                continue;
            }
            {
                char *sym = NULL;
                int64_t add = 0;
                if (parse_symbol_addend_arg(d->args[i], &sym, &add) == 0 && sym != NULL) {
                    free(sym);
                    if (bytebuf_append_zeros(buf, width) != 0) {
                        return -1;
                    }
                    continue;
                }
                free(sym);
            }
            return -1;
        }
        return 1;
    }

    if (strcmp(d->name, ".zero") == 0 || strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0) {
        long long fill = 0;
        size_t j;
        if (d->arg_count < 1 || (parse_int64(d->args[0], &v) != 0 && parse_const_expr_string(d->args[0], &v) != 0) ||
            v < 0) {
            return -1;
        }
        if ((strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0) &&
            d->arg_count >= 2) {
            if (parse_int64(d->args[1], &fill) != 0) {
                return -1;
            }
            for (j = 0; j < (size_t)v; ++j) {
                if (bytebuf_append_u64_le(buf, (uint64_t)fill, 1) != 0) {
                    return -1;
                }
            }
            return 1;
        }
        if (bytebuf_append_zeros(buf, (size_t)v) != 0) {
            return -1;
        }
        return 1;
    }

    if (strcmp(d->name, ".fill") == 0) {
        long long repeat = 0;
        long long size = 1;
        long long value = 0;

        if (d->arg_count < 1 ||
            (parse_int64(d->args[0], &repeat) != 0 && parse_const_expr_string(d->args[0], &repeat) != 0) ||
            repeat < 0) {
            return -1;
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
        for (i = 0; i < (size_t)repeat; ++i) {
            if (bytebuf_append_u64_le(buf, (uint64_t)value, (unsigned)size) != 0) {
                return -1;
            }
        }
        return 1;
    }

    if (strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 || strcmp(d->name, ".p2align") == 0) {
        size_t align = 1;
        size_t need;
        long long fill = 0;
        if (d->arg_count < 1 ||
            (parse_int64(d->args[0], &v) != 0 && parse_const_expr_string(d->args[0], &v) != 0) ||
            v < 0) {
            return -1;
        }
        if (directive_has_arg(d, 1) &&
            (parse_int64(d->args[1], &fill) != 0 && parse_const_expr_string(d->args[1], &fill) != 0)) {
            return -1;
        }
        if (strcmp(d->name, ".p2align") == 0) {
            if (v >= (long long)(sizeof(size_t) * CHAR_BIT - 1)) {
                return -1;
            }
            align = (size_t)1u << (unsigned)v;
        } else {
            align = (size_t)v;
        }
        if (align == 0 || (align & (align - 1)) != 0) {
            return -1;
        }
        need = (align - (buf->len & (align - 1))) & (align - 1);
        switch (align_exceeds_max_skip(d, need)) {
        case 1: need = 0; break;
        case 0: break;
        default: return -1;
        }
        if (append_align_fill(buf, d, need, (uint64_t)fill) != 0) {
            return -1;
        }
        return 1;
    }

    if (strcmp(d->name, ".org") == 0) {
        if (d->arg_count < 1 || parse_int64(d->args[0], &v) != 0 || v < 0) {
            return -1;
        }
        if ((size_t)v < buf->len) {
            return -1;
        }
        if ((size_t)v > buf->len && bytebuf_append_zeros(buf, (size_t)v - buf->len) != 0) {
            return -1;
        }
        return 1;
    }

    if (strcmp(d->name, ".ascii") == 0 || strcmp(d->name, ".asciz") == 0 || strcmp(d->name, ".string") == 0) {
        int nul = (strcmp(d->name, ".ascii") == 0) ? 0 : 1;
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

    if (strcmp(d->name, ".float") == 0 || strcmp(d->name, ".double") == 0) {
        int is_double = strcmp(d->name, ".double") == 0;
        for (i = 0; i < d->arg_count; ++i) {
            char *end;
            double dv = strtod(d->args[i], &end);
            if (end == d->args[i] || *end != '\0') {
                return -1;
            }
            if (is_double) {
                union {
                    double d;
                    uint64_t u;
                } u64;
                u64.d = dv;
                if (bytebuf_append_u64_le(buf, u64.u, 8) != 0) {
                    return -1;
                }
            } else {
                union {
                    float f;
                    uint32_t u;
                } u32;
                u32.f = (float)dv;
                if (bytebuf_append_u64_le(buf, (uint64_t)u32.u, 4) != 0) {
                    return -1;
                }
            }
        }
        return 1;
    }

    return 0;
}

static const char *section_from_directive(const as_directive_t *d) {
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
    if (strcmp(d->name, ".text") == 0) return ".text";
    if (strcmp(d->name, ".data") == 0) return ".data";
    if (strcmp(d->name, ".rodata") == 0) return ".rodata";
    if (strcmp(d->name, ".bss") == 0) return ".bss";
    if ((strcmp(d->name, ".section") == 0 || strcmp(d->name, ".pushsection") == 0) &&
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

static unsigned infer_avx_vector_bits(const as_instruction_t *in) {
    size_t i;

    if (in == NULL) {
        return 128;
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        unsigned vr;

        if (op->kind == AS_OPERAND_REGISTER && op->u.reg != NULL) {
            if (parse_zmm_reg(op->u.reg, &vr) == 0) {
                return 512;
            }
            if (parse_ymm_reg(op->u.reg, &vr) == 0) {
                return 256;
            }
        } else if (op->kind == AS_OPERAND_MEMORY) {
            if (op->u.mem.size_bits == 512) {
                return 512;
            }
            if (op->u.mem.size_bits == 256) {
                return 256;
            }
        }
    }
    return 128;
}

static int parse_avx_implicit_reg(const char *name, as_x86_reg_t *out) {
    unsigned vr;

    if (name == NULL || out == NULL) {
        return -1;
    }
    if (parse_xmm_reg(name, &vr) == 0 || parse_ymm_reg(name, &vr) == 0) {
        *out = (as_x86_reg_t)vr;
        return 0;
    }
    return -1;
}

static int convert_operand_x86_evex(const as_operand_t *op, const char *mnemonic, as_x86_operand_t *dst, int is64,
                                    int intel_syntax, char *errbuf, size_t errbuf_sz) {
    unsigned kr;

    if (op != NULL && op->kind == AS_OPERAND_REGISTER && parse_k_reg(op->u.reg, &kr) == 0) {
        memset(dst, 0, sizeof(*dst));
        dst->kind = AS_X86_OP_REG;
        dst->u.reg = (as_x86_reg_t)kr;
        return 0;
    }
    return convert_operand_x86(op, mnemonic, dst, is64, intel_syntax, errbuf, errbuf_sz);
}

static int operand_is_stmt_immediate(const as_operand_t *op, int intel_syntax) {
    if (op == NULL) {
        return 0;
    }
    if (op->kind != AS_OPERAND_IMMEDIATE && op->kind != AS_OPERAND_LABEL_REF) {
        return 0;
    }
    if (intel_syntax) {
        return 1;
    }
    return op->raw != NULL && (op->raw[0] == '$' || op->raw[0] == '#');
}

/*
 * VEX instructions that the tables of the AVX encoders have no row for,
 * and that the EVEX encoders do: with those tried second, these alone
 * still came out as AVX-512 where GNU as writes AVX -- or, for the
 * AVX-VNNI-INT16, SM3 and SM4 ones, where there is no EVEX form to
 * write.  Three shapes:
 *
 *   two operands     dst <- src                  vvvv unused
 *   shift by a byte  dst <- src shifted          ModRM.reg an opcode
 *                                                extension, vvvv the dst
 *   three operands   dst <- src1 op src2
 *
 * Registers %xmm0-15 and %ymm0-15, and memory where the VEX form takes
 * it.  Returns 0 with the bytes, or nonzero with none where the
 * statement is not one of these: the EVEX encoders are tried next, and
 * `vpsllq $3, (%eax), %xmm1`, which only they can say, is theirs.
 */
static int try_encode_x86_vex_extra_stmt(const as_instruction_t *in, int intel_syntax, int is64,
                                         unsigned char *code, size_t code_cap, size_t *code_len,
                                         char *encerr, size_t encerr_sz) {
    enum { TWO, TWO_NARROW, SHIFT, THREE, THREE_XMM };
    static const struct {
        const char *mnemonic;
        int shape;
        as_vex_pp_t pp;
        as_vex_map_t map;
        uint8_t opcode;
        uint8_t ext;
    } rows[] = {
        {"vsqrtps", TWO, AS_VEX_PP_NONE, AS_VEX_MAP_0F, 0x51, 0},
        {"vsqrtpd", TWO, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x51, 0},
        {"vcvtdq2ps", TWO, AS_VEX_PP_NONE, AS_VEX_MAP_0F, 0x5b, 0},
        {"vcvtps2dq", TWO, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x5b, 0},
        {"vcvtpd2dq", TWO_NARROW, AS_VEX_PP_F2, AS_VEX_MAP_0F, 0xe6, 0},
        {"vcvtpd2dqx", TWO_NARROW, AS_VEX_PP_F2, AS_VEX_MAP_0F, 0xe6, 0},
        {"vcvtpd2dqy", TWO_NARROW, AS_VEX_PP_F2, AS_VEX_MAP_0F, 0xe6, 0},
        {"vpsrlw", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x71, 2},
        {"vpsraw", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x71, 4},
        {"vpsllw", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x71, 6},
        {"vpsrld", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x72, 2},
        {"vpsrad", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x72, 4},
        {"vpslld", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x72, 6},
        {"vpsrlq", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x73, 2},
        {"vpsrldq", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x73, 3},
        {"vpsllq", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x73, 6},
        {"vpslldq", SHIFT, AS_VEX_PP_66, AS_VEX_MAP_0F, 0x73, 7},
        {"vpdpwsud", THREE, AS_VEX_PP_F3, AS_VEX_MAP_0F38, 0xd2, 0},
        {"vpdpwsuds", THREE, AS_VEX_PP_F3, AS_VEX_MAP_0F38, 0xd3, 0},
        {"vpdpwusd", THREE, AS_VEX_PP_66, AS_VEX_MAP_0F38, 0xd2, 0},
        {"vpdpwusds", THREE, AS_VEX_PP_66, AS_VEX_MAP_0F38, 0xd3, 0},
        {"vpdpwuud", THREE, AS_VEX_PP_NONE, AS_VEX_MAP_0F38, 0xd2, 0},
        {"vpdpwuuds", THREE, AS_VEX_PP_NONE, AS_VEX_MAP_0F38, 0xd3, 0},
        {"vsm3msg1", THREE_XMM, AS_VEX_PP_NONE, AS_VEX_MAP_0F38, 0xda, 0},
        {"vsm3msg2", THREE_XMM, AS_VEX_PP_66, AS_VEX_MAP_0F38, 0xda, 0},
        {"vsm4key4", THREE, AS_VEX_PP_F3, AS_VEX_MAP_0F38, 0xda, 0},
        {"vsm4rnds4", THREE, AS_VEX_PP_F2, AS_VEX_MAP_0F38, 0xda, 0},
    };
    as_x86_vex_insn_t vex;
    as_x86_operand_t op[3];
    char mnbuf[32];
    char scratch[64];
    long long immv = 0;
    size_t n = in != NULL ? in->operand_count : 0;
    size_t nreg;
    size_t row;
    size_t i;
    size_t len = 0;
    unsigned bits;

    (void)encerr;
    (void)encerr_sz;
    if (code_len != NULL) {
        *code_len = 0;
    }
    if (in == NULL || code == NULL || code_len == NULL || in->mnemonic == NULL || strlen(in->mnemonic) >= sizeof(mnbuf)) {
        return -1;
    }
    for (i = 0; in->mnemonic[i] != '\0'; ++i) {
        mnbuf[i] = (char)tolower((unsigned char)in->mnemonic[i]);
    }
    mnbuf[i] = '\0';
    for (row = 0; row < sizeof(rows) / sizeof(rows[0]); ++row) {
        if (strcmp(mnbuf, rows[row].mnemonic) == 0) {
            break;
        }
    }
    if (row == sizeof(rows) / sizeof(rows[0])) {
        return -1;
    }
    nreg = (rows[row].shape == THREE || rows[row].shape == THREE_XMM) ? 3u : 2u;
    if (n != nreg + (rows[row].shape == SHIFT ? 1u : 0u)) {
        return -1;
    }
    /* The operands in Intel's order: destination first, a count last. */
    for (i = 0; i < nreg; ++i) {
        const as_operand_t *src = &in->operands[intel_syntax ? i : n - 1 - i];

        if (convert_operand_x86(src, mnbuf, &op[i], is64, intel_syntax, scratch, sizeof(scratch)) != 0) {
            return -1;
        }
        if (op[i].kind == AS_X86_OP_REG) {
            if ((op[i].size_bits != 128 && op[i].size_bits != 256) || op[i].u.reg > AS_X86_REG_R15) {
                return -1;
            }
        } else if (op[i].kind != AS_X86_OP_MEM || i != nreg - 1 || rows[row].shape == SHIFT) {
            /* Memory is the last operand only, and the shift's VEX form
             * has none. */
            return -1;
        }
    }
    if (rows[row].shape == SHIFT) {
        const as_operand_t *count = &in->operands[intel_syntax ? n - 1 : 0];

        if ((count->kind != AS_OPERAND_IMMEDIATE && count->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(count->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
    }
    bits = op[0].size_bits;

    memset(&vex, 0, sizeof(vex));
    vex.opcode = rows[row].opcode;
    vex.map = rows[row].map;
    vex.pp = rows[row].pp;
    switch (rows[row].shape) {
    case TWO:
        if (op[1].kind == AS_X86_OP_REG && op[1].size_bits != bits) {
            return -1;
        }
        vex.vex_l = bits == 256;
        vex.dst = op[0].u.reg;
        vex.src2 = op[1];
        break;
    case TWO_NARROW: {
        /* 128 bits out of 128 or of 256: the source says which, and a
         * source in memory says it with the mnemonic's x or y. */
        size_t mlen = strlen(mnbuf);
        char sfx = mnbuf[mlen - 1];
        unsigned sbits = op[1].kind == AS_X86_OP_REG ? op[1].size_bits : 0;

        if (bits != 128) {
            return -1;
        }
        if (sfx == 'x' || sfx == 'y') {
            unsigned want = sfx == 'y' ? 256u : 128u;

            if (sbits != 0 && sbits != want) {
                return -1;
            }
            sbits = want;
        }
        if (sbits == 0) {
            return -1;
        }
        vex.vex_l = sbits == 256;
        vex.dst = op[0].u.reg;
        vex.src2 = op[1];
        break;
    }
    case SHIFT:
        if (op[1].size_bits != bits) {
            return -1;
        }
        vex.vex_l = bits == 256;
        vex.dst = (as_x86_reg_t)rows[row].ext;
        vex.src1 = op[0].u.reg;
        vex.src2 = op[1];
        break;
    default:
        if (op[1].size_bits != bits || (op[2].kind == AS_X86_OP_REG && op[2].size_bits != bits) ||
            (rows[row].shape == THREE_XMM && bits != 128)) {
            return -1;
        }
        vex.vex_l = bits == 256;
        vex.dst = op[0].u.reg;
        vex.src1 = op[1].u.reg;
        vex.src2 = op[2];
        break;
    }
    if (as_x86_encode_vex_3op(&vex, code, code_cap, &len, scratch, sizeof(scratch)) != 0) {
        return -1;
    }
    if (rows[row].shape == SHIFT) {
        if (len >= code_cap) {
            return -1;
        }
        code[len++] = (unsigned char)immv;
    }
    *code_len = len;
    return 0;
}

static int try_encode_x86_avx_stmt(const as_instruction_t *in, int intel_syntax, int is64, unsigned char *code, size_t code_cap,
                                   size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_avx_insn_t avx;
    char mnbuf[32];
    char suffix = '\0';
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    size_t imm_i;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V')) {
        return -1;
    }

    memset(&avx, 0, sizeof(avx));
    avx.mnemonic = in->mnemonic;
    if ((strncmp(in->mnemonic, "vcvtsi2sd", 9) == 0 || strncmp(in->mnemonic, "vcvtsi2ss", 9) == 0 ||
         strncmp(in->mnemonic, "vcvttsd2si", 10) == 0 || strncmp(in->mnemonic, "vcvttss2si", 10) == 0) &&
        normalize_x86_mnemonic(in->mnemonic, mnbuf, sizeof(mnbuf), &suffix) == 0) {
        avx.mnemonic = mnbuf;
    }
    avx.vector_bits = infer_avx_vector_bits(in);
    /* A q, or a 64-bit register for the integer: the conversion is from
     * 64 bits and has VEX.W. */
    if (suffix == 'q' || (avx.mnemonic == mnbuf && suffix != 'l' && x86_stmt_has_gpr_of_width(in, 64))) {
        avx.vex_w = 1;
    }

    if (in->operand_count == 0) {
        avx.op_count = 0;
    } else if (in->operand_count == 4 &&
               (streq_ci(in->mnemonic, "vblendvps") || streq_ci(in->mnemonic, "vblendvpd") ||
                streq_ci(in->mnemonic, "vpblendvb"))) {
        size_t immreg_i = intel_syntax ? 3u : 0u;

        dst_i = intel_syntax ? 0u : 3u;
        src1_i = intel_syntax ? 1u : 2u;
        src2_i = intel_syntax ? 2u : 1u;
        avx.op_count = 4;
        avx.has_imm_reg = 1;
        if (convert_operand_x86(&in->operands[dst_i], avx.mnemonic, &avx.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src1_i], avx.mnemonic, &avx.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src2_i], avx.mnemonic, &avx.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
        if (in->operands[immreg_i].kind != AS_OPERAND_REGISTER ||
            parse_avx_implicit_reg(in->operands[immreg_i].u.reg, &avx.imm_reg) != 0) {
            return -1;
        }
    } else if (in->operand_count == 5 &&
               (streq_ci(in->mnemonic, "vpermil2ps") || streq_ci(in->mnemonic, "vpermil2pd"))) {
        long long immv;
        size_t immreg_i = intel_syntax ? 2u : 1u;

        dst_i = intel_syntax ? 0u : 4u;
        src1_i = intel_syntax ? 1u : 3u;
        src2_i = intel_syntax ? 3u : 2u;
        imm_i = intel_syntax ? 4u : 0u;
        if (intel_syntax && in->operands[2].kind != AS_OPERAND_REGISTER &&
            in->operands[3].kind == AS_OPERAND_REGISTER) {
            src2_i = 2u;
            immreg_i = 3u;
        }
        if (!intel_syntax &&
            in->operands[2].kind == AS_OPERAND_REGISTER &&
            in->operands[3].kind == AS_OPERAND_REGISTER &&
            streq_ci(in->operands[2].u.reg, in->operands[3].u.reg)) {
            src1_i = 2u;
            src2_i = 1u;
            immreg_i = 3u;
            avx.vex_w = 1;
        }
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        avx.imm8 = (uint8_t)immv;
        avx.has_imm8 = 1;
        avx.op_count = 4;
        avx.has_imm_reg = 1;
        if (convert_operand_x86(&in->operands[dst_i], avx.mnemonic, &avx.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src1_i], avx.mnemonic, &avx.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src2_i], avx.mnemonic, &avx.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
        if (in->operands[immreg_i].kind != AS_OPERAND_REGISTER ||
            parse_avx_implicit_reg(in->operands[immreg_i].u.reg, &avx.imm_reg) != 0) {
            return -1;
        }
    } else if (in->operand_count == 2) {
        dst_i = intel_syntax ? 0u : 1u;
        src2_i = intel_syntax ? 1u : 0u;
        avx.op_count = 2;
        if (convert_operand_x86(&in->operands[dst_i], avx.mnemonic, &avx.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src2_i], avx.mnemonic, &avx.op2, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 3) {
        dst_i = intel_syntax ? 0u : 2u;
        src1_i = 1u;
        src2_i = intel_syntax ? 2u : 0u;
        avx.op_count = 3;
        if (convert_operand_x86(&in->operands[dst_i], avx.mnemonic, &avx.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src1_i], avx.mnemonic, &avx.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src2_i], avx.mnemonic, &avx.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 4) {
        long long immv;

        dst_i = intel_syntax ? 0u : 3u;
        src1_i = intel_syntax ? 1u : 2u;
        src2_i = intel_syntax ? 2u : 1u;
        imm_i = intel_syntax ? 3u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        avx.imm8 = (uint8_t)immv;
        avx.has_imm8 = 1;
        avx.op_count = 3;
        if (convert_operand_x86(&in->operands[dst_i], avx.mnemonic, &avx.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src1_i], avx.mnemonic, &avx.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src2_i], avx.mnemonic, &avx.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else {
        return -1;
    }

    return as_x86_encode_avx(&avx, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_avx2_stmt(const as_instruction_t *in, int intel_syntax, int is64, unsigned char *code, size_t code_cap,
                                    size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_avx2_insn_t avx2;
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    size_t imm_i;
    long long immv;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V')) {
        return -1;
    }

    memset(&avx2, 0, sizeof(avx2));
    avx2.mnemonic = in->mnemonic;
    avx2.vector_bits = infer_avx_vector_bits(in);

    if (in->operand_count == 0) {
        avx2.op_count = 0;
    } else if (in->operand_count == 2) {
        dst_i = intel_syntax ? 0u : 1u;
        src2_i = intel_syntax ? 1u : 0u;
        avx2.op_count = 2;
        if (convert_operand_x86(&in->operands[dst_i], in->mnemonic, &avx2.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src2_i], in->mnemonic, &avx2.op2, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 3) {
        dst_i = intel_syntax ? 0u : 2u;
        src1_i = 1u;
        src2_i = intel_syntax ? 2u : 0u;
        avx2.op_count = 3;
        if (convert_operand_x86(&in->operands[dst_i], in->mnemonic, &avx2.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src1_i], in->mnemonic, &avx2.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src2_i], in->mnemonic, &avx2.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 4) {
        dst_i = intel_syntax ? 0u : 3u;
        src1_i = intel_syntax ? 1u : 2u;
        src2_i = intel_syntax ? 2u : 1u;
        imm_i = intel_syntax ? 3u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        avx2.imm8 = (uint8_t)immv;
        avx2.has_imm8 = 1;
        avx2.op_count = 3;
        if (convert_operand_x86(&in->operands[dst_i], in->mnemonic, &avx2.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src1_i], in->mnemonic, &avx2.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[src2_i], in->mnemonic, &avx2.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else {
        return -1;
    }

    return as_x86_encode_avx2(&avx2, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_fma_stmt(const as_instruction_t *in, int intel_syntax, int is64, unsigned char *code, size_t code_cap,
                                   size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_fma_insn_t fma;
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    size_t immreg_i = 0;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V')) {
        return -1;
    }
    if (strncmp(in->mnemonic + 1, "fm", 2) != 0 && strncmp(in->mnemonic + 1, "fnm", 3) != 0) {
        return -1;
    }
    if (in->operand_count != 3 && in->operand_count != 4) {
        return -1;
    }

    memset(&fma, 0, sizeof(fma));
    fma.mnemonic = in->mnemonic;
    fma.vector_bits = infer_avx_vector_bits(in);
    if (in->operand_count == 4) {
        fma.op_count = 4;
        dst_i = intel_syntax ? 0u : 3u;
        src1_i = intel_syntax ? 1u : 2u;
        src2_i = intel_syntax ? 2u : 1u;
        immreg_i = intel_syntax ? 3u : 0u;
        if (intel_syntax &&
            in->operands[2].kind == AS_OPERAND_REGISTER &&
            in->operands[1].kind == AS_OPERAND_REGISTER &&
            streq_ci(in->operands[2].u.reg, in->operands[1].u.reg)) {
            src2_i = 3u;
            immreg_i = 2u;
            fma.vex_w = 1;
        }
        if (!intel_syntax &&
            in->operands[1].kind == AS_OPERAND_REGISTER &&
            in->operands[2].kind == AS_OPERAND_REGISTER &&
            streq_ci(in->operands[1].u.reg, in->operands[2].u.reg)) {
            src1_i = 1u;
            src2_i = 0u;
            immreg_i = 2u;
            fma.vex_w = 1;
        }
    } else {
        fma.op_count = 3;
        dst_i = intel_syntax ? 0u : 2u;
        src1_i = 1u;
        src2_i = intel_syntax ? 2u : 0u;
    }
    if (convert_operand_x86(&in->operands[dst_i], in->mnemonic, &fma.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86(&in->operands[src1_i], in->mnemonic, &fma.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86(&in->operands[src2_i], in->mnemonic, &fma.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
        return -1;
    }
    if (in->operand_count == 4) {
        fma.has_imm_reg = 1;
        if (in->operands[immreg_i].kind != AS_OPERAND_REGISTER ||
            parse_avx_implicit_reg(in->operands[immreg_i].u.reg, &fma.imm_reg) != 0) {
            return -1;
        }
    }

    return as_x86_encode_fma(&fma, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_avx512f_stmt(const as_instruction_t *in, int intel_syntax, unsigned char *code, size_t code_cap,
                                       size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_avx512f_insn_t ev;
    char mnbuf[32];
    char suffix = '\0';
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    size_t imm_i;
    long long immv;
    unsigned kop;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V')) {
        return -1;
    }
    if (in->operand_count != 3 && in->operand_count != 4) {
        return -1;
    }
    memset(&ev, 0, sizeof(ev));
    ev.mnemonic = in->mnemonic;
    if ((strncmp(in->mnemonic, "vcvtusi2sd", 10) == 0 || strncmp(in->mnemonic, "vcvtusi2ss", 10) == 0) &&
        normalize_x86_mnemonic(in->mnemonic, mnbuf, sizeof(mnbuf), &suffix) == 0) {
        ev.mnemonic = mnbuf;
    }
    ev.vector_bits = infer_avx_vector_bits(in);
    ev.op_count = 3;
    ev.rounding_mode = -1;
    ev.evex_w_override = -1;
    if (ev.mnemonic == mnbuf) {
        /* vcvtusi2sd and vcvtusi2ss: EVEX.W is the width of the integer
         * -- a q, or a 64-bit register -- and not the row's.  It was
         * set for every vcvtusi2sd and for no vcvtusi2ss. */
        ev.evex_w_override = (suffix == 'q' || (suffix != 'l' && x86_stmt_has_gpr_of_width(in, 64))) ? 1 : 0;
    } else if (suffix == 'q') {
        ev.evex_w_override = 1;
    }
    dst_i = intel_syntax ? 0u : (in->operand_count == 4 ? 3u : 2u);
    if (in->operands[dst_i].kind != AS_OPERAND_REGISTER || parse_k_reg(in->operands[dst_i].u.reg, &kop) != 0) {
        return -1;
    }
    ev.opmask = (uint8_t)kop;
    if (in->operand_count == 4) {
        src1_i = intel_syntax ? 1u : 2u;
        src2_i = intel_syntax ? 2u : 1u;
        imm_i = intel_syntax ? 3u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        ev.has_imm8 = 1;
        ev.imm8 = (uint8_t)immv;
    } else {
        src1_i = intel_syntax ? 1u : 1u;
        src2_i = intel_syntax ? 2u : 0u;
    }
    if (convert_operand_x86_evex(&in->operands[dst_i], ev.mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86_evex(&in->operands[src1_i], ev.mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86_evex(&in->operands[src2_i], ev.mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
        return -1;
    }
    return as_x86_encode_avx512f(&ev, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_avx512bw_stmt(const as_instruction_t *in, int intel_syntax, unsigned char *code, size_t code_cap,
                                        size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_avx512bw_insn_t ev;
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    unsigned kop;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V') || in->operand_count != 3) {
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.mnemonic = in->mnemonic;
    ev.vector_bits = infer_avx_vector_bits(in);
    ev.op_count = 3;
    ev.rounding_mode = -1;
    dst_i = intel_syntax ? 0u : 2u;
    if (in->operands[dst_i].kind != AS_OPERAND_REGISTER || parse_k_reg(in->operands[dst_i].u.reg, &kop) != 0) {
        return -1;
    }
    ev.opmask = (uint8_t)kop;
    src1_i = intel_syntax ? 1u : 1u;
    src2_i = intel_syntax ? 2u : 0u;
    if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86_evex(&in->operands[src1_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
        return -1;
    }
    return as_x86_encode_avx512bw(&ev, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_avx512dq_stmt(const as_instruction_t *in, int intel_syntax, unsigned char *code, size_t code_cap,
                                        size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_avx512dq_insn_t ev;
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    unsigned kop;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V') || in->operand_count != 3) {
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.mnemonic = in->mnemonic;
    ev.vector_bits = infer_avx_vector_bits(in);
    ev.op_count = 3;
    ev.rounding_mode = -1;
    dst_i = intel_syntax ? 0u : 2u;
    if (in->operands[dst_i].kind != AS_OPERAND_REGISTER || parse_k_reg(in->operands[dst_i].u.reg, &kop) != 0) {
        return -1;
    }
    ev.opmask = (uint8_t)kop;
    src1_i = intel_syntax ? 1u : 1u;
    src2_i = intel_syntax ? 2u : 0u;
    if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86_evex(&in->operands[src1_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
        return -1;
    }
    return as_x86_encode_avx512dq(&ev, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_avx512dq_generic_stmt(const as_instruction_t *in, int intel_syntax, unsigned char *code,
                                                size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_avx512dq_insn_t ev;
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    size_t imm_i;
    long long immv;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V')) {
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.mnemonic = in->mnemonic;
    ev.vector_bits = infer_avx_vector_bits(in);
    ev.opmask = (uint8_t)in->opmask;
    ev.zeroing = in->zeroing;
    ev.broadcast = in->broadcast;
    ev.sae = in->sae;
    ev.rounding_mode = in->rounding_mode;

    if (in->operand_count == 2) {
        dst_i = intel_syntax ? 0u : 1u;
        src2_i = intel_syntax ? 1u : 0u;
        ev.op_count = 2;
        if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 3 &&
               ((intel_syntax && operand_is_stmt_immediate(&in->operands[2], intel_syntax)) ||
                (!intel_syntax && operand_is_stmt_immediate(&in->operands[0], intel_syntax)))) {
        dst_i = intel_syntax ? 0u : 2u;
        src2_i = intel_syntax ? 1u : 1u;
        imm_i = intel_syntax ? 2u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        ev.has_imm8 = 1;
        ev.imm8 = (uint8_t)immv;
        ev.op_count = 2;
        if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 3) {
        dst_i = intel_syntax ? 0u : 2u;
        src1_i = intel_syntax ? 1u : 1u;
        src2_i = intel_syntax ? 2u : 0u;
        ev.op_count = 3;
        if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src1_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 4) {
        dst_i = intel_syntax ? 0u : 3u;
        src1_i = intel_syntax ? 1u : 2u;
        src2_i = intel_syntax ? 2u : 1u;
        imm_i = intel_syntax ? 3u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        ev.has_imm8 = 1;
        ev.imm8 = (uint8_t)immv;
        ev.op_count = 3;
        if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src1_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else {
        return -1;
    }

    return as_x86_encode_avx512dq(&ev, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_avx512bw_generic_stmt(const as_instruction_t *in, int intel_syntax, unsigned char *code,
                                                size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_avx512bw_insn_t ev;
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    size_t imm_i;
    long long immv;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V')) {
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.mnemonic = in->mnemonic;
    ev.vector_bits = infer_avx_vector_bits(in);
    ev.opmask = (uint8_t)in->opmask;
    ev.zeroing = in->zeroing;
    ev.broadcast = in->broadcast;
    ev.sae = in->sae;
    ev.rounding_mode = in->rounding_mode;

    if (in->operand_count == 2) {
        dst_i = intel_syntax ? 0u : 1u;
        src2_i = intel_syntax ? 1u : 0u;
        ev.op_count = 2;
        if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 3 &&
               ((intel_syntax && operand_is_stmt_immediate(&in->operands[2], intel_syntax)) ||
                (!intel_syntax && operand_is_stmt_immediate(&in->operands[0], intel_syntax)))) {
        dst_i = intel_syntax ? 0u : 2u;
        src2_i = intel_syntax ? 1u : 1u;
        imm_i = intel_syntax ? 2u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        ev.has_imm8 = 1;
        ev.imm8 = (uint8_t)immv;
        ev.op_count = 2;
        if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 3) {
        dst_i = intel_syntax ? 0u : 2u;
        src1_i = intel_syntax ? 1u : 1u;
        src2_i = intel_syntax ? 2u : 0u;
        ev.op_count = 3;
        if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src1_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 4) {
        dst_i = intel_syntax ? 0u : 3u;
        src1_i = intel_syntax ? 1u : 2u;
        src2_i = intel_syntax ? 2u : 1u;
        imm_i = intel_syntax ? 3u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        ev.has_imm8 = 1;
        ev.imm8 = (uint8_t)immv;
        ev.op_count = 3;
        if (convert_operand_x86_evex(&in->operands[dst_i], in->mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src1_i], in->mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], in->mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else {
        return -1;
    }

    return as_x86_encode_avx512bw(&ev, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_avx512f_generic_stmt(const as_instruction_t *in, int intel_syntax, unsigned char *code,
                                               size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_avx512f_insn_t ev;
    char mnbuf[32];
    char suffix = '\0';
    size_t dst_i;
    size_t src1_i;
    size_t src2_i;
    size_t imm_i;
    long long immv;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (in->mnemonic[0] != 'v' && in->mnemonic[0] != 'V')) {
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.mnemonic = in->mnemonic;
    if ((strncmp(in->mnemonic, "vcvtusi2sd", 10) == 0 || strncmp(in->mnemonic, "vcvtusi2ss", 10) == 0) &&
        normalize_x86_mnemonic(in->mnemonic, mnbuf, sizeof(mnbuf), &suffix) == 0) {
        ev.mnemonic = mnbuf;
    }
    ev.vector_bits = infer_avx_vector_bits(in);
    ev.opmask = (uint8_t)in->opmask;
    ev.zeroing = in->zeroing;
    ev.broadcast = in->broadcast;
    ev.sae = in->sae;
    ev.rounding_mode = in->rounding_mode;
    ev.evex_w_override = -1;
    if (ev.mnemonic == mnbuf) {
        ev.evex_w_override = (suffix == 'q' || (suffix != 'l' && x86_stmt_has_gpr_of_width(in, 64))) ? 1 : 0;
    } else if (suffix == 'q') {
        ev.evex_w_override = 1;
    }

    if (in->operand_count == 2) {
        dst_i = intel_syntax ? 0u : 1u;
        src2_i = intel_syntax ? 1u : 0u;
        ev.op_count = 2;
        if (convert_operand_x86_evex(&in->operands[dst_i], ev.mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], ev.mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 3 &&
               ((intel_syntax && operand_is_stmt_immediate(&in->operands[2], intel_syntax)) ||
                (!intel_syntax && operand_is_stmt_immediate(&in->operands[0], intel_syntax)))) {
        dst_i = intel_syntax ? 0u : 2u;
        src2_i = intel_syntax ? 1u : 1u;
        imm_i = intel_syntax ? 2u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        ev.has_imm8 = 1;
        ev.imm8 = (uint8_t)immv;
        ev.op_count = 2;
        if (convert_operand_x86_evex(&in->operands[dst_i], ev.mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], ev.mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 3) {
        dst_i = intel_syntax ? 0u : 2u;
        src1_i = intel_syntax ? 1u : 1u;
        src2_i = intel_syntax ? 2u : 0u;
        ev.op_count = 3;
        if (convert_operand_x86_evex(&in->operands[dst_i], ev.mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src1_i], ev.mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], ev.mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else if (in->operand_count == 4) {
        dst_i = intel_syntax ? 0u : 3u;
        src1_i = intel_syntax ? 1u : 2u;
        src2_i = intel_syntax ? 2u : 1u;
        imm_i = intel_syntax ? 3u : 0u;
        if ((in->operands[imm_i].kind != AS_OPERAND_IMMEDIATE && in->operands[imm_i].kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(in->operands[imm_i].u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        ev.has_imm8 = 1;
        ev.imm8 = (uint8_t)immv;
        ev.op_count = 3;
        if (convert_operand_x86_evex(&in->operands[dst_i], ev.mnemonic, &ev.op1, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src1_i], ev.mnemonic, &ev.op2, 0, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86_evex(&in->operands[src2_i], ev.mnemonic, &ev.op3, 0, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else {
        return -1;
    }

    return as_x86_encode_avx512f(&ev, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_bmi1_stmt(const as_instruction_t *in, int intel_syntax, int is64, unsigned char *code, size_t code_cap,
                                    size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_bmi1_insn_t bmi1;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL) {
        return -1;
    }
    /* blsi, blsr, blsmsk: what is read, and where the result goes. */
    if (streq_ci(in->mnemonic, "blsi") || streq_ci(in->mnemonic, "blsr") || streq_ci(in->mnemonic, "blsmsk")) {
        if (in->operand_count != 2) {
            return -1;
        }
        memset(&bmi1, 0, sizeof(bmi1));
        bmi1.mnemonic = in->mnemonic;
        bmi1.op_count = 2;
        if (convert_operand_x86(&in->operands[intel_syntax ? 0u : 1u], in->mnemonic, &bmi1.op1, is64, intel_syntax,
                                encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[intel_syntax ? 1u : 0u], in->mnemonic, &bmi1.op2, is64, intel_syntax,
                                encerr, encerr_sz) != 0) {
            return -1;
        }
        bmi1.width_bits = bmi1.op1.size_bits;
        return as_x86_encode_bmi1(&bmi1, code, code_cap, code_len, encerr, encerr_sz);
    }
    if (!streq_ci(in->mnemonic, "andn") && !streq_ci(in->mnemonic, "bextr")) {
        return -1;
    }

    memset(&bmi1, 0, sizeof(bmi1));
    bmi1.mnemonic = in->mnemonic;
    bmi1.op_count = 3;

    if (streq_ci(in->mnemonic, "andn")) {
        if (in->operand_count != 3) {
            return -1;
        }
        if (convert_operand_x86(&in->operands[intel_syntax ? 0u : 2u], in->mnemonic, &bmi1.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[1], in->mnemonic, &bmi1.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[intel_syntax ? 2u : 0u], in->mnemonic, &bmi1.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else {
        if (in->operand_count != 3) {
            return -1;
        }
        if (convert_operand_x86(&in->operands[intel_syntax ? 0u : 2u], in->mnemonic, &bmi1.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[1], in->mnemonic, &bmi1.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[intel_syntax ? 2u : 0u], in->mnemonic, &bmi1.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    }
    bmi1.width_bits = bmi1.op1.size_bits != 0 ? bmi1.op1.size_bits :
                      (bmi1.op2.kind == AS_X86_OP_MEM ? bmi1.op2.u.mem.size_bits : bmi1.op2.size_bits);

    return as_x86_encode_bmi1(&bmi1, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_bmi2_stmt(const as_instruction_t *in, int intel_syntax, int is64, unsigned char *code, size_t code_cap,
                                    size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_bmi2_insn_t bmi2;

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL) {
        return -1;
    }
    /* rorx: a count, what is rotated, and where the result goes.  The
     * encoder had it and nothing sent it there. */
    if (streq_ci(in->mnemonic, "rorx")) {
        const as_operand_t *count = &in->operands[intel_syntax ? 2u : 0u];
        long long immv = 0;

        if (in->operand_count != 3 || count->kind != AS_OPERAND_IMMEDIATE ||
            eval_expr_const(count->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        memset(&bmi2, 0, sizeof(bmi2));
        bmi2.mnemonic = in->mnemonic;
        bmi2.op_count = 2;
        bmi2.has_imm8 = 1;
        bmi2.imm8 = (uint8_t)immv;
        if (convert_operand_x86(&in->operands[intel_syntax ? 0u : 2u], in->mnemonic, &bmi2.op1, is64, intel_syntax,
                                encerr, encerr_sz) != 0 ||
            convert_operand_x86(&in->operands[1], in->mnemonic, &bmi2.op2, is64, intel_syntax, encerr, encerr_sz) !=
                0) {
            return -1;
        }
        bmi2.width_bits = bmi2.op1.size_bits;
        return as_x86_encode_bmi2(&bmi2, code, code_cap, code_len, encerr, encerr_sz);
    }
    if ((!streq_ci(in->mnemonic, "bzhi") && !streq_ci(in->mnemonic, "mulx") &&
         !streq_ci(in->mnemonic, "pdep") && !streq_ci(in->mnemonic, "pext") &&
         !streq_ci(in->mnemonic, "sarx") && !streq_ci(in->mnemonic, "shlx") &&
         !streq_ci(in->mnemonic, "shrx"))) {
        return -1;
    }

    if (in->operand_count != 3) {
        return -1;
    }

    memset(&bmi2, 0, sizeof(bmi2));
    bmi2.mnemonic = in->mnemonic;
    bmi2.op_count = 3;
    if (convert_operand_x86(&in->operands[intel_syntax ? 0u : 2u], in->mnemonic, &bmi2.op1, is64, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86(&in->operands[1], in->mnemonic, &bmi2.op2, is64, intel_syntax, encerr, encerr_sz) != 0 ||
        convert_operand_x86(&in->operands[intel_syntax ? 2u : 0u], in->mnemonic, &bmi2.op3, is64, intel_syntax, encerr, encerr_sz) != 0) {
        return -1;
    }
    bmi2.width_bits = bmi2.op1.size_bits != 0 ? bmi2.op1.size_bits :
                      (bmi2.op2.kind == AS_X86_OP_MEM ? bmi2.op2.u.mem.size_bits : bmi2.op2.size_bits);
    return as_x86_encode_bmi2(&bmi2, code, code_cap, code_len, encerr, encerr_sz);
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
    if (strcmp(d->name, ".previous") == 0) {
        if (st->previous == NULL) {
            return 1;
        }
        tmp = st->current;
        st->current = st->previous;
        st->previous = tmp;
        return 1;
    }
    if (strcmp(d->name, ".popsection") == 0) {
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
    if (strcmp(d->name, ".code16") == 0) {
        st->x86_code_bits = 16u;
        return 1;
    }
    if (strcmp(d->name, ".code32") == 0) {
        st->x86_code_bits = 32u;
        return 1;
    }
    if (strcmp(d->name, ".code64") == 0) {
        st->x86_code_bits = 64u;
        return 1;
    }
    sec_name = section_from_directive(d);
    if (sec_name == NULL) {
        return 0;
    }
    if (strcmp(d->name, ".pushsection") == 0 && section_track_push(st) != 0) {
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
    if (strcmp(name, ".text") == 0) {
        return ctx->text_sec;
    }
    if (strcmp(name, ".data") == 0) {
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
            if (strcmp(ctx->code_sections[k], track.current) == 0) {
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
        if (strcmp(ctx->code_sections[k], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int try_encode_x86_ssse3(const as_x86_insn_t *in, unsigned char *code, size_t code_cap,
                                size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_ssse3_insn_t ss;

    if (in == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    memset(&ss, 0, sizeof(ss));
    ss.mnemonic = in->mnemonic;
    ss.op_count = in->op_count;
    if (in->op_count >= 1) {
        ss.dst = in->ops[0];
    }
    if (in->op_count >= 2) {
        ss.src = in->ops[1];
    }
    if (in->op_count >= 3 && in->ops[2].kind == AS_X86_OP_IMM) {
        ss.has_imm8 = 1;
        ss.imm8 = (uint8_t)in->ops[2].u.imm;
    }
    return as_x86_encode_ssse3(&ss, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_sse41(const as_x86_insn_t *in, unsigned char *code, size_t code_cap,
                                size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_sse41_insn_t s41;
    int explicit_xmm0_mask_first;
    int explicit_xmm0_mask_last;
    int immediate_3op;

    if (in == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    memset(&s41, 0, sizeof(s41));
    s41.mnemonic = in->mnemonic;
    explicit_xmm0_mask_first =
        (streq_ci(in->mnemonic, "blendvps") || streq_ci(in->mnemonic, "blendvpd") || streq_ci(in->mnemonic, "pblendvb")) &&
        in->op_count == 3 && in->ops[0].kind == AS_X86_OP_REG && in->ops[0].u.reg == 0;
    explicit_xmm0_mask_last =
        (streq_ci(in->mnemonic, "blendvps") || streq_ci(in->mnemonic, "blendvpd") || streq_ci(in->mnemonic, "pblendvb")) &&
        in->op_count == 3 && in->ops[2].kind == AS_X86_OP_REG && in->ops[2].u.reg == 0;
    immediate_3op = in->op_count == 3 &&
                    (in->ops[0].kind == AS_X86_OP_IMM || in->ops[2].kind == AS_X86_OP_IMM);

    if (explicit_xmm0_mask_last) {
        s41.op_count = 2;
        s41.dst = in->ops[0];
        s41.src = in->ops[1];
    } else if (explicit_xmm0_mask_first) {
        s41.op_count = 2;
        s41.dst = in->ops[2];
        s41.src = in->ops[1];
    } else if (immediate_3op) {
        s41.op_count = 2;
        if (in->ops[0].kind == AS_X86_OP_IMM) {
            s41.imm8 = (uint8_t)in->ops[0].u.imm;
            s41.src = in->ops[1];
            s41.dst = in->ops[2];
        } else {
            s41.imm8 = (uint8_t)in->ops[2].u.imm;
            s41.dst = in->ops[0];
            s41.src = in->ops[1];
        }
        s41.has_imm8 = 1;
    } else {
        s41.op_count = in->op_count;
        if (in->op_count >= 1) {
            s41.dst = in->ops[0];
        }
        if (in->op_count >= 2) {
            s41.src = in->ops[1];
        }
    }
    if (!explicit_xmm0_mask_first && !explicit_xmm0_mask_last && !immediate_3op &&
        in->op_count >= 3 && in->ops[2].kind == AS_X86_OP_IMM) {
        s41.has_imm8 = 1;
        s41.imm8 = (uint8_t)in->ops[2].u.imm;
    }
    return as_x86_encode_sse41(&s41, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_sse42(const as_x86_insn_t *in, unsigned char *code, size_t code_cap,
                                size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_sse42_insn_t s42;
    int immediate_3op;

    if (in == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    memset(&s42, 0, sizeof(s42));
    s42.mnemonic = in->mnemonic;
    immediate_3op = in->op_count == 3 &&
                    (in->ops[0].kind == AS_X86_OP_IMM || in->ops[2].kind == AS_X86_OP_IMM);
    if (immediate_3op) {
        s42.op_count = 2;
        if (in->ops[0].kind == AS_X86_OP_IMM) {
            s42.imm8 = (uint8_t)in->ops[0].u.imm;
            s42.src = in->ops[1];
            s42.dst = in->ops[2];
        } else {
            s42.imm8 = (uint8_t)in->ops[2].u.imm;
            s42.dst = in->ops[0];
            s42.src = in->ops[1];
        }
        s42.has_imm8 = 1;
    } else {
        s42.op_count = in->op_count;
        if (in->op_count >= 1) {
            s42.dst = in->ops[0];
        }
        if (in->op_count >= 2) {
            s42.src = in->ops[1];
        }
        if (in->op_count >= 3 && in->ops[2].kind == AS_X86_OP_IMM) {
            s42.has_imm8 = 1;
            s42.imm8 = (uint8_t)in->ops[2].u.imm;
        }
    }
    return as_x86_encode_sse42(&s42, code, code_cap, code_len, encerr, encerr_sz);
}

static int try_encode_x86_sse3(const as_x86_insn_t *in, unsigned char *code, size_t code_cap,
                               size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_sse3_insn_t s3;

    if (in == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    memset(&s3, 0, sizeof(s3));
    s3.mnemonic = in->mnemonic;
    s3.op_count = in->op_count;
    if (in->op_count >= 1) {
        s3.dst = in->ops[0];
    }
    if (in->op_count >= 2) {
        s3.src = in->ops[1];
    }
    if (streq_ci(in->mnemonic, "fisttp")) {
        /* (With an address of 16-bit registers it is the x87 emitter's,
         * which knows `fisttp` with no suffix from `fisttpl`: this has
         * lost the suffix, and would make the first the second.) */
        if (in->op_count >= 1 && in->ops[0].kind == AS_X86_OP_MEM && in->ops[0].u.mem.addr_bits == 16u) {
            return -1;
        }
        if (in->operand_size_override) s3.width_bits = 16;
        else if (in->rex_w) s3.width_bits = 64;
        else s3.width_bits = 32;
    }
    return as_x86_encode_sse3(&s3, code, code_cap, code_len, encerr, encerr_sz);
}

static int instruction_has_symbolic_reloc(const as_instruction_t *in) {
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

static int x86_mem_operand_is_dx_port(const as_x86_operand_t *op) {
    return op != NULL &&
           op->kind == AS_X86_OP_MEM &&
           op->u.mem.has_base &&
           op->u.mem.base == AS_X86_REG_RDX &&
           !op->u.mem.has_index &&
           !op->u.mem.has_disp;
}

static void set_x86_isa_requirement(char *encerr, size_t encerr_sz, const char *mnemonic, unsigned min_level) {
    if (encerr == NULL || encerr_sz == 0) {
        return;
    }
    snprintf(encerr, encerr_sz, "instruction requires -march=x86-64-v%u or higher: %s",
             min_level, mnemonic != NULL ? mnemonic : "<null>");
}

static int x86_stmt_requires_v3(const as_instruction_t *insn, int intel_syntax, int is_64);

static int x86_stmt_requires_v4(const as_instruction_t *insn, int intel_syntax) {
    unsigned char probe[32];
    size_t probe_len = 0;
    char probe_err[128];

    if (insn == NULL) {
        return 0;
    }
    if (x86_stmt_requires_v3(insn, intel_syntax, 1)) {
        return 0;
    }
    return try_encode_x86_avx512f_stmt(insn, intel_syntax, probe, sizeof(probe), &probe_len, probe_err, sizeof(probe_err)) == 0 ||
           try_encode_x86_avx512bw_stmt(insn, intel_syntax, probe, sizeof(probe), &probe_len, probe_err, sizeof(probe_err)) == 0 ||
           try_encode_x86_avx512bw_generic_stmt(insn, intel_syntax, probe, sizeof(probe), &probe_len, probe_err,
                                                sizeof(probe_err)) == 0 ||
           try_encode_x86_avx512dq_stmt(insn, intel_syntax, probe, sizeof(probe), &probe_len, probe_err, sizeof(probe_err)) == 0 ||
           try_encode_x86_avx512f_generic_stmt(insn, intel_syntax, probe, sizeof(probe), &probe_len, probe_err,
                                               sizeof(probe_err)) == 0 ||
           try_encode_x86_avx512dq_generic_stmt(insn, intel_syntax, probe, sizeof(probe), &probe_len, probe_err,
                                                sizeof(probe_err)) == 0;
}

static int x86_stmt_requires_v3(const as_instruction_t *insn, int intel_syntax, int is_64) {
    unsigned char probe[32];
    size_t probe_len = 0;
    char probe_err[128];

    if (insn == NULL) {
        return 0;
    }
    return try_encode_x86_avx_stmt(insn, intel_syntax, is_64, probe, sizeof(probe), &probe_len, probe_err, sizeof(probe_err)) == 0 ||
           try_encode_x86_avx2_stmt(insn, intel_syntax, is_64, probe, sizeof(probe), &probe_len, probe_err,
                                    sizeof(probe_err)) == 0 ||
           try_encode_x86_fma_stmt(insn, intel_syntax, is_64, probe, sizeof(probe), &probe_len, probe_err, sizeof(probe_err)) == 0 ||
           try_encode_x86_bmi1_stmt(insn, intel_syntax, is_64, probe, sizeof(probe), &probe_len, probe_err,
                                     sizeof(probe_err)) == 0 ||
           try_encode_x86_bmi2_stmt(insn, intel_syntax, is_64, probe, sizeof(probe), &probe_len, probe_err,
                                     sizeof(probe_err)) == 0;
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
static void warn_stmt(const as_elf_cfg_t *cfg, const as_stmt_t *st, const char *text) {
    as_elf_warnings_t *w = cfg->warnings;

    if (w != NULL) {
        size_t i;

        if (w->probe) {
            w->wanted++;
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

/*
 * The most operands that any form of an instruction takes, for the
 * mnemonics the encoders know by name (those of AVX apart).  It is what
 * GNU as 2.46 takes, found by trying each mnemonic with one to five
 * operands of every kind in 32- and 64-bit code; the forms of APX,
 * which give some two-operand instructions a third, are counted.
 *
 * A statement with more is refused.  Many of the emitters read the
 * operands they expect and never looked for more: `movaps %xmm0, %xmm1,
 * %xmm2` was `movaps %xmm0, %xmm1`, `hlt $1` was hlt, and a statement
 * of four operands was cut to three on its way to the encoders.
 */
static const char *const x86_max_operands[] = {
    /* 0 */
    " aaa aas cbtw cbw cdq cdqe clac clc cld cli cltd cltq clts cmc cpuid cqo cqto cs cwd cwde"
    " cwtd cwtl daa das ds emms enclv endbr32 endbr64 es f2xm1 fabs fchs fclex fcompp fcos fdecstp"
    " femms fincstp finit fld1 fldl2e fldl2t fldlg2 fldln2 fldpi fldz fnclex fninit fnop fpatan"
    " fprem fprem1 fptan frndint fs fscale fsin fsincos fsqrt ftst fucompp fwait fxam fxtract"
    " fyl2x fyl2xp1 getsec gs hlt int1 int3 into invd iret iretq iretw lahf leave lfence mfence"
    " montmul pause popa popal popaw popf pusha pushal pushaw pushf rdpmc rdtsc"
    " rdtscp rex rsm sahf salc serialize sfence ss stac stc std sti swapgs syscall sysenter"
    " sysexit sysexitl sysexitq sysret sysretl sysretq ud2 ud2a udb wait wbinvd wbnoinvd wrmsr"
    " xcryptcbc xcryptcfb xcryptctr xcryptecb xcryptofb xgetbv xsetbv xsha1 xsha256 xstore"
    " xstorerng ",
    /* 1 */
    " aad aam aesencwide128kl aesencwide256kl bswap cldemote clflush clflushopt clrssbsy clwb"
    " cmpxchg16b cmpxchg8b faddl fadds fbld fbstp fcom fcoml fcomp fcompl fcomps fcoms fdivl"
    " fdivrl fdivrs fdivs ffree ffreep fiadd fiaddl fiadds ficom ficoml ficomp ficompl ficomps"
    " ficoms fidiv fidivl fidivr fidivrl fidivrs fidivs fild fildl fildll fildq filds fimul fimull"
    " fimuls fist fistl fistp fistpl fistpll fistpq fistps fists fisttp fisttpl fisttpll fisttpq"
    " fisttps fisub fisubl fisubr fisubrl fisubrs fisubs fld fldcw fldenv fldl flds fldt fmull"
    " fmuls fnsave fnstcw fnstenv fnstsw frstor fsave fst fstcw fstenv fstl fstp fstpl fstps fstpt"
    " fsts fstsw fsubl fsubrl fsubrs fsubs fucom fucomp fxch fxrstor fxrstor64 fxsave fxsave64"
    " hreset imulb incsspd int invlpg ja jae jb jbe jc jcxz je jecxz jg jge jl jle jna jnae jnb"
    " jnbe jnc jne jng jnge jnl jnle jno jnp jns jnz jo jp jpe jpo jrcxz js jz ldmxcsr lgdt lgdtl"
    " lidt lidtl lldt lmsw loop loope loopne loopnz loopz lret lretq lretw ltr mul mulb mull mulq"
    " nop pop prefetch prefetchnta prefetcht0 prefetcht1 prefetcht2 prefetchw prefetchwt1 ptwrite"
    " push rdfsbase rdgsbase rdpid rdrand rdseed ret retf retfq retfw retl retq retw seta setae"
    " setb setbe setc sete setg setge setl setle setna setnae setnb setnbe setnc setne setng"
    " setnge setnl setnle setno setnp setns setnz seto setp setpe setpo sets setz sgdt sgdtl sidt"
    " sidtl sldt smsw stmxcsr str umonitor wrfsbase wrgsbase xabort xbegin xlat"
    " xlatb xrstor xrstor64 xrstors xsave xsave64 xsavec xsavec64 xsaveopt xsaveopt64 xsaves"
    " xsaves64 ",
    /* 2 */
    " aadd aand addpd addps addsd addss addsubpd addsubps aesdec aesdec128kl aesdec256kl"
    " aesdeclast aesenc aesenc128kl aesenc256kl aesenclast aesimc andnpd andnps andpd andps aor"
    " arpl axor blsi blsmsk blsr bound bsf bsr bt btc btr bts call cmp cmps cmpsb cmpsl cmpsq"
    " cmpsw cmpxchg comisd comiss crc32 cvtdq2pd cvtdq2ps cvtpd2dq cvtpd2pi cvtpd2ps cvtpi2pd"
    " cvtpi2ps cvtps2dq cvtps2pd cvtps2pi cvtsd2si cvtsd2ss cvtsi2sd cvtsi2ss cvtss2sd cvtss2si"
    " cvttpd2dq cvttpd2pi cvttps2dq cvttps2pi cvttsd2si cvttss2si dec decb decl div divb divl"
    " divpd divps divq divsd divss encodekey128 encodekey256 enqcmd enqcmds enter fadd faddp"
    " fcmovb fcmovbe fcmove fcmovnb fcmovnbe fcmovne fcmovnu fcmovu fcomi fcomip fdiv fdivp fdivr"
    " fdivrp fmul fmulp fsub fsubp fsubr fsubrp fucomi fucomip gf2p8mulb haddpd haddps hsubpd"
    " hsubps idiv idivb idivl idivq in inc incb incl ins insb insl insw invept invpcid invvpid jmp"
    " kmovw lar lcall lddqu lds lea les lfs lgs ljmp loadiwkey lods lodsb lodsl lodsq lodsw lsl"
    " lss lzcnt maskmovdqu maskmovq maxpd maxps maxsd maxss minpd minps minsd minss mov movabs"
    " movabsq movapd movaps movb movbe movd movddup movdir64b movdiri movdq2q movdqa movdqu"
    " movhlps movhpd movhps movlhps movlpd movlps movmskpd movmskps movntdq movntdqa movnti"
    " movntil movntiq movntpd movntps movntq movntsd movntss movq movq2dq movs movsb movsbl movsbq"
    " movsbw movsd movshdup movsl movsldup movslq movsq movss movsw movswl movswq movsx movsxb"
    " movsxd movsxw movupd movups movzbl movzbq movzbw movzwl movzwq movzx movzxb movzxw mulpd"
    " mulps mulsd mulss neg negb negl not notb notl orpd orps out outs outsb outsl outsw pabsb"
    " pabsd pabsw packssdw packsswb packusdw packuswb paddb paddd paddq paddsb paddsw paddusb"
    " paddusw paddw pand pandn pavgb pavgusb pavgw pcmpeqb pcmpeqd pcmpeqq pcmpeqw pcmpgtb pcmpgtd"
    " pcmpgtq pcmpgtw pf2id pf2iw pfacc pfadd pfcmpeq pfcmpge pfcmpgt pfmax pfmin pfmul pfnacc"
    " pfpnacc pfrcp pfrcpit1 pfrcpit2 pfrsqit1 pfrsqrt pfsub pfsubr phaddd phaddsw phaddw"
    " phminposuw phsubd phsubsw phsubw pi2fd pi2fw pmaddubsw pmaddwd pmaxsb pmaxsd pmaxsw pmaxub"
    " pmaxud pmaxuw pminsb pminsd pminsw pminub pminud pminuw pmovmskb pmovsxbd pmovsxbq pmovsxbw"
    " pmovsxdq pmovsxwd pmovsxwq pmovzxbd pmovzxbq pmovzxbw pmovzxdq pmovzxwd pmovzxwq pmuldq"
    " pmulhrsw pmulhrw pmulhuw pmulhw pmulld pmullw pmuludq popcnt por psadbw pshufb psignb psignd"
    " psignw pslld pslldq psllq psllw psrad psraw psrld psrldq psrlq psrlw psubb psubd psubq"
    " psubsb psubsw psubusb psubusw psubw pswapd ptest punpckhbw punpckhdq punpckhqdq punpckhwd"
    " punpcklbw punpckldq punpcklqdq punpcklwd pxor rcpps rcpss rdmsr rsqrtps rsqrtss scas scasb"
    " scasl scasq scasw sha1msg1 sha1msg2 sha1nexte sha256msg1 sha256msg2 sqrtpd sqrtps sqrtsd"
    " sqrtss stos stosb stosl stosq stosw subpd subps subsd subss test testb testl tzcnt ucomisd"
    " ucomiss ud0 ud1 ud2b unpckhpd unpckhps unpcklpd unpcklps wrssd wrussd xadd xchg xorpd xorps"
    " mwait ",
    /* 3 */
    " adc adcx add adox aeskeygenassist and andn bextr blendpd blendps blendvpd blendvps bzhi"
    " cmova cmovae cmovb cmovbe cmovc cmove cmovg cmovge cmovl cmovle cmovna cmovnae cmovnb"
    " cmovnbe cmovnc cmovne cmovng cmovnge cmovnl cmovnle cmovno cmovnp cmovns cmovnz cmovo cmovp"
    " cmovpe cmovpo cmovs cmovz cmppd cmpps cmpsd cmpss dppd dpps extractps extrq gf2p8affineinvqb"
    " gf2p8affineqb imul imull imulq insertps mpsadbw mulx or palignr pblendvb pblendw pclmulqdq"
    " pcmpestri pcmpestriq pcmpestrm pcmpestrmq pcmpistri pcmpistrm pdep pext pextrb pextrd pextrq"
    " pextrw pinsrb pinsrd pinsrq pinsrw pshufd pshufhw pshuflw pshufw rcl rcr rol ror rorx"
    " roundpd roundps roundsd roundss sal sar sarl sarq sarx sbb sha1rnds4 sha256rnds2 shl shll"
    " shlq shlx shr shrl shrq shrx shufpd shufps sub xor monitor tpause umwait ",
    /* 4 */
    " insertq shld shrd ",
};

/* The entry of x86_max_operands that has the mnemonic, or -1. */
static int x86_max_operand_count(const char *mn) {
    char key[40];
    size_t n = strlen(mn);
    size_t i;

    if (n == 0 || n + 3 > sizeof(key)) {
        return -1;
    }
    key[0] = ' ';
    for (i = 0; i < n; ++i) {
        key[i + 1] = (char)tolower((unsigned char)mn[i]);
    }
    key[n + 1] = ' ';
    key[n + 2] = '\0';
    for (i = 0; i < sizeof(x86_max_operands) / sizeof(x86_max_operands[0]); ++i) {
        if (strstr(x86_max_operands[i], key) != NULL) {
            return (int)i;
        }
    }
    return -1;
}

static int x86_name_in(const char *mn, const char *list) {
    char key[40];
    size_t n = strlen(mn);

    if (n == 0 || n + 3 > sizeof(key)) {
        return 0;
    }
    key[0] = ' ';
    memcpy(key + 1, mn, n);
    key[n + 1] = ' ';
    key[n + 2] = '\0';
    return strstr(list, key) != NULL;
}

/*
 * Whether an immediate written in the statement goes in the field its
 * instruction has for it.  Two kinds of field are checked, the two where
 * a value that does not fit was cut down in silence:
 *
 *   - a byte that is a count, a selector or a mask and nothing else --
 *     a shift's count, int's vector, the byte of the SSE instructions.
 *     0 to 255; for the shuffles, blends and rotates, whose byte GNU as
 *     also takes written as a negative number, -128 to 255.  `shl $256,
 *     %eax` shifted by nothing and `int $256` was `int $0`.
 *   - the four bytes that a 64-bit operation sign-extends.  `addq
 *     $0xffffffff, %rax` added -1, and `pushq $0x80000000` pushed a
 *     negative number.  -2^31 to 2^31 - 1.
 *
 * The lists and the ranges are GNU as 2.46's, found by trial.  0 if it
 * fits or is not one of these; -1, with the message, if it does not.
 */
static int x86_immediate_fits(const as_instruction_t *insn, const char *mn, char suffix, int intel_syntax, int is_64,
                              char *err, size_t err_sz) {
    static const char unsigned_byte[] =
        " aad aam aeskeygenassist bt btc btr bts cmppd cmpps cmpsd cmpss extractps gf2p8affineqb"
        " gf2p8affineinvqb insertps int palignr pcmpestri pcmpestrm pcmpistri pcmpistrm pextrb pextrd"
        " pextrq pextrw pinsrb pinsrd pinsrq pinsrw pslld pslldq psllq psllw psrad psraw psrld psrldq"
        " psrlq psrlw rcl rcr roundpd roundps roundsd roundss sal sar shl shld shr shrd xabort ";
    static const char signed_byte[] =
        " blendpd blendps dppd dpps mpsadbw pblendw pclmulqdq pshufd pshufhw pshuflw pshufw rol ror"
        " rorx sha1rnds4 shufpd shufps ";
    static const char extended[] = " add or adc sbb and sub xor cmp test imul push ";
    const as_operand_t *imm;
    long long v;

    if (insn->operand_count == 0) {
        return 0;
    }
    /* enter's second operand, the nesting level, is such a byte too, and
     * is written second in either syntax. */
    if (strcmp(mn, "enter") == 0 && insn->operand_count == 2 && insn->operands[1].kind == AS_OPERAND_IMMEDIATE &&
        eval_expr_const(insn->operands[1].u.expr, &v) == 0 && (v < 0 || v > 255)) {
        snprintf(err, err_sz, "the nesting level of 'enter' is a byte, 0 to 255, and %lld is not", v);
        return -1;
    }
    /* The immediate is written first in AT&T syntax and last in Intel. */
    imm = &insn->operands[intel_syntax ? insn->operand_count - 1 : 0];
    if (imm->kind != AS_OPERAND_IMMEDIATE || (!intel_syntax && (imm->raw == NULL || imm->raw[0] != '$')) ||
        eval_expr_const(imm->u.expr, &v) != 0) {
        return 0;
    }
    if (x86_name_in(mn, unsigned_byte) || x86_name_in(mn, signed_byte)) {
        long long low = x86_name_in(mn, signed_byte) ? -128 : 0;

        /* A shift of a byte: there GNU as takes the count written as a
         * negative number too, the operand being a byte as it is. */
        if (x86_name_in(mn, " sal sar shl shr rcl rcr ") &&
            (suffix == 'b' || x86_stmt_has_gpr_of_width(insn, 8))) {
            const as_operand_t *dst = &insn->operands[intel_syntax ? 0 : insn->operand_count - 1];

            if (suffix == 'b' || (dst->kind == AS_OPERAND_REGISTER && x86_reg_width_bits(dst->u.reg) == 8)) {
                low = -128;
            }
        }

        /* cmpsd with fewer than three operands is the string one. */
        if (strcmp(mn, "cmpsd") == 0 && insn->operand_count != 3) {
            return 0;
        }
        if (v < low || v > 255) {
            snprintf(err, err_sz, "the immediate of '%s' is a byte, %lld to 255, and %lld is not", mn, low, v);
            return -1;
        }
        return 0;
    }
    if (is_64 && (v < -2147483648LL || v > 2147483647LL)) {
        int wide = suffix == 'q' || x86_stmt_has_gpr_of_width(insn, 64);

        if (strcmp(mn, "push") == 0) {
            wide = suffix != 'w';
        } else if (strcmp(mn, "mov") == 0) {
            /* To a register there is the form with all eight bytes. */
            const as_operand_t *dst = &insn->operands[intel_syntax ? 0 : insn->operand_count - 1];

            wide = wide && dst->kind != AS_OPERAND_REGISTER;
        } else if (!x86_name_in(mn, extended)) {
            wide = 0;
        }
        if (wide) {
            snprintf(err, err_sz,
                     "the immediate of '%s' is four bytes sign-extended to 64 bits, and %lld does not fit", mn, v);
            return -1;
        }
    }
    return 0;
}

/*
 * The conversions from an integer -- cvtsi2sd, cvtsi2ss, and the same
 * with v and with u -- take an l or a q for the integer's width.  One
 * that contradicts the register written, or a q outside 64-bit code,
 * was assembled as the suffix said or as the register did.  1 if so.
 */
/* The name is `base`, or `base` and one of the letters of `suffixes`. */
static int x86_name_is(const char *mn, const char *base, const char *suffixes) {
    size_t n = strlen(base);

    if (strncmp(mn, base, n) != 0) {
        return 0;
    }
    return mn[n] == '\0' || (mn[n + 1] == '\0' && strchr(suffixes, mn[n]) != NULL);
}

/* Whether a register is one that only 64-bit code has. */
static int x86_reg_is_of_64_bit_code(const as_x86_reg_info_t *r) {
    switch (r->cls) {
    case AS_X86_REGCLASS_GENERAL:
        return r->width == 64 || r->number > 7 || (r->flags & AS_X86_REGF_REX8) != 0;
    case AS_X86_REGCLASS_IP:
        return r->width == 64;
    case AS_X86_REGCLASS_XMM:
    case AS_X86_REGCLASS_YMM:
    case AS_X86_REGCLASS_ZMM:
    case AS_X86_REGCLASS_CONTROL:
    case AS_X86_REGCLASS_DEBUG:
        return r->number > 7;
    case AS_X86_REGCLASS_NO_INDEX:
        return r->width == 64;
    default:
        return 0;
    }
}

/*
 * A register that is not of a class and a width the instruction takes
 * where it is written.
 *
 * The encoders took a register by its number and asked no more of it:
 * `mov %al, %ebx` was `mov %eax, %ebx`, `seta %ax` was `seta %al`,
 * `push %al` pushed %eax, `fldl (%al)` loaded through %eax, `movl %eax,
 * (%r8d)` in 32-bit code stored through %eax, and `movq $1, %mm0` put
 * the one in %rax.  GNU as refuses each, and so does this.
 *
 * It is not the table of what every instruction takes, which the
 * encoders have yet to be given; it is the rules that hold for all of
 * them and the instructions that were found assembling what they
 * cannot mean.  1 with the reason in `encerr` if the statement is
 * refused.
 */
static int x86_operand_classes_refused(const as_instruction_t *insn, int intel_syntax, int is_64, char *encerr,
                                       size_t encerr_sz) {
    const as_x86_reg_info_t *reg[4] = {NULL, NULL, NULL, NULL};
    const as_operand_t *src;
    const as_operand_t *dst;
    const as_x86_reg_info_t *rs;
    const as_x86_reg_info_t *rd;
    char mn[24];
    size_t i;
    size_t n;

    if (insn->mnemonic == NULL || insn->operand_count > 4) {
        return 0;
    }
    n = strlen(insn->mnemonic);
    if (n >= sizeof(mn)) {
        return 0;
    }
    for (i = 0; i <= n; ++i) {
        mn[i] = (char)tolower((unsigned char)insn->mnemonic[i]);
    }

    /* Every register, and every register an address is made of. */
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];

        if (op->kind == AS_OPERAND_REGISTER && op->u.reg != NULL) {
            reg[i] = as_x86_reg_find(op->u.reg);
            if (reg[i] != NULL && !is_64 && x86_reg_is_of_64_bit_code(reg[i])) {
                snprintf(encerr, encerr_sz, "'%s' is a register of 64-bit code", op->u.reg);
                return 1;
            }
        } else if (op->kind == AS_OPERAND_MEMORY) {
            const char *names[2];
            int wide = 0;
            int k;
            long long disp = 0;

            names[0] = op->u.mem.base_reg;
            names[1] = op->u.mem.index_reg;
            for (k = 0; k < 2; ++k) {
                const as_x86_reg_info_t *r = names[k] != NULL ? as_x86_reg_find(names[k]) : NULL;

                if (r == NULL) {
                    continue;
                }
                if (!is_64 && x86_reg_is_of_64_bit_code(r)) {
                    snprintf(encerr, encerr_sz, "'%s' is a register of 64-bit code", names[k]);
                    return 1;
                }
                if ((r->cls == AS_X86_REGCLASS_GENERAL && r->width == 8) || r->cls == AS_X86_REGCLASS_SEGMENT ||
                    r->cls == AS_X86_REGCLASS_X87 || r->cls == AS_X86_REGCLASS_MMX ||
                    r->cls == AS_X86_REGCLASS_MASK || r->cls == AS_X86_REGCLASS_CONTROL ||
                    r->cls == AS_X86_REGCLASS_DEBUG || r->cls == AS_X86_REGCLASS_TEST) {
                    snprintf(encerr, encerr_sz, "'%s' is not a register an address is made of", names[k]);
                    return 1;
                }
                /* (`(%dx)` is how a port is written, and no address.) */
                if (is_64 && r->cls == AS_X86_REGCLASS_GENERAL && r->width == 16 &&
                    !(r->number == 2 && (strncmp(mn, "in", 2) == 0 || strncmp(mn, "out", 3) == 0))) {
                    snprintf(encerr, encerr_sz, "'%s' is not a register an address of 64-bit code is made of",
                             names[k]);
                    return 1;
                }
                wide |= r->width == 64 && (r->cls == AS_X86_REGCLASS_GENERAL || r->cls == AS_X86_REGCLASS_IP);
            }
            /* A displacement is of 32 bits and is sign-extended: one
             * that 32 bits do not hold was cut down to them.  (But
             * an address computed into 32 bits is cut down anyway.) */
            if (x86_name_is(mn, "lea", "wlq")) {
                const as_operand_t *to = &insn->operands[intel_syntax ? 0 : insn->operand_count - 1];
                const as_x86_reg_info_t *r =
                    to->kind == AS_OPERAND_REGISTER && to->u.reg != NULL ? as_x86_reg_find(to->u.reg) : NULL;

                if (r != NULL && r->width <= 32) {
                    wide = 0;
                }
            }
            if (wide && op->u.mem.disp != NULL && !expr_has_symbol(op->u.mem.disp) &&
                eval_expr_const(op->u.mem.disp, &disp) == 0 && (disp < INT32_MIN || disp > INT32_MAX)) {
                snprintf(encerr, encerr_sz, "the displacement %lld is not one 32 bits hold", disp);
                return 1;
            }
        }
    }
    /*
     * Names 64-bit code has no instruction for: the descriptor tables'
     * have one size there, the string instructions on ports none of 64
     * bits, and the far branches through memory no form of 64 that GNU
     * as will write.
     */
    if (is_64 &&
        (strcmp(mn, "lgdtl") == 0 || strcmp(mn, "lgdtw") == 0 || strcmp(mn, "lidtl") == 0 || strcmp(mn, "lidtw") == 0 ||
         strcmp(mn, "sgdtl") == 0 || strcmp(mn, "sgdtw") == 0 || strcmp(mn, "sidtl") == 0 || strcmp(mn, "sidtw") == 0 ||
         strcmp(mn, "insq") == 0 || strcmp(mn, "outsq") == 0 || strcmp(mn, "ljmpq") == 0 || strcmp(mn, "lcallq") == 0 ||
         /* (A frame of 32 bits is none of 64-bit code: `enterl $1, $2`
          * was taken, and made the frame `enterq` makes.) */
         strcmp(mn, "enterl") == 0 ||
         (insn->operand_count == 0 &&
          (strcmp(mn, "into") == 0 || strcmp(mn, "salc") == 0 || strcmp(mn, "pushfl") == 0 ||
           strcmp(mn, "popfl") == 0 || strcmp(mn, "leavel") == 0 || strcmp(mn, "retl") == 0)))) {
        snprintf(encerr, encerr_sz, "'%s' is not an instruction of 64-bit code", mn);
        return 1;
    }
    if (insn->operand_count == 0) {
        return 0;
    }
    src = &insn->operands[intel_syntax ? insn->operand_count - 1 : 0];
    dst = &insn->operands[intel_syntax ? 0 : insn->operand_count - 1];
    rs = reg[intel_syntax ? insn->operand_count - 1 : 0];
    rd = reg[intel_syntax ? 0 : insn->operand_count - 1];
    snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);

    /*
     * The instructions of integers take general registers and no
     * others: `inc %xmm0` was `inc %eax`, `mul %mm0` was `mul %eax`.
     * (A move takes a segment, control or debug register too, and has
     * its own rules below.)
     */
    {
        static const char *const integer[] = {
            "add",  "or",   "adc",     "sbb",  "and",  "sub",  "xor",  "cmp",    "test",  "xchg",  "inc",
            "dec",  "neg",  "not",     "mul",  "imul", "div",  "idiv", "shl",    "shr",   "sal",   "sar",
            "rol",  "ror",  "rcl",     "rcr",  "xadd", "lea",  "bsf",  "bsr",    "bt",    "bts",   "btr",
            "btc",  "popcnt", "lzcnt", "tzcnt", "bswap", "cmpxchg",
        };
        /* Of these the two registers are of one width, and of the
         * last seven neither is a byte. */
        static const char *const one_width[] = {
            "xadd", "cmpxchg", "bsf", "bsr", "popcnt", "lzcnt", "tzcnt", "imul",
        };
        int is_integer = x86_name_is(mn, "mov", "bwl");
        int is_one_width = strncmp(mn, "cmov", 4) == 0;
        int no_byte = is_one_width;

        for (i = 0; i < sizeof(integer) / sizeof(integer[0]) && !is_integer; ++i) {
            is_integer = x86_name_is(mn, integer[i], "bwlq");
        }
        for (i = 0; i < sizeof(one_width) / sizeof(one_width[0]) && !is_one_width; ++i) {
            is_one_width = x86_name_is(mn, one_width[i], "bwlq");
            no_byte = is_one_width && i >= 2;
        }
        for (i = 0; i < insn->operand_count && is_integer; ++i) {
            if (reg[i] == NULL || reg[i]->cls == AS_X86_REGCLASS_GENERAL) {
                continue;
            }
            if (mn[0] == 'm' && mn[1] == 'o' &&
                (reg[i]->cls == AS_X86_REGCLASS_SEGMENT || reg[i]->cls == AS_X86_REGCLASS_CONTROL ||
                 reg[i]->cls == AS_X86_REGCLASS_DEBUG || reg[i]->cls == AS_X86_REGCLASS_TEST)) {
                continue;
            }
            return 1;
        }
        if (is_one_width && insn->operand_count == 2 && rs != NULL && rd != NULL &&
            rs->cls == AS_X86_REGCLASS_GENERAL && rd->cls == AS_X86_REGCLASS_GENERAL &&
            (rs->width != rd->width || (no_byte && rs->width == 8))) {
            return 1;
        }
    }
    /* What is of one kind of vector register is of no other, but for
     * the conversions between them. */
    {
        /*
         * The instructions that have a general register and a vector
         * one among their operands: the moves between the two, the
         * conversions of an integer, the extractions and insertions of
         * an element, the masks of signs, and the broadcasts of
         * AVX-512.  They are every one GNU as will assemble with such
         * a pair, of some 12,900 names tried.  In any other instruction
         * a general register where a vector one belongs was taken by
         * its number: `vaddps %eax, %xmm0, %xmm1` was `vaddps %xmm0,
         * %xmm0, %xmm1`, and `pshufb %eax, %xmm0` `pshufb %mm0, %xmm0`.
         */
        static const char *const of_both[] = {
            "cvtsd2si",     "cvtsi2sd",     "cvtsi2ss",    "cvtss2si",     "cvttsd2si",    "cvttss2si",
            "extractps",    "movd",         "movmskpd",    "movmskps",     "movq",         "pextrb",
            "pextrd",       "pextrq",       "pextrw",      "pinsrb",       "pinsrd",       "pinsrq",
            "pinsrw",       "pmovmskb",     "vcvtsd2si",   "vcvtsd2usi",   "vcvtsh2si",    "vcvtsh2usi",
            "vcvtsi2sd",    "vcvtsi2sh",    "vcvtsi2ss",   "vcvtss2si",    "vcvtss2usi",   "vcvttsd2si",
            "vcvttsd2sis",  "vcvttsd2usi",  "vcvttsd2usis", "vcvttsh2si",  "vcvttsh2usi",  "vcvttss2si",
            "vcvttss2sis",  "vcvttss2usi",  "vcvttss2usis", "vcvtusi2sd",  "vcvtusi2sh",   "vcvtusi2ss",
            "vextractps",   "vmovd",        "vmovmskpd",   "vmovmskps",    "vmovq",        "vmovw",
            "vpbroadcastb", "vpbroadcastd", "vpbroadcastq", "vpbroadcastw", "vpextrb",     "vpextrd",
            "vpextrq",      "vpextrw",      "vpinsrb",     "vpinsrd",      "vpinsrq",      "vpinsrw",
            "vpmovmskb",
        };
        int mmx = 0;
        int xmm = 0;
        int wide = 0;
        int general = 0;

        for (i = 0; i < insn->operand_count; ++i) {
            if (reg[i] != NULL) {
                mmx += reg[i]->cls == AS_X86_REGCLASS_MMX;
                xmm += reg[i]->cls == AS_X86_REGCLASS_XMM;
                wide += reg[i]->cls == AS_X86_REGCLASS_YMM || reg[i]->cls == AS_X86_REGCLASS_ZMM;
                general += reg[i]->cls == AS_X86_REGCLASS_GENERAL;
            }
        }
        /*
         * And an instruction whose name begins with v -- of AVX, FMA
         * and AVX-512 -- has a general register for an operand only if
         * it is one of those, or one of the seven below, which are not
         * of vectors at all: `vaddps %eax, %ebx, %ecx` was assembled.
         */
        if (general != 0 && (mmx + xmm + wide != 0 || mn[0] == 'v')) {
            int not_of_vectors = strcmp(mn, "verr") == 0 || strcmp(mn, "verw") == 0 || strcmp(mn, "vmload") == 0 ||
                                 strcmp(mn, "vmread") == 0 || strcmp(mn, "vmrun") == 0 || strcmp(mn, "vmsave") == 0 ||
                                 strcmp(mn, "vmwrite") == 0;
            int found = 0;

            /* (By its name, or its name and the l or q of the integer's
             * width: `cvtsi2sdl`.) */
            for (i = 0; i < sizeof(of_both) / sizeof(of_both[0]) && !found; ++i) {
                found = x86_name_is(mn, of_both[i], "lq");
            }
            if (!found && !not_of_vectors) {
                return 1;
            }
            /* And one of those has a vector register or an address
             * among its operands -- but for movq, which is a move of
             * general registers too. */
            if (found && mmx + xmm + wide == 0 && strcmp(mn, "movq") != 0) {
                int in_memory = 0;

                for (i = 0; i < insn->operand_count; ++i) {
                    in_memory |= insn->operands[i].kind == AS_OPERAND_MEMORY || operand_is_bare_address(&insn->operands[i]);
                }
                if (!in_memory) {
                    return 1;
                }
            }
        }
        /*
         * The instructions of SSE3, SSSE3 and SSE4.1 that are of
         * vectors and nothing else, and the comparisons by name: their
         * encoders took a register's number, so `pshufb %eax, %ebx`
         * was `pshufb %mm0, %mm3`.
         */
        if (general != 0) {
            static const char *const of_vectors[] = {
                "addsubpd", "addsubps", "blendvpd",   "blendvps", "haddpd",    "haddps",    "hsubpd",   "hsubps",
                "lddqu",    "movddup",  "movshdup",   "movsldup", "pabsb",     "pabsd",     "pabsw",    "packusdw",
                "pblendvb", "pcmpeqq",  "pcmpgtq",    "phaddd",   "phaddsw",   "phaddw",    "phminposuw", "phsubd",
                "phsubsw",  "phsubw",   "pmaddubsw",  "pmaxsb",   "pmaxsd",    "pmaxud",    "pmaxuw",   "pminsb",
                "pminsd",   "pminud",   "pminuw",     "pmovsxbd", "pmovsxbq",  "pmovsxbw",  "pmovsxdq", "pmovsxwd",
                "pmovsxwq", "pmovzxbd", "pmovzxbq",   "pmovzxbw", "pmovzxdq",  "pmovzxwd",  "pmovzxwq", "pmuldq",
                "pmulhrsw", "pmulld",   "pshufb",     "psignb",   "psignd",    "psignw",    "ptest",
            };
            size_t len = strlen(mn);

            for (i = 0; i < sizeof(of_vectors) / sizeof(of_vectors[0]); ++i) {
                if (strcmp(mn, of_vectors[i]) == 0) {
                    return 1;
                }
            }
            if (len >= 5 && strncmp(mn, "cmp", 3) == 0 &&
                (strcmp(mn + len - 2, "sd") == 0 || strcmp(mn + len - 2, "ss") == 0 ||
                 strcmp(mn + len - 2, "pd") == 0 || strcmp(mn + len - 2, "ps") == 0)) {
                return 1;
            }
        }
        /* Instructions with one operand in memory, always. */
        if (insn->operand_count == 2 && rs != NULL && rd != NULL) {
            static const char *const one_in_memory[] = {
                "aadd",  "aand",      "aor",     "axor",   "enqcmd", "enqcmds", "invept", "invpcid",
                "invvpid", "movbe",   "movdir64b", "movdiri", "movnti", "wrssd", "wrussd",
            };

            for (i = 0; i < sizeof(one_in_memory) / sizeof(one_in_memory[0]); ++i) {
                if (x86_name_is(mn, one_in_memory[i], "wlq")) {
                    return 1;
                }
            }
        }
        if (mmx != 0 && xmm != 0 && strcmp(mn, "movq2dq") != 0 && strcmp(mn, "movdq2q") != 0 &&
            strncmp(mn, "cvt", 3) != 0) {
            return 1;
        }
        /* movd is between a vector register and a general one or
         * memory; movsd and movss of two operands are of %xmm. */
        if (strcmp(mn, "movd") == 0 && insn->operand_count == 2 && rs != NULL && rd != NULL && mmx + xmm != 1) {
            return 1;
        }
        if ((strcmp(mn, "movsd") == 0 || strcmp(mn, "movss") == 0) && insn->operand_count == 2 &&
            (mmx != 0 || general != 0)) {
            return 1;
        }
        if ((x86_name_is(mn, "cvtsi2sd", "lq") || x86_name_is(mn, "cvtsi2ss", "lq")) && insn->operand_count == 2 &&
            rs != NULL && rs->cls != AS_X86_REGCLASS_GENERAL) {
            return 1;
        }
    }
    if (x86_name_is(mn, "mov", "bwlq") && insn->operand_count == 2) {
        const as_x86_reg_info_t *general = rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL ? rs : rd;
        const as_x86_reg_info_t *other = general == rs ? rd : rs;

        if (rs != NULL && rd != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rd->cls == AS_X86_REGCLASS_GENERAL &&
            rs->width != rd->width) {
            return 1;
        }
        if (general != NULL && general->cls == AS_X86_REGCLASS_GENERAL && other != NULL) {
            if (other->cls == AS_X86_REGCLASS_SEGMENT && general->width == 8) {
                return 1;
            }
            if ((other->cls == AS_X86_REGCLASS_CONTROL || other->cls == AS_X86_REGCLASS_DEBUG ||
                 other->cls == AS_X86_REGCLASS_TEST) &&
                general->width != (is_64 ? 64u : 32u)) {
                return 1;
            }
        }
        /* A segment register is moved to and from memory as a word. */
        if (((rs != NULL && rs->cls == AS_X86_REGCLASS_SEGMENT) || (rd != NULL && rd->cls == AS_X86_REGCLASS_SEGMENT)) &&
            (mn[3] == 'b' || (mn[3] == 'l' && (src->kind == AS_OPERAND_MEMORY || dst->kind == AS_OPERAND_MEMORY)))) {
            return 1;
        }
        /* movq of a number is a move to a general register. */
        if (mn[3] == 'q' && src->kind == AS_OPERAND_IMMEDIATE && !operand_is_bare_address(src) && rd != NULL &&
            (rd->cls == AS_X86_REGCLASS_MMX || rd->cls == AS_X86_REGCLASS_XMM)) {
            return 1;
        }
    }
    if (insn->operand_count == 2 && rs != NULL && rd != NULL && rs->cls == AS_X86_REGCLASS_GENERAL &&
        rd->cls == AS_X86_REGCLASS_GENERAL && rs->width != rd->width &&
        (x86_name_is(mn, "add", "bwlq") || x86_name_is(mn, "or", "bwlq") || x86_name_is(mn, "adc", "bwlq") ||
         x86_name_is(mn, "sbb", "bwlq") || x86_name_is(mn, "and", "bwlq") || x86_name_is(mn, "sub", "bwlq") ||
         x86_name_is(mn, "xor", "bwlq") || x86_name_is(mn, "cmp", "bwlq") || x86_name_is(mn, "test", "bwlq") ||
         x86_name_is(mn, "xchg", "bwlq"))) {
        return 1;
    }
    if (x86_name_is(mn, "push", "bwlq") || x86_name_is(mn, "pop", "bwlq")) {
        char suffix = mn[mn[1] == 'u' ? 4 : 3];

        /* Nothing of one byte is pushed, and in 64-bit code nothing
         * of four. */
        if (suffix == 'b' || (rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 8)) {
            return 1;
        }
        if (is_64 && (suffix == 'l' || (rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 32))) {
            return 1;
        }
    }
    if (strncmp(mn, "set", 3) == 0 && insn->operand_count == 1 && rs != NULL &&
        (rs->cls != AS_X86_REGCLASS_GENERAL || rs->width != 8)) {
        return 1;
    }
    if (strncmp(mn, "cmov", 4) == 0 && insn->operand_count == 2 &&
        ((rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 8) ||
         (rd != NULL && rd->cls == AS_X86_REGCLASS_GENERAL && rd->width == 8))) {
        return 1;
    }
    if (strncmp(mn, "bswap", 5) == 0 && rs != NULL && rs->width != 32 && rs->width != 64) {
        return 1;
    }
    if (strcmp(mn, "leab") == 0 || strcmp(mn, "btb") == 0 || strcmp(mn, "btsb") == 0 || strcmp(mn, "btrb") == 0 ||
        strcmp(mn, "btcb") == 0) {
        return 1;
    }
    if ((x86_name_is(mn, "lea", "wlq") || strncmp(mn, "movsx", 5) == 0 || strncmp(mn, "movzx", 5) == 0) &&
        insn->operand_count == 2 && rd != NULL && rd->cls == AS_X86_REGCLASS_GENERAL && rd->width == 8) {
        return 1;
    }
    /* What is in memory and nowhere else. */
    if (insn->operand_count == 1 && rs != NULL &&
        (strcmp(mn, "lgdt") == 0 || strcmp(mn, "lidt") == 0 || strcmp(mn, "sgdt") == 0 || strcmp(mn, "sidt") == 0 ||
         strcmp(mn, "invlpg") == 0 || strcmp(mn, "cmpxchg8b") == 0 || strcmp(mn, "cmpxchg16b") == 0)) {
        return 1;
    }
    if (insn->operand_count == 1 && rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL &&
        ((strcmp(mn, "lmsw") == 0 && rs->width != 16) ||
         ((strcmp(mn, "fnstsw") == 0 || strcmp(mn, "fstsw") == 0) && (rs->width != 16 || rs->number != 0)))) {
        return 1;
    }
    if (!is_64 && (strcmp(mn, "rdfsbase") == 0 || strcmp(mn, "rdgsbase") == 0 || strcmp(mn, "wrfsbase") == 0 ||
                   strcmp(mn, "wrgsbase") == 0)) {
        snprintf(encerr, encerr_sz, "'%s' is an instruction of 64-bit code", mn);
        return 1;
    }
    /* A branch through a register goes through one as wide as an
     * address, and a branch goes to no number with a `$` on it. */
    if (mnemonic_takes_a_target(mn) && insn->operand_count == 1) {
        const char *raw = src->raw != NULL ? src->raw : "";

        if (raw[0] == '$' || (raw[0] == '*' && raw[1] == '$')) {
            return 1;
        }
        if (rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && (rs->width == 8 || (is_64 && rs->width == 32))) {
            return 1;
        }
        if (is_64 && raw[0] == '*' && (strcmp(mn, "jmpl") == 0 || strcmp(mn, "calll") == 0)) {
            return 1;
        }
    }
    /* A conditional move of the stack is to its top, and says so. */
    if (strncmp(mn, "fcmov", 5) == 0 && insn->operand_count == 1) {
        return 1;
    }
    /* A count in a register is in %cl. */
    if (insn->operand_count == 2 && rs != NULL &&
        (x86_name_is(mn, "shl", "bwlq") || x86_name_is(mn, "shr", "bwlq") || x86_name_is(mn, "sal", "bwlq") ||
         x86_name_is(mn, "sar", "bwlq") || x86_name_is(mn, "rol", "bwlq") || x86_name_is(mn, "ror", "bwlq") ||
         x86_name_is(mn, "rcl", "bwlq") || x86_name_is(mn, "rcr", "bwlq")) &&
        !(rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 8 && rs->number == 1 &&
          (rs->flags & (AS_X86_REGF_HIGH8 | AS_X86_REGF_REX8)) == 0)) {
        return 1;
    }
    /* A port is read into the accumulator and written from it, in the
     * width the name says where it says one. */
    if ((x86_name_is(mn, "in", "bwl") || x86_name_is(mn, "out", "bwl")) && insn->operand_count == 2) {
        const as_x86_reg_info_t *acc = mn[0] == 'i' ? rd : rs;
        char suffix = mn[mn[0] == 'i' ? 2 : 3];

        if (acc == NULL || acc->cls != AS_X86_REGCLASS_GENERAL || acc->number != 0 || acc->width > 32 ||
            (acc->flags & AS_X86_REGF_HIGH8) != 0 ||
            (suffix != '\0' && acc->width != (suffix == 'b' ? 8u : (suffix == 'w' ? 16u : 32u)))) {
            return 1;
        }
    }
    encerr[0] = '\0';
    return 0;
}

static int x86_int_convert_suffix_contradicts(const as_instruction_t *insn, int is_64) {
    const char *mn = insn->mnemonic;
    size_t n;
    char last;

    if (mn == NULL) {
        return 0;
    }
    if (mn[0] == 'v' || mn[0] == 'V') {
        mn++;
    }
    n = strlen(mn);
    if (!((n == 9 && (strncasecmp(mn, "cvtsi2sd", 8) == 0 || strncasecmp(mn, "cvtsi2ss", 8) == 0)) ||
          (n == 10 && (strncasecmp(mn, "cvtusi2sd", 9) == 0 || strncasecmp(mn, "cvtusi2ss", 9) == 0)))) {
        return 0;
    }
    last = mn[n - 1];
    if (last == 'q' || last == 'Q') {
        return !is_64 || x86_stmt_has_gpr_of_width(insn, 32);
    }
    if (last == 'l' || last == 'L') {
        return x86_stmt_has_gpr_of_width(insn, 64);
    }
    return 0;
}

/*
 * Whether lock may stand before this instruction.  The processor takes
 * it on a short list of instructions and only where the destination is
 * in memory, and faults otherwise; GNU as refuses the rest.  They were
 * assembled -- `lock addl %eax, %ebx`, `lock addps` -- or, by some of
 * the emitters, assembled without the prefix: `lock fldl (%eax)`.
 */
static int x86_lock_is_allowed(const as_instruction_t *insn, const char *mn, int intel_syntax) {
    static const char *const names[] = {
        "add", "adc", "and", "btc", "btr", "bts", "cmpxchg", "cmpxchg8b", "cmpxchg16b", "dec",
        "inc", "neg", "not", "or",  "sbb", "sub", "xor",     "xadd",      "xchg",
    };
    const as_operand_t *dst;
    size_t i;
    int known = 0;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(mn, names[i]) == 0) {
            known = 1;
        }
    }
    if (!known || insn->operand_count == 0) {
        return 0;
    }
    /* xchg has no destination: either side. */
    if (strcmp(mn, "xchg") == 0) {
        for (i = 0; i < insn->operand_count; ++i) {
            if (insn->operands[i].kind == AS_OPERAND_MEMORY || operand_is_bare_address(&insn->operands[i])) {
                return 1;
            }
        }
        return 0;
    }
    dst = &insn->operands[intel_syntax ? 0 : insn->operand_count - 1];
    return dst->kind == AS_OPERAND_MEMORY || operand_is_bare_address(dst);
}

/*
 * cmp<predicate><ps|pd|ss|sd>: the predicate's number, 0 to 7, and the
 * instruction it is a spelling of in `base`; -1 if the mnemonic is not
 * one of the thirty-two.
 */
static int x86_sse_compare_alias(const char *mn, char *base, size_t base_sz) {
    static const char *const predicates[] = {"eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord"};
    size_t n = strlen(mn);
    size_t i;

    if (n < 7 || base_sz < 6 || strncasecmp(mn, "cmp", 3) != 0) {
        return -1;
    }
    if (strcasecmp(mn + n - 2, "ps") != 0 && strcasecmp(mn + n - 2, "pd") != 0 &&
        strcasecmp(mn + n - 2, "ss") != 0 && strcasecmp(mn + n - 2, "sd") != 0) {
        return -1;
    }
    for (i = 0; i < sizeof(predicates) / sizeof(predicates[0]); ++i) {
        size_t len = strlen(predicates[i]);

        if (n == 3 + len + 2 && strncasecmp(mn + 3, predicates[i], len) == 0) {
            snprintf(base, base_sz, "cmp%c%c", tolower((unsigned char)mn[n - 2]), tolower((unsigned char)mn[n - 1]));
            return (int)i;
        }
    }
    return -1;
}

static int encode_x86_stmt_body(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                                size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_insn_t in;
    size_t j;
    size_t op_index[3] = {0, 1, 2};
    char mnbuf[32];
    char suffix = '\0';
    int intel_syntax;

    if (cfg == NULL || st == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0) {
        return -1;
    }
    if (st->kind != AS_STMT_INSTRUCTION) {
        return -1;
    }
    *code_len = 0;

    /*
     * The statement's own syntax, which the parser set from the option
     * and then from each .intel_syntax and .att_syntax it met: not the
     * option again, or -msyntax=intel would outlast an .att_syntax.
     */
    intel_syntax = (st->u.instr.syntax_intel != 0);
    x86_stmt_pseudo = st->u.instr.pseudo;
    /*
     * The SSE comparisons by name: `cmpltsd %xmm0, %xmm1` is `cmpsd $1,
     * %xmm0, %xmm1`, and so for the eight predicates of each of cmpps,
     * cmppd, cmpss and cmpsd.  The statement is assembled as the one it
     * stands for.
     */
    if (st->u.instr.operand_count == 2 && st->u.instr.mnemonic != NULL) {
        char base[8];
        int predicate = x86_sse_compare_alias(st->u.instr.mnemonic, base, sizeof(base));

        if (predicate >= 0) {
            static char *const text[] = {"$0", "$1", "$2", "$3", "$4", "$5", "$6", "$7"};
            as_expr_t value;
            as_operand_t operands[3];
            as_stmt_t whole = *st;

            memset(&value, 0, sizeof(value));
            value.kind = AS_EXPR_CONST;
            value.value = predicate;
            memset(operands, 0, sizeof(operands));
            operands[intel_syntax ? 2 : 0].kind = AS_OPERAND_IMMEDIATE;
            operands[intel_syntax ? 2 : 0].raw = text[predicate];
            operands[intel_syntax ? 2 : 0].u.expr = &value;
            operands[intel_syntax ? 0 : 1] = st->u.instr.operands[0];
            operands[intel_syntax ? 1 : 2] = st->u.instr.operands[1];
            whole.u.instr.mnemonic = base;
            whole.u.instr.operands = operands;
            whole.u.instr.operand_count = 3;
            return encode_x86_stmt_body(ctx, cfg, &whole, code, code_cap, code_len, encerr, encerr_sz);
        }
    }
    memset(&in, 0, sizeof(in));
    if (normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        snprintf(encerr, encerr_sz, "unsupported mnemonic length");
        return -1;
    }
    {
        /* By the name as written, which may be an instruction of its
         * own (movsd, cmpsl), and failing that by the name less its
         * suffix. */
        int most = x86_max_operand_count(st->u.instr.mnemonic);

        if (most < 0) {
            most = x86_max_operand_count(mnbuf);
        }
        if (most >= 0 && st->u.instr.operand_count > (size_t)most) {
            snprintf(encerr, encerr_sz, "'%s' takes %d operand%s at most, and has %zu", st->u.instr.mnemonic, most,
                     most == 1 ? "" : "s", st->u.instr.operand_count);
            return -1;
        }
    }
    if (x86_immediate_fits(&st->u.instr, mnbuf, suffix, intel_syntax, cfg->is_64, encerr, encerr_sz) != 0) {
        return -1;
    }
    if (x86_int_convert_suffix_contradicts(&st->u.instr, cfg->is_64)) {
        snprintf(encerr, encerr_sz, "'%s': the suffix is not the width of the integer operand", st->u.instr.mnemonic);
        return -1;
    }
    /* (By the mode of the code, which .code32 in a 64-bit object makes
     * other than the object's.) */
    if (x86_operand_classes_refused(&st->u.instr, intel_syntax,
                                    cfg->is_64 && cfg->x86_code_bits != 32u && cfg->x86_code_bits != 16u, encerr,
                                    encerr_sz)) {
        return -1;
    }
    if ((st->u.instr.prefixes & AS_PREFIX_LOCK) != 0 && !x86_lock_is_allowed(&st->u.instr, mnbuf, intel_syntax)) {
        snprintf(encerr, encerr_sz, "lock is not allowed on '%s' with these operands", mnbuf);
        return -1;
    }
    /*
     * The suffix of movsx and movzx is the width of the source and not
     * of the instruction, whose size is the destination register's:
     * `movsxw %ax, %ebx` extends a word to 32 bits and has no prefix,
     * `movzxb %al, %bx` extends a byte to 16 and has one.  The encoders
     * know the source's width by these names.
     */
    if (!st->u.instr.syntax_intel && (strcmp(mnbuf, "movsx") == 0 || strcmp(mnbuf, "movzx") == 0)) {
        int is_sign = mnbuf[3] == 's';

        if (suffix == 'b') {
            memcpy(mnbuf, is_sign ? "movsxb" : "movzxb", sizeof("movsxb"));
            suffix = '\0';
        } else if (suffix == 'w') {
            memcpy(mnbuf, is_sign ? "movsxw" : "movzxw", sizeof("movsxw"));
            suffix = '\0';
        } else if (suffix == 'l' && is_sign) {
            memcpy(mnbuf, "movsxd", sizeof("movsxd"));
            suffix = '\0';
        }
    }
    in.mnemonic = mnbuf;
    in.default_bits = cfg->is_64 ? 64u : (cfg->x86_code_bits == 16u ? 16u : 32u);
    in.rel_is_disp = (cfg->x86_rel_is_disp != 0);
    in.force_rel32 = 0u;
    in.has_section_offset = (cfg->have_current_text_offset != 0);
    in.section_offset = cfg->current_text_offset;
    in.seg_override = map_seg(st->u.instr.segment_override);
    in.lock_prefix = (st->u.instr.prefixes & AS_PREFIX_LOCK) != 0;
    in.hle_prefix = (st->u.instr.prefixes & AS_PREFIX_HLE) != 0;
    in.explicit_rex = (st->u.instr.prefixes & AS_PREFIX_REX) != 0;
    in.prefer_load = (st->u.instr.pseudo & AS_PSEUDO_LOAD) != 0;
    in.rex_wanted = (st->u.instr.pseudo & AS_PSEUDO_REX) != 0;
    if (in.rex_wanted && !cfg->is_64) {
        snprintf(encerr, encerr_sz, "{rex} is only for 64-bit code");
        return -1;
    }
    in.rex_bits = (uint8_t)(st->u.instr.rex_bits & 0x0f);
    if ((st->u.instr.prefixes & AS_PREFIX_REPNE) != 0) {
        in.rep_prefix = 2;
    } else if ((st->u.instr.prefixes & (AS_PREFIX_REP | AS_PREFIX_REPE)) != 0) {
        in.rep_prefix = 1;
    }
    if ((st->u.instr.prefixes & AS_PREFIX_DATA16) != 0) {
        in.operand_size_override = 1;
    }
    if ((st->u.instr.prefixes & AS_PREFIX_ADDR16) != 0) {
        in.address_size_override = 1;
    }
    in.op_count = st->u.instr.operand_count > 3 ? 3 : st->u.instr.operand_count;
    if (suffix == 'b') {
        in.byte_op = 1;
    } else if (suffix == 'w') {
        in.operand_size_override = (in.default_bits != 16u);
    } else if (suffix == 'l') {
        in.operand_size_override = (in.default_bits == 16u);
    } else if (suffix == 'q' && !streq_ci(mnbuf, "enter") &&
               !(cfg->is_64 &&
                 (streq_ci(mnbuf, "pushf") || streq_ci(mnbuf, "popf") || streq_ci(mnbuf, "leave") ||
                  streq_ci(mnbuf, "call") || streq_ci(mnbuf, "jmp") || streq_ci(mnbuf, "lgdt") ||
                  streq_ci(mnbuf, "lidt") || streq_ci(mnbuf, "sgdt") || streq_ci(mnbuf, "sidt")))) {
        /* (Those are of 64 bits in 64-bit code with no prefix to say
         * so: `pushfq`, `leaveq`, `callq *%rax` had a REX.W that did
         * nothing but take a byte.  In 32-bit code a q is what they
         * are refused for.) */
        in.rex_w = 1;
    }
    /* The instructions on a 16-bit selector in memory are 16 bits with
     * no prefix; a w on one -- `sldtw (%rax)`, `ltrw sym` -- says only
     * what is so. */
    if (in.operand_size_override && strcmp(mnbuf, "mov") != 0 && !x86_special_takes_opsize(&st->u.instr, mnbuf)) {
        in.operand_size_override = 0;
    }

    if (st->u.instr.operand_count == 1 &&
        is_rel_mnemonic(st->u.instr.mnemonic) &&
        !is_fixed_short_rel_mnemonic(st->u.instr.mnemonic) &&
        suffix != 'b') {
        const as_operand_t *relop = &st->u.instr.operands[0];
        const as_expr_t *relexpr = (relop->kind == AS_OPERAND_LABEL_REF ||
                                    relop->kind == AS_OPERAND_IMMEDIATE) ? relop->u.expr : NULL;
        if (is_call_mnemonic(st->u.instr.mnemonic) ||
            relexpr == NULL ||
            (!expr_has_local_ref(relexpr) && !expr_is_local_temp_symbol(relexpr) &&
             !raw_is_numeric_local_ref(relop->raw))) {
            in.force_rel32 = 1u;
        }
    }

    /*
     * AT&T writes the destination last and the encoder wants it first.
     * `enter` has no destination: its operands are the frame's size and
     * then the nesting level in both syntaxes, and stay as written.
     */
    if (!intel_syntax && in.op_count == 2 && !streq_ci(mnbuf, "enter")) {
        op_index[0] = 1;
        op_index[1] = 0;
    } else if (!intel_syntax && in.op_count == 3 &&
               (streq_ci(mnbuf, "imul") ||
                st->u.instr.operands[0].kind == AS_OPERAND_IMMEDIATE ||
                st->u.instr.operands[0].kind == AS_OPERAND_LABEL_REF)) {
        op_index[0] = 2;
        op_index[1] = 1;
        op_index[2] = 0;
    }

    if (intel_syntax && suffix == '\0' && mnemonic_needs_uniform_width(mnbuf) && in.op_count > 0 &&
        !(streq_ci(mnbuf, "mov") &&
          ((in.op_count > 0 && operand_is_x86_seg_reg(&st->u.instr.operands[op_index[0]])) ||
           (in.op_count > 1 && operand_is_x86_seg_reg(&st->u.instr.operands[op_index[1]]))))) {
        int bits = 0;
        if (infer_uniform_operand_width_bits(&st->u.instr, op_index, in.op_count, &bits, encerr, encerr_sz) != 0) {
            return -1;
        }
        if (bits == 0 && has_unsized_memory_and_immediate(&st->u.instr, op_index, in.op_count)) {
            snprintf(encerr, encerr_sz, "ambiguous Intel operand size");
            return -1;
        }
        if (bits == 8) {
            in.byte_op = 1;
        } else if (bits == 16) {
            in.operand_size_override = 1;
        } else if (bits == 64) {
            if (!cfg->is_64) {
                snprintf(encerr, encerr_sz, "unsupported Intel size qualifier: qword in 32-bit mode");
                return -1;
            }
            in.rex_w = 1;
        }
    }
    /*
     * AT&T with no suffix: the registers say what size the instruction
     * is.  Without this `add %ax, %bx` was `add %eax, %ebx`, and
     * `mov %al, %cl` was `mov %eax, %ecx`.
     */
    if (!intel_syntax && suffix == '\0' && !in.operand_size_override && !in.byte_op && !in.rex_w) {
        int bits = att_register_operand_size(&st->u.instr, mnbuf);

        if (bits == 8) {
            in.byte_op = 1;
        } else if (bits == 16 && in.default_bits != 16u) {
            in.operand_size_override = 1;
        } else if (bits == 32 && in.default_bits == 16u) {
            /* And of 32 bits in 16-bit code, where it is they that
             * have the prefix: `xor %eax, %eax` was `xor %ax, %ax`. */
            in.operand_size_override = 1;
        } else if (bits == 64 && cfg->is_64 && !x86_mnemonic_is_64bit_by_default(mnbuf)) {
            /* And of 64 bits where they are: `mov %rsp, %rbp` was
             * `mov %esp, %ebp`. */
            in.rex_w = 1;
        }
    }
    if (intel_syntax && suffix == '\0' && (streq_ci(mnbuf, "in") || streq_ci(mnbuf, "out")) && in.op_count == 2) {
        int bits = 0;
        for (j = 0; j < in.op_count; ++j) {
            const as_operand_t *op = &st->u.instr.operands[op_index[j]];
            int cur = 0;

            if (operand_is_x86_dx_port_reg(op)) {
                continue;
            }
            if (op->kind == AS_OPERAND_REGISTER && op->u.reg != NULL) {
                cur = x86_reg_width_bits(op->u.reg);
            }
            if (cur == 0) {
                continue;
            }
            bits = cur;
            break;
        }
        if (bits == 8) {
            in.byte_op = 1;
        } else if (bits == 16) {
            in.operand_size_override = 1;
        } else if (bits == 64) {
            if (!cfg->is_64) {
                snprintf(encerr, encerr_sz, "unsupported Intel size qualifier: qword in 32-bit mode");
                return -1;
            }
            in.rex_w = 1;
        }
    }
    if (suffix == '\0' &&
        (streq_ci(mnbuf, "movs") || streq_ci(mnbuf, "cmps") || streq_ci(mnbuf, "stos") ||
         streq_ci(mnbuf, "lods") || streq_ci(mnbuf, "scas") || streq_ci(mnbuf, "ins") ||
         streq_ci(mnbuf, "outs"))) {
        int bits = infer_explicit_string_width_bits(&st->u.instr, mnbuf);
        if (bits == 8) {
            in.byte_op = 1;
        } else if (bits == 16) {
            in.operand_size_override = 1;
        } else if (bits == 64) {
            if (!cfg->is_64) {
                snprintf(encerr, encerr_sz, "unsupported explicit string width in 32-bit mode");
                return -1;
            }
            in.rex_w = 1;
        }
    }

    if (streq_ci(mnbuf, "mov") && in.op_count == 2) {
        const as_operand_t *dst_raw = &st->u.instr.operands[op_index[0]];
        const as_operand_t *src_raw = &st->u.instr.operands[op_index[1]];
        long long immv;
        if (dst_raw->kind == AS_OPERAND_REGISTER && src_raw->kind == AS_OPERAND_IMMEDIATE &&
            src_raw->raw != NULL && (src_raw->raw[0] == '$' || src_raw->raw[0] == '#') &&
            is_x86_low8_reg(dst_raw->u.reg) && eval_expr_const(src_raw->u.expr, &immv) == 0 &&
            (immv < -128 || immv > 255)) {
            warn_stmt(cfg, st, "immediate truncated to 8 bits");
        }
    }

    /*
     * The emitters below that are handed the statement know one ModRM
     * table, the 32-bit one.  An address through 16-bit registers in
     * 32-bit code has another, and they wrote `fldl (%bx)` as `fldl
     * (%ebx)`; such a statement goes past them to the encoder that has
     * both tables, or is refused there.
     */
    if (!x86_stmt_is_the_encoders(cfg, &st->u.instr, X86_PSEUDO_FORMS) &&
        emit_x87_forms(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len) == 0 &&
        *code_len > 0) {
        return x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len);
    }
    if (!x86_stmt_is_the_encoders(cfg, &st->u.instr, X86_PSEUDO_FORMS) &&
        emit_simd_movq(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len) == 0 &&
        *code_len > 0) {
        return x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len);
    }
    {
        int rc = emit_x86_padlock_waitpkg(mnbuf, &st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len);

        if (rc == 0) {
            return 0;
        }
        if (rc < 0) {
            snprintf(encerr, encerr_sz, "unsupported operand form for '%s'", mnbuf);
            return -1;
        }
    }
    if (!cfg->is_64 && !x86_stmt_is_the_encoders(cfg, &st->u.instr, X86_PSEUDO_FORMS) &&
        emit_i386_special(&st->u.instr, intel_syntax, code, code_cap, code_len) == 0) {
        if (*code_len > 0) {
            /*
             * Most of that function's branches are not handed the
             * instruction's prefixes and do not write them: `lock
             * cmpxchgl %ebx, (%eax)` came out as 0f b1 18, an exchange
             * that is not atomic.  If lock was written and is not among
             * the prefixes of what was encoded, it goes in front.
             */
            if ((st->u.instr.prefixes & AS_PREFIX_LOCK) != 0 &&
                x86_prepend_lock_if_absent(code, code_cap, code_len) != 0) {
                return -1;
            }
            /*
             * Nor the operand-size prefix: `bsf %ax, %bx`, `btw`,
             * `shld $4, %ax, %bx`, `cmpxchg %ax, (%ebx)` and `popcnt
             * %ax, %bx` were each the 32-bit instruction.  Whatever
             * made the statement a 16-bit one -- a w, or its registers
             * -- has been settled above.
             */
            /* (And in 16-bit code the other way about: there it is the
             * 32-bit statement that has the prefix, and `cmpxchg %eax,
             * (%bx)`, `btl $1, sym` and `bsf (%si), %eax` had none.) */
            if (((in.operand_size_override && x86_special_takes_opsize(&st->u.instr, mnbuf)) ||
                 (cfg->x86_code_bits != 16u &&
                  x86_segment_form_is_16bit(&st->u.instr, mnbuf, suffix, intel_syntax))) &&
                x86_prepend_opsize(code, code_cap, code_len) != 0) {
                return -1;
            }
            return x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len);
        }
    }
    /*
     * VEX before EVEX.  An instruction that both can encode is VEX: the
     * EVEX form of a 128- or 256-bit instruction is AVX-512VL, and a
     * processor with AVX and no AVX-512 faults on it.  The EVEX encoders
     * were tried first, so in 32-bit code, and in 64-bit code at
     * x86-64-v4, `vaddps %ymm0, %ymm1, %ymm2` was the AVX-512 one.
     */
    /* {vex} and {vex3} ask for VEX whatever would be chosen, and {evex}
     * for the other. */
    if ((!cfg->is_64 || cfg->x86_64_isa_level >= 3) &&
        !x86_stmt_is_the_encoders(cfg, &st->u.instr, X86_PSEUDO_FORMS & ~X86_PSEUDO_DISP) &&
        (st->u.instr.pseudo & AS_PSEUDO_EVEX) == 0 &&
        ((st->u.instr.pseudo & (AS_PSEUDO_VEX | AS_PSEUDO_VEX3)) != 0 ||
         !((!cfg->is_64 || cfg->x86_64_isa_level >= 4) &&
           (x86_mnemonic_is_evex_first(mnbuf) || x86_stmt_has_decorator(&st->u.instr))))) {
        if ((try_encode_x86_vex_extra_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_avx_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_avx2_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_fma_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_bmi1_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_bmi2_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0)) {
            if ((st->u.instr.pseudo & AS_PSEUDO_VEX3) != 0 && x86_vex_make_3byte(code, code_cap, code_len) != 0) {
                return -1;
            }
            if (x86_prepend_segment(&st->u.instr, code, code_cap, code_len) != 0) {
                return -1;
            }
            return x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len);
        }
    }
    if ((!cfg->is_64 || cfg->x86_64_isa_level >= 4) &&
        !x86_stmt_is_the_encoders(cfg, &st->u.instr, X86_PSEUDO_FORMS & ~X86_PSEUDO_DISP) &&
        (st->u.instr.pseudo & (AS_PSEUDO_VEX | AS_PSEUDO_VEX3)) == 0) {
        if ((try_encode_x86_avx512f_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_avx512bw_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_avx512bw_generic_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_avx512dq_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_avx512f_generic_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0) ||
            (try_encode_x86_avx512dq_generic_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
             *code_len > 0)) {
            if (x86_prepend_segment(&st->u.instr, code, code_cap, code_len) != 0) {
                return -1;
            }
            return x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len);
        }
    } else if (cfg->is_64 && x86_stmt_requires_v4(&st->u.instr, intel_syntax)) {
        set_x86_isa_requirement(encerr, encerr_sz, mnbuf, 4);
        return -1;
    }
    /* A mask, a broadcast or a way of rounding is of an EVEX prefix,
     * and what is left has none: `addss (%rax){1to8}, %xmm2` and
     * `psllw (%rax), %xmm2{%k1}` were assembled as if nothing were in
     * the braces. */
    if (x86_stmt_has_decorator(&st->u.instr)) {
        snprintf(encerr, encerr_sz, "'%s' takes no mask, broadcast or rounding", mnbuf);
        return -1;
    }
    if (cfg->is_64 && cfg->x86_64_isa_level < 3 && x86_stmt_requires_v3(&st->u.instr, intel_syntax, cfg->is_64)) {
        set_x86_isa_requirement(encerr, encerr_sz, mnbuf, 3);
        return -1;
    }
    if ((st->u.instr.pseudo & (AS_PSEUDO_VEX | AS_PSEUDO_VEX3 | AS_PSEUDO_EVEX)) != 0) {
        snprintf(encerr, encerr_sz, "'%s' cannot be encoded as its pseudo-prefix asks", mnbuf);
        return -1;
    }
    if (cfg->is_64 && !x86_stmt_is_the_encoders(cfg, &st->u.instr, X86_PSEUDO_FORMS)) {
        int s64 = emit_x86_64_special(&st->u.instr, intel_syntax, cfg->x86_64_isa_level, code, code_cap, code_len);
        if (s64 == 0) {
            if (*code_len > 0) {
                /* movabs to or from %ax is a 16-bit move and has the
                 * prefix; its emitter wrote none. */
                if ((x86_segment_form_is_16bit(&st->u.instr, mnbuf, suffix, intel_syntax) ||
                     (strcmp(mnbuf, "movabs") == 0 && x86_stmt_has_gpr_of_width(&st->u.instr, 16))) &&
                    x86_prepend_opsize(code, code_cap, code_len) != 0) {
                    return -1;
                }
                return x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len);
            }
        }
        if (s64 == -2) {
            set_x86_isa_requirement(encerr, encerr_sz, mnbuf, 3);
            return -1;
        }
    }

    for (j = 0; j < in.op_count; ++j) {
        as_operand_t alias_op;
        const as_operand_t *src_op = x86_operand_with_reg_alias(ctx, &st->u.instr.operands[op_index[j]], &alias_op);
        if (convert_operand_x86(src_op, in.mnemonic, &in.ops[j], cfg->is_64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    }
    /* (64-bit code was left out of this, and `movl (%eax), %ecx` there had
     * no 67: it read through all of %rax.) */
    {
        unsigned mem_addr_bits = 0;

        for (j = 0; j < in.op_count; ++j) {
            if (in.ops[j].kind != AS_X86_OP_MEM || in.ops[j].u.mem.addr_bits == 0) {
                continue;
            }
            if ((streq_ci(mnbuf, "in") || streq_ci(mnbuf, "out") ||
                 streq_ci(mnbuf, "ins") || streq_ci(mnbuf, "insb") || streq_ci(mnbuf, "insw") ||
                 streq_ci(mnbuf, "insd") || streq_ci(mnbuf, "insl") ||
                 streq_ci(mnbuf, "outs") || streq_ci(mnbuf, "outsb") || streq_ci(mnbuf, "outsw") ||
                 streq_ci(mnbuf, "outsd") || streq_ci(mnbuf, "outsl")) &&
                x86_mem_operand_is_dx_port(&in.ops[j])) {
                continue;
            }
            if (mem_addr_bits == 0) {
                mem_addr_bits = in.ops[j].u.mem.addr_bits;
                continue;
            }
            if (mem_addr_bits != in.ops[j].u.mem.addr_bits) {
                snprintf(encerr, encerr_sz, "mixed x86 address sizes in one instruction are not supported");
                return -1;
            }
        }
        /*
         * An address is of the mode's width or of the one the 67 prefix
         * selects: 16 or 32 bits in 16- and 32-bit code, 32 or 64 in
         * 64-bit code.  There is no encoding for the others.
         */
        if ((cfg->is_64 && mem_addr_bits == 16u) || (!cfg->is_64 && mem_addr_bits == 64u)) {
            snprintf(encerr, encerr_sz, "a %u-bit address cannot be encoded in %u-bit code", mem_addr_bits,
                     in.default_bits);
            return -1;
        }
        if (mem_addr_bits != 0 && mem_addr_bits != in.default_bits) {
            in.address_size_override = 1;
        }
    }
    if (in.seg_override == AS_X86_SEG_NONE) {
        int string_insn = is_string_instruction(in.mnemonic);

        for (j = 0; j < in.op_count; ++j) {
            const as_operand_t *op = &st->u.instr.operands[op_index[j]];
            if (op->kind == AS_OPERAND_MEMORY && op->u.mem.segment_reg != NULL) {
                /*
                 * A string instruction has two addresses and one prefix,
                 * which is the source's.  The destination is through
                 * %es whatever is written, and its %es: is no prefix;
                 * nor is a %ds: on the source, which is where it reads
                 * anyway.  (The first segment written was taken, and
                 * `movsb %fs:(%esi), %es:(%edi)` got Intel's order's
                 * first, an %es prefix, and read from the wrong place.)
                 */
                if (string_insn) {
                    as_x86_seg_t seg = map_seg(op->u.mem.segment_reg);
                    char family = string_index_register(op->u.mem.base_reg);

                    /* The destination is through %es and nothing can
                     * make it otherwise: another segment written on it
                     * was dropped, and the store went to %es. */
                    if (family == 'd' && seg != AS_X86_SEG_ES) {
                        snprintf(encerr, encerr_sz, "'%s': the destination of a string instruction is through %%es",
                                 mnbuf);
                        return -1;
                    }
                    if (family == 'd' || (family == 's' && seg == AS_X86_SEG_DS)) {
                        continue;
                    }
                }
                in.seg_override = map_seg(op->u.mem.segment_reg);
                break;
            }
        }
    }

    /*
     * The encoders below take three operands.  A statement of more that
     * has come this far is no form they have, and was cut to its first
     * three: `shld $1, %eax, %ebx, %ecx` was assembled without the last.
     */
    if (st->u.instr.operand_count > 3) {
        snprintf(encerr, encerr_sz, "unsupported operand form for '%s': %zu operands", mnbuf,
                 st->u.instr.operand_count);
        return -1;
    }
    /*
     * crc32's suffix is the width of its source, which the encoder reads
     * from the operand: the destination is 32 bits, or 64, whatever the
     * source is.
     */
    if (suffix != '\0' && in.op_count == 2 && streq_ci(mnbuf, "crc32")) {
        unsigned bits = 64u;

        if (suffix == 'b') {
            bits = 8u;
        } else if (suffix == 'w') {
            bits = 16u;
        } else if (suffix == 'l') {
            bits = 32u;
        }
        if (in.ops[1].kind == AS_X86_OP_REG && in.ops[1].size_bits != bits) {
            snprintf(encerr, encerr_sz, "crc32%c: the source register is not of the suffix's width", suffix);
            return -1;
        }
        if (in.ops[1].kind == AS_X86_OP_MEM) {
            in.ops[1].size_bits = bits;
            in.ops[1].u.mem.size_bits = bits;
        }
    }

    if (cfg->is_64) {
        if (as_x86_encode_x86_64(&in, code, code_cap, code_len, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else {
        if (as_x86_encode_ext(try_encode_x86_sse3, &in, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (as_x86_encode_ext(try_encode_x86_ssse3, &in, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (as_x86_encode_ext(try_encode_x86_sse41, &in, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (as_x86_encode_ext(try_encode_x86_sse42, &in, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (as_x86_encode_i386(&in, code, code_cap, code_len, encerr, encerr_sz) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * data16 written before an instruction is the byte 66 and nothing more:
 * the instruction is assembled as it would be without, at the size its
 * suffix and its registers give it, and the prefix goes in front.
 * `data16 movl $1, %eax` is 66 b8 01 00 00 00 to GNU as, four bytes of
 * immediate and all.  It was made the instruction's operand size, which
 * a suffix then took away again (`data16 movl %eax, %ebx` had no 66) or
 * which shortened an immediate.  Before an instruction that has the
 * prefix already it is the prefix twice, and refused.
 */
/*
 * An instruction with a VEX or an EVEX prefix: the statement's
 * operands made what the table of them is written in, and the table's
 * encoder asked (as_x86_vec.h).
 *
 * 1 if the statement is one and is encoded, 0 if its name is none of
 * them, -1 with the reason if it is one and is refused.
 */
static int encode_x86_vec(const as_elf_cfg_t *cfg, const as_instruction_t *insn, unsigned char *code, size_t code_cap,
                          size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_operand_t ops[5];
    as_x86_vec_req_t rq;
    char mn[32];
    size_t n = insn->operand_count;
    /* (The statement's own syntax: a source may change it as it goes.) */
    int intel = insn->syntax_intel != 0;
    size_t i;

    if (insn->mnemonic == NULL || strlen(insn->mnemonic) >= sizeof(mn)) {
        return 0;
    }
    for (i = 0; insn->mnemonic[i] != '\0'; ++i) {
        mn[i] = (char)tolower((unsigned char)insn->mnemonic[i]);
    }
    mn[i] = '\0';
    if (!as_x86_vec_knows(mn)) {
        return 0;
    }
    if (n > sizeof(ops) / sizeof(ops[0])) {
        snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);
        return -1;
    }
    for (i = 0; i < n; ++i) {
        /* In AT&T's order, which Intel's is the reverse of. */
        const as_operand_t *src = &insn->operands[intel ? n - 1 - i : i];
        const as_x86_reg_info_t *index = NULL;

        if (src->kind == AS_OPERAND_REGISTER) {
            if (as_x86_reg_operand(src->u.reg, &ops[i]) != 0 || ops[i].kind != AS_X86_OP_REG) {
                snprintf(encerr, encerr_sz, "'%s' does not take the register %s", mn,
                         src->u.reg != NULL ? src->u.reg : "");
                return -1;
            }
            continue;
        }
        if (src->kind == AS_OPERAND_MEMORY && src->u.mem.index_reg != NULL) {
            index = as_x86_reg_find(src->u.mem.index_reg);
        }
        if (index != NULL && (index->cls == AS_X86_REGCLASS_XMM || index->cls == AS_X86_REGCLASS_YMM ||
                              index->cls == AS_X86_REGCLASS_ZMM)) {
            /* A vector for the index: the rest of the address is
             * read as any other is, and the index put to it. */
            as_operand_t plain = *src;

            plain.u.mem.index_reg = NULL;
            if (convert_operand_x86(&plain, mn, &ops[i], cfg->is_64, intel, encerr, encerr_sz) != 0) {
                return -1;
            }
            ops[i].u.mem.has_index = 1;
            ops[i].u.mem.index = (as_x86_reg_t)index->number;
            ops[i].u.mem.index_class = index->cls;
            ops[i].u.mem.disp_only = 0;
            if (ops[i].u.mem.addr_bits == 0) {
                ops[i].u.mem.addr_bits = cfg->is_64 ? 64u : 32u;
            }
            continue;
        }
        if (convert_operand_x86(src, mn, &ops[i], cfg->is_64, intel, encerr, encerr_sz) != 0) {
            return -1;
        }
        /* An address with no register is of the code's size. */
        if (ops[i].kind == AS_X86_OP_MEM && ops[i].u.mem.disp_only && ops[i].u.mem.addr_bits == 0 && !cfg->is_64 &&
            cfg->x86_code_bits == 16u) {
            ops[i].u.mem.addr_bits = 16u;
        }
    }
    memset(&rq, 0, sizeof(rq));
    rq.mnemonic = mn;
    rq.ops = ops;
    rq.op_count = n;
    rq.opmask = insn->opmask;
    rq.have_opmask = insn->have_opmask;
    rq.zeroing = insn->zeroing;
    rq.broadcast = insn->broadcast > 0 ? (unsigned)insn->broadcast : 0u;
    rq.sae = insn->sae;
    rq.rounding = insn->rounding_mode;
    rq.pseudo = insn->pseudo;
    rq.mode = cfg->is_64 ? 64u : cfg->x86_code_bits == 16u ? 16u : 32u;
    rq.level = cfg->is_64 ? cfg->x86_64_isa_level : 4u;
    rq.explicit_rex = (insn->prefixes & AS_PREFIX_REX) != 0 || (insn->pseudo & AS_PSEUDO_REX) != 0;
    rq.rex_bits = insn->rex_bits & 0xfu;
    {
        int rc = as_x86_vec_encode(&rq, code, code_cap, code_len, encerr, encerr_sz);

        if (rc <= 0) {
            return rc;
        }
    }
    /*
     * A prefix written before one of these.  None of them is locked
     * or repeated, and where F3, F2 or 66 stands before one it is part
     * of its opcode: `lock addps`, `rep addps`, `data16 vaddps` were
     * assembled with the prefix left off, and nothing said.
     */
    {
        static const struct {
            unsigned flag;
            const char *name;
        } none[] = {{AS_PREFIX_LOCK, "lock"},     {AS_PREFIX_HLE, "xacquire or xrelease"},
                    {AS_PREFIX_REP, "rep"},       {AS_PREFIX_REPE, "repe"},
                    {AS_PREFIX_REPNE, "repne"},   {AS_PREFIX_DATA16, "data16"}};

        for (i = 0; i < sizeof(none) / sizeof(none[0]); ++i) {
            if ((insn->prefixes & none[i].flag) != 0) {
                /* (66 before one of general registers, or of none, is
                 * the operand size: `data16 popcnt`, `data16 xgetbv`.
                 * It is the first byte but for an address size's.) */
                if (none[i].flag == AS_PREFIX_DATA16 && as_x86_vec_takes_data16(mn)) {
                    size_t at = *code_len != 0 && code[0] == 0x67 ? 1 : 0;

                    if (*code_len + 1 > code_cap) {
                        return -1;
                    }
                    if (at < *code_len && code[at] == 0x66) {
                        snprintf(encerr, encerr_sz, "'%s' has the operand-size prefix already, for its 16-bit operands",
                                 mn);
                        return -1;
                    }
                    memmove(code + at + 1, code + at, *code_len - at);
                    code[at] = 0x66;
                    (*code_len)++;
                    continue;
                }
                snprintf(encerr, encerr_sz, "'%s' takes no %s prefix", mn, none[i].name);
                return -1;
            }
        }
    }
    /* The address size, written: the byte, unless the address is of
     * registers that already say another. */
    if ((insn->prefixes & AS_PREFIX_ADDR16) != 0) {
        unsigned other = cfg->is_64 ? 32u : cfg->x86_code_bits == 16u ? 32u : 16u;
        int have = 0;

        for (i = 0; i < n; ++i) {
            /* (From where the instruction is, the prefix is all that
             * says the address is of 32 bits: `(%eip)`.) */
            if (ops[i].kind == AS_X86_OP_MEM && ops[i].u.mem.addr_bits != 0 && !ops[i].u.mem.rip_relative) {
                if (ops[i].u.mem.addr_bits != other) {
                    snprintf(encerr, encerr_sz, "'%s' has an address-size prefix, and an address of another size", mn);
                    return -1;
                }
                have = 1;
            }
        }
        /* (An address of the other size has the byte put on by what
         * writes the prefixes outside the instruction.) */
        if (!have) {
            if (*code_len >= code_cap) {
                return -1;
            }
            memmove(code + 1, code, *code_len);
            code[0] = 0x67;
            (*code_len)++;
        }
    }
    return 1;
}

/*
 * The opmask instructions, all of them, from one table.
 *
 * There were two emitters, for `kmovw` into a mask register and for the
 * binary operations, and they found an instruction by how its name
 * began.  So `kmovw %eax, %k1` was assembled as `kmovw %k0, %k1`, the
 * general register's number taken for a mask's; `kunpckbw` had
 * `kunpckwd`'s prefix and `kunpckwd` `kunpckdq`'s; `kand`, `kandfoo`
 * and `kaddz`, which are not instructions, were assembled as `kandw`
 * or `kaddw`; and every other opmask instruction -- a move to memory
 * or to a general register, `knot`, `kortest`, `ktest`, the shifts,
 * and `kmov` of any size but 16 bits -- was "unknown register".
 *
 * 1 if the statement is an opmask instruction and is encoded, 0 if its
 * name is none of them, -1 if it is one and its operands are not its.
 */
static int encode_x86_opmask(const as_instruction_t *insn, int intel_syntax, int is64, unsigned char *code,
                             size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    enum { K_MOVE, K_BINARY, K_UNARY, K_SHIFT };
    /* The prefix and VEX.W of a size, for all but the moves through a
     * general register: w, b, q, d. */
    static const struct {
        char letter;
        as_vex_pp_t pp;
        int w;
    } sizes[] = {{'w', AS_VEX_PP_NONE, 0}, {'b', AS_VEX_PP_66, 0}, {'q', AS_VEX_PP_NONE, 1}, {'d', AS_VEX_PP_66, 1}};
    static const struct {
        const char *base;
        int kind;
        unsigned char opcode;
    } rows[] = {
        {"kmov", K_MOVE, 0x90},    {"kand", K_BINARY, 0x41},   {"kandn", K_BINARY, 0x42}, {"kor", K_BINARY, 0x45},
        {"kxnor", K_BINARY, 0x46}, {"kxor", K_BINARY, 0x47},   {"kadd", K_BINARY, 0x4a},  {"knot", K_UNARY, 0x44},
        {"kortest", K_UNARY, 0x98}, {"ktest", K_UNARY, 0x99},  {"kshiftr", K_SHIFT, 0x30}, {"kshiftl", K_SHIFT, 0x32},
    };
    const char *mn = insn->mnemonic;
    as_x86_vex_insn_t v;
    const as_operand_t *ops[3];
    size_t n;
    size_t i;
    int kind = -1;
    char size = '\0';
    long long count = 0;
    unsigned k[3] = {0, 0, 0};
    int is_k[3] = {0, 0, 0};

    if (mn == NULL || (mn[0] != 'k' && mn[0] != 'K') || insn->operand_count > 3) {
        return 0;
    }
    n = strlen(mn);
    memset(&v, 0, sizeof(v));
    v.mnemonic = mn;
    v.map = AS_VEX_MAP_0F;
    if (strcasecmp(mn, "kunpckbw") == 0 || strcasecmp(mn, "kunpckwd") == 0 || strcasecmp(mn, "kunpckdq") == 0) {
        kind = K_BINARY;
        v.opcode = 0x4b;
        size = tolower((unsigned char)mn[6]) == 'b' ? 'b' : (tolower((unsigned char)mn[6]) == 'w' ? 'w' : 'q');
    } else {
        for (i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
            if (n == strlen(rows[i].base) + 1 && strncasecmp(mn, rows[i].base, n - 1) == 0 &&
                strchr("wbqd", tolower((unsigned char)mn[n - 1])) != NULL) {
                kind = rows[i].kind;
                v.opcode = rows[i].opcode;
                size = (char)tolower((unsigned char)mn[n - 1]);
                break;
            }
        }
    }
    if (kind < 0) {
        return 0;
    }
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        if (sizes[i].letter == size) {
            v.pp = sizes[i].pp;
            v.vex_w = sizes[i].w;
        }
    }

    /* The operands, as AT&T writes them: source first. */
    for (i = 0; i < insn->operand_count; ++i) {
        ops[i] = &insn->operands[intel_syntax ? insn->operand_count - 1 - i : i];
        is_k[i] = ops[i]->kind == AS_OPERAND_REGISTER && parse_k_reg(ops[i]->u.reg, &k[i]) == 0;
    }
    snprintf(encerr, encerr_sz, "'%s' is not given the operands it takes", mn);

    if (kind == K_BINARY) {
        /* What is read, what it is combined with, and where it goes. */
        if (insn->operand_count != 3 || !is_k[0] || !is_k[1] || !is_k[2]) {
            return -1;
        }
        v.vex_l = 1;
        v.dst = (as_x86_reg_t)k[2];
        v.src1 = (as_x86_reg_t)k[1];
        v.src2.kind = AS_X86_OP_REG;
        v.src2.u.reg = (as_x86_reg_t)k[0];
    } else if (kind == K_UNARY) {
        if (insn->operand_count != 2 || !is_k[0] || !is_k[1]) {
            return -1;
        }
        v.dst = (as_x86_reg_t)k[1];
        v.src2.kind = AS_X86_OP_REG;
        v.src2.u.reg = (as_x86_reg_t)k[0];
    } else if (kind == K_SHIFT) {
        /* A count, what is shifted, and where it goes.  Of bytes and
         * words the opcode is the lower of two, and W says which. */
        if (insn->operand_count != 3 || ops[0]->kind != AS_OPERAND_IMMEDIATE ||
            eval_expr_const(ops[0]->u.expr, &count) != 0 || count < 0 || count > 255 || !is_k[1] || !is_k[2]) {
            return -1;
        }
        v.map = AS_VEX_MAP_0F3A;
        v.pp = AS_VEX_PP_66;
        v.vex_w = (size == 'w' || size == 'q') ? 1 : 0;
        if (size == 'd' || size == 'q') {
            v.opcode++;
        }
        v.dst = (as_x86_reg_t)k[2];
        v.src2.kind = AS_X86_OP_REG;
        v.src2.u.reg = (as_x86_reg_t)k[1];
    } else {
        /* A move: between masks or from memory (90), to memory (91),
         * from a general register (92) or to one (93). */
        const as_x86_reg_info_t *gpr;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (is_k[1] && (is_k[0] || ops[0]->kind == AS_OPERAND_MEMORY || operand_is_bare_address(ops[0]))) {
            v.opcode = 0x90;
            v.dst = (as_x86_reg_t)k[1];
            if (is_k[0]) {
                v.src2.kind = AS_X86_OP_REG;
                v.src2.u.reg = (as_x86_reg_t)k[0];
            } else if (convert_operand_x86(ops[0], mn, &v.src2, is64, intel_syntax, encerr, encerr_sz) != 0) {
                return -1;
            }
        } else if (is_k[0] && (ops[1]->kind == AS_OPERAND_MEMORY || operand_is_bare_address(ops[1]))) {
            v.opcode = 0x91;
            v.dst = (as_x86_reg_t)k[0];
            if (convert_operand_x86(ops[1], mn, &v.src2, is64, intel_syntax, encerr, encerr_sz) != 0) {
                return -1;
            }
        } else if ((is_k[0] && ops[1]->kind == AS_OPERAND_REGISTER) || (is_k[1] && ops[0]->kind == AS_OPERAND_REGISTER)) {
            int to_mask = is_k[1];
            unsigned mask = to_mask ? k[1] : k[0];

            /* The register is of 32 bits, or of 64 for a move of 64. */
            gpr = as_x86_reg_find_class(ops[to_mask ? 0 : 1]->u.reg, AS_X86_REGCLASS_GENERAL, is64 ? 15u : 7u);
            if (gpr == NULL || gpr->width != (size == 'q' ? 64u : 32u)) {
                return -1;
            }
            v.opcode = to_mask ? 0x92 : 0x93;
            v.pp = (size == 'w') ? AS_VEX_PP_NONE : (size == 'b' ? AS_VEX_PP_66 : AS_VEX_PP_F2);
            v.vex_w = size == 'q' ? 1 : 0;
            /* To a mask the mask is in the register field; from one,
             * the general register is. */
            v.dst = to_mask ? (as_x86_reg_t)mask : (as_x86_reg_t)gpr->number;
            v.src2.kind = AS_X86_OP_REG;
            v.src2.u.reg = to_mask ? (as_x86_reg_t)gpr->number : (as_x86_reg_t)mask;
        } else {
            return -1;
        }
    }
    encerr[0] = '\0';
    if (as_x86_encode_vex_3op(&v, code, code_cap, code_len, encerr, encerr_sz) != 0) {
        if (encerr[0] == '\0') {
            snprintf(encerr, encerr_sz, "'%s' is not given the operands it takes", mn);
        }
        return -1;
    }
    if (kind == K_SHIFT) {
        if (*code_len + 1 > code_cap) {
            return -1;
        }
        code[(*code_len)++] = (unsigned char)count;
    }
    return 1;
}

/* Whether an expression is a symbol with the @TLSCALL operator on it. */
static int expr_is_tlscall(const as_expr_t *e) {
    return e != NULL && e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && e->reloc_op != NULL &&
           strcasecmp(e->reloc_op, "TLSCALL") == 0;
}

/*
 * Whether an instruction's suffix says one size and a general register
 * among its operands is of another: `movl %ax, %ebx`, `addw %al, %bx`,
 * `incl %ax`, `pushw %eax`.  They were assembled, the suffix or the
 * register winning as it fell out.
 *
 * Asked of the instructions whose general registers are all of the
 * operand's size, and of no other: not of one with a segment, control
 * or debug register, a shift's count or a port.
 */
static int x86_suffix_denied_by_register(const as_instruction_t *insn, const char *base, char suffix, int intel_syntax,
                                         char *encerr, size_t encerr_sz) {
    static const char *const same_size[] = {
        "add", "adc", "sbb", "sub", "and", "or",  "xor", "cmp",  "mov",  "test", "xchg", "xadd", "cmpxchg",
        "inc", "dec", "not", "neg", "mul", "div", "idiv", "imul", "push", "pop",  "bt",   "bts",  "btr",
        "btc", "bsf", "bsr",
    };
    static const char *const shifts[] = {"shl", "shr", "sar", "sal", "rol", "ror", "rcl", "rcr"};
    unsigned want = suffix == 'b' ? 8u : (suffix == 'w' ? 16u : (suffix == 'l' ? 32u : 64u));
    int is_shift = 0;
    int known = strncmp(base, "cmov", 4) == 0;
    size_t i;

    for (i = 0; !known && i < sizeof(same_size) / sizeof(same_size[0]); ++i) {
        known = strcmp(base, same_size[i]) == 0;
    }
    for (i = 0; !known && i < sizeof(shifts) / sizeof(shifts[0]); ++i) {
        known = is_shift = strcmp(base, shifts[i]) == 0;
    }
    if (!known) {
        return 0;
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];
        const as_x86_reg_info_t *r;

        if (op->kind != AS_OPERAND_REGISTER) {
            continue;
        }
        r = as_x86_reg_find(op->u.reg);
        if (r == NULL || r->cls != AS_X86_REGCLASS_GENERAL) {
            /* A segment register, or one of the system's: the
             * instruction is another kind of move. */
            return 0;
        }
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];
        const as_x86_reg_info_t *r;

        /* A shift's count is %cl whatever is shifted. */
        if (op->kind != AS_OPERAND_REGISTER ||
            (is_shift && insn->operand_count == 2 && i == (intel_syntax ? 1u : 0u))) {
            continue;
        }
        r = as_x86_reg_find(op->u.reg);
        if (r->width != want) {
            snprintf(encerr, encerr_sz, "'%s%c' is of %u bits and %%%s is of %u", base, suffix, want, r->name,
                     (unsigned)r->width);
            return 1;
        }
    }
    return 0;
}

/*
 * A move between the accumulator and an address that is a number or a
 * symbol and no register: in 32-bit code there is a form with no ModRM,
 * a byte shorter, and it is the one GNU as writes.  (In 64-bit code
 * that form takes an address of eight bytes, and is `movabs`.)  A load
 * through the global offset table is left as it is, for the linker to
 * make a `lea` of.
 */
static void x86_shorten_accumulator_move(const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                                         size_t *code_len) {
    const as_instruction_t *insn;
    size_t at = 0;
    size_t disp_len = 4;
    unsigned char absolute = 0x05;
    size_t i;

    if (cfg == NULL || st == NULL || st->kind != AS_STMT_INSTRUCTION || cfg->is_64) {
        return;
    }
    /* (In 16-bit code the address is of two bytes, behind r/m 110.) */
    if (cfg->x86_code_bits == 16u) {
        disp_len = 2;
        absolute = 0x06;
    }
    insn = &st->u.instr;
    if (insn->operand_count != 2 || insn->mnemonic == NULL ||
        !(streq_ci(insn->mnemonic, "mov") || streq_ci(insn->mnemonic, "movb") || streq_ci(insn->mnemonic, "movw") ||
          streq_ci(insn->mnemonic, "movl"))) {
        return;
    }
    while (at < *code_len && (code[at] == 0x26 || code[at] == 0x2e || code[at] == 0x36 || code[at] == 0x3e ||
                              code[at] == 0x64 || code[at] == 0x65 || code[at] == 0x66)) {
        ++at;
    }
    if (at + 2 + disp_len != *code_len || code[at + 1] != absolute ||
        !(code[at] == 0x88 || code[at] == 0x89 || code[at] == 0x8a || code[at] == 0x8b)) {
        return;
    }
    for (i = 0; i < insn->operand_count && (code[at] == 0x8a || code[at] == 0x8b); ++i) {
        const as_operand_t *op = &insn->operands[i];
        const as_expr_t *e = op->kind == AS_OPERAND_MEMORY
                                 ? op->u.mem.disp
                                 : ((op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) ? op->u.expr
                                                                                                           : NULL);
        const char *rop = as_expr_reloc_op(e);

        if (rop != NULL && strcasecmp(rop, "GOT") == 0) {
            return;
        }
    }
    /* 8a and 8b read, to a0 and a1; 88 and 89 write, a2 and a3. */
    code[at] = (unsigned char)(0xa0u + (code[at] & 1u) + ((code[at] & 2u) ? 0u : 2u));
    memmove(code + at + 1, code + at + 2, disp_len);
    (*code_len)--;
}

/*
 * The statement's bytes, by the encoders or by the emitters.  A
 * statement of 32-bit code with an address of 16-bit registers is the
 * encoders', which have that address's table; but they have no x87 and
 * little SSE, and `fldl (%bx)` and `movaps (%bx), %xmm0` were
 * "unsupported mnemonic".  What they refuse is given to the emitters.
 */
static int encode_x86_stmt_either(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                                  size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    char first[256];
    int rc = encode_x86_stmt_body(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);

    if (rc == 0 || cfg == NULL || st == NULL || st->kind != AS_STMT_INSTRUCTION || cfg->is_64 ||
        !x86_stmt_is_the_encoders(cfg, &st->u.instr, 0)) {
        return rc;
    }
    snprintf(first, sizeof(first), "%s", encerr != NULL ? encerr : "");
    x86_addr16_by_emitters = 1;
    rc = encode_x86_stmt_body(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);
    x86_addr16_by_emitters = 0;
    if (rc != 0 && encerr != NULL && encerr_sz != 0) {
        snprintf(encerr, encerr_sz, "%s", first);
    }
    return rc;
}

/*
 * In 32-bit code, the prefix of an address through 16-bit registers,
 * where the emitter that wrote the instruction has not put it on.
 */
static int x86_addr16_prefix(const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code, size_t code_cap,
                             size_t *code_len) {
    size_t skip;
    size_t len;
    size_t i;

    if (cfg == NULL || st == NULL || st->kind != AS_STMT_INSTRUCTION) {
        return 0;
    }
    /* In 64-bit code the registers of an address say its size to every
     * emitter; what some did not hear is the prefix written, before an
     * address from %rip or with no register: `fldl sym(%eip)` was
     * `fldl sym(%rip)`. */
    if (cfg->is_64) {
        if ((st->u.instr.prefixes & AS_PREFIX_ADDR16) == 0 || *code_len == 0) {
            return 0;
        }
        skip = (*code_len > 1 && code[0] == 0x9b) ? 1 : 0;
        for (i = skip; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                         code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                         code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
            if (code[i] == 0x67) {
                return 0;
            }
        }
        if (*code_len + 1 > code_cap) {
            return -1;
        }
        memmove(code + skip + 1, code + skip, *code_len - skip);
        code[skip] = 0x67;
        (*code_len)++;
        return 0;
    }
    /* (A wait is an instruction of its own, and the prefix is of the
     * one after it.) */
    skip = (*code_len > 1 && code[0] == 0x9b) ? 1 : 0;
    for (i = skip; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                     code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                     code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
        if (code[i] == 0x67) {
            return 0;
        }
    }
    len = *code_len - skip;
    /* (In 16-bit code it is the 32-bit registers that have it:
     * `fldl (%eax)` there loaded through %bx and %si.) */
    if (x86_prepend_addr_size(&st->u.instr, cfg->x86_code_bits == 16u ? 32 : 16, code + skip, code_cap - skip, &len) !=
        0) {
        return -1;
    }
    *code_len = len + skip;
    return 0;
}

/* Whether the segment written on an address is the one it has with
 * none written. */
static int x86_segment_is_the_default(const as_mem_operand_t *mem) {
    static const char *const of_stack[] = {"ebp", "esp", "rbp", "rsp", "bp", "sp"};
    const char *seg = mem->segment_reg;
    const char *base = mem->base_reg;
    int stack = 0;
    size_t i;

    if (seg == NULL) {
        return 0;
    }
    seg += seg[0] == '%';
    if (base != NULL) {
        base += base[0] == '%';
        for (i = 0; i < sizeof(of_stack) / sizeof(of_stack[0]); ++i) {
            if (streq_ci(base, of_stack[i])) {
                stack = 1;
            }
        }
    }
    return streq_ci(seg, stack ? "ss" : "ds");
}

/* Whether the address is the one a string instruction stores or
 * compares through: (%edi), in whichever size. */
static int x86_is_string_destination(const char *mn, const as_mem_operand_t *mem) {
    static const char *const names[] = {"movs", "cmps", "scas", "stos", "ins"};
    const char *base = mem->base_reg;
    size_t i;

    if (mn == NULL || base == NULL) {
        return 0;
    }
    base += base[0] == '%';
    if (!streq_ci(base, "di") && !streq_ci(base, "edi") && !streq_ci(base, "rdi")) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        size_t n = strlen(names[i]);

        if (strncasecmp(mn, names[i], n) == 0 && (mn[n] == '\0' || (strchr("bwlqdBWLQD", mn[n]) != NULL && mn[n + 1] == '\0'))) {
            return 1;
        }
    }
    return 0;
}

/* Is this base the place the instruction is at: %rip, or %eip, which is
 * the same in an address of 32 bits? */
static int x86_base_is_ip(const char *base_reg) {
    return base_reg != NULL && (streq_ci(base_reg, "rip") || streq_ci(base_reg, "eip"));
}

/*
 * An operator of thread-local storage belongs to one instruction, the
 * one its access model is written with, and the linker rewrites that
 * instruction by its bytes: on any other it would rewrite something
 * else.  So each is taken where GNU as takes it and nowhere else --
 * `movl %eax, sym@TLSGD(%ebx)` and thirty like it were assembled.
 *
 *   32-bit  @TLSGD      leal sym@TLSGD(,%ebx,1), %eax
 *                       leal sym@TLSGD(%reg), %eax         not (%eax)
 *           @TLSLDM     leal sym@TLSLDM(%reg), %eax        not (%eax)
 *           @TLSDESC    leal sym@TLSDESC(%ebx), %reg
 *           @GOTTPOFF, @GOTNTPOFF
 *                       movl, addl, subl sym@OP(%reg), %reg
 *           @INDNTPOFF  movl, addl sym@INDNTPOFF, %reg
 *   64-bit  @TLSGD, @TLSLD
 *                       leaq sym@OP(%rip), %rdi
 *           @TLSDESC    leaq sym@TLSDESC(%rip), %reg
 *           @GOTTPOFF   movq, addq sym@GOTTPOFF(%rip), %reg
 *   both    @TLSCALL    call *sym@TLSCALL(%eax), or (%rax)
 *
 * NULL if the statement may have the operator it has, or what is wrong.
 */
static const char *x86_tls_shape_wrong(const as_elf_cfg_t *cfg, const as_stmt_t *st, const char **op_out) {
    const as_instruction_t *in = &st->u.instr;
    const as_operand_t *mem = NULL;
    const as_operand_t *dst;
    const as_x86_reg_info_t *dr;
    const as_x86_reg_info_t *br;
    const char *rop = NULL;
    char base[32];
    char suffix = '\0';
    size_t regs = 0;
    size_t imms = 0;
    size_t i;
    unsigned want = cfg->is_64 ? 64u : 32u;
    int lea_model;

    if (in->operand_count == 0 || in->operand_count > 4) {
        return NULL;
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        const as_expr_t *e = op->kind == AS_OPERAND_MEMORY                                         ? op->u.mem.disp
                             : (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) ? op->u.expr
                                                                                                    : NULL;
        const char *r = first_symbol_op_in_expr(e);

        regs += op->kind == AS_OPERAND_REGISTER;
        imms += op->kind == AS_OPERAND_IMMEDIATE;
        if (r != NULL && rop == NULL) {
            rop = r;
            mem = op;
        }
    }
    if (rop == NULL) {
        return NULL;
    }
    /* An operator's relocation is of four bytes, and an address of 16
     * bits has two for it. */
    if (mem->kind == AS_OPERAND_MEMORY) {
        const as_x86_reg_info_t *b = mem->u.mem.base_reg != NULL ? as_x86_reg_find(mem->u.mem.base_reg) : NULL;
        const as_x86_reg_info_t *x = mem->u.mem.index_reg != NULL ? as_x86_reg_find(mem->u.mem.index_reg) : NULL;

        if ((b != NULL && b->cls == AS_X86_REGCLASS_GENERAL && b->width == 16) ||
            (x != NULL && x->cls == AS_X86_REGCLASS_GENERAL && x->width == 16)) {
            *op_out = rop;
            return "the address is of 16 bits, and its relocation of 32";
        }
    }
    /* And three that are not of thread-local storage alone: a place in
     * a table or a block is no distance from %rip, and its relocation
     * is signed where an address of 32 bits is not. */
    if (cfg->is_64 && mem->kind == AS_OPERAND_MEMORY &&
        (streq_ci(rop, "TPOFF") || streq_ci(rop, "DTPOFF") || streq_ci(rop, "GOT") || streq_ci(rop, "GOTPCREL"))) {
        const as_x86_reg_info_t *b = mem->u.mem.base_reg != NULL ? as_x86_reg_find(mem->u.mem.base_reg) : NULL;
        const as_x86_reg_info_t *x = mem->u.mem.index_reg != NULL ? as_x86_reg_find(mem->u.mem.index_reg) : NULL;

        *op_out = rop;
        if ((b != NULL && b->cls == AS_X86_REGCLASS_GENERAL && b->width == 32) ||
            (x != NULL && x->cls == AS_X86_REGCLASS_GENERAL && x->width == 32)) {
            return "the address is of 32 bits, and its relocation is not";
        }
        if (!streq_ci(rop, "GOTPCREL") && streq_ci(mem->u.mem.base_reg != NULL ? mem->u.mem.base_reg : "", "rip")) {
            return "it is a place, and no distance from %rip";
        }
        return NULL;
    }
    if (!(streq_ci(rop, "TLSGD") || streq_ci(rop, "TLSDESC") || streq_ci(rop, "TLSCALL") ||
          streq_ci(rop, "GOTTPOFF") ||
          (cfg->is_64 ? streq_ci(rop, "TLSLD")
                      : streq_ci(rop, "TLSLDM") || streq_ci(rop, "GOTNTPOFF") || streq_ci(rop, "INDNTPOFF")))) {
        return NULL;
    }
    *op_out = rop;
    if (normalize_x86_mnemonic(in->mnemonic, base, sizeof(base), &suffix) != 0) {
        return NULL;
    }
    if (mem->kind == AS_OPERAND_IMMEDIATE) {
        return "it is on a number, and belongs on an address";
    }
    /* (A symbol alone is an address with no register.) */
    if (mem->kind != AS_OPERAND_MEMORY) {
        static const as_operand_t bare_address = {.kind = AS_OPERAND_MEMORY};

        mem = &bare_address;
    }
    br = mem->u.mem.base_reg != NULL ? as_x86_reg_find(mem->u.mem.base_reg) : NULL;
    if (streq_ci(rop, "TLSCALL")) {
        if (strcmp(base, "call") != 0) {
            return "it is for a call";
        }
        if (mem->u.mem.index_reg != NULL || br == NULL || (strcmp(br->name, "eax") != 0 && strcmp(br->name, "rax") != 0)) {
            return cfg->is_64 ? "the call is through (%rax)" : "the call is through (%eax)";
        }
        return NULL;
    }
    dst = &in->operands[in->syntax_intel ? 0 : in->operand_count - 1];
    dr = dst->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(dst->u.reg) : NULL;
    if (dr != NULL && dr->cls != AS_X86_REGCLASS_GENERAL) {
        dr = NULL;
    }
    lea_model = streq_ci(rop, "TLSGD") || streq_ci(rop, "TLSDESC") || streq_ci(rop, "TLSLD") || streq_ci(rop, "TLSLDM");
    if (lea_model) {
        if (strcmp(base, "lea") != 0) {
            return "it is for lea";
        }
    } else if (strcmp(base, "mov") != 0 && strcmp(base, "add") != 0 &&
               (cfg->is_64 || streq_ci(rop, "INDNTPOFF") || strcmp(base, "sub") != 0)) {
        return cfg->is_64 || streq_ci(rop, "INDNTPOFF") ? "it is for mov or add" : "it is for mov, add or sub";
    }
    if (!lea_model && (imms != 0 || dr == NULL || (!cfg->is_64 && regs != 1))) {
        return "it is for an address read into a register";
    }
    if (dr == NULL || dr->width != want) {
        return cfg->is_64 ? "the register is of 64 bits" : "the register is of 32 bits";
    }
    if (cfg->is_64) {
        if (br == NULL || strcmp(br->name, "rip") != 0 || mem->u.mem.index_reg != NULL) {
            return "the address is from %rip";
        }
        if ((streq_ci(rop, "TLSGD") || streq_ci(rop, "TLSLD")) && strcmp(dr->name, "rdi") != 0) {
            return "the register is %rdi";
        }
        return NULL;
    }
    if (streq_ci(rop, "INDNTPOFF")) {
        return mem->u.mem.base_reg != NULL || mem->u.mem.index_reg != NULL ? "the address has no register" : NULL;
    }
    if (streq_ci(rop, "TLSGD") && mem->u.mem.index_reg != NULL) {
        const as_x86_reg_info_t *xr = as_x86_reg_find(mem->u.mem.index_reg);

        if (mem->u.mem.base_reg != NULL || xr == NULL || strcmp(xr->name, "ebx") != 0 || mem->u.mem.scale > 1) {
            return "with an index the address is (,%ebx,1)";
        }
        return strcmp(dr->name, "eax") != 0 ? "the register is %eax" : NULL;
    }
    if (mem->u.mem.index_reg != NULL) {
        return "the address has no index";
    }
    if (br == NULL || br->cls != AS_X86_REGCLASS_GENERAL) {
        return "the address has a base register";
    }
    if (streq_ci(rop, "TLSDESC")) {
        return strcmp(br->name, "ebx") != 0 ? "the address is from %ebx" : NULL;
    }
    if (streq_ci(rop, "TLSGD") || streq_ci(rop, "TLSLDM")) {
        if (strcmp(dr->name, "eax") != 0) {
            return "the register is %eax";
        }
        return strcmp(br->name, "eax") == 0 ? "the address is not from %eax" : NULL;
    }
    return br->width != 32 ? "the base register is of 32 bits" : NULL;
}

/*
 * What is refused of a statement before either emitter is asked, for
 * being something neither should be asked: each of these was assembled,
 * into an instruction that was not the one written.  1, and the reason,
 * or 0.
 */
static int x86_stmt_refused(const as_elf_cfg_t *cfg, const as_stmt_t *st, char *encerr, size_t encerr_sz) {
    const as_instruction_t *in = &st->u.instr;
    const char *word = x86_size_word_wrong(cfg, st);
    unsigned bits = cfg->is_64 ? 64u : cfg->x86_code_bits == 16u ? 16u : 32u;
    char base[32];
    char suffix = '\0';
    int direct;
    size_t i;

    if (word != NULL) {
        snprintf(encerr, encerr_sz, "'%s' is no prefix of %u-bit code", word, bits);
        return 1;
    }
    if (in->operand_count > 4 || normalize_x86_mnemonic(in->mnemonic, base, sizeof(base), &suffix) != 0) {
        return 0;
    }
    direct = in->operand_count == 1 &&
             (in->operands[0].kind == AS_OPERAND_LABEL_REF || in->operands[0].kind == AS_OPERAND_IMMEDIATE);
    /* A jump to a label is of the code's size, and has no letter to
     * say another; nor has a call one for 32 bits in 64-bit code. */
    if (direct && suffix != '\0' && suffix != 'b' &&
        (strcmp(base, "jmp") == 0 || (strcmp(base, "call") == 0 && cfg->is_64 && suffix == 'l'))) {
        snprintf(encerr, encerr_sz, "'%s' to a label takes no size: it is '%s'", in->mnemonic, base);
        return 1;
    }
    /* A segment is 16 bits, and so is where a far jump of 16 goes. */
    if (in->operand_count == 2 && (strcmp(base, "ljmp") == 0 || strcmp(base, "lcall") == 0) &&
        in->operands[0].kind == AS_OPERAND_IMMEDIATE && in->operands[1].kind == AS_OPERAND_IMMEDIATE) {
        long long v;

        /* (Intel's `segment:offset` is kept with the offset first.) */
        if (eval_expr_const(in->operands[in->syntax_intel ? 1 : 0].u.expr, &v) == 0 && (v < -32768 || v > 65535)) {
            snprintf(encerr, encerr_sz, "segment %lld is not of 16 bits", v);
            return 1;
        }
        if ((suffix == 'w' || (bits == 16u && suffix != 'l')) && eval_expr_const(in->operands[in->syntax_intel ? 0 : 1].u.expr, &v) == 0 &&
            (v < -32768 || v > 65535)) {
            snprintf(encerr, encerr_sz, "offset %lld is not of 16 bits", v);
            return 1;
        }
    }
    /* What `stos` stores is the accumulator, of the size it stores. */
    if (in->operand_count == 2 && strcmp(base, "stos") == 0) {
        const as_operand_t *src = &in->operands[in->syntax_intel ? 1 : 0];
        const as_x86_reg_info_t *r = src->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(src->u.reg) : NULL;
        unsigned want = suffix == 'b' ? 8u : suffix == 'w' ? 16u : suffix == 'l' ? 32u : suffix == 'q' ? 64u : 0u;

        if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL &&
            ((strcmp(r->name, "al") != 0 && strcmp(r->name, "ax") != 0 && strcmp(r->name, "eax") != 0 &&
              strcmp(r->name, "rax") != 0) ||
             (want != 0 && r->width != want))) {
            snprintf(encerr, encerr_sz, "'%s' stores the accumulator, and %%%s is not it", in->mnemonic, r->name);
            return 1;
        }
    }
    /* `movsxd` widens 32 bits. */
    if (in->operand_count == 2 && strcmp(base, "movsxd") == 0) {
        const as_operand_t *src = &in->operands[in->syntax_intel ? 1 : 0];
        const as_x86_reg_info_t *r = src->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(src->u.reg) : NULL;

        if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && r->width != 32) {
            snprintf(encerr, encerr_sz, "'movsxd' widens a register of 32 bits, and %%%s is of %u", r->name,
                     (unsigned)r->width);
            return 1;
        }
    }
    /* A segment's prefix by its name. */
    if ((in->prefixes & AS_PREFIX_SEG_OVERRIDE) != 0 && in->segment_override != NULL) {
        if (cfg->is_64 && (streq_ci(in->segment_override, "es") || streq_ci(in->segment_override, "ss"))) {
            snprintf(encerr, encerr_sz, "'%s' is no prefix of 64-bit code", in->segment_override);
            return 1;
        }
        for (i = 0; i < in->operand_count; ++i) {
            /* (Where a string instruction stores is through %es and no
             * other, and writing that is no second segment: `ds stos
             * %ax, %es:(%di)`.) */
            if (in->operands[i].kind == AS_OPERAND_MEMORY && in->operands[i].u.mem.segment_reg != NULL &&
                !x86_is_string_destination(in->mnemonic, &in->operands[i].u.mem)) {
                snprintf(encerr, encerr_sz, "a segment is written before '%s' and another in its address",
                         in->mnemonic);
                return 1;
            }
        }
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];

        /* The address-size prefix, and an address of the size it says
         * the address is not. */
        if ((in->prefixes & AS_PREFIX_ADDR16) != 0 && op->kind == AS_OPERAND_MEMORY) {
            const as_x86_reg_info_t *b = op->u.mem.base_reg != NULL ? as_x86_reg_find(op->u.mem.base_reg) : NULL;
            const as_x86_reg_info_t *x = op->u.mem.index_reg != NULL ? as_x86_reg_find(op->u.mem.index_reg) : NULL;

            if ((b != NULL && b->cls == AS_X86_REGCLASS_GENERAL && b->width == bits) ||
                (x != NULL && x->cls == AS_X86_REGCLASS_GENERAL && x->width == bits)) {
                snprintf(encerr, encerr_sz, "'%s' has an address-size prefix, and an address of %u bits", in->mnemonic,
                         bits);
                return 1;
            }
        }
        /* In 64-bit code a place in a table or a block is a signed
         * number: it is no part of an address that `lea` puts in a
         * register of 32 bits, nor a number of 32 that is not widened. */
        if (cfg->is_64 && (op->kind == AS_OPERAND_MEMORY || op->kind == AS_OPERAND_IMMEDIATE ||
                           op->kind == AS_OPERAND_LABEL_REF)) {
            const char *rop = first_symbol_op_in_expr(op->kind == AS_OPERAND_MEMORY ? op->u.mem.disp : op->u.expr);
            int wide = suffix == 'q' || strcmp(base, "push") == 0;
            size_t k;

            if (rop == NULL ||
                !(streq_ci(rop, "TPOFF") || streq_ci(rop, "DTPOFF") || streq_ci(rop, "GOT") || streq_ci(rop, "PLT"))) {
                continue;
            }
            /* (A symbol alone is an address without a register.) */
            if (op->kind != AS_OPERAND_IMMEDIATE && strcmp(base, "lea") != 0) {
                continue;
            }
            /* (From %rip it is a distance, and the same in any width.) */
            if (op->kind == AS_OPERAND_MEMORY && x86_base_is_ip(op->u.mem.base_reg)) {
                continue;
            }
            for (k = 0; k < in->operand_count; ++k) {
                const as_x86_reg_info_t *r =
                    in->operands[k].kind == AS_OPERAND_REGISTER ? as_x86_reg_find(in->operands[k].u.reg) : NULL;

                wide |= r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && r->width == 64;
            }
            if (!wide) {
                snprintf(encerr, encerr_sz, "'@%s' is a signed number, and '%s' has 32 bits that are not widened", rop,
                         in->mnemonic);
                return 1;
            }
        }
    }
    return 0;
}

/*
 * A statement that is prefixes alone (AS_BARE_PREFIX_MNEMONIC): their
 * bytes in the order written.  0 if it was one and is assembled, -1 if
 * it was one and is refused, 1 if it is something else.
 */
static int x86_bare_prefixes(const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code, size_t code_cap,
                             size_t *code_len, char *encerr, size_t encerr_sz) {
    const char *p = st->u.instr.mnemonic;
    size_t n = 0;

    if (strncmp(p, AS_BARE_PREFIX_MNEMONIC, sizeof(AS_BARE_PREFIX_MNEMONIC) - 1u) != 0) {
        return 1;
    }
    p += sizeof(AS_BARE_PREFIX_MNEMONIC) - 1u;
    while (*p == ' ') {
        char word[16];
        size_t k = 0;

        p++;
        while (*p != '\0' && *p != ' ' && k + 1 < sizeof(word)) {
            word[k++] = *p++;
        }
        word[k] = '\0';
        /* The segments 64-bit code has no use for, and the sizes that
         * are not its other ones. */
        if (cfg->is_64 && (streq_ci(word, "es") || streq_ci(word, "ss") || streq_ci(word, "data32") ||
                           streq_ci(word, "addr16"))) {
            snprintf(encerr, encerr_sz, "'%s' is no prefix of 64-bit code", word);
            return -1;
        }
        if (n >= code_cap) {
            return -1;
        }
        code[n++] = (unsigned char)as_x86_bare_prefix_byte(word);
    }
    *code_len = n;
    return 0;
}

/* A general register for an operand that was not written. */
static void x86_implied_reg(as_operand_t *op, const char *reg) {
    static char raw[8];

    memset(op, 0, sizeof(*op));
    op->kind = AS_OPERAND_REGISTER;
    snprintf(raw, sizeof(raw), "%%%s", reg);
    op->raw = raw;
    op->u.reg = raw + 1;
}

/*
 * The spellings of older assemblers, which GNU as still takes: each is
 * another way to write an instruction this assembler has, and is made
 * that instruction here, once, for both emitters.  None was assembled.
 *
 *   smovl, sscab, slod, ssto, scmp   the string instructions, by the
 *                                    names they had on System V
 *   movsb %al,%ebx, movzw %ax,%ebx   an extension whose second size is
 *                                    its register's
 *   div %ebx,%eax                    the accumulator written out
 *   in %dx, outb %dx, in $5          the accumulator left out
 *   setab                            a byte's suffix on what is a byte
 *   fldenvs, fnsavel                 an environment of 16 bits, or 32
 *   jmp $8,$0x1000                   a far jump without its l
 *
 * 1 if `bare` is now the statement to assemble; `ops` and `name` are
 * what it points into.  2 if it is, behind a wait (9B) that the caller
 * is to write, and 3 behind a lock (F0).
 */
static int x86_old_spelling(const as_elf_cfg_t *cfg, const as_stmt_t *st, as_stmt_t *bare, as_operand_t ops[4],
                            char *name, size_t name_sz) {
    static const struct {
        const char *old;
        const char *now;
    } strings[] = {{"smov", "movs"}, {"ssca", "scas"}, {"slod", "lods"}, {"ssto", "stos"}, {"scmp", "cmps"}};
    static const char *const env[] = {"fldenv", "fnstenv", "fstenv", "fsave", "fnsave", "frstor"};
    const as_instruction_t *in = &st->u.instr;
    const char *mn = in->mnemonic;
    size_t len = strlen(mn);
    size_t nops = in->operand_count;
    size_t i;
    char low[16];

    if (len == 0 || len >= sizeof(low) || nops > 4) {
        return 0;
    }
    for (i = 0; i <= len; ++i) {
        low[i] = (char)tolower((unsigned char)mn[i]);
    }
    *bare = *st;
    for (i = 0; i < nops; ++i) {
        ops[i] = in->operands[i];
    }
    bare->u.instr.operands = ops;
    bare->u.instr.mnemonic = name;

    for (i = 0; nops == 0 && i < sizeof(strings) / sizeof(strings[0]); ++i) {
        if (strncmp(low, strings[i].old, 4) == 0 &&
            (low[4] == '\0' || (strchr("bwlq", low[4]) != NULL && low[5] == '\0'))) {
            snprintf(name, name_sz, "%s%c", strings[i].now, low[4] != '\0' ? low[4] : 'l');
            return 1;
        }
    }
    if (nops == 2 && ops[1].kind == AS_OPERAND_REGISTER &&
        (strcmp(low, "movsb") == 0 || strcmp(low, "movzb") == 0 || strcmp(low, "movsw") == 0 ||
         strcmp(low, "movzw") == 0 || strcmp(low, "movsl") == 0)) {
        unsigned bits = parse_x86_reg_bits(ops[1].u.reg);
        const as_x86_reg_info_t *r = as_x86_reg_find(ops[1].u.reg);

        if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && (bits == 16u || bits == 32u || bits == 64u)) {
            snprintf(name, name_sz, "%s%c", low, bits == 16u ? 'w' : bits == 32u ? 'l' : 'q');
            return 1;
        }
    }
    if (nops == 2 && ops[1].kind == AS_OPERAND_REGISTER &&
        (strncmp(low, "div", 3) == 0 || strncmp(low, "idiv", 4) == 0)) {
        const char *rest = low + (low[0] == 'i' ? 4 : 3);
        const as_x86_reg_info_t *r = as_x86_reg_find(ops[1].u.reg);

        if ((rest[0] == '\0' || (strchr("bwlq", rest[0]) != NULL && rest[1] == '\0')) && r != NULL &&
            r->cls == AS_X86_REGCLASS_GENERAL &&
            (strcmp(r->name, "al") == 0 || strcmp(r->name, "ax") == 0 || strcmp(r->name, "eax") == 0 ||
             strcmp(r->name, "rax") == 0)) {
            /* (Its size is kept, for a divisor in memory.) */
            snprintf(name, name_sz, "%.*s%c", (int)(rest - low), low,
                     rest[0] != '\0' ? rest[0] : r->width == 8 ? 'b' : r->width == 16 ? 'w' : r->width == 32 ? 'l' : 'q');
            bare->u.instr.operand_count = 1;
            return 1;
        }
    }
    if (nops == 1 && (strncmp(low, "in", 2) == 0 || strncmp(low, "out", 3) == 0)) {
        int out = low[0] == 'o';
        const char *rest = low + (out ? 3 : 2);
        int port = (ops[0].kind == AS_OPERAND_REGISTER && streq_ci(ops[0].u.reg, "dx")) ||
                   ops[0].kind == AS_OPERAND_IMMEDIATE;

        if (port && (rest[0] == '\0' || (strchr("bwl", rest[0]) != NULL && rest[1] == '\0'))) {
            snprintf(name, name_sz, "%s", low);
            if (out) {
                ops[1] = ops[0];
            }
            x86_implied_reg(&ops[out ? 0 : 1], rest[0] == 'b' ? "al" : rest[0] == 'w' ? "ax" : "eax");
            bare->u.instr.operand_count = 2;
            return 1;
        }
    }
    if (nops == 1 && len >= 5 && strncmp(low, "set", 3) == 0 && low[len - 1] == 'b') {
        char jcc[16];
        unsigned cc;

        snprintf(jcc, sizeof(jcc), "j%s", low + 3);
        if (x86_cond_code(jcc, &cc) != 0) {
            jcc[strlen(jcc) - 1] = '\0';
            if (x86_cond_code(jcc, &cc) == 0) {
                snprintf(name, name_sz, "%.*s", (int)(len - 1), low);
                return 1;
            }
        }
    }
    for (i = 0; nops == 1 && i < sizeof(env) / sizeof(env[0]); ++i) {
        size_t el = strlen(env[i]);

        if (strncmp(low, env[i], el) == 0 && (low[el] == 's' || low[el] == 'l') && low[el + 1] == '\0') {
            /* 16 bits where the mode's are 32, or 32 where they are 16. */
            int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;

            if ((low[el] == 's') == code16) {
                snprintf(name, name_sz, "%s", env[i]);
                return 1;
            }
            /* The wait of `fsave` and `fstenv` is before the size's
             * prefix, not after: the instruction is the one without
             * it, and the caller writes the wait. */
            bare->u.instr.prefixes |= AS_PREFIX_DATA16;
            if (strcmp(env[i], "fsave") == 0 || strcmp(env[i], "fstenv") == 0) {
                snprintf(name, name_sz, "fn%s", env[i] + 1);
                return 2;
            }
            snprintf(name, name_sz, "%s", env[i]);
            return 1;
        }
    }
    /* A comparison with the register under the top, not written. */
    if (nops == 0 && (strcmp(low, "fcomi") == 0 || strcmp(low, "fucomi") == 0 || strcmp(low, "fcomip") == 0 ||
                      strcmp(low, "fucomip") == 0)) {
        snprintf(name, name_sz, "%s", low);
        x86_implied_reg(&ops[0], "st(1)");
        bare->u.instr.operand_count = 1;
        return 1;
    }
    /* A string through the port, of the size the port's register is
     * not: 32 bits unless a letter says. */
    if (nops == 2 && (strcmp(low, "ins") == 0 || strcmp(low, "outs") == 0)) {
        snprintf(name, name_sz, "%sl", low);
        return 1;
    }
    if (nops == 0 && strcmp(low, "sysretl") == 0 && !cfg->is_64) {
        snprintf(name, name_sz, "sysret");
        return 1;
    }
    /* From where the instruction is, in an address of 32 bits: %rip
     * behind the prefix that says so. */
    for (i = 0; cfg->is_64 && i < nops; ++i) {
        static char rip[] = "rip";

        if (ops[i].kind == AS_OPERAND_MEMORY && ops[i].u.mem.base_reg != NULL &&
            streq_ci(ops[i].u.mem.base_reg, "eip") && ops[i].u.mem.index_reg == NULL &&
            (in->prefixes & AS_PREFIX_ADDR16) == 0) {
            snprintf(name, name_sz, "%s", low);
            ops[i].u.mem.base_reg = rip;
            bare->u.instr.prefixes |= AS_PREFIX_ADDR16 | AS_PREFIX_ADDR_32;
            return 1;
        }
    }
    /* %cr8 where there are no registers past the seventh: %cr0 behind
     * a lock, which is how AMD's processors have it in 32-bit code. */
    if (nops == 2 && !cfg->is_64 && (strcmp(low, "mov") == 0 || strcmp(low, "movl") == 0) &&
        (in->prefixes & AS_PREFIX_LOCK) == 0) {
        for (i = 0; i < 2; ++i) {
            const char *r = ops[i].kind == AS_OPERAND_REGISTER ? ops[i].u.reg : "";
            unsigned num = strncasecmp(r, "cr", 2) == 0 && r[2] != '\0' ? (unsigned)atoi(r + 2) : 0u;

            if (num >= 8u && num <= 15u && strlen(r) <= 4 && ops[1 - i].kind == AS_OPERAND_REGISTER) {
                char low_cr[4];

                snprintf(name, name_sz, "%s", low);
                snprintf(low_cr, sizeof(low_cr), "cr%u", num - 8u);
                x86_implied_reg(&ops[i], low_cr);
                return 3;
            }
        }
    }
    if (nops == 2 && (strcmp(low, "jmp") == 0 || strcmp(low, "call") == 0) && ops[0].kind == AS_OPERAND_IMMEDIATE &&
        ops[1].kind == AS_OPERAND_IMMEDIATE) {
        snprintf(name, name_sz, "l%s", low);
        return 1;
    }
    return 0;
}

static int encode_x86_stmt(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                           size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    as_stmt_t bare;
    size_t i;

    /* (For the emitters that are handed operands and no mode.) */
    x86_emit_code16 = cfg != NULL && !cfg->is_64 && cfg->x86_code_bits == 16u;

    /* An instruction that was encoded as it was read (as_x86_precode())
     * is its bytes, and has nothing else left to encode it from. */
    if (st != NULL && cfg != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.code_len != 0) {
        unsigned bits = cfg->is_64 ? 64u : cfg->x86_code_bits == 16u ? 16u : 32u;

        if (st->u.instr.code_bits != bits || st->u.instr.code_len > code_cap) {
            snprintf(encerr, encerr_sz, "internal: '%s' was encoded for %u-bit code and is in %u-bit",
                     st->u.instr.mnemonic != NULL ? st->u.instr.mnemonic : "?", (unsigned)st->u.instr.code_bits, bits);
            return -1;
        }
        memcpy(code, st->u.instr.code, st->u.instr.code_len);
        *code_len = st->u.instr.code_len;
        return 0;
    }
    if (st != NULL && cfg != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL) {
        int rc = x86_bare_prefixes(cfg, st, code, code_cap, code_len, encerr, encerr_sz);

        if (rc <= 0) {
            return rc;
        }
        if (x86_stmt_refused(cfg, st, encerr, encerr_sz)) {
            return -1;
        }
        /*
         * The address-size prefix before an address that has no
         * register to say its size: the address is of the size the
         * prefix says, 16 bits in 32-bit code and 32 in 16-bit.  It is
         * assembled as this code's and made the other's here, for all
         * the emitters at once: most of them left the prefix out, and
         * the rest wrote it before the ModRM byte of this code's
         * address, which behind the prefix is `(%di)` -- `addr16 movss
         * sym, %xmm0` loaded from there.
         */
        if (!cfg->is_64 && (st->u.instr.prefixes & AS_PREFIX_ADDR16) != 0 &&
            st->u.instr.operand_count <= 4 &&
            (!is_rel_mnemonic(st->u.instr.mnemonic) ||
             (st->u.instr.operand_count == 1 && st->u.instr.operands[0].raw != NULL &&
              st->u.instr.operands[0].raw[0] == '*'))) {
            int code16 = cfg->x86_code_bits == 16u;
            int absolute = 0;
            long long where = 0;
            int where_known = 0;

            for (i = 0; i < st->u.instr.operand_count; ++i) {
                const as_operand_t *op = &st->u.instr.operands[i];

                /* (In Intel's syntax a number has no mark, and is not
                 * taken for an address here.) */
                if ((!st->u.instr.syntax_intel && operand_is_bare_address(op)) ||
                    (op->kind == AS_OPERAND_MEMORY && op->u.mem.base_reg == NULL && op->u.mem.index_reg == NULL)) {
                    absolute = 1;
                    where_known = eval_expr_const(op->kind == AS_OPERAND_MEMORY ? op->u.mem.disp : op->u.expr, &where) == 0;
                }
            }
            if (absolute) {
                size_t off = 0;
                size_t n;

                bare = *st;
                bare.u.instr.prefixes &= ~(AS_PREFIX_ADDR16 | AS_PREFIX_ADDR_32);
                if (code_cap < 4 ||
                    encode_x86_stmt(ctx, cfg, &bare, code + 1, code_cap - 3, code_len, encerr, encerr_sz) != 0) {
                    return -1;
                }
                n = *code_len;
                if ((code16 ? x86_disp16_offset(code + 1, n, 1, &off) : x86_disp32_offset(code + 1, n, 0, &off)) != 0 ||
                    off == 0 || off + (code16 ? 2u : 4u) > n) {
                    snprintf(encerr, encerr_sz, "'%s' has an address-size prefix, and no address it can be for",
                             st->u.instr.mnemonic);
                    return -1;
                }
                off += 1;
                /* (The byte before the address is the ModRM byte, whose
                 * "no register" is 5 in an address of 32 bits and 6 in
                 * one of 16; or it is the opcode of a move to or from
                 * the accumulator, which has none.  The address itself
                 * is two bytes shorter, or longer.) */
                if ((code[off - 1] & 0xc7u) == (code16 ? 0x06u : 0x05u)) {
                    code[off - 1] = (unsigned char)((code[off - 1] & 0x38u) | (code16 ? 0x05u : 0x06u));
                } else if (code[off - 1] < 0xa0 || code[off - 1] > 0xa3) {
                    snprintf(encerr, encerr_sz, "'%s' has an address-size prefix, and no address it can be for",
                             st->u.instr.mnemonic);
                    return -1;
                }
                if (code16) {
                    memmove(code + off + 4, code + off + 2, n + 1 - (off + 2));
                    /* (A number has its upper half, which the two
                     * bytes it was assembled into had not.) */
                    code[off + 2] = where_known ? (unsigned char)(((uint32_t)where >> 16) & 0xffu) : 0;
                    code[off + 3] = where_known ? (unsigned char)(((uint32_t)where >> 24) & 0xffu) : 0;
                    n += 2;
                } else {
                    memmove(code + off + 2, code + off + 4, n + 1 - (off + 4));
                    n -= 2;
                }
                code[0] = 0x67;
                *code_len = n + 1;
                return 0;
            }
        }
        /*
         * A call or a jump to a place not known here, of the size the
         * code's is not -- `callw sym`, `data16 jmp sym` -- is the
         * instruction of the other code behind the prefix.  `data16
         * jne sym` had the four bytes of this code's behind a prefix
         * that said two, and `callw sym` in 64-bit code the same.
         * (Before `loop` and its like `data16` says nothing.)
         */
        if (st->u.instr.operand_count == 1 && is_rel_mnemonic(st->u.instr.mnemonic) &&
            (st->u.instr.operands[0].kind == AS_OPERAND_LABEL_REF ||
             st->u.instr.operands[0].kind == AS_OPERAND_IMMEDIATE)) {
            char base[32];
            char letter = '\0';
            int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;

            if (is_fixed_short_rel_mnemonic(st->u.instr.mnemonic)) {
                if ((st->u.instr.prefixes & AS_PREFIX_DATA16) != 0) {
                    bare = *st;
                    bare.u.instr.prefixes &= ~AS_PREFIX_DATA16;
                    return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
                }
            } else if (normalize_x86_mnemonic(st->u.instr.mnemonic, base, sizeof(base), &letter) == 0) {
                int op16;

                long long number;

                letter = x86_branch_size_letter(st, letter, cfg->is_64 ? 64u : cfg->x86_code_bits);
                op16 = letter == 'w' || (code16 && letter != 'l');
                /* (To a number, which is no place in this object, the
                 * prefix is written before the jump as it is.) */
                if (op16 != code16 && eval_expr_const(st->u.instr.operands[0].u.expr, &number) != 0) {
                    as_elf_cfg_t other = *cfg;

                    other.x86_code_bits = code16 ? 32u : 16u;
                    other.is_64 = 0;
                    bare = *st;
                    bare.u.instr.mnemonic = base;
                    bare.u.instr.prefixes &= ~(AS_PREFIX_DATA16 | AS_PREFIX_ADDR16 | AS_PREFIX_ADDR_32);
                    if (code_cap < 2 ||
                        encode_x86_stmt(ctx, &other, &bare, code + 1, code_cap - 1, code_len, encerr, encerr_sz) != 0) {
                        return -1;
                    }
                    code[0] = 0x66;
                    (*code_len)++;
                    return 0;
                }
            }
        }
        {
            const char *rop = NULL;
            const char *wrong = x86_tls_shape_wrong(cfg, st, &rop);

            if (wrong != NULL) {
                snprintf(encerr, encerr_sz, "'@%s' is not for this instruction: %s", rop, wrong);
                return -1;
            }
        }
        if (!st->u.instr.syntax_intel) {
            as_operand_t ops[4];
            char name[32];

            int how = x86_old_spelling(cfg, st, &bare, ops, name, sizeof(name));

            if (how >= 2) {
                if (code_cap < 2) {
                    return -1;
                }
                code[0] = how == 2 ? 0x9b : 0xf0;
                rc = encode_x86_stmt(ctx, cfg, &bare, code + 1, code_cap - 1, code_len, encerr, encerr_sz);
                if (rc == 0) {
                    (*code_len)++;
                }
                return rc;
            }
            if (how == 1) {
                return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
    }

    /*
     * A segment that is the one the address has anyway -- %ss with the
     * frame or stack pointer for its base, %ds with anything else -- is
     * no prefix: `movl %ds:sym, %ecx` had a 3E that said nothing.
     */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.operand_count <= 4) {
        as_operand_t ops[4];
        int dropped = 0;

        for (i = 0; i < st->u.instr.operand_count; ++i) {
            ops[i] = st->u.instr.operands[i];
            /* (Not where a string instruction stores: that is through
             * %es and nothing else, and %ds written there is refused
             * for what it is.) */
            if (ops[i].kind == AS_OPERAND_MEMORY && x86_segment_is_the_default(&ops[i].u.mem) &&
                !x86_is_string_destination(st->u.instr.mnemonic, &ops[i].u.mem)) {
                ops[i].u.mem.segment_reg = NULL;
                dropped = 1;
            }
        }
        if (dropped) {
            bare = *st;
            bare.u.instr.operands = ops;
            return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
        }
    }

    /*
     * `andnl`, `bzhiq`, `shlxl`: the instructions of BMI are found by
     * their names, which have no suffix, and their size is their
     * registers'.  The suffix is checked against the register the
     * result goes to and taken off.
     */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL &&
        st->u.instr.operand_count >= 2) {
        static const char *const bases[] = {"andn", "bzhi", "blsi", "blsr", "blsmsk", "bextr", "pdep",
                                            "pext", "mulx", "rorx", "sarx", "shlx",   "shrx"};
        const char *mn = st->u.instr.mnemonic;
        size_t n = strlen(mn);
        char base[16];

        if (n >= 2 && n <= sizeof(base) && (mn[n - 1] == 'l' || mn[n - 1] == 'q' || mn[n - 1] == 'L' || mn[n - 1] == 'Q')) {
            size_t k;

            for (k = 0; k < n - 1; ++k) {
                base[k] = (char)tolower((unsigned char)mn[k]);
            }
            base[n - 1] = '\0';
            for (k = 0; k < sizeof(bases) / sizeof(bases[0]); ++k) {
                if (strcmp(base, bases[k]) == 0) {
                    const as_operand_t *dst = &st->u.instr.operands[cfg != NULL && cfg->intel_syntax
                                                                        ? 0
                                                                        : st->u.instr.operand_count - 1];
                    unsigned want = (mn[n - 1] == 'l' || mn[n - 1] == 'L') ? 32u : 64u;

                    if (dst->kind != AS_OPERAND_REGISTER || parse_x86_reg_bits(dst->u.reg) != want) {
                        snprintf(encerr, encerr_sz, "'%s' is of %u bits, and the register it is given is not", mn,
                                 want);
                        return -1;
                    }
                    bare = *st;
                    bare.u.instr.mnemonic = base;
                    return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
                }
            }
        }
    }

    /* The opmask instructions are encoded in one place, which is not
     * either of the two that the rest are divided between. */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && cfg != NULL) {
        int opmask = encode_x86_opmask(&st->u.instr, st->u.instr.syntax_intel != 0, cfg->is_64 != 0, code, code_cap,
                                       code_len, encerr, encerr_sz);

        if (opmask < 0) {
            return -1;
        }
        if (opmask > 0) {
            /* (They are AVX-512's, and held to the level it is.) */
            if (cfg->is_64 && cfg->x86_64_isa_level < 4) {
                set_x86_isa_requirement(encerr, encerr_sz, st->u.instr.mnemonic, 4);
                return -1;
            }
            if (x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len) != 0) {
                return -1;
            }
            return x86_addr16_prefix(cfg, st, code, code_cap, code_len);
        }
    }

    /* Nor are those with a VEX or an EVEX prefix, which have a table. */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && cfg != NULL) {
        int vec = encode_x86_vec(cfg, &st->u.instr, code, code_cap, code_len, encerr, encerr_sz);

        if (vec < 0) {
            return -1;
        }
        if (vec > 0) {
            if (x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len) != 0) {
                return -1;
            }
            return x86_addr16_prefix(cfg, st, code, code_cap, code_len);
        }
    }

    /*
     * `fsubp %st(1), %st`: a subtraction or a division that pops puts
     * its result where it does not pop from, which is %st(i) and never
     * the top; GNU as refuses the top for its destination.  (An addition
     * or a multiplication is the same either way about, and is taken.)
     */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL &&
        st->u.instr.operand_count == 2 && (cfg == NULL || !cfg->intel_syntax)) {
        const char *mn = st->u.instr.mnemonic;
        const as_operand_t *src = &st->u.instr.operands[0];
        const as_operand_t *dst = &st->u.instr.operands[1];

        if ((streq_ci(mn, "fsubp") || streq_ci(mn, "fsubrp") || streq_ci(mn, "fdivp") || streq_ci(mn, "fdivrp")) &&
            src->kind == AS_OPERAND_COPROCESSOR && dst->kind == AS_OPERAND_COPROCESSOR && src->u.coproc != NULL &&
            dst->u.coproc != NULL && (streq_ci(dst->u.coproc, "st") || streq_ci(dst->u.coproc, "st(0)")) &&
            !(streq_ci(src->u.coproc, "st") || streq_ci(src->u.coproc, "st(0)"))) {
            snprintf(encerr, encerr_sz, "'%s' leaves its result in %%st(i), and is given %%st for it", mn);
            return -1;
        }
    }

    /* A vector register is an index in a gather or a scatter and in
     * nothing else. */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL) {
        for (i = 0; i < st->u.instr.operand_count; ++i) {
            const as_operand_t *op = &st->u.instr.operands[i];

            if (op->kind == AS_OPERAND_MEMORY && op->u.mem.index_reg != NULL &&
                x86_index_addr_bits(op->u.mem.index_reg) == 0 && parse_x86_reg_bits(op->u.mem.index_reg) != 0 &&
                strstr(st->u.instr.mnemonic, "gather") == NULL && strstr(st->u.instr.mnemonic, "scatter") == NULL) {
                snprintf(encerr, encerr_sz, "'%s' has a vector register for an index, and is no gather or scatter",
                         st->u.instr.mnemonic);
                return -1;
            }
        }
    }

    /* A REX prefix is of 64-bit code and of no other: `rex nop` was
     * assembled in 32-bit code, the prefix left out and nothing said. */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && cfg != NULL && !cfg->is_64 &&
        (st->u.instr.prefixes & AS_PREFIX_REX) != 0) {
        snprintf(encerr, encerr_sz, "a REX prefix is written, and there is none but in 64-bit code");
        return -1;
    }

    /*
     * `call *foo@TLSCALL(%eax)`: the operator marks the call for the
     * linker and is no part of the address, which is (%eax) and nothing
     * added.  The relocation is made where the others are.
     */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.operand_count <= 4) {
        for (i = 0; i < st->u.instr.operand_count; ++i) {
            const as_operand_t *op = &st->u.instr.operands[i];

            if (op->kind == AS_OPERAND_MEMORY && expr_is_tlscall(op->u.mem.disp)) {
                as_operand_t ops[4];

                memcpy(ops, st->u.instr.operands, st->u.instr.operand_count * sizeof(ops[0]));
                ops[i].u.mem.disp = NULL;
                bare = *st;
                bare.u.instr.operands = ops;
                return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
    }

    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL) {
        char base[32];
        char suffix = '\0';

        if (normalize_x86_mnemonic(st->u.instr.mnemonic, base, sizeof(base), &suffix) != 0) {
            base[0] = '\0';
        }
        /* `shld %eax, %ebx`: the count that is not written is %cl. */
        if (st->u.instr.operand_count == 2 && (strcmp(base, "shld") == 0 || strcmp(base, "shrd") == 0)) {
            as_operand_t ops[3];
            as_operand_t count;
            char cl[] = "cl";
            char cl_raw[] = "%cl";
            int intel = cfg != NULL && cfg->intel_syntax;

            memset(&count, 0, sizeof(count));
            count.kind = AS_OPERAND_REGISTER;
            count.raw = cl_raw;
            count.u.reg = cl;
            ops[intel ? 0 : 1] = st->u.instr.operands[0];
            ops[intel ? 1 : 2] = st->u.instr.operands[1];
            ops[intel ? 2 : 0] = count;
            bare = *st;
            bare.u.instr.operands = ops;
            bare.u.instr.operand_count = 3;
            return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
        }
        /* `imul $2, %eax`: the register is multiplied and is where the
         * product goes. */
        if (st->u.instr.operand_count == 2 && strcmp(base, "imul") == 0) {
            int intel = cfg != NULL && cfg->intel_syntax;
            const as_operand_t *imm = &st->u.instr.operands[intel ? 1 : 0];
            const as_operand_t *reg = &st->u.instr.operands[intel ? 0 : 1];

            if (imm->kind == AS_OPERAND_IMMEDIATE && imm->raw != NULL && (intel || imm->raw[0] == '$') &&
                reg->kind == AS_OPERAND_REGISTER) {
                as_operand_t ops[3];

                ops[intel ? 2 : 0] = *imm;
                ops[1] = *reg;
                ops[intel ? 0 : 2] = *reg;
                bare = *st;
                bare.u.instr.operands = ops;
                bare.u.instr.operand_count = 3;
                return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
        if (suffix != '\0' && x86_suffix_denied_by_register(&st->u.instr, base, suffix, cfg != NULL && cfg->intel_syntax,
                                                            encerr, encerr_sz)) {
            return -1;
        }
        /* `movsbl %ax, %ebx`: the two letters are the sizes of what is
         * extended and of what it is extended to. */
        {
            const char *mn = st->u.instr.mnemonic;

            if (strlen(mn) == 6 && strncasecmp(mn, "mov", 3) == 0 && strchr("szSZ", mn[3]) != NULL &&
                strchr("bwlBWL", mn[4]) != NULL && strchr("wlqWLQ", mn[5]) != NULL &&
                st->u.instr.operand_count == 2 && (cfg == NULL || !cfg->intel_syntax)) {
                unsigned sizes[2];
                int k;

                sizes[0] = tolower((unsigned char)mn[4]) == 'b' ? 8u : (tolower((unsigned char)mn[4]) == 'w' ? 16u : 32u);
                sizes[1] = tolower((unsigned char)mn[5]) == 'w' ? 16u : (tolower((unsigned char)mn[5]) == 'l' ? 32u : 64u);
                for (k = 0; k < 2; ++k) {
                    const as_operand_t *op = &st->u.instr.operands[k];
                    const as_x86_reg_info_t *r = op->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(op->u.reg) : NULL;

                    if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && r->width != sizes[k]) {
                        snprintf(encerr, encerr_sz, "'%s' takes a register of %u bits there, and %%%s is of %u", mn,
                                 sizes[k], r->name, (unsigned)r->width);
                        return -1;
                    }
                }
            }
        }
    }

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION || (st->u.instr.prefixes & AS_PREFIX_DATA16) == 0) {
        int rc = encode_x86_stmt_either(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);

        if (rc == 0) {
            x86_shorten_accumulator_move(cfg, st, code, code_len);
            rc = x86_addr16_prefix(cfg, st, code, code_cap, code_len);
        }

        /*
         * xacquire and xrelease: some of the emitters write a lock and
         * no repeat prefix, having no instruction that repeats, and the
         * byte was left out -- `xacquire lock cmpxchg` was `lock
         * cmpxchg`.  It goes before the lock, where GNU as has it.
         */
        if (rc == 0 && st != NULL && st->kind == AS_STMT_INSTRUCTION && (st->u.instr.prefixes & AS_PREFIX_HLE) != 0) {
            unsigned char want = (st->u.instr.prefixes & AS_PREFIX_REPNE) != 0 ? 0xf2 : 0xf3;
            size_t at = 0;
            int have = 0;

            for (i = 0; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                          code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                          code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
                have |= code[i] == want;
                if (code[i] != 0xf0 && code[i] != 0x66) {
                    at = i + 1;
                }
            }
            if (!have) {
                if (*code_len + 1 > code_cap) {
                    return -1;
                }
                memmove(code + at + 1, code + at, *code_len - at);
                code[at] = want;
                (*code_len)++;
            }
        }
        return rc;
    }
    bare = *st;
    bare.u.instr.prefixes &= ~AS_PREFIX_DATA16;
    if (encode_x86_stmt_either(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz) != 0) {
        return -1;
    }
    for (i = 0; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                  code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                  code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
        if (code[i] == 0x66) {
            snprintf(encerr, encerr_sz, "data16 before '%s', which has the prefix already", st->u.instr.mnemonic);
            return -1;
        }
    }
    x86_shorten_accumulator_move(cfg, st, code, code_len);
    if (x86_prepend_opsize(code, code_cap, code_len) != 0) {
        return -1;
    }
    return x86_addr16_prefix(cfg, st, code, code_cap, code_len);
}

static int eval_local_rel_expr_virtual(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st, uint64_t base_off,
                                       unsigned x86_code_bits, const as_expr_t *e, long long *out);

static unsigned directive_fixed_scalar_width(const as_directive_t *d) {
    if (d == NULL || d->name == NULL) {
        return 0;
    }
    if (strcmp(d->name, ".byte") == 0) return 1;
    if (strcmp(d->name, ".word") == 0 || strcmp(d->name, ".short") == 0 ||
        strcmp(d->name, ".hword") == 0 || strcmp(d->name, ".2byte") == 0) return 2;
    if (strcmp(d->name, ".long") == 0 || strcmp(d->name, ".4byte") == 0) return 4;
    if (strcmp(d->name, ".quad") == 0 || strcmp(d->name, ".8byte") == 0) return 8;
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
                    matches = (st->labels[j].name != NULL && strcmp(st->labels[j].name, sym_name) == 0);
                } else {
                    int label_digit = -1;
                    matches = (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 &&
                               label_digit == digit && st->labels[j].line == line &&
                               st->labels[j].file != NULL && file != NULL &&
                               strcmp(st->labels[j].file, file) == 0);
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
            const uint64_t *pfx;
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
                matches = (st->labels[j].name != NULL && strcmp(st->labels[j].name, sym_name) == 0);
            } else {
                int label_digit = -1;
                matches = (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 &&
                       label_digit == digit &&
                       st->labels[j].line == line &&
                       st->labels[j].file != NULL &&
                       strcmp(st->labels[j].file, file) == 0);
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
                strcmp(st->labels[j].file, file) != 0) {
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
                    st->labels[j].file != NULL && strcmp(st->labels[j].file, file) == 0) {
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
                    strcmp(st->labels[j].file, file) == 0) {
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
                    strcmp(st->labels[j - 1].file, file) == 0) {
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

static int eval_local_rel_expr_virtual(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st, uint64_t base_off,
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
        if (e->symbol != NULL && strcmp(e->symbol, ".") == 0) {
            *out = 0;
            return 0;
        }
        if (e->symbol == NULL || e->reloc_op != NULL ||
            find_label_virtual_location(ctx, base_st, NULL, 0, 0, e->symbol,
                                        &target_loc.section,
                                        &target_off) != 0 ||
            strcmp(target_loc.section, section_name) != 0) {
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
                strcmp(target_loc.section, section_name) == 0) {
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
                strcmp(target_loc.section, section_name) != 0) {
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

        if (e->symbol == NULL || e->reloc_op != NULL || strcmp(e->symbol, ".") == 0) {
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
                if (strcmp(l.section, r.section) != 0) {
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

static int eval_abs_local_diff_expr_virtual(emit_ctx_t *ctx, const as_stmt_t *base_st,
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

static int eval_linux_alt_pad_expr(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
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
        strcmp(v744f.section, v743f.section) != 0 ||
        section_name == NULL || strcmp(section_name, v740b.section) != 0) {
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

static int eval_linux_alt_len_expr(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, long long *out) {
    virtual_addr_value_t a;
    virtual_addr_value_t b;

    if (ctx == NULL || st == NULL || arg == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(arg, "742b-740b") == 0) {
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
            !a.has_section || !b.has_section || strcmp(a.section, b.section) != 0) {
            return -1;
        }
        *out = a.value - b.value;
        return 0;
    }
    if (strcmp(arg, "744f-743f") == 0) {
        if (numeric_local_virtual_location(ctx, st, 744, 1, &a) != 0 ||
            numeric_local_virtual_location(ctx, st, 743, 1, &b) != 0 ||
            !a.has_section || !b.has_section || strcmp(a.section, b.section) != 0) {
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

static int x86_cond_code(const char *mnemonic, unsigned *cc_out) {
    static const struct {
        const char *name;
        unsigned cc;
    } map[] = {
        {"jo", 0x0},   {"jno", 0x1}, {"jb", 0x2},  {"jnae", 0x2}, {"jc", 0x2},   {"jnb", 0x3},
        {"jae", 0x3},  {"jnc", 0x3}, {"je", 0x4},  {"jz", 0x4},   {"jne", 0x5},  {"jnz", 0x5},
        {"jbe", 0x6},  {"jna", 0x6}, {"ja", 0x7},  {"jnbe", 0x7}, {"js", 0x8},   {"jns", 0x9},
        {"jp", 0xa},   {"jpe", 0xa}, {"jnp", 0xb}, {"jpo", 0xb},  {"jl", 0xc},   {"jnge", 0xc},
        {"jge", 0xd},  {"jnl", 0xd}, {"jle", 0xe}, {"jng", 0xe},  {"jg", 0xf},   {"jnle", 0xf},
    };
    size_t i;

    if (mnemonic == NULL || cc_out == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (streq_ci(mnemonic, map[i].name)) {
            *cc_out = map[i].cc;
            return 0;
        }
    }
    return -1;
}

static int emit_resolved_x86_branch_ex(const as_elf_cfg_t *cfg, const as_stmt_t *st, uint64_t sec_off,
                                       long long abs_target, int allow_short, unsigned char *code, size_t code_cap,
                                       size_t *code_len, char *encerr, size_t encerr_sz) {
    char mnbuf[32];
    char suffix = '\0';
    unsigned cc = 0;
    long long disp8;

    if (cfg == NULL || st == NULL || code == NULL || code_len == NULL ||
        normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        return -1;
    }
    *code_len = 0;
    if (x86_stmt_refused(cfg, st, encerr, encerr_sz)) {
        return -1;
    }
    suffix = x86_branch_size_letter(st, suffix, cfg->is_64 ? 64u : cfg->x86_code_bits);
    if (streq_ci(mnbuf, "call")) {
        /* A call of 16 bits -- `callw`, or `call` in 16-bit code -- has
         * two bytes for its distance and pushes two; where that is not
         * the mode's own size the prefix says so.  Each was the 32-bit
         * call whatever was written. */
        int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;
        int op16 = suffix == 'w' || (code16 && suffix != 'l');
        size_t n = 0;
        size_t len = (size_t)(op16 != code16) + 1u + (op16 ? 2u : 4u);
        long long disp = abs_target - ((long long)sec_off + (long long)len);

        if ((op16 ? (disp < -32768 || disp > 32767) : (disp < INT32_MIN || disp > INT32_MAX)) || code_cap < len) {
            snprintf(encerr, encerr_sz, "call target out of range");
            return -1;
        }
        if (op16 != code16) {
            code[n++] = 0x66;
        }
        code[n++] = 0xe8;
        code[n++] = (unsigned char)((uint32_t)disp & 0xffu);
        code[n++] = (unsigned char)(((uint32_t)disp >> 8) & 0xffu);
        if (!op16) {
            code[n++] = (unsigned char)(((uint32_t)disp >> 16) & 0xffu);
            code[n++] = (unsigned char)(((uint32_t)disp >> 24) & 0xffu);
        }
        *code_len = n;
        return 0;
    }
    if (streq_ci(mnbuf, "xbegin")) {
        /* Where a transaction goes if it is abandoned: a distance of
         * the mode's size from the instruction's end.  (No branch that
         * could be resolved, it was said to be, in either mode.) */
        int op16 = !cfg->is_64 && cfg->x86_code_bits == 16u;
        size_t len = op16 ? 4u : 6u;
        long long disp = abs_target - ((long long)sec_off + (long long)len);
        size_t n = 0;

        if ((op16 ? (disp < -32768 || disp > 32767) : (disp < INT32_MIN || disp > INT32_MAX)) || code_cap < len) {
            snprintf(encerr, encerr_sz, "xbegin target out of range");
            return -1;
        }
        code[n++] = 0xc7;
        code[n++] = 0xf8;
        code[n++] = (unsigned char)((uint32_t)disp & 0xffu);
        code[n++] = (unsigned char)(((uint32_t)disp >> 8) & 0xffu);
        if (!op16) {
            code[n++] = (unsigned char)(((uint32_t)disp >> 16) & 0xffu);
            code[n++] = (unsigned char)(((uint32_t)disp >> 24) & 0xffu);
        }
        *code_len = n;
        return 0;
    }
    {
        int is_jmp = streq_ci(mnbuf, "jmp");

        if (is_jmp || x86_cond_code(mnbuf, &cc) == 0) {
            /*
             * A jump: one byte of distance where that reaches, and
             * where it does not, two or four by the size it is of --
             * the code's, or the one a `w`, an `l` or `data16` gives
             * it, behind the prefix.  (The size written was not asked:
             * `data16 jmp` to a label was `jmp`.)
             */
            const char *what = is_jmp ? "jump" : "conditional branch";
            int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;
            int op16 = suffix == 'w' || (code16 && suffix != 'l');
            size_t pre = x86_branch_flip16(st) ? 1u : 0u;
            size_t n = 0;
            size_t len;
            long long disp;

            disp8 = abs_target - ((long long)sec_off + 2 + (long long)pre);
            if (allow_short && disp8 >= -128 && disp8 <= 127) {
                if (code_cap < 2 + pre) {
                    return -1;
                }
                if (pre) {
                    code[n++] = 0x66;
                }
                code[n++] = is_jmp ? 0xeb : (unsigned char)(0x70u | cc);
                code[n++] = (unsigned char)(signed char)disp8;
                *code_len = n;
                return 0;
            }
            if (is_jmp && suffix == 'b') {
                snprintf(encerr, encerr_sz, "jump target out of range");
                return -1;
            }
            len = (size_t)(op16 != code16) + (is_jmp ? 1u : 2u) + (op16 ? 2u : 4u);
            disp = abs_target - ((long long)sec_off + (long long)len);
            if ((op16 ? (disp < -32768 || disp > 32767) : (disp < INT32_MIN || disp > INT32_MAX)) || code_cap < len) {
                snprintf(encerr, encerr_sz, "%s target out of range", what);
                return -1;
            }
            if (op16 != code16) {
                code[n++] = 0x66;
            }
            if (is_jmp) {
                code[n++] = 0xe9;
            } else {
                code[n++] = 0x0f;
                code[n++] = (unsigned char)(0x80u | cc);
            }
            code[n++] = (unsigned char)((uint32_t)disp & 0xffu);
            code[n++] = (unsigned char)(((uint32_t)disp >> 8) & 0xffu);
            if (!op16) {
                code[n++] = (unsigned char)(((uint32_t)disp >> 16) & 0xffu);
                code[n++] = (unsigned char)(((uint32_t)disp >> 24) & 0xffu);
            }
            *code_len = n;
            return 0;
        }
    }
    {
        int need67 = 0;
        unsigned char opcode = 0;
        int kind = x86_fixed_short_branch(st->u.instr.mnemonic, cfg->is_64 ? 64u : cfg->x86_code_bits, &need67,
                                          &opcode);

        if (kind == -2) {
            snprintf(encerr, encerr_sz, "'%s' is not an instruction of %u-bit code", st->u.instr.mnemonic,
                     cfg->is_64 ? 64u : cfg->x86_code_bits);
            return -1;
        }
        /* The address-size prefix, written: it is what the instruction
         * counts in, and `addr16 loop` was `loop`.  One that has the
         * prefix for its own has no other register to count in. */
        if (kind == 0 && (st->u.instr.prefixes & AS_PREFIX_ADDR16) != 0) {
            if (need67) {
                snprintf(encerr, encerr_sz, "'%s' has the address-size prefix already", st->u.instr.mnemonic);
                return -1;
            }
            need67 = 1;
        }
        if (kind == 0) {
            size_t len = need67 ? 3u : 2u;

            disp8 = abs_target - ((long long)sec_off + (long long)len);
            if (disp8 < -128 || disp8 > 127) {
                snprintf(encerr, encerr_sz, "'%s' has only a one-byte displacement, and its target is %lld bytes away",
                         st->u.instr.mnemonic, disp8);
                return -1;
            }
            if (code_cap < len) {
                return -1;
            }
            if (need67) {
                code[0] = 0x67;
            }
            code[len - 2] = opcode;
            code[len - 1] = (unsigned char)(signed char)disp8;
            *code_len = len;
            return 0;
        }
    }
    snprintf(encerr, encerr_sz, "'%s' is not a branch this assembler can resolve", st->u.instr.mnemonic);
    return -1;
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
            if (st->labels[j].name != NULL && strcmp(st->labels[j].name, rel_expr->symbol) == 0) {
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
    if (strcmp(name, ".byte") == 0) {
        return 1;
    }
    if (strcmp(name, ".word") == 0 || strcmp(name, ".short") == 0 ||
        strcmp(name, ".hword") == 0 || strcmp(name, ".2byte") == 0) {
        return 2;
    }
    if (strcmp(name, ".long") == 0 || strcmp(name, ".4byte") == 0) {
        return 4;
    }
    if (strcmp(name, ".quad") == 0 || strcmp(name, ".8byte") == 0) {
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
    return strcmp(name, ".align") == 0 || strcmp(name, ".balign") == 0 ||
           strcmp(name, ".p2align") == 0 || strcmp(name, ".org") == 0;
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
static const uint64_t *get_section_prefix_sums(emit_ctx_t *ctx, const char *section_name,
                                               unsigned x86_code_bits) {
    size_t i;
    section_track_t track;
    uint64_t *prefix;
    uint64_t total;
    if (ctx == NULL || ctx->parsed == NULL || section_name == NULL) {
        return NULL;
    }
    for (i = 0; i < ctx->section_prefix_count; ++i) {
        if (ctx->section_prefixes[i].name != NULL &&
            ctx->section_prefixes[i].x86_code_bits == x86_code_bits &&
            strcmp(ctx->section_prefixes[i].name, section_name) == 0) {
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
    prefix = (uint64_t *)calloc(ctx->parsed->count + 1, sizeof(*prefix));
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
                prefix[i + 1] = total;
                continue;
            }
        }
        if (track.current != NULL && strcmp(track.current, section_name) == 0) {
            size_t n = 0;
            /* (In the mode the statement is in, which a .code16 before
             * it may have made another than the file began in.) */
            if (stmt_virtual_size_in_section(ctx, section_name, st, total, track.x86_code_bits, &n) == 0) {
                total += (uint64_t)n;
            }
        }
        prefix[i + 1] = total;
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
    const uint64_t *prefix;

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
        if (track.current == NULL || strcmp(track.current, section_name) != 0) {
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
        if (track.current == NULL || strcmp(track.current, section_name) != 0) {
            continue;
        }
        if (st->kind == AS_STMT_DIRECTIVE &&
            st->u.directive.name != NULL &&
            (strcmp(st->u.directive.name, ".skip") == 0 ||
             strcmp(st->u.directive.name, ".space") == 0) &&
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
    int c = strcmp(x->name, y->name);

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

        if (strcmp(ctx->label_index[mid].name, name) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == ctx->label_index_count || strcmp(ctx->label_index[lo].name, name) != 0) {
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
            strcmp(target_section, section_name) != 0) {
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
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && strcmp(e->symbol, ".") == 0) {
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
        strcmp(ctx->stmt_section_at[target], section) != 0) {
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
    uint64_t *off = NULL;
    struct relax_at {
        const char *name;
        uint64_t off;
    } *at = NULL;
    size_t at_count = 0, at_cap = 0;
    section_track_t track;
    int tracking = 0;
    int changed = 1;
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
        if (slots == NULL || sid == NULL) {
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
            for (s = h & (cap - 1); slots[s] != 0 && strcmp(at[slots[s] - 1].name, name) != 0; s = (s + 1) & (cap - 1)) {
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
                slots[s] = ++at_count;
            }
            sid[i] = slots[s] - 1;
        }
        free(slots);
    }
    ctx->relax_state = 1;
    ctx->virtual_scanning++;
    while (changed) {
        changed = 0;
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

            off[i] = at[k].off;
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
                    n = 2;
                } else if (ctx->branch_short[i] == 2) {
                    n = relax_long_size(st, track.x86_code_bits);
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
        for (i = 0; i < count; ++i) {
            const as_expr_t *place;
            long long addend;
            long long disp;

            if (ctx->branch_short[i] != 1) {
                continue;
            }
            (void)relax_branch_parts(ctx->parsed->items[i].u.instr.operands[0].u.expr, &place, &addend);
            disp = (long long)off[target[i]] + addend - (long long)(off[i] + 2);
            if (disp < -128 || disp > 127) {
                ctx->branch_short[i] = 2;
                changed = 1;
            }
        }
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
        place->kind != AS_EXPR_SYMBOL || strcmp(place->symbol, ".") != 0) {
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
    if (strcmp(place->symbol, ".") == 0) {
        *abs_target = (long long)sec_off + addend;
        return 0;
    }
    if (addend == 0 ||
        eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, place,
                                        *is_short ? 2 : relax_long_size(st, x86_code_bits), abs_target) != 0) {
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
                (section_name != NULL && strcmp(section_name, ".altinstr_replacement") != 0 &&
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
        lc->section == NULL || strcmp(sec, lc->section) != 0) {
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
static int append_arm_nops(bytebuf_t *buf, size_t need, unsigned target, unsigned code_bits) {
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
            return bytebuf_append_zeros(buf, local_rel_len);
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
        int rc = encode_x86_stmt_by_text(ctx, &stmt_cfg, x86_code_bits, st, code, sizeof(code), &code_len, encerr,
                                         sizeof(encerr));

        if (rc < 0 ||
            (rc == 1 && encode_x86_stmt(ctx, &stmt_cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0)) {
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
        if (strcmp(word, "basic_block") == 0) {
            r->row.flags |= AS_DWLINE_BASIC_BLOCK;
        } else if (strcmp(word, "prologue_end") == 0) {
            r->row.flags |= AS_DWLINE_PROLOGUE_END;
        } else if (strcmp(word, "epilogue_begin") == 0) {
            r->row.flags |= AS_DWLINE_EPILOGUE_BEGIN;
        } else if (strcmp(word, "is_stmt") == 0 || strcmp(word, "isa") == 0 || strcmp(word, "discriminator") == 0) {
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
        } else if (strcmp(word, "view") == 0) {
            *why = "a name after view";
            if (!next_word(d, &arg, &at, word, sizeof(word)) || strlen(word) >= sizeof(r->view)) {
                return -1;
            }
            memcpy(r->view, word, strlen(word) + 1);
            if (strcmp(word, "-0") == 0) {
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
                     strcmp(st->u.directive.name, ".loc") == 0;
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
        if (ctx->line_rows[i].view[0] != '\0' && strcmp(ctx->line_rows[i].view, "-0") != 0 &&
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

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL || strcmp(d->name, ".loc") != 0) {
            continue;
        }
        while (next_word(d, &arg, &at, word, sizeof(word))) {
            if (strcmp(word, "view") == 0 && next_word(d, &arg, &at, word, sizeof(word)) &&
                strcmp(word, "-0") != 0) {
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

        if (strcmp(before->row.section, ctx->line_rows[at].row.section) != 0) {
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
    if (strcmp(d->name, ".uleb128") == 0 || strcmp(d->name, ".sleb128") == 0) {
        return append_leb128_directive(ctx, buf, section_name, st, sec_off, x86_code_bits, d);
    }
    if (ctx != NULL && ctx->virtual_scanning &&
        (strcmp(d->name, ".skip") == 0 || strcmp(d->name, ".space") == 0)) {
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
    if (strcmp(d->name, ".incbin") == 0) {
        return append_incbin_to_bytebuf(ctx, st, buf, d);
    }
    if (strcmp(d->name, ".org") == 0) {
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
    if (strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 || strcmp(d->name, ".p2align") == 0) {
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
        if (strcmp(d->name, ".p2align") == 0) {
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
            return append_x86_nops(buf, need, x86_code_bits, ctx->cfg->machine == EM_X86_64) == 0 ? 1 : -1;
        }
        if (append_align_fill(buf, d, need, (uint64_t)fill) != 0) {
            set_err(ctx, "%s:%u: alignment padding (%lu bytes) is not a multiple of %u",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0,
                    (unsigned long)need, d->fill_width);
            return -1;
        }
        return 1;
    }
    if (strcmp(d->name, ".fill") == 0) {
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
    if (strcmp(d->name, ".byte") == 0) {
        width = 1;
    } else if (strcmp(d->name, ".word") == 0 || strcmp(d->name, ".short") == 0 ||
               strcmp(d->name, ".hword") == 0 || strcmp(d->name, ".2byte") == 0) {
        width = 2;
    } else if (strcmp(d->name, ".long") == 0 || strcmp(d->name, ".4byte") == 0) {
        width = 4;
    } else if (strcmp(d->name, ".quad") == 0 || strcmp(d->name, ".8byte") == 0) {
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
    if (strcmp(d->name, ".zero") == 0 || strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0) {
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
                if ((strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0) && ctx != NULL &&
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
        if (count < 0 && (strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0)) {
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
        if ((strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0) && d->arg_count >= 2) {
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
    return append_directive_data(buf, d);
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
    size_t code_len = 0;

    if (off < 0) {
        off = getenv("AS_NO_STMT_CODE") != NULL;
    }
    if (off || cfg == NULL || st == NULL || cfg->target != 0 || st->kind != AS_STMT_INSTRUCTION ||
        st->u.instr.code_len != 0 || (cfg->machine != EM_386 && cfg->machine != EM_X86_64) ||
        !stmt_is_place_free(st)) {
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
    if (encode_x86_stmt(NULL, &stmt_cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0 ||
        probe.wanted != 0 || code_len == 0 || code_len > sizeof(st->u.instr.code)) {
        return 0;
    }
    as_instruction_drop_operands(&st->u.instr);
    memcpy(st->u.instr.code, code, code_len);
    st->u.instr.code_len = (unsigned char)code_len;
    st->u.instr.code_bits = (unsigned char)code_bits;
    return 1;
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
static size_t text_code_key(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits, char *key, size_t key_sz) {
    const as_instruction_t *in = &st->u.instr;
    size_t n;
    size_t i;
    int w;

    /* (One that is its bytes already has no operands to be the key.) */
    if (ctx->asm_reg_alias_count != 0 || in->code_len != 0 || !stmt_is_place_free(st)) {
        return 0;
    }
    w = snprintf(key, key_sz, "%u %x %x %x %x %d %d %d %d %d %u %s %s", x86_code_bits, in->prefixes, in->pseudo,
                 in->rex_bits, in->opmask, in->have_opmask, in->zeroing, in->broadcast, in->sae, in->rounding_mode,
                 in->syntax_intel, in->segment_override != NULL ? in->segment_override : "-", in->mnemonic);
    if (w < 0 || (size_t)w >= key_sz) {
        return 0;
    }
    n = (size_t)w;
    /* What each operand is, not how it was written: a name that is a
     * number is written the same when the number has been set again. */
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        char mark = op->raw != NULL && (op->raw[0] == '$' || op->raw[0] == '*') ? op->raw[0] : '.';
        long long v = 0;

        if (op->kind == AS_OPERAND_REGISTER) {
            w = snprintf(key + n, key_sz - n, "\t%c%%%s", mark, op->u.reg);
        } else if (op->kind == AS_OPERAND_IMMEDIATE) {
            if (eval_expr_const(op->u.expr, &v) != 0) {
                return 0;
            }
            w = snprintf(key + n, key_sz - n, "\t%c%lld", mark, v);
        } else {
            const as_mem_operand_t *m = &op->u.mem;

            if (m->disp != NULL && eval_expr_const(m->disp, &v) != 0) {
                return 0;
            }
            w = snprintf(key + n, key_sz - n, "\t%c%s:%d%lld(%s,%s,%d)%d", mark,
                         m->segment_reg != NULL ? m->segment_reg : "", m->disp != NULL, v,
                         m->base_reg != NULL ? m->base_reg : "", m->index_reg != NULL ? m->index_reg : "", m->scale,
                         (int)m->size_bits);
        }
        if (w < 0 || (size_t)w >= key_sz - n) {
            return 0;
        }
        n += (size_t)w;
    }
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

static int text_code_find(emit_ctx_t *ctx, const char *key, size_t n, unsigned char *code, size_t code_cap,
                          size_t *code_len) {
    uint32_t h = text_code_hash(key, n);
    size_t at;

    if (ctx->text_code_cap == 0) {
        return 0;
    }
    for (at = h & (ctx->text_code_cap - 1); ctx->text_code[at].key != NULL; at = (at + 1) & (ctx->text_code_cap - 1)) {
        const struct text_code *t = &ctx->text_code[at];

        if (t->hash == h && strcmp(t->key, key) == 0) {
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
static void text_code_keep(emit_ctx_t *ctx, const char *key, size_t n, const unsigned char *code, size_t code_len) {
    uint32_t h = text_code_hash(key, n);
    struct text_code *t;
    size_t at;

    if (code_len == 0 || code_len > sizeof(t->bytes)) {
        return;
    }
    if ((ctx->text_code_count + 1) * 2 > ctx->text_code_cap) {
        size_t ncap = ctx->text_code_cap == 0 ? 1024 : ctx->text_code_cap * 2;
        struct text_code *next = (struct text_code *)calloc(ncap, sizeof(*next));
        size_t i;

        if (next == NULL) {
            return;
        }
        for (i = 0; i < ctx->text_code_cap; ++i) {
            if (ctx->text_code[i].key != NULL) {
                for (at = ctx->text_code[i].hash & (ncap - 1); next[at].key != NULL; at = (at + 1) & (ncap - 1)) {
                }
                next[at] = ctx->text_code[i];
            }
        }
        free(ctx->text_code);
        ctx->text_code = next;
        ctx->text_code_cap = ncap;
    }
    for (at = h & (ctx->text_code_cap - 1); ctx->text_code[at].key != NULL; at = (at + 1) & (ctx->text_code_cap - 1)) {
    }
    t = &ctx->text_code[at];
    t->key = xstrdup(key);
    if (t->key == NULL) {
        return;
    }
    t->hash = h;
    t->len = (unsigned char)code_len;
    memcpy(t->bytes, code, code_len);
    ctx->text_code_count++;
}

static void text_code_free(emit_ctx_t *ctx) {
    size_t i;

    for (i = 0; i < ctx->text_code_cap; ++i) {
        free(ctx->text_code[i].key);
    }
    free(ctx->text_code);
    ctx->text_code = NULL;
    ctx->text_code_cap = 0;
    ctx->text_code_count = 0;
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
    size_t n;
    int rc;

    if (stmt_code_slot(ctx, st) == NULL) {
        return 1;
    }
    n = text_code_key(ctx, st, x86_code_bits, key, sizeof(key));
    if (n == 0) {
        return 1;
    }
    if (text_code_find(ctx, key, n, code, code_cap, code_len)) {
        return 0;
    }
    rc = encode_x86_stmt(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);
    if (rc == 0) {
        text_code_keep(ctx, key, n, code, *code_len);
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
static int encode_x86_stmt_for_layout(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
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

    if (ctx == NULL || ctx->cfg == NULL || st == NULL) {
        return -1;
    }
    if (ctx->cfg->target != 0 && st->kind == AS_STMT_INSTRUCTION) {
        return arm_encode_stmt(ctx, section_name, sec_off, 1, x86_code_bits, st, code, code_cap, code_len, encerr,
                               encerr_sz);
    }
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
                       strcmp(target.section, section_name) == 0) {
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
    if (strcmp(name, ".") != 0) {
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

static int x86_got_load_kind(const unsigned char *code, size_t len) {
    size_t i = 0;
    int rex = 0;

    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf2 || code[i] == 0xf3 || code[i] == 0x2e ||
                       code[i] == 0x36 || code[i] == 0x3e || code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65)) {
        /* (Of 16 bits it is not one of them: there is nothing for the
         * linker to make it into.) */
        if (code[i] == 0x66) {
            return 0;
        }
        i++;
    }
    if (i < len && (code[i] & 0xf0) == 0x40) {
        rex = 1;
        i++;
    }
    if (i + 1 >= len) {
        return 0;
    }
    switch (code[i]) {
    case 0x8b: /* mov */
    case 0x85: /* test */
    case 0x03: case 0x0b: case 0x13: case 0x1b: case 0x23: case 0x2b: case 0x33: case 0x3b:
        return rex ? 2 : 1;
    case 0xff: /* call, jmp through memory; and, as GNU as has it, push */
        return ((code[i + 1] >> 3) & 7) == 2 || ((code[i + 1] >> 3) & 7) == 4 || ((code[i + 1] >> 3) & 7) == 6
                   ? (rex ? 2 : 1)
                   : 0;
    default:
        return 0;
    }
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
        if (machine == EM_386 && strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0 && fallback == R_386_32) {
            return R_386_GOTPC;
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
        if (strcmp(ctx->sym_map[idx].name, name) == 0) {
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
    if (strcmp(s->alias_target, s->name) == 0) {
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

static int emit_data_program(emit_ctx_t *ctx, const as_data_program_t *data) {
    sec_buf_vec_t secbufs;
    section_track_t track;
    size_t i;
    int trc;

    (void)data;
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

        if (st->kind != AS_STMT_DIRECTIVE || strcmp(st->u.directive.name, ".file") != 0 ||
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
            if (strcmp(seen[k].name, end) != 0) {
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
        if (strcmp(st->u.directive.name, ".file") == 0 || strcmp(st->u.directive.name, ".loc") == 0) {
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
        if (strcmp(d->name, ".loc") == 0) {
            return 1;
        }
        /* A file with a number is of a line table the source means to
         * write, whether it has got to a row of it or not: a compiler's
         * output for a function with no line, and what GCC's configure
         * asks -g of. */
        if (strcmp(d->name, ".file") == 0 && d->arg_count != 0 && d->args[0] != NULL &&
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
            (strcmp(d->name, ".section") == 0 || strcmp(d->name, ".pushsection") == 0) && d->arg_count != 0 &&
            d->args[0] != NULL &&
            (strcmp(d->args[0], ".debug_line") == 0 || strcmp(d->args[0], ".debug_info") == 0)) {
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

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL || strcmp(d->name, ".file") != 0 ||
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
        if (next_word(d, &arg, &at, word, sizeof(word)) && strcmp(word, "md5") != 0) {
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
            if (strcmp(ends[k].section, rows[i].section) == 0) {
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
        if (strcmp(st->u.directive.name, ".cfi_sections") != 0) {
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
        if (strcmp(d->name, ".file") == 0 && d->arg_count == 1) {
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
        } else if (strcmp(d->name, ".ident") == 0) {
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
        if (locs[i].file != NULL && locs[i].line == line && strcmp(locs[i].file, file) == 0) {
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
        if (strcmp(locs[hit].name, name) == 0) {
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
        if (strcmp((*locs)[hit].name, name) == 0) {
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
                    strcmp(base_sym->alias_target, s->size_target_symbol) == 0) {
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
                    if (strcmp(ctx->cfg->thumb_funcs[tf], s->name) == 0) {
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
static const char *first_symbol_op_in_expr(const as_expr_t *e) {
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

static char *trim_copy_arg(const char *s) {
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

static int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out) {
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

typedef struct {
    char *name;
    unsigned type;
    unsigned flags;
    unsigned align;
    int touched;
    /* For padding where instructions go: the mode the code is in, and
     * whether what was last put in the section was data. */
    unsigned code_bits;
    int after_data;
    bytebuf_t buf;
} bin_section_t;

typedef struct {
    bin_section_t *items;
    size_t count;
    size_t cap;
} bin_section_vec_t;

static void bin_section_vec_free(bin_section_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
        free(v->items[i].buf.data);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

static int bin_section_vec_push(bin_section_vec_t *v, const char *name, unsigned type, unsigned flags, unsigned align) {
    bin_section_t *next;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        next = (bin_section_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    memset(&v->items[v->count], 0, sizeof(v->items[v->count]));
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return -1;
    }
    v->items[v->count].type = type;
    v->items[v->count].flags = flags;
    v->items[v->count].align = align > 0 ? align : 1;
    v->count++;
    return 0;
}

static bin_section_t *bin_section_find(bin_section_vec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i].name, name) == 0) {
            return &v->items[i];
        }
    }
    return NULL;
}

static int bin_sections_init(bin_section_vec_t *v, const as_section_state_t *sections) {
    size_t i;

    if (v == NULL) {
        return -1;
    }
    memset(v, 0, sizeof(*v));
    if (sections == NULL) {
        return 0;
    }
    for (i = 0; i < sections->count; ++i) {
        const as_section_t *s = &sections->items[i];
        bin_section_t *cur = bin_section_find(v, s->name);
        if (cur == NULL) {
            if (bin_section_vec_push(v, s->name, s->type, s->flags, s->align) != 0) {
                return -1;
            }
            continue;
        }
        if (s->align > cur->align) {
            cur->align = s->align;
        }
        cur->flags |= s->flags;
        if (s->type == SHT_NOBITS) {
            cur->type = SHT_NOBITS;
        }
    }
    return 0;
}

static bin_section_t *bin_section_get_or_add(bin_section_vec_t *v, const char *name) {
    bin_section_t *sec;
    unsigned type = SHT_PROGBITS;
    unsigned flags = 0;
    unsigned align = 1;

    sec = bin_section_find(v, name);
    if (sec != NULL) {
        return sec;
    }
    if (strcmp(name, ".text") == 0) {
        type = SHT_PROGBITS;
        flags = SHF_ALLOC | SHF_EXECINSTR;
        align = 16;
    } else if (strcmp(name, ".rodata") == 0) {
        type = SHT_PROGBITS;
        flags = SHF_ALLOC;
        align = 4;
    } else if (strcmp(name, ".data") == 0) {
        type = SHT_PROGBITS;
        flags = SHF_ALLOC | SHF_WRITE;
        align = 4;
    } else if (strcmp(name, ".bss") == 0) {
        type = SHT_NOBITS;
        flags = SHF_ALLOC | SHF_WRITE;
        align = 4;
    }
    if (bin_section_vec_push(v, name, type, flags, align) != 0) {
        return NULL;
    }
    return &v->items[v->count - 1];
}

static int parse_nonneg_u64_or_reloc(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg,
                                     const char *what, unsigned long long *out) {
    long long v;
    char *sym = NULL;
    int64_t add = 0;

    if (parse_int64(arg, &v) == 0 || parse_const_expr_string(arg, &v) == 0) {
        if (v < 0) {
            set_err(ctx, "%s:%u: %s must be non-negative", st->file != NULL ? st->file : "<input>", st->line, what);
            return -1;
        }
        *out = (unsigned long long)v;
        return 0;
    }
    if (parse_symbol_addend_arg(arg, &sym, &add) == 0 && sym != NULL) {
        set_err(ctx, "%s:%u: unresolved relocation to '%s' not allowed with -O binary",
                st->file != NULL ? st->file : "<input>", st->line, sym);
        free(sym);
        return -1;
    }
    set_err(ctx, "%s:%u: invalid %s argument: %s", st->file != NULL ? st->file : "<input>", st->line, what, arg);
    free(sym);
    return -1;
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

static int append_incbin_to_bytebuf(emit_ctx_t *ctx, const as_stmt_t *st, bytebuf_t *buf, const as_directive_t *d) {
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

static int append_incbin_bytes(emit_ctx_t *ctx, const as_stmt_t *st, bin_section_t *sec, const as_directive_t *d) {
    if (append_incbin_to_bytebuf(ctx, st, &sec->buf, d) < 0) {
        return -1;
    }
    sec->touched = 1;
    return 0;
}

static int append_data_directive_binary(emit_ctx_t *ctx, const as_stmt_t *st, bin_section_t *sec, int *handled) {
    const as_directive_t *d;
    size_t i;

    if (ctx == NULL || st == NULL || sec == NULL || handled == NULL || st->kind != AS_STMT_DIRECTIVE) {
        return -1;
    }
    d = &st->u.directive;
    *handled = 0;

    if (strcmp(d->name, ".incbin") == 0) {
        *handled = 1;
        return append_incbin_bytes(ctx, st, sec, d);
    }
    if (strcmp(d->name, ".ascii") == 0 || strcmp(d->name, ".asciz") == 0 || strcmp(d->name, ".string") == 0) {
        int nul = (strcmp(d->name, ".ascii") == 0) ? 0 : 1;
        *handled = 1;
        for (i = 0; i < d->arg_count; ++i) {
            const char *s = d->args[i] != NULL ? d->args[i] : "";
            char *bytes = NULL;
            size_t len = 0;
            if (as_decode_string_literal(s, &bytes, &len) != 0) {
                set_err(ctx, "%s:%u: malformed string literal", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            if (bytebuf_append(&sec->buf, bytes, len) != 0) {
                free(bytes);
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            if (nul && bytebuf_append_zeros(&sec->buf, 1) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
        }
        sec->touched = 1;
        return 0;
    }
    if (strcmp(d->name, ".float") == 0 || strcmp(d->name, ".double") == 0) {
        int is_double = strcmp(d->name, ".double") == 0;
        *handled = 1;
        for (i = 0; i < d->arg_count; ++i) {
            char *tmp = trim_copy_arg(d->args[i]);
            char *end;
            double dv;
            if (tmp == NULL || tmp[0] == '\0') {
                free(tmp);
                set_err(ctx, "%s:%u: malformed %s argument", st->file != NULL ? st->file : "<input>", st->line, d->name);
                return -1;
            }
            dv = strtod(tmp, &end);
            if (end == tmp || *end != '\0') {
                char *sym = NULL;
                int64_t add = 0;
                if (parse_symbol_addend_arg(d->args[i], &sym, &add) == 0 && sym != NULL) {
                    set_err(ctx, "%s:%u: unresolved relocation to '%s' not allowed with -O binary",
                            st->file != NULL ? st->file : "<input>", st->line, sym);
                    free(sym);
                } else {
                    set_err(ctx, "%s:%u: malformed %s argument", st->file != NULL ? st->file : "<input>", st->line,
                            d->name);
                }
                free(tmp);
                return -1;
            }
            free(tmp);
            if (is_double) {
                if (bytebuf_append(&sec->buf, &dv, sizeof(dv)) != 0) {
                    set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                    return -1;
                }
            } else {
                float fv = (float)dv;
                if (bytebuf_append(&sec->buf, &fv, sizeof(fv)) != 0) {
                    set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                    return -1;
                }
            }
        }
        sec->touched = 1;
        return 0;
    }
    if (strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 || strcmp(d->name, ".p2align") == 0) {
        unsigned long long raw;
        unsigned long long fill = (sec->flags & SHF_EXECINSTR) != 0 ? 0x90 : 0;
        size_t align;
        size_t need;
        *handled = 1;
        if (d->arg_count < 1 || parse_nonneg_u64_or_reloc(ctx, st, d->args[0], d->name, &raw) != 0) {
            return -1;
        }
        if (directive_has_arg(d, 1) && parse_nonneg_u64_or_reloc(ctx, st, d->args[1], ".align fill", &fill) != 0) {
            return -1;
        }
        if (strcmp(d->name, ".p2align") == 0) {
            if (raw >= (unsigned long long)(sizeof(size_t) * CHAR_BIT - 1)) {
                set_err(ctx, "%s:%u: .p2align exponent too large", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            align = (size_t)1u << (unsigned)raw;
        } else {
            align = (size_t)raw;
        }
        if (align == 0 || (align & (align - 1)) != 0) {
            set_err(ctx, "%s:%u: alignment must be a power of two", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        need = (align - (sec->buf.len & (align - 1))) & (align - 1);
        switch (align_exceeds_max_skip(d, need)) {
        case 1: need = 0; break;
        case 0: break;
        default:
            set_err(ctx, "%s:%u: %s: the maximum to skip is not a number",
                    st->file != NULL ? st->file : "<input>", st->line, d->name);
            return -1;
        }
        /* Where instructions go, padding that is NOPs -- none named, or
         * 0x90 named -- is the NOPs the object writer pads with: this
         * wrote 0x90 for each byte. */
        if ((sec->flags & SHF_EXECINSTR) != 0 && ctx->cfg != NULL &&
            (ctx->cfg->target != 0 ? !directive_has_arg(d, 1)
                                   : fill == 0x90 && !(d->fill_width > 1 && directive_has_arg(d, 1)))) {
            int rc;

            if (ctx->cfg->target != 0) {
                rc = append_arm_nops(&sec->buf, need, ctx->cfg->target, sec->code_bits);
            } else {
                rc = 0;
                if (sec->after_data && need != 0) {
                    rc = bytebuf_append_u64_le(&sec->buf, 0x90, 1);
                    need--;
                }
                if (rc == 0) {
                    rc = append_x86_nops(&sec->buf, need, sec->code_bits, ctx->cfg->machine == EM_X86_64);
                }
            }
            if (rc != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            sec->touched = 1;
            sec->after_data = 0;
            return 0;
        }
        switch (append_align_fill(&sec->buf, d, need, fill)) {
        case 0:
            break;
        case -2:
            set_err(ctx, "%s:%u: alignment padding (%lu bytes) is not a multiple of %u",
                    st->file != NULL ? st->file : "<input>", st->line, (unsigned long)need, d->fill_width);
            return -1;
        default:
            set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        sec->touched = 1;
        return 0;
    }
    if (strcmp(d->name, ".org") == 0) {
        unsigned long long off;
        *handled = 1;
        if (d->arg_count < 1 || parse_nonneg_u64_or_reloc(ctx, st, d->args[0], ".org offset", &off) != 0) {
            return -1;
        }
        if ((size_t)off < sec->buf.len) {
            set_err(ctx, "%s:%u: backward .org is not supported in binary mode",
                    st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        if ((size_t)off > sec->buf.len && bytebuf_append_zeros(&sec->buf, (size_t)off - sec->buf.len) != 0) {
            set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        sec->touched = 1;
        return 0;
    }
    if (strcmp(d->name, ".zero") == 0 || strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0) {
        unsigned long long n;
        unsigned long long fill = 0;
        long long signed_n = 0;
        size_t k;
        *handled = 1;
        if (d->arg_count < 1) {
            set_err(ctx, "%s:%u: %s requires count argument", st->file != NULL ? st->file : "<input>", st->line, d->name);
            return -1;
        }
        if (eval_linux_alt_pad_expr(ctx, sec->name, st, sec->buf.len,
                                    ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u,
                                    d->args[0], &signed_n) == 0) {
            n = signed_n < 0 ? 0 : (unsigned long long)signed_n;
        } else if (parse_nonneg_u64_or_reloc(ctx, st, d->args[0], d->name, &n) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->file : NULL,
                                                  st != NULL ? st->line : 0);
            if (expr != NULL &&
                       (eval_abs_local_diff_expr_virtual(ctx, st, expr, &signed_n) == 0 ||
                        eval_local_rel_expr_virtual(ctx, sec->name, st, sec->buf.len,
                                                    ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u,
                                                    expr, &signed_n) == 0)) {
                n = signed_n < 0 ? 0 : (unsigned long long)signed_n;
            } else {
                as_expr_free(expr);
                return -1;
            }
            as_expr_free(expr);
        }
        if (n > (1ULL << 20)) {
            set_err(ctx, "%s:%u: %s count is unreasonably large: %llu",
                    st->file != NULL ? st->file : "<input>", st->line, d->name, n);
            return -1;
        }
        if ((strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0) && d->arg_count >= 2) {
            long long fv = 0;
            if (parse_int64(d->args[1], &fv) != 0 && parse_const_expr_string(d->args[1], &fv) != 0) {
                return -1;
            }
            fill = (unsigned long long)fv;
        }
        for (k = 0; k < (size_t)n; ++k) {
            if (bytebuf_append_u64_le(&sec->buf, fill, 1) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
        }
        sec->touched = 1;
        return 0;
    }
    if (strcmp(d->name, ".fill") == 0) {
        unsigned long long repeat = 0;
        unsigned long long size = 1;
        unsigned long long value = 0;
        long long repeat_expr = 0;
        *handled = 1;
        if (d->arg_count < 1) {
            return -1;
        }
        if (parse_nonneg_u64_or_reloc(ctx, st, d->args[0], ".fill repeat", &repeat) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->file : NULL,
                                                  st != NULL ? st->line : 0);
            if (expr == NULL ||
                (eval_abs_local_diff_expr_virtual(ctx, st, expr, &repeat_expr) != 0 &&
                 eval_local_rel_expr_virtual(ctx, sec->name, st, sec->buf.len,
                                             ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u, expr,
                                             &repeat_expr) != 0) ||
                repeat_expr < 0) {
                as_expr_free(expr);
                return -1;
            }
            as_expr_free(expr);
            repeat = (unsigned long long)repeat_expr;
        }
        if (d->arg_count >= 2 && parse_nonneg_u64_or_reloc(ctx, st, d->args[1], ".fill size", &size) != 0) {
            return -1;
        }
        if (d->arg_count >= 3 && parse_nonneg_u64_or_reloc(ctx, st, d->args[2], ".fill value", &value) != 0) {
            return -1;
        }
        if (repeat > (1ULL << 20)) {
            set_err(ctx, "%s:%u: .fill repeat is unreasonably large: %llu",
                    st->file != NULL ? st->file : "<input>", st->line, repeat);
            return -1;
        }
        if (size == 0 || size > 8) {
            set_err(ctx, "%s:%u: .fill size must be in [1,8]", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        for (i = 0; i < (size_t)repeat; ++i) {
            if (bytebuf_append_u64_le(&sec->buf, value, (unsigned)size) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
        }
        sec->touched = 1;
        return 0;
    }
    if (strcmp(d->name, ".byte") == 0 || strcmp(d->name, ".word") == 0 || strcmp(d->name, ".short") == 0 ||
        strcmp(d->name, ".hword") == 0 || strcmp(d->name, ".2byte") == 0 || strcmp(d->name, ".long") == 0 ||
        strcmp(d->name, ".int") == 0 || strcmp(d->name, ".4byte") == 0 || strcmp(d->name, ".quad") == 0 ||
        strcmp(d->name, ".8byte") == 0) {
        unsigned width = 1;
        *handled = 1;
        if (strcmp(d->name, ".word") == 0 || strcmp(d->name, ".short") == 0 || strcmp(d->name, ".hword") == 0 ||
            strcmp(d->name, ".2byte") == 0) {
            width = 2;
        } else if (strcmp(d->name, ".long") == 0 || strcmp(d->name, ".int") == 0 || strcmp(d->name, ".4byte") == 0) {
            width = 4;
        } else if (strcmp(d->name, ".quad") == 0 || strcmp(d->name, ".8byte") == 0) {
            width = 8;
        }
        for (i = 0; i < d->arg_count; ++i) {
            long long v;
            if (eval_linux_alt_len_expr(ctx, st, d->args[i], &v) != 0 &&
                parse_int64(d->args[i], &v) != 0 &&
                parse_const_expr_string(d->args[i], &v) != 0 &&
                eval_arg_asm_vars(ctx, st, d->args[i], &v) != 0) {
                char *sym = NULL;
                int64_t add = 0;
                as_expr_t *expr = as_parse_expr_string(d->args[i], st != NULL ? st->file : NULL,
                                                       st != NULL ? st->line : 0);
                if (expr != NULL &&
                    (eval_abs_local_diff_expr_virtual(ctx, st, expr, &v) == 0 ||
                     eval_local_rel_expr_virtual(ctx, sec->name, st, sec->buf.len + (uint64_t)(i * width),
                                                 ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u,
                                                 expr, &v) == 0)) {
                    as_expr_free(expr);
                } else if (parse_symbol_addend_arg(d->args[i], &sym, &add) == 0 && sym != NULL) {
                    as_expr_free(expr);
                    free(sym);
                    v = 0;
                } else if (expr != NULL && expr_has_symbol(expr)) {
                    as_expr_free(expr);
                    v = 0;
                } else {
                    as_expr_free(expr);
                    set_err(ctx, "%s:%u: malformed %s argument", st->file != NULL ? st->file : "<input>", st->line,
                            d->name);
                    return -1;
                }
            }
            if (bytebuf_append_u64_le(&sec->buf, (uint64_t)v, width) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
        }
        sec->touched = 1;
        return 0;
    }

    return 0;
}

static int is_binary_metadata_directive(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, ".globl") == 0 || strcmp(name, ".global") == 0 || strcmp(name, ".local") == 0 ||
        strcmp(name, ".weak") == 0 || strcmp(name, ".type") == 0 || strcmp(name, ".size") == 0 ||
        strcmp(name, ".hidden") == 0 || strcmp(name, ".protected") == 0 || strcmp(name, ".internal") == 0 ||
        strcmp(name, ".symver") == 0 || strcmp(name, ".comm") == 0 || strcmp(name, ".lcomm") == 0 ||
        strcmp(name, ".file") == 0 || strcmp(name, ".loc") == 0 || strcmp(name, ".ident") == 0) {
        return 1;
    }
    if (strncmp(name, ".cfi_", 5) == 0) {
        return 1;
    }
    return 0;
}

static int write_binary_file(emit_ctx_t *ctx, const bin_section_vec_t *v, const char *out_path) {
    static const char *const first_order[] = {".text", ".rodata", ".data"};
    bytebuf_t out;
    size_t i;
    unsigned char *used;
    FILE *fp;

    memset(&out, 0, sizeof(out));
    used = (unsigned char *)calloc(v->count, 1);
    if (used == NULL) {
        return -1;
    }

    for (i = 0; i < sizeof(first_order) / sizeof(first_order[0]); ++i) {
        size_t j;
        for (j = 0; j < v->count; ++j) {
            const bin_section_t *s = &v->items[j];
            size_t align;
            size_t need;
            if (used[j] || strcmp(s->name, first_order[i]) != 0) {
                continue;
            }
            if (s->buf.len == 0 && !s->touched) {
                used[j] = 1;
                continue;
            }
            align = s->align > 0 ? s->align : 1;
            if ((align & (align - 1)) != 0) {
                align = 1;
            }
            need = (align - (out.len & (align - 1))) & (align - 1);
            if (need > 0 && bytebuf_append_zeros(&out, need) != 0) {
                free(used);
                free(out.data);
                return -1;
            }
            if (s->type == SHT_NOBITS) {
                if (s->buf.len > 0 && bytebuf_append_zeros(&out, s->buf.len) != 0) {
                    free(used);
                    free(out.data);
                    return -1;
                }
            } else if (s->buf.len > 0 && bytebuf_append(&out, s->buf.data, s->buf.len) != 0) {
                free(used);
                free(out.data);
                return -1;
            }
            used[j] = 1;
        }
    }

    for (i = 0; i < v->count; ++i) {
        const bin_section_t *s = &v->items[i];
        size_t align;
        size_t need;
        if (used[i] || strcmp(s->name, ".bss") == 0) {
            continue;
        }
        if (s->buf.len == 0 && !s->touched) {
            used[i] = 1;
            continue;
        }
        align = s->align > 0 ? s->align : 1;
        if ((align & (align - 1)) != 0) {
            align = 1;
        }
        need = (align - (out.len & (align - 1))) & (align - 1);
        if (need > 0 && bytebuf_append_zeros(&out, need) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        if (s->type == SHT_NOBITS) {
            if (s->buf.len > 0 && bytebuf_append_zeros(&out, s->buf.len) != 0) {
                free(used);
                free(out.data);
                return -1;
            }
        } else if (s->buf.len > 0 && bytebuf_append(&out, s->buf.data, s->buf.len) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        used[i] = 1;
    }

    for (i = 0; i < v->count; ++i) {
        const bin_section_t *s = &v->items[i];
        size_t align;
        size_t need;
        if (used[i] || strcmp(s->name, ".bss") != 0) {
            continue;
        }
        if (s->buf.len == 0 && !s->touched) {
            used[i] = 1;
            continue;
        }
        align = s->align > 0 ? s->align : 1;
        if ((align & (align - 1)) != 0) {
            align = 1;
        }
        need = (align - (out.len & (align - 1))) & (align - 1);
        if (need > 0 && bytebuf_append_zeros(&out, need) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        if (s->buf.len > 0 && bytebuf_append_zeros(&out, s->buf.len) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        used[i] = 1;
    }

    free(used);
    fp = fopen(out_path, "wb");
    if (fp == NULL) {
        free(out.data);
        set_err(ctx, "can't create %s: %s", out_path, strerror(errno));
        return -1;
    }
    if (out.len > 0 && fwrite(out.data, 1, out.len, fp) != out.len) {
        fclose(fp);
        free(out.data);
        set_err(ctx, "failed to write output file: %s", out_path);
        return -1;
    }
    fclose(fp);
    free(out.data);
    return 0;
}

int as_elf_emit_binary_file(const as_parse_result_t *parsed,
                            const as_section_state_t *sections,
                            const as_elf_cfg_t *cfg,
                            const char *out_path,
                            char *errbuf,
                            size_t errbuf_sz) {
    emit_ctx_t ctx;
    bin_section_vec_t sv;
    const char *cur_section = ".text";
    unsigned cur_code_bits;
    size_t i;

    if (parsed == NULL || cfg == NULL || out_path == NULL) {
        return -1;
    }
    if (cfg->target != 0 || !(cfg->machine == EM_386 || cfg->machine == EM_X86_64)) {
        if (errbuf != NULL && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "a flat image (-O binary) is written for i386 and x86-64 only");
        }
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
    if (bin_sections_init(&sv, sections) != 0) {
        set_err(&ctx, "failed to initialize section layout");
        return -1;
    }
    cur_code_bits = cfg->is_64 ? 64u : (cfg->x86_code_bits == 16u ? 16u : 32u);

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        bin_section_t *sec = bin_section_get_or_add(&sv, cur_section);
        if (sec == NULL) {
            set_err(&ctx, "out of memory");
            goto fail;
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            const as_directive_t *d = &st->u.directive;
            const char *next = section_from_directive(d);
            int handled = 0;

            if (next != NULL) {
                cur_section = next;
                continue;
            }
            if (strcmp(d->name, ".code16") == 0) {
                cur_code_bits = 16u;
                continue;
            }
            if (strcmp(d->name, ".code32") == 0) {
                cur_code_bits = 32u;
                continue;
            }
            if (strcmp(d->name, ".code64") == 0) {
                cur_code_bits = 64u;
                continue;
            }
            if (is_binary_metadata_directive(d->name)) {
                set_err(&ctx, "%s:%u: ELF metadata directive %s is not supported with -O binary",
                        st->file != NULL ? st->file : "<input>", st->line, d->name);
                goto fail;
            }
            {
                size_t before = sec->buf.len;
                int aligns = strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 ||
                             strcmp(d->name, ".p2align") == 0;

                sec->code_bits = cur_code_bits;
                if (append_data_directive_binary(&ctx, st, sec, &handled) != 0) {
                    goto fail;
                }
                if (!aligns && sec->buf.len != before) {
                    sec->after_data = 1;
                }
            }
            (void)handled;
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION) {
            unsigned char code[32];
            size_t code_len = 0;
            char encerr[256];

            if (sec->type == SHT_NOBITS) {
                set_err(&ctx, "%s:%u: instructions are not allowed in SHT_NOBITS section %s",
                        st->file != NULL ? st->file : "<input>", st->line, sec->name);
                goto fail;
            }
            if (instruction_has_symbolic_reloc(&st->u.instr)) {
                set_err(&ctx, "%s:%u: unresolved relocation in instruction is not allowed with -O binary",
                        st->file != NULL ? st->file : "<input>", st->line);
                goto fail;
            }
            if (encode_x86_stmt_for_layout(&ctx, sec->name, (uint64_t)sec->buf.len, cur_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
                set_err(&ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, encerr);
                goto fail;
            }
            if (bytebuf_append(&sec->buf, code, code_len) != 0) {
                set_err(&ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                goto fail;
            }
            sec->touched = 1;
            sec->after_data = 0;
        }
    }

    if (write_binary_file(&ctx, &sv, out_path) != 0) {
        goto fail;
    }
    bin_section_vec_free(&sv);
    return 0;

fail:
    bin_section_vec_free(&sv);
    return -1;
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

/*
 * How wide the immediate at the end of this encoded instruction is, and
 * whether the operation is a 64-bit one (REX.W) that sign-extends it.
 * -1 for an instruction this does not know the immediate of.
 *
 * A symbolic immediate's relocation goes on that field and is that wide.
 * The width was guessed from the operand as written -- four bytes, or
 * eight when anything about the instruction was 64 bits and the code was
 * long enough to hold eight -- so `addq $sym, 8(%rsp)` had eight bytes of
 * relocation written over all of it but the REX, and `addw $sym, %ax`
 * four over its opcode.
 */
/*
 * The width of an instruction's immediate, read from its bytes.  The
 * 66 prefix says the other operand size than the code's: 16 bits in
 * 32- and 64-bit code, 32 in 16-bit code -- where it had been read as
 * it is in the others, so that `movw $sym, %ax` was thought to have
 * four bytes and was refused for having two, and `movl $sym, %eax`
 * had a relocation of two on its four.
 */
static int x86_imm_field_width(const unsigned char *code, size_t len, unsigned code_bits,
                               unsigned *width_out, int *rex_w_out, unsigned char *opcode_out) {
    int is_64 = code_bits == 64u;
    size_t i = 0;
    int has_66 = 0;
    int rex_w = 0;
    unsigned char op;
    unsigned width;

    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 ||
                       code[i] == 0xf3 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                       code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65)) {
        if (code[i] == 0x66) {
            has_66 = 1;
        }
        i++;
    }
    if (is_64 && i < len && (code[i] & 0xf0u) == 0x40u) {
        rex_w = (code[i] & 0x08u) != 0;
        i++;
    }
    if (i >= len) {
        return -1;
    }
    if (code_bits == 16u) {
        has_66 = !has_66;
    }
    op = code[i];
    if (op == 0x80 || op == 0xc6 || op == 0xf6 || op == 0xa8 || (op >= 0xb0 && op <= 0xb7) ||
        (op & 0xc7u) == 0x04u) {
        width = 1;
    } else if (op >= 0xb8 && op <= 0xbf) {
        width = rex_w ? 8u : (has_66 ? 2u : 4u);
    } else if (op == 0x81 || op == 0xc7 || op == 0xf7 || op == 0xa9 || op == 0x68 || op == 0x69 ||
               (op & 0xc7u) == 0x05u) {
        width = has_66 ? 2u : 4u;
    } else {
        return -1;
    }
    if (len - i <= width) {
        return -1;
    }
    *width_out = width;
    *rex_w_out = rex_w;
    *opcode_out = op;
    return 0;
}

/*
 * Where the four-byte displacement of this encoded instruction is: read
 * from its own prefixes, opcode, ModRM and SIB.  -1 if it has none.
 *
 * The relocation of a symbolic address goes there.  It was put on the
 * last four bytes of the instruction, which is where the displacement
 * is unless an immediate follows it: `movl $5, var(%rip)` and
 * `cmpl $0, var` had the relocation on the immediate, which was lost,
 * and nothing on the address.
 */
/*
 * The two-byte displacement of an instruction of 32-bit code that
 * addresses through 16-bit registers: a 67 prefix, and then ModRM by
 * the 16-bit table, which has no SIB byte.  0 and its place, or -1.
 */
/* How many of an instruction's first bytes are prefixes, a REX among
 * them in 64-bit code: what its opcode is behind. */
static size_t x86_prefix_count(const unsigned char *code, size_t len, int is_64) {
    size_t i = 0;

    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3 ||
                       code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e || code[i] == 0x26 || code[i] == 0x64 ||
                       code[i] == 0x65)) {
        i++;
    }
    if (is_64 && i < len && (code[i] & 0xf0u) == 0x40u) {
        i++;
    }
    return i;
}

static int x86_disp16_offset(const unsigned char *code, size_t len, int code16, size_t *off_out) {
    size_t i = 0;
    /* Of 16 bits in 16-bit code, and in 32-bit code behind the prefix;
     * the prefix in 16-bit code makes it of 32. */
    int addr16 = code16;
    unsigned mod;
    unsigned rm;

    /* A wait before an x87 instruction is not part of it. */
    if (len > 1 && code[0] == 0x9b) {
        i = 1;
    }
    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 ||
                       code[i] == 0xf3 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                       code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65)) {
        if (code[i] == 0x67) {
            addr16 = !code16;
        }
        i++;
    }
    if (!addr16 || i >= len) {
        return -1;
    }
    /* A move between the accumulator and an address with no ModRM:
     * the address is what follows the opcode. */
    if (code[i] >= 0xa0 && code[i] <= 0xa3) {
        if (i + 3 != len) {
            return -1;
        }
        *off_out = i + 1;
        return 0;
    }
    /* VEX and EVEX, which in 32-bit code are told from les, lds and
     * bound by the two top bits of the next byte. */
    if (i + 1 < len && code[i] == 0xc5 && (code[i + 1] & 0xc0u) == 0xc0u) {
        i += 2;
    } else if (i + 1 < len && code[i] == 0xc4 && (code[i + 1] & 0xc0u) == 0xc0u) {
        i += 3;
    } else if (i + 1 < len && code[i] == 0x62 && (code[i + 1] & 0xc0u) == 0xc0u) {
        i += 4;
    } else if (code[i] == 0x0f) {
        i++;
        if (i < len && (code[i] == 0x38 || code[i] == 0x3a)) {
            i++;
        }
    }
    i++;
    if (i >= len) {
        return -1;
    }
    mod = code[i] >> 6;
    rm = code[i] & 7u;
    i++;
    if (!(mod == 2u || (mod == 0u && rm == 6u)) || i + 2 > len) {
        return -1;
    }
    *off_out = i;
    return 0;
}

static int x86_64_disp_reloc_is_unsigned(const unsigned char *code, size_t len, const char *mnemonic) {
    size_t p = 0;
    int addr32 = 0;

    while (p < len && (code[p] == 0x26 || code[p] == 0x2e || code[p] == 0x36 || code[p] == 0x3e ||
                       code[p] == 0x64 || code[p] == 0x65 || code[p] == 0x66 || code[p] == 0x67 ||
                       code[p] == 0xf0 || code[p] == 0xf2 || code[p] == 0xf3)) {
        if (code[p] == 0x67) {
            addr32 = 1;
        }
        ++p;
    }
    if (addr32) {
        return 1;
    }
    if (mnemonic != NULL && strncasecmp(mnemonic, "lea", 3) == 0) {
        return !(p < len && (code[p] & 0xf8) == 0x48);
    }
    return 0;
}

/*
 * Whether the bytes are a far jump or call with its target written out
 * -- EA or 9A, an offset of two bytes or of four, and a segment of two
 * -- and where the offset begins.
 */
static int x86_far_pointer_offset(const unsigned char *code, size_t len, size_t *off_out) {
    size_t p = 0;

    while (p < len && (code[p] == 0x66 || code[p] == 0x67)) {
        ++p;
    }
    if (p >= len || (code[p] != 0xea && code[p] != 0x9a) || (len - p != 5 && len - p != 7)) {
        return 0;
    }
    *off_out = p + 1;
    return 1;
}

static int x86_disp32_offset(const unsigned char *code, size_t len, int is_64, size_t *off_out) {
    size_t i = 0;
    unsigned char modrm;
    unsigned mod;
    unsigned rm;

    /* A wait before an x87 instruction is not part of it. */
    if (len > 1 && code[0] == 0x9b) {
        i = 1;
    }
    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 ||
                       code[i] == 0xf3 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                       code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65)) {
        /* In 32-bit code 67 is 16-bit addressing, which has another
         * ModRM table and a two-byte displacement.  In 64-bit code it
         * is 32-bit addressing, laid out as 64-bit is. */
        if (code[i] == 0x67 && !is_64) {
            return -1;
        }
        i++;
    }
    if (i + 1 >= len) {
        return -1;
    }
    /* VEX and EVEX: in 32-bit code the same bytes are les, lds and bound
     * unless the next has its two top bits set. */
    if (code[i] == 0xc5 && (is_64 || (code[i + 1] & 0xc0u) == 0xc0u)) {
        i += 3;
    } else if (code[i] == 0xc4 && (is_64 || (code[i + 1] & 0xc0u) == 0xc0u)) {
        i += 4;
    } else if (code[i] == 0x62 && (is_64 || (code[i + 1] & 0xc0u) == 0xc0u)) {
        i += 5;
    } else {
        if (is_64 && (code[i] & 0xf0u) == 0x40u) {
            i++;
        }
        if (i < len && code[i] == 0x0f) {
            i++;
            if (i < len && (code[i] == 0x38 || code[i] == 0x3a)) {
                i++;
            }
        } else if (i < len && code[i] >= 0xa0 && code[i] <= 0xa3) {
            /* mov between the accumulator and an address with no ModRM. */
            if (is_64 || i + 5 > len) {
                return -1;
            }
            *off_out = i + 1;
            return 0;
        }
        i++;
    }
    if (i >= len) {
        return -1;
    }
    modrm = code[i++];
    mod = modrm >> 6;
    rm = modrm & 7u;
    if (mod == 3u) {
        return -1;
    }
    if (rm == 4u) {
        unsigned char sib;

        if (i >= len) {
            return -1;
        }
        sib = code[i++];
        if (mod == 0u && (sib & 7u) != 5u) {
            return -1;
        }
        if (mod == 1u) {
            return -1;
        }
    } else if (!((mod == 0u && rm == 5u) || mod == 2u)) {
        return -1;
    }
    if (i + 4 > len) {
        return -1;
    }
    *off_out = i;
    return 0;
}

/*
 * The same in 16-bit code, where an address of 32 bits is behind the
 * prefix that is one of 16 in 32-bit code: the instruction less that
 * prefix is laid out as 32-bit code's is.  (Without this the four
 * bytes were looked for at the instruction's end, and `addr32 bt $1,
 * sym` had its relocation over the count.)
 */
static int x86_disp32_offset_code16(const unsigned char *code, size_t len, size_t *off_out) {
    unsigned char tmp[32];
    size_t at = 0;
    size_t i;

    if (len == 0 || len > sizeof(tmp)) {
        return -1;
    }
    for (i = 0; i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 ||
                            code[i] == 0xf3 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                            code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x9b); ++i) {
        if (code[i] == 0x67) {
            break;
        }
    }
    if (i >= len || code[i] != 0x67) {
        return -1;
    }
    at = i;
    memcpy(tmp, code, at);
    memcpy(tmp + at, code + at + 1, len - at - 1);
    if (x86_disp32_offset(tmp, len - 1, 0, off_out) != 0) {
        return -1;
    }
    *off_out += 1;
    return 0;
}

static void adjust_x86_rel_reloc_to_encoding(unsigned machine, const as_instruction_t *in,
                                             const as_operand_t *op, const unsigned char *code,
                                             size_t code_len, uint32_t *type, uint64_t *width) {
    if (type == NULL || width == NULL || in == NULL || op == NULL || code == NULL) {
        return;
    }
    if (!(machine == EM_386 || machine == EM_X86_64)) {
        return;
    }
    if (!(op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) ||
        !is_rel_mnemonic(in->mnemonic) || code_len == 0) {
        return;
    }

    /* The prefixes are no part of the shape: `data16 jmp sym`, `ds
     * call sym` and `callw sym` in 64-bit code had the relocation of a
     * distance of four bytes, which the first and last have not. */
    while (code_len > 2 && (code[0] == 0x26 || code[0] == 0x2e || code[0] == 0x36 || code[0] == 0x3e ||
                            code[0] == 0x64 || code[0] == 0x65 || code[0] == 0x66 || code[0] == 0x67)) {
        code++;
        code_len--;
    }
    if (code_len == 2 &&
        (code[0] == 0xeb || (code[0] >= 0x70 && code[0] <= 0x7f) || (code[0] >= 0xe0 && code[0] <= 0xe3))) {
        *type = (machine == EM_X86_64) ? R_X86_64_PC8 : R_386_PC8;
        *width = 1;
        return;
    }
    if ((code_len == 3 && (code[0] == 0xe8 || code[0] == 0xe9)) ||
        (code_len == 4 && code[0] == 0x0f && (code[1] & 0xf0) == 0x80) ||
        (code_len == 4 && code[0] == 0xc7 && code[1] == 0xf8)) {
        *type = (machine == EM_X86_64) ? R_X86_64_PC16 : R_386_PC16;
        *width = 2;
        return;
    }

    if ((code_len == 5 && (code[0] == 0xe8 || code[0] == 0xe9)) ||
        (code_len == 6 && code[0] == 0xc7 && code[1] == 0xf8) ||
        (code_len == 6 && code[0] == 0x0f && (code[1] & 0xf0) == 0x80)) {
        /* A call of a local label of another section is of a place in
         * that section, not of a function that may be elsewhere. */
        /*
         * Without an `@PLT` of its own, which is read elsewhere: on
         * x86-64 a call or a jump, conditional or not, of a symbol
         * that may be in another object goes by the procedure linkage
         * table, as GNU as has it; on i386 none does.  (It had been
         * the call alone, on both.)
         */
        if (machine == EM_X86_64) {
            /* (A symbol and something added to it is a place, not a
             * procedure: `jmp sym+4` is R_X86_64_PC32.) */
            int bare = in->operand_count == 1 &&
                       (in->operands[0].kind == AS_OPERAND_LABEL_REF ||
                        in->operands[0].kind == AS_OPERAND_IMMEDIATE) &&
                       in->operands[0].u.expr != NULL && in->operands[0].u.expr->kind == AS_EXPR_SYMBOL;

            *type = (in->target_in_other_section == 1 || !bare) ? R_X86_64_PC32 : R_X86_64_PLT32;
        } else {
            *type = R_386_PC32;
        }
        *width = 4;
        return;
    }

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
            strcmp(cur->file, ref->src_file) != 0) {
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
        strcmp(lin.add_symbol, ".") == 0 || strcmp(lin.sub_symbol, ".") == 0) {
        return -1;
    }

    /* Where the subtracted label is: it must be here. */
    if (find_seen_temp_label(labels, label_count, lin.sub_symbol, &seen) == 0) {
        if (strcmp(seen->section, cur_section) != 0) {
            return -1;
        }
        off = seen->off;
    } else if (find_label_virtual_location(ctx, st, NULL, 0, 0, lin.sub_symbol,
                                           &section, &off) != 0 ||
               strcmp(section, cur_section) != 0) {
        return -1;
    }

    /* And x must not be: two labels of one section differ by a number,
     * which the directive has already been given. */
    seen = NULL;
    if (find_seen_temp_label(labels, label_count, lin.add_symbol, &seen) == 0) {
        if (strcmp(seen->section, cur_section) == 0) {
            return -1;
        }
    } else {
        uint64_t add_off = 0;

        if (find_label_virtual_location(ctx, st, NULL, 0, 0, lin.add_symbol,
                                        &section, &add_off) == 0 &&
            strcmp(section, cur_section) == 0) {
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

    while (slots[at] != 0 && strcmp(labels[slots[at] - 1].name, labels[i].name) != 0) {
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

            if (strcmp(cur->name, name) == 0) {
                *out = cur;
                return 0;
            }
        }
        return -1;
    }
    for (i = count; i > 0; --i) {
        const local_emit_label_t *cur = &labels[i - 1];
        if (cur->name != NULL && strcmp(cur->name, name) == 0) {
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
        if (v->t[i].is_section == term->is_section && strcmp(v->t[i].base, term->base) == 0 &&
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
        if (strcmp(e->symbol, ".") == 0) {
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
        } else if (v->t[i].k == -1 && v->t[i].is_section && strcmp(v->t[i].base, ev->section) == 0 && !*pcrel) {
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
            if (strcmp(d->name, ".byte") == 0) width = 1;
            else if (strcmp(d->name, ".word") == 0 || strcmp(d->name, ".short") == 0 ||
                     strcmp(d->name, ".hword") == 0 || strcmp(d->name, ".2byte") == 0) width = 2;
            else if (strcmp(d->name, ".long") == 0 || strcmp(d->name, ".4byte") == 0) width = 4;
            else if (strcmp(d->name, ".quad") == 0 || strcmp(d->name, ".8byte") == 0) width = 8;

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
            if ((uint64_t)sb->buf.len > cur_off && strcmp(d->name, ".align") != 0 &&
                strcmp(d->name, ".balign") != 0 && strcmp(d->name, ".p2align") != 0) {
                sb->after_data = strcmp(d->name, ".org") != 0;
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
                                strcmp(expr->rhs->symbol, ".") == 0) {
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
                                       strcmp(minuend->symbol, ".") != 0) {
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
                               (strcmp(lin.add_symbol, ".") == 0 || strcmp(lin.sub_symbol, ".") == 0)) {
                        /*
                         * With `.`, which is where the instruction
                         * begins: `. - label` and `label - .` are
                         * numbers, and `ext - .` is a distance.
                         */
                        long long here = (long long)cur_off;
                        const char *other = strcmp(lin.add_symbol, ".") == 0 ? lin.sub_symbol : lin.add_symbol;
                        int dot_is_taken = strcmp(lin.sub_symbol, ".") == 0;
                        as_expr_linear_t at;
                        const char *label_section = "";
                        uint64_t label_off = 0;

                        if (strcmp(other, ".") == 0) {
                            is_label_difference = 1;
                            label_difference = lin.value;
                        } else if (as_expr_eval_linear(e, reloc_dot_lookup, &here, &at) != AS_EXPR_EVAL_OK) {
                            taken_from = lin.sub_symbol;
                        } else if (find_label_virtual_location(ctx, NULL, NULL, 0, 0, other, &label_section,
                                                               &label_off) == 0 &&
                                   track.current != NULL && strcmp(label_section, track.current) == 0) {
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
                               strcmp(lin.add_symbol, ".") == 0 && lin.sub_symbol == NULL && lin.add_local == NULL &&
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
                        lin.add_symbol != NULL && strcmp(lin.add_symbol, sym) == 0 &&
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
                            lin.add_symbol != NULL && strcmp(lin.add_symbol, sym) == 0 && lin.sub_symbol != NULL &&
                            lin.sub_local == NULL && lin.add_local == NULL &&
                            find_label_virtual_location(ctx, NULL, NULL, 0, 0, lin.sub_symbol, &label_section,
                                                        &label_off) == 0 &&
                            track.current != NULL && strcmp(label_section, track.current) == 0) {
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
                if (machine == EM_386 && t == R_386_32 && strcmp(sym, "_GLOBAL_OFFSET_TABLE_") == 0 &&
                    code_len >= reloc_width) {
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

                    if (reloc_width != 4 ||
                        find_label_virtual_location(ctx, NULL, NULL, 0, 0, taken_from, &label_section,
                                                    &label_off) != 0 ||
                        track.current == NULL || strcmp(label_section, track.current) != 0) {
                        set_err(ctx, "%s:%u: '%s' is taken from '%s' and is not a label of this section",
                                st->file != NULL ? st->file : "<input>", st->line, taken_from, sym);
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    t = machine == EM_X86_64 ? R_X86_64_PC32 : R_386_PC32;
                    addend = (int64_t)taken_value + (int64_t)(reloc_off - label_off);
                }
                if (dot_relative) {
                    if (reloc_width != 4) {
                        set_err(ctx, "%s:%u: '%s' less '.' does not fit the %u bytes it has in '%s'",
                                st->file != NULL ? st->file : "<input>", st->line, sym, (unsigned)reloc_width,
                                st->u.instr.mnemonic);
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    /* (`.` was taken as the section's offset of the
                     * instruction, and the field is further on.) */
                    t = machine == EM_X86_64 ? R_X86_64_PC32 : R_386_PC32;
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
} far_label_t;

static int far_label_cmp(const void *a, const void *b) {
    const far_label_t *x = (const far_label_t *)a;
    const far_label_t *y = (const far_label_t *)b;

    if ((x->name == NULL) != (y->name == NULL)) {
        return x->name == NULL ? -1 : 1;
    }
    if (x->name != NULL) {
        return strcmp(x->name, y->name);
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
            label_count++;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            const as_directive_t *d = &st->u.directive;

            /* What another file may see, or replace, is not the
             * source's own. */
            if (strcmp(d->name, ".globl") == 0 || strcmp(d->name, ".global") == 0 || strcmp(d->name, ".weak") == 0) {
                for (j = 0; j < d->arg_count; ++j) {
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
    near_names = (char **)calloc(label_count != 0 ? label_count : 1, sizeof(*near_names));
    if (near_names == NULL) {
        goto out;
    }
    for (i = 0; i < label_count; ++i) {
        const char *name = labels[i].name;

        /* (Sorted by name, so one defined twice is twice together.) */
        if (name == NULL || is_local_temp_symbol_name(name) ||
            (near_name_count != 0 && strcmp(near_names[near_name_count - 1], name) == 0) ||
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

        if (!as_parse_is_direct_branch(st)) {
            continue;
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
            (strcmp(st->u.directive.name, ".uleb128") == 0 || strcmp(st->u.directive.name, ".sleb128") == 0)) {
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
            (strcmp(st->u.directive.name, ".uleb128") == 0 || strcmp(st->u.directive.name, ".sleb128") == 0)) {
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
                     const as_data_program_t *data,
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

    if (parsed == NULL || sections == NULL || symtab == NULL || data == NULL || cfg == NULL || out_path == NULL) {
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

        if (strcmp(s->name, ".text") == 0 && s->subsection == 0) {
            ctx.text_sec = es;
        }
        if (strcmp(s->name, ".data") == 0 && s->subsection == 0) {
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

    if (emit_data_program(&ctx, data) != 0) {
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
    text_code_free(&ctx);
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
