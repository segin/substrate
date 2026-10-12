#include "as_arm_int.h"

#include "elfobj.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * Thumb, as ARMv7 has it (Thumb-2).  See as_arm.h.
 *
 * An instruction is 16 bits or 32.  Where both can say what is written
 * the short one is used, unless `.w` is on the mnemonic; `.n` asks for
 * the short one and is refused where there is none.  The short forms of
 * the data-processing instructions set the flags when they are outside
 * an IT block and leave them when they are inside one, so whether one
 * may be used is a matter of the `s` on the mnemonic and of where the
 * instruction stands.
 *
 * A branch is as long as it is written to be and is not shortened by
 * how near its label turns out to be: 32 bits, or 16 with `.n`.
 */

#define text as_arm_text
#define number as_arm_number
#define symbol_ref as_arm_symbol
#define core_reg as_arm_core_reg

#define COND_AL 14u

typedef struct {
    const as_arm_req_t *rq;
    as_arm_out_t *out;
    char *errbuf;
    size_t errbuf_sz;
    const char *base;
    int s;
    unsigned cond;
    int wide;   /* `.w` */
    int narrow; /* `.n` */
    int in_it;
} th_t;

static int fail(th_t *t, const char *fmt, ...) {
    va_list ap;

    if (t->errbuf != NULL && t->errbuf_sz != 0) {
        va_start(ap, fmt);
        vsnprintf(t->errbuf, t->errbuf_sz, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int emit16(th_t *t, uint32_t w) {
    if (t->wide) {
        return fail(t, "'%s': there is no form of 32 bits of it with these operands", t->rq->mnemonic);
    }
    t->out->bytes[0] = (uint8_t)w;
    t->out->bytes[1] = (uint8_t)(w >> 8);
    t->out->len = 2;
    return 0;
}

/* The two halves, the first first. */
static int emit32(th_t *t, uint32_t w) {
    if (t->narrow) {
        return fail(t, "'%s': there is no form of 16 bits of it with these operands", t->rq->mnemonic);
    }
    t->out->bytes[0] = (uint8_t)(w >> 16);
    t->out->bytes[1] = (uint8_t)(w >> 24);
    t->out->bytes[2] = (uint8_t)w;
    t->out->bytes[3] = (uint8_t)(w >> 8);
    t->out->len = 4;
    return 0;
}

static int fixup(th_t *t, unsigned type, const char *symbol) {
    as_arm_fixup_t *f = &t->out->fixups[0];

    if (strlen(symbol) >= sizeof(f->symbol)) {
        return fail(t, "'%s': a symbol's name too long", t->rq->mnemonic);
    }
    f->offset = 0;
    f->type = type;
    memcpy(f->symbol, symbol, strlen(symbol) + 1);
    f->addend = 0;
    t->out->fixup_count = 1;
    return 0;
}

/* Whether the short forms that set the flags outside an IT block, and
 * do not inside one, do what is written. */
static int flags_as_narrow(const th_t *t) {
    return !t->wide && (t->in_it ? !t->s : t->s);
}

/*
 * A number as the 32-bit instructions hold one: a byte, a byte in two
 * or in all four places, or a byte with its top bit set turned right.
 * 0 with the twelve bits i:imm3:imm8.
 */
static int thumb_imm(uint32_t v, uint32_t *enc) {
    unsigned rot;
    uint32_t b = v & 0xffu;

    if (v <= 0xffu) {
        *enc = v;
        return 0;
    }
    if (v == (b | (b << 16))) {
        *enc = 0x100u | b;
        return 0;
    }
    b = (v >> 8) & 0xffu;
    if (v == ((b << 8) | (b << 24))) {
        *enc = 0x200u | b;
        return 0;
    }
    if (v == (b | (b << 8) | (b << 16) | (b << 24))) {
        *enc = 0x300u | b;
        return 0;
    }
    for (rot = 8; rot < 32; ++rot) {
        uint32_t r = (v << rot) | (v >> (32 - rot));

        if (r >= 0x80u && r <= 0xffu) {
            *enc = (rot << 7) | (r & 0x7fu);
            return 0;
        }
    }
    return -1;
}

/* Those twelve bits where a 32-bit instruction has them. */
static uint32_t imm12_bits(uint32_t enc) {
    return ((enc & 0x800u) << 15) | ((enc & 0x700u) << 4) | (enc & 0xffu);
}

static int is_imm(const char *s) {
    char buf[8];

    text(s, buf, sizeof(buf));
    return buf[0] == '#' || isdigit((unsigned char)buf[0]) || buf[0] == '-' || buf[0] == '~' || buf[0] == '(';
}

/* `lsl #n` and its like: the kind (0 to 3; rrx is 3 by nothing) and the
 * amount as the instruction holds it. */
static int shift_of(th_t *t, const char *s, unsigned *type, unsigned *amount) {
    static const char *const names[] = {"lsl", "lsr", "asr", "ror"};
    char buf[32];
    long long n;
    unsigned k;

    text(s, buf, sizeof(buf));
    if (strcmp(buf, "rrx") == 0) {
        *type = 3;
        *amount = 0;
        return 0;
    }
    for (k = 0; k < 4 && strncmp(buf, names[k], 3) != 0; ++k) {
    }
    if (k == 4 || number(buf + 3, &n) != 0) {
        return fail(t, "'%s': '%s' is no shift by a number", t->rq->mnemonic, s);
    }
    if ((k == 1 || k == 2) && n == 32) {
        n = 0;
    } else if (n < 0 || n > 31) {
        return fail(t, "'%s': a shift of %lld", t->rq->mnemonic, n);
    } else if (n == 0) {
        k = 0;
    }
    *type = k;
    *amount = (unsigned)n;
    return 0;
}

static int low(int r) {
    return r >= 0 && r < 8;
}

/* The operation's four bits in a 32-bit instruction. */
static int wide_op(const char *b, unsigned *op) {
    static const struct {
        const char *name;
        unsigned op;
    } ops[] = {{"and", 0}, {"tst", 0}, {"bic", 1}, {"orr", 2},  {"mov", 2},  {"orn", 3},  {"mvn", 3}, {"eor", 4},
               {"teq", 4}, {"add", 8}, {"cmn", 8}, {"adc", 10}, {"sbc", 11}, {"sub", 13}, {"cmp", 13}, {"rsb", 14}};
    size_t i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (strcmp(b, ops[i].name) == 0) {
            *op = ops[i].op;
            return 0;
        }
    }
    return -1;
}

/* The operation's four bits in the 16-bit instructions of two low
 * registers. */
static int narrow_op(const char *b, unsigned *op) {
    static const struct {
        const char *name;
        unsigned op;
    } ops[] = {{"and", 0}, {"eor", 1}, {"lsl", 2},  {"lsr", 3},  {"asr", 4},  {"adc", 5},  {"sbc", 6},
               {"ror", 7}, {"tst", 8}, {"cmn", 11}, {"orr", 12}, {"mul", 13}, {"bic", 14}, {"mvn", 15}};
    size_t i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (strcmp(b, ops[i].name) == 0) {
            *op = ops[i].op;
            return 0;
        }
    }
    return -1;
}

/* and, eor, sub, rsb, add, adc, sbc, orr, orn, bic; tst, teq, cmp, cmn;
 * mov, mvn. */
static int data_processing(th_t *t) {
    const as_arm_req_t *rq = t->rq;
    const char *b = t->base;
    int compare = strcmp(b, "tst") == 0 || strcmp(b, "teq") == 0 || strcmp(b, "cmp") == 0 || strcmp(b, "cmn") == 0;
    int move = strcmp(b, "mov") == 0 || strcmp(b, "mvn") == 0;
    int is_add = strcmp(b, "add") == 0;
    int is_sub = strcmp(b, "sub") == 0;
    int commutes = strcmp(b, "and") == 0 || strcmp(b, "eor") == 0 || strcmp(b, "orr") == 0 || strcmp(b, "adc") == 0;
    unsigned s = (unsigned)(t->s || compare);
    unsigned op = 0;
    unsigned nop = 0;
    size_t at = 0;
    int rd = 15;
    int rn = 15;
    int rm;

    (void)wide_op(b, &op);
    if (rq->op_count < 2) {
        return fail(t, "'%s' has too few operands", rq->mnemonic);
    }
    if (!compare) {
        rd = core_reg(rq->ops[at++]);
    }
    if (!move) {
        /* (One register before the last operand is both.) */
        if (compare || (rq->op_count - at >= 2 && !is_imm(rq->ops[at]) && core_reg(rq->ops[at]) >= 0 &&
                        (rq->op_count - at >= 3 || is_imm(rq->ops[at + 1]) || core_reg(rq->ops[at + 1]) >= 0))) {
            rn = core_reg(rq->ops[at++]);
        } else {
            rn = rd;
        }
    }
    if ((!compare && rd < 0) || (!move && rn < 0) || at >= rq->op_count) {
        return fail(t, "'%s': a register is not one", rq->mnemonic);
    }
    if (is_imm(rq->ops[at])) {
        long long v;
        uint32_t enc;
        uint32_t u;

        if (at + 1 != rq->op_count || number(rq->ops[at], &v) != 0) {
            return fail(t, "'%s': '%s' is not a number", rq->mnemonic, rq->ops[at]);
        }
        if (v < -0x80000000ll || v > 0xffffffffll) {
            return fail(t, "'%s': %lld is not a number of 32 bits", rq->mnemonic, v);
        }
        u = (uint32_t)v;
        if (is_add || is_sub) {
            int sub = is_sub;

            if (v < 0) {
                v = -v;
                u = (uint32_t)v;
                sub = !sub;
                op = sub ? 13 : 8;
            }
            if (!t->wide && !s && rd == 13 && rn == 13 && (v & 3) == 0 && v <= 508) {
                return emit16(t, (sub ? 0xb080u : 0xb000u) | (uint32_t)(v >> 2));
            }
            if (!t->wide && !s && !sub && low(rd) && rn == 13 && (v & 3) == 0 && v <= 1020) {
                return emit16(t, 0xa800u | ((uint32_t)rd << 8) | (uint32_t)(v >> 2));
            }
            if (flags_as_narrow(t) && low(rd) && low(rn)) {
                /* Three bits with two registers; eight with one --
                 * which is used for two that are one only where the
                 * number does not fit in three. */
                if (v <= 7 && (rd != rn || rq->op_count == 3)) {
                    return emit16(t, (sub ? 0x1e00u : 0x1c00u) | ((uint32_t)v << 6) | ((uint32_t)rn << 3) |
                                         (uint32_t)rd);
                }
                if (rd == rn && v <= 255) {
                    return emit16(t, (sub ? 0x3800u : 0x3000u) | ((uint32_t)rd << 8) | (uint32_t)v);
                }
            }
            if (thumb_imm(u, &enc) == 0) {
                return emit32(t, 0xf0000000u | (op << 21) | (s << 20) | ((uint32_t)rn << 16) | ((uint32_t)rd << 8) |
                                     imm12_bits(enc));
            }
            if (!s && v <= 4095) {
                return emit32(t, (sub ? 0xf2a00000u : 0xf2000000u) | ((uint32_t)rn << 16) | ((uint32_t)rd << 8) |
                                     imm12_bits((uint32_t)v));
            }
            return fail(t, "'%s': %lld is not a number it can hold", rq->mnemonic, v);
        }
        if (strcmp(b, "cmp") == 0 && !t->wide && low(rn) && v >= 0 && v <= 255) {
            return emit16(t, 0x2800u | ((uint32_t)rn << 8) | (uint32_t)v);
        }
        if (strcmp(b, "mov") == 0 && flags_as_narrow(t) && low(rd) && v >= 0 && v <= 255) {
            return emit16(t, 0x2000u | ((uint32_t)rd << 8) | (uint32_t)v);
        }
        if (strcmp(b, "rsb") == 0 && flags_as_narrow(t) && low(rd) && low(rn) && v == 0) {
            return emit16(t, 0x4240u | ((uint32_t)rn << 3) | (uint32_t)rd);
        }
        if (thumb_imm(u, &enc) != 0) {
            /* What the instruction's opposite can hold, it holds. */
            static const struct {
                const char *name;
                unsigned other;
                int negate; /* else inverted */
            } pairs[] = {{"mov", 3, 0}, {"mvn", 2, 0}, {"and", 1, 0}, {"bic", 0, 0}, {"orr", 3, 0},
                         {"orn", 2, 0}, {"cmp", 8, 1}, {"cmn", 13, 1}, {"adc", 11, 0}, {"sbc", 10, 0}};
            size_t k;

            for (k = 0; k < sizeof(pairs) / sizeof(pairs[0]); ++k) {
                if (strcmp(b, pairs[k].name) == 0 && thumb_imm(pairs[k].negate ? 0u - u : ~u, &enc) == 0) {
                    op = pairs[k].other;
                    break;
                }
            }
            if (k == sizeof(pairs) / sizeof(pairs[0])) {
                if (strcmp(b, "mov") == 0 && !s && v >= 0 && v <= 0xffff) {
                    return emit32(t, 0xf2400000u | (((uint32_t)v & 0xf000u) << 4) | ((uint32_t)rd << 8) |
                                         imm12_bits((uint32_t)v & 0xfffu));
                }
                return fail(t, "'%s': %lld is not a number it can hold", rq->mnemonic, v);
            }
        }
        return emit32(t, 0xf0000000u | (op << 21) | (s << 20) | ((uint32_t)rn << 16) | ((uint32_t)rd << 8) |
                             imm12_bits(enc));
    }
    rm = core_reg(rq->ops[at]);
    if (rm < 0) {
        return fail(t, "'%s': '%s' is not a register or a number", rq->mnemonic, rq->ops[at]);
    }
    if (at + 1 == rq->op_count) {
        if (is_add) {
            if (flags_as_narrow(t) && low(rd) && low(rn) && low(rm)) {
                return emit16(t, 0x1800u | ((uint32_t)rm << 6) | ((uint32_t)rn << 3) | (uint32_t)rd);
            }
            /* Any two registers, the flags left. */
            if (!t->wide && !s && (rd == rn || rd == rm)) {
                int other = rd == rn ? rm : rn;

                return emit16(t, 0x4400u | (((uint32_t)rd & 8u) << 4) | ((uint32_t)other << 3) | ((uint32_t)rd & 7u));
            }
        } else if (is_sub) {
            if (flags_as_narrow(t) && low(rd) && low(rn) && low(rm)) {
                return emit16(t, 0x1a00u | ((uint32_t)rm << 6) | ((uint32_t)rn << 3) | (uint32_t)rd);
            }
        } else if (strcmp(b, "cmp") == 0) {
            if (!t->wide && low(rn) && low(rm)) {
                return emit16(t, 0x4280u | ((uint32_t)rm << 3) | (uint32_t)rn);
            }
            if (!t->wide) {
                return emit16(t, 0x4500u | (((uint32_t)rn & 8u) << 4) | ((uint32_t)rm << 3) | ((uint32_t)rn & 7u));
            }
        } else if (strcmp(b, "mov") == 0) {
            if (!t->wide && !s) {
                return emit16(t, 0x4600u | (((uint32_t)rd & 8u) << 4) | ((uint32_t)rm << 3) | ((uint32_t)rd & 7u));
            }
            if (!t->wide && !t->in_it && low(rd) && low(rm)) {
                return emit16(t, ((uint32_t)rm << 3) | (uint32_t)rd);
            }
        } else if (narrow_op(b, &nop) == 0 && low(rm) && (compare || move ? !t->wide && (compare || flags_as_narrow(t))
                                                                           : flags_as_narrow(t))) {
            if (compare && low(rn)) {
                return emit16(t, 0x4000u | (nop << 6) | ((uint32_t)rm << 3) | (uint32_t)rn);
            }
            if (move && low(rd)) {
                return emit16(t, 0x4000u | (nop << 6) | ((uint32_t)rm << 3) | (uint32_t)rd);
            }
            if (!compare && !move && low(rd) && rd == rn) {
                return emit16(t, 0x4000u | (nop << 6) | ((uint32_t)rm << 3) | (uint32_t)rd);
            }
            if (!compare && !move && low(rd) && low(rn) && rd == rm && commutes) {
                return emit16(t, 0x4000u | (nop << 6) | ((uint32_t)rn << 3) | (uint32_t)rd);
            }
        }
        return emit32(t, 0xea000000u | (op << 21) | (s << 20) | ((uint32_t)rn << 16) | ((uint32_t)rd << 8) |
                             (uint32_t)rm);
    }
    {
        unsigned type;
        unsigned amount;

        if (at + 2 != rq->op_count || shift_of(t, rq->ops[at + 1], &type, &amount) != 0) {
            return t->errbuf != NULL && t->errbuf[0] != '\0' ? -1 : fail(t, "'%s' has too many operands", rq->mnemonic);
        }
        return emit32(t, 0xea000000u | (op << 21) | (s << 20) | ((uint32_t)rn << 16) | ((amount & 0x1cu) << 10) |
                             ((uint32_t)rd << 8) | ((amount & 3u) << 6) | (type << 4) | (uint32_t)rm);
    }
}

/* lsl, lsr, asr, ror, rrx. */
static int shift(th_t *t) {
    static const char *const names[] = {"lsl", "lsr", "asr", "ror"};
    const as_arm_req_t *rq = t->rq;
    unsigned s = (unsigned)t->s;
    unsigned type;
    int rd;
    int rm;
    int rs;
    size_t at = 0;

    if (strcmp(t->base, "rrx") == 0) {
        if (rq->op_count != 2 || (rd = core_reg(rq->ops[0])) < 0 || (rm = core_reg(rq->ops[1])) < 0) {
            return fail(t, "'%s' takes two registers", rq->mnemonic);
        }
        return emit32(t, 0xea4f0030u | (s << 20) | ((uint32_t)rd << 8) | (uint32_t)rm);
    }
    for (type = 0; type < 4 && strcmp(t->base, names[type]) != 0; ++type) {
    }
    if (rq->op_count < 2 || rq->op_count > 3 || (rd = core_reg(rq->ops[at++])) < 0) {
        return fail(t, "'%s': a register, a register and by how much", rq->mnemonic);
    }
    rm = rq->op_count == 3 ? core_reg(rq->ops[at++]) : rd;
    if (rm < 0) {
        return fail(t, "'%s': '%s' is not a register", rq->mnemonic, rq->ops[1]);
    }
    rs = core_reg(rq->ops[at]);
    if (rs >= 0) {
        if (flags_as_narrow(t) && low(rd) && rd == rm && low(rs)) {
            unsigned nop = 0;

            (void)narrow_op(t->base, &nop);
            return emit16(t, 0x4000u | (nop << 6) | ((uint32_t)rs << 3) | (uint32_t)rd);
        }
        return emit32(t, 0xfa00f000u | (type << 21) | (s << 20) | ((uint32_t)rm << 16) | ((uint32_t)rd << 8) |
                             (uint32_t)rs);
    }
    {
        long long n;

        if (number(rq->ops[at], &n) != 0) {
            return fail(t, "'%s': '%s' is not a register or a number", rq->mnemonic, rq->ops[at]);
        }
        if (n < 0 || n > 32 || (n == 32 && type != 1 && type != 2)) {
            return fail(t, "'%s': a shift of %lld", rq->mnemonic, n);
        }
        if (n == 0) {
            type = 0; /* (a shift of nothing is a move) */
        }
        n &= 31;
        if (type != 3 && flags_as_narrow(t) && low(rd) && low(rm) && !(type == 0 && n == 0 && t->in_it)) {
            return emit16(t, (type << 11) | ((uint32_t)n << 6) | ((uint32_t)rm << 3) | (uint32_t)rd);
        }
        return emit32(t, 0xea4f0000u | (s << 20) | (((uint32_t)n & 0x1cu) << 10) | ((uint32_t)rd << 8) |
                             (((uint32_t)n & 3u) << 6) | (type << 4) | (uint32_t)rm);
    }
}

static int multiply(th_t *t) {
    const as_arm_req_t *rq = t->rq;
    int rd;
    int rn;
    int rm;

    if (rq->op_count < 2 || rq->op_count > 3 || (rd = core_reg(rq->ops[0])) < 0 ||
        (rn = core_reg(rq->ops[rq->op_count - 2])) < 0 || (rm = core_reg(rq->ops[rq->op_count - 1])) < 0) {
        return fail(t, "'%s' takes two registers or three", rq->mnemonic);
    }
    if (rq->op_count == 2) {
        rn = rm;
        rm = rd;
    }
    if (flags_as_narrow(t) && low(rd) && low(rn) && low(rm) && (rd == rm || rd == rn)) {
        return emit16(t, 0x4340u | ((uint32_t)(rd == rm ? rn : rm) << 3) | (uint32_t)rd);
    }
    if (t->s) {
        return fail(t, "'%s': only the short form sets the flags, and it is of low registers, one of them twice",
                    rq->mnemonic);
    }
    return emit32(t, 0xfb00f000u | ((uint32_t)rn << 16) | ((uint32_t)rd << 8) | (uint32_t)rm);
}

/* movw and movt. */
static int move_wide(th_t *t) {
    const as_arm_req_t *rq = t->rq;
    int top = t->base[3] == 't';
    long long v;
    int rd;

    if (rq->op_count != 2 || (rd = core_reg(rq->ops[0])) < 0) {
        return fail(t, "'%s': a register and a number of 16 bits", rq->mnemonic);
    }
    {
        const char *p = rq->ops[1];
        const char *op = top ? ":upper16:" : ":lower16:";

        while (*p == '#' || isspace((unsigned char)*p)) {
            p++;
        }
        if (strncasecmp(p, op, 9) == 0) {
            char name[128];
            long long addend;

            if (symbol_ref(p + 9, name, sizeof(name), &addend) != 0 || addend != 0 ||
                fixup(t, top ? R_ARM_THM_MOVT_ABS : R_ARM_THM_MOVW_ABS_NC, name) != 0) {
                return fail(t, "'%s': a symbol, and nothing added to it", rq->mnemonic);
            }
            return emit32(t, (top ? 0xf2c00000u : 0xf2400000u) | ((uint32_t)rd << 8));
        }
    }
    if (number(rq->ops[1], &v) != 0 || v < 0 || v > 0xffff) {
        return fail(t, "'%s': a number of 16 bits, and '%s' is not one", rq->mnemonic, rq->ops[1]);
    }
    return emit32(t, (top ? 0xf2c00000u : 0xf2400000u) | (((uint32_t)v & 0xf000u) << 4) | ((uint32_t)rd << 8) |
                         imm12_bits((uint32_t)v & 0xfffu));
}

/* The distance to a label of this section from where the processor
 * counts it, four bytes on: 0; 1 for a name that is none; 2 while the
 * instructions are only being measured. */
static int distance(th_t *t, const char *name, long long addend, long long *d) {
    uint64_t at = 0;

    if (t->rq->label == NULL) {
        *d = 0;
        return 2;
    }
    if (t->rq->label(t->rq->cookie, name, &at) != 0) {
        return 1;
    }
    *d = (long long)at + addend - (long long)(t->rq->pc + 4);
    return 0;
}

/* The fields of a long branch: S, J1, J2 and the two parts of the
 * distance. */
static uint32_t branch24(long long d) {
    uint32_t u = (uint32_t)d;
    uint32_t s = (u >> 24) & 1u;
    uint32_t i1 = (u >> 23) & 1u;
    uint32_t i2 = (u >> 22) & 1u;

    return (s << 26) | (((u >> 12) & 0x3ffu) << 16) | ((i1 ^ s ^ 1u) << 13) | ((i2 ^ s ^ 1u) << 11) |
           ((u >> 1) & 0x7ffu);
}

static int branch(th_t *t) {
    const as_arm_req_t *rq = t->rq;
    const char *b = t->base;
    char name[128];
    long long addend;
    long long d = 0;
    int known;
    int rm;

    if (rq->op_count != 1) {
        return fail(t, "'%s' takes one operand", rq->mnemonic);
    }
    rm = core_reg(rq->ops[0]);
    if (strcmp(b, "bx") == 0 || (strcmp(b, "blx") == 0 && rm >= 0)) {
        if (rm < 0) {
            return fail(t, "'%s': a register", rq->mnemonic);
        }
        return emit16(t, (b[1] == 'x' ? 0x4700u : 0x4780u) | ((uint32_t)rm << 3));
    }
    if (symbol_ref(rq->ops[0], name, sizeof(name), &addend) != 0) {
        return fail(t, "'%s': '%s' is not where a branch can go", rq->mnemonic, rq->ops[0]);
    }
    known = distance(t, name, addend, &d);
    if (known == 1 && addend != 0) {
        return fail(t, "'%s': a symbol of another section or file, and nothing added to it", rq->mnemonic);
    }
    if (known != 1 && (d & 1) != 0 && strcmp(b, "blx") != 0) {
        return fail(t, "'%s': '%s' is not at a multiple of two bytes from here", rq->mnemonic, name);
    }
    if (strcmp(b, "bl") == 0 || strcmp(b, "blx") == 0) {
        uint32_t w = b[2] == 'x' ? 0xf000c000u : 0xf000d000u;

        if (known == 1) {
            if (fixup(t, R_ARM_THM_CALL, name) != 0) {
                return -1;
            }
            return emit32(t, w | 0x07ff2ffeu);
        }
        if (b[2] == 'x') {
            /* To ARM code, which is at a multiple of four: from this
             * instruction's own place brought down to one. */
            d = d + (long long)(t->rq->pc & 2u);
            if ((d & 3) != 0) {
                return fail(t, "'%s': '%s' is not at a multiple of four bytes", rq->mnemonic, name);
            }
        }
        if (d < -0x1000000ll || d > 0xfffffell) {
            return fail(t, "'%s': '%s' is %lld bytes away, which it cannot reach", rq->mnemonic, name, d);
        }
        return emit32(t, w | branch24(d));
    }
    if (t->cond != COND_AL && !t->in_it) {
        /* With a condition of its own. */
        if (t->narrow) {
            if (known == 1) {
                if (fixup(t, R_ARM_THM_JUMP8, name) != 0) {
                    return -1;
                }
                return emit16(t, 0xd0feu | (t->cond << 8));
            }
            if (d < -256 || d > 254) {
                return fail(t, "'%s': '%s' is %lld bytes away, which the short form cannot reach", rq->mnemonic,
                            name, d);
            }
            return emit16(t, 0xd000u | (t->cond << 8) | (((uint32_t)d >> 1) & 0xffu));
        }
        if (known == 1) {
            if (fixup(t, R_ARM_THM_JUMP19, name) != 0) {
                return -1;
            }
            return emit32(t, 0xf43faffeu | (t->cond << 22));
        }
        if (d < -0x100000ll || d > 0xffffell) {
            return fail(t, "'%s': '%s' is %lld bytes away, which it cannot reach", rq->mnemonic, name, d);
        }
        {
            uint32_t u = (uint32_t)d;

            return emit32(t, 0xf0008000u | (((u >> 20) & 1u) << 26) | (t->cond << 22) | (((u >> 12) & 0x3fu) << 16) |
                                 (((u >> 18) & 1u) << 13) | (((u >> 19) & 1u) << 11) | ((u >> 1) & 0x7ffu));
        }
    }
    if (t->narrow) {
        if (known == 1) {
            if (fixup(t, R_ARM_THM_JUMP11, name) != 0) {
                return -1;
            }
            return emit16(t, 0xe7feu);
        }
        if (d < -2048 || d > 2046) {
            return fail(t, "'%s': '%s' is %lld bytes away, which the short form cannot reach", rq->mnemonic, name, d);
        }
        return emit16(t, 0xe000u | (((uint32_t)d >> 1) & 0x7ffu));
    }
    if (known == 1) {
        if (fixup(t, R_ARM_THM_JUMP24, name) != 0) {
            return -1;
        }
        return emit32(t, 0xf7ffbffeu);
    }
    if (d < -0x1000000ll || d > 0xfffffell) {
        return fail(t, "'%s': '%s' is %lld bytes away, which it cannot reach", rq->mnemonic, name, d);
    }
    return emit32(t, 0xf0009000u | branch24(d));
}

/* cbz and cbnz. */
static int compare_branch(th_t *t) {
    const as_arm_req_t *rq = t->rq;
    char name[128];
    long long addend;
    long long d;
    int rn;

    if (rq->op_count != 2 || (rn = core_reg(rq->ops[0])) < 0 || !low(rn) ||
        symbol_ref(rq->ops[1], name, sizeof(name), &addend) != 0) {
        return fail(t, "'%s': a low register and a label", rq->mnemonic);
    }
    if (distance(t, name, addend, &d) == 1) {
        return fail(t, "'%s': '%s' is not a label of this section", rq->mnemonic, name);
    }
    if (d < 0 || d > 126 || (d & 1) != 0) {
        return fail(t, "'%s': '%s' is %lld bytes away, and it reaches forward 126", rq->mnemonic, name, d);
    }
    return emit16(t, (t->base[2] == 'n' ? 0xb900u : 0xb100u) | (((uint32_t)d & 0x40u) << 3) |
                         (((uint32_t)d & 0x3eu) << 2) | (uint32_t)rn);
}

/* it, and with it up to three of t and e. */
static int if_then(th_t *t) {
    static const char *const conds[] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                        "hi", "ls", "ge", "lt", "gt", "le", "al"};
    const as_arm_req_t *rq = t->rq;
    char m[8];
    char c[8];
    unsigned cond;
    unsigned mask = 0;
    size_t n;
    size_t i;

    text(rq->mnemonic, m, sizeof(m));
    n = strlen(m) - 2;
    if (rq->op_count != 1) {
        return fail(t, "'%s': a condition", rq->mnemonic);
    }
    text(rq->ops[0], c, sizeof(c));
    if (strcmp(c, "hs") == 0) {
        cond = 2;
    } else if (strcmp(c, "lo") == 0) {
        cond = 3;
    } else {
        for (cond = 0; cond < 15 && strcmp(c, conds[cond]) != 0; ++cond) {
        }
        if (cond == 15) {
            return fail(t, "'%s': '%s' is not a condition", rq->mnemonic, rq->ops[0]);
        }
    }
    for (i = 0; i < n; ++i) {
        unsigned bit = m[2 + i] == 't' ? (cond & 1u) : ((cond & 1u) ^ 1u);

        mask |= bit << (3 - i);
    }
    mask |= 1u << (3 - n);
    return emit16(t, 0xbf00u | (cond << 4) | mask);
}

