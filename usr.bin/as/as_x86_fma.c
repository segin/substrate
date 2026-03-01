#include "as_x86_fma.h"

#include "as_x86_vex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    uint8_t base_opcode;
    int packed_only;
} fma_family_t;

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

static size_t ascii_lower_copy(char *dst, size_t dst_cap, const char *src) {
    size_t i;

    if (dst_cap == 0) {
        return 0;
    }

    for (i = 0; src[i] != '\0' && i + 1 < dst_cap; ++i) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        dst[i] = c;
    }
    dst[i] = '\0';
    return i;
}

static int parse_form_offset(const char *p, uint8_t *offset, const char **rest) {
    if (p[0] == '1' && p[1] == '3' && p[2] == '2') {
        *offset = 0x00;
        *rest = p + 3;
        return 0;
    }
    if (p[0] == '2' && p[1] == '1' && p[2] == '3') {
        *offset = 0x10;
        *rest = p + 3;
        return 0;
    }
    if (p[0] == '2' && p[1] == '3' && p[2] == '1') {
        *offset = 0x20;
        *rest = p + 3;
        return 0;
    }
    return -1;
}

int as_x86_encode_fma(const as_x86_fma_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz) {
    static const fma_family_t families[] = {
        {"fmaddsub", 0x96, 1},
        {"fmsubadd", 0x97, 1},
        {"fmadd", 0x98, 0},
        {"fmsub", 0x9a, 0},
        {"fnmadd", 0x9c, 0},
        {"fnmsub", 0x9e, 0},
    };
    char mn[64];
    const fma_family_t *fam = NULL;
    uint8_t form_offset = 0;
    const char *p;
    const char *suffix;
    size_t i;
    uint8_t opcode;
    int vex_w = 0;
    int vex_l = 0;
    as_x86_vex_insn_t vex;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || out == NULL || insn->mnemonic == NULL) {
        return -1;
    }

    if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
        insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE) {
        return -1;
    }

    ascii_lower_copy(mn, sizeof(mn), insn->mnemonic);
    if (mn[0] != 'v') {
        return -1;
    }

    p = mn + 1;
    for (i = 0; i < sizeof(families) / sizeof(families[0]); ++i) {
        size_t n = strlen(families[i].name);
        if (strncmp(p, families[i].name, n) == 0) {
            fam = &families[i];
            p += n;
            break;
        }
    }
    if (fam == NULL) {
        return set_err(errbuf, errbuf_sz, "unsupported FMA mnemonic: %s", insn->mnemonic);
    }

    if (parse_form_offset(p, &form_offset, &suffix) != 0) {
        return -1;
    }

    if (strcmp(suffix, "ps") == 0) {
        if (insn->vector_bits == 128) {
            vex_l = 0;
        } else if (insn->vector_bits == 256) {
            vex_l = 1;
        } else {
            return -1;
        }
        vex_w = 0;
        opcode = (uint8_t)(fam->base_opcode + form_offset);
    } else if (strcmp(suffix, "pd") == 0) {
        if (insn->vector_bits == 128) {
            vex_l = 0;
        } else if (insn->vector_bits == 256) {
            vex_l = 1;
        } else {
            return -1;
        }
        vex_w = 1;
        opcode = (uint8_t)(fam->base_opcode + form_offset);
    } else if (strcmp(suffix, "ss") == 0 || strcmp(suffix, "sd") == 0) {
        if (fam->packed_only || insn->vector_bits != 128) {
            return -1;
        }
        vex_l = 0;
        vex_w = (suffix[1] == 'd') ? 1 : 0;
        opcode = (uint8_t)(fam->base_opcode + form_offset + 1);
    } else {
        return -1;
    }

    memset(&vex, 0, sizeof(vex));
    vex.opcode = opcode;
    vex.map = AS_VEX_MAP_0F38;
    vex.pp = AS_VEX_PP_66;
    vex.vex_w = vex_w;
    vex.vex_l = vex_l;
    vex.dst = insn->op1.u.reg;
    vex.src1 = insn->op2.u.reg;
    vex.src2 = insn->op3;

    return as_x86_encode_vex_3op(&vex, out, out_cap, out_len, errbuf, errbuf_sz);
}
