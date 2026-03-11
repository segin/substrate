#include "as_x86_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *out;
    size_t out_cap;
    size_t at;
    char *errbuf;
    size_t errbuf_sz;
} enc_ctx_t;

static void set_err(enc_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
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

static int emit8(enc_ctx_t *ctx, uint8_t v) {
    if (ctx->at >= ctx->out_cap) {
        set_err(ctx, "encoding overflow");
        return -1;
    }
    ctx->out[ctx->at++] = v;
    return 0;
}

static int emit32(enc_ctx_t *ctx, uint32_t v) {
    if (emit8(ctx, (uint8_t)(v & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 8) & 0xffu)) != 0 ||
        emit8(ctx, (uint8_t)((v >> 16) & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 24) & 0xffu)) != 0) {
        return -1;
    }
    return 0;
}

static int emit64(enc_ctx_t *ctx, uint64_t v) {
    if (emit32(ctx, (uint32_t)(v & 0xffffffffu)) != 0 ||
        emit32(ctx, (uint32_t)((v >> 32) & 0xffffffffu)) != 0) {
        return -1;
    }
    return 0;
}

static int mem_is_single_base_reg(const as_x86_operand_t *op, as_x86_reg_t reg) {
    const as_x86_mem_t *m;

    if (op == NULL || op->kind != AS_X86_OP_MEM) {
        return 0;
    }
    m = &op->u.mem;
    return m->has_base && m->base == reg && !m->has_index && !m->has_disp;
}

static int reg_is_exact_low3(const as_x86_operand_t *op, as_x86_reg_t reg) {
    if (op == NULL || op->kind != AS_X86_OP_REG) {
        return 0;
    }
    if (op->u.reg >= AS_X86_REG_AH || reg >= AS_X86_REG_AH) {
        return op->u.reg == reg;
    }
    return (((unsigned)op->u.reg) & 7u) == (((unsigned)reg) & 7u);
}

static int is_high8_reg(as_x86_reg_t r) {
    return r == AS_X86_REG_AH || r == AS_X86_REG_CH ||
           r == AS_X86_REG_DH || r == AS_X86_REG_BH;
}

static uint8_t reg_code3(as_x86_reg_t r) {
    switch (r) {
    case AS_X86_REG_AH:
        return 4;
    case AS_X86_REG_CH:
        return 5;
    case AS_X86_REG_DH:
        return 6;
    case AS_X86_REG_BH:
        return 7;
    default:
        return (uint8_t)(r & 7);
    }
}

static int is_dx_port_operand(const as_x86_operand_t *op) {
    return reg_is_exact_low3(op, AS_X86_REG_EDX) || mem_is_single_base_reg(op, AS_X86_REG_EDX);
}

static int mem_operand_bits(const as_x86_operand_t *op) {
    if (op == NULL || op->kind != AS_X86_OP_MEM) {
        return 0;
    }
    return (int)op->u.mem.size_bits;
}

static int merge_string_op_bits(const as_x86_operand_t *a, const as_x86_operand_t *b) {
    int ab = mem_operand_bits(a);
    int bb = mem_operand_bits(b);

    if (ab != 0 && bb != 0 && ab != bb) {
        return -1;
    }
    if (ab != 0) {
        return ab;
    }
    return bb;
}

static int default_string_op_bits(const as_x86_insn_t *insn) {
    if (insn == NULL) {
        return 0;
    }
    if (insn->byte_op) {
        return 8;
    }
    if (insn->operand_size_override) {
        return 16;
    }
    if (insn->rex_w) {
        return 64;
    }
    return 32;
}

static int resolved_string_op_bits(const as_x86_insn_t *insn, const as_x86_operand_t *a, const as_x86_operand_t *b) {
    int bits = merge_string_op_bits(a, b);

    if (bits != 0) {
        return bits;
    }
    return default_string_op_bits(insn);
}

static int is_disp8(int32_t v) {
    return v >= -128 && v <= 127;
}

static int emit_prefixes(enc_ctx_t *ctx, const as_x86_insn_t *insn) {
    if (insn->lock_prefix && emit8(ctx, 0xf0) != 0) {
        return -1;
    }
    if (insn->rep_prefix == 1 && emit8(ctx, 0xf3) != 0) {
        return -1;
    }
    if (insn->rep_prefix == 2 && emit8(ctx, 0xf2) != 0) {
        return -1;
    }
    if (insn->operand_size_override && emit8(ctx, 0x66) != 0) {
        return -1;
    }
    if (insn->address_size_override && emit8(ctx, 0x67) != 0) {
        return -1;
    }
    switch (insn->seg_override) {
    case AS_X86_SEG_CS:
        return emit8(ctx, 0x2e);
    case AS_X86_SEG_DS:
        return emit8(ctx, 0x3e);
    case AS_X86_SEG_ES:
        return emit8(ctx, 0x26);
    case AS_X86_SEG_FS:
        return emit8(ctx, 0x64);
    case AS_X86_SEG_GS:
        return emit8(ctx, 0x65);
    case AS_X86_SEG_SS:
        return emit8(ctx, 0x36);
    default:
        return 0;
    }
}

static int modrm_sib_disp(enc_ctx_t *ctx, uint8_t reg_field, const as_x86_operand_t *rm_op) {
    uint8_t mod;
    uint8_t rm;
    uint8_t modrm;

    if (rm_op->kind == AS_X86_OP_REG) {
        modrm = (uint8_t)(0xc0u | (reg_code3((as_x86_reg_t)reg_field) << 3) | reg_code3(rm_op->u.reg));
        return emit8(ctx, modrm);
    }

    if (rm_op->kind != AS_X86_OP_MEM) {
        set_err(ctx, "expected r/m operand");
        return -1;
    }

    {
        const as_x86_mem_t *m = &rm_op->u.mem;
        int needs_sib = 0;
        uint8_t sib = 0;

        if (m->scale != 0 && m->scale != 1 && m->scale != 2 && m->scale != 4 && m->scale != 8) {
            set_err(ctx, "invalid scale %u", m->scale);
            return -1;
        }
        if (m->rip_relative) {
            set_err(ctx, "RIP-relative mode is x86-64 only");
            return -1;
        }
        if (m->has_index && m->index == AS_X86_REG_ESP) {
            set_err(ctx, "ESP cannot be used as SIB index");
            return -1;
        }

        if (m->disp_only) {
            mod = 0;
            rm = 5;
            modrm = (uint8_t)((mod << 6) | ((reg_field & 7u) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }

        if (!m->has_base && !m->has_index) {
            set_err(ctx, "memory operand missing base/index");
            return -1;
        }

        if (m->has_index && !m->has_base) {
            mod = 0;
            rm = 4;
            needs_sib = 1;
            sib = (uint8_t)((m->scale == 8 ? 3 : (m->scale == 4 ? 2 : (m->scale == 2 ? 1 : 0))) << 6);
            sib |= (uint8_t)((m->index & 7) << 3);
            sib |= 5;
            modrm = (uint8_t)((mod << 6) | ((reg_field & 7u) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, sib) != 0 || emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0)) != 0) {
                return -1;
            }
            return 0;
        }

        if (!m->has_disp && m->base != AS_X86_REG_EBP) {
            mod = 0;
        } else if (m->has_disp && is_disp8(m->disp)) {
            mod = 1;
        } else {
            mod = 2;
        }

        rm = (uint8_t)(m->base & 7);
        if (m->has_index || m->base == AS_X86_REG_ESP) {
            uint8_t scale_bits;
            uint8_t index_bits = 4;
            uint8_t base_bits = (uint8_t)(m->base & 7);

            needs_sib = 1;
            rm = 4;

            if (m->scale == 8) {
                scale_bits = 3;
            } else if (m->scale == 4) {
                scale_bits = 2;
            } else if (m->scale == 2) {
                scale_bits = 1;
            } else {
                scale_bits = 0;
            }
            if (m->has_index) {
                index_bits = (uint8_t)(m->index & 7);
            }
            sib = (uint8_t)((scale_bits << 6) | (index_bits << 3) | base_bits);
        }

        modrm = (uint8_t)((mod << 6) | (reg_code3((as_x86_reg_t)reg_field) << 3) | rm);
        if (emit8(ctx, modrm) != 0) {
            return -1;
        }
        if (needs_sib && emit8(ctx, sib) != 0) {
            return -1;
        }

        if (mod == 1) {
            return emit8(ctx, (uint8_t)m->disp);
        }
        if (mod == 2 || (mod == 0 && m->base == AS_X86_REG_EBP && !m->has_disp)) {
            return emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0));
        }
    }

    return 0;
}

static uint8_t reg_low3(as_x86_reg_t r) {
    return reg_code3(r);
}

static uint8_t reg_ext(as_x86_reg_t r) {
    if (is_high8_reg(r)) {
        return 0;
    }
    return (uint8_t)((r >> 3) & 1);
}

static int needs_rex_low8(as_x86_reg_t r) {
    uint8_t lo = reg_low3(r);
    if (is_high8_reg(r)) {
        return 0;
    }
    if (reg_ext(r)) {
        return 1;
    }
    return lo == AS_X86_REG_RSP || lo == AS_X86_REG_RBP || lo == AS_X86_REG_RSI || lo == AS_X86_REG_RDI;
}

