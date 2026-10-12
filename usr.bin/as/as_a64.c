#include "as_arm.h"

#include "as_arm_int.h"

#include "elfobj.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The instructions of AArch64 (A64).  See as_arm.h. */

#define text as_arm_text
#define number as_arm_number
#define symbol_ref as_arm_symbol

typedef struct {
    const as_arm_req_t *rq;
    as_arm_out_t *out;
    char *errbuf;
    size_t errbuf_sz;
    char m[24]; /* the mnemonic, in lower case */
} a64_t;

typedef as_a64_reg_t reg_t;

#define parse_reg as_a64_parse_reg
#define cond_of as_a64_cond

static int fail(a64_t *a, const char *fmt, ...) {
    va_list ap;

    if (a->errbuf != NULL && a->errbuf_sz != 0) {
        va_start(ap, fmt);
        vsnprintf(a->errbuf, a->errbuf_sz, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int word(a64_t *a, uint32_t w) {
    unsigned i;

    for (i = 0; i < 4; ++i) {
        a->out->bytes[i] = (uint8_t)(w >> (8 * i));
    }
    a->out->len = 4;
    return 0;
}

static int fixup(a64_t *a, unsigned type, const char *symbol, int64_t addend) {
    as_arm_fixup_t *f;

    if (a->out->fixup_count >= sizeof(a->out->fixups) / sizeof(a->out->fixups[0]) ||
        strlen(symbol) >= sizeof(f->symbol)) {
        return fail(a, "'%s': a symbol's name too long", a->rq->mnemonic);
    }
    f = &a->out->fixups[a->out->fixup_count++];
    f->offset = 0;
    f->type = type;
    memcpy(f->symbol, symbol, strlen(symbol) + 1);
    f->addend = addend;
    return 0;
}

/* A number written as it is: no sign, and no 0 before another digit --
 * x05 is no register. */
static int plain_number(const char *s, unsigned max, unsigned *out) {
    char *end;
    unsigned long n;

    if (!isdigit((unsigned char)s[0]) || (s[0] == '0' && s[1] != '\0')) {
        return -1;
    }
    n = strtoul(s, &end, 10);
    if (*end != '\0' || n > max) {
        return -1;
    }
    *out = (unsigned)n;
    return 0;
}

int as_a64_parse_reg(const char *s, reg_t *r) {
    static const struct {
        const char *name;
        unsigned n;
        unsigned bits;
        int sp;
        int zr;
    } named[] = {{"sp", 31, 64, 1, 0},  {"wsp", 31, 32, 1, 0}, {"xzr", 31, 64, 0, 1}, {"wzr", 31, 32, 0, 1},
                 {"fp", 29, 64, 0, 0},  {"lr", 30, 64, 0, 0},  {"ip0", 16, 64, 0, 0}, {"ip1", 17, 64, 0, 0}};
    char buf[24];
    char *dot;
    char *br;
    size_t i;

    memset(r, 0, sizeof(*r));
    r->index = -1;
    text(s, buf, sizeof(buf));
    for (i = 0; i < sizeof(named) / sizeof(named[0]); ++i) {
        if (strcmp(buf, named[i].name) == 0) {
            r->n = named[i].n;
            r->bits = named[i].bits;
            r->is_sp = named[i].sp;
            r->is_zr = named[i].zr;
            return 0;
        }
    }
    if ((buf[0] == 'x' || buf[0] == 'w') && plain_number(buf + 1, 30, &r->n) == 0) {
        r->bits = buf[0] == 'x' ? 64 : 32;
        return 0;
    }
    if (strchr("bhsdq", buf[0]) != NULL && buf[0] != '\0' && plain_number(buf + 1, 31, &r->n) == 0) {
        r->fp = 1;
        r->bits = buf[0] == 'b' ? 8 : buf[0] == 'h' ? 16 : buf[0] == 's' ? 32 : buf[0] == 'd' ? 64 : 128;
        return 0;
    }
    if (buf[0] != 'v') {
        return -1;
    }
    /* v0.16b, v0.s[1]. */
    dot = strchr(buf, '.');
    if (dot == NULL) {
        return -1;
    }
    *dot++ = '\0';
    if (plain_number(buf + 1, 31, &r->n) != 0) {
        return -1;
    }
    r->vec = 1;
    br = strchr(dot, '[');
    if (br != NULL) {
        char *close = strchr(br, ']');
        unsigned idx;

        if (close == NULL || close[1] != '\0') {
            return -1;
        }
        *close = '\0';
        *br = '\0';
        if (plain_number(br + 1, 15, &idx) != 0 || dot[1] != '\0' || strchr("bhsd", dot[0]) == NULL) {
            return -1;
        }
        r->index = (int)idx;
        r->esize = dot[0] == 'b' ? 0u : dot[0] == 'h' ? 1u : dot[0] == 's' ? 2u : 3u;
        r->q = 1;
        return 0;
    }
    {
        static const struct {
            const char *name;
            unsigned q;
            unsigned esize;
        } arr[] = {{"8b", 0, 0}, {"16b", 1, 0}, {"4h", 0, 1}, {"8h", 1, 1},
                   {"2s", 0, 2}, {"4s", 1, 2},  {"1d", 0, 3}, {"2d", 1, 3}};

        for (i = 0; i < sizeof(arr) / sizeof(arr[0]); ++i) {
            if (strcmp(dot, arr[i].name) == 0) {
                r->q = arr[i].q;
                r->esize = arr[i].esize;
                return 0;
            }
        }
    }
    return -1;
}

int as_a64_register(const char *name, unsigned *number_out, unsigned *bits, int *is_sp) {
    reg_t r;

    if (parse_reg(name, &r) != 0) {
        return -1;
    }
    *number_out = r.n;
    *bits = r.bits;
    *is_sp = r.is_sp;
    return 0;
}

/* A general register: with `sp` 1 the stack pointer may be it, 0 the
 * zero register may, 2 either. */
static int gpr(a64_t *a, size_t at, int sp, reg_t *r) {
    if (at >= a->rq->op_count || parse_reg(a->rq->ops[at], r) != 0 || r->fp || r->vec ||
        (r->is_sp && sp == 0) || (r->is_zr && sp == 1)) {
        return fail(a, "'%s': operand %u is not a register it takes there", a->rq->mnemonic, (unsigned)at + 1);
    }
    return 0;
}

static const char *const conds[] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                    "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"};

int as_a64_cond(const char *s, unsigned *c) {
    char buf[8];
    unsigned i;

    text(s, buf, sizeof(buf));
    for (i = 0; i < 16; ++i) {
        if (strcmp(buf, conds[i]) == 0) {
            *c = i;
            return 0;
        }
    }
    if (strcmp(buf, "hs") == 0) {
        *c = 2;
        return 0;
    }
    if (strcmp(buf, "lo") == 0) {
        *c = 3;
        return 0;
    }
    return -1;
}

/*
 * A number as the logical instructions hold one: a run of ones, turned,
 * and repeated across the register.  0 with N, immr and imms in their
 * thirteen bits, or -1 for a number that is not such.
 */
static int bitmask(uint64_t v, unsigned bits, uint32_t *enc) {
    unsigned size;

    if (bits == 32) {
        v &= 0xffffffffu;
        v |= v << 32;
    }
    if (v == 0 || v == ~(uint64_t)0) {
        return -1;
    }
    for (size = 2; size <= 64; size *= 2) {
        uint64_t mask = size == 64 ? ~(uint64_t)0 : (((uint64_t)1 << size) - 1);
        uint64_t elem = v & mask;
        unsigned ones = 0;
        unsigned rot;
        unsigned i;
        uint64_t rep = 0;

        for (i = 0; i < 64; i += size) {
            rep |= elem << i;
        }
        if (rep != v) {
            continue;
        }
        for (i = 0; i < size; ++i) {
            ones += (unsigned)((elem >> i) & 1u);
        }
        if (ones == 0 || ones == size) {
            continue;
        }
        /* The turn that leaves the ones at the bottom. */
        for (rot = 0; rot < size; ++rot) {
            uint64_t t = rot == 0 ? elem : ((elem << rot) | (elem >> (size - rot))) & mask;

            if (t == (((uint64_t)1 << ones) - 1)) {
                unsigned imms = ((~(size * 2 - 1)) & 0x3fu) | (ones - 1);

                *enc = ((size == 64 ? 1u : 0u) << 12) | (rot << 6) | (imms & 0x3fu);
                return 0;
            }
        }
    }
    return -1;
}

/* `lsl #n`, `uxtw #n`, `sxtx` and their like after a register: 0 with
 * which and by how much (-1 for no amount written). */
static int shift_or_extend(const char *s, char *kind, size_t kind_sz, long long *amount) {
    char buf[32];
    size_t n = 0;

    text(s, buf, sizeof(buf));
    while (isalpha((unsigned char)buf[n]) && n + 1 < kind_sz) {
        kind[n] = buf[n];
        n++;
    }
    kind[n] = '\0';
    *amount = -1;
    while (isspace((unsigned char)buf[n])) {
        n++;
    }
    if (buf[n] == '\0') {
        return n != 0 ? 0 : -1;
    }
    return number(buf + n, amount);
}

static int is_reg_text(const char *s) {
    reg_t r;

    return parse_reg(s, &r) == 0;
}

/* The distance from this instruction to a label of its section: 0, or
 * 1 for a name that is none. */
static int distance(a64_t *a, const char *name, long long addend, long long *d) {
    uint64_t at = 0;

    if (a->rq->label == NULL || a->rq->label(a->rq->cookie, name, &at) != 0) {
        return 1;
    }
    *d = (long long)at + addend - (long long)a->rq->pc;
    return 0;
}

/* add, sub, adds, subs, cmp, cmn, neg, negs. */
static int add_sub(a64_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *m = a->m;
    int is_cmp = strcmp(m, "cmp") == 0 || strcmp(m, "cmn") == 0;
    int is_neg = strncmp(m, "neg", 3) == 0;
    unsigned sub = (strncmp(m, "sub", 3) == 0 || strcmp(m, "cmp") == 0 || is_neg) ? 1u : 0u;
    unsigned s = (is_cmp || m[strlen(m) - 1] == 's') ? 1u : 0u;
    size_t at = 0;
    reg_t rd;
    reg_t rn;
    reg_t rm;
    const char *src;
    unsigned sf;

    memset(&rd, 0, sizeof(rd));
    memset(&rn, 0, sizeof(rn));
    if (is_cmp) {
        if (gpr(a, at++, 1, &rn) != 0) {
            return -1;
        }
        rd = rn;
        rd.n = 31;
    } else {
        if (gpr(a, at++, s ? 0 : 1, &rd) != 0) {
            return -1;
        }
        if (is_neg) {
            rn = rd;
            rn.n = 31;
        } else if (gpr(a, at++, 1, &rn) != 0) {
            return -1;
        }
    }
    sf = (is_cmp ? rn.bits : rd.bits) == 64 ? 1u : 0u;
    if (at >= rq->op_count) {
        return fail(a, "'%s' has too few operands", rq->mnemonic);
    }
    src = rq->ops[at];
    while (isspace((unsigned char)*src)) {
        src++;
    }
    if (!is_reg_text(src)) {
        long long v;
        unsigned shift12 = 0;
        const char *p = src[0] == '#' ? src + 1 : src;

        if (is_neg) {
            return fail(a, "'%s': a register", rq->mnemonic);
        }
        /* The low twelve bits of an address, for the linker. */
        if (strncasecmp(p, ":lo12:", 6) == 0) {
            char name[128];
            long long addend;

            if (symbol_ref(p + 6, name, sizeof(name), &addend) != 0 || sub || s ||
                fixup(a, R_AARCH64_ADD_ABS_LO12_NC, name, addend) != 0) {
                return fail(a, "'%s': :lo12: and a symbol, in an add", rq->mnemonic);
            }
            return word(a, (sf << 31) | 0x11000000u | (rn.n << 5) | rd.n);
        }
        if (number(src, &v) != 0) {
            return fail(a, "'%s': '%s' is not a register or a number", rq->mnemonic, src);
        }
        if (at + 2 == rq->op_count) {
            char kind[8];
            long long amount;

            if (shift_or_extend(rq->ops[at + 1], kind, sizeof(kind), &amount) != 0 || strcmp(kind, "lsl") != 0 ||
                (amount != 0 && amount != 12)) {
                return fail(a, "'%s': a number is shifted left by 12 or not at all", rq->mnemonic);
            }
            shift12 = amount == 12;
        } else if (at + 1 != rq->op_count) {
            return fail(a, "'%s' has too many operands", rq->mnemonic);
        }
        /* A number taken away is its opposite added. */
        if (v < 0) {
            v = -v;
            sub ^= 1u;
        }
        if (!shift12 && v > 0xfff && (v & 0xfff) == 0 && v <= 0xfff000) {
            v >>= 12;
            shift12 = 1;
        }
        if (v > 0xfff) {
            return fail(a, "'%s': %lld is not a number of twelve bits, shifted by 12 or not", rq->mnemonic, v);
        }
        return word(a, (sf << 31) | (sub << 30) | (s << 29) | 0x11000000u | (shift12 << 22) | ((uint32_t)v << 10) |
                           (rn.n << 5) | rd.n);
    }
    if (gpr(a, at, 0, &rm) != 0) {
        return -1;
    }
    {
        char kind[8] = "";
        long long amount = -1;
        int extended = rd.is_sp || rn.is_sp;

        if (at + 2 == rq->op_count) {
            if (shift_or_extend(rq->ops[at + 1], kind, sizeof(kind), &amount) != 0) {
                return fail(a, "'%s': '%s' is no shift or extension", rq->mnemonic, rq->ops[at + 1]);
            }
        } else if (at + 1 != rq->op_count) {
            return fail(a, "'%s' has too many operands", rq->mnemonic);
        }
        if (kind[0] == 'u' || kind[0] == 's') {
            extended = 1;
        }
        if (extended) {
            static const char *const ext[] = {"uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx"};
            unsigned option = sf ? 3u : 2u;
            unsigned i;

            /* (With the stack pointer, `lsl` is the extension that
             * extends nothing.) */
            if (kind[0] != '\0' && strcmp(kind, "lsl") != 0) {
                for (i = 0; i < 8 && strcmp(kind, ext[i]) != 0; ++i) {
                }
                if (i == 8) {
                    return fail(a, "'%s': '%s' is no extension", rq->mnemonic, kind);
                }
                option = i;
            } else if (rm.bits == 32 && sf) {
                option = 2;
            }
            if (amount < 0) {
                amount = 0;
            }
            if (amount > 4) {
                return fail(a, "'%s': an extension is shifted by 0 to 4", rq->mnemonic);
            }
            return word(a, (sf << 31) | (sub << 30) | (s << 29) | 0x0b200000u | (rm.n << 16) | (option << 13) |
                               ((uint32_t)amount << 10) | (rn.n << 5) | rd.n);
        }
        {
            static const char *const sh[] = {"lsl", "lsr", "asr"};
            unsigned type = 0;

            if (kind[0] != '\0') {
                for (type = 0; type < 3 && strcmp(kind, sh[type]) != 0; ++type) {
                }
                if (type == 3 || amount < 0 || amount >= (sf ? 64 : 32)) {
                    return fail(a, "'%s': '%s' is no shift it takes", rq->mnemonic, rq->ops[at + 1]);
                }
            } else {
                amount = 0;
            }
            return word(a, (sf << 31) | (sub << 30) | (s << 29) | 0x0b000000u | (type << 22) | (rm.n << 16) |
                               ((uint32_t)amount << 10) | (rn.n << 5) | rd.n);
        }
    }
}

/* and, orr, eor, ands, bic, orn, eon, bics, tst, mvn, and a move of a
 * register. */
static int logical(a64_t *a) {
    static const struct {
        const char *name;
        unsigned opc;
        unsigned n;
    } ops[] = {{"and", 0, 0}, {"bic", 0, 1}, {"orr", 1, 0}, {"orn", 1, 1}, {"eor", 2, 0},
               {"eon", 2, 1}, {"ands", 3, 0}, {"bics", 3, 1}, {"tst", 3, 0}, {"mvn", 1, 1}};
    const as_arm_req_t *rq = a->rq;
    unsigned opc = 0;
    unsigned inv = 0;
    size_t at = 0;
    reg_t rd;
    reg_t rn;
    reg_t rm;
    unsigned sf;
    unsigned i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (strcmp(a->m, ops[i].name) == 0) {
            opc = ops[i].opc;
            inv = ops[i].n;
        }
    }
    memset(&rd, 0, sizeof(rd));
    memset(&rn, 0, sizeof(rn));
    if (strcmp(a->m, "tst") == 0) {
        if (gpr(a, at++, 0, &rn) != 0) {
            return -1;
        }
        rd = rn;
        rd.n = 31;
    } else if (strcmp(a->m, "mvn") == 0) {
        if (gpr(a, at++, 0, &rd) != 0) {
            return -1;
        }
        rn = rd;
        rn.n = 31;
    } else if (gpr(a, at++, opc == 3 || inv ? 0 : 2, &rd) != 0 || gpr(a, at++, 0, &rn) != 0) {
        return -1;
    }
    sf = rn.bits == 64 || rd.bits == 64 ? 1u : 0u;
    if (at >= rq->op_count) {
        return fail(a, "'%s' has too few operands", rq->mnemonic);
    }
    if (!is_reg_text(rq->ops[at])) {
        long long v;
        uint32_t enc;

        if (inv || at + 1 != rq->op_count || number(rq->ops[at], &v) != 0) {
            return fail(a, "'%s': '%s' is not a register or a number", rq->mnemonic, rq->ops[at]);
        }
        if (bitmask((uint64_t)v, sf ? 64 : 32, &enc) != 0) {
            return fail(a, "'%s': %lld is not a number it can hold: a run of ones, turned and repeated", rq->mnemonic,
                        v);
        }
        return word(a, (sf << 31) | (opc << 29) | 0x12000000u | (enc << 10) | (rn.n << 5) | rd.n);
    }
    if (gpr(a, at, 0, &rm) != 0) {
        return -1;
    }
    {
        static const char *const sh[] = {"lsl", "lsr", "asr", "ror"};
        char kind[8] = "";
        long long amount = 0;
        unsigned type = 0;

        if (at + 2 == rq->op_count) {
            if (shift_or_extend(rq->ops[at + 1], kind, sizeof(kind), &amount) != 0) {
                return fail(a, "'%s': '%s' is no shift", rq->mnemonic, rq->ops[at + 1]);
            }
            for (type = 0; type < 4 && strcmp(kind, sh[type]) != 0; ++type) {
            }
            if (type == 4 || amount < 0 || amount >= (sf ? 64 : 32)) {
                return fail(a, "'%s': '%s' is no shift it takes", rq->mnemonic, rq->ops[at + 1]);
            }
        } else if (at + 1 != rq->op_count) {
            return fail(a, "'%s' has too many operands", rq->mnemonic);
        }
        return word(a, (sf << 31) | (opc << 29) | 0x0a000000u | (type << 22) | (inv << 21) | (rm.n << 16) |
                           ((uint32_t)amount << 10) | (rn.n << 5) | rd.n);
    }
}

/* mov, movz, movn, movk. */
static int move(a64_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *m = a->m;
    reg_t rd;
    reg_t rm;
    long long v;
    unsigned sf;

    if (rq->op_count < 2 || gpr(a, 0, 2, &rd) != 0) {
        return fail(a, "'%s': a register and what is moved to it", rq->mnemonic);
    }
    sf = rd.bits == 64 ? 1u : 0u;
    if (strcmp(m, "mov") == 0 && parse_reg(rq->ops[1], &rm) == 0) {
        if (rq->op_count != 2 || rm.fp || rm.vec) {
            return fail(a, "'%s': two registers", rq->mnemonic);
        }
        /* To or from the stack pointer it is an addition of nothing;
         * otherwise an OR with the zero register. */
        if (rd.is_sp || rm.is_sp) {
            return word(a, (sf << 31) | 0x11000000u | (rm.n << 5) | rd.n);
        }
        return word(a, (sf << 31) | 0x2a0003e0u | (rm.n << 16) | rd.n);
    }
    if (rd.is_sp) {
        return fail(a, "'%s': the stack pointer is not moved to so", rq->mnemonic);
    }
    {
        const char *p = rq->ops[1];
        static const struct {
            const char *op;
            unsigned type;
            unsigned hw;
        } parts[] = {{":abs_g0:", R_AARCH64_MOVW_UABS_G0, 0},       {":abs_g0_nc:", R_AARCH64_MOVW_UABS_G0_NC, 0},
                     {":abs_g1:", R_AARCH64_MOVW_UABS_G1, 1},       {":abs_g1_nc:", R_AARCH64_MOVW_UABS_G1_NC, 1},
                     {":abs_g2:", R_AARCH64_MOVW_UABS_G2, 2},       {":abs_g2_nc:", R_AARCH64_MOVW_UABS_G2_NC, 2},
                     {":abs_g3:", R_AARCH64_MOVW_UABS_G3, 3}};
        size_t i;

        while (isspace((unsigned char)*p) || *p == '#') {
            p++;
        }
        for (i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
            size_t n = strlen(parts[i].op);

            if (strncasecmp(p, parts[i].op, n) == 0) {
                char name[128];
                long long addend;

                if (strcmp(m, "mov") == 0 || symbol_ref(p + n, name, sizeof(name), &addend) != 0 ||
                    fixup(a, parts[i].type, name, addend) != 0) {
                    return fail(a, "'%s': a part of a symbol's address", rq->mnemonic);
                }
                return word(a, (sf << 31) | (strcmp(m, "movk") == 0 ? 0x72800000u : 0x52800000u) |
                                   (parts[i].hw << 21) | rd.n);
            }
        }
    }
    if (number(rq->ops[1], &v) != 0) {
        return fail(a, "'%s': '%s' is not a register or a number", rq->mnemonic, rq->ops[1]);
    }
    if (strcmp(m, "mov") != 0) {
        unsigned opc = strcmp(m, "movn") == 0 ? 0u : strcmp(m, "movz") == 0 ? 2u : 3u;
        unsigned hw = 0;

        if (rq->op_count == 3) {
            char kind[8];
            long long amount;

            if (shift_or_extend(rq->ops[2], kind, sizeof(kind), &amount) != 0 || strcmp(kind, "lsl") != 0 ||
                amount < 0 || (amount % 16) != 0 || amount >= (sf ? 64 : 32)) {
                return fail(a, "'%s': shifted left by 0, 16, 32 or 48", rq->mnemonic);
            }
            hw = (unsigned)amount / 16;
        } else if (rq->op_count != 2) {
            return fail(a, "'%s' has too many operands", rq->mnemonic);
        }
        if (v < 0 || v > 0xffff) {
            return fail(a, "'%s': %lld is not a number of 16 bits", rq->mnemonic, v);
        }
        return word(a, (sf << 31) | (opc << 29) | 0x12800000u | (hw << 21) | ((uint32_t)v << 5) | rd.n);
    }
    if (rq->op_count != 2) {
        return fail(a, "'%s' has too many operands", rq->mnemonic);
    }
    {
        /* A number: sixteen bits of it and the rest zeros, or the rest
         * ones, or what the logical instructions hold. */
        uint64_t u = sf ? (uint64_t)v : (uint64_t)v & 0xffffffffu;
        unsigned limit = sf ? 4 : 2;
        unsigned hw;
        uint32_t enc;

        if (!sf && (v < -0x80000000ll || v > 0xffffffffll)) {
            return fail(a, "'%s': %lld is not a number of 32 bits", rq->mnemonic, v);
        }
        for (hw = 0; hw < limit; ++hw) {
            if ((u & ~((uint64_t)0xffff << (16 * hw))) == 0) {
                return word(a, (sf << 31) | 0x52800000u | (hw << 21) | ((uint32_t)(u >> (16 * hw)) << 5) | rd.n);
            }
        }
        {
            uint64_t n = sf ? ~u : ~u & 0xffffffffu;

            for (hw = 0; hw < limit; ++hw) {
                if ((n & ~((uint64_t)0xffff << (16 * hw))) == 0) {
                    return word(a, (sf << 31) | 0x12800000u | (hw << 21) | ((uint32_t)(n >> (16 * hw)) << 5) | rd.n);
                }
            }
        }
        if (bitmask(u, sf ? 64 : 32, &enc) == 0) {
            return word(a, (sf << 31) | 0x320003e0u | (enc << 10) | rd.n);
        }
        return fail(a, "'%s': %lld is not a number one instruction can move", rq->mnemonic, v);
    }
}

/* The shifts and bit fields that are one instruction with other
 * numbers, and the extensions. */
static int bitfield(a64_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *m = a->m;
    reg_t rd;
    reg_t rn;
    reg_t rm;
    long long x = 0;
    long long y = 0;
    unsigned sf;
    unsigned size;
    unsigned opc; /* 0 sbfm, 1 bfm, 2 ubfm */
    unsigned immr;
    unsigned imms;

    if (gpr(a, 0, 0, &rd) != 0 || gpr(a, 1, 0, &rn) != 0) {
        return -1;
    }
    sf = rd.bits == 64 ? 1u : 0u;
    size = sf ? 64u : 32u;
    if (strncmp(m, "uxt", 3) == 0 || strncmp(m, "sxt", 3) == 0) {
        if (rq->op_count != 2) {
            return fail(a, "'%s' takes two registers", rq->mnemonic);
        }
        opc = m[0] == 's' ? 0u : 2u;
        /* (The unsigned ones are of 32 bits whatever is written.) */
        if (m[0] == 'u') {
            sf = 0;
        }
        return word(a, (sf << 31) | (opc << 29) | 0x13000000u | (sf << 22) | (m[3] == 'b' ? 7u : m[3] == 'h' ? 15u : 31u) << 10 |
                           (rn.n << 5) | rd.n);
    }
    if ((strcmp(m, "lsl") == 0 || strcmp(m, "lsr") == 0 || strcmp(m, "asr") == 0 || strcmp(m, "ror") == 0) &&
        rq->op_count == 3 && parse_reg(rq->ops[2], &rm) == 0) {
        /* By a register. */
        return word(a, (sf << 31) | 0x1ac02000u | (rm.n << 16) |
                           ((m[0] == 'l' ? (m[2] == 'l' ? 0u : 1u) : m[0] == 'a' ? 2u : 3u) << 10) | (rn.n << 5) | rd.n);
    }
    if (rq->op_count < 3 || number(rq->ops[2], &x) != 0 || (rq->op_count == 4 && number(rq->ops[3], &y) != 0) ||
        rq->op_count > 4) {
        return fail(a, "'%s': two registers and a number or two", rq->mnemonic);
    }
    if (x < 0 || x >= size || y < 0 || y > size) {
        return fail(a, "'%s': a number past the register's %u bits", rq->mnemonic, size);
    }
    if (strcmp(m, "ror") == 0) {
        /* A rotation is an extraction from the register twice over. */
        return word(a, (sf << 31) | 0x13800000u | (sf << 22) | (rn.n << 16) | ((uint32_t)x << 10) | (rn.n << 5) | rd.n);
    }
    if (strcmp(m, "lsl") == 0) {
        opc = 2;
        immr = (unsigned)((size - x) % size);
        imms = (unsigned)(size - 1 - x);
    } else if (strcmp(m, "lsr") == 0 || strcmp(m, "asr") == 0) {
        opc = m[0] == 'a' ? 0u : 2u;
        immr = (unsigned)x;
        imms = size - 1;
    } else if (strcmp(m, "ubfx") == 0 || strcmp(m, "sbfx") == 0 || strcmp(m, "bfxil") == 0) {
        opc = m[0] == 's' ? 0u : m[0] == 'b' ? 1u : 2u;
        immr = (unsigned)x;
        imms = (unsigned)(x + y - 1);
    } else if (strcmp(m, "ubfiz") == 0 || strcmp(m, "sbfiz") == 0 || strcmp(m, "bfi") == 0) {
        opc = m[0] == 's' ? 0u : m[0] == 'b' ? 1u : 2u;
        immr = (unsigned)((size - x) % size);
        imms = (unsigned)(y - 1);
    } else {
        opc = m[0] == 's' ? 0u : m[0] == 'b' ? 1u : 2u;
        immr = (unsigned)x;
        imms = (unsigned)y;
    }
    if (imms >= size || immr >= size) {
        return fail(a, "'%s': a field past the register's %u bits", rq->mnemonic, size);
    }
    return word(a, (sf << 31) | (opc << 29) | 0x13000000u | (sf << 22) | (immr << 16) | (imms << 10) | (rn.n << 5) |
                       rd.n);
}

/* Three registers, or two, and a fixed pattern. */
static int register_ops(a64_t *a) {
    static const struct {
        const char *name;
        uint32_t pattern;
        unsigned count; /* registers */
        int ra_zero;    /* the fourth is the zero register */
    } ops[] = {
        {"udiv", 0x1ac00800u, 3, 0},  {"sdiv", 0x1ac00c00u, 3, 0},  {"madd", 0x1b000000u, 4, 0},
        {"msub", 0x1b008000u, 4, 0},  {"mul", 0x1b007c00u, 3, 1},   {"mneg", 0x1b00fc00u, 3, 1},
        {"adc", 0x1a000000u, 3, 0},   {"adcs", 0x3a000000u, 3, 0},  {"sbc", 0x5a000000u, 3, 0},
        {"sbcs", 0x7a000000u, 3, 0},  {"clz", 0x5ac01000u, 2, 0},   {"cls", 0x5ac01400u, 2, 0},
        {"rbit", 0x5ac00000u, 2, 0},  {"rev16", 0x5ac00400u, 2, 0}, {"rev", 0x5ac00800u, 2, 0},
        {"rev32", 0x5ac00800u, 2, 0}, {"smulh", 0x9b407c00u, 3, 1}, {"umulh", 0x9bc07c00u, 3, 1},
        {"smull", 0x9b207c00u, 3, 1}, {"umull", 0x9ba07c00u, 3, 1},
    };
    const as_arm_req_t *rq = a->rq;
    reg_t r[4];
    unsigned sf;
    size_t i;
    size_t k;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]) && strcmp(a->m, ops[i].name) != 0; ++i) {
    }
    if (i == sizeof(ops) / sizeof(ops[0])) {
        return 1;
    }
    if (rq->op_count != ops[i].count) {
        return fail(a, "'%s' takes %u registers", rq->mnemonic, ops[i].count);
    }
    memset(r, 0, sizeof(r));
    for (k = 0; k < ops[i].count; ++k) {
        if (gpr(a, k, 0, &r[k]) != 0) {
            return -1;
        }
    }
    sf = r[0].bits == 64 ? 1u : 0u;
    if ((ops[i].pattern & 0x80000000u) != 0) {
        sf = 0; /* (of 64 bits already, in the pattern) */
    }
    if (ops[i].count == 2) {
        uint32_t p = ops[i].pattern;

        /* A whole register reversed is one more in 64 bits. */
        if (strcmp(a->m, "rev") == 0 && sf) {
            p |= 0x400u;
        }
        return word(a, (sf << 31) | p | (r[1].n << 5) | r[0].n);
    }
    if (ops[i].count == 4) {
        return word(a, (sf << 31) | ops[i].pattern | (r[2].n << 16) | (r[3].n << 10) | (r[1].n << 5) | r[0].n);
    }
    return word(a, (sf << 31) | ops[i].pattern | (r[2].n << 16) | (r[1].n << 5) | r[0].n);
}

