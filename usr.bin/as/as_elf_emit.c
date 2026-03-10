#include "as_elf_emit.h"
#include "as_x86_encode.h"
#include "as_x86_avx2.h"
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
    const as_elf_cfg_t *cfg;
    const as_parse_result_t *parsed;
    elfobj_t *obj;
    elf_section_t *text_sec;
    elf_section_t *data_sec;
    emit_sym_t *sym_map;
    size_t sym_count;
    char *errbuf;
    size_t errbuf_sz;
} emit_ctx_t;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} bytebuf_t;

static const char *first_symbol_in_expr(const as_expr_t *e);
static int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out);
static as_x86_seg_t map_seg(const char *s);
static int emit_seg_override_byte(unsigned char *out, size_t out_cap, size_t *pos_io, const char *segment_reg);
static int parse_xmm_reg(const char *name, unsigned *out);
static int parse_mmx_reg(const char *name, unsigned *out);

static void set_err(emit_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
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

static char *trim_copy(const char *s) {
    const char *b;
    const char *e;
    size_t n;
    char *out;

    if (s == NULL) {
        return NULL;
    }
    b = s;
    while (*b != '\0' && isspace((unsigned char)*b)) {
        b++;
    }
    e = b + strlen(b);
    while (e > b && isspace((unsigned char)e[-1])) {
        e--;
    }
    n = (size_t)(e - b);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, b, n);
    out[n] = '\0';
    return out;
}