static uint8_t scale_bits(unsigned scale) {
    if (scale == 8) {
        return 3;
    }
    if (scale == 4) {
        return 2;
    }
    if (scale == 2) {
        return 1;
    }
    return 0;
}

static int modrm_sib_disp64(enc_ctx_t *ctx, as_x86_reg_t reg_field, const as_x86_operand_t *rm_op,
                            uint8_t *rex_r, uint8_t *rex_x, uint8_t *rex_b) {
    uint8_t mod;
    uint8_t rm;
    uint8_t modrm;

    if (rm_op->kind == AS_X86_OP_REG) {
        *rex_r |= reg_ext(reg_field);
        *rex_b |= reg_ext(rm_op->u.reg);
        modrm = (uint8_t)(0xc0u | (reg_low3(reg_field) << 3) | reg_low3(rm_op->u.reg));
        return emit8(ctx, modrm);
    }
    if (rm_op->kind != AS_X86_OP_MEM) {
        set_err(ctx, "expected r/m operand");
        return -1;
    }

    {
        const as_x86_mem_t *m = &rm_op->u.mem;
        int needs_sib = 0;
        uint8_t sib = 0;

        *rex_r |= reg_ext(reg_field);

        if (m->scale != 0 && m->scale != 1 && m->scale != 2 && m->scale != 4 && m->scale != 8) {
            set_err(ctx, "invalid scale %u", m->scale);
            return -1;
        }
        if (m->has_index && (m->index & 7) == AS_X86_REG_RSP) {
            set_err(ctx, "RSP cannot be used as SIB index");
            return -1;
        }

        if (m->rip_relative) {
            mod = 0;
            rm = 5;
            modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }

        if (m->disp_only) {
            mod = 0;
            rm = 4;
            needs_sib = 1;
            sib = 0x25;
            modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, sib) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }

        if (!m->has_base && !m->has_index) {
            set_err(ctx, "memory operand missing base/index");
            return -1;
        }

        if (m->has_index && !m->has_base) {
            mod = 0;
            rm = 4;
            needs_sib = 1;
            *rex_x |= reg_ext(m->index);
            sib = (uint8_t)(scale_bits(m->scale) << 6);
            sib |= (uint8_t)(reg_low3(m->index) << 3);
            sib |= 5;
            modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, sib) != 0 || emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0)) != 0) {
                return -1;
            }
            return 0;
        }

        if (!m->has_disp && (m->base & 7) != AS_X86_REG_RBP) {
            mod = 0;
        } else if (m->has_disp && is_disp8(m->disp)) {
            mod = 1;
        } else {
            mod = 2;
        }

        rm = reg_low3(m->base);
        *rex_b |= reg_ext(m->base);
        if (m->has_index || reg_low3(m->base) == AS_X86_REG_RSP) {
            uint8_t index_bits = 4;
            uint8_t base_bits = reg_low3(m->base);

            needs_sib = 1;
            rm = 4;

            if (m->has_index) {
                index_bits = reg_low3(m->index);
                *rex_x |= reg_ext(m->index);
            }
            sib = (uint8_t)((scale_bits(m->scale) << 6) | (index_bits << 3) | base_bits);
        }

        modrm = (uint8_t)((mod << 6) | (reg_code3(reg_field) << 3) | rm);
        if (emit8(ctx, modrm) != 0) {
            return -1;
        }
        if (needs_sib && emit8(ctx, sib) != 0) {
            return -1;
        }
        if (mod == 1) {
            return emit8(ctx, (uint8_t)m->disp);
        }
        if (mod == 2 || (mod == 0 && reg_low3(m->base) == AS_X86_REG_RBP && !m->has_disp)) {
            return emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0));
        }
    }

    return 0;
}

static int encode_reg_rm_pair(enc_ctx_t *ctx, uint8_t opcode, const as_x86_operand_t *dst, const as_x86_operand_t *src,
                              int dst_is_rm) {
    if (emit8(ctx, opcode) != 0) {
        return -1;
    }
    if (dst_is_rm) {
        return modrm_sib_disp(ctx, (uint8_t)src->u.reg, dst);
    }
    return modrm_sib_disp(ctx, (uint8_t)dst->u.reg, src);
}