static int reg_list(th_t *t, const char *s, uint32_t *mask) {
    char buf[160];
    char *p;
    char *close;

    text(s, buf, sizeof(buf));
    *mask = 0;
    if (buf[0] != '{' || (close = strchr(buf, '}')) == NULL) {
        return fail(t, "'%s': '%s' is not a list of registers", t->rq->mnemonic, s);
    }
    *close = '\0';
    for (p = buf + 1; *p != '\0';) {
        char *comma = strchr(p, ',');
        char *dash;
        int lo;
        int hi;

        if (comma != NULL) {
            *comma = '\0';
        }
        dash = strchr(p, '-');
        if (dash != NULL) {
            *dash = '\0';
        }
        lo = core_reg(p);
        hi = dash != NULL ? core_reg(dash + 1) : lo;
        if (lo < 0 || hi < lo) {
            return fail(t, "'%s': '%s' is not a list of registers", t->rq->mnemonic, s);
        }
        for (; lo <= hi; ++lo) {
            *mask |= 1u << lo;
        }
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    return *mask != 0 ? 0 : fail(t, "'%s': a list of no registers", t->rq->mnemonic);
}

static int push_pop(th_t *t) {
    const as_arm_req_t *rq = t->rq;
    int pop = t->base[1] == 'o';
    uint32_t mask;

    if (rq->op_count != 1 || reg_list(t, rq->ops[0], &mask) != 0) {
        return t->errbuf != NULL && t->errbuf[0] != '\0' ? -1 : fail(t, "'%s': a list of registers", rq->mnemonic);
    }
    if (!t->wide && (mask & ~(0xffu | (pop ? 0x8000u : 0x4000u))) == 0) {
        return emit16(t, (pop ? 0xbc00u : 0xb400u) | ((mask & 0xc000u) != 0 ? 0x100u : 0u) | (mask & 0xffu));
    }
    if ((mask & 0x2000u) != 0 || (!pop && (mask & 0x8000u) != 0) || (pop && (mask & 0xc000u) == 0xc000u)) {
        return fail(t, "'%s': a register in the list that may not be", rq->mnemonic);
    }
    if ((mask & (mask - 1)) == 0) {
        /* One register is a store or a load of it. */
        unsigned r;

        for (r = 0; (mask >> r) != 1u; ++r) {
        }
        return emit32(t, (pop ? 0xf85d0b04u : 0xf84d0d04u) | (r << 12));
    }
    return emit32(t, (pop ? 0xe8bd0000u : 0xe92d0000u) | mask);
}

/* ldr, str and those of a byte or a half word. */
static int load_store(th_t *t) {
    const as_arm_req_t *rq = t->rq;
    const char *b = t->base;
    int load = b[0] == 'l';
    int is_signed = b[3] == 's';
    char size = b[is_signed ? 4 : 3]; /* b, h, or the end */
    char buf[160];
    char *parts[3];
    size_t count = 0;
    char *close;
    char *p;
    int pre = 0;
    int post = 0;
    int rt;
    int rn;
    int rm = -1;
    long long imm = 0;
    unsigned shift = 0;
    uint32_t wide_base;

    if (rq->op_count < 2 || (rt = core_reg(rq->ops[0])) < 0 || (is_signed && !load)) {
        return fail(t, "'%s': a register and an address", rq->mnemonic);
    }
    wide_base = size == 'b' ? 0xf8000000u : size == 'h' ? 0xf8200000u : 0xf8400000u;
    wide_base |= (load ? 0x00100000u : 0u) | (is_signed ? 0x01000000u : 0u);
    if (strchr(rq->ops[1], '[') == NULL) {
        char name[128];
        long long addend;
        long long d;

        /* A label: a word at a distance from here, here being brought
         * down to a multiple of four. */
        if (!load || rq->op_count != 2 || symbol_ref(rq->ops[1], name, sizeof(name), &addend) != 0) {
            return fail(t, "'%s': '%s' is not an address", rq->mnemonic, rq->ops[1]);
        }
        if (distance(t, name, addend, &d) == 1) {
            return fail(t, "'%s': '%s' is not a label of this section, and no other can be loaded from here",
                        rq->mnemonic, name);
        }
        if (t->rq->label != NULL) {
            d += (long long)(t->rq->pc & 2u);
        }
        if (size == '\0' && !t->wide && low(rt)) {
            if (d < 0 || d > 1020 || (d & 3) != 0) {
                return fail(t, "'%s': '%s' is %lld bytes away, and the short form reaches forward 1020: `ldr.w`",
                            rq->mnemonic, name, d);
            }
            return emit16(t, 0x4800u | ((uint32_t)rt << 8) | (uint32_t)(d >> 2));
        }
        if (d < -4095 || d > 4095) {
            return fail(t, "'%s': '%s' is %lld bytes away, which it cannot reach", rq->mnemonic, name, d);
        }
        return emit32(t, wide_base | 0x000f0000u | (d >= 0 ? 0x00800000u : 0u) | ((uint32_t)rt << 12) |
                             (uint32_t)(d >= 0 ? d : -d));
    }
    text(rq->ops[1], buf, sizeof(buf));
    if (buf[0] != '[' || (close = strchr(buf, ']')) == NULL) {
        return fail(t, "'%s': '%s' is not an address", rq->mnemonic, rq->ops[1]);
    }
    if (close[1] == '!') {
        pre = 1;
    } else if (close[1] != '\0') {
        return fail(t, "'%s': what follows the ] of '%s'", rq->mnemonic, rq->ops[1]);
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
    rn = core_reg(parts[0]);
    if (rn < 0) {
        return fail(t, "'%s': the base of an address is a register", rq->mnemonic);
    }
    if (count >= 2) {
        rm = core_reg(parts[1]);
        if (rm >= 0) {
            unsigned type = 0;

            if (count == 3 && (shift_of(t, parts[2], &type, &shift) != 0 || type != 0 || shift > 3)) {
                return fail(t, "'%s': an index is shifted left by 0 to 3", rq->mnemonic);
            }
        } else if (count != 2 || number(parts[1], &imm) != 0) {
            return fail(t, "'%s': the offset of an address is a number or a register", rq->mnemonic);
        }
    }
    if (rq->op_count == 3) {
        if (count != 1 || pre || number(rq->ops[2], &imm) != 0) {
            return fail(t, "'%s': what follows its address", rq->mnemonic);
        }
        post = 1;
    } else if (rq->op_count != 2) {
        return fail(t, "'%s' has too many operands", rq->mnemonic);
    }
    if (rm >= 0) {
        if (pre || post) {
            return fail(t, "'%s': an index, and the base written back", rq->mnemonic);
        }
        if (!t->wide && shift == 0 && low(rt) && low(rn) && low(rm)) {
            unsigned op = load ? (is_signed ? (size == 'b' ? 3u : 7u) : size == 'b' ? 6u : size == 'h' ? 5u : 4u)
                               : (size == 'b' ? 2u : size == 'h' ? 1u : 0u);

            return emit16(t, 0x5000u | (op << 9) | ((uint32_t)rm << 6) | ((uint32_t)rn << 3) | (uint32_t)rt);
        }
        return emit32(t, wide_base | ((uint32_t)rn << 16) | ((uint32_t)rt << 12) | (shift << 4) | (uint32_t)rm);
    }
    if (!pre && !post && !is_signed && !t->wide && low(rt) && imm >= 0) {
        if (size == '\0' && low(rn) && (imm & 3) == 0 && imm <= 124) {
            return emit16(t, (load ? 0x6800u : 0x6000u) | ((uint32_t)imm << 4) | ((uint32_t)rn << 3) | (uint32_t)rt);
        }
        if (size == '\0' && rn == 13 && (imm & 3) == 0 && imm <= 1020) {
            return emit16(t, (load ? 0x9800u : 0x9000u) | ((uint32_t)rt << 8) | (uint32_t)(imm >> 2));
        }
        if (size == 'b' && low(rn) && imm <= 31) {
            return emit16(t, (load ? 0x7800u : 0x7000u) | ((uint32_t)imm << 6) | ((uint32_t)rn << 3) | (uint32_t)rt);
        }
        if (size == 'h' && low(rn) && (imm & 1) == 0 && imm <= 62) {
            return emit16(t, (load ? 0x8800u : 0x8000u) | ((uint32_t)imm << 5) | ((uint32_t)rn << 3) | (uint32_t)rt);
        }
    }
    if (!pre && !post && imm >= 0) {
        if (imm > 4095) {
            return fail(t, "'%s': an offset of %lld, and it has twelve bits for one", rq->mnemonic, imm);
        }
        return emit32(t, wide_base | 0x00800000u | ((uint32_t)rn << 16) | ((uint32_t)rt << 12) | (uint32_t)imm);
    }
    if (imm < -255 || imm > 255) {
        return fail(t, "'%s': an offset of %lld, and it has eight bits for one", rq->mnemonic, imm);
    }
    return emit32(t, wide_base | ((uint32_t)rn << 16) | ((uint32_t)rt << 12) | 0x800u | (post ? 0u : 0x400u) |
                         (imm >= 0 ? 0x200u : 0u) | (pre || post ? 0x100u : 0u) | (uint32_t)(imm >= 0 ? imm : -imm));
}

int as_arm_thumb(const as_arm_req_t *rq, const char *base, int sets_flags, unsigned cond, as_arm_out_t *out,
                 char *errbuf, size_t errbuf_sz) {
    static const char *const dp[] = {"and", "eor", "sub", "rsb", "add", "adc", "sbc", "orr",
                                     "orn", "bic", "tst", "teq", "cmp", "cmn", "mov", "mvn"};
    static const struct {
        const char *name;
        uint32_t narrow;
        uint32_t wide;
    } hints[] = {{"nop", 0xbf00u, 0xf3af8000u}, {"yield", 0xbf10u, 0xf3af8001u}, {"wfe", 0xbf20u, 0xf3af8002u},
                 {"wfi", 0xbf30u, 0xf3af8003u}, {"sev", 0xbf40u, 0xf3af8004u}};
    size_t len = strlen(rq->mnemonic);
    th_t t;
    size_t i;

    memset(&t, 0, sizeof(t));
    t.rq = rq;
    t.out = out;
    t.errbuf = errbuf;
    t.errbuf_sz = errbuf_sz;
    t.base = base;
    t.s = sets_flags;
    t.cond = cond;
    t.in_it = rq->in_it;
    if (len > 2 && rq->mnemonic[len - 2] == '.') {
        t.wide = tolower((unsigned char)rq->mnemonic[len - 1]) == 'w';
        t.narrow = tolower((unsigned char)rq->mnemonic[len - 1]) == 'n';
    }
    if (strcmp(base, "it") == 0) {
        return if_then(&t);
    }
    if (base[0] == 'b' && (base[1] == '\0' || strcmp(base, "bl") == 0 || strcmp(base, "bx") == 0 ||
                           strcmp(base, "blx") == 0)) {
        return branch(&t);
    }
    /* A condition is the IT block's to give; the instruction says it
     * over again. */
    if (cond != COND_AL && !t.in_it) {
        return fail(&t, "'%s': a condition, and no IT block that it is in", rq->mnemonic);
    }
    for (i = 0; i < sizeof(dp) / sizeof(dp[0]); ++i) {
        if (strcmp(base, dp[i]) == 0) {
            /* A move of a shifted register is the shift. */
            if (strcmp(base, "mov") == 0 && rq->op_count == 3 && !is_imm(rq->ops[1])) {
                unsigned type;
                unsigned amount;
                char reg[16];

                if (shift_of(&t, rq->ops[2], &type, &amount) != 0) {
                    return -1;
                }
                text(rq->ops[1], reg, sizeof(reg));
                return emit32(&t, 0xea4f0000u | ((uint32_t)t.s << 20) | ((amount & 0x1cu) << 10) |
                                      ((uint32_t)core_reg(rq->ops[0]) << 8) | ((amount & 3u) << 6) | (type << 4) |
                                      (uint32_t)core_reg(reg));
            }
            return data_processing(&t);
        }
    }
    if (strcmp(base, "lsl") == 0 || strcmp(base, "lsr") == 0 || strcmp(base, "asr") == 0 || strcmp(base, "ror") == 0 ||
        strcmp(base, "rrx") == 0) {
        return shift(&t);
    }
    if (strcmp(base, "mul") == 0) {
        return multiply(&t);
    }
    if (strcmp(base, "movw") == 0 || strcmp(base, "movt") == 0) {
        return move_wide(&t);
    }
    if (strcmp(base, "cbz") == 0 || strcmp(base, "cbnz") == 0) {
        return compare_branch(&t);
    }
    if (strcmp(base, "push") == 0 || strcmp(base, "pop") == 0) {
        return push_pop(&t);
    }
    if (strncmp(base, "ldr", 3) == 0 || strncmp(base, "str", 3) == 0) {
        return load_store(&t);
    }
    for (i = 0; i < sizeof(hints) / sizeof(hints[0]); ++i) {
        if (strcmp(base, hints[i].name) == 0) {
            if (rq->op_count != 0) {
                return fail(&t, "'%s' takes no operand", rq->mnemonic);
            }
            return t.wide ? emit32(&t, hints[i].wide) : emit16(&t, hints[i].narrow);
        }
    }
    if (strcmp(base, "svc") == 0 || strcmp(base, "bkpt") == 0) {
        long long n;

        if (rq->op_count != 1 || number(rq->ops[0], &n) != 0 || n < 0 || n > 255) {
            return fail(&t, "'%s': a number of eight bits", rq->mnemonic);
        }
        return emit16(&t, (base[0] == 's' ? 0xdf00u : 0xbe00u) | (uint32_t)n);
    }
    return fail(&t, "'%s' is not an instruction of Thumb that this assembler has", rq->mnemonic);
}