static int bytebuf_reserve(bytebuf_t *b, size_t extra) {
    unsigned char *next;

    if (b->len + extra <= b->cap) {
        return 0;
    }
    {
        size_t ncap = b->cap == 0 ? 256 : b->cap;
        while (ncap < b->len + extra) {
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
        default:
            return -1;
        }
    default:
        return -1;
    }
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

static int parse_x86_reg(const char *name, as_x86_reg_t *out) {
    static const struct {
        const char *name;
        as_x86_reg_t reg;
    } map[] = {
        {"al", AS_X86_REG_RAX}, {"ah", AS_X86_REG_RAX}, {"ax", AS_X86_REG_RAX}, {"eax", AS_X86_REG_RAX}, {"rax", AS_X86_REG_RAX},
        {"cl", AS_X86_REG_RCX}, {"ch", AS_X86_REG_RCX}, {"cx", AS_X86_REG_RCX}, {"ecx", AS_X86_REG_RCX}, {"rcx", AS_X86_REG_RCX},
        {"dl", AS_X86_REG_RDX}, {"dh", AS_X86_REG_RDX}, {"dx", AS_X86_REG_RDX}, {"edx", AS_X86_REG_RDX}, {"rdx", AS_X86_REG_RDX},
        {"bl", AS_X86_REG_RBX}, {"bh", AS_X86_REG_RBX}, {"bx", AS_X86_REG_RBX}, {"ebx", AS_X86_REG_RBX}, {"rbx", AS_X86_REG_RBX},
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
        *out = (unsigned)reg & 7u;
        return 0;
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
    *out = (unsigned)reg & 7u;
    return 0;
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
        streq_ci(p, "di") || streq_ci(p, "sp") || streq_ci(p, "bp") || streq_ci(p, "ip")) {
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

static int emit_i386_xmm_srcdst_rm(unsigned char opcode2, const as_operand_t *src, const as_operand_t *dst,
                                   unsigned char *out, size_t out_cap, size_t *out_len) {
    unsigned xr;
    unsigned xm;

    if (src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst->u.reg, &xr) != 0) {
        return -1;
    }
    if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
        return -1;
    }
    return emit_i386_legacy_simd_rm(0x00, opcode2, xr, src, out, out_cap, out_len);
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
    if (streq_ci(mn, "call") || streq_ci(mn, "jmp") || streq_ci(mn, "xbegin")) {
        return 1;
    }
    if ((mn[0] == 'j' || mn[0] == 'J') && mn[1] != '\0') {
        return 1;
    }
    return 0;
}

static int is_size_suffixable_base(const char *mn) {
    static const char *const names[] = {
        "add", "adc", "sbb", "sub", "and", "or", "xor", "cmp", "mov", "lea", "imul", "shl",
        "shr", "sar", "ror", "rol", "rcl", "rcr", "bt", "bts", "btr", "btc", "bsf", "bsr",
        "movsx", "movzx", "test", "push", "pushf", "popf", "xadd", "cmpxchg",
        "inc", "dec", "not", "neg", "mul", "div", "idiv", "enter", "call", "jmp", "lcall", "ljmp",
        "sldt", "str", "lldt", "ltr", "verr", "verw", "sgdt", "sidt", "lgdt", "lidt", "smsw", "lmsw",
        "pop", "ret", "leave",
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
    if (strncmp(dst, "crc32", 5) == 0 && n == 6 &&
        (dst[5] == 'b' || dst[5] == 'w' || dst[5] == 'l' || dst[5] == 'q')) {
        dst[5] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
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
    if (mem->base_reg == NULL) {
        return -1;
    }
    if (streq_ci(mem->base_reg, "rip") || streq_ci(mem->base_reg, "%rip")) {
        rip_relative = 1;
        base = AS_X86_REG_RBP; /* placeholder for r/m=101 */
    } else if (parse_x86_reg(mem->base_reg, &base) != 0) {
        return -1;
    }
    if (mem->index_reg != NULL || mem->segment_reg != NULL) {
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
    if (mem->base_reg == NULL) {
        return -1;
    }
    if (streq_ci(mem->base_reg, "rip") || streq_ci(mem->base_reg, "%rip")) {
        rip_relative = 1;
        base = AS_X86_REG_RBP; /* placeholder for r/m=101 */
    } else if (parse_x86_reg(mem->base_reg, &base) != 0) {
        return -1;
    }
    if (mem->index_reg != NULL || mem->segment_reg != NULL) {
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
    if (mem->base_reg == NULL) {
        return -1;
    }
    if (streq_ci(mem->base_reg, "rip") || streq_ci(mem->base_reg, "%rip")) {
        rip_relative = 1;
        base = AS_X86_REG_RBP;
    } else if (parse_x86_reg(mem->base_reg, &base) != 0) {
        return -1;
    }
    if (mem->index_reg != NULL || mem->segment_reg != NULL) {
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
        const as_operand_t *op_rm;
        const as_operand_t *op_vvvv;
        const as_operand_t *op_dst;
        unsigned krm;
        unsigned kvvvv;
        unsigned kdst;

        if (insn->operand_count != 3) {
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
        if (op_rm->kind != AS_OPERAND_REGISTER || op_vvvv->kind != AS_OPERAND_REGISTER || op_dst->kind != AS_OPERAND_REGISTER ||
            parse_k_reg(op_rm->u.reg, &krm) != 0 || parse_k_reg(op_vvvv->u.reg, &kvvvv) != 0 ||
            parse_k_reg(op_dst->u.reg, &kdst) != 0) {
            return -1;
        }
        return emit_i386_vex_klogic(mnbuf, krm, kvvvv, kdst, out, out_cap, out_len);
    }
    if ((strcmp(mnbuf, "push") == 0 || strcmp(mnbuf, "pop") == 0) && insn->operand_count == 1 && a != NULL &&
        a->kind == AS_OPERAND_REGISTER) {
        as_x86_seg_t seg;

        if (parse_seg_reg_text(a->u.reg, &seg) != 0) {
            return -1;
        }
        if (strcmp(mnbuf, "push") == 0) {
            switch (seg) {
            case AS_X86_SEG_ES: out[0] = 0x06; *out_len = 1; return 0;
            case AS_X86_SEG_CS: out[0] = 0x0e; *out_len = 1; return 0;
            case AS_X86_SEG_SS: out[0] = 0x16; *out_len = 1; return 0;
            case AS_X86_SEG_DS: out[0] = 0x1e; *out_len = 1; return 0;
            case AS_X86_SEG_FS: out[0] = 0x0f; out[1] = 0xa0; *out_len = 2; return 0;
            case AS_X86_SEG_GS: out[0] = 0x0f; out[1] = 0xa8; *out_len = 2; return 0;
            default: return -1;
            }
        } else {
            switch (seg) {
            case AS_X86_SEG_ES: out[0] = 0x07; *out_len = 1; return 0;
            case AS_X86_SEG_SS: out[0] = 0x17; *out_len = 1; return 0;
            case AS_X86_SEG_DS: out[0] = 0x1f; *out_len = 1; return 0;
            case AS_X86_SEG_FS: out[0] = 0x0f; out[1] = 0xa1; *out_len = 2; return 0;
            case AS_X86_SEG_GS: out[0] = 0x0f; out[1] = 0xa9; *out_len = 2; return 0;
            default: return -1;
            }
        }
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
    if (strcmp(mnbuf, "fld1") == 0) {
        if (insn->operand_count != 0) {
            return -1;
        }
        out[0] = 0xd9;
        out[1] = 0xe8;
        if (out_len != NULL) {
            *out_len = 2;
        }
        return 0;
    }
    if (strcmp(mnbuf, "fld") == 0) {
        if (insn->operand_count != 1 || a == NULL || operand_st_index(a, &stidx) != 0) {
            return -1;
        }
        out[0] = 0xd9;
        out[1] = (unsigned char)(0xc0u + (stidx & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "fxch") == 0) {
        if (insn->operand_count != 1 || a == NULL || operand_st_index(a, &stidx) != 0) {
            return -1;
        }
        out[0] = 0xd9;
        out[1] = (unsigned char)(0xc8u + (stidx & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "fnop") == 0 || strcmp(mnbuf, "fchs") == 0 || strcmp(mnbuf, "f2xm1") == 0 || strcmp(mnbuf, "fprem") == 0) {
        unsigned char op2;
        if (insn->operand_count != 0) {
            return -1;
        }
        if (strcmp(mnbuf, "fnop") == 0) op2 = 0xd0;
        else if (strcmp(mnbuf, "fchs") == 0) op2 = 0xe0;
        else if (strcmp(mnbuf, "f2xm1") == 0) op2 = 0xf0;
        else op2 = 0xf8;
        out[0] = 0xd9;
        out[1] = op2;
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "fcmovb") == 0 || strcmp(mnbuf, "fcmove") == 0 || strcmp(mnbuf, "fcmovbe") == 0 || strcmp(mnbuf, "fcmovu") == 0 ||
        strcmp(mnbuf, "fcmovnb") == 0 || strcmp(mnbuf, "fcmovne") == 0 || strcmp(mnbuf, "fcmovnbe") == 0 || strcmp(mnbuf, "fcmovnu") == 0 ||
        strcmp(mnbuf, "fucomi") == 0 || strcmp(mnbuf, "fcomi") == 0) {
        unsigned char op1;
        unsigned char base;
        if (insn->operand_count != 2 || a == NULL || b == NULL || operand_st_index(a, &stsrc) != 0 || operand_st_index(b, &stdst) != 0 ||
            stdst != 0) {
            return -1;
        }
        if (strcmp(mnbuf, "fcmovb") == 0) { op1 = 0xda; base = 0xc0; }
        else if (strcmp(mnbuf, "fcmove") == 0) { op1 = 0xda; base = 0xc8; }
        else if (strcmp(mnbuf, "fcmovbe") == 0) { op1 = 0xda; base = 0xd0; }
        else if (strcmp(mnbuf, "fcmovu") == 0) { op1 = 0xda; base = 0xd8; }
        else if (strcmp(mnbuf, "fcmovnb") == 0) { op1 = 0xdb; base = 0xc0; }
        else if (strcmp(mnbuf, "fcmovne") == 0) { op1 = 0xdb; base = 0xc8; }
        else if (strcmp(mnbuf, "fcmovnbe") == 0) { op1 = 0xdb; base = 0xd0; }
        else if (strcmp(mnbuf, "fcmovnu") == 0) { op1 = 0xdb; base = 0xd8; }
        else if (strcmp(mnbuf, "fucomi") == 0) { op1 = 0xdb; base = 0xe8; }
        else { op1 = 0xdb; base = 0xf0; }
        out[0] = op1;
        out[1] = (unsigned char)(base + (stsrc & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "fadd") == 0 || strcmp(mnbuf, "fmul") == 0 || strcmp(mnbuf, "fcom") == 0 || strcmp(mnbuf, "fcomp") == 0 ||
        strcmp(mnbuf, "fsub") == 0 || strcmp(mnbuf, "fsubr") == 0 || strcmp(mnbuf, "fdiv") == 0 || strcmp(mnbuf, "fdivr") == 0) {
        unsigned stsrc;
        unsigned stdst;
        unsigned char base;
        if (insn->operand_count == 1 && a != NULL) {
            if (operand_st_index(a, &stsrc) != 0) {
                return -1;
            }
            if (strcmp(mnbuf, "fcom") == 0) {
                out[0] = 0xd8;
                out[1] = (unsigned char)(0xd0u + (stsrc & 7u));
                if (out_len != NULL) *out_len = 2;
                return 0;
            }
            if (strcmp(mnbuf, "fcomp") == 0) {
                out[0] = 0xd8;
                out[1] = (unsigned char)(0xd8u + (stsrc & 7u));
                if (out_len != NULL) *out_len = 2;
                return 0;
            }
            return -1;
        }
        if (insn->operand_count != 2 || a == NULL || b == NULL || operand_st_index(a, &stsrc) != 0 || operand_st_index(b, &stdst) != 0 ||
            stdst != 0) {
            return -1;
        }
        if (strcmp(mnbuf, "fadd") == 0) base = 0xc0;
        else if (strcmp(mnbuf, "fmul") == 0) base = 0xc8;
        else if (strcmp(mnbuf, "fcom") == 0) base = 0xd0;
        else if (strcmp(mnbuf, "fcomp") == 0) base = 0xd8;
        else if (strcmp(mnbuf, "fsub") == 0) base = 0xe0;
        else if (strcmp(mnbuf, "fsubr") == 0) base = 0xe8;
        else if (strcmp(mnbuf, "fdiv") == 0) base = 0xf0;
        else base = 0xf8;
        out[0] = 0xd8;
        out[1] = (unsigned char)(base + (stsrc & 7u));
        if (out_len != NULL) {
            *out_len = 2;
        }
        return 0;
    }
    if (strcmp(mnbuf, "fstp") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_COPROCESSOR ||
            parse_st_index(a->u.coproc, &stidx) != 0) {
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
        if (insn->operand_count != 1 || a == NULL || operand_st_index(a, &stidx) != 0) {
            return -1;
        }
        if (strcmp(mnbuf, "ffree") == 0) base = 0xc0;
        else if (strcmp(mnbuf, "fst") == 0) base = 0xd0;
        else if (strcmp(mnbuf, "fucom") == 0) base = 0xe0;
        else base = 0xe8;
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
    if (strcmp(mnbuf, "fldl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdd, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "flds") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd9, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fldt") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdb, 5u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fstpl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdd, 3u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fstps") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd9, 3u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fstpt") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdb, 7u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "faddl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdc, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fadds") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd8, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fsubl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdc, 4u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fsubs") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd8, 4u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fsubrs") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd8, 5u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fsubrl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdc, 5u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fmull") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdc, 1u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fmuls") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd8, 1u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fdivl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdc, 6u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fdivs") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd8, 6u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fdivrs") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd8, 7u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fdivrl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdc, 7u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fcoms") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd8, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fcomps") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd8, 3u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fcoml") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdc, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fcompl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdc, 3u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fildl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdb, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "filds") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdf, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fildll") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdf, 5u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fistl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdb, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fistpl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdb, 3u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fists") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdf, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fistps") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdf, 3u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fistpll") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdf, 7u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fisttps") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdf, 1u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fisttpl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdb, 1u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fisttpll") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdd, 1u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fstl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdd, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fbld") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdf, 4u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fbstp") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xdf, 6u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fldcw") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd9, 5u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fnstcw") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd9, 7u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fsts") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd9, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fldenv") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd9, 4u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fnstenv") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xd9, 6u, a, out, out_cap, out_len);
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
        if (insn->operand_count != 2 || a == NULL || b == NULL || operand_st_index(a, &stsrc) != 0 || operand_st_index(b, &stdst) != 0 ||
            stdst != 0) {
            return -1;
        }
        if (strcmp(mnbuf, "faddp") == 0) base = 0xc0;
        else if (strcmp(mnbuf, "fmulp") == 0) base = 0xc8;
        else if (strcmp(mnbuf, "fsubp") == 0) base = 0xe0;
        else if (strcmp(mnbuf, "fsubrp") == 0) base = 0xe8;
        else if (strcmp(mnbuf, "fdivp") == 0) base = 0xf0;
        else base = 0xf8;
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
        if (insn->operand_count != 2 || a == NULL || b == NULL || operand_st_index(a, &stsrc) != 0 || operand_st_index(b, &stdst) != 0 ||
            stdst != 0) {
            return -1;
        }
        base = strcmp(mnbuf, "fucomip") == 0 ? 0xe8u : 0xf0u;
        out[0] = 0xdf;
        out[1] = (unsigned char)(base + (stsrc & 7u));
        if (out_len != NULL) *out_len = 2;
        return 0;
    }
    if (strcmp(mnbuf, "ficoml") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xda, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ficompl") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xda, 3u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ficoms") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xde, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ficomps") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_1byte_x87_mem(insn, 0xde, 3u, a, out, out_cap, out_len);
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
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(0x66, 0x6f, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x7f, xr, dst, out, out_cap, out_len);
        }
        return -1;
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
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(0xf3, 0x10, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0xf3, 0x11, xr, dst, out, out_cap, out_len);
        }
        return -1;
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
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_x86_reg(dst->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0xf2, strcmp(mnbuf, "cvttsd2si") == 0 ? 0x2c : 0x2d,
                                        (unsigned)gr & 7u, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttss2si") == 0 || strcmp(mnbuf, "cvtss2si") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_x86_reg(dst->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0xf3, strcmp(mnbuf, "cvttss2si") == 0 ? 0x2c : 0x2d,
                                        (unsigned)gr & 7u, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "sqrtsd") == 0 || strcmp(mnbuf, "addsd") == 0 || strcmp(mnbuf, "mulsd") == 0 ||
        strcmp(mnbuf, "cvtsd2ss") == 0 || strcmp(mnbuf, "subsd") == 0 || strcmp(mnbuf, "minsd") == 0 ||
        strcmp(mnbuf, "divsd") == 0 || strcmp(mnbuf, "maxsd") == 0) {
        unsigned char opcode2;

        if (strcmp(mnbuf, "sqrtsd") == 0) opcode2 = 0x51;
        else if (strcmp(mnbuf, "addsd") == 0) opcode2 = 0x58;
        else if (strcmp(mnbuf, "mulsd") == 0) opcode2 = 0x59;
        else if (strcmp(mnbuf, "cvtsd2ss") == 0) opcode2 = 0x5a;
        else if (strcmp(mnbuf, "subsd") == 0) opcode2 = 0x5c;
        else if (strcmp(mnbuf, "minsd") == 0) opcode2 = 0x5d;
        else if (strcmp(mnbuf, "divsd") == 0) opcode2 = 0x5e;
        else opcode2 = 0x5f;
        return emit_i386_prefixed_xmm_srcdst_rm(0xf2, opcode2, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "sqrtss") == 0 || strcmp(mnbuf, "rsqrtss") == 0 || strcmp(mnbuf, "rcpss") == 0 ||
        strcmp(mnbuf, "addss") == 0 || strcmp(mnbuf, "mulss") == 0 || strcmp(mnbuf, "cvtss2sd") == 0 ||
        strcmp(mnbuf, "cvttps2dq") == 0 || strcmp(mnbuf, "subss") == 0 || strcmp(mnbuf, "minss") == 0 ||
        strcmp(mnbuf, "divss") == 0 || strcmp(mnbuf, "maxss") == 0) {
        unsigned char opcode2;

        if (strcmp(mnbuf, "sqrtss") == 0) opcode2 = 0x51;
        else if (strcmp(mnbuf, "rsqrtss") == 0) opcode2 = 0x52;
        else if (strcmp(mnbuf, "rcpss") == 0) opcode2 = 0x53;
        else if (strcmp(mnbuf, "addss") == 0) opcode2 = 0x58;
        else if (strcmp(mnbuf, "mulss") == 0) opcode2 = 0x59;
        else if (strcmp(mnbuf, "cvtss2sd") == 0) opcode2 = 0x5a;
        else if (strcmp(mnbuf, "cvttps2dq") == 0) opcode2 = 0x5b;
        else if (strcmp(mnbuf, "subss") == 0) opcode2 = 0x5c;
        else if (strcmp(mnbuf, "minss") == 0) opcode2 = 0x5d;
        else if (strcmp(mnbuf, "divss") == 0) opcode2 = 0x5e;
        else opcode2 = 0x5f;
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

        if (insn->operand_count != 2) {
            return -1;
        }
        if (strcmp(mnbuf, "vmread") == 0) {
            if (intel_syntax) {
                rm_op = &insn->operands[0];
                reg_op = &insn->operands[1];
            } else {
                reg_op = &insn->operands[0];
                rm_op = &insn->operands[1];
            }
        } else {
            if (intel_syntax) {
                reg_op = &insn->operands[0];
                rm_op = &insn->operands[1];
            } else {
                rm_op = &insn->operands[0];
                reg_op = &insn->operands[1];
            }
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
        if (strcmp(mnbuf, "cpuid") == 0) opcode2 = 0xa2;
        else if (strcmp(mnbuf, "montmul") == 0) opcode2 = 0xa6;
        else if (strcmp(mnbuf, "xstore-rng") == 0) opcode2 = 0xa7;
        else opcode2 = 0xaa;
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

        if (insn->operand_count != 3) {
            return -1;
        }
        if (intel_syntax) {
            dst_op = &insn->operands[0];
            src_op = &insn->operands[1];
            count_op = &insn->operands[2];
        } else {
            count_op = &insn->operands[0];
            src_op = &insn->operands[1];
            dst_op = &insn->operands[2];
        }
        if (src_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(src_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        if (count_op->kind == AS_OPERAND_REGISTER) {
            as_x86_reg_t cr;

            if (parse_x86_reg(count_op->u.reg, &cr) != 0 || cr != AS_X86_REG_ECX) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(0x00, strcmp(mnbuf, "shld") == 0 ? 0xa5 : 0xad,
                                            (unsigned)gr & 7u, dst_op, out, out_cap, out_len);
        }
        if ((count_op->kind != AS_OPERAND_IMMEDIATE && count_op->kind != AS_OPERAND_LABEL_REF) ||
            eval_expr_const(count_op->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        return emit_i386_legacy_simd_rm_imm8(0x00, strcmp(mnbuf, "shld") == 0 ? 0xa4 : 0xac,
                                             (unsigned)gr & 7u, dst_op, (unsigned char)immv, out, out_cap, out_len);
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
        prefix = (insn->prefixes & AS_PREFIX_DATA16) != 0 ? 0x66 : 0x00;
        if (strcmp(mnbuf, "fxsave") == 0) reg_field = 0;
        else if (strcmp(mnbuf, "fxrstor") == 0) reg_field = 1;
        else if (strcmp(mnbuf, "ldmxcsr") == 0) reg_field = 2;
        else if (strcmp(mnbuf, "stmxcsr") == 0) reg_field = 3;
        else if (strcmp(mnbuf, "xsave") == 0) reg_field = 4;
        else if (strcmp(mnbuf, "xrstor") == 0) reg_field = 5;
        else if (strcmp(mnbuf, "xsaveopt") == 0) reg_field = 6;
        else if (strcmp(mnbuf, "clwb") == 0) {
            reg_field = 6;
            prefix = 0x66;
        } else if (strcmp(mnbuf, "clflushopt") == 0) {
            reg_field = 7;
            prefix = 0x66;
        }
        else reg_field = 7;
        return emit_i386_prefixed_0f_rm(prefix, 0xae, reg_field, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "lfence") == 0 || strcmp(mnbuf, "mfence") == 0 || strcmp(mnbuf, "sfence") == 0) {
        size_t pos = 0;

        if (insn->operand_count != 0) {
            return -1;
        }
        if ((insn->prefixes & AS_PREFIX_DATA16) != 0) {
            out[pos++] = 0x66;
        }
        out[pos++] = 0x0f;
        out[pos++] = 0xae;
        out[pos++] = strcmp(mnbuf, "lfence") == 0 ? 0xe8 : (strcmp(mnbuf, "mfence") == 0 ? 0xf0 : 0xf8);
        if (out_len != NULL) *out_len = pos;
        return 0;
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
        if (strcmp(mnbuf, "sldt") == 0) reg_field = 0u;
        else if (strcmp(mnbuf, "str") == 0) reg_field = 1u;
        else if (strcmp(mnbuf, "lldt") == 0) reg_field = 2u;
        else if (strcmp(mnbuf, "ltr") == 0) reg_field = 3u;
        else if (strcmp(mnbuf, "verr") == 0) reg_field = 4u;
        else reg_field = 5u;
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
        if (strcmp(mnbuf, "bt") == 0) {
            reg_field = 4;
            opcode2 = 0xa3;
        } else if (strcmp(mnbuf, "bts") == 0) {
            reg_field = 5;
            opcode2 = 0xab;
        } else if (strcmp(mnbuf, "btr") == 0) {
            reg_field = 6;
            opcode2 = 0xb3;
        } else {
            reg_field = 7;
            opcode2 = 0xbb;
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
        rm_op = &insn->operands[0];
        reg_op = &insn->operands[1];
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0xb9, (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "bsf") == 0 || strcmp(mnbuf, "bsr") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;

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
        return emit_i386_prefixed_0f_rm(0x00, strcmp(mnbuf, "bsf") == 0 ? 0xbc : 0xbd,
                                        (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cmpps") == 0 || strcmp(mnbuf, "cmppd") == 0 ||
        strcmp(mnbuf, "pinsrw") == 0 || strcmp(mnbuf, "pextrw") == 0 ||
        strcmp(mnbuf, "shufps") == 0 || strcmp(mnbuf, "shufpd") == 0 ||
        strcmp(mnbuf, "pshufd") == 0) {
        const as_operand_t *imm_op;
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        long long immv;
        as_x86_reg_t gr;

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
            eval_expr_const(imm_op->u.expr, &immv) != 0 || immv < 0 || immv > 255) {
            return -1;
        }
        if (strcmp(mnbuf, "cmpps") == 0 || strcmp(mnbuf, "cmppd") == 0 ||
            strcmp(mnbuf, "shufps") == 0 || strcmp(mnbuf, "shufpd") == 0 ||
            strcmp(mnbuf, "pshufd") == 0) {
            if (dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0) {
                return -1;
            }
            if (src_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src_op->u.reg, &xm) != 0) {
                return -1;
            }
            if (strcmp(mnbuf, "cmpps") == 0) {
                return emit_i386_legacy_simd_rm_imm8(0x00, 0xc2, xr, src_op, (unsigned char)immv, out, out_cap, out_len);
            }
            if (strcmp(mnbuf, "cmppd") == 0) {
                return emit_i386_legacy_simd_rm_imm8(0x66, 0xc2, xr, src_op, (unsigned char)immv, out, out_cap, out_len);
            }
            if (strcmp(mnbuf, "shufps") == 0) {
                return emit_i386_legacy_simd_rm_imm8(0x00, 0xc6, xr, src_op, (unsigned char)immv, out, out_cap, out_len);
            }
            if (strcmp(mnbuf, "shufpd") == 0) {
                return emit_i386_legacy_simd_rm_imm8(0x66, 0xc6, xr, src_op, (unsigned char)immv, out, out_cap, out_len);
            }
            return emit_i386_legacy_simd_rm_imm8(0x66, 0x70, xr, src_op, (unsigned char)immv, out, out_cap, out_len);
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
        const as_operand_t *src_op;
        const as_operand_t *dst_op;
        unsigned char prefix = 0x00;
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
        if (src_op->kind != AS_OPERAND_REGISTER) {
            return -1;
        }
        if (parse_mmx_reg(src_op->u.reg, &xm) == 0) {
            prefix = 0x00;
        } else if (parse_xmm_reg(src_op->u.reg, &xm) == 0) {
            prefix = 0x66;
        } else {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, 0xd7, (unsigned)gr & 7u, src_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movntq") == 0) {
        const as_operand_t *src_op;
        const as_operand_t *dst_op;

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
        if (src_op->kind != AS_OPERAND_REGISTER || parse_mmx_reg(src_op->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0xe7, xr, dst_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movdq2q") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_mmx_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0xf2, 0xd6, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttpd2dq") == 0) {
        return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0xe6, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtpd2dq") == 0) {
        return emit_i386_prefixed_xmm_srcdst_rm(0xf2, 0xe6, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movntdq") == 0) {
        const as_operand_t *src_op;
        const as_operand_t *dst_op;

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
        if (dst_op->kind == AS_OPERAND_REGISTER || src_op->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(src_op->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x66, 0xe7, xr, dst_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "maskmovq") == 0) {
        const as_operand_t *src_op;
        const as_operand_t *mask_op;

        if (insn->operand_count != 2) {
            return -1;
        }
        src_op = &insn->operands[0];
        mask_op = &insn->operands[1];
        if (src_op->kind != AS_OPERAND_REGISTER || mask_op->kind != AS_OPERAND_REGISTER ||
            parse_mmx_reg(src_op->u.reg, &xr) != 0 || parse_mmx_reg(mask_op->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0xf7, xr, mask_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "maskmovdqu") == 0) {
        const as_operand_t *src_op;
        const as_operand_t *mask_op;

        if (insn->operand_count != 2) {
            return -1;
        }
        src_op = &insn->operands[0];
        mask_op = &insn->operands[1];
        if (src_op->kind != AS_OPERAND_REGISTER || mask_op->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(src_op->u.reg, &xr) != 0 || parse_xmm_reg(mask_op->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x66, 0xf7, xr, mask_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ud0") == 0) {
        const as_operand_t *rm_op;
        const as_operand_t *reg_op;
        as_x86_reg_t gr;

        if (insn->operand_count != 2) {
            return -1;
        }
        rm_op = &insn->operands[0];
        reg_op = &insn->operands[1];
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0xff, (unsigned)gr & 7u, rm_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cmpxchg8b") == 0 || strcmp(mnbuf, "rdrand") == 0 || strcmp(mnbuf, "rdseed") == 0 ||
        strcmp(mnbuf, "xrstors") == 0 || strcmp(mnbuf, "xsavec") == 0 || strcmp(mnbuf, "xsaves") == 0 ||
        strcmp(mnbuf, "vmclear") == 0 || strcmp(mnbuf, "vmptrld") == 0 || strcmp(mnbuf, "vmptrst") == 0) {
        unsigned reg_field;
        unsigned char prefix;
        const as_operand_t *op;

        if (insn->operand_count != 1) {
            return -1;
        }
        op = &insn->operands[0];
        prefix = (insn->prefixes & AS_PREFIX_DATA16) != 0 ? 0x66 : 0x00;
        if (strcmp(mnbuf, "cmpxchg8b") == 0) reg_field = 1;
        else if (strcmp(mnbuf, "xrstors") == 0) reg_field = 3;
        else if (strcmp(mnbuf, "xsavec") == 0) reg_field = 4;
        else if (strcmp(mnbuf, "xsaves") == 0) reg_field = 5;
        else if (strcmp(mnbuf, "rdrand") == 0 || strcmp(mnbuf, "vmclear") == 0 || strcmp(mnbuf, "vmptrld") == 0) {
            reg_field = 6;
            if (strcmp(mnbuf, "vmclear") == 0) {
                prefix = 0x66;
            }
        }
        else reg_field = 7;
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
        if (strcmp(mnbuf, "sha1nexte") == 0) opcode3 = 0xc8;
        else if (strcmp(mnbuf, "sha1msg1") == 0) opcode3 = 0xc9;
        else if (strcmp(mnbuf, "sha1msg2") == 0) opcode3 = 0xca;
        else if (strcmp(mnbuf, "sha256msg1") == 0) opcode3 = 0xcc;
        else opcode3 = 0xcd;
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
        if (strcmp(mnbuf, "pclmulqdq") == 0) opcode3 = 0x44;
        else if (strcmp(mnbuf, "gf2p8affineqb") == 0) opcode3 = 0xce;
        else if (strcmp(mnbuf, "gf2p8affineinvqb") == 0) opcode3 = 0xcf;
        else opcode3 = 0xdf;
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

        if (insn->operand_count != 2) {
            return -1;
        }
        if (intel_syntax) {
            if (insn->operands[0].kind == AS_OPERAND_REGISTER) {
                reg_op = &insn->operands[0];
                rm_op = &insn->operands[1];
                opcode3 = 0xf0;
            } else {
                rm_op = &insn->operands[0];
                reg_op = &insn->operands[1];
                opcode3 = 0xf1;
            }
        } else {
            if (insn->operands[0].kind == AS_OPERAND_REGISTER) {
                reg_op = &insn->operands[0];
                rm_op = &insn->operands[1];
                opcode3 = 0xf1;
            } else {
                rm_op = &insn->operands[0];
                reg_op = &insn->operands[1];
                opcode3 = 0xf0;
            }
        }
        if (reg_op->kind != AS_OPERAND_REGISTER || parse_x86_reg(reg_op->u.reg, &gr) != 0 || (gr & 8u) != 0u) {
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
        if (strcmp(mnbuf, "invept") == 0) opcode3 = 0x80;
        else if (strcmp(mnbuf, "invvpid") == 0) opcode3 = 0x81;
        else opcode3 = 0x82;
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
        if (strcmp(mnbuf, "gf2p8mulb") == 0) opcode3 = 0xcf;
        else if (strcmp(mnbuf, "aesimc") == 0) opcode3 = 0xdb;
        else if (strcmp(mnbuf, "aesenc") == 0) opcode3 = 0xdc;
        else if (strcmp(mnbuf, "aesenclast") == 0) opcode3 = 0xdd;
        else if (strcmp(mnbuf, "aesdec") == 0) opcode3 = 0xde;
        else opcode3 = 0xdf;
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
        if (strcmp(mnbuf, "loadiwkey") == 0 || strcmp(mnbuf, "aesenc128kl") == 0) opcode3 = 0xdc;
        else if (strcmp(mnbuf, "aesdec128kl") == 0) opcode3 = 0xdd;
        else if (strcmp(mnbuf, "aesenc256kl") == 0) opcode3 = 0xde;
        else opcode3 = 0xdf;
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

        if (insn->operand_count != 2) {
            return -1;
        }
        if (strcmp(mnbuf, "movdir64b") == 0 || strcmp(mnbuf, "enqcmd") == 0 || strcmp(mnbuf, "enqcmds") == 0) {
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
        prefix = 0x00;
        if (strcmp(mnbuf, "wrssd") == 0) opcode3 = 0xf6;
        else if (strcmp(mnbuf, "wrussd") == 0) {
            opcode3 = 0xf5;
            prefix = 0x66;
        }
        else if (strcmp(mnbuf, "movdir64b") == 0) {
            opcode3 = 0xf8;
            prefix = 0x66;
        } else if (strcmp(mnbuf, "enqcmd") == 0) {
            opcode3 = 0xf8;
            prefix = 0xf2;
        } else if (strcmp(mnbuf, "enqcmds") == 0) {
            opcode3 = 0xf8;
            prefix = 0xf3;
        } else if (strcmp(mnbuf, "movdiri") == 0) opcode3 = 0xf9;
        else {
            opcode3 = 0xfc;
            if (strcmp(mnbuf, "aand") == 0) {
                prefix = 0x66;
            } else if (strcmp(mnbuf, "aor") == 0) {
                prefix = 0xf2;
            } else if (strcmp(mnbuf, "axor") == 0) {
                prefix = 0xf3;
            }
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
        prefix = strcmp(mnbuf, "adcx") == 0 ? 0x66 : 0xf3;
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
        opcode3 = strcmp(mnbuf, "encodekey128") == 0 ? 0xfau : 0xfbu;
        return emit_i386_prefixed_0f_map_rm(0xf3, 0x38, opcode3, (unsigned)gr & 7u, src_op, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movups") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_legacy_simd_rm(0x00, 0x10, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x00, 0x11, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "movupd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(0x66, 0x10, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x11, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "movlpd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && src->kind != AS_OPERAND_REGISTER &&
            parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x12, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind != AS_OPERAND_REGISTER &&
            parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x13, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "nopw") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x66, 0x1d, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "unpcklpd") == 0) {
        return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x14, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "unpckhpd") == 0) {
        return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x15, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movhpd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && src->kind != AS_OPERAND_REGISTER &&
            parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x16, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind != AS_OPERAND_REGISTER &&
            parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x17, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "cvtpi2pd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x66, 0x2a, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttpd2pi") == 0 || strcmp(mnbuf, "cvtpd2pi") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_mmx_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x66, strcmp(mnbuf, "cvttpd2pi") == 0 ? 0x2c : 0x2d,
                                        xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ucomisd") == 0 || strcmp(mnbuf, "comisd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x66, strcmp(mnbuf, "ucomisd") == 0 ? 0x2e : 0x2f,
                                        xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "extrq") == 0 || strcmp(mnbuf, "insertq") == 0) {
        unsigned char prefix = strcmp(mnbuf, "extrq") == 0 ? 0x66 : 0xf2;

        if (insn->operand_count == 2) {
            const as_operand_t *src_op;
            const as_operand_t *dst_op;

            if (intel_syntax) {
                dst_op = &insn->operands[0];
                src_op = &insn->operands[1];
            } else {
                src_op = &insn->operands[0];
                dst_op = &insn->operands[1];
            }
            if (dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0) {
                return -1;
            }
            if (src_op->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src_op->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(prefix, 0x79, xr, src_op, out, out_cap, out_len);
        }
        if (insn->operand_count == 3 && strcmp(mnbuf, "extrq") == 0) {
            const as_operand_t *len_op;
            const as_operand_t *off_op;
            const as_operand_t *dst_op;
            long long lenv;
            long long offv;

            if (intel_syntax) {
                dst_op = &insn->operands[0];
                len_op = &insn->operands[1];
                off_op = &insn->operands[2];
            } else {
                len_op = &insn->operands[0];
                off_op = &insn->operands[1];
                dst_op = &insn->operands[2];
            }
            if (dst_op->kind != AS_OPERAND_REGISTER || parse_xmm_reg(dst_op->u.reg, &xr) != 0 ||
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
            if (out_len != NULL) *out_len = 6;
            return 0;
        }
        if (insn->operand_count == 4 && strcmp(mnbuf, "insertq") == 0) {
            const as_operand_t *len_op;
            const as_operand_t *off_op;
            const as_operand_t *src_op;
            const as_operand_t *dst_op;
            long long lenv;
            long long offv;

            if (intel_syntax) {
                dst_op = &insn->operands[0];
                src_op = &insn->operands[1];
                len_op = &insn->operands[2];
                off_op = &insn->operands[3];
            } else {
                len_op = &insn->operands[0];
                off_op = &insn->operands[1];
                src_op = &insn->operands[2];
                dst_op = &insn->operands[3];
            }
            if (dst_op->kind != AS_OPERAND_REGISTER || src_op->kind != AS_OPERAND_REGISTER ||
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
            if (out_len != NULL) *out_len = 6;
            return 0;
        }
        return -1;
    }
    if (strcmp(mnbuf, "psrldq") == 0 || strcmp(mnbuf, "pslldq") == 0) {
        const as_operand_t *imm_op;
        const as_operand_t *dst_op;
        long long immv;

        if (insn->operand_count != 2) {
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
        return emit_i386_legacy_simd_rm_imm8(0x66, 0x73, strcmp(mnbuf, "psrldq") == 0 ? 3u : 7u,
                                             dst_op, (unsigned char)immv, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "pshuflw") == 0 || strcmp(mnbuf, "pshufhw") == 0) {
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
        return emit_i386_legacy_simd_rm_imm8(strcmp(mnbuf, "pshuflw") == 0 ? 0xf2 : 0xf3, 0x70, xr,
                                             src_op, (unsigned char)immv, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movhlps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_legacy_simd_rm(0x00, 0x12, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movlps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_i386_legacy_simd_rm(0x00, 0x12, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x00, 0x13, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "unpcklps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_legacy_simd_rm(0x00, 0x14, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "unpckhps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_legacy_simd_rm(0x00, 0x15, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movlhps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_legacy_simd_rm(0x00, 0x16, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movhps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_i386_legacy_simd_rm(0x00, 0x16, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x00, 0x17, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "cldemote") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind == AS_OPERAND_REGISTER || a->kind == AS_OPERAND_COPROCESSOR) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x1c, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movaps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_legacy_simd_rm(0x00, 0x28, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x00, 0x29, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "movapd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (dst->kind == AS_OPERAND_REGISTER && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
                return -1;
            }
            return emit_i386_prefixed_0f_rm(0x66, 0x28, xr, src, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_i386_prefixed_0f_rm(0x66, 0x29, xr, dst, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "cvtpi2ps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_legacy_simd_rm(0x00, 0x2a, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movntps") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || src->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(src->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x2b, xr, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movntpd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || src->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(src->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x66, 0x2b, xr, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvttps2pi") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_mmx_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_legacy_simd_rm(0x00, 0x2c, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtps2pi") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_mmx_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_legacy_simd_rm(0x00, 0x2d, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ucomiss") == 0 || strcmp(mnbuf, "comiss") == 0) {
        unsigned char opcode2 = strcmp(mnbuf, "ucomiss") == 0 ? 0x2e : 0x2f;
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_legacy_simd_rm(0x00, opcode2, xr, src, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movmskps") == 0) {
        as_x86_reg_t gr;
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            src->kind != AS_OPERAND_REGISTER || parse_x86_reg(dst->u.reg, &gr) != 0 || parse_xmm_reg(src->u.reg, &xr) != 0) {
            return -1;
        }
        out[0] = 0x0f;
        out[1] = 0x50;
        out[2] = (unsigned char)(0xc0u | (((unsigned)gr & 7u) << 3) | (xr & 7u));
        if (out_len != NULL) *out_len = 3;
        return 0;
    }
    if (strcmp(mnbuf, "movmskpd") == 0) {
        as_x86_reg_t gr;
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            src->kind != AS_OPERAND_REGISTER || parse_x86_reg(dst->u.reg, &gr) != 0 || parse_xmm_reg(src->u.reg, &xr) != 0) {
            return -1;
        }
        out[0] = 0x66;
        out[1] = 0x0f;
        out[2] = 0x50;
        out[3] = (unsigned char)(0xc0u | (((unsigned)gr & 7u) << 3) | (xr & 7u));
        if (out_len != NULL) *out_len = 4;
        return 0;
    }
    if (strcmp(mnbuf, "sqrtps") == 0) return emit_i386_xmm_srcdst_rm(0x51, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "rsqrtps") == 0) return emit_i386_xmm_srcdst_rm(0x52, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "rcpps") == 0) return emit_i386_xmm_srcdst_rm(0x53, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "andps") == 0) return emit_i386_xmm_srcdst_rm(0x54, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "andnps") == 0) return emit_i386_xmm_srcdst_rm(0x55, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "orps") == 0) return emit_i386_xmm_srcdst_rm(0x56, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "xorps") == 0) return emit_i386_xmm_srcdst_rm(0x57, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "addps") == 0) return emit_i386_xmm_srcdst_rm(0x58, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "mulps") == 0) return emit_i386_xmm_srcdst_rm(0x59, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "cvtps2pd") == 0) return emit_i386_xmm_srcdst_rm(0x5a, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "cvtdq2ps") == 0) return emit_i386_xmm_srcdst_rm(0x5b, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "subps") == 0) return emit_i386_xmm_srcdst_rm(0x5c, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "minps") == 0) return emit_i386_xmm_srcdst_rm(0x5d, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "divps") == 0) return emit_i386_xmm_srcdst_rm(0x5e, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "maxps") == 0) return emit_i386_xmm_srcdst_rm(0x5f, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "sqrtpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x51, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "andpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x54, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "andnpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x55, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "orpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x56, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "xorpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x57, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "addpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x58, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "mulpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x59, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "cvtpd2ps") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x5a, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "cvtps2dq") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x5b, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "subpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x5c, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "minpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x5d, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "divpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x5e, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "maxpd") == 0) return emit_i386_prefixed_xmm_srcdst_rm(0x66, 0x5f, src, dst, out, out_cap, out_len);
    if (strcmp(mnbuf, "prefetch") == 0) {
        if (insn->operand_count != 1 || a == NULL) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x0d, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "prefetchnta") == 0 || strcmp(mnbuf, "prefetcht0") == 0 ||
        strcmp(mnbuf, "prefetcht1") == 0 || strcmp(mnbuf, "prefetcht2") == 0) {
        unsigned char regf;
        if (insn->operand_count != 1 || a == NULL || a->kind == AS_OPERAND_REGISTER || a->kind == AS_OPERAND_COPROCESSOR) {
            return -1;
        }
        if (strcmp(mnbuf, "prefetchnta") == 0) regf = 0u;
        else if (strcmp(mnbuf, "prefetcht0") == 0) regf = 1u;
        else if (strcmp(mnbuf, "prefetcht1") == 0) regf = 2u;
        else regf = 3u;
        return emit_i386_prefixed_0f_rm(0x00, 0x18, regf, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "pfcmpge") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_mmx_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_mmx_reg(src->u.reg, &xm) != 0) {
            return -1;
        }
        return emit_i386_3dnow_rm(xr, src, 0x90, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "prefetchwt1") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_MEMORY) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x0d, 2u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "nopl") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind == AS_OPERAND_REGISTER || a->kind == AS_OPERAND_COPROCESSOR) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x1d, 0u, a, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "bndldx") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_bnd_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(0x00, 0x1a, xr, src, out, out_cap, out_len);
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
        unsigned char prefix = (strcmp(mnbuf, "bndcl") == 0 || strcmp(mnbuf, "bndmk") == 0) ? 0xf3 : 0xf2;
        unsigned char opcode2 = (strcmp(mnbuf, "bndcn") == 0 || strcmp(mnbuf, "bndmk") == 0) ? 0x1b : 0x1a;
        if (insn->operand_count != 2 || src == NULL || dst == NULL || dst->kind != AS_OPERAND_REGISTER ||
            parse_bnd_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_i386_prefixed_0f_rm(prefix, opcode2, xr, src, out, out_cap, out_len);
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
    unsigned xr;
    unsigned xm;
    as_x86_reg_t gr;
    long long imm64;
    unsigned char rex;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (insn == NULL || out == NULL || out_cap < 12) {
        return -1;
    }
    if (normalize_x86_mnemonic(insn->mnemonic, mnbuf, sizeof(mnbuf), NULL) != 0) {
        return -1;
    }
    if (insn->prefixes != 0 || insn->segment_override != NULL) {
        return -1;
    }

    a = insn->operand_count > 0 ? &insn->operands[0] : NULL;
    b = insn->operand_count > 1 ? &insn->operands[1] : NULL;
    src = intel_syntax ? b : a;
    dst = intel_syntax ? a : b;

    if (strcmp(mnbuf, "movabs") == 0) {
        size_t i;
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            (src->kind != AS_OPERAND_IMMEDIATE && src->kind != AS_OPERAND_LABEL_REF) ||
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
    if (strcmp(mnbuf, "fldl") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_MEMORY) {
            return -1;
        }
        return emit_x86_64_1byte_regfield_memop(0xdd, 0u, &a->u.mem, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fldt") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_MEMORY) {
            return -1;
        }
        return emit_x86_64_1byte_regfield_memop(0xdb, 5u, &a->u.mem, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fstpl") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_MEMORY) {
            return -1;
        }
        return emit_x86_64_1byte_regfield_memop(0xdd, 3u, &a->u.mem, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "fstpt") == 0) {
        if (insn->operand_count != 1 || a == NULL || a->kind != AS_OPERAND_MEMORY) {
            return -1;
        }
        return emit_x86_64_1byte_regfield_memop(0xdb, 7u, &a->u.mem, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movdqa") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            src->kind != AS_OPERAND_REGISTER || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(src->u.reg, &xm) != 0 || parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        return emit_x86_64_xmm_regop(0x66, 0x6f, xr, xm, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "movdqu") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER &&
            parse_xmm_reg(src->u.reg, &xm) == 0 && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            rex = (unsigned char)(0x40u | ((xr & 8u) ? 0x04u : 0u) | ((xm & 8u) ? 0x01u : 0u));
            out[0] = 0xf3;
            if (rex != 0x40u) {
                out[1] = rex;
                out[2] = 0x0f;
                out[3] = 0x6f;
                out[4] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (xm & 7u));
                if (out_len != NULL) {
                    *out_len = 5;
                }
            } else {
                out[1] = 0x0f;
                out[2] = 0x6f;
                out[3] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (xm & 7u));
                if (out_len != NULL) {
                    *out_len = 4;
                }
            }
            return 0;
        }
        if (src->kind == AS_OPERAND_MEMORY && dst->kind == AS_OPERAND_REGISTER &&
            parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_x86_64_xmm_memop(0xf3, 0x6f, xr, &src->u.mem, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_MEMORY &&
            parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_x86_64_xmm_memop(0xf3, 0x7f, xr, &dst->u.mem, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "movsd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER &&
            parse_xmm_reg(src->u.reg, &xm) == 0 && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_x86_64_xmm_regop(0xf2, 0x10, xr, xm, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_MEMORY && dst->kind == AS_OPERAND_REGISTER &&
            parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_x86_64_xmm_memop(0xf2, 0x10, xr, &src->u.mem, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_MEMORY &&
            parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_x86_64_xmm_memop(0xf2, 0x11, xr, &dst->u.mem, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "movss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_REGISTER &&
            parse_xmm_reg(src->u.reg, &xm) == 0 && parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_x86_64_xmm_regop(0xf3, 0x10, xr, xm, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_MEMORY && dst->kind == AS_OPERAND_REGISTER &&
            parse_xmm_reg(dst->u.reg, &xr) == 0) {
            return emit_x86_64_xmm_memop(0xf3, 0x10, xr, &src->u.mem, out, out_cap, out_len);
        }
        if (src->kind == AS_OPERAND_REGISTER && dst->kind == AS_OPERAND_MEMORY &&
            parse_xmm_reg(src->u.reg, &xr) == 0) {
            return emit_x86_64_xmm_memop(0xf3, 0x11, xr, &dst->u.mem, out, out_cap, out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "cvtsd2ss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf2, 0x5a, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtss2sd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf3, 0x5a, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "addsd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf2, 0x58, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "addss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf3, 0x58, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "subsd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf2, 0x5c, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "subss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf3, 0x5c, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "mulsd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf2, 0x59, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "mulss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf3, 0x59, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "divsd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf2, 0x5e, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "divss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0xf3, 0x5e, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ucomisd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0x66, 0x2e, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "ucomiss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0x00, 0x2e, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "comisd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0x66, 0x2f, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "comiss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL) {
            return -1;
        }
        return emit_x86_64_xmm_srcdst(0x00, 0x2f, src, dst, out, out_cap, out_len);
    }
    if (strcmp(mnbuf, "cvtsi2sd") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            src->kind != AS_OPERAND_REGISTER || dst->kind != AS_OPERAND_REGISTER ||
            parse_x86_reg(src->u.reg, &gr) != 0 || parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        rex = (unsigned char)(0x48u | ((xr & 8u) ? 0x04u : 0u) | ((((unsigned)gr) & 8u) ? 0x01u : 0u));
        out[0] = 0xf2;
        out[1] = rex;
        out[2] = 0x0f;
        out[3] = 0x2a;
        out[4] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (((unsigned)gr) & 7u));
        if (out_len != NULL) {
            *out_len = 5;
        }
        return 0;
    }
    if (strcmp(mnbuf, "cvtsi2ss") == 0) {
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            src->kind != AS_OPERAND_REGISTER || dst->kind != AS_OPERAND_REGISTER ||
            parse_x86_reg(src->u.reg, &gr) != 0 || parse_xmm_reg(dst->u.reg, &xr) != 0) {
            return -1;
        }
        rex = (unsigned char)(0x48u | ((xr & 8u) ? 0x04u : 0u) | ((((unsigned)gr) & 8u) ? 0x01u : 0u));
        out[0] = 0xf3;
        out[1] = rex;
        out[2] = 0x0f;
        out[3] = 0x2a;
        out[4] = (unsigned char)(0xc0u | ((xr & 7u) << 3) | (((unsigned)gr) & 7u));
        if (out_len != NULL) {
            *out_len = 5;
        }
        return 0;
    }
    if (strcmp(mnbuf, "cvttsd2si") == 0) {
        int dst_bits;
        unsigned src_xmm;

        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            dst->kind != AS_OPERAND_REGISTER || parse_x86_reg(dst->u.reg, &gr) != 0) {
            return -1;
        }
        dst_bits = x86_reg_width_bits(dst->u.reg);
        if (dst_bits != 32 && dst_bits != 64) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &src_xmm) == 0) {
            rex = (unsigned char)(0x40u | (dst_bits == 64 ? 0x08u : 0u) | ((((unsigned)gr) & 8u) ? 0x04u : 0u) |
                                  ((src_xmm & 8u) ? 0x01u : 0u));
            out[0] = 0xf2;
            if (rex != 0x40u) {
                out[1] = rex;
                out[2] = 0x0f;
                out[3] = 0x2c;
                out[4] = (unsigned char)(0xc0u | ((((unsigned)gr) & 7u) << 3) | (src_xmm & 7u));
                if (out_len != NULL) {
                    *out_len = 5;
                }
            } else {
                out[1] = 0x0f;
                out[2] = 0x2c;
                out[3] = (unsigned char)(0xc0u | ((((unsigned)gr) & 7u) << 3) | (src_xmm & 7u));
                if (out_len != NULL) {
                    *out_len = 4;
                }
            }
            return 0;
        }
        if (src->kind == AS_OPERAND_MEMORY) {
            return emit_x86_64_regfield_memop(0xf2, 0x2c, (unsigned)gr, dst_bits == 64, &src->u.mem, out, out_cap,
                                              out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "cvttss2si") == 0) {
        int dst_bits;
        unsigned src_xmm;

        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            dst->kind != AS_OPERAND_REGISTER || parse_x86_reg(dst->u.reg, &gr) != 0) {
            return -1;
        }
        dst_bits = x86_reg_width_bits(dst->u.reg);
        if (dst_bits != 32 && dst_bits != 64) {
            return -1;
        }
        if (src->kind == AS_OPERAND_REGISTER && parse_xmm_reg(src->u.reg, &src_xmm) == 0) {
            rex = (unsigned char)(0x40u | (dst_bits == 64 ? 0x08u : 0u) | ((((unsigned)gr) & 8u) ? 0x04u : 0u) |
                                  ((src_xmm & 8u) ? 0x01u : 0u));
            out[0] = 0xf3;
            if (rex != 0x40u) {
                out[1] = rex;
                out[2] = 0x0f;
                out[3] = 0x2c;
                out[4] = (unsigned char)(0xc0u | ((((unsigned)gr) & 7u) << 3) | (src_xmm & 7u));
                if (out_len != NULL) {
                    *out_len = 5;
                }
            } else {
                out[1] = 0x0f;
                out[2] = 0x2c;
                out[3] = (unsigned char)(0xc0u | ((((unsigned)gr) & 7u) << 3) | (src_xmm & 7u));
                if (out_len != NULL) {
                    *out_len = 4;
                }
            }
            return 0;
        }
        if (src->kind == AS_OPERAND_MEMORY) {
            return emit_x86_64_regfield_memop(0xf3, 0x2c, (unsigned)gr, dst_bits == 64, &src->u.mem, out, out_cap,
                                              out_len);
        }
        return -1;
    }
    if (strcmp(mnbuf, "vpbroadcastd") == 0) {
        as_x86_avx2_insn_t avx2;
        unsigned yd;
        unsigned xs;
        char avxerr[128];
        if (isa_level < 3u) {
            return -2;
        }
        if (insn->operand_count != 2 || src == NULL || dst == NULL ||
            src->kind != AS_OPERAND_REGISTER || dst->kind != AS_OPERAND_REGISTER ||
            parse_xmm_reg(src->u.reg, &xs) != 0 || parse_ymm_reg(dst->u.reg, &yd) != 0) {
            return -1;
        }
        memset(&avx2, 0, sizeof(avx2));
        avx2.mnemonic = "vpbroadcastd";
        avx2.op_count = 2;
        avx2.vector_bits = 256;
        avx2.op1.kind = AS_X86_OP_REG;
        avx2.op1.u.reg = (as_x86_reg_t)yd;
        avx2.op2.kind = AS_X86_OP_REG;
        avx2.op2.u.reg = (as_x86_reg_t)xs;
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
        if (parse_x86_reg(op->u.reg, &dst->u.reg) != 0) {
            snprintf(errbuf, errbuf_sz, "unknown x86 register: %s", op->u.reg != NULL ? op->u.reg : "<null>");
            return -1;
        }
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
            dst->u.imm = 0;
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
        dst->kind = AS_X86_OP_MEM;
        memset(&dst->u.mem, 0, sizeof(dst->u.mem));
        dst->u.mem.size_bits = (unsigned)(op->u.mem.size_bits > 0 ? op->u.mem.size_bits : 0);
        if (op->u.mem.base_reg != NULL) {
            if (is64 && streq_ci(op->u.mem.base_reg, "rip")) {
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
            if (parse_int64(d->args[i], &v) == 0) {
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

    if (strcmp(d->name, ".zero") == 0 || strcmp(d->name, ".space") == 0) {
        if (d->arg_count < 1 || parse_int64(d->args[0], &v) != 0 || v < 0) {
            return -1;
        }
        if (bytebuf_append_zeros(buf, (size_t)v) != 0) {
            return -1;
        }
        return 1;
    }

    if (strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 || strcmp(d->name, ".p2align") == 0) {
        size_t align = 1;
        size_t need;
        if (d->arg_count < 1 || parse_int64(d->args[0], &v) != 0 || v < 0) {
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
        if (need > 0 && bytebuf_append_zeros(buf, need) != 0) {
            return -1;
        }
        return 1;
    }

    if (strcmp(d->name, ".ascii") == 0 || strcmp(d->name, ".asciz") == 0 || strcmp(d->name, ".string") == 0) {
        int nul = (strcmp(d->name, ".ascii") == 0) ? 0 : 1;
        for (i = 0; i < d->arg_count; ++i) {
            const char *s = d->args[i] != NULL ? d->args[i] : "";
            if (bytebuf_append(buf, s, strlen(s)) != 0) {
                return -1;
            }
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

static int section_track_init(section_track_t *st) {
    if (st == NULL) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->current = xstrdup(".text");
    st->previous = xstrdup(".text");
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
    int explicit_xmm0_mask;
    int immediate_3op;

    if (in == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    memset(&s41, 0, sizeof(s41));
    s41.mnemonic = in->mnemonic;
    explicit_xmm0_mask =
        (streq_ci(in->mnemonic, "blendvps") || streq_ci(in->mnemonic, "blendvpd") || streq_ci(in->mnemonic, "pblendvb")) &&
        in->op_count == 3 && in->ops[0].kind == AS_X86_OP_REG && in->ops[0].u.reg == 0;
    immediate_3op = in->op_count == 3 &&
                    (in->ops[0].kind == AS_X86_OP_IMM || in->ops[2].kind == AS_X86_OP_IMM);

    if (explicit_xmm0_mask) {
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
    if (!explicit_xmm0_mask && !immediate_3op && in->op_count >= 3 && in->ops[2].kind == AS_X86_OP_IMM) {
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

static int encode_x86_stmt(const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code, size_t code_cap,
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

    intel_syntax = (st->u.instr.syntax_intel != 0) ? 1 : (cfg->intel_syntax != 0);
    memset(&in, 0, sizeof(in));
    if (normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        snprintf(encerr, encerr_sz, "unsupported mnemonic length");
        return -1;
    }
    in.mnemonic = mnbuf;
    in.seg_override = map_seg(st->u.instr.segment_override);
    in.lock_prefix = (st->u.instr.prefixes & AS_PREFIX_LOCK) != 0;
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
        in.operand_size_override = 1;
    } else if (suffix == 'q') {
        in.rex_w = 1;
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

    if (intel_syntax && suffix == '\0' && mnemonic_needs_uniform_width(mnbuf) && in.op_count > 0) {
        int bits = 0;
        if (infer_uniform_operand_width_bits(&st->u.instr, op_index, in.op_count, &bits, encerr, encerr_sz) != 0) {
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

    if (streq_ci(mnbuf, "mov") && in.op_count == 2) {
        const as_operand_t *dst_raw = &st->u.instr.operands[op_index[0]];
        const as_operand_t *src_raw = &st->u.instr.operands[op_index[1]];
        long long immv;
        if (dst_raw->kind == AS_OPERAND_REGISTER && src_raw->kind == AS_OPERAND_IMMEDIATE &&
            is_x86_low8_reg(dst_raw->u.reg) && eval_expr_const(src_raw->u.expr, &immv) == 0 &&
            (immv < -128 || immv > 255)) {
            fprintf(stderr, "as: warning: %s:%u: immediate truncated to 8 bits\n",
                    st->file != NULL ? st->file : "<input>", st->line);
        }
    }

    if (!cfg->is_64 && emit_i386_special(&st->u.instr, intel_syntax, code, code_cap, code_len) == 0) {
        return 0;
    }
    if (cfg->is_64) {
        int s64 = emit_x86_64_special(&st->u.instr, intel_syntax, cfg->x86_64_isa_level, code, code_cap, code_len);
        if (s64 == 0) {
            return 0;
        }
        if (s64 == -2) {
            snprintf(encerr, encerr_sz, "AVX2 instruction requires -march=x86-64-v3 or higher");
            return -1;
        }
    }

    for (j = 0; j < in.op_count; ++j) {
        if (convert_operand_x86(&st->u.instr.operands[op_index[j]], in.mnemonic, &in.ops[j], cfg->is_64, intel_syntax, encerr,
                                encerr_sz) != 0) {
            return -1;
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
        if (try_encode_x86_sse3(&in, code, code_cap, code_len, encerr, encerr_sz) == 0) {
            return 0;
        }
        if (try_encode_x86_ssse3(&in, code, code_cap, code_len, encerr, encerr_sz) == 0) {
            return 0;
        }
        if (try_encode_x86_sse41(&in, code, code_cap, code_len, encerr, encerr_sz) == 0) {
            return 0;
        }
        if (try_encode_x86_sse42(&in, code, code_cap, code_len, encerr, encerr_sz) == 0) {
            return 0;
        }
        if (as_x86_encode_i386(&in, code, code_cap, code_len, encerr, encerr_sz) != 0) {
            return -1;
        }
    }
    return 0;
}

static int emit_text_program(emit_ctx_t *ctx) {
    sec_buf_vec_t secbufs;
    size_t i;
    unsigned char code[32];
    size_t code_len;
    char encerr[256];
    section_track_t track;
    int trc;

    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        int in_exec;
        int drc;

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
            drc = append_directive_data(&sb->buf, &st->u.directive);
            if (drc < 0) {
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
            if (encode_x86_stmt(ctx->cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
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
    size_t i;

    for (i = 0; i < ctx->sym_count; ++i) {
        if (strcmp(ctx->sym_map[i].name, name) == 0) {
            return ctx->sym_map[i].sym;
        }
    }
    return NULL;
}

static int append_emit_symbol(emit_ctx_t *ctx, const char *name, elf_symbol_t *sym);

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

    sym = elf_add_symbol(ctx->obj, name, 0, 0, STB_GLOBAL, STT_NOTYPE);
    if (sym == NULL) {
        return NULL;
    }
    sec = section_for_name(ctx, name);
    if (sec != NULL) {
        (void)elf_symbol_define(sym, sec, 0);
    }
    if (append_emit_symbol(ctx, name, sym) != 0) {
        return NULL;
    }
    return sym;
}

static int append_emit_symbol(emit_ctx_t *ctx, const char *name, elf_symbol_t *sym) {
    emit_sym_t *next;

    next = (emit_sym_t *)realloc(ctx->sym_map, (ctx->sym_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    ctx->sym_map = next;
    ctx->sym_map[ctx->sym_count].name = xstrdup(name);
    ctx->sym_map[ctx->sym_count].sym = sym;
    if (ctx->sym_map[ctx->sym_count].name == NULL) {
        return -1;
    }
    ctx->sym_count++;
    return 0;
}

static int emit_data_program(emit_ctx_t *ctx, const as_data_program_t *data) {
    sec_buf_vec_t secbufs;
    section_track_t track;
    size_t i;
    int trc;

    (void)data;
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track) != 0) {
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
        drc = append_directive_data(&sb->buf, &st->u.directive);
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

static int is_numeric_local_label_name(const char *name) {
    return name != NULL && name[0] >= '0' && name[0] <= '9' && name[1] == '\0';
}

static const sym_loc_t *find_sym_loc(const sym_loc_t *locs, size_t count, const char *name) {
    size_t i;

    if (locs == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(locs[i].name, name) == 0) {
            return &locs[i];
        }
    }
    return NULL;
}

static int upsert_sym_loc(sym_loc_t **locs, size_t *count, size_t *cap, const char *name, elf_section_t *sec, uint64_t off) {
    sym_loc_t *next;
    size_t i;

    if (locs == NULL || count == NULL || cap == NULL || name == NULL) {
        return -1;
    }

    for (i = 0; i < *count; ++i) {
        if (strcmp((*locs)[i].name, name) == 0) {
            (*locs)[i].sec = sec;
            (*locs)[i].off = off;
            return 0;
        }
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
    (*count)++;
    return 0;
}

static int directive_assigns_symbol(const as_directive_t *d, const char *sym_name) {
    char *lhs;
    int ok = 0;

    if (d == NULL || sym_name == NULL || d->name == NULL || d->arg_count < 1) {
        return 0;
    }
    if (strcmp(d->name, ".set") != 0 && strcmp(d->name, ".equ") != 0) {
        return 0;
    }
    lhs = trim_copy(d->args[0]);
    if (lhs == NULL) {
        return 0;
    }
    if (strcmp(lhs, sym_name) == 0) {
        ok = 1;
    }
    free(lhs);
    return ok;
}

static int find_set_dot_location(emit_ctx_t *ctx, const char *sym_name, const char *file, unsigned line,
                                 elf_section_t **sec_out, uint64_t *off_out) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || sym_name == NULL || file == NULL || sec_out == NULL || off_out == NULL) {
        return -1;
    }
    *sec_out = NULL;
    *off_out = 0;

    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        uint64_t cur_off;
        elf_section_t *cur_sec;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        cur_off = (uint64_t)sb->buf.len;
        cur_sec = section_for_name(ctx, track.current);

        if (st->kind == AS_STMT_DIRECTIVE && st->file != NULL && strcmp(st->file, file) == 0 && st->line == line &&
            directive_assigns_symbol(&st->u.directive, sym_name)) {
            *sec_out = cur_sec;
            *off_out = cur_off;
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return 0;
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc;
            int drc;

            trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            drc = append_directive_data(&sb->buf, &st->u.directive);
            if (drc < 0) {
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

            if (encode_x86_stmt(ctx->cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
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

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return -1;
}

static int collect_symbol_locations(emit_ctx_t *ctx, sym_loc_t **locs, size_t *loc_count, size_t *loc_cap) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || locs == NULL || loc_count == NULL || loc_cap == NULL) {
        return -1;
    }
    *locs = NULL;
    *loc_count = 0;
    *loc_cap = 0;
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track) != 0) {
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
            if (upsert_sym_loc(locs, loc_count, loc_cap, st->labels[j].name, cur_sec, cur_off) != 0) {
                free_sym_locs(*locs, *loc_count);
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
                *locs = NULL;
                *loc_count = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            drc = append_directive_data(&sb->buf, &st->u.directive);
            if (drc < 0) {
                free_sym_locs(*locs, *loc_count);
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
            if (encode_x86_stmt(ctx->cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
                free_sym_locs(*locs, *loc_count);
                *locs = NULL;
                *loc_count = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                set_err(ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, encerr);
                return -1;
            }
            if (bytebuf_append(&sb->buf, code, code_len) != 0) {
                free_sym_locs(*locs, *loc_count);
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

static int emit_symbols(emit_ctx_t *ctx, const as_symtab_t *symtab) {
    size_t i;
    sym_loc_t *locs = NULL;
    size_t loc_count = 0;
    size_t loc_cap = 0;

    if (collect_symbol_locations(ctx, &locs, &loc_count, &loc_cap) != 0) {
        return -1;
    }

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;

        esym = elf_add_symbol(ctx->obj, s->name, 0, s->size, map_bind(s->bind), map_type(s->type));
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
        if (elf_symbol_set_visibility(esym, map_vis(s->visibility)) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
        if (s->version != NULL && elf_symbol_set_version(esym, 1) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
        if (append_emit_symbol(ctx, s->name, esym) != 0) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
    }

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym = ensure_emit_symbol(ctx, s->name);
        const sym_loc_t *loc;

        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
        if (s->is_common) {
            if (elf_symbol_set_shndx(esym, SHN_COMMON) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                return -1;
            }
            if (elf_symbol_set_value(esym, s->common_size) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                return -1;
            }
        } else if (s->is_absolute) {
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK ||
                elf_symbol_set_value(esym, s->absolute_value) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                return -1;
            }
        } else if (s->defined) {
            if (s->alias_target != NULL) {
                continue;
            }
            loc = find_sym_loc(locs, loc_count, s->name);
            if (loc != NULL && loc->sec != NULL) {
                if (elf_symbol_define(esym, loc->sec, loc->off) != ELF_OK) {
                    free_sym_locs(locs, loc_count);
                    return -1;
                }
            } else if (ctx->text_sec != NULL && elf_symbol_define(esym, ctx->text_sec, 0) != ELF_OK) {
                free_sym_locs(locs, loc_count);
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

        if (s->alias_target == NULL) {
            continue;
        }
        esym = ensure_emit_symbol(ctx, s->name);
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
        if (s->alias_from_dot) {
            const sym_loc_t *target_loc = find_sym_loc(locs, loc_count, s->alias_target);
            elf_section_t *dot_sec = NULL;
            uint64_t dot_off = 0;

            if (target_loc == NULL || target_loc->sec == NULL) {
                set_err(ctx, "cannot resolve .-expression target for %s", s->name);
                free_sym_locs(locs, loc_count);
                return -1;
            }
            if (find_set_dot_location(ctx, s->name, s->def_file, s->def_line, &dot_sec, &dot_off) != 0 || dot_sec == NULL) {
                set_err(ctx, "cannot resolve current location for %s", s->name);
                free_sym_locs(locs, loc_count);
                return -1;
            }
            if (dot_sec != target_loc->sec) {
                set_err(ctx, "cross-section .-symbol assignment is not supported for %s", s->name);
                free_sym_locs(locs, loc_count);
                return -1;
            }
            v = (int64_t)dot_off - (int64_t)target_loc->off + s->alias_addend;
            if (v < 0) {
                set_err(ctx, "negative absolute value for %s", s->name);
                free_sym_locs(locs, loc_count);
                return -1;
            }
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK || elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                return -1;
            }
            continue;
        }
        target = ensure_emit_symbol(ctx, s->alias_target);
        if (target == NULL) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
        shndx = elf_symbol_shndx(target);
        if (elf_symbol_set_shndx(esym, shndx) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
        v = (int64_t)elf_symbol_value(target) + s->alias_addend;
        if (elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            return -1;
        }
    }

    free_sym_locs(locs, loc_count);
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
    int sign = 1;
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

    for (i = 1; compact[i] != '\0'; ++i) {
        if (compact[i] == '+' || compact[i] == '-') {
            sep = &compact[i];
            break;
        }
    }
    if (sep != NULL) {
        long long addv;
        char opch = *sep;
        *sep = '\0';
        sign = opch == '-' ? -1 : 1;
        if (parse_int64(sep + 1, &addv) == 0) {
            *add_out = (int64_t)(sign > 0 ? addv : -addv);
        }
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

    if (parse_int64(arg, &v) == 0) {
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

static int append_incbin_bytes(emit_ctx_t *ctx, const as_stmt_t *st, bin_section_t *sec, const as_directive_t *d) {
    const char *path_arg;
    char *resolved = NULL;
    unsigned long long skip = 0;
    unsigned long long count = 0;
    int has_count = 0;
    FILE *fp = NULL;
    unsigned char tmp[1024];

    if (d->arg_count < 1) {
        set_err(ctx, "%s:%u: .incbin requires path argument", st->file != NULL ? st->file : "<input>", st->line);
        return -1;
    }
    path_arg = d->args[0];
    if (path_arg == NULL || path_arg[0] == '\0') {
        set_err(ctx, "%s:%u: malformed .incbin path", st->file != NULL ? st->file : "<input>", st->line);
        return -1;
    }
    if (path_arg[0] == '/') {
        resolved = xstrdup(path_arg);
    } else {
        char *dir = dirname_dup2(st->file);
        if (dir != NULL) {
            resolved = join_path2(dir, path_arg);
        }
        free(dir);
    }
    if (resolved == NULL) {
        set_err(ctx, "%s:%u: failed to resolve .incbin path", st->file != NULL ? st->file : "<input>", st->line);
        return -1;
    }
    if (d->arg_count >= 2 && parse_nonneg_u64_or_reloc(ctx, st, d->args[1], ".incbin skip", &skip) != 0) {
        free(resolved);
        return -1;
    }
    if (d->arg_count >= 3 && parse_nonneg_u64_or_reloc(ctx, st, d->args[2], ".incbin count", &count) != 0) {
        free(resolved);
        return -1;
    }
    has_count = (d->arg_count >= 3) ? 1 : 0;

    fp = fopen(resolved, "rb");
    if (fp == NULL) {
        set_err(ctx, "%s:%u: failed to open .incbin file %s", st->file != NULL ? st->file : "<input>", st->line, resolved);
        free(resolved);
        return -1;
    }
    free(resolved);

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
        if (bytebuf_append(&sec->buf, tmp, nread) != 0) {
            fclose(fp);
            set_err(ctx, "%s:%u: out of memory while appending .incbin", st->file != NULL ? st->file : "<input>",
                    st->line);
            return -1;
        }
        if (has_count) {
            count -= nread;
        }
    }
    fclose(fp);
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
            if (bytebuf_append(&sec->buf, s, strlen(s)) != 0) {
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
        size_t align;
        size_t need;
        *handled = 1;
        if (d->arg_count < 1 || parse_nonneg_u64_or_reloc(ctx, st, d->args[0], d->name, &raw) != 0) {
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
        if (need > 0 && bytebuf_append_zeros(&sec->buf, need) != 0) {
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
        *handled = 1;
        if (d->arg_count < 1 || parse_nonneg_u64_or_reloc(ctx, st, d->args[0], d->name, &n) != 0) {
            return -1;
        }
        if (n > 0 && bytebuf_append_zeros(&sec->buf, (size_t)n) != 0) {
            set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        sec->touched = 1;
        return 0;
    }
    if (strcmp(d->name, ".fill") == 0) {
        unsigned long long repeat = 0;
        unsigned long long size = 1;
        unsigned long long value = 0;
        *handled = 1;
        if (d->arg_count < 1 || parse_nonneg_u64_or_reloc(ctx, st, d->args[0], ".fill repeat", &repeat) != 0) {
            return -1;
        }
        if (d->arg_count >= 2 && parse_nonneg_u64_or_reloc(ctx, st, d->args[1], ".fill size", &size) != 0) {
            return -1;
        }
        if (d->arg_count >= 3 && parse_nonneg_u64_or_reloc(ctx, st, d->args[2], ".fill value", &value) != 0) {
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
            if (parse_int64(d->args[i], &v) != 0) {
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
                return -1;
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
            if (encode_x86_stmt(cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
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
            if (streq_ci(in->mnemonic, "call")) {
                return R_X86_64_PLT32;
            }
            return R_X86_64_PC32;
        }
        if (op != NULL && op->kind == AS_OPERAND_MEMORY && op->u.mem.base_reg != NULL &&
            streq_ci(op->u.mem.base_reg, "rip")) {
            return R_X86_64_PC32;
        }
        return R_X86_64_64;
    }
    return reloc_type_for_machine(machine);
}

static int emit_relocations(emit_ctx_t *ctx) {
    size_t i;
    unsigned machine = ctx->cfg != NULL ? ctx->cfg->machine : EM_386;
    sec_buf_vec_t secbufs;
    section_track_t track;
    static int trace_env = -1;

    if (trace_env < 0) {
        const char *v = getenv("AS_DEBUG_RELOC_TRACE");
        trace_env = (v != NULL && v[0] != '\0') ? 1 : 0;
    }
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track) != 0) {
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

        if (st->kind == AS_STMT_DIRECTIVE) {
            const as_directive_t *d = &st->u.directive;
            int drc;

            trc = section_track_apply_directive(&track, d);
            if (trc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            {
                unsigned width = 0;
                if (strcmp(d->name, ".byte") == 0) width = 1;
                else if (strcmp(d->name, ".word") == 0 || strcmp(d->name, ".short") == 0 ||
                         strcmp(d->name, ".hword") == 0 || strcmp(d->name, ".2byte") == 0) width = 2;
                else if (strcmp(d->name, ".long") == 0 || strcmp(d->name, ".4byte") == 0) width = 4;
                else if (strcmp(d->name, ".quad") == 0 || strcmp(d->name, ".8byte") == 0) width = 8;

                if (width != 0) {
                    for (j = 0; j < d->arg_count; ++j) {
                        char *sym = NULL;
                        int64_t addend = 0;
                        if (parse_symbol_addend_arg(d->args[j], &sym, &addend) == 0 && sym != NULL) {
                            uint32_t t = reloc_type_for_machine(machine);
                            if (add_reloc_for_symbol_ex(ctx, cur_sec, sym, cur_off + (uint64_t)(j * width), t, addend) != 0) {
                                free(sym);
                                section_track_free(&track);
                                sec_buf_vec_free(&secbufs);
                                return -1;
                            }
                            free(sym);
                        }
                    }
                }
            }
            drc = append_directive_data(&sb->buf, d);
            if (drc < 0) {
                set_err(ctx, "%s:%u: malformed directive data", st->file != NULL ? st->file : "<input>", st->line);
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
            unsigned char code[32];
            size_t code_len = 0;
            char encerr[256];
            size_t rel_count = 0;

            if (encode_x86_stmt(ctx->cfg, st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
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

                if (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) {
                    e = op->u.expr;
                } else if (op->kind == AS_OPERAND_MEMORY) {
                    e = op->u.mem.disp;
                }
                sym = first_symbol_in_expr(e);
                if (sym == NULL) {
                    continue;
                }
                t = default_text_reloc_type(machine, &st->u.instr, op);
                if (machine == EM_X86_64 && t == R_X86_64_64) {
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
                if (code_len < reloc_width) {
                    set_err(ctx, "%s:%u: relocation width exceeds encoded instruction size",
                            st->file != NULL ? st->file : "<input>", st->line);
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                if (machine == EM_X86_64) {
                    if ((op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
                        is_rel_mnemonic(st->u.instr.mnemonic)) {
                        addend = -4;
                    } else if (op->kind == AS_OPERAND_MEMORY &&
                               op->u.mem.base_reg != NULL &&
                               streq_ci(op->u.mem.base_reg, "rip")) {
                        addend = -4;
                    }
                }
                if (rel_count > 0) {
                    set_err(ctx, "%s:%u: multiple symbolic relocations in one x86 instruction are not yet supported",
                            st->file != NULL ? st->file : "<input>", st->line);
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                reloc_off = cur_off + (uint64_t)code_len - reloc_width;
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
        if (s->group != NULL) {
            (void)elf_section_set_group(es, 1, s->comdat);
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

    if (emit_data_program(&ctx, data) != 0) {
        set_err(&ctx, "failed to emit data program");
        goto fail;
    }

    if (emit_symbols(&ctx, symtab) != 0) {
        set_err(&ctx, "failed to emit symbols");
        goto fail;
    }

    if (emit_relocations(&ctx) != 0) {
        if (ctx.errbuf == NULL || ctx.errbuf[0] == '\0') {
            set_err(&ctx, "failed to emit relocations");
        }
        goto fail;
    }

    if (ensure_section_exists(&ctx, ".note.GNU-stack", SHT_PROGBITS, 0, 1, NULL, 0) != 0) {
        set_err(&ctx, "failed to emit .note.GNU-stack");
        goto fail;
    }

    if (cfg->machine == EM_X86_64 && cfg->x86_64_isa_level >= 2) {
        static const unsigned char gnu_prop[] = {
            4, 0, 0, 0, 16, 0, 0, 0, 5, 0, 0, 0,
            'G', 'N', 'U', 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
        };
        if (ensure_section_exists(&ctx, ".note.gnu.property", SHT_NOTE, SHF_ALLOC, 4, gnu_prop, sizeof(gnu_prop)) != 0) {
            set_err(&ctx, "failed to emit .note.gnu.property");
            goto fail;
        }
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
    elf_close(ctx.obj);
    return 0;

fail:
    for (i = 0; i < ctx.sym_count; ++i) {
        free(ctx.sym_map[i].name);
    }
    free(ctx.sym_map);
    elf_close(ctx.obj);
    return -1;
}
