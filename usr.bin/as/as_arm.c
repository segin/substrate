#include "as_arm.h"

#include "as_arm_int.h"
#include "as_expr.h"
#include "elfobj.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The instructions of ARM (A32).  See as_arm.h. */

#define COND_AL 14u

typedef struct {
    const as_arm_req_t *rq;
    as_arm_out_t *out;
    char *errbuf;
    size_t errbuf_sz;
    char base[16];
    int s;
    unsigned cond;
} arm_t;

static int fail(arm_t *a, const char *fmt, ...) {
    va_list ap;

    if (a->errbuf != NULL && a->errbuf_sz != 0) {
        va_start(ap, fmt);
        vsnprintf(a->errbuf, a->errbuf_sz, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int word(arm_t *a, uint32_t w) {
    unsigned i;

    for (i = 0; i < 4; ++i) {
        a->out->bytes[i] = (uint8_t)(w >> (8 * i));
    }
    a->out->len = 4;
    return 0;
}

static int fixup(arm_t *a, unsigned type, const char *symbol, int64_t addend) {
    as_arm_fixup_t *f;

    if (a->out->fixup_count >= sizeof(a->out->fixups) / sizeof(a->out->fixups[0]) ||
        strlen(symbol) >= sizeof(f->symbol)) {
        return fail(a, "'%s': a symbol's name too long, or too many of them", a->rq->mnemonic);
    }
    f = &a->out->fixups[a->out->fixup_count++];
    f->offset = 0;
    f->type = type;
    memcpy(f->symbol, symbol, strlen(symbol) + 1);
    f->addend = addend;
    return 0;
}

/* An operand's text without the blanks around it, in lower case: into
 * buf, which is returned. */
#define text as_arm_text
#define number as_arm_number
#define symbol_ref as_arm_symbol

char *as_arm_text(const char *s, char *buf, size_t buf_sz) {
    size_t n = 0;

    while (isspace((unsigned char)*s)) {
        s++;
    }
    while (*s != '\0' && n + 1 < buf_sz) {
        buf[n++] = (char)tolower((unsigned char)*s++);
    }
    while (n > 0 && isspace((unsigned char)buf[n - 1])) {
        n--;
    }
    buf[n] = '\0';
    return buf;
}

#define core_reg as_arm_core_reg
#define modified_imm as_arm_modified_imm

/* A core register by its name: 0 to 15, or -1. */
int as_arm_core_reg(const char *s) {
    static const struct {
        const char *name;
        int n;
    } other[] = {{"sp", 13}, {"lr", 14}, {"pc", 15}, {"ip", 12}, {"fp", 11}, {"sl", 10}, {"sb", 9},
                 {"a1", 0},  {"a2", 1},  {"a3", 2},  {"a4", 3},  {"v1", 4},  {"v2", 5},  {"v3", 6},
                 {"v4", 7},  {"v5", 8},  {"v6", 9},  {"v7", 10}, {"v8", 11}};
    char buf[16];
    size_t i;

    text(s, buf, sizeof(buf));
    if (buf[0] == 'r' && isdigit((unsigned char)buf[1])) {
        char *end;
        long n = strtol(buf + 1, &end, 10);

        /* (r05 is no register: the number is written as it is.) */
        return *end == '\0' && n <= 15 && !(buf[1] == '0' && buf[2] != '\0') ? (int)n : -1;
    }
    for (i = 0; i < sizeof(other) / sizeof(other[0]); ++i) {
        if (strcmp(buf, other[i].name) == 0) {
            return other[i].n;
        }
    }
    return -1;
}

/* A number: what follows a `#`, or stands bare.  0, or -1 if it is no
 * number this can work out here. */
int as_arm_number(const char *s, long long *out) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '#') {
        s++;
    }
    return as_expr_eval_string(s, NULL, NULL, out);
}

/* `name`, `name+n` or `name-n`: 0 with the name and the number. */
int as_arm_symbol(const char *s, char *name, size_t name_sz, long long *addend) {
    size_t n = 0;

    while (isspace((unsigned char)*s)) {
        s++;
    }
    *addend = 0;
    if (!(isalpha((unsigned char)*s) || *s == '_' || *s == '.' || *s == '$')) {
        return -1;
    }
    while ((isalnum((unsigned char)*s) || *s == '_' || *s == '.' || *s == '$') && n + 1 < name_sz) {
        name[n++] = *s++;
    }
    name[n] = '\0';
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return 0;
    }
    if (*s != '+' && *s != '-') {
        return -1;
    }
    return as_expr_eval_string(s, NULL, NULL, addend);
}

/* The rotated form of a number: eight bits turned right by an even
 * count.  0 with its twelve bits, or -1 if there is none. */
int as_arm_modified_imm(uint32_t v, uint32_t *enc) {
    unsigned rot;

    for (rot = 0; rot < 32; rot += 2) {
        uint32_t r = rot == 0 ? v : (v << rot) | (v >> (32 - rot));

        if (r <= 0xffu) {
            *enc = ((rot / 2) << 8) | r;
            return 0;
        }
    }
    return -1;
}

static const char *const cond_names[] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                         "hi", "ls", "ge", "lt", "gt", "le", "al"};

static int cond_of(const char *s, size_t n, unsigned *cond) {
    size_t i;

    if (n != 2) {
        return -1;
    }
    for (i = 0; i < sizeof(cond_names) / sizeof(cond_names[0]); ++i) {
        if (strncmp(s, cond_names[i], 2) == 0) {
            *cond = (unsigned)i;
            return 0;
        }
    }
    if (strncmp(s, "hs", 2) == 0) {
        *cond = 2;
        return 0;
    }
    if (strncmp(s, "lo", 2) == 0) {
        *cond = 3;
        return 0;
    }
    return -1;
}

/* The instructions there are, and whether each may set the flags by an
 * `s` after its name. */