static int encode_jcc_rel32(enc_ctx_t *ctx, const char *mnemonic, int32_t rel) {
    static const struct {
        const char *name;
        uint8_t cc;
    } map[] = {
        {"jo", 0x0},   {"jno", 0x1}, {"jb", 0x2},  {"jnae", 0x2}, {"jc", 0x2},   {"jnb", 0x3},
        {"jae", 0x3},  {"jnc", 0x3}, {"je", 0x4},  {"jz", 0x4},   {"jne", 0x5},  {"jnz", 0x5},
        {"jbe", 0x6},  {"jna", 0x6}, {"ja", 0x7},  {"jnbe", 0x7}, {"js", 0x8},   {"jns", 0x9},
        {"jp", 0xa},   {"jpe", 0xa}, {"jnp", 0xb}, {"jpo", 0xb},  {"jl", 0xc},   {"jnge", 0xc},
        {"jge", 0xd},  {"jnl", 0xd}, {"jle", 0xe}, {"jng", 0xe},  {"jg", 0xf},   {"jnle", 0xf},
    };
    size_t i;

    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (streq_ci(mnemonic, map[i].name)) {
            if (emit8(ctx, 0x0f) != 0 || emit8(ctx, (uint8_t)(0x80 | map[i].cc)) != 0 || emit32(ctx, (uint32_t)rel) != 0) {
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

static int is_reg_or_mem(const as_x86_operand_t *op) {
    return op->kind == AS_X86_OP_REG || op->kind == AS_X86_OP_MEM;
}

int as_x86_encode_i386(const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz) {
    enc_ctx_t ctx;
    const as_x86_operand_t *a;
    const as_x86_operand_t *b;
    const as_x86_operand_t *c;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.out_cap = out_cap;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;

    if (emit_prefixes(&ctx, insn) != 0) {
        return -1;
    }

    a = insn->op_count > 0 ? &insn->ops[0] : NULL;
    b = insn->op_count > 1 ? &insn->ops[1] : NULL;
    c = insn->op_count > 2 ? &insn->ops[2] : NULL;
    c = insn->op_count > 2 ? &insn->ops[2] : NULL;

    if (streq_ci(insn->mnemonic, "mov")) {
        if (insn->byte_op) {
            if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM) {
                if (emit8(&ctx, (uint8_t)(0xb0 | (a->u.reg & 7))) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && is_reg_or_mem(b)) {
                if (encode_reg_rm_pair(&ctx, 0x8a, a, b, 0) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG) {
                if (encode_reg_rm_pair(&ctx, 0x88, a, b, 1) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && a->kind == AS_X86_OP_MEM && b->kind == AS_X86_OP_IMM) {
                if (emit8(&ctx, 0xc6) != 0 || modrm_sib_disp(&ctx, 0, a) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else {
                set_err(&ctx, "unsupported movb form");
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, (uint8_t)(0xb8 | (a->u.reg & 7))) != 0 || emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && is_reg_or_mem(b)) {
            if (encode_reg_rm_pair(&ctx, 0x8b, a, b, 0) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG) {
            if (encode_reg_rm_pair(&ctx, 0x89, a, b, 1) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_MEM && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xc7) != 0 || modrm_sib_disp(&ctx, 0, a) != 0 || emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported mov form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lea")) {
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || b->kind != AS_X86_OP_MEM ||
            emit8(&ctx, 0x8d) != 0 || modrm_sib_disp(&ctx, (uint8_t)a->u.reg, b) != 0) {
            set_err(&ctx, "unsupported lea form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xchg")) {
        if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG) {
            if (encode_reg_rm_pair(&ctx, 0x87, a, b, 1) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported xchg form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xadd")) {
        if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, 0x0f) != 0 || encode_reg_rm_pair(&ctx, insn->byte_op ? 0xc0 : 0xc1, a, b, 1) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported xadd form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "add") || streq_ci(insn->mnemonic, "adc") || streq_ci(insn->mnemonic, "sbb") ||
               streq_ci(insn->mnemonic, "sub") || streq_ci(insn->mnemonic, "and") ||
               streq_ci(insn->mnemonic, "or") || streq_ci(insn->mnemonic, "xor") || streq_ci(insn->mnemonic, "cmp")) {
        uint8_t op_rm_reg;
        uint8_t op_reg_rm;
        uint8_t ext;

        if (streq_ci(insn->mnemonic, "add")) {
            op_rm_reg = 0x01;
            op_reg_rm = 0x03;
            ext = 0;
        } else if (streq_ci(insn->mnemonic, "adc")) {
            op_rm_reg = 0x11;
            op_reg_rm = 0x13;
            ext = 2;
        } else if (streq_ci(insn->mnemonic, "sbb")) {
            op_rm_reg = 0x19;
            op_reg_rm = 0x1b;
            ext = 3;
        } else if (streq_ci(insn->mnemonic, "or")) {
            op_rm_reg = 0x09;
            op_reg_rm = 0x0b;
            ext = 1;
        } else if (streq_ci(insn->mnemonic, "and")) {
            op_rm_reg = 0x21;
            op_reg_rm = 0x23;
            ext = 4;
        } else if (streq_ci(insn->mnemonic, "sub")) {
            op_rm_reg = 0x29;
            op_reg_rm = 0x2b;
            ext = 5;
        } else if (streq_ci(insn->mnemonic, "xor")) {
            op_rm_reg = 0x31;
            op_reg_rm = 0x33;
            ext = 6;
        } else {
            op_rm_reg = 0x39;
            op_reg_rm = 0x3b;
            ext = 7;
        }

        if (insn->byte_op) {
            if (streq_ci(insn->mnemonic, "add")) {
                op_rm_reg = 0x00;
                op_reg_rm = 0x02;
                ext = 0;
            } else if (streq_ci(insn->mnemonic, "or")) {
                op_rm_reg = 0x08;
                op_reg_rm = 0x0a;
                ext = 1;
            } else if (streq_ci(insn->mnemonic, "and")) {
                op_rm_reg = 0x20;
                op_reg_rm = 0x22;
                ext = 4;
            } else if (streq_ci(insn->mnemonic, "sub")) {
                op_rm_reg = 0x28;
                op_reg_rm = 0x2a;
                ext = 5;
            } else if (streq_ci(insn->mnemonic, "xor")) {
                op_rm_reg = 0x30;
                op_reg_rm = 0x32;
                ext = 6;
            } else {
                op_rm_reg = 0x38;
                op_reg_rm = 0x3a;
                ext = 7;
            }
        }
        if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG) {
            if (encode_reg_rm_pair(&ctx, op_rm_reg, a, b, 1) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && is_reg_or_mem(b)) {
            if (encode_reg_rm_pair(&ctx, op_reg_rm, a, b, 0) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_IMM) {
            if (insn->byte_op) {
                if (emit8(&ctx, 0x80) != 0 || modrm_sib_disp(&ctx, ext, a) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (emit8(&ctx, 0x81) != 0 || modrm_sib_disp(&ctx, ext, a) != 0 || emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported ALU form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "imul") || streq_ci(insn->mnemonic, "imulb") || streq_ci(insn->mnemonic, "imull")) {
        if (insn->op_count == 3 && a->kind == AS_X86_OP_REG && is_reg_or_mem(b) && c->kind == AS_X86_OP_IMM) {
            if ((int8_t)c->u.imm == c->u.imm) {
                if (emit8(&ctx, 0x6b) != 0 || modrm_sib_disp(&ctx, (uint8_t)(a->u.reg & 7), b) != 0 ||
                    emit8(&ctx, (uint8_t)c->u.imm) != 0) {
                    return -1;
                }
            } else {
                if (emit8(&ctx, 0x69) != 0 || modrm_sib_disp(&ctx, (uint8_t)(a->u.reg & 7), b) != 0 ||
                    emit32(&ctx, (uint32_t)c->u.imm) != 0) {
                    return -1;
                }
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && is_reg_or_mem(b)) {
            if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xaf) != 0 ||
                modrm_sib_disp(&ctx, (uint8_t)(a->u.reg & 7), b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && is_reg_or_mem(a)) {
            if (emit8(&ctx, insn->byte_op ? 0xf6 : 0xf7) != 0 || modrm_sib_disp(&ctx, AS_X86_REG_EBP, a) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported imul form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "mul") || streq_ci(insn->mnemonic, "mulb") || streq_ci(insn->mnemonic, "mull") ||
               streq_ci(insn->mnemonic, "div") || streq_ci(insn->mnemonic, "divb") || streq_ci(insn->mnemonic, "divl") ||
               streq_ci(insn->mnemonic, "idiv") || streq_ci(insn->mnemonic, "idivb") || streq_ci(insn->mnemonic, "idivl")) {
        as_x86_reg_t ext_reg;
        if (insn->op_count != 1 || !is_reg_or_mem(a)) {
            set_err(&ctx, "unsupported %s form", insn->mnemonic);
            return -1;
        }
        if (streq_ci(insn->mnemonic, "mul") || streq_ci(insn->mnemonic, "mulb") || streq_ci(insn->mnemonic, "mull"))
            ext_reg = AS_X86_REG_ESP;
        else if (streq_ci(insn->mnemonic, "div") || streq_ci(insn->mnemonic, "divb") || streq_ci(insn->mnemonic, "divl"))
            ext_reg = AS_X86_REG_ESI;
        else ext_reg = AS_X86_REG_EDI;
        if (emit8(&ctx, insn->byte_op ? 0xf6 : 0xf7) != 0 || modrm_sib_disp(&ctx, ext_reg, a) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsx") || streq_ci(insn->mnemonic, "movsxb") || streq_ci(insn->mnemonic, "movsxw")) {
        uint8_t op2 = streq_ci(insn->mnemonic, "movsxw") ? 0xbf : 0xbe;
        if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && is_reg_or_mem(b)) {
            if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, op2) != 0 ||
                modrm_sib_disp(&ctx, (uint8_t)(a->u.reg & 7), b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported movsx form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movzx") || streq_ci(insn->mnemonic, "movzxb") || streq_ci(insn->mnemonic, "movzxw")) {
        uint8_t op2 = streq_ci(insn->mnemonic, "movzxw") ? 0xb7 : 0xb6;
        if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && is_reg_or_mem(b)) {
            if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, op2) != 0 ||
                modrm_sib_disp(&ctx, (uint8_t)(a->u.reg & 7), b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported movzx form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "bt") || streq_ci(insn->mnemonic, "bts")) {
        uint8_t ext = streq_ci(insn->mnemonic, "bt") ? 4u : 5u;
        uint8_t op = streq_ci(insn->mnemonic, "bt") ? 0xa3u : 0xabu;
        if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, op) != 0 ||
                modrm_sib_disp(&ctx, (uint8_t)(b->u.reg & 7), a) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xba) != 0 ||
                modrm_sib_disp(&ctx, ext, a) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported bit-test form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "shl") || streq_ci(insn->mnemonic, "shr") || streq_ci(insn->mnemonic, "sar") ||
               streq_ci(insn->mnemonic, "ror") || streq_ci(insn->mnemonic, "rol") ||
               streq_ci(insn->mnemonic, "rcl") || streq_ci(insn->mnemonic, "rcr")) {
        uint8_t ext;
        if (streq_ci(insn->mnemonic, "rol")) {
            ext = 0;
        } else if (streq_ci(insn->mnemonic, "rcl")) {
            ext = 2;
        } else if (streq_ci(insn->mnemonic, "rcr")) {
            ext = 3;
        } else if (streq_ci(insn->mnemonic, "shl")) {
            ext = 4;
        } else if (streq_ci(insn->mnemonic, "shr")) {
            ext = 5;
        } else if (streq_ci(insn->mnemonic, "ror")) {
            ext = 1;
        } else {
            ext = 7;
        }

        if (insn->op_count == 1 && is_reg_or_mem(a)) {
            if (emit8(&ctx, insn->byte_op ? 0xd0 : 0xd1) != 0 || modrm_sib_disp(&ctx, ext, a) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, insn->byte_op ? 0xc0 : 0xc1) != 0 || modrm_sib_disp(&ctx, ext, a) != 0 ||
                emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG &&
                   ((b->u.reg & 7) == AS_X86_REG_ECX)) {
            if (emit8(&ctx, insn->byte_op ? 0xd2 : 0xd3) != 0 || modrm_sib_disp(&ctx, ext, a) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported shift form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movs")) {
        int bits;

        if (insn->op_count != 2 ||
            !mem_is_single_base_reg(a, AS_X86_REG_EDI) ||
            !mem_is_single_base_reg(b, AS_X86_REG_ESI)) {
            set_err(&ctx, "unsupported movs form");
            return -1;
        }
        bits = resolved_string_op_bits(insn, a, b);
        if (bits == 8) {
            if (emit8(&ctx, 0xa4) != 0) return -1;
        } else if (bits == 16) {
            if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0xa5) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0xa5) != 0) return -1;
        } else {
            set_err(&ctx, "unsupported movs form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsb")) {
        if (emit8(&ctx, 0xa4) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsd") || streq_ci(insn->mnemonic, "movsl") || streq_ci(insn->mnemonic, "movsw")) {
        if (emit8(&ctx, 0xa5) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosb")) {
        if (emit8(&ctx, 0xaa) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosd") || streq_ci(insn->mnemonic, "stosl") || streq_ci(insn->mnemonic, "stosw")) {
        if (emit8(&ctx, 0xab) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stos")) {
        if (emit8(&ctx, insn->byte_op ? 0xaa : 0xab) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsb")) {
        if (emit8(&ctx, 0xa6) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsd") || streq_ci(insn->mnemonic, "cmpsl") || streq_ci(insn->mnemonic, "cmpsw")) {
        if (emit8(&ctx, 0xa7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmps")) {
        if (insn->op_count == 2) {
            int bits;

            if (!mem_is_single_base_reg(a, AS_X86_REG_ESI) ||
                !mem_is_single_base_reg(b, AS_X86_REG_EDI)) {
                set_err(&ctx, "unsupported cmps form");
                return -1;
            }
            bits = resolved_string_op_bits(insn, a, b);
            if (bits == 8) {
                if (emit8(&ctx, 0xa6) != 0) return -1;
            } else if (bits == 16) {
                if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0xa7) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xa7) != 0) return -1;
            } else {
                set_err(&ctx, "unsupported cmps form");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xa6 : 0xa7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsb")) {
        if (emit8(&ctx, 0xac) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsd") || streq_ci(insn->mnemonic, "lodsl") || streq_ci(insn->mnemonic, "lodsw")) {
        if (emit8(&ctx, 0xad) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lods")) {
        if (insn->op_count == 2) {
            int bits;

            if (!reg_is_exact_low3(a, AS_X86_REG_EAX) ||
                !mem_is_single_base_reg(b, AS_X86_REG_ESI)) {
                set_err(&ctx, "unsupported lods form");
                return -1;
            }
            bits = mem_operand_bits(b);
            if (bits == 0) {
                bits = default_string_op_bits(insn);
            }
            if (bits == 8) {
                if (emit8(&ctx, 0xac) != 0) return -1;
            } else if (bits == 16) {
                if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0xad) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xad) != 0) return -1;
            } else {
                set_err(&ctx, "unsupported lods form");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xac : 0xad) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasb")) {
        if (emit8(&ctx, 0xae) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasd") || streq_ci(insn->mnemonic, "scasl") || streq_ci(insn->mnemonic, "scasw")) {
        if (emit8(&ctx, 0xaf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scas")) {
        if (insn->op_count == 2) {
            int bits;

            if (!reg_is_exact_low3(a, AS_X86_REG_EAX) ||
                !mem_is_single_base_reg(b, AS_X86_REG_EDI)) {
                set_err(&ctx, "unsupported scas form");
                return -1;
            }
            bits = mem_operand_bits(b);
            if (bits == 0) {
                bits = default_string_op_bits(insn);
            }
            if (bits == 8) {
                if (emit8(&ctx, 0xae) != 0) return -1;
            } else if (bits == 16) {
                if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0xaf) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xaf) != 0) return -1;
            } else {
                set_err(&ctx, "unsupported scas form");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xae : 0xaf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ins")) {
        int bits = insn->byte_op ? 8 : 0;

        if (insn->op_count != 2 ||
            !mem_is_single_base_reg(a, AS_X86_REG_EDI) ||
            !is_dx_port_operand(b)) {
            set_err(&ctx, "unsupported ins form");
            return -1;
        }
        if (bits == 0) {
            bits = (int)a->u.mem.size_bits;
        }
        if (bits == 8) {
            if (emit8(&ctx, 0x6c) != 0) return -1;
        } else if (bits == 16) {
            if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x6d) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0x6d) != 0) return -1;
        } else {
            set_err(&ctx, "unsupported ins form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insb")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_EDI) &&
              is_dx_port_operand(b))) {
            set_err(&ctx, "unsupported insb form");
            return -1;
        }
        if (emit8(&ctx, 0x6c) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insw")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_EDI) &&
              is_dx_port_operand(b))) {
            set_err(&ctx, "unsupported insw form");
            return -1;
        }
        if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x6d) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insd") || streq_ci(insn->mnemonic, "insl")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_EDI) &&
              is_dx_port_operand(b))) {
            set_err(&ctx, "unsupported insd form");
            return -1;
        }
        if (emit8(&ctx, 0x6d) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outs")) {
        int bits = insn->byte_op ? 8 : 0;

        if (insn->op_count != 2 ||
            !is_dx_port_operand(a) ||
            !mem_is_single_base_reg(b, AS_X86_REG_ESI)) {
            set_err(&ctx, "unsupported outs form");
            return -1;
        }
        if (bits == 0) {
            bits = (int)b->u.mem.size_bits;
        }
        if (bits == 8) {
            if (emit8(&ctx, 0x6e) != 0) return -1;
        } else if (bits == 16) {
            if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x6f) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0x6f) != 0) return -1;
        } else {
            set_err(&ctx, "unsupported outs form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsb")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_ESI))) {
            set_err(&ctx, "unsupported outsb form");
            return -1;
        }
        if (emit8(&ctx, 0x6e) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsw")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_ESI))) {
            set_err(&ctx, "unsupported outsw form");
            return -1;
        }
        if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x6f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsd") || streq_ci(insn->mnemonic, "outsl")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_ESI))) {
            set_err(&ctx, "unsupported outsd form");
            return -1;
        }
        if (emit8(&ctx, 0x6f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "jmp")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            if (emit8(&ctx, 0xe9) != 0) {
                return -1;
            }
            if (insn->operand_size_override) {
                if (emit8(&ctx, (uint8_t)(a->u.rel & 0xff)) != 0 || emit8(&ctx, (uint8_t)((a->u.rel >> 8) & 0xff)) != 0) {
                    return -1;
                }
            } else if (emit32(&ctx, (uint32_t)a->u.rel) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && is_reg_or_mem(a)) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp(&ctx, 4, a) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported jmp form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "call")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            if (emit8(&ctx, 0xe8) != 0) {
                return -1;
            }
            if (insn->operand_size_override) {
                if (emit8(&ctx, (uint8_t)(a->u.rel & 0xff)) != 0 || emit8(&ctx, (uint8_t)((a->u.rel >> 8) & 0xff)) != 0) {
                    return -1;
                }
            } else if (emit32(&ctx, (uint32_t)a->u.rel) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && is_reg_or_mem(a)) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp(&ctx, 2, a) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported call form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "leave")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xc9) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "enter")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xc8) != 0 || emit8(&ctx, (uint8_t)(a->u.imm & 0xff)) != 0 ||
                emit8(&ctx, (uint8_t)((a->u.imm >> 8) & 0xff)) != 0 || emit8(&ctx, (uint8_t)(b->u.imm & 0xff)) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported enter form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "clc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xf8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xf9) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cli")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xfa) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sti")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xfb) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cld")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xfc) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "std")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xfd) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xf5) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "daa")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x27) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "das")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x2f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "aaa")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x37) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "aas")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x3f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "aam")) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_IMM || emit8(&ctx, 0xd4) != 0 ||
            emit8(&ctx, (uint8_t)a->u.imm) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "aad")) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_IMM || emit8(&ctx, 0xd5) != 0 ||
            emit8(&ctx, (uint8_t)a->u.imm) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "syscall")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x05) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sysenter")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x34) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sysexit")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x35) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "wrmsr")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x30) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "rdtsc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x31) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "rdmsr")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x32) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "rdpmc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x33) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "getsec")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x37) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "bswap")) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_REG) {
            set_err(&ctx, "unsupported bswap form");
            return -1;
        }
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, (uint8_t)(0xc8 | reg_low3(a->u.reg))) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "inc") || streq_ci(insn->mnemonic, "incb") || streq_ci(insn->mnemonic, "incl") ||
               streq_ci(insn->mnemonic, "dec") || streq_ci(insn->mnemonic, "decb") || streq_ci(insn->mnemonic, "decl")) {
        int is_inc = streq_ci(insn->mnemonic, "inc") || streq_ci(insn->mnemonic, "incb") || streq_ci(insn->mnemonic, "incl");
        uint8_t ext = is_inc ? 0u : 1u;
        uint8_t short_op = (uint8_t)(((is_inc ? 0x40 : 0x48)) | (reg_low3(a->u.reg)));
        uint8_t group_op = insn->byte_op ? 0xfe : 0xff;

        if (insn->op_count != 1 || (a->kind != AS_X86_OP_REG && a->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported %s form", insn->mnemonic);
            return -1;
        }
        if (a->kind == AS_X86_OP_REG && !insn->byte_op) {
            if (emit8(&ctx, short_op) != 0) {
                return -1;
            }
        } else if (emit8(&ctx, group_op) != 0 || modrm_sib_disp(&ctx, ext, a) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "not") || streq_ci(insn->mnemonic, "notb") || streq_ci(insn->mnemonic, "notl") ||
               streq_ci(insn->mnemonic, "neg") || streq_ci(insn->mnemonic, "negb") || streq_ci(insn->mnemonic, "negl")) {
        uint8_t ext = (streq_ci(insn->mnemonic, "not") || streq_ci(insn->mnemonic, "notb") || streq_ci(insn->mnemonic, "notl")) ? 2u : 3u;
        uint8_t group_op = insn->byte_op ? 0xf6 : 0xf7;

        if (insn->op_count != 1 || (a->kind != AS_X86_OP_REG && a->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported %s form", insn->mnemonic);
            return -1;
        }
        if (emit8(&ctx, group_op) != 0 || modrm_sib_disp(&ctx, ext, a) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "pusha") || streq_ci(insn->mnemonic, "pushal") || streq_ci(insn->mnemonic, "pushaw")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x60) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "popa") || streq_ci(insn->mnemonic, "popal") || streq_ci(insn->mnemonic, "popaw")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x61) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "bound")) {
        if (insn->op_count != 2) {
            set_err(&ctx, "unsupported bound form");
            return -1;
        }
        if (a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0x62) != 0 || modrm_sib_disp(&ctx, a->u.reg, b) != 0) {
                return -1;
            }
        } else if (a->kind == AS_X86_OP_MEM && b->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, 0x62) != 0 || modrm_sib_disp(&ctx, b->u.reg, a) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported bound form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "les") || streq_ci(insn->mnemonic, "lds")) {
        uint8_t opcode = streq_ci(insn->mnemonic, "les") ? 0xc4 : 0xc5;

        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || b->kind != AS_X86_OP_MEM) {
            set_err(&ctx, "unsupported %s form", insn->mnemonic);
            return -1;
        }
        if (emit8(&ctx, opcode) != 0 || modrm_sib_disp(&ctx, a->u.reg, b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lss") || streq_ci(insn->mnemonic, "lfs") || streq_ci(insn->mnemonic, "lgs")) {
        uint8_t opcode2 = streq_ci(insn->mnemonic, "lss") ? 0xb2 : (streq_ci(insn->mnemonic, "lfs") ? 0xb4 : 0xb5);

        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || b->kind != AS_X86_OP_MEM) {
            set_err(&ctx, "unsupported %s form", insn->mnemonic);
            return -1;
        }
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, opcode2) != 0 || modrm_sib_disp(&ctx, a->u.reg, b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "arpl")) {
        if (insn->op_count != 2 || (a->kind != AS_X86_OP_REG && a->kind != AS_X86_OP_MEM) || b->kind != AS_X86_OP_REG) {
            set_err(&ctx, "unsupported arpl form");
            return -1;
        }
        if (emit8(&ctx, 0x63) != 0 || modrm_sib_disp(&ctx, b->u.reg, a) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "test") || streq_ci(insn->mnemonic, "testb") || streq_ci(insn->mnemonic, "testl")) {
        if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, insn->byte_op ? 0x84 : 0x85) != 0 || modrm_sib_disp(&ctx, b->u.reg, a) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_IMM) {
            if (insn->byte_op) {
                if (emit8(&ctx, 0xf6) != 0 || modrm_sib_disp(&ctx, 0, a) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (emit8(&ctx, 0xf7) != 0 || modrm_sib_disp(&ctx, 0, a) != 0 || emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported test form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cbtw") || streq_ci(insn->mnemonic, "cbw") || streq_ci(insn->mnemonic, "cwtl") ||
               streq_ci(insn->mnemonic, "cwde")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x98) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cwtd") || streq_ci(insn->mnemonic, "cwd") || streq_ci(insn->mnemonic, "cltd") ||
               streq_ci(insn->mnemonic, "cdq")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x99) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lcall")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0x9a) != 0) {
                return -1;
            }
            if (insn->operand_size_override) {
                if (emit8(&ctx, (uint8_t)(a->u.imm & 0xff)) != 0 || emit8(&ctx, (uint8_t)((a->u.imm >> 8) & 0xff)) != 0) {
                    return -1;
                }
            } else if (emit32(&ctx, (uint32_t)a->u.imm) != 0) {
                return -1;
            }
            if (emit8(&ctx, (uint8_t)(b->u.imm & 0xff)) != 0 || emit8(&ctx, (uint8_t)((b->u.imm >> 8) & 0xff)) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp(&ctx, 3, a) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported lcall form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ljmp")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xea) != 0) {
                return -1;
            }
            if (insn->operand_size_override) {
                if (emit8(&ctx, (uint8_t)(a->u.imm & 0xff)) != 0 || emit8(&ctx, (uint8_t)((a->u.imm >> 8) & 0xff)) != 0) {
                    return -1;
                }
            } else if (emit32(&ctx, (uint32_t)a->u.imm) != 0) {
                return -1;
            }
            if (emit8(&ctx, (uint8_t)(b->u.imm & 0xff)) != 0 || emit8(&ctx, (uint8_t)((b->u.imm >> 8) & 0xff)) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp(&ctx, 5, a) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported ljmp form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "fwait") || streq_ci(insn->mnemonic, "wait")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x9b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sahf")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x9e) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lahf")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x9f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xabort")) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_IMM || emit8(&ctx, 0xc6) != 0 || emit8(&ctx, 0xf8) != 0 ||
            emit8(&ctx, (uint8_t)a->u.imm) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xbegin")) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_REL || emit8(&ctx, 0xc7) != 0 || emit8(&ctx, 0xf8) != 0 ||
            emit32(&ctx, (uint32_t)a->u.rel) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "int3")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xcc) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "into")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xce) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "salc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xd6) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "iret") || streq_ci(insn->mnemonic, "iretw")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xcf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xlat") || streq_ci(insn->mnemonic, "xlatb")) {
        if (insn->op_count != 1 || emit8(&ctx, 0xd7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lret") || streq_ci(insn->mnemonic, "retf") || streq_ci(insn->mnemonic, "lretw")) {
        if (insn->op_count == 0) {
            if (emit8(&ctx, 0xcb) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xca) != 0 || emit8(&ctx, (uint8_t)(a->u.imm & 0xff)) != 0 ||
                emit8(&ctx, (uint8_t)((a->u.imm >> 8) & 0xff)) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported lret form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ret")) {
        if (emit8(&ctx, 0xc3) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "loop") || streq_ci(insn->mnemonic, "loope") ||
               streq_ci(insn->mnemonic, "loopz") || streq_ci(insn->mnemonic, "loopne") ||
               streq_ci(insn->mnemonic, "loopnz") || streq_ci(insn->mnemonic, "jecxz")) {
        uint8_t opcode;
        int32_t rel;

        if (!(insn->op_count == 1 && a->kind == AS_X86_OP_REL)) {
            set_err(&ctx, "unsupported loop form");
            return -1;
        }
        rel = a->u.rel;
        if (rel < -128 || rel > 127) {
            set_err(&ctx, "loop target out of range");
            return -1;
        }
        if (streq_ci(insn->mnemonic, "loop")) opcode = 0xe2;
        else if (streq_ci(insn->mnemonic, "loope") || streq_ci(insn->mnemonic, "loopz")) opcode = 0xe1;
        else if (streq_ci(insn->mnemonic, "loopne") || streq_ci(insn->mnemonic, "loopnz")) opcode = 0xe0;
        else opcode = 0xe3;
        if (emit8(&ctx, opcode) != 0 || emit8(&ctx, (uint8_t)rel) != 0) {
            return -1;
        }
    } else if (encode_jcc_rel32(&ctx, insn->mnemonic, (insn->op_count == 1 && a->kind == AS_X86_OP_REL) ? a->u.rel : 0) == 0) {
        if (!(insn->op_count == 1 && a->kind == AS_X86_OP_REL)) {
            set_err(&ctx, "unsupported jcc form");
            return -1;
        }
    } else if (strncmp(insn->mnemonic, "set", 3) == 0) {
        static const struct {
            const char *name;
            uint8_t cc;
        } ccmap[] = {
            {"seto", 0x0},   {"setno", 0x1}, {"setb", 0x2},  {"setnae", 0x2}, {"setc", 0x2},   {"setnb", 0x3},
            {"setae", 0x3},  {"setnc", 0x3}, {"sete", 0x4},  {"setz", 0x4},   {"setne", 0x5},  {"setnz", 0x5},
            {"setbe", 0x6},  {"setna", 0x6}, {"seta", 0x7},  {"setnbe", 0x7}, {"sets", 0x8},   {"setns", 0x9},
            {"setp", 0xa},   {"setpe", 0xa}, {"setnp", 0xb}, {"setpo", 0xb},  {"setl", 0xc},   {"setnge", 0xc},
            {"setge", 0xd},  {"setnl", 0xd}, {"setle", 0xe}, {"setng", 0xe},  {"setg", 0xf},   {"setnle", 0xf},
        };
        size_t i;
        int found = 0;
        uint8_t cc = 0;

        if (insn->op_count != 1 || (a->kind != AS_X86_OP_REG && a->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported setcc form");
            return -1;
        }
        for (i = 0; i < sizeof(ccmap) / sizeof(ccmap[0]); ++i) {
            if (streq_ci(insn->mnemonic, ccmap[i].name)) {
                cc = ccmap[i].cc;
                found = 1;
                break;
            }
        }
        if (!found) {
            set_err(&ctx, "unsupported i386 setcc mnemonic: %s", insn->mnemonic);
            return -1;
        }
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, (uint8_t)(0x90 | cc)) != 0 || modrm_sib_disp(&ctx, 0, a) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "push")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, (uint8_t)(0x50 | (a->u.reg & 7))) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp(&ctx, 6, a) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0x68) != 0 || emit32(&ctx, (uint32_t)a->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported push form (op_count=%zu kind=%d)", insn->op_count,
                    a != NULL ? (int)a->kind : -1);
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "pop")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, (uint8_t)(0x58 | (a->u.reg & 7))) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0x8f) != 0 || modrm_sib_disp(&ctx, 0, a) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported pop form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "clc")) {
        if (emit8(&ctx, 0xf8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stc")) {
        if (emit8(&ctx, 0xf9) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cli")) {
        if (emit8(&ctx, 0xfa) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sti")) {
        if (emit8(&ctx, 0xfb) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cld")) {
        if (emit8(&ctx, 0xfc) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "std")) {
        if (emit8(&ctx, 0xfd) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "pushf")) {
        if (emit8(&ctx, 0x9c) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "popf")) {
        if (emit8(&ctx, 0x9d) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "in")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM && (a->u.reg & 7) == AS_X86_REG_EAX) {
            if (emit8(&ctx, insn->byte_op ? 0xe4 : 0xe5) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_REG &&
                   (a->u.reg & 7) == AS_X86_REG_EAX && (b->u.reg & 7) == AS_X86_REG_EDX) {
            if (emit8(&ctx, insn->byte_op ? 0xec : 0xed) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported in form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "out")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_REG && (b->u.reg & 7) == AS_X86_REG_EAX) {
            if (emit8(&ctx, insn->byte_op ? 0xe6 : 0xe7) != 0 || emit8(&ctx, (uint8_t)a->u.imm) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_REG &&
                   (a->u.reg & 7) == AS_X86_REG_EDX && (b->u.reg & 7) == AS_X86_REG_EAX) {
            if (emit8(&ctx, insn->byte_op ? 0xee : 0xef) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported out form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sldt")) {
        if (insn->op_count != 1 || !is_reg_or_mem(a) || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x00) != 0 ||
            modrm_sib_disp(&ctx, 0, a) != 0) {
            set_err(&ctx, "unsupported sldt form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sgdt") || streq_ci(insn->mnemonic, "sgdtl") || streq_ci(insn->mnemonic, "sgdtd") ||
               streq_ci(insn->mnemonic, "sidt") || streq_ci(insn->mnemonic, "sidtl") || streq_ci(insn->mnemonic, "sidtd") ||
               streq_ci(insn->mnemonic, "lgdt") || streq_ci(insn->mnemonic, "lgdtl") || streq_ci(insn->mnemonic, "lgdtd") ||
               streq_ci(insn->mnemonic, "lidt") || streq_ci(insn->mnemonic, "lidtl") || streq_ci(insn->mnemonic, "lidtd") ||
               streq_ci(insn->mnemonic, "smsw") || streq_ci(insn->mnemonic, "lmsw") ||
               streq_ci(insn->mnemonic, "invlpg")) {
        uint8_t ext;
        if (insn->op_count != 1 || !is_reg_or_mem(a)) {
            set_err(&ctx, "unsupported %s form", insn->mnemonic);
            return -1;
        }
        if (streq_ci(insn->mnemonic, "sgdt") || streq_ci(insn->mnemonic, "sgdtl") || streq_ci(insn->mnemonic, "sgdtd")) ext = 0;
        else if (streq_ci(insn->mnemonic, "sidt") || streq_ci(insn->mnemonic, "sidtl") || streq_ci(insn->mnemonic, "sidtd")) ext = 1;
        else if (streq_ci(insn->mnemonic, "lgdt") || streq_ci(insn->mnemonic, "lgdtl") || streq_ci(insn->mnemonic, "lgdtd")) ext = 2;
        else if (streq_ci(insn->mnemonic, "lidt") || streq_ci(insn->mnemonic, "lidtl") || streq_ci(insn->mnemonic, "lidtd")) ext = 3;
        else if (streq_ci(insn->mnemonic, "smsw")) ext = 4;
        else if (streq_ci(insn->mnemonic, "lmsw")) ext = 6;
        else ext = 7;
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || modrm_sib_disp(&ctx, ext, a) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "enclv")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || emit8(&ctx, 0xc0) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "monitor")) {
        if (insn->op_count != 3 || a->kind != AS_X86_OP_REG || b->kind != AS_X86_OP_REG || c->kind != AS_X86_OP_REG ||
            emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || emit8(&ctx, 0xc8) != 0) {
            set_err(&ctx, "unsupported monitor form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xgetbv")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || emit8(&ctx, 0xd0) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "vmrun")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || emit8(&ctx, 0xd8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "serialize")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || emit8(&ctx, 0xe8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lar") || streq_ci(insn->mnemonic, "lsl")) {
        uint8_t op2 = streq_ci(insn->mnemonic, "lar") ? 0x02 : 0x03;
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || !is_reg_or_mem(b) ||
            emit8(&ctx, 0x0f) != 0 || emit8(&ctx, op2) != 0 || modrm_sib_disp(&ctx, a->u.reg, b) != 0) {
            set_err(&ctx, "unsupported %s form", insn->mnemonic);
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmovo") || streq_ci(insn->mnemonic, "cmovno") ||
               streq_ci(insn->mnemonic, "cmovb") || streq_ci(insn->mnemonic, "cmovae") ||
               streq_ci(insn->mnemonic, "cmove") || streq_ci(insn->mnemonic, "cmovne") ||
               streq_ci(insn->mnemonic, "cmovbe") || streq_ci(insn->mnemonic, "cmova") ||
               streq_ci(insn->mnemonic, "cmovs") || streq_ci(insn->mnemonic, "cmovns") ||
               streq_ci(insn->mnemonic, "cmovp") || streq_ci(insn->mnemonic, "cmovnp") ||
               streq_ci(insn->mnemonic, "cmovl") || streq_ci(insn->mnemonic, "cmovge") ||
               streq_ci(insn->mnemonic, "cmovle") || streq_ci(insn->mnemonic, "cmovg")) {
        uint8_t op2;
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || !is_reg_or_mem(b)) {
            set_err(&ctx, "unsupported %s form", insn->mnemonic);
            return -1;
        }
        if (streq_ci(insn->mnemonic, "cmovo")) op2 = 0x40;
        else if (streq_ci(insn->mnemonic, "cmovno")) op2 = 0x41;
        else if (streq_ci(insn->mnemonic, "cmovb")) op2 = 0x42;
        else if (streq_ci(insn->mnemonic, "cmovae")) op2 = 0x43;
        else if (streq_ci(insn->mnemonic, "cmove")) op2 = 0x44;
        else if (streq_ci(insn->mnemonic, "cmovne")) op2 = 0x45;
        else if (streq_ci(insn->mnemonic, "cmovbe")) op2 = 0x46;
        else if (streq_ci(insn->mnemonic, "cmova")) op2 = 0x47;
        else if (streq_ci(insn->mnemonic, "cmovs")) op2 = 0x48;
        else if (streq_ci(insn->mnemonic, "cmovns")) op2 = 0x49;
        else if (streq_ci(insn->mnemonic, "cmovp")) op2 = 0x4a;
        else if (streq_ci(insn->mnemonic, "cmovnp")) op2 = 0x4b;
        else if (streq_ci(insn->mnemonic, "cmovl")) op2 = 0x4c;
        else if (streq_ci(insn->mnemonic, "cmovge")) op2 = 0x4d;
        else if (streq_ci(insn->mnemonic, "cmovle")) op2 = 0x4e;
        else op2 = 0x4f;
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, op2) != 0 || modrm_sib_disp(&ctx, a->u.reg, b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "clts")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x06) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sysret")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x07) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "invd")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x08) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "wbinvd")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x09) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "femms")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x0e) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "nop")) {
        if (emit8(&ctx, 0x90) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "pause")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xf3) != 0 || emit8(&ctx, 0x90) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "mfence")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xae) != 0 || emit8(&ctx, 0xf0) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lfence")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xae) != 0 || emit8(&ctx, 0xe8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sfence")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xae) != 0 || emit8(&ctx, 0xf8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "emms")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x77) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ud2")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x0b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "hlt")) {
        if (emit8(&ctx, 0xf4) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "int")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xcd) != 0 || emit8(&ctx, (uint8_t)a->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported int form");
            return -1;
        }
    } else {
        set_err(&ctx, "unsupported mnemonic: %s", insn->mnemonic != NULL ? insn->mnemonic : "<null>");
        return -1;
    }

    if (out_len != NULL) {
        *out_len = ctx.at;
    }
    return 0;
}

