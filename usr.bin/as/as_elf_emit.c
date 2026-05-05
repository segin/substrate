#include "as_elf_emit.h"
#include "as_x86_encode.h"
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

#include "elfobj.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef STV_DEFAULT
#define STV_DEFAULT 0
#define STV_INTERNAL 1
#define STV_HIDDEN 2
#define STV_PROTECTED 3
#endif

#ifndef R_ARM_ABS32
#define R_ARM_ABS32 2
#endif
#ifndef R_AARCH64_ABS64
#define R_AARCH64_ABS64 257
#endif

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
} virtual_label_cache_t;

typedef struct {
    char *name;
    long long value;
} asm_var_t;

typedef struct {
    char *name;
    char *reg;
} asm_reg_alias_t;

typedef struct {
    const as_elf_cfg_t *cfg;
    const as_parse_result_t *parsed;
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
    asm_var_t *asm_vars;
    size_t asm_var_count;
    size_t asm_var_cap;
    asm_reg_alias_t *asm_reg_aliases;
    size_t asm_reg_alias_count;
    size_t asm_reg_alias_cap;
    int virtual_scanning;
    /* Per-statement size cache for the conservative virtual-size path.
     * Linux's syscall_32.s drove ~13k stmts × thousands of branch range
     * queries, each redoing the full encode_x86_stmt for every entry —
     * an O(N^2) blow-up that wedged the build for minutes. Sizes
     * computed below the precise (top-level) sizing path are
     * offset-independent, so we memoise them by parsed-stmt index. */
    size_t *stmt_size_cache;
    unsigned char *stmt_size_cached;
    size_t stmt_size_cache_count;
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

typedef struct {
    int has_section;
    char section[128];
    long long value;
} virtual_addr_value_t;

static const char *first_symbol_in_expr(const as_expr_t *e);
static int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out);
static as_x86_seg_t map_seg(const char *s);
static int emit_seg_override_byte(unsigned char *out, size_t out_cap, size_t *pos_io, const char *segment_reg);
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
static int stmt_range_size_in_section(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                      uint64_t sec_off, unsigned x86_code_bits, uint64_t *size_out);
static int linux_alt_original_range_size(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                         unsigned x86_code_bits, uint64_t *size_out);
static int stmt_declared_label_section(emit_ctx_t *ctx, const as_stmt_t *target_st,
                                       char *section_out, size_t section_out_sz);
static int eval_direct_local_branch_target(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st,
                                           uint64_t base_off, unsigned x86_code_bits, const as_expr_t *rel_expr,
                                           size_t branch_len, long long *abs_target_out);
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
static char *xstrdup(const char *s);
static int is_local_temp_symbol_name(const char *name);
static int expr_is_local_temp_symbol(const as_expr_t *e);

static void set_err(emit_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

static int is_local_temp_symbol_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return name[0] == '.' && name[1] == 'L';
}