static const struct {
    const char *name;
    int may_set;
} bases[] = {
    {"and", 1},    {"eor", 1},    {"sub", 1},    {"rsb", 1},     {"add", 1},    {"adc", 1},    {"sbc", 1},
    {"rsc", 1},    {"tst", 0},    {"teq", 0},    {"cmp", 0},     {"cmn", 0},    {"orr", 1},    {"mov", 1},
    {"bic", 1},    {"mvn", 1},    {"lsl", 1},    {"lsr", 1},     {"asr", 1},    {"ror", 1},    {"rrx", 1},
    {"orn", 1},    {"cbz", 0},    {"cbnz", 0},
    {"movw", 0},   {"movt", 0},   {"mul", 1},    {"mla", 1},     {"mls", 0},    {"umull", 1},  {"umlal", 1},
    {"smull", 1},  {"smlal", 1},  {"sdiv", 0},   {"udiv", 0},    {"clz", 0},    {"rev", 0},    {"rev16", 0},
    {"uxtb", 0},   {"uxth", 0},   {"sxtb", 0},   {"sxth", 0},    {"ldr", 0},    {"str", 0},    {"ldrb", 0},
    {"strb", 0},   {"ldrh", 0},   {"strh", 0},   {"ldrsb", 0},   {"ldrsh", 0},  {"ldrd", 0},   {"strd", 0},
    {"ldm", 0},    {"stm", 0},    {"ldmia", 0},  {"ldmib", 0},   {"ldmda", 0},  {"ldmdb", 0},  {"stmia", 0},
    {"stmib", 0},  {"stmda", 0},  {"stmdb", 0},  {"ldmfd", 0},   {"ldmed", 0},  {"ldmfa", 0},  {"ldmea", 0},
    {"stmfd", 0},  {"stmed", 0},  {"stmfa", 0},  {"stmea", 0},   {"push", 0},   {"pop", 0},    {"b", 0},
    {"bl", 0},     {"bx", 0},     {"blx", 0},    {"svc", 0},     {"swi", 0},    {"bkpt", 0},   {"mrs", 0},
    {"msr", 0},    {"nop", 0},    {"yield", 0},  {"wfe", 0},     {"wfi", 0},    {"sev", 0},    {"mrc", 0},
    {"mcr", 0},    {"adr", 0},    {"vadd", 0},   {"vsub", 0},    {"vmul", 0},   {"vdiv", 0},   {"vmla", 0},
    {"vmls", 0},   {"vnmul", 0},  {"vabs", 0},   {"vneg", 0},    {"vsqrt", 0},  {"vmov", 0},   {"vcmp", 0},
    {"vcmpe", 0},  {"vcvt", 0},   {"vldr", 0},   {"vstr", 0},    {"vpush", 0},  {"vpop", 0},   {"vmrs", 0},
    {"vmsr", 0},   {"vand", 0},   {"vorr", 0},   {"veor", 0},    {"vbic", 0},   {"vld1", 0},   {"vst1", 0},
    {"vdup", 0},   {"vmvn", 0},   {"vshl", 0},   {"vshr", 0},    {"vmax", 0},   {"vmin", 0},   {"vceq", 0},
    {"vcgt", 0},   {"vcge", 0},   {"vtst", 0},   {"vnmla", 0},   {"vnmls", 0},  {"vfma", 0},   {"vfms", 0},
    {"vfnma", 0},  {"vfnms", 0},  {"vldmia", 0}, {"vldmdb", 0},  {"vstmia", 0}, {"vstmdb", 0}, {"vldm", 0},
    {"vstm", 0},   {"vorn", 0},   {"vqadd", 0},  {"vqsub", 0},   {"vcnt", 0},   {"vclz", 0},   {"vrev64", 0},
    {"vswp", 0},
};

int as_arm_split_mnemonic(const char *mnemonic, char *base, size_t base_sz, int *sets_flags, unsigned *cond) {
    char m[32];
    char *dot;
    size_t n;
    size_t i;

    text(mnemonic, m, sizeof(m));
    /* (What follows a dot is the size of what a vector instruction
     * works on, and is not the name's.) */
    dot = strchr(m, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    n = strlen(m);
    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        size_t b = strlen(bases[i].name);
        const char *rest = m + b;
        size_t r;
        unsigned c = COND_AL;
        int s = 0;

        if (b > n || strncmp(m, bases[i].name, b) != 0 || b >= base_sz) {
            continue;
        }
        r = n - b;
        /* Nothing; an s; a condition; the two in either order, the old
         * one and the new. */
        if (r == 0) {
        } else if (r == 1 && rest[0] == 's' && bases[i].may_set) {
            s = 1;
        } else if (r == 2 && cond_of(rest, 2, &c) == 0) {
        } else if (r == 3 && bases[i].may_set && rest[0] == 's' && cond_of(rest + 1, 2, &c) == 0) {
            s = 1;
        } else if (r == 3 && bases[i].may_set && rest[2] == 's' && cond_of(rest, 2, &c) == 0) {
            s = 1;
        } else {
            continue;
        }
        memcpy(base, bases[i].name, b + 1);
        *sets_flags = s;
        *cond = c;
        return 0;
    }
    return -1;
}

/* The operand that is a shift written after a register: `lsl #3`,
 * `asr r2`, `rrx`.  0 with the bits 11 to 4 of the second operand. */
static int shift_bits(arm_t *a, const char *s, uint32_t *bits) {
    static const char *const names[] = {"lsl", "lsr", "asr", "ror"};
    char buf[48];
    unsigned type;
    long long n;
    int r;

    text(s, buf, sizeof(buf));
    if (strcmp(buf, "rrx") == 0) {
        *bits = 3u << 5;
        return 0;
    }
    for (type = 0; type < 4; ++type) {
        if (strncmp(buf, names[type], 3) == 0 && (isspace((unsigned char)buf[3]) || buf[3] == '#')) {
            break;
        }
    }
    if (type == 4) {
        return fail(a, "'%s': '%s' is no shift", a->rq->mnemonic, s);
    }
    r = core_reg(buf + 3);
    if (r >= 0) {
        *bits = ((uint32_t)r << 8) | (type << 5) | (1u << 4);
        return 0;
    }
    if (number(buf + 3, &n) != 0) {
        return fail(a, "'%s': the count of a shift is a register or a number", a->rq->mnemonic);
    }
    /* A shift to the right is by 1 to 32, and 32 is written as none;
     * one to the left by 0 to 31; a rotation by 1 to 31, none being
     * the rotation through the carry. */
    if ((type == 1 || type == 2) && n == 32) {
        n = 0;
    } else if (n < 0 || n > 31) {
        return fail(a, "'%s': a shift of %lld", a->rq->mnemonic, n);
    } else if (n == 0) {
        /* A shift of nothing is none, whichever way it was to go: the
         * bits that would say a shift right or a rotation by nothing
         * say one by 32, and a rotation through the carry. */
        type = 0;
    }
    *bits = ((uint32_t)n << 7) | (type << 5);
    return 0;
}