int as_x86_encode_x86_64(const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz) {
    enc_ctx_t ctx;
    const as_x86_operand_t *a;
    const as_x86_operand_t *b;
    const as_x86_operand_t *c;
    size_t rex_pos;
    uint8_t rex_w = 0;
    uint8_t rex_r = 0;
    uint8_t rex_x = 0;
    uint8_t rex_b = 0;
    uint8_t rex;
    int force_rex = 0;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.out_cap = out_cap;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;

    if (emit_prefixes(&ctx, insn) != 0) {
        return -1;
    }

    if (insn->rex_w) {
        rex_w = 1;
    }

    /* Reserve one byte for REX prefix; it will be removed if unused. */
    rex_pos = ctx.at;
    if (emit8(&ctx, 0x40) != 0) {
        return -1;
    }

    a = insn->op_count > 0 ? &insn->ops[0] : NULL;
    b = insn->op_count > 1 ? &insn->ops[1] : NULL;
    c = insn->op_count > 2 ? &insn->ops[2] : NULL;

    if (streq_ci(insn->mnemonic, "movabs")) {
        rex_w = 1;
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || b->kind != AS_X86_OP_IMM) {
            set_err(&ctx, "unsupported x86_64 movabs form");
            return -1;
        }
        rex_b |= reg_ext(a->u.reg);
        if (emit8(&ctx, (uint8_t)(0xb8u | reg_low3(a->u.reg))) != 0 || emit64(&ctx, (uint64_t)b->u.imm) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "mov")) {
        if (insn->byte_op) {
            if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM) {
                rex_b |= reg_ext(a->u.reg);
                if (needs_rex_low8(a->u.reg)) {
                    force_rex = 1;
                }
                if (emit8(&ctx, (uint8_t)(0xb0u | reg_low3(a->u.reg))) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && (b->kind == AS_X86_OP_REG || b->kind == AS_X86_OP_MEM)) {
                if (needs_rex_low8(a->u.reg)) {
                    force_rex = 1;
                }
                if (b->kind == AS_X86_OP_REG && needs_rex_low8(b->u.reg)) {
                    force_rex = 1;
                }
                if (emit8(&ctx, 0x8a) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
                if (needs_rex_low8(b->u.reg)) {
                    force_rex = 1;
                }
                if (a->kind == AS_X86_OP_REG && needs_rex_low8(a->u.reg)) {
                    force_rex = 1;
                }
                if (emit8(&ctx, 0x88) != 0 || modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && a->kind == AS_X86_OP_MEM && b->kind == AS_X86_OP_IMM) {
                if (emit8(&ctx, 0xc6) != 0 ||
                    modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0 ||
                    emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else {
                set_err(&ctx, "unsupported x86_64 movb form");
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM) {
            int use_imm64 = 0;
            rex_b |= reg_ext(a->u.reg);
            if (rex_w) {
                if (b->u.imm < (int64_t)INT32_MIN || b->u.imm > (int64_t)INT32_MAX) {
                    use_imm64 = 1;
                }
            }
            if (use_imm64) {
                if (emit8(&ctx, (uint8_t)(0xb8u | reg_low3(a->u.reg))) != 0 ||
                    emit64(&ctx, (uint64_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (rex_w) {
                if (emit8(&ctx, 0xc7) != 0 || emit8(&ctx, (uint8_t)(0xc0u | reg_low3(a->u.reg))) != 0 ||
                    emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                    return -1;
                }
            } else {
                if (emit8(&ctx, (uint8_t)(0xb8u | reg_low3(a->u.reg))) != 0 ||
                    emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                    return -1;
                }
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && (b->kind == AS_X86_OP_REG || b->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0x8b) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, 0x89) != 0 || modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_MEM && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xc7) != 0 ||
                modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0 ||
                emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 mov form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsxd")) {
        rex_w = 1;
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG ||
            (b->kind != AS_X86_OP_REG && b->kind != AS_X86_OP_MEM) ||
            emit8(&ctx, 0x63) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
            set_err(&ctx, "unsupported movsxd form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsx") || streq_ci(insn->mnemonic, "movsxb") || streq_ci(insn->mnemonic, "movsxw")) {
        uint8_t op2 = streq_ci(insn->mnemonic, "movsxw") ? 0xbf : 0xbe;
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || (b->kind != AS_X86_OP_REG && b->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported movsx form");
            return -1;
        }
        if (b->kind == AS_X86_OP_REG && needs_rex_low8(b->u.reg) && op2 == 0xbe) {
            force_rex = 1;
        }
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, op2) != 0 ||
            modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movzx") || streq_ci(insn->mnemonic, "movzxb") || streq_ci(insn->mnemonic, "movzxw")) {
        uint8_t op2 = streq_ci(insn->mnemonic, "movzxw") ? 0xb7 : 0xb6;
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG ||
            (b->kind != AS_X86_OP_REG && b->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported movzx form");
            return -1;
        }
        if (b->kind == AS_X86_OP_REG && needs_rex_low8(b->u.reg) && op2 == 0xb6) {
            force_rex = 1;
        }
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, op2) != 0 ||
            modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "imul")) {
        if (insn->op_count == 3 && a->kind == AS_X86_OP_REG &&
            (b->kind == AS_X86_OP_REG || b->kind == AS_X86_OP_MEM) && c->kind == AS_X86_OP_IMM) {
            if ((int8_t)c->u.imm == c->u.imm) {
                if (emit8(&ctx, 0x6b) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0 ||
                    emit8(&ctx, (uint8_t)c->u.imm) != 0) {
                    return -1;
                }
            } else {
                if (emit8(&ctx, 0x69) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0 ||
                    emit32(&ctx, (uint32_t)c->u.imm) != 0) {
                    return -1;
                }
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG &&
                   (b->kind == AS_X86_OP_REG || b->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xaf) != 0 ||
                modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0xf7) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RBP, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 imul form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "mul") || streq_ci(insn->mnemonic, "div") || streq_ci(insn->mnemonic, "idiv")) {
        as_x86_reg_t ext_reg;
        if (insn->op_count != 1 || (a->kind != AS_X86_OP_REG && a->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported x86_64 %s form", insn->mnemonic);
            return -1;
        }
        if (streq_ci(insn->mnemonic, "mul")) ext_reg = AS_X86_REG_RSP;      /* /4 */
        else if (streq_ci(insn->mnemonic, "div")) ext_reg = AS_X86_REG_RSI; /* /6 */
        else ext_reg = AS_X86_REG_RDI;                                       /* /7 */
        if (emit8(&ctx, 0xf7) != 0 || modrm_sib_disp64(&ctx, ext_reg, a, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lea")) {
        rex_w = 1;
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || b->kind != AS_X86_OP_MEM ||
            emit8(&ctx, 0x8d) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
            set_err(&ctx, "unsupported x86_64 lea form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xchg")) {
        rex_w = 1;
        if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, 0x87) != 0 || modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 xchg form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xadd")) {
        rex_w = insn->byte_op ? 0 : 1;
        if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, insn->byte_op ? 0xc0 : 0xc1) != 0 ||
                modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 xadd form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "add") || streq_ci(insn->mnemonic, "adc") || streq_ci(insn->mnemonic, "sbb") ||
               streq_ci(insn->mnemonic, "sub") || streq_ci(insn->mnemonic, "and") ||
               streq_ci(insn->mnemonic, "or") || streq_ci(insn->mnemonic, "xor") || streq_ci(insn->mnemonic, "cmp")) {
        uint8_t op_rm_reg;
        uint8_t op_reg_rm;
        uint8_t ext;

        rex_w = insn->byte_op ? 0 : 1;
        if (streq_ci(insn->mnemonic, "add")) {
            op_rm_reg = 0x01;
            op_reg_rm = 0x03;
            ext = 0;
        } else if (streq_ci(insn->mnemonic, "adc")) {
            op_rm_reg = 0x11;
            op_reg_rm = 0x13;
            ext = 2;
        } else if (streq_ci(insn->mnemonic, "sbb")) {
            op_rm_reg = 0x19;
            op_reg_rm = 0x1b;
            ext = 3;
        } else if (streq_ci(insn->mnemonic, "or")) {
            op_rm_reg = 0x09;
            op_reg_rm = 0x0b;
            ext = 1;
        } else if (streq_ci(insn->mnemonic, "and")) {
            op_rm_reg = 0x21;
            op_reg_rm = 0x23;
            ext = 4;
        } else if (streq_ci(insn->mnemonic, "sub")) {
            op_rm_reg = 0x29;
            op_reg_rm = 0x2b;
            ext = 5;
        } else if (streq_ci(insn->mnemonic, "xor")) {
            op_rm_reg = 0x31;
            op_reg_rm = 0x33;
            ext = 6;
        } else {
            op_rm_reg = 0x39;
            op_reg_rm = 0x3b;
            ext = 7;
        }
        if (insn->byte_op) {
            if (streq_ci(insn->mnemonic, "add")) {
                op_rm_reg = 0x00;
                op_reg_rm = 0x02;
                ext = 0;
            } else if (streq_ci(insn->mnemonic, "or")) {
                op_rm_reg = 0x08;
                op_reg_rm = 0x0a;
                ext = 1;
            } else if (streq_ci(insn->mnemonic, "and")) {
                op_rm_reg = 0x20;
                op_reg_rm = 0x22;
                ext = 4;
            } else if (streq_ci(insn->mnemonic, "sub")) {
                op_rm_reg = 0x28;
                op_reg_rm = 0x2a;
                ext = 5;
            } else if (streq_ci(insn->mnemonic, "xor")) {
                op_rm_reg = 0x30;
                op_reg_rm = 0x32;
                ext = 6;
            } else {
                op_rm_reg = 0x38;
                op_reg_rm = 0x3a;
                ext = 7;
            }
        }

        if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
            if (insn->byte_op && needs_rex_low8(b->u.reg)) {
                force_rex = 1;
            }
            if (insn->byte_op && a->kind == AS_X86_OP_REG && needs_rex_low8(a->u.reg)) {
                force_rex = 1;
            }
            if (emit8(&ctx, op_rm_reg) != 0 || modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && (b->kind == AS_X86_OP_REG || b->kind == AS_X86_OP_MEM)) {
            if (insn->byte_op && needs_rex_low8(a->u.reg)) {
                force_rex = 1;
            }
            if (insn->byte_op && b->kind == AS_X86_OP_REG && needs_rex_low8(b->u.reg)) {
                force_rex = 1;
            }
            if (emit8(&ctx, op_reg_rm) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_IMM) {
            if (insn->byte_op) {
                if (emit8(&ctx, 0x80) != 0 || modrm_sib_disp64(&ctx, (as_x86_reg_t)ext, a, &rex_r, &rex_x, &rex_b) != 0 ||
                    emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (emit8(&ctx, 0x81) != 0 || modrm_sib_disp64(&ctx, (as_x86_reg_t)ext, a, &rex_r, &rex_x, &rex_b) != 0 ||
                       emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 ALU form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "test")) {
        rex_w = insn->byte_op ? 0 : 1;
        if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
            if (insn->byte_op && needs_rex_low8(b->u.reg)) {
                force_rex = 1;
            }
            if (insn->byte_op && a->kind == AS_X86_OP_REG && needs_rex_low8(a->u.reg)) {
                force_rex = 1;
            }
            if (emit8(&ctx, insn->byte_op ? 0x84 : 0x85) != 0 ||
                modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_IMM) {
            if (insn->byte_op) {
                if (emit8(&ctx, 0xf6) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0 ||
                    emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (emit8(&ctx, 0xf7) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0 ||
                       emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 test form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "shl") || streq_ci(insn->mnemonic, "shr") || streq_ci(insn->mnemonic, "sar") ||
               streq_ci(insn->mnemonic, "rol") || streq_ci(insn->mnemonic, "ror") ||
               streq_ci(insn->mnemonic, "rcl") || streq_ci(insn->mnemonic, "rcr")) {
        uint8_t ext;
        rex_w = insn->byte_op ? 0 : 1;
        if (streq_ci(insn->mnemonic, "rol")) {
            ext = 0;
        } else if (streq_ci(insn->mnemonic, "rcl")) {
            ext = 2;
        } else if (streq_ci(insn->mnemonic, "rcr")) {
            ext = 3;
        } else if (streq_ci(insn->mnemonic, "ror")) {
            ext = 1;
        } else if (streq_ci(insn->mnemonic, "shl")) {
            ext = 4;
        } else if (streq_ci(insn->mnemonic, "shr")) {
            ext = 5;
        } else {
            ext = 7;
        }
        if (insn->op_count == 1 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM)) {
            if (insn->byte_op && a->kind == AS_X86_OP_REG && needs_rex_low8(a->u.reg)) {
                force_rex = 1;
            }
            if (emit8(&ctx, insn->byte_op ? 0xd0 : 0xd1) != 0 ||
                modrm_sib_disp64(&ctx, (as_x86_reg_t)ext, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_IMM) {
            if (insn->byte_op && a->kind == AS_X86_OP_REG && needs_rex_low8(a->u.reg)) {
                force_rex = 1;
            }
            if (emit8(&ctx, insn->byte_op ? 0xc0 : 0xc1) != 0 ||
                modrm_sib_disp64(&ctx, (as_x86_reg_t)ext, a, &rex_r, &rex_x, &rex_b) != 0 ||
                emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG &&
                   reg_low3(b->u.reg) == AS_X86_REG_RCX) {
            rex_b |= reg_ext(b->u.reg);
            if (insn->byte_op) {
                if (needs_rex_low8(b->u.reg) || (a->kind == AS_X86_OP_REG && needs_rex_low8(a->u.reg))) {
                    force_rex = 1;
                }
            }
            if (emit8(&ctx, insn->byte_op ? 0xd2 : 0xd3) != 0 ||
                modrm_sib_disp64(&ctx, (as_x86_reg_t)ext, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 shift form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "jmp")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            if (emit8(&ctx, 0xe9) != 0 || emit32(&ctx, (uint32_t)a->u.rel) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RSP, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 jmp form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "call")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            if (emit8(&ctx, 0xe8) != 0 || emit32(&ctx, (uint32_t)a->u.rel) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RDX, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 call form");
            return -1;
        }
    } else if (encode_jcc_rel32(&ctx, insn->mnemonic, (insn->op_count == 1 && a->kind == AS_X86_OP_REL) ? a->u.rel : 0) == 0) {
        if (!(insn->op_count == 1 && a->kind == AS_X86_OP_REL)) {
            set_err(&ctx, "unsupported x86_64 jcc form");
            return -1;
        }
    } else if (strncmp(insn->mnemonic, "set", 3) == 0) {
        static const struct {
            const char *name;
            uint8_t cc;
        } ccmap[] = {
            {"seto", 0x0},   {"setno", 0x1}, {"setb", 0x2},  {"setnae", 0x2}, {"setc", 0x2},   {"setnb", 0x3},
            {"setae", 0x3},  {"setnc", 0x3}, {"sete", 0x4},  {"setz", 0x4},   {"setne", 0x5},  {"setnz", 0x5},
            {"setbe", 0x6},  {"setna", 0x6}, {"seta", 0x7},  {"setnbe", 0x7}, {"sets", 0x8},   {"setns", 0x9},
            {"setp", 0xa},   {"setpe", 0xa}, {"setnp", 0xb}, {"setpo", 0xb},  {"setl", 0xc},   {"setnge", 0xc},
            {"setge", 0xd},  {"setnl", 0xd}, {"setle", 0xe}, {"setng", 0xe},  {"setg", 0xf},   {"setnle", 0xf},
        };
        size_t i;
        int found = 0;
        uint8_t cc = 0;
        if (insn->op_count != 1 || (a->kind != AS_X86_OP_REG && a->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported setcc form");
            return -1;
        }
        for (i = 0; i < sizeof(ccmap) / sizeof(ccmap[0]); ++i) {
            if (streq_ci(insn->mnemonic, ccmap[i].name)) {
                cc = ccmap[i].cc;
                found = 1;
                break;
            }
        }
        if (!found) {
            set_err(&ctx, "unsupported x86_64 setcc mnemonic: %s", insn->mnemonic);
            return -1;
        }
        if (a->kind == AS_X86_OP_REG && needs_rex_low8(a->u.reg)) {
            force_rex = 1;
        }
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, (uint8_t)(0x90 | cc)) != 0 ||
            modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "push")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REG) {
            rex_b |= reg_ext(a->u.reg);
            if (emit8(&ctx, (uint8_t)(0x50 | reg_low3(a->u.reg))) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RSI, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0x68) != 0 || emit32(&ctx, (uint32_t)a->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 push form (op_count=%zu kind=%d)", insn->op_count,
                    a != NULL ? (int)a->kind : -1);
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "pop")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REG) {
            rex_b |= reg_ext(a->u.reg);
            if (emit8(&ctx, (uint8_t)(0x58 | reg_low3(a->u.reg))) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0x8f) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 pop form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "bswap")) {
        rex_w = 1;
        if (insn->op_count != 1 || a->kind != AS_X86_OP_REG) {
            set_err(&ctx, "unsupported x86_64 bswap form");
            return -1;
        }
        rex_b |= reg_ext(a->u.reg);
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, (uint8_t)(0xc8 | reg_low3(a->u.reg))) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sysenter")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x34) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "leave")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xc9) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "enter")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xc8) != 0 || emit8(&ctx, (uint8_t)(a->u.imm & 0xff)) != 0 ||
                emit8(&ctx, (uint8_t)((a->u.imm >> 8) & 0xff)) != 0 || emit8(&ctx, (uint8_t)(b->u.imm & 0xff)) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 enter form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "clc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xf8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xf9) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cli")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xfa) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sti")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xfb) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cld")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xfc) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "std")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xfd) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmc")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xf5) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ret")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xc3) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cqto") || streq_ci(insn->mnemonic, "cqo")) {
        rex_w = 1;
        if (insn->op_count != 0 || emit8(&ctx, 0x99) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "nop")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x90) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "pause")) {
        if (insn->op_count != 0 || emit8(&ctx, 0xf3) != 0 || emit8(&ctx, 0x90) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "mfence")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xae) != 0 || emit8(&ctx, 0xf0) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lfence")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xae) != 0 || emit8(&ctx, 0xe8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sfence")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xae) != 0 || emit8(&ctx, 0xf8) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "emms")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x77) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ud2")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x0b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpxchg16b")) {
        rex_w = 1;
        if (insn->op_count != 1 || a->kind != AS_X86_OP_MEM || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xc7) != 0 ||
            modrm_sib_disp64(&ctx, AS_X86_REG_RCX, a, &rex_r, &rex_x, &rex_b) != 0) {
            set_err(&ctx, "unsupported cmpxchg16b form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "syscall")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x05) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sysret")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x07) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "swapgs")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || emit8(&ctx, 0xf8) != 0) {
            return -1;
        }
    } else {
        set_err(&ctx, "unsupported x86_64 mnemonic: %s", insn->mnemonic != NULL ? insn->mnemonic : "<null>");
        return -1;
    }

    rex = (uint8_t)(0x40 | (rex_w ? 0x08 : 0) | (rex_r ? 0x04 : 0) | (rex_x ? 0x02 : 0) | (rex_b ? 0x01 : 0));
    if (rex == 0x40 && !force_rex) {
        size_t i;
        for (i = rex_pos + 1; i < ctx.at; ++i) {
            ctx.out[i - 1] = ctx.out[i];
        }
        ctx.at--;
    } else {
        ctx.out[rex_pos] = rex;
    }

    if (out_len != NULL) {
        *out_len = ctx.at;
    }
    return 0;
}