/* The selections by a condition, and their short names. */
static int conditional(a64_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *m = a->m;
    reg_t rd;
    reg_t rn;
    reg_t rm;
    unsigned c;
    unsigned sf;
    unsigned op;
    unsigned o2;

    if (strcmp(m, "ccmp") == 0 || strcmp(m, "ccmn") == 0) {
        long long nzcv;
        long long imm = 0;
        int is_imm;

        if (rq->op_count != 4 || gpr(a, 0, 0, &rn) != 0 || number(rq->ops[2], &nzcv) != 0 ||
            cond_of(rq->ops[3], &c) != 0 || nzcv < 0 || nzcv > 15) {
            return fail(a, "'%s': a register, a register or a number, the flags and a condition", rq->mnemonic);
        }
        is_imm = !is_reg_text(rq->ops[1]);
        if (is_imm ? (number(rq->ops[1], &imm) != 0 || imm < 0 || imm > 31) : gpr(a, 1, 0, &rm) != 0) {
            return fail(a, "'%s': a register, or a number of five bits", rq->mnemonic);
        }
        return word(a, ((rn.bits == 64 ? 1u : 0u) << 31) | (m[3] == 'p' ? 0x7a400000u : 0x3a400000u) |
                           ((is_imm ? (uint32_t)imm : rm.n) << 16) | (c << 12) | ((uint32_t)is_imm << 11) |
                           (rn.n << 5) | (uint32_t)nzcv);
    }
    if (gpr(a, 0, 0, &rd) != 0) {
        return -1;
    }
    sf = rd.bits == 64 ? 1u : 0u;
    if (strcmp(m, "cset") == 0 || strcmp(m, "csetm") == 0) {
        if (rq->op_count != 2 || cond_of(rq->ops[1], &c) != 0 || c >= 14) {
            return fail(a, "'%s': a register and a condition", rq->mnemonic);
        }
        return word(a, (sf << 31) | (m[4] == 'm' ? 0x5a9f03e0u : 0x1a9f07e0u) | ((c ^ 1u) << 12) | rd.n);
    }
    if (strcmp(m, "cinc") == 0 || strcmp(m, "cinv") == 0 || strcmp(m, "cneg") == 0) {
        if (rq->op_count != 3 || gpr(a, 1, 0, &rn) != 0 || cond_of(rq->ops[2], &c) != 0 || c >= 14) {
            return fail(a, "'%s': two registers and a condition", rq->mnemonic);
        }
        return word(a, (sf << 31) | (m[3] == 'c' ? 0x1a800400u : m[3] == 'v' ? 0x5a800000u : 0x5a800400u) |
                           (rn.n << 16) | ((c ^ 1u) << 12) | (rn.n << 5) | rd.n);
    }
    if (rq->op_count != 4 || gpr(a, 1, 0, &rn) != 0 || gpr(a, 2, 0, &rm) != 0 || cond_of(rq->ops[3], &c) != 0) {
        return fail(a, "'%s': three registers and a condition", rq->mnemonic);
    }
    op = strcmp(m, "csinv") == 0 || strcmp(m, "csneg") == 0;
    o2 = strcmp(m, "csinc") == 0 || strcmp(m, "csneg") == 0;
    return word(a, (sf << 31) | (op << 30) | 0x1a800000u | (rm.n << 16) | (c << 12) | (o2 << 10) | (rn.n << 5) | rd.n);
}