/*
 * The second operand of a data-processing instruction, from operand
 * `at` on: a number, a register, or a register and its shift.  0 with
 * bit 25 and the low twelve; -2 if it is a number with no rotated
 * form, which the caller may have another instruction for -- with the
 * number in *value.
 */
static int operand2(arm_t *a, size_t at, uint32_t *bits, long long *value) {
    const as_arm_req_t *rq = a->rq;
    const char *s = rq->ops[at];
    uint32_t sh = 0;
    int rm;

    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '#') {
        uint32_t enc;

        if (at + 1 != rq->op_count) {
            return fail(a, "'%s': something after its number", rq->mnemonic);
        }
        if (number(s, value) != 0) {
            return fail(a, "'%s': '%s' is not a number", rq->mnemonic, s);
        }
        if (*value < -0x80000000ll || *value > 0xffffffffll) {
            return fail(a, "'%s': %lld is not a number of 32 bits", rq->mnemonic, *value);
        }
        if (modified_imm((uint32_t)*value, &enc) != 0) {
            return -2;
        }
        *bits = (1u << 25) | enc;
        return 0;
    }
    rm = core_reg(s);
    if (rm < 0) {
        return fail(a, "'%s': '%s' is not a register or a number", rq->mnemonic, s);
    }
    if (at + 2 == rq->op_count) {
        if (shift_bits(a, rq->ops[at + 1], &sh) != 0) {
            return -1;
        }
    } else if (at + 1 != rq->op_count) {
        return fail(a, "'%s' has too many operands", rq->mnemonic);
    }
    *bits = sh | (uint32_t)rm;
    return 0;
}

/* Is the operand a shift -- `lsl #2`, `asr r3`, `rrx` -- and no
 * register or number? */
static int is_shift_text(const char *s) {
    char buf[16];

    text(s, buf, sizeof(buf));
    return strcmp(buf, "rrx") == 0 ||
           ((strncmp(buf, "lsl", 3) == 0 || strncmp(buf, "lsr", 3) == 0 || strncmp(buf, "asr", 3) == 0 ||
             strncmp(buf, "ror", 3) == 0) &&
            (isspace((unsigned char)buf[3]) || buf[3] == '#'));
}

static int need_reg(arm_t *a, size_t at, int *r) {
    if (at >= a->rq->op_count || (*r = core_reg(a->rq->ops[at])) < 0) {
        return fail(a, "'%s': operand %u is to be a register", a->rq->mnemonic, (unsigned)at + 1);
    }
    return 0;
}

static int data_processing(arm_t *a, unsigned opcode) {
    static const char *const instead[16] = {"bic", NULL, "add", NULL, "sub", "sbc", "adc", NULL,
                                            NULL,  NULL, "cmn", "cmp", NULL,  "mvn", "and", "mov"};
    static const unsigned other[16] = {14, 0, 4, 0, 2, 6, 5, 0, 0, 0, 11, 10, 0, 15, 0, 13};
    const as_arm_req_t *rq = a->rq;
    int compare = opcode >= 8 && opcode <= 11;
    int move = opcode == 13 || opcode == 15;
    size_t at = 0;
    uint32_t op2 = 0;
    long long value = 0;
    int rd = 0;
    int rn = 0;
    int rc;

    if (!compare) {
        if (need_reg(a, at++, &rd) != 0) {
            return -1;
        }
    }
    if (!move) {
        /* `add r0, r1` is `add r0, r0, r1`, and `add r0, r1, lsl #2`
         * the same with a shift: the first register is both. */
        if (!compare && (rq->op_count == 2 || (rq->op_count == 3 && is_shift_text(rq->ops[2])))) {
            rn = rd;
        } else if (need_reg(a, at++, &rn) != 0) {
            return -1;
        }
    }
    if (at >= rq->op_count) {
        return fail(a, "'%s' has too few operands", rq->mnemonic);
    }
    rc = operand2(a, at, &op2, &value);
    if (rc == -2) {
        uint32_t enc;
        uint32_t v = (uint32_t)value;

        /* A number with no rotated form may have one negated or
         * inverted, and then the instruction that is the same with it
         * is this one's: `mov r0, #-1` is `mvn r0, #0`. */
        if (instead[opcode] != NULL &&
            modified_imm(opcode == 2 || opcode == 4 || opcode == 10 || opcode == 11 ? 0u - v : ~v, &enc) == 0) {
            opcode = other[opcode];
            op2 = (1u << 25) | enc;
        } else if (opcode == 13 && !a->s && v <= 0xffffu) {
            /* And a move of 16 bits has an instruction of its own. */
            return word(a, (a->cond << 28) | 0x03000000u | ((v >> 12) << 16) | ((uint32_t)rd << 12) | (v & 0xfffu));
        } else {
            return fail(a, "'%s': %lld is not a number the instruction can hold: eight bits, turned by an even count",
                        rq->mnemonic, value);
        }
    } else if (rc != 0) {
        return -1;
    }
    return word(a, (a->cond << 28) | op2 | (opcode << 21) | ((uint32_t)(a->s || compare) << 20) |
                       ((uint32_t)rn << 16) | ((uint32_t)rd << 12));
}

/* `lsl r0, r1, #3` and its fellows: a move with a shift. */
static int shift_move(arm_t *a, unsigned type) {
    const as_arm_req_t *rq = a->rq;
    int rd;
    int rm;
    int rs;
    long long n;

    if (need_reg(a, 0, &rd) != 0) {
        return -1;
    }
    if (type == 4) {
        if (rq->op_count != 2 || need_reg(a, 1, &rm) != 0) {
            return fail(a, "'%s': two registers", rq->mnemonic);
        }
        return word(a, (a->cond << 28) | 0x01a00060u | ((uint32_t)a->s << 20) | ((uint32_t)rd << 12) | (uint32_t)rm);
    }
    if (rq->op_count == 2) {
        rm = rd;
    } else if (rq->op_count != 3 || need_reg(a, 1, &rm) != 0) {
        return fail(a, "'%s': a register, a register and a count", rq->mnemonic);
    }
    rs = core_reg(rq->ops[rq->op_count - 1]);
    if (rs >= 0) {
        return word(a, (a->cond << 28) | 0x01a00010u | ((uint32_t)a->s << 20) | ((uint32_t)rd << 12) |
                           ((uint32_t)rs << 8) | (type << 5) | (uint32_t)rm);
    }
    if (number(rq->ops[rq->op_count - 1], &n) != 0) {
        return fail(a, "'%s': the count is a register or a number", rq->mnemonic);
    }
    if ((type == 1 || type == 2) && n == 32) {
        n = 0;
    } else if (n < 0 || n > 31) {
        return fail(a, "'%s': a shift of %lld", rq->mnemonic, n);
    } else if (n == 0) {
        type = 0;
    }
    return word(a, (a->cond << 28) | 0x01a00000u | ((uint32_t)a->s << 20) | ((uint32_t)rd << 12) | ((uint32_t)n << 7) |
                       (type << 5) | (uint32_t)rm);
}

