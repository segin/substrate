#include "as_x86_sse42.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *out;
    size_t out_cap;
    size_t at;
    char *errbuf;
    size_t errbuf_sz;
} s42_ctx_t;

typedef struct {
    const char *mnemonic;
    uint8_t map;
    uint8_t opcode;
} s42_desc_t;

static void set_err(s42_ctx_t *ctx, const char *fmt, ...) {
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

static int emit8(s42_ctx_t *ctx, uint8_t v) {
    if (ctx->at >= ctx->out_cap) {
        set_err(ctx, "encoding overflow");
        return -1;
    }
    ctx->out[ctx->at++] = v;
    return 0;
}

static int emit32(s42_ctx_t *ctx, uint32_t v) {
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

static int modrm_sib_disp64(s42_ctx_t *ctx, as_x86_reg_t reg_field, const as_x86_operand_t *rm_op,
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

static int encode_prefixed_map(s42_ctx_t *ctx, const as_x86_sse42_insn_t *insn, uint8_t mandatory_prefix,
                               uint8_t map, uint8_t opcode, int force_rex_w, int has_imm8) {
    size_t rex_pos;
    uint8_t rex = 0x40;
    uint8_t rex_r = 0;
    uint8_t rex_x = 0;
    uint8_t rex_b = 0;

    if (insn->op_count != 2 || insn->dst.kind != AS_X86_OP_REG) {
        return -1;
    }

    if (!!insn->has_imm8 != !!has_imm8) {
        return -1;
    }

    if (mandatory_prefix != 0 && emit8(ctx, mandatory_prefix) != 0) {
        return -1;
    }

    rex_pos = ctx->at;
    if (emit8(ctx, rex) != 0 || emit8(ctx, 0x0f) != 0 || emit8(ctx, map) != 0 || emit8(ctx, opcode) != 0) {
        return -1;
    }

    rex_r |= reg_ext(insn->dst.u.reg);
    if (modrm_sib_disp64(ctx, insn->dst.u.reg, &insn->src, &rex_x, &rex_b) != 0) {
        return -1;
    }

    if (has_imm8 && emit8(ctx, insn->imm8) != 0) {
        return -1;
    }

    rex |= (uint8_t)(((force_rex_w || insn->rex_w) ? 0x08 : 0) | (rex_r ? 0x04 : 0) | (rex_x ? 0x02 : 0) |
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

static int encode_crc32(s42_ctx_t *ctx, const as_x86_sse42_insn_t *insn) {
    uint8_t opcode;
    int force_rex_w = 0;

    if (insn->op_count != 2 || insn->dst.kind != AS_X86_OP_REG || insn->has_imm8) {
        return -1;
    }

    if (insn->width_bits == 8) {
        opcode = 0xf0;
    } else if (insn->width_bits == 16 || insn->width_bits == 32 || insn->width_bits == 64) {
        opcode = 0xf1;
    } else {
        return -1;
    }

    if (insn->width_bits == 16) {
        if (emit8(ctx, 0x66) != 0) {
            return -1;
        }
    }

    if (insn->dst.u.reg >= AS_X86_REG_R8) {
        /* High register destination works for both 32- and 64-bit forms. */
    }

    if (insn->width_bits == 64 || insn->dst.u.reg >= AS_X86_REG_R8) {
        /* width=64 requires REX.W; high regs require REX regardless. */
        force_rex_w = (insn->width_bits == 64);
    }

    if (encode_prefixed_map(ctx, insn, 0xf2, 0x38, opcode, force_rex_w, 0) != 0) {
        return -1;
    }

    return 0;
}

static const s42_desc_t *lookup_pcmp_desc(const char *mnemonic) {
    static const s42_desc_t table[] = {
        {"pcmpestrm", 0x3a, 0x60},
        {"pcmpestri", 0x3a, 0x61},
        {"pcmpistrm", 0x3a, 0x62},
        {"pcmpistri", 0x3a, 0x63},
    };
    size_t i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (streq_ci(mnemonic, table[i].mnemonic)) {
            return &table[i];
        }
    }

    return NULL;
}

int as_x86_encode_sse42(const as_x86_sse42_insn_t *insn, uint8_t *out, size_t out_cap,
                        size_t *out_len, char *errbuf, size_t errbuf_sz) {
    s42_ctx_t ctx;
    const s42_desc_t *pcmp;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || insn->mnemonic == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.out_cap = out_cap;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;

    if (streq_ci(insn->mnemonic, "crc32")) {
        if (encode_crc32(&ctx, insn) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "pcmpgtq")) {
        if (encode_prefixed_map(&ctx, insn, 0x66, 0x38, 0x37, 0, 0) != 0) {
            return -1;
        }
    } else {
        pcmp = lookup_pcmp_desc(insn->mnemonic);
        if (pcmp == NULL) {
            return -1;
        }
        if (encode_prefixed_map(&ctx, insn, 0x66, pcmp->map, pcmp->opcode, 0, 1) != 0) {
            return -1;
        }
    }

    if (out_len != NULL) {
        *out_len = ctx.at;
    }
    return 0;
}