/* An address in brackets, and what may follow it. */
typedef struct {
    reg_t base;
    int has_imm;
    long long imm;
    char lo12[128]; /* `:lo12:sym`: the symbol */
    long long lo12_addend;
    int has_index;
    reg_t index;
    char extend[8];
    long long amount;
    int pre;
    int post;
} address_t;

static int address(a64_t *a, size_t at, address_t *m) {
    const as_arm_req_t *rq = a->rq;
    char buf[160];
    char *parts[3];
    size_t count = 0;
    char *close;
    char *p;

    memset(m, 0, sizeof(*m));
    m->amount = -1;
    text(rq->ops[at], buf, sizeof(buf));
    if (buf[0] != '[' || (close = strchr(buf, ']')) == NULL) {
        return fail(a, "'%s': '%s' is not an address", rq->mnemonic, rq->ops[at]);
    }
    if (close[1] == '!') {
        m->pre = 1;
    } else if (close[1] != '\0') {
        return fail(a, "'%s': what follows the ] of '%s'", rq->mnemonic, rq->ops[at]);
    }
    *close = '\0';
    for (p = buf + 1; count < 3;) {
        char *comma = strchr(p, ',');

        parts[count++] = p;
        if (comma == NULL) {
            break;
        }
        *comma = '\0';
        p = comma + 1;
    }
    if (parse_reg(parts[0], &m->base) != 0 || m->base.bits != 64 || m->base.is_zr || m->base.fp || m->base.vec) {
        return fail(a, "'%s': the base of an address is a register of 64 bits or the stack pointer", rq->mnemonic);
    }
    if (count >= 2) {
        if (parse_reg(parts[1], &m->index) == 0) {
            m->has_index = 1;
            if (count == 3 && shift_or_extend(parts[2], m->extend, sizeof(m->extend), &m->amount) != 0) {
                return fail(a, "'%s': '%s' is no extension of an index", rq->mnemonic, parts[2]);
            }
        } else {
            const char *q = parts[1];

            while (isspace((unsigned char)*q) || *q == '#') {
                q++;
            }
            if (count != 2) {
                return fail(a, "'%s': too much in an address", rq->mnemonic);
            }
            if (strncmp(q, ":lo12:", 6) == 0) {
                /* (The name in the case it was written in.) */
                const char *orig = strstr(rq->ops[at], ":lo12:");
                const char *alt = strstr(rq->ops[at], ":LO12:");
                char sym[160];
                size_t n = 0;

                orig = orig != NULL ? orig : alt;
                if (orig == NULL) {
                    return fail(a, "'%s': :lo12: and a symbol", rq->mnemonic);
                }
                for (orig += 6; *orig != '\0' && *orig != ']' && n + 1 < sizeof(sym); ++orig) {
                    sym[n++] = *orig;
                }
                sym[n] = '\0';
                if (symbol_ref(sym, m->lo12, sizeof(m->lo12), &m->lo12_addend) != 0) {
                    return fail(a, "'%s': :lo12: and a symbol", rq->mnemonic);
                }
            } else if (number(parts[1], &m->imm) != 0) {
                return fail(a, "'%s': the offset of an address is a number or a register", rq->mnemonic);
            }
            m->has_imm = 1;
        }
    }
    if (at + 1 < rq->op_count) {
        /* A number after the bracket is added after. */
        if (count != 1 || m->pre || at + 2 != rq->op_count || number(rq->ops[at + 1], &m->imm) != 0) {
            return fail(a, "'%s': what follows its address", rq->mnemonic);
        }
        m->post = 1;
        m->has_imm = 1;
    }
    return 0;
}