/* movw and movt: sixteen bits, or half a symbol's address. */
static int move_wide(arm_t *a, int top) {
    const as_arm_req_t *rq = a->rq;
    const char *s;
    long long v = 0;
    int rd;

    if (rq->op_count != 2 || need_reg(a, 0, &rd) != 0) {
        return fail(a, "'%s': a register and a number", rq->mnemonic);
    }
    s = rq->ops[1];
    while (isspace((unsigned char)*s) || *s == '#') {
        s++;
    }
    if (strncasecmp(s, ":lower16:", 9) == 0 || strncasecmp(s, ":upper16:", 9) == 0) {
        char name[128];
        long long addend;
        int upper = tolower((unsigned char)s[1]) == 'u';

        if (symbol_ref(s + 9, name, sizeof(name), &addend) != 0) {
            return fail(a, "'%s': a symbol after %s", rq->mnemonic, upper ? ":upper16:" : ":lower16:");
        }
        if (fixup(a, upper ? R_ARM_MOVT_ABS : R_ARM_MOVW_ABS_NC, name, addend) != 0) {
            return -1;
        }
        /* (The addend is in the instruction, where an object of this
         * machine keeps it.) */
        v = addend & 0xffff;
    } else if (number(s, &v) != 0 || v < 0 || v > 0xffff) {
        return fail(a, "'%s': a number of 16 bits, and '%s' is not one", rq->mnemonic, rq->ops[1]);
    }
    return word(a, (a->cond << 28) | (top ? 0x03400000u : 0x03000000u) | (((uint32_t)v >> 12) << 16) |
                       ((uint32_t)rd << 12) | ((uint32_t)v & 0xfffu));
}

static int multiply(arm_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *b = a->base;
    int r[4] = {0, 0, 0, 0};
    size_t want = strcmp(b, "mul") == 0 ? 3 : 4;
    size_t i;
    uint32_t s = (uint32_t)a->s << 20;

    /* (`mul r0, r1` is `mul r0, r0, r1`.) */
    if (want == 3 && rq->op_count == 2) {
        if (need_reg(a, 0, &r[0]) != 0 || need_reg(a, 1, &r[2]) != 0) {
            return -1;
        }
        r[1] = r[0];
    } else {
        if (rq->op_count != want) {
            return fail(a, "'%s' takes %u registers", rq->mnemonic, (unsigned)want);
        }
        for (i = 0; i < want; ++i) {
            if (need_reg(a, i, &r[i]) != 0) {
                return -1;
            }
        }
    }
    if (strcmp(b, "mul") == 0) {
        return word(a, (a->cond << 28) | s | ((uint32_t)r[0] << 16) | ((uint32_t)r[2] << 8) | 0x90u | (uint32_t)r[1]);
    }
    if (strcmp(b, "mla") == 0 || strcmp(b, "mls") == 0) {
        return word(a, (a->cond << 28) | (b[2] == 'a' ? 0x00200000u : 0x00600000u) | s | ((uint32_t)r[0] << 16) |
                           ((uint32_t)r[3] << 12) | ((uint32_t)r[2] << 8) | 0x90u | (uint32_t)r[1]);
    }
    /* The long ones: low, high, and the two that are multiplied. */
    return word(a, (a->cond << 28) |
                       (strcmp(b, "umull") == 0   ? 0x00800000u
                        : strcmp(b, "umlal") == 0 ? 0x00a00000u
                        : strcmp(b, "smull") == 0 ? 0x00c00000u
                                                  : 0x00e00000u) |
                       s | ((uint32_t)r[1] << 16) | ((uint32_t)r[0] << 12) | ((uint32_t)r[3] << 8) | 0x90u |
                       (uint32_t)r[2]);
}

/* Two registers and a fixed pattern: divide, count zeros, reverse,
 * extend. */
static int two_or_three(arm_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *b = a->base;
    int rd;
    int rm;
    int rn;

    if (need_reg(a, 0, &rd) != 0 || need_reg(a, 1, &rm) != 0) {
        return -1;
    }
    if (strcmp(b, "sdiv") == 0 || strcmp(b, "udiv") == 0) {
        if (rq->op_count == 2) {
            /* (`sdiv r0, r1` divides r0 by r1.) */
            return word(a, (a->cond << 28) | (b[0] == 's' ? 0x0710f010u : 0x0730f010u) | ((uint32_t)rd << 16) |
                               ((uint32_t)rm << 8) | (uint32_t)rd);
        }
        if (rq->op_count != 3 || need_reg(a, 2, &rn) != 0) {
            return fail(a, "'%s' takes three registers", rq->mnemonic);
        }
        return word(a, (a->cond << 28) | (b[0] == 's' ? 0x0710f010u : 0x0730f010u) | ((uint32_t)rd << 16) |
                           ((uint32_t)rn << 8) | (uint32_t)rm);
    }
    if (rq->op_count != 2) {
        return fail(a, "'%s' takes two registers", rq->mnemonic);
    }
    return word(a, (a->cond << 28) |
                       (strcmp(b, "clz") == 0     ? 0x016f0f10u
                        : strcmp(b, "rev") == 0   ? 0x06bf0f30u
                        : strcmp(b, "rev16") == 0 ? 0x06bf0fb0u
                        : strcmp(b, "uxtb") == 0  ? 0x06ef0070u
                        : strcmp(b, "uxth") == 0  ? 0x06ff0070u
                        : strcmp(b, "sxtb") == 0  ? 0x06af0070u
                                                  : 0x06bf0070u) |
                       ((uint32_t)rd << 12) | (uint32_t)rm);
}

