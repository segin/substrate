#include "as_a64_branch.h"

#include "as_a64_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static int encode_b_bl(const as_a64_branch_insn_t *insn, uint32_t *out_word) {
    int64_t disp;
    uint32_t imm26;
    uint32_t base;

    if (!streq_ci(insn->mnemonic, "b") && !streq_ci(insn->mnemonic, "bl")) {
        return -1;
    }
    if ((insn->imm & 3ll) != 0) {
        return -2;
    }

    disp = insn->imm >> 2;
    if (disp < -(1ll << 25) || disp > ((1ll << 25) - 1ll)) {
        return -2;
    }

    imm26 = (uint32_t)((uint64_t)disp & 0x03ffffffu);
    base = streq_ci(insn->mnemonic, "bl") ? 0x94000000u : 0x14000000u;
    *out_word = base | imm26;
    return 0;
}

static int encode_b_cond(const as_a64_branch_insn_t *insn, uint32_t *out_word) {
    int64_t disp;
    uint32_t imm19;

    if (!streq_ci(insn->mnemonic, "b.cond")) {
        return -1;
    }
    if ((insn->imm & 3ll) != 0) {
        return -2;
    }

    disp = insn->imm >> 2;
    if (disp < -(1ll << 18) || disp > ((1ll << 18) - 1ll)) {
        return -2;
    }

    imm19 = (uint32_t)((uint64_t)disp & 0x7ffffu);
    *out_word = 0x54000000u | (imm19 << 5) | ((uint32_t)insn->cond & 0xfu);
    return 0;
}

static int encode_br_blr_ret(const as_a64_branch_insn_t *insn, uint32_t *out_word) {
    uint32_t base;

    if (insn->rn > 31) {
        return -2;
    }

    if (streq_ci(insn->mnemonic, "br")) {
        base = 0xd61f0000u;
    } else if (streq_ci(insn->mnemonic, "blr")) {
        base = 0xd63f0000u;
    } else if (streq_ci(insn->mnemonic, "ret")) {
        base = 0xd65f0000u;
    } else {
        return -1;
    }

    *out_word = base | ((uint32_t)(insn->rn & 0x1fu) << 5);
    return 0;
}

static int encode_cbz_cbnz(const as_a64_branch_insn_t *insn, uint32_t *out_word) {
    int64_t disp;
    uint32_t imm19;
    uint32_t base;

    if (!streq_ci(insn->mnemonic, "cbz") && !streq_ci(insn->mnemonic, "cbnz")) {
        return -1;
    }
    if (insn->rt > 31) {
        return -2;
    }
    if ((insn->imm & 3ll) != 0) {
        return -2;
    }

    disp = insn->imm >> 2;
    if (disp < -(1ll << 18) || disp > ((1ll << 18) - 1ll)) {
        return -2;
    }

    imm19 = (uint32_t)((uint64_t)disp & 0x7ffffu);

    if (insn->is64) {
        base = streq_ci(insn->mnemonic, "cbnz") ? 0xb5000000u : 0xb4000000u;
    } else {
        base = streq_ci(insn->mnemonic, "cbnz") ? 0x35000000u : 0x34000000u;
    }

    *out_word = base | (imm19 << 5) | (uint32_t)(insn->rt & 0x1fu);
    return 0;
}

static int encode_tbz_tbnz(const as_a64_branch_insn_t *insn, uint32_t *out_word) {
    int64_t disp;
    uint32_t imm14;
    uint32_t base;
    uint32_t b5;
    uint32_t b40;

    if (!streq_ci(insn->mnemonic, "tbz") && !streq_ci(insn->mnemonic, "tbnz")) {
        return -1;
    }
    if (insn->rt > 31) {
        return -2;
    }
    if (insn->bit > 63) {
        return -2;
    }
    if (!insn->is64 && insn->bit >= 32) {
        return -2;
    }
    if ((insn->imm & 3ll) != 0) {
        return -2;
    }

    disp = insn->imm >> 2;
    if (disp < -(1ll << 13) || disp > ((1ll << 13) - 1ll)) {
        return -2;
    }

    imm14 = (uint32_t)((uint64_t)disp & 0x3fffu);
    base = streq_ci(insn->mnemonic, "tbnz") ? 0x37000000u : 0x36000000u;
    b5 = (uint32_t)((insn->bit >> 5) & 1u);
    b40 = (uint32_t)(insn->bit & 0x1fu);

    *out_word = base | (b5 << 31) | (b40 << 19) | (imm14 << 5) | (uint32_t)(insn->rt & 0x1fu);
    return 0;
}

static int encode_exception(const as_a64_branch_insn_t *insn, uint32_t *out_word) {
    uint32_t base;

    if (streq_ci(insn->mnemonic, "svc")) {
        base = 0xd4000001u;
    } else if (streq_ci(insn->mnemonic, "hvc")) {
        base = 0xd4000002u;
    } else if (streq_ci(insn->mnemonic, "smc")) {
        base = 0xd4000003u;
    } else if (streq_ci(insn->mnemonic, "brk")) {
        base = 0xd4200000u;
    } else if (streq_ci(insn->mnemonic, "hlt")) {
        base = 0xd4400000u;
    } else {
        return -1;
    }

    *out_word = base | (((uint32_t)insn->imm16 & 0xffffu) << 5);
    return 0;
}

int as_a64_encode_branch(const as_a64_branch_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz) {
    uint32_t word;
    int rc;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid A64 branch encode inputs");
    }

    rc = encode_b_bl(insn, &word);
    if (rc == 0) {
        goto emit;
    }
    if (rc == -2) {
        return set_err(errbuf, errbuf_sz, "A64 B/BL operand out of range");
    }

    rc = encode_b_cond(insn, &word);
    if (rc == 0) {
        goto emit;
    }
    if (rc == -2) {
        return set_err(errbuf, errbuf_sz, "A64 B.cond operand out of range");
    }

    rc = encode_br_blr_ret(insn, &word);
    if (rc == 0) {
        goto emit;
    }
    if (rc == -2) {
        return set_err(errbuf, errbuf_sz, "A64 BR/BLR/RET register out of range");
    }

    rc = encode_cbz_cbnz(insn, &word);
    if (rc == 0) {
        goto emit;
    }
    if (rc == -2) {
        return set_err(errbuf, errbuf_sz, "A64 CBZ/CBNZ operand out of range");
    }

    rc = encode_tbz_tbnz(insn, &word);
    if (rc == 0) {
        goto emit;
    }
    if (rc == -2) {
        return set_err(errbuf, errbuf_sz, "A64 TBZ/TBNZ operand out of range");
    }

    rc = encode_exception(insn, &word);
    if (rc == 0) {
        goto emit;
    }

    return set_err(errbuf, errbuf_sz, "unsupported A64 branch mnemonic: %s", insn->mnemonic);

emit:
    as_a64_put32le(out, word);
    if (out_len != NULL) {
        *out_len = 4;
    }
    return 0;
}