/* The loads and stores of one register. */
static int load_store(a64_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *m = a->m;
    int load = m[0] == 'l';
    int unscaled = strncmp(m + 2, "ur", 2) == 0;
    const char *suffix = m + (unscaled ? 4 : 3);
    int is_signed = suffix[0] == 's';
    unsigned size; /* log2 of the bytes moved */
    unsigned v = 0;
    unsigned opc;
    address_t ad;
    reg_t rt;

    if (rq->op_count < 2 || parse_reg(rq->ops[0], &rt) != 0 || rt.vec || rt.is_sp) {
        return fail(a, "'%s': a register and an address", rq->mnemonic);
    }
    if (is_signed) {
        suffix++;
    }
    if (rt.fp) {
        if (suffix[0] != '\0') {
            return fail(a, "'%s': such a register has its size in its name", rq->mnemonic);
        }
        v = 1;
        size = rt.bits == 8 ? 0u : rt.bits == 16 ? 1u : rt.bits == 32 ? 2u : rt.bits == 64 ? 3u : 4u;
        opc = load ? 1u : 0u;
        if (size == 4) {
            /* (128 bits is the size 0 with another bit set.) */
            opc |= 2u;
        }
    } else {
        size = suffix[0] == 'b' ? 0u : suffix[0] == 'h' ? 1u : suffix[0] == 'w' ? 2u : rt.bits == 64 ? 3u : 2u;
        if (is_signed) {
            opc = rt.bits == 64 ? 2u : 3u;
        } else {
            opc = load ? 1u : 0u;
            if (suffix[0] != '\0' && rt.bits != 32) {
                return fail(a, "'%s': a byte or a half word is of a register of 32 bits", rq->mnemonic);
            }
        }
    }
    if (strchr(rq->ops[1], '[') == NULL) {
        char name[128];
        long long addend;
        long long d;
        uint32_t w;

        /* A label: a word at a distance from here. */
        if (!load || rq->op_count != 2 || size < 2 || unscaled ||
            symbol_ref(rq->ops[1], name, sizeof(name), &addend) != 0) {
            return fail(a, "'%s': '%s' is not an address", rq->mnemonic, rq->ops[1]);
        }
        w = v ? ((size - 2) << 30) | 0x1c000000u
              : (is_signed ? 0x98000000u : size == 3 ? 0x58000000u : 0x18000000u);
        if (distance(a, name, addend, &d) == 0) {
            if ((d & 3) != 0 || d < -0x100000 || d > 0xffffc) {
                return fail(a, "'%s': '%s' is %lld bytes away, which it cannot reach", rq->mnemonic, name, d);
            }
            return word(a, w | ((((uint32_t)d >> 2) & 0x7ffffu) << 5) | rt.n);
        }
        if (fixup(a, R_AARCH64_LD_PREL_LO19, name, addend) != 0) {
            return -1;
        }
        return word(a, w | rt.n);
    }
    if (address(a, 1, &ad) != 0) {
        return -1;
    }
    {
        unsigned sz = size == 4 ? 0u : size;
        uint32_t head = (sz << 30) | 0x38000000u | (v << 26) | (opc << 22);

        if (ad.has_index) {
            static const char *const ext[] = {"uxtw", "lsl", "sxtw", "sxtx"};
            static const unsigned option[] = {2, 3, 6, 7};
            unsigned o = 3;
            unsigned s = 0;
            unsigned i;

            if (ad.pre || ad.post || unscaled) {
                return fail(a, "'%s': an index, and the base written back", rq->mnemonic);
            }
            if (ad.extend[0] != '\0') {
                for (i = 0; i < 4 && strcmp(ad.extend, ext[i]) != 0; ++i) {
                }
                if (i == 4) {
                    return fail(a, "'%s': '%s' is no extension of an index", rq->mnemonic, ad.extend);
                }
                o = option[i];
            } else if (ad.index.bits == 32) {
                return fail(a, "'%s': an index of 32 bits is extended, and says how", rq->mnemonic);
            }
            if (ad.amount > 0) {
                if ((unsigned)ad.amount != size) {
                    return fail(a, "'%s': an index is shifted by %u or not at all", rq->mnemonic, size);
                }
                s = 1;
            } else if (ad.amount == 0 && ad.extend[0] != '\0' && size == 0) {
                /* (`lsl #0` of a byte's index is said.) */
                s = strcmp(ad.extend, "lsl") == 0 ? 0u : 0u;
            }
            return word(a, head | (1u << 21) | (ad.index.n << 16) | (o << 13) | (s << 12) | 0x800u |
                               (ad.base.n << 5) | rt.n);
        }
        if (ad.lo12[0] != '\0') {
            static const unsigned types[] = {R_AARCH64_LDST8_ABS_LO12_NC, R_AARCH64_LDST16_ABS_LO12_NC,
                                             R_AARCH64_LDST32_ABS_LO12_NC, R_AARCH64_LDST64_ABS_LO12_NC,
                                             R_AARCH64_LDST128_ABS_LO12_NC};

            if (ad.pre || ad.post || unscaled || fixup(a, types[size], ad.lo12, ad.lo12_addend) != 0) {
                return fail(a, "'%s': :lo12: in an address that writes its base back", rq->mnemonic);
            }
            return word(a, head | 0x01000000u | (ad.base.n << 5) | rt.n);
        }
        if (ad.pre || ad.post) {
            if (ad.imm < -256 || ad.imm > 255) {
                return fail(a, "'%s': an offset of %lld, and it has nine bits for one", rq->mnemonic, ad.imm);
            }
            return word(a, head | (((uint32_t)ad.imm & 0x1ffu) << 12) | (ad.pre ? 0xc00u : 0x400u) |
                               (ad.base.n << 5) | rt.n);
        }
        /* The scaled form where the offset is a multiple of the size,
         * and not negative; the other where it is small. */
        if (!unscaled && ad.imm >= 0 && (ad.imm & ((1ll << size) - 1)) == 0 && (ad.imm >> size) <= 0xfff) {
            return word(a, head | 0x01000000u | ((uint32_t)(ad.imm >> size) << 10) | (ad.base.n << 5) | rt.n);
        }
        if (ad.imm < -256 || ad.imm > 255) {
            return fail(a, "'%s': an offset of %lld, which it cannot hold", rq->mnemonic, ad.imm);
        }
        return word(a, head | (((uint32_t)ad.imm & 0x1ffu) << 12) | (ad.base.n << 5) | rt.n);
    }
}