/*
 * An address in brackets, as the operands from `at` have it:
 *
 *   [rn]  [rn, #n]  [rn, #n]!  [rn], #n
 *   [rn, rm]  [rn, -rm, lsl #2]!  [rn], rm, lsl #2
 *
 * The parts the instruction is made of come back: the base, whether
 * the offset is added, whether it is applied before and whether the
 * base is written back, and the offset as a number or as a register
 * and its shift.
 */
typedef struct {
    int rn;
    int up;
    int pre;
    int writeback;
    int is_reg;
    long long imm;
    int rm;
    uint32_t shift; /* bits 11 to 5 of the instruction */
} address_t;

static int address(arm_t *a, size_t at, address_t *m) {
    const as_arm_req_t *rq = a->rq;
    char buf[128];
    char *parts[4];
    size_t part_count = 0;
    char *close;
    char *p;
    size_t i;

    memset(m, 0, sizeof(*m));
    m->up = 1;
    m->pre = 1;
    text(rq->ops[at], buf, sizeof(buf));
    if (buf[0] != '[' || (close = strchr(buf, ']')) == NULL) {
        return fail(a, "'%s': '%s' is not an address", rq->mnemonic, rq->ops[at]);
    }
    if (close[1] == '!') {
        m->writeback = 1;
    } else if (close[1] != '\0') {
        return fail(a, "'%s': what follows the ] of '%s'", rq->mnemonic, rq->ops[at]);
    }
    *close = '\0';
    for (p = buf + 1; part_count < 4;) {
        char *comma = strchr(p, ',');

        parts[part_count++] = p;
        if (comma == NULL) {
            break;
        }
        *comma = '\0';
        p = comma + 1;
    }
    m->rn = core_reg(parts[0]);
    if (m->rn < 0) {
        return fail(a, "'%s': the base of an address is a register", rq->mnemonic);
    }
    /* What is after the bracket is applied after: the offset of the
     * other form, with the base always written back. */
    if (at + 1 < rq->op_count) {
        static char after[2][64];

        if (part_count != 1 || m->writeback || rq->op_count - at - 1 > 2) {
            return fail(a, "'%s': an offset in the brackets and one after them", rq->mnemonic);
        }
        m->pre = 0;
        for (i = at + 1; i < rq->op_count; ++i) {
            text(rq->ops[i], after[i - at - 1], sizeof(after[0]));
            parts[part_count++] = after[i - at - 1];
        }
    }
    if (part_count == 1) {
        return 0;
    }
    p = parts[1];
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '#') {
        if (part_count != 2 || number(p, &m->imm) != 0) {
            return fail(a, "'%s': the offset of an address is a number or a register", rq->mnemonic);
        }
        /* (`#-0` is nothing, taken away.) */
        if (m->imm < 0 || strncmp(p, "#-", 2) == 0) {
            m->up = 0;
            m->imm = -m->imm;
        }
        return 0;
    }
    if (*p == '-') {
        m->up = 0;
        p++;
    } else if (*p == '+') {
        p++;
    }
    m->is_reg = 1;
    m->rm = core_reg(p);
    if (m->rm < 0) {
        return fail(a, "'%s': the offset of an address is a number or a register", rq->mnemonic);
    }
    if (part_count == 3) {
        uint32_t sh;

        if (shift_bits(a, parts[2], &sh) != 0) {
            return -1;
        }
        if ((sh & 0x10u) != 0) {
            return fail(a, "'%s': an index is shifted by a number", rq->mnemonic);
        }
        m->shift = sh;
    } else if (part_count != 2) {
        return fail(a, "'%s': too much in an address", rq->mnemonic);
    }
    return 0;
}

/* The distance from an instruction to a label of its section, counted
 * as the processor counts it, from eight bytes on.  0, or 1 if the
 * name is no label of this section. */
static int label_distance(arm_t *a, const char *name, long long addend, long long *distance) {
    uint64_t at = 0;

    if (a->rq->label == NULL || a->rq->label(a->rq->cookie, name, &at) != 0) {
        return 1;
    }
    *distance = (long long)at + addend - (long long)(a->rq->pc + 8);
    return 0;
}

static int load_store(arm_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *b = a->base;
    int load = b[0] == 'l';
    int byte = strcmp(b + 3, "b") == 0;
    /* The others have their offset in two halves and no shifted index:
     * the bits 6 and 5 say which, with bit 20. */
    int narrow = b[3] != '\0' && !byte;
    uint32_t sh_bits = 0;
    size_t addr_at = 1;
    address_t m;
    int rt;

    if (rq->op_count < 2 || need_reg(a, 0, &rt) != 0) {
        return fail(a, "'%s': a register and an address", rq->mnemonic);
    }
    /* Two words are two registers, the second the one after the first
     * and written or not. */
    if ((strcmp(b, "ldrd") == 0 || strcmp(b, "strd") == 0) && rq->op_count > 2 && core_reg(rq->ops[1]) >= 0) {
        if (core_reg(rq->ops[1]) != rt + 1 || (rt & 1) != 0) {
            return fail(a, "'%s': two registers, an even one and the one after it", rq->mnemonic);
        }
        addr_at = 2;
    }
    if (narrow) {
        sh_bits = strcmp(b, "strh") == 0    ? 0xb0u
                  : strcmp(b, "ldrh") == 0  ? 0xb0u
                  : strcmp(b, "ldrsb") == 0 ? 0xd0u
                  : strcmp(b, "ldrsh") == 0 ? 0xf0u
                  : strcmp(b, "ldrd") == 0  ? 0xd0u
                                            : 0xf0u;
        load = strcmp(b, "ldrh") == 0 || strcmp(b, "ldrsb") == 0 || strcmp(b, "ldrsh") == 0;
    }
    if (strchr(rq->ops[addr_at], '[') == NULL) {
        char name[128];
        long long addend;
        long long d;

        /* A label: from the program counter. */
        if (rq->op_count != addr_at + 1 || symbol_ref(rq->ops[addr_at], name, sizeof(name), &addend) != 0) {
            return fail(a, "'%s': '%s' is not an address", rq->mnemonic, rq->ops[addr_at]);
        }
        if (rq->label == NULL) {
            d = 0; /* (being measured: where the labels are is not known yet) */
        } else if (label_distance(a, name, addend, &d) != 0) {
            return fail(a, "'%s': '%s' is not a label of this section, and no other can be loaded from here",
                        rq->mnemonic, name);
        }
        memset(&m, 0, sizeof(m));
        m.rn = 15;
        m.pre = 1;
        m.up = d >= 0;
        m.imm = d >= 0 ? d : -d;
    } else if (address(a, addr_at, &m) != 0) {
        return -1;
    }
    if (narrow) {
        if (m.is_reg) {
            if (m.shift != 0) {
                return fail(a, "'%s': its index has no shift", rq->mnemonic);
            }
            return word(a, (a->cond << 28) | ((uint32_t)m.pre << 24) | ((uint32_t)m.up << 23) |
                               ((uint32_t)(m.pre && m.writeback) << 21) | ((uint32_t)load << 20) |
                               ((uint32_t)m.rn << 16) | ((uint32_t)rt << 12) | sh_bits | (uint32_t)m.rm);
        }
        if (m.imm > 255) {
            return fail(a, "'%s': an offset of %lld, and it has eight bits for one", rq->mnemonic, m.imm);
        }
        return word(a, (a->cond << 28) | ((uint32_t)m.pre << 24) | ((uint32_t)m.up << 23) | (1u << 22) |
                           ((uint32_t)(m.pre && m.writeback) << 21) | ((uint32_t)load << 20) |
                           ((uint32_t)m.rn << 16) | ((uint32_t)rt << 12) | (((uint32_t)m.imm >> 4) << 8) | sh_bits |
                           ((uint32_t)m.imm & 0xfu));
    }
    if (!m.is_reg && m.imm > 4095) {
        return fail(a, "'%s': an offset of %lld, and it has twelve bits for one", rq->mnemonic, m.imm);
    }
    return word(a, (a->cond << 28) | 0x04000000u | ((uint32_t)m.is_reg << 25) | ((uint32_t)m.pre << 24) |
                       ((uint32_t)m.up << 23) | ((uint32_t)byte << 22) | ((uint32_t)(m.pre && m.writeback) << 21) |
                       ((uint32_t)load << 20) | ((uint32_t)m.rn << 16) | ((uint32_t)rt << 12) |
                       (m.is_reg ? m.shift | (uint32_t)m.rm : (uint32_t)m.imm));
}

