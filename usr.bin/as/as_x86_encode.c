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

static int is_disp8(int32_t v) {
    return v >= -128 && v <= 127;
}

static int emit_prefixes(enc_ctx_t *ctx, const as_x86_insn_t *insn) {
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
        modrm = (uint8_t)(0xc0u | ((reg_field & 7u) << 3) | (uint8_t)(rm_op->u.reg & 7));
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

        modrm = (uint8_t)((mod << 6) | ((reg_field & 7u) << 3) | rm);
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

    if (streq_ci(insn->mnemonic, "mov")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM) {
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
    } else if (streq_ci(insn->mnemonic, "add") || streq_ci(insn->mnemonic, "sub") || streq_ci(insn->mnemonic, "and") ||
               streq_ci(insn->mnemonic, "or") || streq_ci(insn->mnemonic, "xor") || streq_ci(insn->mnemonic, "cmp")) {
        uint8_t op_rm_reg;
        uint8_t op_reg_rm;
        uint8_t ext;

        if (streq_ci(insn->mnemonic, "add")) {
            op_rm_reg = 0x01;
            op_reg_rm = 0x03;
            ext = 0;
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

        if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG) {
            if (encode_reg_rm_pair(&ctx, op_rm_reg, a, b, 1) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && is_reg_or_mem(b)) {
            if (encode_reg_rm_pair(&ctx, op_reg_rm, a, b, 0) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0x81) != 0 || modrm_sib_disp(&ctx, ext, a) != 0 || emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported ALU form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "shl") || streq_ci(insn->mnemonic, "shr") || streq_ci(insn->mnemonic, "sar")) {
        uint8_t ext;
        if (streq_ci(insn->mnemonic, "shl")) {
            ext = 4;
        } else if (streq_ci(insn->mnemonic, "shr")) {
            ext = 5;
        } else {
            ext = 7;
        }

        if (insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xc1) != 0 || modrm_sib_disp(&ctx, ext, a) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported shift form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsb")) {
        if (emit8(&ctx, 0xa4) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsd")) {
        if (emit8(&ctx, 0xa5) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosb")) {
        if (emit8(&ctx, 0xaa) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosd")) {
        if (emit8(&ctx, 0xab) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsb")) {
        if (emit8(&ctx, 0xa6) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsd")) {
        if (emit8(&ctx, 0xa7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsb")) {
        if (emit8(&ctx, 0xac) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsd")) {
        if (emit8(&ctx, 0xad) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasb")) {
        if (emit8(&ctx, 0xae) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasd")) {
        if (emit8(&ctx, 0xaf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "jmp")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            if (emit8(&ctx, 0xe9) != 0 || emit32(&ctx, (uint32_t)a->u.rel) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported jmp form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "call")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            if (emit8(&ctx, 0xe8) != 0 || emit32(&ctx, (uint32_t)a->u.rel) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported call form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ret")) {
        if (emit8(&ctx, 0xc3) != 0) {
            return -1;
        }
    } else if (encode_jcc_rel32(&ctx, insn->mnemonic, (insn->op_count == 1 && a->kind == AS_X86_OP_REL) ? a->u.rel : 0) == 0) {
        if (!(insn->op_count == 1 && a->kind == AS_X86_OP_REL)) {
            set_err(&ctx, "unsupported jcc form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "push")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, (uint8_t)(0x50 | (a->u.reg & 7))) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0x68) != 0 || emit32(&ctx, (uint32_t)a->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported push form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "pop")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, (uint8_t)(0x58 | (a->u.reg & 7))) != 0) {
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
        if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM && a->u.reg == AS_X86_REG_EAX) {
            if (emit8(&ctx, 0xe5) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported in form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "out")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_REG && b->u.reg == AS_X86_REG_EAX) {
            if (emit8(&ctx, 0xe7) != 0 || emit8(&ctx, (uint8_t)a->u.imm) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported out form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "nop")) {
        if (emit8(&ctx, 0x90) != 0) {
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