/* The loads and stores of two registers. */
static int load_store_pair(a64_t *a) {
    const as_arm_req_t *rq = a->rq;
    int load = a->m[0] == 'l';
    int sw = strcmp(a->m, "ldpsw") == 0;
    address_t ad;
    reg_t r1;
    reg_t r2;
    unsigned scale;
    unsigned opc;
    unsigned v = 0;
    long long imm;

    if (rq->op_count < 3 || parse_reg(rq->ops[0], &r1) != 0 || parse_reg(rq->ops[1], &r2) != 0 || r1.vec || r2.vec ||
        r1.bits != r2.bits || r1.fp != r2.fp || r1.is_sp || r2.is_sp) {
        return fail(a, "'%s': two registers of one size and an address", rq->mnemonic);
    }
    if (address(a, 2, &ad) != 0) {
        return -1;
    }
    if (ad.has_index || ad.lo12[0] != '\0') {
        return fail(a, "'%s': its address is a base and a number", rq->mnemonic);
    }
    if (r1.fp) {
        if (r1.bits < 32) {
            return fail(a, "'%s': registers of 32, 64 or 128 bits", rq->mnemonic);
        }
        v = 1;
        opc = r1.bits == 32 ? 0u : r1.bits == 64 ? 1u : 2u;
        scale = 2 + opc;
    } else {
        opc = sw ? 1u : r1.bits == 64 ? 2u : 0u;
        scale = sw ? 2u : r1.bits == 64 ? 3u : 2u;
    }
    imm = ad.imm;
    if ((imm & ((1ll << scale) - 1)) != 0 || (imm >> scale) < -64 || (imm >> scale) > 63) {
        return fail(a, "'%s': an offset of %lld, which it cannot hold", rq->mnemonic, imm);
    }
    return word(a, (opc << 30) | 0x28000000u | (v << 26) | ((ad.post ? 1u : ad.pre ? 3u : 2u) << 23) |
                       ((uint32_t)load << 22) | ((((uint32_t)(imm >> scale)) & 0x7fu) << 15) | (r2.n << 10) |
                       (ad.base.n << 5) | r1.n);
}