/* A list of registers in braces: 0 with a bit for each. */
static int reg_list(arm_t *a, const char *s, uint32_t *mask, unsigned *count, int *last) {
    char buf[160];
    char *p;
    char *close;

    text(s, buf, sizeof(buf));
    *mask = 0;
    *count = 0;
    if (buf[0] != '{' || (close = strchr(buf, '}')) == NULL) {
        return fail(a, "'%s': '%s' is not a list of registers", a->rq->mnemonic, s);
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
            return fail(a, "'%s': '%s' is not a list of registers", a->rq->mnemonic, s);
        }
        for (; lo <= hi; ++lo) {
            if ((*mask & (1u << lo)) == 0) {
                (*count)++;
            }
            *mask |= 1u << lo;
            *last = lo;
        }
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    return *count != 0 ? 0 : fail(a, "'%s': a list of no registers", a->rq->mnemonic);
}

static int load_store_multiple(arm_t *a) {
    /* The four ways, as P and U: and the stack's names for them, which
     * are one way for a load and the other for a store. */
    static const struct {
        const char *name;
        unsigned load_pu;
        unsigned store_pu;
    } modes[] = {{"", 1, 1},   {"ia", 1, 1}, {"ib", 3, 3}, {"da", 0, 0}, {"db", 2, 2},
                 {"fd", 1, 2}, {"ed", 3, 0}, {"fa", 0, 3}, {"ea", 2, 1}};
    const as_arm_req_t *rq = a->rq;
    const char *b = a->base;
    int load = b[0] == 'l' || strcmp(b, "pop") == 0;
    uint32_t mask;
    unsigned count;
    unsigned pu = 0;
    int last = 0;
    int rn;
    int writeback = 0;
    int user = 0;
    char buf[160];
    size_t i;

    if (strcmp(b, "push") == 0 || strcmp(b, "pop") == 0) {
        if (rq->op_count != 1 || reg_list(a, rq->ops[0], &mask, &count, &last) != 0) {
            return a->errbuf != NULL && a->errbuf[0] != '\0' ? -1 : fail(a, "'%s': a list of registers", rq->mnemonic);
        }
        /* One register is a store or a load of it, with the stack
         * pointer moved. */
        if (count == 1) {
            return word(a, (a->cond << 28) | (load ? 0x049d0004u : 0x052d0004u) | ((uint32_t)last << 12));
        }
        return word(a, (a->cond << 28) | (load ? 0x08bd0000u : 0x092d0000u) | mask);
    }
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (strcmp(b + 3, modes[i].name) == 0) {
            pu = load ? modes[i].load_pu : modes[i].store_pu;
            break;
        }
    }
    if (rq->op_count != 2) {
        return fail(a, "'%s': a register and a list of registers", rq->mnemonic);
    }
    text(rq->ops[0], buf, sizeof(buf));
    if (buf[0] != '\0' && buf[strlen(buf) - 1] == '!') {
        writeback = 1;
        buf[strlen(buf) - 1] = '\0';
    }
    rn = core_reg(buf);
    if (rn < 0) {
        return fail(a, "'%s': '%s' is not a register", rq->mnemonic, rq->ops[0]);
    }
    text(rq->ops[1], buf, sizeof(buf));
    if (buf[0] != '\0' && buf[strlen(buf) - 1] == '^') {
        user = 1;
        buf[strlen(buf) - 1] = '\0';
    }
    if (reg_list(a, buf, &mask, &count, &last) != 0) {
        return -1;
    }
    return word(a, (a->cond << 28) | 0x08000000u | ((pu >> 1) << 24) | ((pu & 1u) << 23) | ((uint32_t)user << 22) |
                       ((uint32_t)writeback << 21) | ((uint32_t)load << 20) | ((uint32_t)rn << 16) | mask);
}

