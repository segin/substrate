#include "as_x86_evex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *out;
    size_t out_cap;
    size_t at;
    char *errbuf;
    size_t errbuf_sz;
} evex_ctx_t;

static void set_err(evex_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

static int emit8(evex_ctx_t *ctx, uint8_t v) {
    if (ctx->at >= ctx->out_cap) {
        set_err(ctx, "encoding overflow");
        return -1;
    }
    ctx->out[ctx->at++] = v;
    return 0;
}

static int emit32(evex_ctx_t *ctx, uint32_t v) {
    if (emit8(ctx, (uint8_t)(v & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 8) & 0xffu)) != 0 ||
        emit8(ctx, (uint8_t)((v >> 16) & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 24) & 0xffu)) != 0) {
        return -1;
    }
    return 0;
}

static uint8_t reg_low3(as_x86_reg_t r) {
    return (uint8_t)(r & 7);
}

static uint8_t reg_ext3(as_x86_reg_t r) {
    return (uint8_t)((r >> 3) & 1);
}

static uint8_t reg_ext4(as_x86_reg_t r) {
    return (uint8_t)((r >> 4) & 1);
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

static int modrm_sib_disp64(evex_ctx_t *ctx, as_x86_reg_t reg_field, const as_x86_operand_t *rm_op,
                            uint8_t *x, uint8_t *b) {
    if (rm_op->kind == AS_X86_OP_REG) {
        *b |= reg_ext3(rm_op->u.reg);
        return emit8(ctx, (uint8_t)(0xc0u | (reg_low3(reg_field) << 3) | reg_low3(rm_op->u.reg)));
    }

    if (rm_op->kind != AS_X86_OP_MEM) {
        set_err(ctx, "expected reg or mem src2");
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
            *x |= reg_ext3(m->index);
            rm = 4;
            needs_sib = 1;
            sib = (uint8_t)((scale_bits(m->scale) << 6) | (reg_low3(m->index) << 3) | 5);
            mod = 0;
            modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, sib) != 0 || emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0)) != 0) {
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
        *b |= reg_ext3(m->base);
        if (m->has_index || reg_low3(m->base) == AS_X86_REG_RSP) {
            uint8_t index_bits = 4;
            uint8_t base_bits = reg_low3(m->base);
            needs_sib = 1;
            rm = 4;
            if (m->has_index) {
                index_bits = reg_low3(m->index);
                *x |= reg_ext3(m->index);
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

int as_x86_encode_evex_3op(const as_x86_evex_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, char *errbuf, size_t errbuf_sz) {
    evex_ctx_t ctx;
    uint8_t r;
    uint8_t x;
    uint8_t b;
    uint8_t r4;
    uint8_t v4;
    uint8_t p0;
    uint8_t p1;
    uint8_t p2;
    uint8_t l2;
    int bbit;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || out == NULL || insn->src2.kind == AS_X86_OP_NONE) {
        return -1;
    }
    if (insn->opmask > 7) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.out_cap = out_cap;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;

    r = reg_ext3(insn->dst);
    r4 = reg_ext4(insn->dst);
    x = 0;
    b = 0;
    v4 = reg_ext4(insn->src1);

    if (emit8(&ctx, insn->opcode) != 0 || modrm_sib_disp64(&ctx, insn->dst, &insn->src2, &x, &b) != 0) {
        return -1;
    }

    l2 = insn->evex_l2 & 0x3;
    bbit = (insn->broadcast || insn->sae || insn->rounding_mode >= 0) ? 1 : 0;
    if (insn->rounding_mode >= 0) {
        l2 = (uint8_t)(insn->rounding_mode & 0x3);
    }

    p0 = (uint8_t)(((r4 ? 0 : 1) << 7) | ((x ? 0 : 1) << 6) | ((b ? 0 : 1) << 5) | ((r ? 0 : 1) << 4) |
                   (insn->map & 0xf));
    p1 = (uint8_t)(((insn->evex_w ? 1 : 0) << 7) | ((((~insn->src1) & 0xf) << 3) & 0x78) | 0x04 |
                   (insn->pp & 0x3));
    p2 = (uint8_t)(((insn->zeroing ? 1 : 0) << 7) | (((l2 >> 1) & 1) << 6) | ((l2 & 1) << 5) |
                   ((bbit ? 1 : 0) << 4) | ((v4 ? 0 : 1) << 3) | (insn->opmask & 0x7));

    memmove(ctx.out + 4, ctx.out, ctx.at);
    ctx.out[0] = 0x62;
    ctx.out[1] = p0;
    ctx.out[2] = p1;
    ctx.out[3] = p2;
    ctx.at += 4;

    if (out_len != NULL) {
        *out_len = ctx.at;
    }
    return 0;
}