/* A branch's field: the distance to a label of this section in words,
 * or nothing and a relocation. */
static int branch_field(a64_t *a, const char *target, unsigned bits, unsigned reloc, uint32_t *field) {
    char name[128];
    long long addend;
    long long d;

    if (symbol_ref(target, name, sizeof(name), &addend) != 0) {
        return fail(a, "'%s': '%s' is not where a branch can go", a->rq->mnemonic, target);
    }
    if (distance(a, name, addend, &d) == 0) {
        long long limit = 1ll << (bits + 1);

        if ((d & 3) != 0) {
            return fail(a, "'%s': '%s' is not at a multiple of four bytes from here", a->rq->mnemonic, name);
        }
        if (d < -limit || d >= limit) {
            return fail(a, "'%s': '%s' is %lld bytes away, which it cannot reach", a->rq->mnemonic, name, d);
        }
        *field = ((uint32_t)(d >> 2)) & ((1u << bits) - 1);
        return 0;
    }
    *field = 0;
    return fixup(a, reloc, name, addend);
}

static int branch(a64_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *m = a->m;
    uint32_t f;
    reg_t r;
    unsigned c;

    if (strcmp(m, "ret") == 0) {
        /* (With no register, the one a call leaves its return in.) */
        if (rq->op_count == 0) {
            return word(a, 0xd65f03c0u);
        }
        if (rq->op_count != 1 || gpr(a, 0, 0, &r) != 0 || r.bits != 64) {
            return fail(a, "'%s': a register of 64 bits, or none", rq->mnemonic);
        }
        return word(a, 0xd65f0000u | (r.n << 5));
    }
    if (strcmp(m, "br") == 0 || strcmp(m, "blr") == 0) {
        if (rq->op_count != 1 || gpr(a, 0, 0, &r) != 0 || r.bits != 64) {
            return fail(a, "'%s': a register of 64 bits", rq->mnemonic);
        }
        return word(a, (m[1] == 'l' ? 0xd63f0000u : 0xd61f0000u) | (r.n << 5));
    }
    if (strcmp(m, "b") == 0 || strcmp(m, "bl") == 0) {
        if (rq->op_count != 1 ||
            branch_field(a, rq->ops[0], 26, m[1] == 'l' ? R_AARCH64_CALL26 : R_AARCH64_JUMP26, &f) != 0) {
            return a->errbuf != NULL && a->errbuf[0] != '\0' ? -1 : fail(a, "'%s': a label", rq->mnemonic);
        }
        return word(a, (m[1] == 'l' ? 0x94000000u : 0x14000000u) | f);
    }
    if (strcmp(m, "cbz") == 0 || strcmp(m, "cbnz") == 0) {
        if (rq->op_count != 2 || gpr(a, 0, 0, &r) != 0 ||
            branch_field(a, rq->ops[1], 19, R_AARCH64_CONDBR19, &f) != 0) {
            return a->errbuf != NULL && a->errbuf[0] != '\0' ? -1 : fail(a, "'%s': a register and a label", rq->mnemonic);
        }
        return word(a, ((r.bits == 64 ? 1u : 0u) << 31) | (m[2] == 'n' ? 0x35000000u : 0x34000000u) | (f << 5) | r.n);
    }
    if (strcmp(m, "tbz") == 0 || strcmp(m, "tbnz") == 0) {
        long long bit;

        if (rq->op_count != 3 || gpr(a, 0, 0, &r) != 0 || number(rq->ops[1], &bit) != 0 || bit < 0 ||
            bit >= (long long)r.bits || branch_field(a, rq->ops[2], 14, R_AARCH64_TSTBR14, &f) != 0) {
            return a->errbuf != NULL && a->errbuf[0] != '\0' ? -1
                                                              : fail(a, "'%s': a register, a bit of it and a label",
                                                                     rq->mnemonic);
        }
        return word(a, (((uint32_t)bit >> 5) << 31) | (m[2] == 'n' ? 0x37000000u : 0x36000000u) |
                           (((uint32_t)bit & 31u) << 19) | (f << 5) | r.n);
    }
    /* b.eq, and beq for it. */
    if (cond_of(m[1] == '.' ? m + 2 : m + 1, &c) != 0) {
        return 1;
    }
    if (rq->op_count != 1 || branch_field(a, rq->ops[0], 19, R_AARCH64_CONDBR19, &f) != 0) {
        return a->errbuf != NULL && a->errbuf[0] != '\0' ? -1 : fail(a, "'%s': a label", rq->mnemonic);
    }
    return word(a, 0x54000000u | (f << 5) | c);
}