static int branch(arm_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *b = a->base;
    char name[128];
    long long addend;
    long long d;
    int rm;

    if (rq->op_count != 1) {
        return fail(a, "'%s' takes one operand", rq->mnemonic);
    }
    rm = core_reg(rq->ops[0]);
    if (strcmp(b, "bx") == 0 || (strcmp(b, "blx") == 0 && rm >= 0)) {
        if (rm < 0) {
            return fail(a, "'%s': a register", rq->mnemonic);
        }
        return word(a, (a->cond << 28) | (b[1] == 'x' ? 0x012fff10u : 0x012fff30u) | (uint32_t)rm);
    }
    if (symbol_ref(rq->ops[0], name, sizeof(name), &addend) != 0) {
        return fail(a, "'%s': '%s' is not where a branch can go", rq->mnemonic, rq->ops[0]);
    }
    if (strcmp(b, "blx") == 0) {
        /* To Thumb code: its condition is none, and a bit of it is the
         * half word of the distance. */
        if (a->cond != COND_AL) {
            return fail(a, "'%s': a call that changes to Thumb has no condition", rq->mnemonic);
        }
        if (label_distance(a, name, addend, &d) == 0) {
            if ((d & 1) != 0 || d < -0x2000000ll || d > 0x1fffffell) {
                return fail(a, "'%s': '%s' is %lld bytes away, which it cannot reach", rq->mnemonic, name, d);
            }
            return word(a, 0xfa000000u | (((uint32_t)d & 2u) << 23) | (((uint32_t)d >> 2) & 0xffffffu));
        }
        if (fixup(a, R_ARM_CALL, name, addend) != 0) {
            return -1;
        }
        return word(a, 0xfa000000u | ((uint32_t)((addend - 8) >> 2) & 0xffffffu));
    }
    if (label_distance(a, name, addend, &d) == 0) {
        if ((d & 3) != 0) {
            return fail(a, "'%s': '%s' is not at a multiple of four bytes from here", rq->mnemonic, name);
        }
        if (d < -0x2000000ll || d > 0x1fffffcll) {
            return fail(a, "'%s': '%s' is %lld bytes away, which it cannot reach", rq->mnemonic, name, d);
        }
        return word(a, (a->cond << 28) | (b[1] == 'l' ? 0x0b000000u : 0x0a000000u) | (((uint32_t)d >> 2) & 0xffffffu));
    }
    /* A call with no condition may be made one to Thumb by the linker;
     * any other branch may not. */
    if (fixup(a, b[1] == 'l' && a->cond == COND_AL ? R_ARM_CALL : R_ARM_JUMP24, name, addend) != 0) {
        return -1;
    }
    return word(a, (a->cond << 28) | (b[1] == 'l' ? 0x0b000000u : 0x0a000000u) |
                       ((uint32_t)((addend - 8) >> 2) & 0xffffffu));
}

static int system_instruction(arm_t *a) {
    const as_arm_req_t *rq = a->rq;
    const char *b = a->base;
    char buf[32];
    long long n;
    int r;

    if (strcmp(b, "svc") == 0 || strcmp(b, "swi") == 0) {
        if (rq->op_count != 1 || number(rq->ops[0], &n) != 0 || n < 0 || n > 0xffffff) {
            return fail(a, "'%s': a number of 24 bits", rq->mnemonic);
        }
        return word(a, (a->cond << 28) | 0x0f000000u | (uint32_t)n);
    }
    if (strcmp(b, "bkpt") == 0) {
        if (rq->op_count != 1 || number(rq->ops[0], &n) != 0 || n < 0 || n > 0xffff) {
            return fail(a, "'%s': a number of 16 bits", rq->mnemonic);
        }
        return word(a, 0xe1200070u | (((uint32_t)n >> 4) << 8) | ((uint32_t)n & 0xfu));
    }
    if (strcmp(b, "nop") == 0 || strcmp(b, "yield") == 0 || strcmp(b, "wfe") == 0 || strcmp(b, "wfi") == 0 ||
        strcmp(b, "sev") == 0) {
        if (rq->op_count != 0) {
            return fail(a, "'%s' takes no operand", rq->mnemonic);
        }
        return word(a, (a->cond << 28) | 0x0320f000u |
                           (b[0] == 'n' ? 0u : b[0] == 'y' ? 1u : strcmp(b, "wfe") == 0 ? 2u : b[0] == 'w' ? 3u : 4u));
    }
    if (strcmp(b, "mrs") == 0) {
        if (rq->op_count != 2 || need_reg(a, 0, &r) != 0) {
            return fail(a, "'%s': a register and a status register", rq->mnemonic);
        }
        text(rq->ops[1], buf, sizeof(buf));
        if (strcmp(buf, "cpsr") != 0 && strcmp(buf, "apsr") != 0 && strcmp(buf, "spsr") != 0) {
            return fail(a, "'%s': '%s' is not a status register", rq->mnemonic, rq->ops[1]);
        }
        return word(a, (a->cond << 28) | 0x010f0000u | (buf[0] == 's' ? 1u << 22 : 0u) | ((uint32_t)r << 12));
    }
    if (strcmp(b, "msr") == 0) {
        uint32_t mask = 0;
        uint32_t op2 = 0;
        long long value = 0;
        char *under;
        int spsr;
        int rc;

        if (rq->op_count != 2) {
            return fail(a, "'%s': a status register and a value", rq->mnemonic);
        }
        text(rq->ops[0], buf, sizeof(buf));
        under = strchr(buf, '_');
        if (under != NULL) {
            *under++ = '\0';
        }
        spsr = strcmp(buf, "spsr") == 0;
        if (!spsr && strcmp(buf, "cpsr") != 0 && strcmp(buf, "apsr") != 0) {
            return fail(a, "'%s': '%s' is not a status register", rq->mnemonic, rq->ops[0]);
        }
        if (under == NULL) {
            /* All of the flags, and of CPSR the control bits too. */
            mask = buf[0] == 'a' ? 8u : 9u;
        } else if (strcmp(under, "nzcvq") == 0) {
            mask = 8;
        } else if (strcmp(under, "g") == 0) {
            mask = 4;
        } else if (strcmp(under, "nzcvqg") == 0) {
            mask = 12;
        } else {
            for (; *under != '\0'; ++under) {
                const char *which = strchr("cxsf", *under);

                if (which == NULL) {
                    return fail(a, "'%s': '%s' is not a part of a status register", rq->mnemonic, rq->ops[0]);
                }
                mask |= 1u << (which - "cxsf");
            }
        }
        rc = operand2(a, 1, &op2, &value);
        if (rc != 0 || ((op2 & 0xff0u) != 0 && (op2 & (1u << 25)) == 0)) {
            return rc == -1 ? -1 : fail(a, "'%s': a register, or a number it can hold", rq->mnemonic);
        }
        return word(a, (a->cond << 28) | 0x0120f000u | (op2 & (1u << 25)) | ((uint32_t)spsr << 22) | (mask << 16) |
                           (op2 & 0xfffu));
    }
    /* mrc and mcr: a coprocessor, a number, the core register, two of
     * the coprocessor's and another number. */
    {
        long long opc1;
        long long opc2 = 0;
        unsigned cp;
        unsigned crn;
        unsigned crm;
        char c1[16];
        char c2[16];
        char cn[16];

        if (rq->op_count < 5 || rq->op_count > 6 || need_reg(a, 2, &r) != 0 || number(rq->ops[1], &opc1) != 0 ||
            (rq->op_count == 6 && number(rq->ops[5], &opc2) != 0)) {
            return fail(a, "'%s': a coprocessor, a number, a register, two of its registers and a number",
                        rq->mnemonic);
        }
        text(rq->ops[0], c1, sizeof(c1));
        text(rq->ops[3], cn, sizeof(cn));
        text(rq->ops[4], c2, sizeof(c2));
        if (c1[0] != 'p' || cn[0] != 'c' || c2[0] != 'c' || opc1 < 0 || opc1 > 7 || opc2 < 0 || opc2 > 7) {
            return fail(a, "'%s': a coprocessor is pN and its registers cN", rq->mnemonic);
        }
        cp = (unsigned)strtoul(c1 + 1, NULL, 10);
        crn = (unsigned)strtoul(cn + 1, NULL, 10);
        crm = (unsigned)strtoul(c2 + 1, NULL, 10);
        if (cp > 15 || crn > 15 || crm > 15) {
            return fail(a, "'%s': a number past 15", rq->mnemonic);
        }
        return word(a, (a->cond << 28) | 0x0e000010u | ((uint32_t)opc1 << 21) | (b[1] == 'r' ? 1u << 20 : 0u) |
                           (crn << 16) | ((uint32_t)r << 12) | (cp << 8) | ((uint32_t)opc2 << 5) | crm);
    }
}

