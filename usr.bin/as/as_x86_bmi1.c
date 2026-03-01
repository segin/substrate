#include "as_x86_bmi1.h"

#include "as_x86_vex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *out;
    size_t out_cap;
    size_t at;
    char *errbuf;
    size_t errbuf_sz;
} bmi1_ctx_t;

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

static int set_err(char *errbuf, size_t errbuf_sz, const char *fmt, ...) {
    va_list ap;

    if (errbuf == NULL || errbuf_sz == 0) {
        return -1;
    }

    va_start(ap, fmt);
    vsnprintf(errbuf, errbuf_sz, fmt, ap);
    va_end(ap);
    return -1;
}

static int emit8(bmi1_ctx_t *ctx, uint8_t v) {
    if (ctx->at >= ctx->out_cap) {
        return set_err(ctx->errbuf, ctx->errbuf_sz, "encoding overflow");
    }
    ctx->out[ctx->at++] = v;
    return 0;
}

static int emit32(bmi1_ctx_t *ctx, uint32_t v) {
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

static int modrm_sib_disp64(bmi1_ctx_t *ctx, as_x86_reg_t reg_field, const as_x86_operand_t *rm_op,
                            uint8_t *rex_x, uint8_t *rex_b) {
    if (rm_op->kind == AS_X86_OP_REG) {
        *rex_b |= reg_ext(rm_op->u.reg);
        return emit8(ctx, (uint8_t)(0xc0u | (reg_low3(reg_field) << 3) | reg_low3(rm_op->u.reg)));
    }

    if (rm_op->kind != AS_X86_OP_MEM) {
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

static int encode_vex_bmi1(as_x86_reg_t dst, as_x86_reg_t src1, const as_x86_operand_t *src2,
                           uint8_t opcode, int vex_w, uint8_t *out, size_t out_cap,
                           size_t *out_len, char *errbuf, size_t errbuf_sz) {
    as_x86_vex_insn_t vex;

    memset(&vex, 0, sizeof(vex));
    vex.opcode = opcode;
    vex.map = AS_VEX_MAP_0F38;
    vex.pp = AS_VEX_PP_NONE;
    vex.vex_w = vex_w;
    vex.vex_l = 0;
    vex.dst = dst;
    vex.src1 = src1;
    vex.src2 = *src2;

    return as_x86_encode_vex_3op(&vex, out, out_cap, out_len, errbuf, errbuf_sz);
}

static int encode_tzcnt(const as_x86_bmi1_insn_t *insn, uint8_t *out, size_t out_cap,
                        size_t *out_len, char *errbuf, size_t errbuf_sz) {
    bmi1_ctx_t ctx;
    size_t rex_pos;
    uint8_t rex = 0x40;
    uint8_t rex_r = 0;
    uint8_t rex_x = 0;
    uint8_t rex_b = 0;

    if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
        (insn->width_bits != 32 && insn->width_bits != 64)) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.out_cap = out_cap;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;

    if (emit8(&ctx, 0xf3) != 0) {
        return -1;
    }

    rex_pos = ctx.at;
    if (emit8(&ctx, rex) != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xbc) != 0) {
        return -1;
    }

    rex_r |= reg_ext(insn->op1.u.reg);
    if (modrm_sib_disp64(&ctx, insn->op1.u.reg, &insn->op2, &rex_x, &rex_b) != 0) {
        return -1;
    }

    rex |= (uint8_t)(((insn->width_bits == 64) ? 0x08 : 0) | (rex_r ? 0x04 : 0) |
                     (rex_x ? 0x02 : 0) | (rex_b ? 0x01 : 0));

    if (rex == 0x40) {
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

int as_x86_encode_bmi1(const as_x86_bmi1_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz) {
    int vex_w;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL ||
        (insn->width_bits != 32 && insn->width_bits != 64)) {
        return -1;
    }

    vex_w = (insn->width_bits == 64) ? 1 : 0;

    if (streq_ci(insn->mnemonic, "andn")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        return encode_vex_bmi1(insn->op1.u.reg, insn->op2.u.reg, &insn->op3, 0xf2, vex_w,
                               out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "bextr")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op3.kind != AS_X86_OP_REG || insn->op2.kind == AS_X86_OP_NONE) {
            return -1;
        }
        return encode_vex_bmi1(insn->op1.u.reg, insn->op3.u.reg, &insn->op2, 0xf7, vex_w,
                               out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "blsi") || streq_ci(insn->mnemonic, "blsmsk") ||
        streq_ci(insn->mnemonic, "blsr")) {
        as_x86_reg_t reg_ext;

        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind == AS_X86_OP_NONE) {
            return -1;
        }

        if (streq_ci(insn->mnemonic, "blsr")) {
            reg_ext = AS_X86_REG_RCX; /* /1 */
        } else if (streq_ci(insn->mnemonic, "blsmsk")) {
            reg_ext = AS_X86_REG_RDX; /* /2 */
        } else {
            reg_ext = AS_X86_REG_RBX; /* /3 */
        }

        return encode_vex_bmi1(reg_ext, insn->op1.u.reg, &insn->op2, 0xf3, vex_w,
                               out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "tzcnt")) {
        return encode_tzcnt(insn, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    return set_err(errbuf, errbuf_sz, "unsupported BMI1 mnemonic: %s", insn->mnemonic);
}