/* adr and adrp. */
static int address_of(a64_t *a) {
    const as_arm_req_t *rq = a->rq;
    int page = a->m[3] == 'p';
    char name[128];
    long long addend;
    long long d;
    reg_t rd;

    if (rq->op_count != 2 || gpr(a, 0, 0, &rd) != 0 || rd.bits != 64 ||
        symbol_ref(rq->ops[1], name, sizeof(name), &addend) != 0) {
        return fail(a, "'%s': a register of 64 bits and a label", rq->mnemonic);
    }
    if (!page && distance(a, name, addend, &d) == 0) {
        if (d < -0x100000 || d > 0xfffff) {
            return fail(a, "'%s': '%s' is %lld bytes away, which it cannot reach", rq->mnemonic, name, d);
        }
        return word(a, 0x10000000u | (((uint32_t)d & 3u) << 29) | ((((uint32_t)d >> 2) & 0x7ffffu) << 5) | rd.n);
    }
    if (fixup(a, page ? R_AARCH64_ADR_PREL_PG_HI21 : R_AARCH64_ADR_PREL_LO21, name, addend) != 0) {
        return -1;
    }
    return word(a, (page ? 0x90000000u : 0x10000000u) | rd.n);
}

/* The system registers that have names here; any may be written
 * s<op0>_<op1>_c<n>_c<m>_<op2>. */
static const struct {
    const char *name;
    unsigned op0, op1, crn, crm, op2;
} sysregs[] = {
    {"nzcv", 3, 3, 4, 2, 0},         {"daif", 3, 3, 4, 2, 1},         {"currentel", 3, 0, 4, 2, 2},
    {"spsel", 3, 0, 4, 2, 0},        {"fpcr", 3, 3, 4, 4, 0},         {"fpsr", 3, 3, 4, 4, 1},
    {"tpidr_el0", 3, 3, 13, 0, 2},   {"tpidrro_el0", 3, 3, 13, 0, 3}, {"tpidr_el1", 3, 0, 13, 0, 4},
    {"tpidr_el2", 3, 4, 13, 0, 2},   {"sctlr_el1", 3, 0, 1, 0, 0},    {"sctlr_el2", 3, 4, 1, 0, 0},
    {"sctlr_el3", 3, 6, 1, 0, 0},    {"actlr_el1", 3, 0, 1, 0, 1},    {"cpacr_el1", 3, 0, 1, 0, 2},
    {"ttbr0_el1", 3, 0, 2, 0, 0},    {"ttbr1_el1", 3, 0, 2, 0, 1},    {"tcr_el1", 3, 0, 2, 0, 2},
    {"spsr_el1", 3, 0, 4, 0, 0},     {"elr_el1", 3, 0, 4, 0, 1},      {"sp_el0", 3, 0, 4, 1, 0},
    {"spsr_el2", 3, 4, 4, 0, 0},     {"elr_el2", 3, 4, 4, 0, 1},      {"sp_el1", 3, 4, 4, 1, 0},
    {"spsr_el3", 3, 6, 4, 0, 0},     {"elr_el3", 3, 6, 4, 0, 1},      {"esr_el1", 3, 0, 5, 2, 0},
    {"esr_el2", 3, 4, 5, 2, 0},      {"far_el1", 3, 0, 6, 0, 0},      {"far_el2", 3, 4, 6, 0, 0},
    {"par_el1", 3, 0, 7, 4, 0},      {"mair_el1", 3, 0, 10, 2, 0},    {"vbar_el1", 3, 0, 12, 0, 0},
    {"vbar_el2", 3, 4, 12, 0, 0},    {"vbar_el3", 3, 6, 12, 0, 0},    {"contextidr_el1", 3, 0, 13, 0, 1},
    {"cntfrq_el0", 3, 3, 14, 0, 0},  {"cntpct_el0", 3, 3, 14, 0, 1},  {"cntvct_el0", 3, 3, 14, 0, 2},
    {"cntkctl_el1", 3, 0, 14, 1, 0}, {"cntp_tval_el0", 3, 3, 14, 2, 0}, {"cntp_ctl_el0", 3, 3, 14, 2, 1},
    {"cntp_cval_el0", 3, 3, 14, 2, 2}, {"cntv_tval_el0", 3, 3, 14, 3, 0}, {"cntv_ctl_el0", 3, 3, 14, 3, 1},
    {"cntv_cval_el0", 3, 3, 14, 3, 2}, {"midr_el1", 3, 0, 0, 0, 0},   {"mpidr_el1", 3, 0, 0, 0, 5},
    {"revidr_el1", 3, 0, 0, 0, 6},   {"id_aa64pfr0_el1", 3, 0, 0, 4, 0}, {"id_aa64isar0_el1", 3, 0, 0, 6, 0},
    {"id_aa64mmfr0_el1", 3, 0, 0, 7, 0}, {"ctr_el0", 3, 3, 0, 0, 1},  {"dczid_el0", 3, 3, 0, 0, 7},
    {"hcr_el2", 3, 4, 1, 1, 0},      {"scr_el3", 3, 6, 1, 1, 0},      {"vttbr_el2", 3, 4, 2, 1, 0},
    {"vtcr_el2", 3, 4, 2, 1, 2},     {"ttbr0_el2", 3, 4, 2, 0, 0},    {"tcr_el2", 3, 4, 2, 0, 2},
    {"mdscr_el1", 2, 0, 0, 2, 2},    {"cptr_el2", 3, 4, 1, 1, 2},     {"hpfar_el2", 3, 4, 6, 0, 4},
    {"cnthctl_el2", 3, 4, 14, 1, 0}, {"cntvoff_el2", 3, 4, 14, 0, 3},
};

/* The sixteen bits of a system register in an instruction. */
static int sysreg(const char *s, uint32_t *bits) {
    char buf[32];
    unsigned o0, o1, cn, cm, o2;
    size_t i;

    text(s, buf, sizeof(buf));
    for (i = 0; i < sizeof(sysregs) / sizeof(sysregs[0]); ++i) {
        if (strcmp(buf, sysregs[i].name) == 0) {
            *bits = ((sysregs[i].op0 & 1u) << 14) | (sysregs[i].op1 << 11) | (sysregs[i].crn << 7) |
                    (sysregs[i].crm << 3) | sysregs[i].op2;
            return 0;
        }
    }
    {
        char extra;

        if (sscanf(buf, "s%u_%u_c%u_c%u_%u%c", &o0, &o1, &cn, &cm, &o2, &extra) == 5 && (o0 == 2 || o0 == 3) &&
            o1 < 8 && cn < 16 && cm < 16 && o2 < 8) {
            *bits = ((o0 & 1u) << 14) | (o1 << 11) | (cn << 7) | (cm << 3) | o2;
            return 0;
        }
    }
    return -1;
}