/* adr: the address of a label of this section, as the program counter
 * and a number. */
static int address_of(arm_t *a) {
    const as_arm_req_t *rq = a->rq;
    char name[128];
    long long addend;
    long long d;
    uint32_t enc;
    int rd;

    if (rq->op_count != 2 || need_reg(a, 0, &rd) != 0 || symbol_ref(rq->ops[1], name, sizeof(name), &addend) != 0) {
        return fail(a, "'%s': a register and a label", rq->mnemonic);
    }
    if (rq->label == NULL) {
        d = 0;
    } else if (label_distance(a, name, addend, &d) != 0) {
        return fail(a, "'%s': '%s' is not a label of this section", rq->mnemonic, name);
    }
    if (modified_imm((uint32_t)(d >= 0 ? d : -d), &enc) != 0) {
        return fail(a, "'%s': '%s' is %lld bytes away, which is not a number it can hold", rq->mnemonic, name, d);
    }
    return word(a, (a->cond << 28) | (d >= 0 ? 0x028f0000u : 0x024f0000u) | ((uint32_t)rd << 12) | enc);
}

int as_arm_encode(const as_arm_req_t *rq, as_arm_out_t *out, char *errbuf, size_t errbuf_sz) {
    static const char *const dp[16] = {"and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
                                       "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"};
    static const char *const shifts[5] = {"lsl", "lsr", "asr", "ror", "rrx"};
    arm_t a;
    const char *b;
    unsigned i;

    memset(&a, 0, sizeof(a));
    memset(out, 0, sizeof(*out));
    a.rq = rq;
    a.out = out;
    a.errbuf = errbuf;
    a.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz != 0) {
        errbuf[0] = '\0';
    }
    {
        /* it, itt, ite and the rest: Thumb's, and in ARM code -- where
         * each instruction has its own condition -- nothing. */
        char m[8];
        size_t n;

        text(rq->mnemonic, m, sizeof(m));
        n = strlen(m);
        if (n >= 2 && n <= 4 && m[0] == 'i' && m[1] == 't' && strspn(m + 2, "te") == n - 2 &&
            strlen(rq->mnemonic) == n) {
            return rq->thumb ? as_arm_thumb(rq, "it", 0, COND_AL, out, errbuf, errbuf_sz) : 0;
        }
    }
    if (as_arm_split_mnemonic(rq->mnemonic, a.base, sizeof(a.base), &a.s, &a.cond) != 0) {
        return fail(&a, "'%s' is not an instruction of ARM that this assembler has", rq->mnemonic);
    }
    b = a.base;
    if (rq->thumb) {
        return as_arm_thumb(rq, b, a.s, a.cond, out, errbuf, errbuf_sz);
    }
    if (b[0] == 'v') {
        return as_arm_vfp_neon(rq, b, a.cond, out, errbuf, errbuf_sz);
    }
    for (i = 0; i < 16; ++i) {
        if (strcmp(b, dp[i]) == 0) {
            return data_processing(&a, i);
        }
    }
    for (i = 0; i < 5; ++i) {
        if (strcmp(b, shifts[i]) == 0) {
            return shift_move(&a, i);
        }
    }
    if (strcmp(b, "movw") == 0 || strcmp(b, "movt") == 0) {
        return move_wide(&a, b[3] == 't');
    }
    if (strcmp(b, "mul") == 0 || strcmp(b, "mla") == 0 || strcmp(b, "mls") == 0 || strcmp(b + 1, "mull") == 0 ||
        strcmp(b + 1, "mlal") == 0) {
        return multiply(&a);
    }
    if (strcmp(b, "sdiv") == 0 || strcmp(b, "udiv") == 0 || strcmp(b, "clz") == 0 || strncmp(b, "rev", 3) == 0 ||
        strncmp(b, "uxt", 3) == 0 || strncmp(b, "sxt", 3) == 0) {
        return two_or_three(&a);
    }
    if (strncmp(b, "ldm", 3) == 0 || strncmp(b, "stm", 3) == 0 || strcmp(b, "push") == 0 || strcmp(b, "pop") == 0) {
        return load_store_multiple(&a);
    }
    if (strncmp(b, "ldr", 3) == 0 || strncmp(b, "str", 3) == 0) {
        return load_store(&a);
    }
    if (strcmp(b, "b") == 0 || strcmp(b, "bl") == 0 || strcmp(b, "bx") == 0 || strcmp(b, "blx") == 0) {
        return branch(&a);
    }
    if (strcmp(b, "adr") == 0) {
        return address_of(&a);
    }
    return system_instruction(&a);
}