static int expr_is_local_temp_symbol(const as_expr_t *e) {
    return e != NULL && e->kind == AS_EXPR_SYMBOL && is_local_temp_symbol_name(e->symbol);
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

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
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

static int streq_ci(const char *a, const char *b) {
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; a[i] != '\0' && b[i] != '\0'; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return 0;
        }
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int startswith_ci(const char *s, const char *prefix) {
    size_t i;

    if (s == NULL || prefix == NULL) {
        return 0;
    }
    for (i = 0; prefix[i] != '\0'; ++i) {
        char cs = s[i];
        char cp = prefix[i];
        if (cs == '\0') {
            return 0;
        }
        if (cs >= 'A' && cs <= 'Z') {
            cs = (char)(cs + ('a' - 'A'));
        }
        if (cp >= 'A' && cp <= 'Z') {
            cp = (char)(cp + ('a' - 'A'));
        }
        if (cs != cp) {
            return 0;
        }
    }
    return 1;
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

static int parse_int64(const char *s, long long *out) {
    char *end;
    long long v;

    if (s == NULL || out == NULL) {
        return -1;
    }
    v = strtoll(s, &end, 0);
    if (end == s || *end != '\0') {
        return -1;
    }
    *out = v;
    return 0;
}

typedef struct {
    const char *s;
    size_t i;
} const_expr_parser_t;

static void const_expr_skip_ws(const_expr_parser_t *p) {
    while (p != NULL && p->s[p->i] != '\0' && isspace((unsigned char)p->s[p->i])) {
        ++p->i;
    }
}

static int const_expr_parse_or(const_expr_parser_t *p, long long *out);

static int const_expr_parse_number(const_expr_parser_t *p, long long *out) {
    const char *start;
    char *end;
    long long v;

    if (p == NULL || out == NULL) {
        return -1;
    }
    const_expr_skip_ws(p);
    start = p->s + p->i;
    if (*start == '\0') {
        return -1;
    }
    v = strtoll(start, &end, 0);
    if (end == start) {
        return -1;
    }
    p->i += (size_t)(end - start);
    *out = v;
    return 0;
}

static int const_expr_parse_primary(const_expr_parser_t *p, long long *out) {
    long long v;

    if (p == NULL || out == NULL) {
        return -1;
    }
    const_expr_skip_ws(p);
    if (p->s[p->i] == '(') {
        ++p->i;
        if (const_expr_parse_or(p, &v) != 0) {
            return -1;
        }
        const_expr_skip_ws(p);
        if (p->s[p->i] != ')') {
            return -1;
        }
        ++p->i;
        *out = v;
        return 0;
    }
    return const_expr_parse_number(p, out);
}

static int const_expr_parse_unary(const_expr_parser_t *p, long long *out) {
    if (p == NULL || out == NULL) {
        return -1;
    }
    const_expr_skip_ws(p);
    if (p->s[p->i] == '+') {
        ++p->i;
        return const_expr_parse_unary(p, out);
    }
    if (p->s[p->i] == '-') {
        ++p->i;
        if (const_expr_parse_unary(p, out) != 0) {
            return -1;
        }
        *out = -*out;
        return 0;
    }
    if (p->s[p->i] == '~') {
        ++p->i;
        if (const_expr_parse_unary(p, out) != 0) {
            return -1;
        }
        *out = ~*out;
        return 0;
    }
    return const_expr_parse_primary(p, out);
}

static int const_expr_parse_mul(const_expr_parser_t *p, long long *out) {
    long long lhs;

    if (const_expr_parse_unary(p, &lhs) != 0) {
        return -1;
    }
    for (;;) {
        long long rhs;
        const_expr_skip_ws(p);
        if (p->s[p->i] == '*') {
            ++p->i;
            if (const_expr_parse_unary(p, &rhs) != 0) {
                return -1;
            }
            lhs *= rhs;
            continue;
        }
        if (p->s[p->i] == '/') {
            ++p->i;
            if (const_expr_parse_unary(p, &rhs) != 0 || rhs == 0) {
                return -1;
            }
            lhs /= rhs;
            continue;
        }
        if (p->s[p->i] == '%') {
            ++p->i;
            if (const_expr_parse_unary(p, &rhs) != 0 || rhs == 0) {
                return -1;
            }
            lhs %= rhs;
            continue;
        }
        break;
    }
    *out = lhs;
    return 0;
}

static int const_expr_parse_add(const_expr_parser_t *p, long long *out) {
    long long lhs;

    if (const_expr_parse_mul(p, &lhs) != 0) {
        return -1;
    }
    for (;;) {
        long long rhs;
        const_expr_skip_ws(p);
        if (p->s[p->i] == '+') {
            ++p->i;
            if (const_expr_parse_mul(p, &rhs) != 0) {
                return -1;
            }
            lhs += rhs;
            continue;
        }
        if (p->s[p->i] == '-') {
            ++p->i;
            if (const_expr_parse_mul(p, &rhs) != 0) {
                return -1;
            }
            lhs -= rhs;
            continue;
        }
        break;
    }
    *out = lhs;
    return 0;
}

static int const_expr_parse_shift(const_expr_parser_t *p, long long *out) {
    long long lhs;

    if (const_expr_parse_add(p, &lhs) != 0) {
        return -1;
    }
    for (;;) {
        long long rhs;
        const_expr_skip_ws(p);
        if (p->s[p->i] == '<' && p->s[p->i + 1] == '<') {
            p->i += 2;
            if (const_expr_parse_add(p, &rhs) != 0) {
                return -1;
            }
            lhs <<= (rhs & 63);
            continue;
        }
        if (p->s[p->i] == '>' && p->s[p->i + 1] == '>') {
            p->i += 2;
            if (const_expr_parse_add(p, &rhs) != 0) {
                return -1;
            }
            lhs >>= (rhs & 63);
            continue;
        }
        break;
    }
    *out = lhs;
    return 0;
}

static int const_expr_parse_and(const_expr_parser_t *p, long long *out) {
    long long lhs;

    if (const_expr_parse_shift(p, &lhs) != 0) {
        return -1;
    }
    while (p->s[p->i] == '&' && p->s[p->i + 1] != '&') {
        long long rhs;
        ++p->i;
        if (const_expr_parse_shift(p, &rhs) != 0) {
            return -1;
        }
        lhs &= rhs;
        const_expr_skip_ws(p);
    }
    *out = lhs;
    return 0;
}

static int const_expr_parse_xor(const_expr_parser_t *p, long long *out) {
    long long lhs;

    if (const_expr_parse_and(p, &lhs) != 0) {
        return -1;
    }
    while (p->s[p->i] == '^') {
        long long rhs;
        ++p->i;
        if (const_expr_parse_and(p, &rhs) != 0) {
            return -1;
        }
        lhs ^= rhs;
        const_expr_skip_ws(p);
    }
    *out = lhs;
    return 0;
}

static int const_expr_parse_or(const_expr_parser_t *p, long long *out) {
    long long lhs;

    if (const_expr_parse_xor(p, &lhs) != 0) {
        return -1;
    }
    while (p->s[p->i] == '|' && p->s[p->i + 1] != '|') {
        long long rhs;
        ++p->i;
        if (const_expr_parse_xor(p, &rhs) != 0) {
            return -1;
        }
        lhs |= rhs;
        const_expr_skip_ws(p);
    }
    *out = lhs;
    return 0;
}

static int parse_const_expr_string(const char *s, long long *out) {
    const_expr_parser_t p;

    if (s == NULL || out == NULL) {
        return -1;
    }
    p.s = s;
    p.i = 0;
    if (const_expr_parse_or(&p, out) != 0) {
        return -1;
    }
    const_expr_skip_ws(&p);
    return p.s[p.i] == '\0' ? 0 : -1;
}

static int eval_expr_const(const as_expr_t *e, long long *out) {
    long long l;
    long long r;

    if (e == NULL || out == NULL) {
        return -1;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        *out = e->value;
        return 0;
    case AS_EXPR_UNARY:
        if (eval_expr_const(e->lhs, &l) != 0) {
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
        if (eval_expr_const(e->lhs, &l) != 0 || eval_expr_const(e->rhs, &r) != 0) {
            return -1;
        }
        switch (e->op) {
        case AS_EXPR_OP_ADD:
            *out = l + r;
            return 0;
        case AS_EXPR_OP_SUB:
            *out = l - r;
            return 0;
        case AS_EXPR_OP_MUL:
            *out = l * r;
            return 0;
        case AS_EXPR_OP_DIV:
            if (r == 0) return -1;
            *out = l / r;
            return 0;
        case AS_EXPR_OP_MOD:
            if (r == 0) return -1;
            *out = l % r;
            return 0;
        case AS_EXPR_OP_OR:
            *out = l | r;
            return 0;
        case AS_EXPR_OP_AND:
            *out = l & r;
            return 0;
        case AS_EXPR_OP_XOR:
            *out = l ^ r;
            return 0;
        case AS_EXPR_OP_SHL:
            *out = l << (r & 63);
            return 0;
        case AS_EXPR_OP_SHR:
            *out = l >> (r & 63);
            return 0;
        case AS_EXPR_OP_EQ:
            *out = (l == r) ? -1 : 0;
            return 0;
        case AS_EXPR_OP_NE:
            *out = (l != r) ? -1 : 0;
            return 0;
        case AS_EXPR_OP_LT:
            *out = (l < r) ? -1 : 0;
            return 0;
        case AS_EXPR_OP_LE:
            *out = (l <= r) ? -1 : 0;
            return 0;
        case AS_EXPR_OP_GT:
            *out = (l > r) ? -1 : 0;
            return 0;
        case AS_EXPR_OP_GE:
            *out = (l >= r) ? -1 : 0;
            return 0;
        default:
            return -1;
        }
    default:
        return -1;
    }
}

static int eval_expr_asm_vars(emit_ctx_t *ctx, const as_expr_t *e, long long *out) {
    long long l;
    long long r;

    if (ctx == NULL || e == NULL || out == NULL) {
        return -1;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        *out = e->value;
        return 0;
    case AS_EXPR_SYMBOL:
        if (e->symbol == NULL || strcmp(e->symbol, ".") == 0) {
            return -1;
        }
        return asm_var_lookup(ctx, e->symbol, out);
    case AS_EXPR_UNARY:
        if (eval_expr_asm_vars(ctx, e->lhs, &l) != 0) {
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
        if (eval_expr_asm_vars(ctx, e->lhs, &l) != 0 ||
            eval_expr_asm_vars(ctx, e->rhs, &r) != 0) {
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
        (strcmp(d->name, ".set") != 0 && strcmp(d->name, ".equ") != 0) ||
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

static int parse_x86_reg(const char *name, as_x86_reg_t *out) {
    static const struct {
        const char *name;
        as_x86_reg_t reg;
    } map[] = {
        {"al", AS_X86_REG_RAX}, {"ah", AS_X86_REG_AH}, {"ax", AS_X86_REG_RAX}, {"eax", AS_X86_REG_RAX}, {"rax", AS_X86_REG_RAX},
        {"cl", AS_X86_REG_RCX}, {"ch", AS_X86_REG_CH}, {"cx", AS_X86_REG_RCX}, {"ecx", AS_X86_REG_RCX}, {"rcx", AS_X86_REG_RCX},
        {"dl", AS_X86_REG_RDX}, {"dh", AS_X86_REG_DH}, {"dx", AS_X86_REG_RDX}, {"edx", AS_X86_REG_RDX}, {"rdx", AS_X86_REG_RDX},
        {"bl", AS_X86_REG_RBX}, {"bh", AS_X86_REG_BH}, {"bx", AS_X86_REG_RBX}, {"ebx", AS_X86_REG_RBX}, {"rbx", AS_X86_REG_RBX},
        {"spl", AS_X86_REG_RSP}, {"sp", AS_X86_REG_RSP}, {"esp", AS_X86_REG_RSP}, {"rsp", AS_X86_REG_RSP},
        {"bpl", AS_X86_REG_RBP}, {"bp", AS_X86_REG_RBP}, {"ebp", AS_X86_REG_RBP}, {"rbp", AS_X86_REG_RBP},
        {"sil", AS_X86_REG_RSI}, {"si", AS_X86_REG_RSI}, {"esi", AS_X86_REG_RSI}, {"rsi", AS_X86_REG_RSI},
        {"dil", AS_X86_REG_RDI}, {"di", AS_X86_REG_RDI}, {"edi", AS_X86_REG_RDI}, {"rdi", AS_X86_REG_RDI},
        {"r8b", AS_X86_REG_R8}, {"r8w", AS_X86_REG_R8}, {"r8d", AS_X86_REG_R8}, {"r8", AS_X86_REG_R8},
        {"r9b", AS_X86_REG_R9}, {"r9w", AS_X86_REG_R9}, {"r9d", AS_X86_REG_R9}, {"r9", AS_X86_REG_R9},
        {"r10b", AS_X86_REG_R10}, {"r10w", AS_X86_REG_R10}, {"r10d", AS_X86_REG_R10}, {"r10", AS_X86_REG_R10},
        {"r11b", AS_X86_REG_R11}, {"r11w", AS_X86_REG_R11}, {"r11d", AS_X86_REG_R11}, {"r11", AS_X86_REG_R11},
        {"r12b", AS_X86_REG_R12}, {"r12w", AS_X86_REG_R12}, {"r12d", AS_X86_REG_R12}, {"r12", AS_X86_REG_R12},
        {"r13b", AS_X86_REG_R13}, {"r13w", AS_X86_REG_R13}, {"r13d", AS_X86_REG_R13}, {"r13", AS_X86_REG_R13},
        {"r14b", AS_X86_REG_R14}, {"r14w", AS_X86_REG_R14}, {"r14d", AS_X86_REG_R14}, {"r14", AS_X86_REG_R14},
        {"r15b", AS_X86_REG_R15}, {"r15w", AS_X86_REG_R15}, {"r15d", AS_X86_REG_R15}, {"r15", AS_X86_REG_R15},
    };
    const char *p = name;
    size_t i;
    unsigned v = 0;

    if (name == NULL || out == NULL) {
        return -1;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (streq_ci(p, map[i].name)) {
            *out = map[i].reg;
            return 0;
        }
    }
    if ((startswith_ci(p, "xmm") || startswith_ci(p, "ymm") || startswith_ci(p, "zmm")) &&
        isdigit((unsigned char)p[3])) {
        p += 3;
        while (isdigit((unsigned char)*p)) {
            v = (v * 10u) + (unsigned)(*p - '0');
            ++p;
        }
        if (*p == '\0' && v <= 15u) {
            *out = (as_x86_reg_t)v;
            return 0;
        }
    }
    if (startswith_ci(p, "mm") && isdigit((unsigned char)p[2])) {
        p += 2;
        while (isdigit((unsigned char)*p)) {
            v = (v * 10u) + (unsigned)(*p - '0');
            ++p;
        }
        if (*p == '\0' && v <= 7u) {
            *out = (as_x86_reg_t)v;
            return 0;
        }
    }
    return -1;
}

static unsigned parse_x86_reg_bits(const char *name) {
    const char *p;
    size_t n;

    if (name == NULL) {
        return 0;
    }
    p = name;
    while (*p == '%') {
        ++p;
    }
    n = strlen(p);
    if (n == 0) {
        return 0;
    }
    if (streq_ci(p, "al") || streq_ci(p, "ah") || streq_ci(p, "bl") || streq_ci(p, "bh") ||
        streq_ci(p, "cl") || streq_ci(p, "ch") || streq_ci(p, "dl") || streq_ci(p, "dh") ||
        streq_ci(p, "sil") || streq_ci(p, "dil") || streq_ci(p, "spl") || streq_ci(p, "bpl") ||
        (p[0] == 'r' && n >= 3 && p[n - 1] == 'b')) {
        return 8;
    }
    if (streq_ci(p, "ax") || streq_ci(p, "bx") || streq_ci(p, "cx") || streq_ci(p, "dx") ||
        streq_ci(p, "si") || streq_ci(p, "di") || streq_ci(p, "sp") || streq_ci(p, "bp") ||
        streq_ci(p, "ip") ||
        (p[0] == 'r' && n >= 3 && p[n - 1] == 'w')) {
        return 16;
    }
    if (streq_ci(p, "eax") || streq_ci(p, "ebx") || streq_ci(p, "ecx") || streq_ci(p, "edx") ||
        streq_ci(p, "esi") || streq_ci(p, "edi") || streq_ci(p, "esp") || streq_ci(p, "ebp") ||
        streq_ci(p, "eip") ||
        (p[0] == 'r' && n >= 3 && p[n - 1] == 'd')) {
        return 32;
    }
    if (streq_ci(p, "rax") || streq_ci(p, "rbx") || streq_ci(p, "rcx") || streq_ci(p, "rdx") ||
        streq_ci(p, "rsi") || streq_ci(p, "rdi") || streq_ci(p, "rsp") || streq_ci(p, "rbp") ||
        streq_ci(p, "rip") ||
        (p[0] == 'r' && n >= 2 && p[n - 1] >= '0' && p[n - 1] <= '9')) {
        return 64;
    }
    if (p[0] == 'm' && p[1] == 'm' && isdigit((unsigned char)p[2])) {
        return 64;
    }
    if (p[0] == 'x' && p[1] == 'm' && p[2] == 'm' && isdigit((unsigned char)p[3])) {
        return 128;
    }
    if (p[0] == 'y' && p[1] == 'm' && p[2] == 'm' && isdigit((unsigned char)p[3])) {
        return 256;
    }
    if (p[0] == 'z' && p[1] == 'm' && p[2] == 'm' && isdigit((unsigned char)p[3])) {
        return 512;
    }
    if (p[0] == 'k' && isdigit((unsigned char)p[1])) {
        return 64;
    }
    if (streq_ci(p, "cs") || streq_ci(p, "ds") || streq_ci(p, "es") || streq_ci(p, "fs") ||
        streq_ci(p, "gs") || streq_ci(p, "ss")) {
        return 16;
    }
    return 0;
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
        index_bits = parse_x86_reg_bits(mem->index_reg);
    }
    if (base_bits != 0 && index_bits != 0 && base_bits != index_bits) {
        return 0;
    }
    if (base_bits != 0) {
        return base_bits;
    }
    return index_bits;
}

static int parse_bnd_reg(const char *name, unsigned *out) {
    const char *p = name;
    unsigned v = 0;

    if (name == NULL || out == NULL) {
        return -1;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    if (!((p[0] == 'b' || p[0] == 'B') &&
          (p[1] == 'n' || p[1] == 'N') &&
          (p[2] == 'd' || p[2] == 'D') &&
          isdigit((unsigned char)p[3]))) {
        return -1;
    }
    p += 3;
    while (isdigit((unsigned char)*p)) {
        v = (v * 10u) + (unsigned)(*p - '0');
        ++p;
    }
    if (*p != '\0' || v > 3u) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_x86_sysreg(const char *name, const char *kind, unsigned max_reg, unsigned *out) {
    const char *p = name;
    unsigned v = 0;

    if (name == NULL || kind == NULL || out == NULL) {
        return -1;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    while (*kind != '\0') {
        if (tolower((unsigned char)*p) != tolower((unsigned char)*kind)) {
            return -1;
        }
        ++p;
        ++kind;
    }
    if (!isdigit((unsigned char)*p)) {
        return -1;
    }
    while (isdigit((unsigned char)*p)) {
        v = (v * 10u) + (unsigned)(*p - '0');
        ++p;
    }
    if (*p != '\0' || v > max_reg) {
        return -1;
    }
    *out = v;
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
    const char *p = name;
    size_t n;

    if (p == NULL || p[0] == '\0') {
        return 0;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    n = strlen(p);
    if (n == 0) {
        return 0;
    }
    if (n >= 3 && (p[0] == 'x' || p[0] == 'X') && (p[1] == 'm' || p[1] == 'M') && (p[2] == 'm' || p[2] == 'M')) {
        return 128;
    }
    if (n >= 3 && (p[0] == 'y' || p[0] == 'Y') && (p[1] == 'm' || p[1] == 'M') && (p[2] == 'm' || p[2] == 'M')) {
        return 256;
    }
    if (n >= 3 && (p[0] == 'z' || p[0] == 'Z') && (p[1] == 'm' || p[1] == 'M') && (p[2] == 'm' || p[2] == 'M')) {
        return 512;
    }
    if (n >= 2 && (p[0] == 'm' || p[0] == 'M') && (p[1] == 'm' || p[1] == 'M')) {
        return 64;
    }
    if (streq_ci(p, "al") || streq_ci(p, "ah") || streq_ci(p, "bl") || streq_ci(p, "bh") || streq_ci(p, "cl") ||
        streq_ci(p, "ch") || streq_ci(p, "dl") || streq_ci(p, "dh") || streq_ci(p, "sil") || streq_ci(p, "dil") ||
        streq_ci(p, "spl") || streq_ci(p, "bpl")) {
        return 8;
    }
    if (n >= 3 && (p[0] == 'r' || p[0] == 'R') && isdigit((unsigned char)p[1]) && (p[n - 1] == 'b' || p[n - 1] == 'B')) {
        return 8;
    }
    if (streq_ci(p, "ax") || streq_ci(p, "bx") || streq_ci(p, "cx") || streq_ci(p, "dx") || streq_ci(p, "si") ||
        streq_ci(p, "di") || streq_ci(p, "sp") || streq_ci(p, "bp") || streq_ci(p, "ip") ||
        streq_ci(p, "cs") || streq_ci(p, "ds") || streq_ci(p, "es") || streq_ci(p, "fs") ||
        streq_ci(p, "gs") || streq_ci(p, "ss")) {
        return 16;
    }
    if (n >= 3 && (p[0] == 'r' || p[0] == 'R') && isdigit((unsigned char)p[1]) && (p[n - 1] == 'w' || p[n - 1] == 'W')) {
        return 16;
    }
    if (n > 1 && (p[0] == 'e' || p[0] == 'E')) {
        return 32;
    }
    if (n >= 3 && (p[0] == 'r' || p[0] == 'R') && isdigit((unsigned char)p[1]) && (p[n - 1] == 'd' || p[n - 1] == 'D')) {
        return 32;
    }
    if (streq_ci(p, "rax") || streq_ci(p, "rbx") || streq_ci(p, "rcx") || streq_ci(p, "rdx") || streq_ci(p, "rsi") ||
        streq_ci(p, "rdi") || streq_ci(p, "rsp") || streq_ci(p, "rbp") || streq_ci(p, "rip")) {
        return 64;
    }
    if (n >= 2 && (p[0] == 'r' || p[0] == 'R') && isdigit((unsigned char)p[1])) {
        return 64;
    }
    return 0;
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

static int emit_i386_modrm_rm_operand(unsigned reg_field, const as_operand_t *rm_op,
                                      unsigned char *out, size_t out_cap, size_t *pos_io) {
    unsigned rm_reg;
    size_t pos;

    if (rm_op == NULL || out == NULL || pos_io == NULL || reg_field > 7u) {
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
            if (!has_disp && base_reg != 5u) {
                mod = 0;
            } else if (has_disp && disp >= -128 && disp <= 127) {
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
        {"xrstors", 0x00, 3u},
        {"xsavec", 0x00, 4u},
        {"xsaves", 0x00, 5u},
        {"rdrand", 0x00, 6u},
        {"vmclear", 0x66, 6u},
        {"vmptrld", 0x00, 6u},
        {"vmxon", 0xf3, 6u},
        {"rdseed", 0x00, 7u},
        {"vmptrst", 0x00, 7u},
        {"rdpid", 0xf3, 7u},
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
        {"ptwrite", 4u},
        {"incsspd", 5u},
        {"umonitor", 6u},
        {"clrssbsy", 6u},
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
        {"clwb", 0x66, 6u},
        {"clflushopt", 0x66, 7u},
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
        {"montmul", 0xa6},
        {"xstore-rng", 0xa7},
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

static int lookup_i386_vm_invalidate_opcode(const char *mnemonic, unsigned char *opcode3) {
    static const struct {
        const char *mnemonic;
        unsigned char opcode3;
    } map[] = {
        {"invept", 0x80},
        {"invvpid", 0x81},
        {"invpcid", 0x82},
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
        {"wrssd", 0x00, 0xf6, 0},
        {"wrussd", 0x66, 0xf5, 0},
        {"movdir64b", 0x66, 0xf8, 1},
        {"enqcmd", 0xf2, 0xf8, 1},
        {"enqcmds", 0xf3, 0xf8, 1},
        {"movdiri", 0x00, 0xf9, 0},
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
        {"cldemote", 0x1c, 0u},
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

static int select_x86_srcdst_operands(const as_instruction_t *insn, int intel_syntax,
                                      const as_operand_t **src_op, const as_operand_t **dst_op) {
    if (insn == NULL || src_op == NULL || dst_op == NULL || insn->operand_count != 2) {
        return -1;
    }
    if (intel_syntax) {
        *dst_op = &insn->operands[0];
        *src_op = &insn->operands[1];
    } else {
        *src_op = &insn->operands[0];
        *dst_op = &insn->operands[1];
    }
    return 0;
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

static int select_x86_dst_immimm_operands(const as_instruction_t *insn, int intel_syntax,
                                          const as_operand_t **dst_op,
                                          const as_operand_t **imm0_op,
                                          const as_operand_t **imm1_op) {
    if (insn == NULL || dst_op == NULL || imm0_op == NULL || imm1_op == NULL || insn->operand_count != 3) {
        return -1;
    }
    if (intel_syntax) {
        *dst_op = &insn->operands[0];
        *imm0_op = &insn->operands[1];
        *imm1_op = &insn->operands[2];
    } else {
        *imm0_op = &insn->operands[0];
        *imm1_op = &insn->operands[1];
        *dst_op = &insn->operands[2];
    }
    return 0;
}

static int select_x86_dstsrc_immimm_operands(const as_instruction_t *insn, int intel_syntax,
                                             const as_operand_t **dst_op,
                                             const as_operand_t **src_op,
                                             const as_operand_t **imm0_op,
                                             const as_operand_t **imm1_op) {
    if (insn == NULL || dst_op == NULL || src_op == NULL || imm0_op == NULL || imm1_op == NULL ||
        insn->operand_count != 4) {
        return -1;
    }
    if (intel_syntax) {
        *dst_op = &insn->operands[0];
        *src_op = &insn->operands[1];
        *imm0_op = &insn->operands[2];
        *imm1_op = &insn->operands[3];
    } else {
        *imm0_op = &insn->operands[0];
        *imm1_op = &insn->operands[1];
        *src_op = &insn->operands[2];
        *dst_op = &insn->operands[3];
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
    if (s == NULL) return AS_X86_SEG_NONE;
    if (streq_ci(s, "cs")) return AS_X86_SEG_CS;
    if (streq_ci(s, "ds")) return AS_X86_SEG_DS;
    if (streq_ci(s, "es")) return AS_X86_SEG_ES;
    if (streq_ci(s, "fs")) return AS_X86_SEG_FS;
    if (streq_ci(s, "gs")) return AS_X86_SEG_GS;
    if (streq_ci(s, "ss")) return AS_X86_SEG_SS;
    return AS_X86_SEG_NONE;
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

static int is_rel_mnemonic(const char *mn) {
    if (mn == NULL || mn[0] == '\0') {
        return 0;
    }
    if (streq_ci(mn, "call") || streq_ci(mn, "jmp") || streq_ci(mn, "xbegin") ||
        streq_ci(mn, "loop") || streq_ci(mn, "loope") || streq_ci(mn, "loopz") ||
        streq_ci(mn, "loopne") || streq_ci(mn, "loopnz")) {
        return 1;
    }
    if ((mn[0] == 'j' || mn[0] == 'J') && mn[1] != '\0') {
        return 1;
    }
    return 0;
}

static int is_call_mnemonic(const char *mn) {
    return streq_ci(mn, "call") || streq_ci(mn, "lcall");
}

static int is_fixed_short_rel_mnemonic(const char *mn) {
    if (mn == NULL || mn[0] == '\0') {
        return 0;
    }
    return streq_ci(mn, "jcxz") ||
           streq_ci(mn, "jecxz") ||
           streq_ci(mn, "jrcxz") ||
           streq_ci(mn, "loop") ||
           streq_ci(mn, "loope") ||
           streq_ci(mn, "loopz") ||
           streq_ci(mn, "loopne") ||
           streq_ci(mn, "loopnz");
}

static int is_size_suffixable_base(const char *mn) {
    static const char *const names[] = {
        "add", "adc", "sbb", "sub", "and", "or", "xor", "cmp", "mov", "lea", "imul", "shl",
        "shr", "sar", "ror", "rol", "rcl", "rcr", "bt", "bts", "btr", "btc", "bsf", "bsr",
        "movsx", "movzx", "test", "push", "pushf", "popf", "xadd", "xchg", "cmpxchg",
        "inc", "dec", "not", "neg", "mul", "div", "idiv", "enter", "call", "jmp", "lcall", "ljmp",
        "sldt", "str", "lldt", "ltr", "verr", "verw", "sgdt", "sidt", "lgdt", "lidt", "smsw", "lmsw",
        "pop", "ret", "leave",
        "cmovo", "cmovno", "cmovb", "cmovae", "cmovc", "cmovnc", "cmovnae", "cmovnb",
        "cmove", "cmovne", "cmovz", "cmovnz",
        "cmovbe", "cmova", "cmovna", "cmovnbe",
        "cmovs", "cmovns",
        "cmovp", "cmovnp", "cmovpe", "cmovpo",
        "cmovl", "cmovge", "cmovnge", "cmovnl",
        "cmovle", "cmovg", "cmovng", "cmovnle",
        "popcnt", "lzcnt", "tzcnt", "bswap",
        "in", "out",
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
        return 0;
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
    const char *p = name;
    unsigned v = 0;

    if (p == NULL || out == NULL) {
        return -1;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    if (!(p[0] == 'x' || p[0] == 'X') || !(p[1] == 'm' || p[1] == 'M') || !(p[2] == 'm' || p[2] == 'M')) {
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
    if (*p != '\0' || v > 15u) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_ymm_reg(const char *name, unsigned *out) {
    const char *p = name;
    unsigned v = 0;

    if (p == NULL || out == NULL) {
        return -1;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    if (!(p[0] == 'y' || p[0] == 'Y') || !(p[1] == 'm' || p[1] == 'M') || !(p[2] == 'm' || p[2] == 'M')) {
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
    if (*p != '\0' || v > 15u) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_zmm_reg(const char *name, unsigned *out) {
    const char *p = name;
    unsigned v = 0;

    if (p == NULL || out == NULL) {
        return -1;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    if (!(p[0] == 'z' || p[0] == 'Z') || !(p[1] == 'm' || p[1] == 'M') || !(p[2] == 'm' || p[2] == 'M')) {
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
    if (*p != '\0' || v > 31u) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_mmx_reg(const char *name, unsigned *out) {
    const char *p = name;
    unsigned v = 0;

    if (p == NULL || out == NULL) {
        return -1;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    if (!(p[0] == 'm' || p[0] == 'M') || !(p[1] == 'm' || p[1] == 'M')) {
        return -1;
    }
    p += 2;
    if (!isdigit((unsigned char)*p)) {
        return -1;
    }
    while (isdigit((unsigned char)*p)) {
        v = (v * 10u) + (unsigned)(*p - '0');
        ++p;
    }
    if (*p != '\0' || v > 7u) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_k_reg(const char *name, unsigned *out) {
    const char *p = name;

    if (p == NULL || out == NULL) {
        return -1;
    }
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    if ((p[0] == 'k' || p[0] == 'K') && p[1] >= '0' && p[1] <= '7' && p[2] == '\0') {
        *out = (unsigned)(p[1] - '0');
        return 0;
    }
    return -1;
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
    if (name == NULL) {
        return 0;
    }
    return streq_ci(name, "al") || streq_ci(name, "%al") || streq_ci(name, "bl") || streq_ci(name, "%bl") ||
           streq_ci(name, "cl") || streq_ci(name, "%cl") || streq_ci(name, "dl") || streq_ci(name, "%dl") ||
           streq_ci(name, "sil") || streq_ci(name, "%sil") || streq_ci(name, "dil") || streq_ci(name, "%dil") ||
           streq_ci(name, "spl") || streq_ci(name, "%spl") || streq_ci(name, "bpl") || streq_ci(name, "%bpl") ||
           streq_ci(name, "r8b") || streq_ci(name, "%r8b") || streq_ci(name, "r9b") || streq_ci(name, "%r9b") ||
           streq_ci(name, "r10b") || streq_ci(name, "%r10b") || streq_ci(name, "r11b") || streq_ci(name, "%r11b") ||
           streq_ci(name, "r12b") || streq_ci(name, "%r12b") || streq_ci(name, "r13b") || streq_ci(name, "%r13b") ||
           streq_ci(name, "r14b") || streq_ci(name, "%r14b") || streq_ci(name, "r15b") || streq_ci(name, "%r15b");
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
    } else if (disp >= -128 && disp <= 127) {
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
    } else if (disp >= -128 && disp <= 127) {
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
    unsigned char rex;
    unsigned char modrm;
    unsigned char mod;
    int need_sib;
    size_t pos = 0;

    if (mem == NULL || out == NULL || out_len == NULL || out_cap < 8) {
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
        base = AS_X86_REG_RBP;
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
    } else if (disp >= -128 && disp <= 127) {
        mod = 1;
    } else {
        mod = 2;
    }

    rex = (unsigned char)(0x40u | ((breg & 8u) ? 0x01u : 0u));
    if (rex != 0x40u) {
        out[pos++] = rex;
    }
    out[pos++] = opcode;
    modrm = (unsigned char)((mod << 6) | ((reg_field & 7u) << 3) | (breg & 7u));
    out[pos++] = modrm;
    if (need_sib) {
        out[pos++] = (unsigned char)(0x20u | (breg & 7u));
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

static int emit_i386_vex2_kmovw(const as_operand_t *src, unsigned dst_k, unsigned char *out, size_t out_cap, size_t *out_len) {
    size_t pos = 0;

    if (src == NULL || out == NULL || out_len == NULL || out_cap < 8 || dst_k > 7u) {
        return -1;
    }
    out[pos++] = 0xc5;
    out[pos++] = 0xf8;
    out[pos++] = 0x90;
    if (emit_i386_modrm_rm_operand(dst_k, src, out, out_cap, &pos) != 0) {
        return -1;
    }
    *out_len = pos;
    return 0;
}

static int emit_i386_vex_klogic(const char *mnemonic, unsigned src_rm_k, unsigned src_vvvv_k, unsigned dst_k,
                                unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char opcode;
    unsigned char vex_p1;
    char last;
    int pp = 0;
    int use_vex3 = 0;

    if (mnemonic == NULL || out == NULL || out_len == NULL || out_cap < 5 || src_rm_k > 7u || src_vvvv_k > 7u || dst_k > 7u) {
        return -1;
    }
    if (strncmp(mnemonic, "kandn", 5) == 0) opcode = 0x42;
    else if (strncmp(mnemonic, "kand", 4) == 0) opcode = 0x41;
    else if (strncmp(mnemonic, "kor", 3) == 0) opcode = 0x45;
    else if (strncmp(mnemonic, "kxnor", 5) == 0) opcode = 0x46;
    else if (strncmp(mnemonic, "kxor", 4) == 0) opcode = 0x47;
    else if (strncmp(mnemonic, "kadd", 4) == 0) opcode = 0x4a;
    else if (strncmp(mnemonic, "kunpck", 6) == 0) opcode = 0x4b;
    else return -1;

    last = mnemonic[strlen(mnemonic) - 1];
    if (last == 'b' || last == 'd') {
        pp = 1;
    }
    if (last == 'q' || last == 'd') {
        use_vex3 = 1;
    }
    vex_p1 = (unsigned char)(0x80u | (((~src_vvvv_k) & 0xfu) << 3) | 0x04u | (unsigned)pp);
    if (use_vex3) {
        out[0] = 0xc4;
        out[1] = 0xe1;
        out[2] = vex_p1;
        out[3] = opcode;
        out[4] = (unsigned char)(0xc0u | ((dst_k & 7u) << 3) | (src_rm_k & 7u));
        *out_len = 5;
        return 0;
    }
    out[0] = 0xc5;
    out[1] = vex_p1;
    out[2] = opcode;
    out[3] = (unsigned char)(0xc0u | ((dst_k & 7u) << 3) | (src_rm_k & 7u));
    *out_len = 4;
    return 0;
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

    if (src == NULL || dst == NULL || out == NULL || out_len == NULL || out_cap < 5 ||
        src->kind != AS_OPERAND_REGISTER || dst->kind != AS_OPERAND_REGISTER ||
        parse_x86_reg(src->u.reg, &gr) != 0 || parse_xmm_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    rex = (unsigned char)(0x48u | ((xr & 8u) ? 0x04u : 0u) | ((((unsigned)gr) & 8u) ? 0x01u : 0u));
    out[0] = prefix;
    out[1] = rex;
    out[2] = 0x0f;
    out[3] = 0x2a;
    out[4] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (((unsigned)gr) & 7u));
    *out_len = 5;
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

static int emit_i386_extrq_insertq_family(const char *mnemonic, const as_instruction_t *insn, int intel_syntax,
                                          unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned char prefix;

    if (mnemonic == NULL || insn == NULL || out == NULL || out_len == NULL ||
        lookup_i386_extrq_insertq_prefix(mnemonic, &prefix) != 0) {
        return -1;
    }
    if (insn->operand_count == 2) {
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        unsigned xr;
        unsigned xm;

        if (select_x86_srcdst_operands(insn, intel_syntax, &src_op, &dst_op) != 0 ||
            dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0) {
            return -1;
        }
        if (src_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src_op->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, 0x79, xr, src_op, out, out_cap, out_len);
    }
    if (insn->operand_count == 3 && strcmp(mnemonic, "extrq") == 0) {
        const as_operand_t *len_op;
        const as_operand_t *off_op;
        const as_operand_t *dst_op;
        unsigned xr;
        long long lenv;
        long long offv;

        if (select_x86_dst_immimm_operands(insn, intel_syntax, &dst_op, &len_op, &off_op) != 0 ||
            dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0 ||
            (len_op->kind != AS_OPERAND_IMMEDIATE && len_op->kind != AS_OPERAND_LABEL_REF) ||
            (off_op->kind != AS_OPERAND_IMMEDIATE && off_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(len_op->u.expr, &lenv) != 0 || lenv < 0 || lenv > 255 ||
            eval_expr_const(off_op->u.expr, &offv) != 0 || offv < 0 || offv > 255) {
            return -1;
        }
        out[0] = prefix;
        out[1] = 0x0f;
        out[2] = 0x78;
        out[3] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (xr & 7u));
        out[4] = (unsigned char)lenv;
        out[5] = (unsigned char)offv;
        *out_len = 6;
        return 0;
    }
    if (insn->operand_count == 4 && strcmp(mnemonic, "insertq") == 0) {
        const as_operand_t *len_op;
        const as_operand_t *off_op;
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        unsigned xr;
        unsigned xm;
        long long lenv;
        long long offv;

        if (select_x86_dstsrc_immimm_operands(insn, intel_syntax, &dst_op, &src_op, &len_op, &off_op) != 0 ||
            dst_op->kind != AS_OPERAND_REGISTER || src_op->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst_op->u.reg, &xr) != 0 || parse_xmm_reg(src_op->u.reg, &xm) != 0 ||
            (len_op->kind != AS_OPERAND_IMMEDIATE && len_op->kind != AS_OPERAND_LABEL_REF) ||
            (off_op->kind != AS_OPERAND_IMMEDIATE && off_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(len_op->u.expr, &lenv) != 0 || lenv < 0 || lenv > 255 ||
            eval_expr_const(off_op->u.expr, &offv) != 0 || offv < 0 || offv > 255) {
            return -1;
        }
        out[0] = prefix;
        out[1] = 0x0f;
        out[2] = 0x78;
        out[3] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (xm & 7u));
        out[4] = (unsigned char)lenv;
        out[5] = (unsigned char)offv;
        *out_len = 6;
        return 0;
    }
    return -1;
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
        eval_expr_const(imm_op->u.expr, &immv) != 0 || immv < 0 || immv > 255 ||
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

static int emit_x86_klogic(const as_instruction_t *insn, int intel_syntax, const char *mnemonic,
                           unsigned char *out, size_t out_cap, size_t *out_len) {
    const as_operand_t *op_rm;
    const as_operand_t *op_vvvv;
    const as_operand_t *op_dst;
    unsigned krm;
    unsigned kvvvv;
    unsigned kdst;

    if (insn == NULL || mnemonic == NULL || out == NULL || insn->operand_count != 3) {
        return -1;
    }
    if (intel_syntax) {
        op_dst = &insn->operands[0];
        op_vvvv = &insn->operands[1];
        op_rm = &insn->operands[2];
    } else {
        op_rm = &insn->operands[0];
        op_vvvv = &insn->operands[1];
        op_dst = &insn->operands[2];
    }
    if (op_rm->kind != AS_OPERAND_REGISTER || op_vvvv->kind != AS_OPERAND_REGISTER ||
        op_dst->kind != AS_OPERAND_REGISTER || parse_k_reg(op_rm->u.reg, &krm) != 0 ||
        parse_k_reg(op_vvvv->u.reg, &kvvvv) != 0 || parse_k_reg(op_dst->u.reg, &kdst) != 0) {
        return -1;
    }
    return emit_i386_vex_klogic(mnemonic, krm, kvvvv, kdst, out, out_cap, out_len);
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
    if (strcmp(mnbuf, "kmovw") == 0) {
        unsigned kd;

        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_k_reg(dst->u.reg, &kd) != 0) {
            return -1;
        }
        return emit_i386_vex2_kmovw(src, kd, out, out_cap, out_len);
    }
    if (strncmp(mnbuf, "kand", 4) == 0 || strncmp(mnbuf, "kor", 3) == 0 || strncmp(mnbuf, "kxor", 4) == 0 ||
        strncmp(mnbuf, "kxnor", 5) == 0 || strncmp(mnbuf, "kadd", 4) == 0 || strncmp(mnbuf, "kunpck", 6) == 0) {
        return emit_x86_klogic(insn, intel_syntax, mnbuf, out, out_cap, out_len);
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
    if (strcmp(mnbuf, "movq") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_mmx_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_legacy_simd_rm(0x00, 0x6f, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x00, 0x7f, xr, dst, out, out_cap, out_len);
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
            eval_expr_const(imm_op->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
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
    if (strcmp(mnbuf, "cpuid") == 0 || strcmp(mnbuf, "montmul") == 0 || strcmp(mnbuf, "xstore-rng") == 0 ||
        strcmp(mnbuf, "rsm") == 0) {
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
    if (strcmp(mnbuf, "wbnoinvd") == 0) {
        if (insn->operand_count != 0 || out == NULL || out_len == NULL || out_cap < 3) {
            return -1;
        }
        out[0] = 0xf3;
        out[1] = 0x0f;
        out[2] = 0x09;
        *out_len = 3;
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
        strcmp(mnbuf, "xsaveopt") == 0 || strcmp(mnbuf, "clflush") == 0 ||
        strcmp(mnbuf, "clwb") == 0 || strcmp(mnbuf, "clflushopt") == 0) {
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
        strcmp(mnbuf, "wrgsbase") == 0 || strcmp(mnbuf, "ptwrite") == 0 || strcmp(mnbuf, "incsspd") == 0 ||
        strcmp(mnbuf, "umonitor") == 0 || strcmp(mnbuf, "clrssbsy") == 0) {
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
        opcode2 = is_x86_low8_reg(reg_op->u.reg) ? 0xb0 : 0xb1;
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
    if (strcmp(mnbuf, "cmpps") == 0 || strcmp(mnbuf, "cmppd") == 0 || strcmp(mnbuf, "cmpss") == 0 ||
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
            eval_expr_const(imm_op->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        if (strcmp(mnbuf, "cmpps") == 0 || strcmp(mnbuf, "cmppd") == 0 || strcmp(mnbuf, "cmpss") == 0 ||
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
        if (strcmp(mnbuf, "movdir64b") == 0 || strcmp(mnbuf, "enqcmd") == 0) {
            if (intel_syntax) {
                reg_op = &insn->operands[0];
                mem_op = &insn->operands[1];
            } else {
                mem_op = &insn->operands[0];
                reg_op = &insn->operands[1];
            }
        } else if (intel_syntax) {
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
        return emit_i386_maskmov(0x00, 0, &insn->operands[0], &insn->operands[1], out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "maskmovdqu") == 0) {
        if (insn->operand_count != 2) {
            return -1;
        }
        return emit_i386_maskmov(0x66, 1, &insn->operands[0], &insn->operands[1], out, out_cap, out_len);
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
    if (strcmp(mnbuf, "cmpxchg8b") == 0 || strcmp(mnbuf, "rdrand") == 0 || strcmp(mnbuf, "rdseed") == 0 ||
        strcmp(mnbuf, "xrstors") == 0 || strcmp(mnbuf, "xsavec") == 0 || strcmp(mnbuf, "xsaves") == 0 ||
        strcmp(mnbuf, "vmclear") == 0 || strcmp(mnbuf, "vmptrld") == 0 || strcmp(mnbuf, "vmptrst") == 0 ||
        strcmp(mnbuf, "vmxon") == 0 || strcmp(mnbuf, "rdpid") == 0) {
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
    if (strcmp(mnbuf, "hreset") == 0) {
        long long immv;

        if (insn->operand_count != 1 || a == NULL ||
            (a->kind != AS_OPERAND_IMMEDIATE && a->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(a->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        if (out_cap < 6) {
            return -1;
        }
        out[0] = 0xf3;
        out[1] = 0x0f;
        out[2] = 0x3a;
        out[3] = 0xf0;
        out[4] = 0xc0;
        out[5] = (unsigned char)immv;
        if (out_len != NULL) *out_len = 6;
        return 0;
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
    if (strcmp(mnbuf, "invept") == 0 || strcmp(mnbuf, "invvpid") == 0 || strcmp(mnbuf, "invpcid") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;
        unsigned char opcode3;

        if (insn->operand_count != 2) {
            return -1;
        }
        if (select_i386_syntax_reg_rm_operands(insn, intel_syntax, 1, &reg_op, &rm_op) != 0) {
            return -1;
        }
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (lookup_i386_vm_invalidate_opcode(mnbuf, &opcode3) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_map_rm(0x66, 0x38, opcode3, (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
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
    if (strcmp(mnbuf, "aesencwide256kl") == 0 || strcmp(mnbuf, "aesencwide128kl") == 0) {
        const as_operand_t *op;

        if (insn->operand_count != 1) {
            return -1;
        }
        op = &insn->operands[0];
        return emit_i386_prefixed_0f_map_rm(0xf3, 0x38, 0xd8, 0u, op, out, out_cap, out_len);
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
    if (strcmp(mnbuf, "wrssd") == 0 || strcmp(mnbuf, "wrussd") == 0 || strcmp(mnbuf, "movdir64b") == 0 ||
        strcmp(mnbuf, "enqcmd") == 0 || strcmp(mnbuf, "enqcmds") == 0 ||
        strcmp(mnbuf, "movdiri") == 0 || strcmp(mnbuf, "aadd") == 0 || strcmp(mnbuf, "aand") == 0 ||
        strcmp(mnbuf, "aor") == 0 || strcmp(mnbuf, "axor") == 0) {
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
    if (strcmp(mnbuf, "nopw") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x66, 0x1d, 0u, a, out, out_cap, out_len);
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
    if (emit_i386_extrq_insertq_family(mnbuf, insn, intel_syntax, out, out_cap, out_len) == 0) {
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
    if (strcmp(mnbuf, "nopl") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind == AS_OPERAND_REGISTER || a->kind == AS_OPERAND_COPROCESSOR) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x1d, 0u, a, out, out_cap, out_len);
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
            (strcmp(mnbuf, "rex") == 0 || strncasecmp(mnbuf, "rex.", 4) == 0)) {
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
        }
    }
    if (strcmp(mnbuf, "kmovw") == 0) {
        unsigned kd;

        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_k_reg(dst->u.reg, &kd) != 0) {
            return -1;
        }
        return emit_i386_vex2_kmovw(src, kd, out, out_cap, out_len);
    }
    if (strncmp(mnbuf, "kand", 4) == 0 || strncmp(mnbuf, "kor", 3) == 0 || strncmp(mnbuf, "kxor", 4) == 0 ||
        strncmp(mnbuf, "kxnor", 5) == 0 || strncmp(mnbuf, "kadd", 4) == 0 || strncmp(mnbuf, "kunpck", 6) == 0) {
        return emit_x86_klogic(insn, intel_syntax, mnbuf, out, out_cap, out_len);
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
    case AS_OPERAND_REGISTER:
        dst->kind = AS_X86_OP_REG;
        if (parse_x86_reg(op->u.reg, &dst->u.reg) == 0) {
            dst->size_bits = parse_x86_reg_bits(op->u.reg);
            return 0;
        }
        {
            unsigned vr;

            if (parse_xmm_reg(op->u.reg, &vr) == 0) {
                dst->u.reg = (as_x86_reg_t)vr;
                dst->size_bits = 128;
                return 0;
            }
            if (parse_ymm_reg(op->u.reg, &vr) == 0) {
                dst->u.reg = (as_x86_reg_t)vr;
                dst->size_bits = 256;
                return 0;
            }
            if (parse_mmx_reg(op->u.reg, &vr) == 0) {
                dst->u.reg = (as_x86_reg_t)vr;
                dst->size_bits = 64;
                return 0;
            }
        }
        {
            snprintf(errbuf, errbuf_sz, "unknown x86 register: %s", op->u.reg != NULL ? op->u.reg : "<null>");
            return -1;
        }
    case AS_OPERAND_COPROCESSOR:
        dst->kind = AS_X86_OP_FPU;
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
            dst->u.imm = streq_ci(mnemonic, "push") ? 0x100 : 0;
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
        dst->u.mem.has_disp = 1;
        dst->u.mem.disp = 0;
        return 0;
    case AS_OPERAND_MEMORY:
        {
            unsigned base_bits = op->u.mem.base_reg != NULL ? parse_x86_reg_bits(op->u.mem.base_reg) : 0;
            unsigned index_bits = op->u.mem.index_reg != NULL ? parse_x86_reg_bits(op->u.mem.index_reg) : 0;
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
        if (!dst->u.mem.has_base && !dst->u.mem.has_index) {
            dst->u.mem.disp_only = 1;
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
        if (d->arg_count >= 2 &&
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
        while (need-- > 0) {
            if (bytebuf_append_u64_le(buf, (uint64_t)fill, 1) != 0) {
                return -1;
            }
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
    static char secbuf[256];
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
        if (*arg == '"' || *arg == '\'') {
            char q = *arg++;
            while (*arg != '\0' && *arg != q && n + 1 < sizeof(secbuf)) {
                secbuf[n++] = *arg++;
            }
            secbuf[n] = '\0';
            return secbuf;
        }
        for (i = 0; arg[i] != '\0' && n + 1 < sizeof(secbuf); ++i) {
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
} sec_buf_t;

typedef struct {
    sec_buf_t *items;
    size_t count;
    size_t cap;
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
    memset(v, 0, sizeof(*v));
}

static sec_buf_t *sec_buf_find(sec_buf_vec_t *v, const char *name) {
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
    if (suffix == 'q') {
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
    if (suffix == 'q') {
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
    if (suffix == 'q') {
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

    if (in == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0 || in->mnemonic == NULL ||
        (!streq_ci(in->mnemonic, "bzhi") && !streq_ci(in->mnemonic, "mulx") &&
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
    if (st->stack_count == st->stack_cap) {
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
    st->stack_count++;
    return 0;
}

static int section_track_apply_directive(section_track_t *st, const as_directive_t *d) {
    const char *sec_name;
    char *tmp;

    if (st == NULL || d == NULL || d->name == NULL) {
        return 0;
    }
    if (strcmp(d->name, ".previous") == 0) {
        tmp = st->current;
        st->current = st->previous;
        st->previous = tmp;
        return 1;
    }
    if (strcmp(d->name, ".popsection") == 0) {
        if (st->stack_count == 0) {
            return 1;
        }
        sec_name = st->stack[--st->stack_count];
        if (section_track_switch(st, sec_name) != 0) {
            free((char *)sec_name);
            return -1;
        }
        free((char *)sec_name);
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
    return elf_find_section(ctx->obj, name);
}

static int section_name_is_executable(emit_ctx_t *ctx, const char *name) {
    elf_section_t *sec = section_for_name(ctx, name);
    if (sec == NULL) {
        return 0;
    }
    return (elf_section_flags(sec) & SHF_EXECINSTR) != 0;
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
    int width_bits = 0;
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
    if (in->byte_op) {
        width_bits = 8;
    } else if (in->operand_size_override) {
        width_bits = 16;
    } else if (in->rex_w) {
        width_bits = 64;
    } else {
        width_bits = 32;
    }
    s42.width_bits = (unsigned)width_bits;
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

static int encode_x86_stmt(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code, size_t code_cap,
                           size_t *code_len, char *encerr, size_t encerr_sz) {
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

    intel_syntax = (st->u.instr.syntax_intel != 0) ? 1 : (cfg->intel_syntax != 0);
    memset(&in, 0, sizeof(in));
    if (normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        snprintf(encerr, encerr_sz, "unsupported mnemonic length");
        return -1;
    }
    in.mnemonic = mnbuf;
    in.default_bits = cfg->is_64 ? 64u : (cfg->x86_code_bits == 16u ? 16u : 32u);
    in.rel_is_disp = (cfg->x86_rel_is_disp != 0);
    in.force_rel32 = 0u;
    in.has_section_offset = (cfg->have_current_text_offset != 0);
    in.section_offset = cfg->current_text_offset;
    in.seg_override = map_seg(st->u.instr.segment_override);
    in.lock_prefix = (st->u.instr.prefixes & AS_PREFIX_LOCK) != 0;
    in.explicit_rex = (st->u.instr.prefixes & AS_PREFIX_REX) != 0;
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
    } else if (suffix == 'q') {
        in.rex_w = 1;
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

    if (!intel_syntax && in.op_count == 2) {
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
            fprintf(stderr, "as: warning: %s:%u: immediate truncated to 8 bits\n",
                    st->file != NULL ? st->file : "<input>", st->line);
        }
    }

    if (!cfg->is_64 && emit_i386_special(&st->u.instr, intel_syntax, code, code_cap, code_len) == 0) {
        if (*code_len > 0) {
            return 0;
        }
    }
    if (!cfg->is_64 || cfg->x86_64_isa_level >= 4) {
        if (try_encode_x86_avx512f_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_avx512bw_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_avx512bw_generic_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_avx512dq_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_avx512f_generic_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_avx512dq_generic_stmt(&st->u.instr, intel_syntax, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
    } else if (cfg->is_64 && x86_stmt_requires_v4(&st->u.instr, intel_syntax)) {
        set_x86_isa_requirement(encerr, encerr_sz, mnbuf, 4);
        return -1;
    }
    if (!cfg->is_64 || cfg->x86_64_isa_level >= 3) {
        if (try_encode_x86_avx_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_avx2_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_fma_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_bmi1_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_bmi2_stmt(&st->u.instr, intel_syntax, cfg->is_64, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
    } else if (cfg->is_64 && x86_stmt_requires_v3(&st->u.instr, intel_syntax, cfg->is_64)) {
        set_x86_isa_requirement(encerr, encerr_sz, mnbuf, 3);
        return -1;
    }
    if (cfg->is_64) {
        int s64 = emit_x86_64_special(&st->u.instr, intel_syntax, cfg->x86_64_isa_level, code, code_cap, code_len);
        if (s64 == 0) {
            if (*code_len > 0) {
                return 0;
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
    if (!cfg->is_64) {
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
        if (mem_addr_bits != 0 && mem_addr_bits != in.default_bits) {
            in.address_size_override = 1;
        }
    }
    if (in.seg_override == AS_X86_SEG_NONE) {
        for (j = 0; j < in.op_count; ++j) {
            const as_operand_t *op = &st->u.instr.operands[op_index[j]];
            if (op->kind == AS_OPERAND_MEMORY && op->u.mem.segment_reg != NULL) {
                in.seg_override = map_seg(op->u.mem.segment_reg);
                break;
            }
        }
    }

    if (cfg->is_64) {
        if (as_x86_encode_x86_64(&in, code, code_cap, code_len, encerr, encerr_sz) != 0) {
            return -1;
        }
    } else {
        if (try_encode_x86_sse3(&in, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_ssse3(&in, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_sse41(&in, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (try_encode_x86_sse42(&in, code, code_cap, code_len, encerr, encerr_sz) == 0 &&
            *code_len > 0) {
            return 0;
        }
        if (as_x86_encode_i386(&in, code, code_cap, code_len, encerr, encerr_sz) != 0) {
            return -1;
        }
    }
    return 0;
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




static int __attribute__((unused)) find_label_virtual_offset(emit_ctx_t *ctx, const char *section_name,
                                                             const as_stmt_t *base_st,
                                                             const char *file, unsigned line, int digit,
                                                             const char *sym_name, uint64_t *off_out) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || section_name == NULL || off_out == NULL ||
        (sym_name == NULL && file == NULL)) {
        return -1;
    }
    *off_out = 0;
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }
    ctx->virtual_scanning++;

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        size_t j;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            ctx->virtual_scanning--;
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        for (j = 0; j < st->label_count; ++j) {
            int matches = 0;

            if (strcmp(track.current, section_name) != 0) {
                continue;
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
                *off_out = (uint64_t)sb->buf.len;
                ctx->virtual_scanning--;
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
                ctx->virtual_scanning--;
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            {
                unsigned scalar_width = directive_fixed_scalar_width(&st->u.directive);
                if (scalar_width != 0) {
                    if (bytebuf_append_zeros(&sb->buf, st->u.directive.arg_count * (size_t)scalar_width) != 0) {
                        ctx->virtual_scanning--;
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    continue;
                }
            }
            if (append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                          track.x86_code_bits, &st->u.directive) < 0) {
                if (st->u.directive.name != NULL &&
                    (strcmp(st->u.directive.name, ".skip") == 0 ||
                     strcmp(st->u.directive.name, ".space") == 0 ||
                     strcmp(st->u.directive.name, ".fill") == 0)) {
                    continue;
                }
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                ctx->virtual_scanning--;
                return -1;
            }
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            if (append_virtual_instruction_bytes(ctx, &sb->buf, track.current, st, track.x86_code_bits) != 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    ctx->virtual_scanning--;
    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return -1;
}

static int virtual_label_cache_lookup(emit_ctx_t *ctx, const char *file, unsigned line, int digit,
                                      const char *sym_name, char *section_out, size_t section_out_sz,
                                      uint64_t *off_out) {
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
    for (i = 0; i < ctx->vlabel_count; ++i) {
        virtual_label_cache_t *c = &ctx->vlabel_cache[i];
        if (sym_name != NULL) {
            if (c->name == NULL || strcmp(c->name, sym_name) != 0) {
                continue;
            }
        } else {
            if (c->file == NULL || file == NULL || strcmp(c->file, file) != 0 ||
                c->line != line || c->digit != digit) {
                continue;
            }
        }
        if (c->off == UINT64_MAX) {
            return -1;
        }
        snprintf(section_out, section_out_sz, "%s", c->section != NULL ? c->section : "");
        *off_out = c->off;
        return 0;
    }
    return -1;
}

static int virtual_label_cache_lookup_stmt(emit_ctx_t *ctx, const as_stmt_t *stmt,
                                           char *section_out, size_t section_out_sz,
                                           uint64_t *off_out) {
    size_t i;

    if (ctx == NULL || stmt == NULL || section_out == NULL || off_out == NULL) {
        return -1;
    }
    for (i = ctx->vlabel_count; i > 0; --i) {
        virtual_label_cache_t *c = &ctx->vlabel_cache[i - 1];
        if (c->stmt != stmt) {
            continue;
        }
        if (c->off == UINT64_MAX) {
            return -1;
        }
        snprintf(section_out, section_out_sz, "%s", c->section != NULL ? c->section : "");
        *off_out = c->off;
        return 0;
    }
    return -1;
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
                                                (char[2]){0}, 2, &(uint64_t){0}) == 0) {
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
    return 0;
}

static void virtual_label_cache_free(emit_ctx_t *ctx) {
    size_t i;

    if (ctx == NULL) {
        return;
    }
    for (i = 0; i < ctx->vlabel_count; ++i) {
        free(ctx->vlabel_cache[i].name);
        free(ctx->vlabel_cache[i].file);
        free(ctx->vlabel_cache[i].section);
    }
    free(ctx->vlabel_cache);
    ctx->vlabel_cache = NULL;
    ctx->vlabel_count = 0;
    ctx->vlabel_cap = 0;
}

/* Lazy full-parse walk that populates vlabel_cache for every label in
 * the file in one shot. Currently unused - retained as a starting point
 * for future perf work on label resolution. */
static int __attribute__((unused)) prebuild_virtual_label_cache(emit_ctx_t *ctx) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || ctx->parsed == NULL) {
        return -1;
    }
    if (ctx->vlabel_count > 0) {
        return 0; /* assume populated */
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
    return 0;
}

static int find_label_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                       const char *file, unsigned line, int digit, const char *sym_name,
                                       char *section_out, size_t section_out_sz, uint64_t *off_out) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || section_out == NULL || section_out_sz == 0 || off_out == NULL ||
        (sym_name == NULL && file == NULL)) {
        return -1;
    }
    section_out[0] = '\0';
    *off_out = 0;
    if (virtual_label_cache_lookup(ctx, file, line, digit, sym_name, section_out, section_out_sz, off_out) == 0) {
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
        for (i = 0; i < ctx->parsed->count; ++i) {
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
                snprintf(section_out, section_out_sz, "%s", target_section);
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
                snprintf(section_out, section_out_sz, "%s", track.current != NULL ? track.current : "");
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
                                      char *section_out, size_t section_out_sz, uint64_t *off_out) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;
    int found = 0;

    if (ctx == NULL || target_st == NULL || section_out == NULL || section_out_sz == 0 || off_out == NULL) {
        return -1;
    }
    section_out[0] = '\0';
    *off_out = 0;
    if (virtual_label_cache_lookup_stmt(ctx, target_st, section_out, section_out_sz, off_out) == 0) {
        if (getenv("AS_PROFILE_LOC") != NULL) {
            static unsigned long h = 0; h++;
            if ((h & 0x3FFFF) == 0) fprintf(stderr, "find_stmt_loc HIT: %lu\n", h);
        }
        return 0;
    }
    if (getenv("AS_PROFILE_LOC") != NULL) {
        static unsigned long m = 0; m++;
        if ((m & 0xFF) == 0) fprintf(stderr, "find_stmt_loc MISS: %lu (target %p)\n", m, (void*)target_st);
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
        if (st == target_st && st->label_count > 0 && !found) {
            snprintf(section_out, section_out_sz, "%s", track.current != NULL ? track.current : "");
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
        if (e->symbol == NULL ||
            find_label_virtual_location(ctx, base_st, NULL, 0, 0, e->symbol,
                                        target_loc.section, sizeof(target_loc.section),
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
                find_stmt_virtual_location(ctx, target_st, target_loc.section, sizeof(target_loc.section),
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
                                            NULL, target_loc.section, sizeof(target_loc.section),
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
            if (find_stmt_virtual_location(ctx, target_st, out->section, sizeof(out->section), &off) != 0) {
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
                                    out->section, sizeof(out->section), &off) != 0) {
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

        if (e->symbol == NULL || strcmp(e->symbol, ".") == 0) {
            return -1;
        }
        if (find_label_virtual_location(ctx, base_st, NULL, 0, 0, e->symbol,
                                        out->section, sizeof(out->section), &off) != 0) {
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
            if (find_stmt_virtual_location(ctx, target_st, out->section, sizeof(out->section), &off) != 0) {
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
                                    out->section, sizeof(out->section), &off) != 0) {
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
    snprintf(v741b.section, sizeof(v741b.section), "%s", section_name);
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
        char section[128];

        if (s742 != NULL && s740 != NULL &&
            parsed_stmt_index(ctx, s742, &i742) == 0 &&
            parsed_stmt_index(ctx, s740, &i740) == 0 &&
            i740 <= i742 &&
            stmt_declared_label_section(ctx, s740, section, sizeof(section)) == 0 &&
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
    unsigned cc;
    long long disp8;

    if (cfg == NULL || st == NULL || code == NULL || code_len == NULL ||
        normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        return -1;
    }
    *code_len = 0;
    if (streq_ci(mnbuf, "call")) {
        long long disp32 = abs_target - ((long long)sec_off + 5);
        if (disp32 < INT32_MIN || disp32 > INT32_MAX || code_cap < 5) {
            snprintf(encerr, encerr_sz, "call target out of range");
            return -1;
        }
        code[0] = 0xe8;
        code[1] = (unsigned char)((uint32_t)disp32 & 0xffu);
        code[2] = (unsigned char)(((uint32_t)disp32 >> 8) & 0xffu);
        code[3] = (unsigned char)(((uint32_t)disp32 >> 16) & 0xffu);
        code[4] = (unsigned char)(((uint32_t)disp32 >> 24) & 0xffu);
        *code_len = 5;
        return 0;
    }
    if (streq_ci(mnbuf, "jmp")) {
        disp8 = abs_target - ((long long)sec_off + 2);
        if (allow_short && disp8 >= -128 && disp8 <= 127) {
            if (code_cap < 2) {
                return -1;
            }
            code[0] = 0xeb;
            code[1] = (unsigned char)(signed char)disp8;
            *code_len = 2;
            return 0;
        }
        if (suffix == 'b') {
            snprintf(encerr, encerr_sz, "jump target out of range");
            return -1;
        }
        if (!cfg->is_64 && cfg->x86_code_bits == 16u) {
            long long disp16 = abs_target - ((long long)sec_off + 3);
            if (disp16 < -32768 || disp16 > 32767 || code_cap < 3) {
                snprintf(encerr, encerr_sz, "jump target out of range");
                return -1;
            }
            code[0] = 0xe9;
            code[1] = (unsigned char)((uint16_t)disp16 & 0xffu);
            code[2] = (unsigned char)(((uint16_t)disp16 >> 8) & 0xffu);
            *code_len = 3;
            return 0;
        }
        {
            long long disp32 = abs_target - ((long long)sec_off + 5);
            if (disp32 < INT32_MIN || disp32 > INT32_MAX || code_cap < 5) {
                snprintf(encerr, encerr_sz, "jump target out of range");
                return -1;
            }
            code[0] = 0xe9;
            code[1] = (unsigned char)((uint32_t)disp32 & 0xffu);
            code[2] = (unsigned char)(((uint32_t)disp32 >> 8) & 0xffu);
            code[3] = (unsigned char)(((uint32_t)disp32 >> 16) & 0xffu);
            code[4] = (unsigned char)(((uint32_t)disp32 >> 24) & 0xffu);
            *code_len = 5;
            return 0;
        }
    }
    if (x86_cond_code(mnbuf, &cc) == 0) {
        disp8 = abs_target - ((long long)sec_off + 2);
        if (allow_short && disp8 >= -128 && disp8 <= 127) {
            if (code_cap < 2) {
                return -1;
            }
            code[0] = (unsigned char)(0x70u | cc);
            code[1] = (unsigned char)(signed char)disp8;
            *code_len = 2;
            return 0;
        }
        if (!cfg->is_64 && cfg->x86_code_bits == 16u) {
            long long disp16 = abs_target - ((long long)sec_off + 4);
            if (disp16 < -32768 || disp16 > 32767 || code_cap < 4) {
                snprintf(encerr, encerr_sz, "conditional branch target out of range");
                return -1;
            }
            code[0] = 0x0f;
            code[1] = (unsigned char)(0x80u | cc);
            code[2] = (unsigned char)((uint16_t)disp16 & 0xffu);
            code[3] = (unsigned char)(((uint16_t)disp16 >> 8) & 0xffu);
            *code_len = 4;
            return 0;
        }
        {
            long long disp32 = abs_target - ((long long)sec_off + 6);
            if (disp32 < INT32_MIN || disp32 > INT32_MAX || code_cap < 6) {
                snprintf(encerr, encerr_sz, "conditional branch target out of range");
                return -1;
            }
            code[0] = 0x0f;
            code[1] = (unsigned char)(0x80u | cc);
            code[2] = (unsigned char)((uint32_t)disp32 & 0xffu);
            code[3] = (unsigned char)(((uint32_t)disp32 >> 8) & 0xffu);
            code[4] = (unsigned char)(((uint32_t)disp32 >> 16) & 0xffu);
            code[5] = (unsigned char)(((uint32_t)disp32 >> 24) & 0xffu);
            *code_len = 6;
            return 0;
        }
    }
    return -1;
}

static int emit_resolved_x86_branch(const as_elf_cfg_t *cfg, const as_stmt_t *st, uint64_t sec_off,
                                    long long abs_target, unsigned char *code, size_t code_cap,
                                    size_t *code_len, char *encerr, size_t encerr_sz) {
    return emit_resolved_x86_branch_ex(cfg, st, sec_off, abs_target, 1, code, code_cap, code_len, encerr, encerr_sz);
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
    if (ctx->virtual_scanning > 0 && ctx->parsed != NULL && parsed_stmt_index(ctx, st, &cache_idx) == 0) {
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
                unsigned char code[32];
                size_t code_len = 0;
                char encerr[128];

                local_cfg.x86_code_bits = x86_code_bits;
                if (eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, e,
                                                    2, &abs_target) == 0 &&
                    emit_resolved_x86_branch(&local_cfg, st, sec_off, abs_target, code, sizeof(code),
                                             &code_len, encerr, sizeof(encerr)) == 0) {
                    if (code_len != 2 &&
                        eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, e,
                                                        code_len, &abs_target) == 0 &&
                        emit_resolved_x86_branch(&local_cfg, st, sec_off, abs_target, code, sizeof(code),
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
            if (stmt_virtual_size_in_section(ctx, section_name, st, total, x86_code_bits, &n) == 0) {
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

static int stmt_declared_label_section(emit_ctx_t *ctx, const as_stmt_t *target_st,
                                       char *section_out, size_t section_out_sz) {
    size_t idx;

    if (ctx == NULL || target_st == NULL || section_out == NULL || section_out_sz == 0) {
        return -1;
    }
    section_out[0] = '\0';
    if (build_stmt_section_at(ctx) != 0) {
        return -1;
    }
    if (parsed_stmt_index(ctx, target_st, &idx) != 0 || idx >= ctx->stmt_section_at_count) {
        return -1;
    }
    if (ctx->stmt_section_at[idx] != NULL) {
        snprintf(section_out, section_out_sz, "%s", ctx->stmt_section_at[idx]);
    }
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
        target_st = NULL;
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
    } else {
        return -1;
    }
    if (target_st == NULL || parsed_stmt_index(ctx, target_st, &target_idx) != 0 || target_idx == base_idx) {
        return -1;
    }
    {
        char target_section[128];

        if (stmt_declared_label_section(ctx, target_st, target_section, sizeof(target_section)) != 0 ||
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

static unsigned stmt_local_rel_virtual_len(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st) {
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
    e = op->u.expr;
    is_local = ((e != NULL && expr_has_local_ref(e)) ||
                (section_name != NULL && strcmp(section_name, ".altinstr_replacement") != 0 &&
                 e != NULL && expr_is_local_temp_symbol(e) &&
                 local_temp_branch_target_within(ctx, st, e, 64)) ||
                raw_is_numeric_local_ref(op->raw));
    if (is_call_mnemonic(st->u.instr.mnemonic)) {
        return 5u;
    }
    if (is_local || is_fixed_short_rel_mnemonic(st->u.instr.mnemonic)) {
        return 2u;
    }
    if (streq_ci(mnbuf, "jmp")) {
        return 5u;
    }
    if (x86_cond_code(mnbuf, &cc) == 0) {
        return 6u;
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

            local_cfg.x86_code_bits = x86_code_bits;
            if (eval_direct_local_branch_target(ctx, section_name, st, (uint64_t)buf->len, x86_code_bits, e,
                                                2, &abs_target) == 0 &&
                emit_resolved_x86_branch(&local_cfg, st, (uint64_t)buf->len, abs_target, code, sizeof(code),
                                         &code_len, encerr, sizeof(encerr)) == 0) {
                if (code_len != 2 &&
                    eval_direct_local_branch_target(ctx, section_name, st, (uint64_t)buf->len, x86_code_bits, e,
                                                    code_len, &abs_target) == 0 &&
                    emit_resolved_x86_branch(&local_cfg, st, (uint64_t)buf->len, abs_target, code, sizeof(code),
                                             &code_len, encerr, sizeof(encerr)) != 0) {
                    return -1;
                }
                return bytebuf_append(buf, code, code_len);
            }
        }
    }
    {
        unsigned local_rel_len = stmt_local_rel_virtual_len(ctx, section_name, st);
        if (local_rel_len != 0) {
            return bytebuf_append_zeros(buf, local_rel_len);
        }
    }
    stmt_cfg = *ctx->cfg;
    stmt_cfg.x86_code_bits = x86_code_bits;
    stmt_cfg.have_current_text_offset = 1u;
    stmt_cfg.current_text_offset = (uint64_t)buf->len;
    if (encode_x86_stmt(ctx, &stmt_cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
        return -1;
    }
    return bytebuf_append(buf, code, code_len);
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
            if (eval_local_rel_expr_virtual(ctx, section_name, st, 0, x86_code_bits, expr, &target) != 0) {
                as_expr_free(expr);
                return -1;
            }
            as_expr_free(expr);
        }
        if (target < 0 || (uint64_t)target < sec_off) {
            return -1;
        }
        if ((uint64_t)target > sec_off &&
            bytebuf_append_zeros(buf, (size_t)((uint64_t)target - sec_off)) != 0) {
            return -1;
        }
        return 1;
    }
    if (strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 || strcmp(d->name, ".p2align") == 0) {
        long long raw = 0;
        long long fill = section_name_is_executable(ctx, section_name) ? 0x90 : 0;
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
            if (raw >= (long long)(sizeof(size_t) * CHAR_BIT - 1)) {
                return -1;
            }
            align = (size_t)1u << (unsigned)raw;
        } else {
            align = (size_t)raw;
        }
        if (align == 0 || (align & (align - 1)) != 0) {
            return -1;
        }
        need = (align - (buf->len & (align - 1))) & (align - 1);
        while (need-- > 0) {
            if (bytebuf_append_u64_le(buf, (uint64_t)fill, 1) != 0) {
                return -1;
            }
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
        if (d->args[0] != NULL && strstr(d->args[0], "0b") != NULL && strstr(d->args[0], "-") != NULL &&
            strstr(d->args[0], ".") != NULL) {
            repeat = 1;
        } else if (parse_int64(d->args[0], &repeat) != 0 && parse_const_expr_string(d->args[0], &repeat) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->file : NULL,
                                                  st != NULL ? st->line : 0);
            if (expr == NULL ||
                (eval_abs_local_diff_expr_virtual(ctx, st, expr, &repeat) != 0 &&
                 eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, &repeat) != 0 &&
                 eval_local_align_fill_expr(ctx, section_name, st, sec_off, x86_code_bits, d->args[0], &repeat) != 0)) {
                if (d->args[0] != NULL && strstr(d->args[0], "0b") != NULL && strstr(d->args[0], "- .") != NULL) {
                    repeat = 1;
                    as_expr_free(expr);
                    expr = NULL;
                } else {
                as_expr_free(expr);
                return -1;
                }
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
        if (repeat < 0 || repeat > (1 << 20)) {
            return -1;
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
                if (strcmp(d->name, ".space") == 0 || strcmp(d->name, ".skip") == 0) {
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
        if (count > (1 << 20)) {
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

static int append_directive_data_location_pass(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name,
                                               const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                                               const as_directive_t *d) {
    return append_directive_data_ctx(ctx, buf, section_name, st, sec_off, x86_code_bits, d);
}

static int encode_x86_stmt_for_layout(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                                      const as_stmt_t *st, unsigned char *code, size_t code_cap,
                                      size_t *code_len, char *encerr, size_t encerr_sz) {
    as_elf_cfg_t stmt_cfg;

    if (ctx == NULL || ctx->cfg == NULL || st == NULL) {
        return -1;
    }
    stmt_cfg = *ctx->cfg;
    stmt_cfg.x86_code_bits = x86_code_bits;
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

        if ((op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            op->u.expr != NULL &&
            op->kind == AS_OPERAND_LABEL_REF &&
            !is_fixed_short_rel_mnemonic(st->u.instr.mnemonic) &&
            !expr_has_local_ref(op->u.expr) &&
            !expr_is_local_temp_symbol(op->u.expr) &&
            !raw_is_numeric_local_ref(op->raw)) {
            stmt_cfg.have_current_text_offset = 0u;
            stmt_cfg.current_text_offset = 0u;
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
            virtual_addr_value_t target;

            if ((rel_expr->kind == AS_EXPR_LOCAL_REF || expr_is_local_temp_symbol(rel_expr)) &&
                eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, rel_expr,
                                                2, &abs_target) == 0) {
                size_t tmp_len = 0;
                if (emit_resolved_x86_branch(&local_cfg, st, sec_off, abs_target, code, code_cap,
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
                int rc = emit_resolved_x86_branch(&local_cfg, &tmp, sec_off, abs_target, code, code_cap,
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
                !streq_ci(op->u.mem.base_reg, "rip") ||
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
            drc = append_directive_data_location_pass(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
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
            if (sb == NULL) {
                sb = sec_buf_get_or_add(&secbufs, track.current);
                if (sb == NULL) {
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
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
        if (elf_section_set_data(sec, secbufs.items[i].buf.data, secbufs.items[i].buf.len) != ELF_OK) {
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
        return STT_OBJECT;
    case AS_SYM_TYPE_TLS_OBJECT:
        return STT_TLS;
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

static uint32_t reloc_type_for_symbol(unsigned machine, const char *name, uint32_t fallback) {
    if (name == NULL) {
        return fallback;
    }
    if (machine == EM_386) {
        if (strstr(name, "@PLT") != NULL) {
            return R_386_PLT32;
        }
        return fallback;
    }
    if (machine == EM_X86_64) {
        if (strstr(name, "@PLT") != NULL) {
            return R_X86_64_PLT32;
        }
        if (strstr(name, "@GOTPCREL") != NULL) {
            return R_X86_64_GOTPCREL;
        }
        if (strstr(name, "@GOTTPOFF") != NULL) {
            return R_X86_64_GOTTPOFF;
        }
        return fallback;
    }
    return fallback;
}

static void strip_reloc_modifier(char *name) {
    char *at;

    if (name == NULL) {
        return;
    }
    at = strchr(name, '@');
    if (at == NULL || at[1] == '\0') {
        return;
    }
    if (streq_ci(at + 1, "PLT") ||
        streq_ci(at + 1, "GOTPCREL") ||
        streq_ci(at + 1, "GOTTPOFF")) {
        *at = '\0';
    }
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
        sym = elf_add_symbol(ctx->obj, name, 0, 0, STB_GLOBAL, STT_NOTYPE);
    }
    if (sym == NULL) {
        return NULL;
    }
    if (sec != NULL) {
        (void)elf_symbol_define(sym, sec, 0);
    }
    if (append_emit_symbol(ctx, name, sym) != 0) {
        return NULL;
    }
    return sym;
}

static const as_symbol_t *find_as_symbol_const(const as_symtab_t *symtab, const char *name) {
    size_t i;

    if (symtab == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < symtab->count; ++i) {
        if (symtab->items[i].name != NULL && strcmp(symtab->items[i].name, name) == 0) {
            return &symtab->items[i];
        }
    }
    return NULL;
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
    }

    for (i = 0; i < secbufs.count; ++i) {
        elf_section_t *sec = section_for_name(ctx, secbufs.items[i].name);
        if (sec == NULL) {
            continue;
        }
        if (elf_section_set_data(sec, secbufs.items[i].buf.data, secbufs.items[i].buf.len) != ELF_OK) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
    }
    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return 0;
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
            drc = append_directive_data_location_pass(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
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
        free_name_index(&loc_index);
        return -1;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase dot locations done count=%zu\n", dot_count);

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;
        unsigned long long sym_size = s->size;

        if (is_local_temp_symbol_name(s->name)) {
            continue;
        }
        if (s->alias_target != NULL && s->name != NULL && strcmp(s->alias_target, s->name) == 0) {
            continue;
        }
        if (s->size_target_symbol != NULL) {
            const sym_loc_t *target_loc = find_sym_loc(locs, &loc_index, s->size_target_symbol);
            elf_section_t *base_sec = NULL;
            uint64_t base_off = 0;

            if (target_loc == NULL || target_loc->sec == NULL) {
                set_err(ctx, "cannot resolve size target for %s", s->name);
                free_sym_locs(locs, loc_count);
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
                const as_symbol_t *base_sym = find_as_symbol_const(symtab, s->size_base_symbol);
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
            if (base_sec != target_loc->sec || base_off < target_loc->off) {
                set_err(ctx, "invalid symbolic size for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            sym_size = (unsigned long long)(base_off - target_loc->off);
        }

        esym = elf_add_symbol(ctx->obj, s->name, 0, sym_size, map_bind(s->bind), map_type(s->type));
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (elf_symbol_set_visibility(esym, map_vis(s->visibility)) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (s->version != NULL) {
            const char *ver = s->version;
            const char *at = strrchr(ver, '@');
            int is_default = 0;
            const char *ver_name = NULL;

            if (at != NULL && at > ver && at[1] != '\0') {
                if (at > ver && at[-1] == '@') {
                    is_default = 1;
                }
                ver_name = at + 1;
            }
            if (ver_name == NULL ||
                elf_symbol_set_version_name(esym, ver_name, is_default) != ELF_OK) {
                set_err(ctx, "invalid .symver mapping for %s", s->name ? s->name : "<anon>");
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
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

        if (is_local_temp_symbol_name(s->name)) {
            continue;
        }
        if (s->alias_target != NULL && s->name != NULL && strcmp(s->alias_target, s->name) == 0) {
            continue;
        }
        esym = ensure_emit_symbol(ctx, s->name);
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_name_index(&loc_index);
            return -1;
        }
        if (s->is_common) {
            if (elf_symbol_set_shndx(esym, SHN_COMMON) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_name_index(&loc_index);
                return -1;
            }
            if (elf_symbol_set_value(esym, s->common_size) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_name_index(&loc_index);
                return -1;
            }
        } else if (s->is_absolute) {
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK ||
                elf_symbol_set_value(esym, s->absolute_value) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_name_index(&loc_index);
                return -1;
            }
        } else if (s->defined) {
            if (s->alias_target != NULL) {
                continue;
            }
            loc = find_sym_loc(locs, &loc_index, s->name);
            if (loc != NULL && loc->sec != NULL) {
                if (elf_symbol_define(esym, loc->sec, loc->off) != ELF_OK) {
                    free_sym_locs(locs, loc_count);
                    free_name_index(&loc_index);
                    return -1;
                }
            } else if (ctx->text_sec != NULL && elf_symbol_define(esym, ctx->text_sec, 0) != ELF_OK) {
                free_sym_locs(locs, loc_count);
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

        if (is_local_temp_symbol_name(s->name)) {
            continue;
        }
        if (s->alias_target != NULL && s->name != NULL && strcmp(s->alias_target, s->name) == 0) {
            continue;
        }
        if (s->alias_target == NULL) {
            continue;
        }
        esym = ensure_emit_symbol(ctx, s->name);
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_name_index(&loc_index);
            return -1;
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
        target = ensure_emit_symbol(ctx, s->alias_target);
        if (target == NULL) {
            free_sym_locs(locs, loc_count);
            free_name_index(&loc_index);
            return -1;
        }
        shndx = elf_symbol_shndx(target);
        if (elf_symbol_set_shndx(esym, shndx) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_name_index(&loc_index);
            return -1;
        }
        v = (int64_t)elf_symbol_value(target) + s->alias_addend;
        if (elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_name_index(&loc_index);
            return -1;
        }
    }

    free_sym_locs(locs, loc_count);
    free_dot_locs(dot_locs);
    free_name_index(&loc_index);
    return 0;
}

static int add_reloc_for_symbol_ex(emit_ctx_t *ctx, elf_section_t *sec, const char *name,
                                   uint64_t offset, uint32_t fallback_type, int64_t addend) {
    char *sym_name;
    elf_symbol_t *sym;
    uint32_t rtype;

    sym_name = xstrdup(name);
    if (sym_name == NULL) {
        return -1;
    }
    strip_reloc_modifier(sym_name);
    if (sym_name[0] == '\0') {
        free(sym_name);
        return -1;
    }
    if (is_local_temp_symbol_name(sym_name)) {
        char target_section[128];
        uint64_t target_off = 0;

        if (find_label_virtual_location(ctx, NULL, NULL, 0, 0, sym_name,
                                        target_section, sizeof(target_section), &target_off) == 0) {
            char *section_name = xstrdup(target_section);
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
    rtype = reloc_type_for_symbol(ctx->cfg != NULL ? ctx->cfg->machine : EM_386, name, fallback_type);
    if (elf_add_relocation(sec, offset, sym, rtype, addend) != ELF_OK) {
        free(sym_name);
        return -1;
    }
    free(sym_name);
    return 0;
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

static int expr_symbol_addend(const as_expr_t *e, const char **sym_out, int64_t *add_out, int sign) {
    if (e == NULL || sym_out == NULL || add_out == NULL) {
        return -1;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        *add_out += (int64_t)(sign * e->value);
        return 0;
    case AS_EXPR_SYMBOL:
        if (e->symbol == NULL) {
            return -1;
        }
        if (*sym_out == NULL) {
            *sym_out = e->symbol;
            return 0;
        }
        return strcmp(*sym_out, e->symbol) == 0 ? 0 : -1;
    case AS_EXPR_BINARY:
        if (e->op == AS_EXPR_OP_ADD) {
            return expr_symbol_addend(e->lhs, sym_out, add_out, sign) == 0 &&
                           expr_symbol_addend(e->rhs, sym_out, add_out, sign) == 0
                       ? 0
                       : -1;
        }
        if (e->op == AS_EXPR_OP_SUB) {
            return expr_symbol_addend(e->lhs, sym_out, add_out, sign) == 0 &&
                           expr_symbol_addend(e->rhs, sym_out, add_out, -sign) == 0
                       ? 0
                       : -1;
        }
        return -1;
    case AS_EXPR_UNARY:
        if (e->op == AS_EXPR_OP_NEG) {
            return expr_symbol_addend(e->lhs, sym_out, add_out, -sign);
        }
        return -1;
    default:
        return -1;
    }
}

static int expr_symbol_addend_with_local(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st,
                                         unsigned x86_code_bits, const as_expr_t *e, const char **sym_out,
                                         int64_t *add_out, int sign) {
    long long v;

    if (e == NULL || sym_out == NULL || add_out == NULL) {
        return -1;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        *add_out += (int64_t)(sign * e->value);
        return 0;
    case AS_EXPR_LOCAL_REF:
        if (eval_local_rel_expr_virtual(ctx, section_name, base_st, 0, x86_code_bits, e, &v) != 0) {
            return -1;
        }
        *add_out += (int64_t)(sign * v);
        return 0;
    case AS_EXPR_SYMBOL:
        if (e->symbol == NULL || strcmp(e->symbol, ".") == 0) {
            return -1;
        }
        if (*sym_out == NULL) {
            *sym_out = e->symbol;
            return 0;
        }
        return strcmp(*sym_out, e->symbol) == 0 ? 0 : -1;
    case AS_EXPR_BINARY:
        if (e->op == AS_EXPR_OP_ADD) {
            return expr_symbol_addend_with_local(ctx, section_name, base_st, x86_code_bits, e->lhs,
                                                 sym_out, add_out, sign) == 0 &&
                           expr_symbol_addend_with_local(ctx, section_name, base_st, x86_code_bits, e->rhs,
                                                         sym_out, add_out, sign) == 0
                       ? 0
                       : -1;
        }
        if (e->op == AS_EXPR_OP_SUB) {
            return expr_symbol_addend_with_local(ctx, section_name, base_st, x86_code_bits, e->lhs,
                                                 sym_out, add_out, sign) == 0 &&
                           expr_symbol_addend_with_local(ctx, section_name, base_st, x86_code_bits, e->rhs,
                                                         sym_out, add_out, -sign) == 0
                       ? 0
                       : -1;
        }
        return -1;
    case AS_EXPR_UNARY:
        if (e->op == AS_EXPR_OP_NEG) {
            return expr_symbol_addend_with_local(ctx, section_name, base_st, x86_code_bits, e->lhs,
                                                 sym_out, add_out, -sign);
        }
        return -1;
    default:
        return -1;
    }
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

static int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out) {
    char *tmp;
    char *compact;
    size_t i;
    size_t n;
    char *sep = NULL;
    long long v = 0;

    if (sym_out == NULL || add_out == NULL) {
        return -1;
    }
    *sym_out = NULL;
    *add_out = 0;
    tmp = trim_copy_arg(arg);
    if (tmp == NULL) {
        return -1;
    }
    if (tmp[0] == '\0') {
        free(tmp);
        return -1;
    }
    if (parse_int64(tmp, &v) == 0) {
        free(tmp);
        return 0;
    }

    n = strlen(tmp);
    compact = (char *)malloc(n + 1);
    if (compact == NULL) {
        free(tmp);
        return -1;
    }
    n = 0;
    for (i = 0; tmp[i] != '\0'; ++i) {
        if (!isspace((unsigned char)tmp[i])) {
            compact[n++] = tmp[i];
        }
    }
    compact[n] = '\0';
    free(tmp);
    while (n >= 2 && compact[0] == '(' && compact[n - 1] == ')') {
        int depth = 0;
        int wraps = 1;
        for (i = 0; i < n; ++i) {
            if (compact[i] == '(') {
                depth++;
            } else if (compact[i] == ')') {
                depth--;
                if (depth == 0 && i + 1 < n) {
                    wraps = 0;
                    break;
                }
            }
            if (depth < 0) {
                wraps = 0;
                break;
            }
        }
        if (!wraps || depth != 0) {
            break;
        }
        memmove(compact, compact + 1, n - 2);
        n -= 2;
        compact[n] = '\0';
    }

    for (i = 1; compact[i] != '\0'; ++i) {
        if (compact[i] == '+' || compact[i] == '-') {
            sep = &compact[i];
            break;
        }
    }
    if (sep != NULL) {
        char *terms;
        char *p;
        int sign;

        terms = xstrdup(sep);
        if (terms == NULL) {
            free(compact);
            return -1;
        }
        *sep = '\0';
        p = terms;
        while (*p != '\0') {
            char *term_start;
            char *term_end;
            char saved;
            long long addv;

            sign = 1;
            if (*p == '+' || *p == '-') {
                sign = *p == '-' ? -1 : 1;
                ++p;
            }
            term_start = p;
            while (*p != '\0' && *p != '+' && *p != '-') {
                ++p;
            }
            term_end = p;
            if (term_end == term_start) {
                free(terms);
                free(compact);
                return -1;
            }
            saved = *term_end;
            *term_end = '\0';
            if (parse_int64(term_start, &addv) != 0) {
                *term_end = saved;
                free(terms);
                free(compact);
                return -1;
            }
            *term_end = saved;
            *add_out += (int64_t)(sign > 0 ? addv : -addv);
        }
        free(terms);
    }
    if (compact[0] == '\0') {
        free(compact);
        return -1;
    }
    *sym_out = compact;
    return 0;
}

typedef struct {
    char *name;
    unsigned type;
    unsigned flags;
    unsigned align;
    int touched;
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
        if (d->arg_count >= 2 && parse_nonneg_u64_or_reloc(ctx, st, d->args[1], ".align fill", &fill) != 0) {
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
        while (need-- > 0) {
            if (bytebuf_append_u64_le(&sec->buf, fill, 1) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
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
        set_err(ctx, "failed to open output file: %s", out_path);
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
    if (!(cfg->machine == EM_386 || cfg->machine == EM_X86_64)) {
        if (errbuf != NULL && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "binary output currently supports x86/i386 targets only");
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
            if (append_data_directive_binary(&ctx, st, sec, &handled) != 0) {
                goto fail;
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
            streq_ci(op->u.mem.base_reg, "rip")) {
            return R_X86_64_PC32;
        }
        if (op != NULL && op->kind == AS_OPERAND_MEMORY) {
            return R_X86_64_32S;
        }
        return R_X86_64_64;
    }
    return reloc_type_for_machine(machine);
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

    if (code_len == 2 &&
        (code[0] == 0xeb || (code[0] >= 0x70 && code[0] <= 0x7f) || (code[0] >= 0xe0 && code[0] <= 0xe3))) {
        *type = (machine == EM_X86_64) ? R_X86_64_PC8 : R_386_PC8;
        *width = 1;
        return;
    }

    if ((code_len == 5 && (code[0] == 0xe8 || code[0] == 0xe9)) ||
        (code_len == 6 && code[0] == 0x0f && (code[1] & 0xf0) == 0x80)) {
        if (machine == EM_X86_64) {
            *type = streq_ci(in->mnemonic, "call") ? R_X86_64_PLT32 : R_X86_64_PC32;
        } else {
            *type = streq_ci(in->mnemonic, "call") ? R_386_PLT32 : R_386_PC32;
        }
        *width = 4;
        return;
    }

    if (machine == EM_386 && code_len == 3 && (code[0] == 0xe8 || code[0] == 0xe9)) {
        *type = R_386_PC16;
        *width = 2;
        return;
    }

    if (machine == EM_386 && code_len == 4 && code[0] == 0x0f && (code[1] & 0xf0) == 0x80) {
        *type = R_386_PC16;
        *width = 2;
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
    char section[128];
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
                                const char *name, const local_emit_label_t **out) {
    size_t i;

    if (labels == NULL || name == NULL || out == NULL) {
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

static int emit_relocations(emit_ctx_t *ctx) {
    size_t i;
    unsigned machine = ctx->cfg != NULL ? ctx->cfg->machine : EM_386;
    sec_buf_vec_t secbufs;
    section_track_t track;
    local_emit_label_t local_labels[8192];
    size_t local_label_count = 0;
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

        for (j = 0; j < st->label_count; ++j) {
            int label_digit = -1;
            if (local_label_count < sizeof(local_labels) / sizeof(local_labels[0]) &&
                (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 ||
                 is_local_temp_symbol_name(st->labels[j].name))) {
                local_emit_label_t *dst = &local_labels[local_label_count++];
                dst->digit = label_digit;
                dst->name = st->labels[j].name;
                dst->file = st->labels[j].file;
                dst->line = st->labels[j].line;
                dst->off = cur_off;
                snprintf(dst->section, sizeof(dst->section), "%s", track.current != NULL ? track.current : "");
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

            drc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                            track.x86_code_bits, d);
            if (drc < 0) {
                set_err(ctx, "%s:%u: malformed directive data", st->file != NULL ? st->file : "<input>", st->line);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (width != 0) {
                for (j = 0; j < d->arg_count; ++j) {
                    char *sym = NULL;
                    int64_t addend = 0;
                    uint64_t reloc_off;
                    long long const_value = 0;
                    if (parse_int64(d->args[j], &const_value) == 0 ||
                        parse_const_expr_string(d->args[j], &const_value) == 0 ||
                        eval_arg_asm_vars(ctx, st, d->args[j], &const_value) == 0) {
                        continue;
                    }
                    if (parse_symbol_addend_arg(d->args[j], &sym, &addend) == 0 && sym != NULL) {
                        uint32_t t = reloc_type_for_machine(machine);
                        long long asm_value;
                        if (asm_var_lookup(ctx, sym, &asm_value) == 0) {
                            free(sym);
                            continue;
                        }
                        reloc_off = cur_off + (uint64_t)(j * width);
                        if (machine_relocation_addend_is_in_place(machine) &&
                            reloc_off + width <= (uint64_t)sb->buf.len) {
                            write_u64_le_at(sb->buf.data + reloc_off, (uint64_t)addend, width);
                        }
                        if (add_reloc_for_symbol_ex(ctx, cur_sec, sym, reloc_off, t, addend) != 0) {
                            free(sym);
                            section_track_free(&track);
                            sec_buf_vec_free(&secbufs);
                            return -1;
                        }
                        free(sym);
                        continue;
                    }
                    {
                        as_expr_t *expr = as_parse_expr_string(d->args[j], st->file, st->line);
                        if (expr != NULL && width == 4 &&
                            expr->kind == AS_EXPR_BINARY &&
                            expr->op == AS_EXPR_OP_SUB &&
                            expr->lhs != NULL &&
                            expr->rhs != NULL &&
                            expr->rhs->kind == AS_EXPR_SYMBOL &&
                            expr->rhs->symbol != NULL &&
                            strcmp(expr->rhs->symbol, ".") == 0) {
                            uint32_t t = (machine == EM_X86_64) ? R_X86_64_PC32 : R_386_PC32;
                            char *target_name = NULL;
                            int handled = 0;

                            if (expr->lhs->kind == AS_EXPR_LOCAL_REF) {
                                const local_emit_label_t *seen = NULL;
                                virtual_addr_value_t target;
                                if (find_seen_local_label(local_labels, local_label_count, expr->lhs, &seen) == 0) {
                                    target_name = xstrdup(seen->section);
                                    addend = (int64_t)seen->off;
                                    handled = (target_name != NULL);
                                } else if (local_ref_virtual_location(ctx, st, expr->lhs, &target) == 0 &&
                                           target.has_section) {
                                    target_name = xstrdup(target.section);
                                    addend = (int64_t)target.value;
                                    handled = (target_name != NULL);
                                }
                            } else if (expr->lhs->kind == AS_EXPR_SYMBOL &&
                                       expr->lhs->symbol != NULL &&
                                       strcmp(expr->lhs->symbol, ".") != 0) {
                                char target_section[128];
                                uint64_t target_off = 0;
                                if (is_local_temp_symbol_name(expr->lhs->symbol)) {
                                    const local_emit_label_t *seen = NULL;
                                    if (find_seen_temp_label(local_labels, local_label_count, expr->lhs->symbol,
                                                             &seen) == 0) {
                                        target_name = xstrdup(seen->section);
                                        addend = (int64_t)seen->off;
                                        handled = (target_name != NULL);
                                    } else if (find_label_virtual_location(ctx, st, NULL, 0, 0, expr->lhs->symbol,
                                                                           target_section, sizeof(target_section),
                                                                           &target_off) == 0) {
                                        target_name = xstrdup(target_section);
                                        addend = (int64_t)target_off;
                                        handled = (target_name != NULL);
                                    }
                                } else {
                                    target_name = xstrdup(expr->lhs->symbol);
                                    addend = 0;
                                    handled = (target_name != NULL);
                                }
                            }
                            if (handled) {
                                reloc_off = cur_off + (uint64_t)(j * width);
                                if (machine_relocation_addend_is_in_place(machine) &&
                                    reloc_off + width <= (uint64_t)sb->buf.len) {
                                    write_u64_le_at(sb->buf.data + reloc_off, (uint64_t)addend, width);
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

            if (encode_x86_stmt_for_layout(ctx, track.current, cur_off, track.x86_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
                set_err(ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, encerr);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
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
                uint32_t t;
                uint64_t reloc_off;
                uint64_t reloc_width = 4;
                int64_t addend = 0;

                if (asm_reg_alias_for_operand(ctx, op) != NULL) {
                    continue;
                }
                if (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) {
                    e = op->u.expr;
                } else if (op->kind == AS_OPERAND_MEMORY) {
                    e = op->u.mem.disp;
                }
                if (e != NULL) {
                    long long resolved_disp;
                    int skip_local_resolve = 0;
                    int can_resolve_without_reloc =
                        (e->kind == AS_EXPR_LOCAL_REF || expr_is_local_temp_symbol(e) ||
                         is_fixed_short_rel_mnemonic(st->u.instr.mnemonic));
                    if (machine == EM_X86_64 &&
                        op->kind == AS_OPERAND_MEMORY &&
                        op->u.mem.base_reg != NULL &&
                        streq_ci(op->u.mem.base_reg, "rip")) {
                        skip_local_resolve = 1;
                    }
                    if (e->kind == AS_EXPR_SYMBOL &&
                        (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
                        is_call_mnemonic(st->u.instr.mnemonic)) {
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
                {
                    const char *expr_sym = NULL;
                    int64_t expr_addend = 0;
                    if ((expr_symbol_addend(e, &expr_sym, &expr_addend, 1) == 0 ||
                         expr_symbol_addend_with_local(ctx, track.current, st, track.x86_code_bits, e,
                                                       &expr_sym, &expr_addend, 1) == 0) &&
                        expr_sym != NULL && strcmp(expr_sym, sym) == 0) {
                        addend = expr_addend;
                    }
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
                if (code_len < reloc_width) {
                    /* cc-emitted dead-code from skipped __always_inline
                     * helpers can produce a byte-immediate instruction
                     * referencing a 4-byte symbol relocation. The body
                     * is unreachable but as still has to lay it out.
                     * Clamp the relocation to the encoded slot. */
                    reloc_width = code_len;
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
                           streq_ci(op->u.mem.base_reg, "rip")) {
                    addend += -4;
                }
                if (rel_count > 0) {
                    set_err(ctx, "%s:%u: multiple symbolic relocations in one x86 instruction are not yet supported",
                            st->file != NULL ? st->file : "<input>", st->line);
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                reloc_off = cur_off + (uint64_t)code_len - reloc_width;
                if (machine_relocation_addend_is_in_place(machine)) {
                    uint64_t code_rel_off = reloc_off - cur_off;
                    if (code_rel_off + reloc_width <= (uint64_t)code_len) {
                        write_u64_le_at(code + code_rel_off, (uint64_t)addend, (unsigned)reloc_width);
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
                if (add_reloc_for_symbol_ex(ctx, cur_sec, sym, reloc_off, t, addend) != 0) {
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                rel_count++;
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
        if (elf_section_set_data(sec, secbufs.items[i].buf.data, secbufs.items[i].buf.len) != ELF_OK) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
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

    cls = cfg->is_64 ? ELFOBJ_CLASS_64 : ELFOBJ_CLASS_32;
    ctx.obj = elf_create(ET_REL, (uint16_t)cfg->machine, cls, ELFOBJ_ENDIAN_LE);
    if (ctx.obj == NULL) {
        set_err(&ctx, "elf_create failed");
        return -1;
    }

    for (i = 0; i < sections->count; ++i) {
        const as_section_t *s = &sections->items[i];
        elf_section_t *es = elf_add_section(ctx.obj, s->name, s->type, s->flags);
        if (es == NULL) {
            set_err(&ctx, "failed to add section %s", s->name);
            goto fail;
        }
        if (elf_section_set_align(es, s->align > 0 ? s->align : 1) != ELF_OK) {
            set_err(&ctx, "failed to set align on %s", s->name);
            goto fail;
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
    if (has_file_loc) {
        if (ensure_section_exists(&ctx, ".debug_line", SHT_PROGBITS, 0, 1, NULL, 0) != 0) {
            set_err(&ctx, "failed to emit .debug_line");
            goto fail;
        }
    }
    if (has_cfi) {
        if (ensure_section_exists(&ctx, ".eh_frame", SHT_PROGBITS, SHF_ALLOC, 4, NULL, 0) != 0 ||
            ensure_section_exists(&ctx, ".eh_frame_hdr", SHT_PROGBITS, SHF_ALLOC, 4, NULL, 0) != 0) {
            set_err(&ctx, "failed to emit .eh_frame/.eh_frame_hdr");
            goto fail;
        }
    }

    if (elf_finalize(ctx.obj) != ELF_OK) {
        set_err(&ctx, "elf_finalize failed");
        goto fail;
    }
    if (elf_write_file(ctx.obj, out_path) != ELF_OK) {
        set_err(&ctx, "elf_write_file failed: %s", out_path);
        goto fail;
    }

    for (i = 0; i < ctx.sym_count; ++i) {
        free(ctx.sym_map[i].name);
    }
    free(ctx.sym_map);
    free(ctx.sym_index);
    asm_var_reset(&ctx);
    virtual_label_cache_free(&ctx);
    free(ctx.stmt_size_cache);
    free(ctx.stmt_size_cached);
    {
        size_t _i;
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
    return 0;

fail:
    for (i = 0; i < ctx.sym_count; ++i) {
        free(ctx.sym_map[i].name);
    }
    free(ctx.sym_map);
    free(ctx.sym_index);
    asm_var_reset(&ctx);
    virtual_label_cache_free(&ctx);
    free(ctx.stmt_size_cache);
    free(ctx.stmt_size_cached);
    {
        size_t _i;
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
    return -1;
}