static int system_instruction(a64_t *a) {
    static const struct {
        const char *name;
        uint32_t w;
    } fixed[] = {{"nop", 0xd503201fu},  {"yield", 0xd503203fu}, {"wfe", 0xd503205fu},  {"wfi", 0xd503207fu},
                 {"sev", 0xd503209fu},  {"sevl", 0xd50320bfu},  {"eret", 0xd69f03e0u}, {"drps", 0xd6bf03e0u},
                 {"isb", 0xd5033fdfu}};
    static const struct {
        const char *name;
        uint32_t w;
    } exc[] = {{"svc", 0xd4000001u}, {"hvc", 0xd4000002u}, {"smc", 0xd4000003u}, {"brk", 0xd4200000u},
               {"hlt", 0xd4400000u}};
    const as_arm_req_t *rq = a->rq;
    const char *m = a->m;
    long long n;
    uint32_t bits;
    reg_t r;
    size_t i;

    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        if (strcmp(m, fixed[i].name) == 0) {
            if (rq->op_count != 0) {
                return fail(a, "'%s' takes no operand", rq->mnemonic);
            }
            return word(a, fixed[i].w);
        }
    }
    for (i = 0; i < sizeof(exc) / sizeof(exc[0]); ++i) {
        if (strcmp(m, exc[i].name) == 0) {
            if (rq->op_count != 1 || number(rq->ops[0], &n) != 0 || n < 0 || n > 0xffff) {
                return fail(a, "'%s': a number of 16 bits", rq->mnemonic);
            }
            return word(a, exc[i].w | ((uint32_t)n << 5));
        }
    }
    if (strcmp(m, "hint") == 0) {
        if (rq->op_count != 1 || number(rq->ops[0], &n) != 0 || n < 0 || n > 127) {
            return fail(a, "'%s': a number of seven bits", rq->mnemonic);
        }
        return word(a, 0xd503201fu | ((uint32_t)n << 5));
    }
    if (strcmp(m, "dsb") == 0 || strcmp(m, "dmb") == 0) {
        static const char *const opts[] = {NULL,    "oshld", "oshst", "osh", NULL,   "nshld", "nshst", "nsh",
                                           NULL,    "ishld", "ishst", "ish", NULL,   "ld",    "st",    "sy"};
        char buf[16];

        if (rq->op_count != 1) {
            return fail(a, "'%s': which barrier", rq->mnemonic);
        }
        text(rq->ops[0], buf, sizeof(buf));
        for (i = 0; i < 16; ++i) {
            if (opts[i] != NULL && strcmp(buf, opts[i]) == 0) {
                break;
            }
        }
        if (i == 16) {
            if (number(rq->ops[0], &n) != 0 || n < 0 || n > 15) {
                return fail(a, "'%s': '%s' is no barrier", rq->mnemonic, rq->ops[0]);
            }
            i = (size_t)n;
        }
        return word(a, (m[1] == 's' ? 0xd503309fu : 0xd50330bfu) | ((uint32_t)i << 8));
    }
    if (strcmp(m, "mrs") == 0) {
        if (rq->op_count != 2 || gpr(a, 0, 0, &r) != 0 || r.bits != 64 || sysreg(rq->ops[1], &bits) != 0) {
            return fail(a, "'%s': a register of 64 bits and a system register", rq->mnemonic);
        }
        return word(a, 0xd5300000u | (bits << 5) | r.n);
    }
    if (strcmp(m, "msr") == 0) {
        char buf[16];

        if (rq->op_count != 2) {
            return fail(a, "'%s': a system register and a value", rq->mnemonic);
        }
        text(rq->ops[0], buf, sizeof(buf));
        if (!is_reg_text(rq->ops[1])) {
            /* A field of the processor's state, and a number. */
            unsigned op1;
            unsigned op2;

            if (strcmp(buf, "spsel") == 0) {
                op1 = 0;
                op2 = 5;
            } else if (strcmp(buf, "daifset") == 0) {
                op1 = 3;
                op2 = 6;
            } else if (strcmp(buf, "daifclr") == 0) {
                op1 = 3;
                op2 = 7;
            } else {
                return fail(a, "'%s': '%s' takes no number", rq->mnemonic, rq->ops[0]);
            }
            if (number(rq->ops[1], &n) != 0 || n < 0 || n > 15) {
                return fail(a, "'%s': a number of four bits", rq->mnemonic);
            }
            return word(a, 0xd500401fu | (op1 << 16) | ((uint32_t)n << 8) | (op2 << 5));
        }
        if (gpr(a, 1, 0, &r) != 0 || r.bits != 64 || sysreg(rq->ops[0], &bits) != 0) {
            return fail(a, "'%s': a system register and a register of 64 bits", rq->mnemonic);
        }
        return word(a, 0xd5100000u | (bits << 5) | r.n);
    }
    return 1;
}

int as_a64_encode(const as_arm_req_t *rq, as_arm_out_t *out, char *errbuf, size_t errbuf_sz) {
    a64_t a;
    const char *m;
    reg_t first;
    int have_first;
    int rc;

    memset(&a, 0, sizeof(a));
    memset(out, 0, sizeof(*out));
    a.rq = rq;
    a.out = out;
    a.errbuf = errbuf;
    a.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz != 0) {
        errbuf[0] = '\0';
    }
    text(rq->mnemonic, a.m, sizeof(a.m));
    m = a.m;
    have_first = rq->op_count != 0 && parse_reg(rq->ops[0], &first) == 0;

    /* What is of the vector registers is the other file's: by the
     * name, or by the first register. */
    if (m[0] == 'f' || strcmp(m, "scvtf") == 0 || strcmp(m, "ucvtf") == 0 || strcmp(m, "ld1") == 0 ||
        strcmp(m, "st1") == 0 || strcmp(m, "dup") == 0 || strcmp(m, "ins") == 0 || strcmp(m, "umov") == 0 ||
        strcmp(m, "smov") == 0 || strcmp(m, "movi") == 0 || (have_first && first.vec) ||
        (have_first && first.fp && strncmp(m, "ld", 2) != 0 && strncmp(m, "st", 2) != 0) ||
        (strcmp(m, "mov") == 0 && rq->op_count == 2 && strchr(rq->ops[1], '.') != NULL)) {
        return as_a64_simd(rq, m, out, errbuf, errbuf_sz);
    }
    if (strcmp(m, "add") == 0 || strcmp(m, "adds") == 0 || strcmp(m, "sub") == 0 || strcmp(m, "subs") == 0 ||
        strcmp(m, "cmp") == 0 || strcmp(m, "cmn") == 0 || strcmp(m, "neg") == 0 || strcmp(m, "negs") == 0) {
        return add_sub(&a);
    }
    if (strcmp(m, "and") == 0 || strcmp(m, "ands") == 0 || strcmp(m, "orr") == 0 || strcmp(m, "eor") == 0 ||
        strcmp(m, "bic") == 0 || strcmp(m, "bics") == 0 || strcmp(m, "orn") == 0 || strcmp(m, "eon") == 0 ||
        strcmp(m, "tst") == 0 || strcmp(m, "mvn") == 0) {
        return logical(&a);
    }
    if (strcmp(m, "mov") == 0 || strcmp(m, "movz") == 0 || strcmp(m, "movn") == 0 || strcmp(m, "movk") == 0) {
        return move(&a);
    }
    if (strcmp(m, "lsl") == 0 || strcmp(m, "lsr") == 0 || strcmp(m, "asr") == 0 || strcmp(m, "ror") == 0 ||
        strcmp(m, "ubfm") == 0 || strcmp(m, "sbfm") == 0 || strcmp(m, "bfm") == 0 || strcmp(m, "ubfx") == 0 ||
        strcmp(m, "sbfx") == 0 || strcmp(m, "bfxil") == 0 || strcmp(m, "ubfiz") == 0 || strcmp(m, "sbfiz") == 0 ||
        strcmp(m, "bfi") == 0 || strcmp(m, "uxtb") == 0 || strcmp(m, "uxth") == 0 || strcmp(m, "sxtb") == 0 ||
        strcmp(m, "sxth") == 0 || strcmp(m, "sxtw") == 0) {
        return bitfield(&a);
    }
    if (strncmp(m, "cs", 2) == 0 || strncmp(m, "cc", 2) == 0 || strncmp(m, "ci", 2) == 0 || strcmp(m, "cneg") == 0) {
        return conditional(&a);
    }
    if (strcmp(m, "adr") == 0 || strcmp(m, "adrp") == 0) {
        return address_of(&a);
    }
    if (strcmp(m, "ldp") == 0 || strcmp(m, "stp") == 0 || strcmp(m, "ldpsw") == 0) {
        return load_store_pair(&a);
    }
    if (strncmp(m, "ldr", 3) == 0 || strncmp(m, "str", 3) == 0 || strncmp(m, "ldur", 4) == 0 ||
        strncmp(m, "stur", 4) == 0) {
        return load_store(&a);
    }
    rc = register_ops(&a);
    if (rc <= 0) {
        return rc;
    }
    rc = system_instruction(&a);
    if (rc <= 0) {
        return rc;
    }
    rc = m[0] == 'b' || strcmp(m, "ret") == 0 || strcmp(m, "cbz") == 0 || strcmp(m, "cbnz") == 0 ||
                 strcmp(m, "tbz") == 0 || strcmp(m, "tbnz") == 0
             ? branch(&a)
             : 1;
    if (rc <= 0) {
        return rc;
    }
    return fail(&a, "'%s' is not an instruction of AArch64 that this assembler has", rq->mnemonic);
}
