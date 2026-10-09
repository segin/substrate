#include "as_x86_sse41.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *out;
    size_t out_cap;
    size_t at;
    char *errbuf;
    size_t errbuf_sz;
} s41_ctx_t;

typedef struct {
    const char *mnemonic;
    uint8_t map;
    uint8_t opcode;
    uint8_t has_imm8;
    uint8_t rm_dst;
    uint8_t rm_must_mem;
    uint8_t force_rex_w;
} s41_desc_t;

static void set_err(s41_ctx_t *ctx, const char *fmt, ...) {
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

static int emit8(s41_ctx_t *ctx, uint8_t v) {
    if (ctx->at >= ctx->out_cap) {
        set_err(ctx, "encoding overflow");
        return -1;
    }
    ctx->out[ctx->at++] = v;
    return 0;
}

static int emit32(s41_ctx_t *ctx, uint32_t v) {
    if (emit8(ctx, (uint8_t)(v & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 8) & 0xffu)) != 0 ||
        emit8(ctx, (uint8_t)((v >> 16) & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 24) & 0xffu)) != 0) {
        return -1;
    }
    return 0;
}

static uint8_t reg_low3(as_x86_reg_t r) {
    return (uint8_t)(r & 7);
}

static uint8_t reg_ext(as_x86_reg_t r) {
    return (uint8_t)((r >> 3) & 1);
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

static int is_disp8(int32_t v) {
    return v >= -128 && v <= 127;
}

static int modrm_sib_disp64(s41_ctx_t *ctx, as_x86_reg_t reg_field, const as_x86_operand_t *rm_op,
                            uint8_t *rex_x, uint8_t *rex_b) {
    if (rm_op->kind == AS_X86_OP_REG) {
        *rex_b |= reg_ext(rm_op->u.reg);
        return emit8(ctx, (uint8_t)(0xc0u | (reg_low3(reg_field) << 3) | reg_low3(rm_op->u.reg)));
    }

    if (rm_op->kind != AS_X86_OP_MEM) {
        set_err(ctx, "expected reg/mem operand");
        return -1;
    }

    {
        const as_x86_mem_t *m = &rm_op->u.mem;
        uint8_t mod = 0;
        uint8_t rm = 0;
        uint8_t modrm;
        uint8_t sib = 0;
        int needs_sib = 0;

        if (m->scale != 0 && m->scale != 1 && m->scale != 2 && m->scale != 4 && m->scale != 8) {
            set_err(ctx, "invalid scale %u", m->scale);
            return -1;
        }

        if (m->rip_relative) {
            modrm = (uint8_t)((reg_low3(reg_field) << 3) | 5);
            if (emit8(ctx, modrm) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }

        if (m->disp_only) {
            modrm = (uint8_t)((reg_low3(reg_field) << 3) | 4);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, 0x25) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }

        if (!m->has_base && !m->has_index) {
            set_err(ctx, "memory operand missing base/index");
            return -1;
        }

        if (m->has_index && !m->has_base) {
            *rex_x |= reg_ext(m->index);
            rm = 4;
            needs_sib = 1;
            sib = (uint8_t)((scale_bits(m->scale) << 6) | (reg_low3(m->index) << 3) | 5);
            mod = 0;
            modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, sib) != 0 ||
                emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0)) != 0) {
                return -1;
            }
            return 0;
        }

        if (!m->has_disp && reg_low3(m->base) != AS_X86_REG_RBP) {
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

        modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
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

static int encode_desc(s41_ctx_t *ctx, const as_x86_sse41_insn_t *insn, const s41_desc_t *desc) {
    size_t rex_pos;
    uint8_t rex = 0x40;
    uint8_t rex_r = 0;
    uint8_t rex_x = 0;
    uint8_t rex_b = 0;
    const as_x86_operand_t *rm_op;
    as_x86_reg_t reg_field;

    if (insn->op_count != 2) {
        set_err(ctx, "invalid operand count for %s", insn->mnemonic);
        return -1;
    }

    if (!!insn->has_imm8 != !!desc->has_imm8) {
        set_err(ctx, "immediate form mismatch for %s", insn->mnemonic);
        return -1;
    }

    if (emit8(ctx, 0x66) != 0) {
        return -1;
    }

    rex_pos = ctx->at;
    if (emit8(ctx, rex) != 0 || emit8(ctx, 0x0f) != 0) {
        return -1;
    }
    if (desc->map != 0 && emit8(ctx, desc->map) != 0) {
        return -1;
    }
    if (emit8(ctx, desc->opcode) != 0) {
        return -1;
    }

    if (!desc->rm_dst) {
        if (insn->dst.kind != AS_X86_OP_REG) {
            set_err(ctx, "destination must be register for %s", insn->mnemonic);
            return -1;
        }
        reg_field = insn->dst.u.reg;
        rm_op = &insn->src;
    } else {
        if (insn->src.kind != AS_X86_OP_REG) {
            set_err(ctx, "source must be register for %s", insn->mnemonic);
            return -1;
        }
        reg_field = insn->src.u.reg;
        rm_op = &insn->dst;
    }

    if (desc->rm_must_mem && rm_op->kind != AS_X86_OP_MEM) {
        set_err(ctx, "memory source required for %s", insn->mnemonic);
        return -1;
    }

    rex_r |= reg_ext(reg_field);
    if (modrm_sib_disp64(ctx, reg_field, rm_op, &rex_x, &rex_b) != 0) {
        return -1;
    }

    if (desc->has_imm8 && emit8(ctx, insn->imm8) != 0) {
        return -1;
    }

    rex |= (uint8_t)((desc->force_rex_w ? 0x08 : 0) | (rex_r ? 0x04 : 0) | (rex_x ? 0x02 : 0) |
                     (rex_b ? 0x01 : 0));

    if (rex == 0x40) {
        size_t i;
        for (i = rex_pos + 1; i < ctx->at; ++i) {
            ctx->out[i - 1] = ctx->out[i];
        }
        ctx->at--;
    } else {
        ctx->out[rex_pos] = rex;
    }

    return 0;
}

static const s41_desc_t *lookup_desc(const char *mnemonic) {
    static const s41_desc_t table[] = {
        {"blendps", 0x3a, 0x0c, 1, 0, 0, 0},
        {"blendpd", 0x3a, 0x0d, 1, 0, 0, 0},
        {"blendvps", 0x38, 0x14, 0, 0, 0, 0},
        {"blendvpd", 0x38, 0x15, 0, 0, 0, 0},
        {"dpps", 0x3a, 0x40, 1, 0, 0, 0},
        {"dppd", 0x3a, 0x41, 1, 0, 0, 0},
        {"extractps", 0x3a, 0x17, 1, 1, 0, 0},
        {"insertps", 0x3a, 0x21, 1, 0, 0, 0},
        {"movntdqa", 0x38, 0x2a, 0, 0, 1, 0},
        {"mpsadbw", 0x3a, 0x42, 1, 0, 0, 0},
        {"packusdw", 0x38, 0x2b, 0, 0, 0, 0},
        {"pblendvb", 0x38, 0x10, 0, 0, 0, 0},
        {"pblendw", 0x3a, 0x0e, 1, 0, 0, 0},
        {"pcmpeqq", 0x38, 0x29, 0, 0, 0, 0},
        {"pextrb", 0x3a, 0x14, 1, 1, 0, 0},
        {"pextrd", 0x3a, 0x16, 1, 1, 0, 0},
        {"pextrq", 0x3a, 0x16, 1, 1, 0, 1},
        {"pextrw", 0x3a, 0x15, 1, 1, 0, 0},
        {"pinsrb", 0x3a, 0x20, 1, 0, 0, 0},
        {"pinsrd", 0x3a, 0x22, 1, 0, 0, 0},
        {"pinsrq", 0x3a, 0x22, 1, 0, 0, 1},
        {"pmaxsb", 0x38, 0x3c, 0, 0, 0, 0},
        {"pmaxsd", 0x38, 0x3d, 0, 0, 0, 0},
        {"pmaxud", 0x38, 0x3f, 0, 0, 0, 0},
        {"pmaxuw", 0x38, 0x3e, 0, 0, 0, 0},
        {"pminsb", 0x38, 0x38, 0, 0, 0, 0},
        {"pminsd", 0x38, 0x39, 0, 0, 0, 0},
        {"pminud", 0x38, 0x3b, 0, 0, 0, 0},
        {"pminuw", 0x38, 0x3a, 0, 0, 0, 0},
        {"pmovsxbw", 0x38, 0x20, 0, 0, 0, 0},
        {"pmovsxbd", 0x38, 0x21, 0, 0, 0, 0},
        {"pmovsxbq", 0x38, 0x22, 0, 0, 0, 0},
        {"pmovsxwd", 0x38, 0x23, 0, 0, 0, 0},
        {"pmovsxwq", 0x38, 0x24, 0, 0, 0, 0},
        {"pmovsxdq", 0x38, 0x25, 0, 0, 0, 0},
        {"pmovzxbw", 0x38, 0x30, 0, 0, 0, 0},
        {"pmovzxbd", 0x38, 0x31, 0, 0, 0, 0},
        {"pmovzxbq", 0x38, 0x32, 0, 0, 0, 0},
        {"pmovzxwd", 0x38, 0x33, 0, 0, 0, 0},
        {"pmovzxwq", 0x38, 0x34, 0, 0, 0, 0},
        {"pmovzxdq", 0x38, 0x35, 0, 0, 0, 0},
        {"pmuldq", 0x38, 0x28, 0, 0, 0, 0},
        {"pmulld", 0x38, 0x40, 0, 0, 0, 0},
        {"ptest", 0x38, 0x17, 0, 0, 0, 0},
        {"roundpd", 0x3a, 0x09, 1, 0, 0, 0},
        {"roundps", 0x3a, 0x08, 1, 0, 0, 0},
        {"roundsd", 0x3a, 0x0b, 1, 0, 0, 0},
        {"roundss", 0x3a, 0x0a, 1, 0, 0, 0},
        {"phminposuw", 0x38, 0x41, 0, 0, 0, 0},
    };
    size_t i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (streq_ci(mnemonic, table[i].mnemonic)) {
            return &table[i];
        }
    }

    return NULL;
}

int as_x86_encode_sse41(const as_x86_sse41_insn_t *insn, uint8_t *out, size_t out_cap,
                        size_t *out_len, char *errbuf, size_t errbuf_sz) {
    s41_ctx_t ctx;
    const s41_desc_t *desc;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || out == NULL || insn->mnemonic == NULL) {
        return -1;
    }

    desc = lookup_desc(insn->mnemonic);
    if (desc == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.out_cap = out_cap;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;

    if (encode_desc(&ctx, insn, desc) != 0) {
        return -1;
    }

    if (out_len != NULL) {
        *out_len = ctx.at;
    }
    return 0;
}
