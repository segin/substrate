#include "as_arm_int.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * VFP and NEON, in ARM code.  See as_arm.h.
 *
 * Several names are both -- `vadd` adds two numbers and adds two
 * vectors.  Which is meant is in the registers and in what follows the
 * dot: an `s` register, or a `d` register with `.f64` or with nothing,
 * is one number, and anything else is a vector.
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
    unsigned cond;
    char suffix[16]; /* what follows the mnemonic's dot, in lower case */
} simd_t;

/* A register of the extension. */
typedef struct {
    char kind; /* s, d or q */
    unsigned n;
} vreg_t;

static int fail(simd_t *v, const char *fmt, ...) {
    va_list ap;

    if (v->errbuf != NULL && v->errbuf_sz != 0) {
        va_start(ap, fmt);
        vsnprintf(v->errbuf, v->errbuf_sz, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int word(simd_t *v, uint32_t w) {
    unsigned i;

    for (i = 0; i < 4; ++i) {
        v->out->bytes[i] = (uint8_t)(w >> (8 * i));
    }
    v->out->len = 4;
    return 0;
}

static int vreg(const char *s, vreg_t *r) {
    char buf[16];
    char *end;
    unsigned long n;

    text(s, buf, sizeof(buf));
    if ((buf[0] != 's' && buf[0] != 'd' && buf[0] != 'q') || !isdigit((unsigned char)buf[1]) ||
        (buf[1] == '0' && buf[2] != '\0')) {
        return -1;
    }
    n = strtoul(buf + 1, &end, 10);
    if (*end != '\0' || n > (buf[0] == 'q' ? 15u : 31u)) {
        return -1;
    }
    r->kind = buf[0];
    r->n = (unsigned)n;
    return 0;
}

/* The register's five bits: four in its field, and the fifth where
 * `bit` says.  Of an `s` the fifth is the low one; of a `d` the high. */
static uint32_t field(const vreg_t *r, unsigned at, unsigned bit) {
    unsigned n = r->kind == 'q' ? r->n * 2 : r->n;

    if (r->kind == 's') {
        return ((uint32_t)(n >> 1) << at) | ((uint32_t)(n & 1u) << bit);
    }
    return ((uint32_t)(n & 15u) << at) | ((uint32_t)(n >> 4) << bit);
}

#define VD(r) field((r), 12, 22)
#define VN(r) field((r), 16, 7)
#define VM(r) field((r), 0, 5)

/* The size after the dot: its two bits, with the letter before the
 * number (i, s, u, f, p or none) in *kind.  -1 for none written. */
static int vec_size(const simd_t *v, char *kind) {
    const char *p = v->suffix;

    *kind = '\0';
    if (isalpha((unsigned char)*p)) {
        *kind = *p++;
    }
    if (strcmp(p, "8") == 0) {
        return 0;
    }
    if (strcmp(p, "16") == 0) {
        return 1;
    }
    if (strcmp(p, "32") == 0) {
        return 2;
    }
    if (strcmp(p, "64") == 0) {
        return 3;
    }
    return -1;
}

/* Three registers of one kind. */
static int three(simd_t *v, vreg_t r[3]) {
    const as_arm_req_t *rq = v->rq;

    if (rq->op_count == 2) {
        if (vreg(rq->ops[0], &r[0]) != 0 || vreg(rq->ops[1], &r[2]) != 0) {
            return -1;
        }
        r[1] = r[0];
    } else if (rq->op_count != 3 || vreg(rq->ops[0], &r[0]) != 0 || vreg(rq->ops[1], &r[1]) != 0 ||
               vreg(rq->ops[2], &r[2]) != 0) {
        return -1;
    }
    return r[0].kind == r[1].kind && r[1].kind == r[2].kind ? 0 : -1;
}

/* Whether the operands and the suffix say one number, not a vector. */
static int is_scalar(const simd_t *v) {
    vreg_t r;

    if (v->rq->op_count == 0 || vreg(v->rq->ops[0], &r) != 0) {
        return 0;
    }
    return r.kind == 's' || (r.kind == 'd' && (v->suffix[0] == '\0' || strcmp(v->suffix, "f64") == 0));
}

/* An eight-bit float: the number's sign, three bits of exponent and
 * four of fraction. */
static int float_imm8(double d, unsigned *imm8) {
    float f = (float)d;
    uint32_t bits;
    uint32_t b;

    if ((double)f != d) {
        return -1;
    }
    memcpy(&bits, &f, sizeof(bits));
    b = (bits >> 29) & 1u;
    if ((bits & 0x7ffffu) != 0 || ((bits >> 30) & 1u) == b || ((bits >> 25) & 0x1fu) != (b ? 0x1fu : 0u)) {
        return -1;
    }
    *imm8 = ((bits >> 31) << 7) | (b << 6) | ((bits >> 19) & 0x3fu);
    return 0;
}

/* The arithmetic of one number or two. */
static int vfp_arith(simd_t *v) {
    static const struct {
        const char *name;
        uint32_t w;
    } ops[] = {{"vmla", 0x0e000a00u},  {"vmls", 0x0e000a40u},  {"vnmls", 0x0e100a00u}, {"vnmla", 0x0e100a40u},
               {"vmul", 0x0e200a00u},  {"vnmul", 0x0e200a40u}, {"vadd", 0x0e300a00u},  {"vsub", 0x0e300a40u},
               {"vdiv", 0x0e800a00u},  {"vfnms", 0x0e900a00u}, {"vfnma", 0x0e900a40u}, {"vfma", 0x0ea00a00u},
               {"vfms", 0x0ea00a40u}};
    static const struct {
        const char *name;
        uint32_t w;
    } two[] = {{"vabs", 0x0eb00ac0u}, {"vneg", 0x0eb10a40u}, {"vsqrt", 0x0eb10ac0u},
               {"vcmp", 0x0eb40a40u}, {"vcmpe", 0x0eb40ac0u}};
    const as_arm_req_t *rq = v->rq;
    vreg_t r[3];
    size_t i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (strcmp(v->base, ops[i].name) == 0) {
            if (three(v, r) != 0 || r[0].kind == 'q') {
                return fail(v, "'%s': three registers of one kind, s or d", rq->mnemonic);
            }
            return word(v, (v->cond << 28) | ops[i].w | (r[0].kind == 'd' ? 0x100u : 0u) | VD(&r[0]) | VN(&r[1]) |
                               VM(&r[2]));
        }
    }
    for (i = 0; i < sizeof(two) / sizeof(two[0]); ++i) {
        if (strcmp(v->base, two[i].name) == 0) {
            if (rq->op_count != 2 || vreg(rq->ops[0], &r[0]) != 0 || r[0].kind == 'q') {
                return fail(v, "'%s': two registers of one kind, s or d", rq->mnemonic);
            }
            if (vreg(rq->ops[1], &r[1]) != 0) {
                long long n;

                /* A comparison with nothing. */
                if (v->base[1] != 'c' || number(rq->ops[1], &n) != 0 || n != 0) {
                    return fail(v, "'%s': '%s' is not a register", rq->mnemonic, rq->ops[1]);
                }
                return word(v, (v->cond << 28) | (two[i].w | 0x00010000u) | (r[0].kind == 'd' ? 0x100u : 0u) |
                                   VD(&r[0]));
            }
            if (r[1].kind != r[0].kind) {
                return fail(v, "'%s': two registers of one kind", rq->mnemonic);
            }
            return word(v, (v->cond << 28) | two[i].w | (r[0].kind == 'd' ? 0x100u : 0u) | VD(&r[0]) | VM(&r[1]));
        }
    }
    return 1;
}

/* vcvt: between the two sizes of float, and between a float and an
 * integer. */
static int vfp_convert(simd_t *v) {
    const as_arm_req_t *rq = v->rq;
    const char *sx = v->suffix;
    vreg_t d;
    vreg_t m;

    if (rq->op_count != 2 || vreg(rq->ops[0], &d) != 0 || vreg(rq->ops[1], &m) != 0 || d.kind == 'q' ||
        m.kind == 'q') {
        return fail(v, "'%s': two registers, s or d", rq->mnemonic);
    }
    if (strcmp(sx, "f64.f32") == 0 || (sx[0] == '\0' && d.kind == 'd' && m.kind == 's')) {
        if (d.kind != 'd' || m.kind != 's') {
            return fail(v, "'%s': a d register from an s", rq->mnemonic);
        }
        return word(v, (v->cond << 28) | 0x0eb70ac0u | VD(&d) | VM(&m));
    }
    if (strcmp(sx, "f32.f64") == 0 || (sx[0] == '\0' && d.kind == 's' && m.kind == 'd')) {
        if (d.kind != 's' || m.kind != 'd') {
            return fail(v, "'%s': an s register from a d", rq->mnemonic);
        }
        return word(v, (v->cond << 28) | 0x0eb70bc0u | VD(&d) | VM(&m));
    }
    if ((sx[0] == 's' || sx[0] == 'u') && strncmp(sx + 1, "32.f", 4) == 0) {
        /* To an integer, toward zero. */
        int dbl = strcmp(sx + 5, "64") == 0;

        if (d.kind != 's' || m.kind != (dbl ? 'd' : 's')) {
            return fail(v, "'%s': an s register from the float's", rq->mnemonic);
        }
        return word(v, (v->cond << 28) | (sx[0] == 's' ? 0x0ebd0ac0u : 0x0ebc0ac0u) | (dbl ? 0x100u : 0u) | VD(&d) |
                           VM(&m));
    }
    if (sx[0] == 'f' && (strcmp(sx + 3, ".s32") == 0 || strcmp(sx + 3, ".u32") == 0)) {
        int dbl = strncmp(sx + 1, "64", 2) == 0;

        if (m.kind != 's' || d.kind != (dbl ? 'd' : 's')) {
            return fail(v, "'%s': the float's register from an s", rq->mnemonic);
        }
        return word(v, (v->cond << 28) | (sx[4] == 's' ? 0x0eb80ac0u : 0x0eb80a40u) | (dbl ? 0x100u : 0u) | VD(&d) |
                           VM(&m));
    }
    return fail(v, "'%s': from what to what is not said, or is not a conversion it has", rq->mnemonic);
}

/* A NEON immediate: the eight bits where the instruction has them. */
static uint32_t neon_imm8(unsigned imm8) {
    return ((uint32_t)(imm8 >> 7) << 24) | ((uint32_t)((imm8 >> 4) & 7u) << 16) | (imm8 & 15u);
}

/* vmov, in its many forms. */
static int vmov(simd_t *v) {
    const as_arm_req_t *rq = v->rq;
    vreg_t a;
    vreg_t b;
    int ra = rq->op_count > 0 ? core_reg(rq->ops[0]) : -1;
    int rb = rq->op_count > 1 ? core_reg(rq->ops[1]) : -1;
    int have_a = rq->op_count > 0 && vreg(rq->ops[0], &a) == 0;
    int have_b = rq->op_count > 1 && vreg(rq->ops[1], &b) == 0;

    if (rq->op_count == 3) {
        vreg_t d;
        int r2 = core_reg(rq->ops[2]);

        /* A d register and two core registers, either way. */
        if (have_a && a.kind == 'd' && rb >= 0 && r2 >= 0) {
            return word(v, (v->cond << 28) | 0x0c400b10u | ((uint32_t)r2 << 16) | ((uint32_t)rb << 12) | VM(&a));
        }
        if (ra >= 0 && rb >= 0 && vreg(rq->ops[2], &d) == 0 && d.kind == 'd') {
            return word(v, (v->cond << 28) | 0x0c500b10u | ((uint32_t)rb << 16) | ((uint32_t)ra << 12) | VM(&d));
        }
        return fail(v, "'%s': a d register and two core registers", rq->mnemonic);
    }
    if (rq->op_count != 2) {
        return fail(v, "'%s' takes two operands, or three", rq->mnemonic);
    }
    if (have_a && a.kind == 's' && rb >= 0) {
        return word(v, (v->cond << 28) | 0x0e000a10u | ((uint32_t)rb << 12) | VN(&a));
    }
    if (ra >= 0 && have_b && b.kind == 's') {
        return word(v, (v->cond << 28) | 0x0e100a10u | ((uint32_t)ra << 12) | VN(&b));
    }
    if (have_a && have_b) {
        if (a.kind != b.kind) {
            return fail(v, "'%s': two registers of one kind", rq->mnemonic);
        }
        if (a.kind == 'q' || (a.kind == 'd' && v->suffix[0] != '\0' && strcmp(v->suffix, "f64") != 0)) {
            /* A vector moved is ORed with itself. */
            if (v->cond != COND_AL) {
                return fail(v, "'%s': a vector instruction has no condition", rq->mnemonic);
            }
            return word(v, 0xf2200110u | (a.kind == 'q' ? 0x40u : 0u) | VD(&a) | VN(&b) | VM(&b));
        }
        return word(v, (v->cond << 28) | 0x0eb00a40u | (a.kind == 'd' ? 0x100u : 0u) | VD(&a) | VM(&b));
    }
    if (have_a) {
        const char *p = rq->ops[1];
        char kind;
        int size = vec_size(v, &kind);
        long long n;

        while (*p == '#' || isspace((unsigned char)*p)) {
            p++;
        }
        if (a.kind == 's' || (a.kind == 'd' && (v->suffix[0] == '\0' || strcmp(v->suffix, "f64") == 0))) {
            unsigned imm8;
            char *end;
            double d = strtod(p, &end);

            if (end == p || *end != '\0' || float_imm8(d, &imm8) != 0) {
                return fail(v, "'%s': '%s' is not a float of eight bits", rq->mnemonic, rq->ops[1]);
            }
            return word(v, (v->cond << 28) | 0x0eb00a00u | (a.kind == 'd' ? 0x100u : 0u) | VD(&a) |
                               ((uint32_t)(imm8 >> 4) << 16) | (imm8 & 15u));
        }
        /* A vector of one number. */
        if (v->cond != COND_AL || size < 0 || kind != 'i' || number(rq->ops[1], &n) != 0) {
            return fail(v, "'%s': a vector, the size of its elements (.i8 to .i64) and a number", rq->mnemonic);
        }
        {
            uint32_t q = a.kind == 'q' ? 0x40u : 0u;
            uint64_t u = (uint64_t)n;
            unsigned k;

            if (size == 0) {
                if (u > 0xff && (int64_t)u < -128) {
                    return fail(v, "'%s': %lld is not a byte", rq->mnemonic, n);
                }
                return word(v, 0xf2800e10u | q | VD(&a) | neon_imm8((unsigned)(u & 0xff)));
            }
            if (size == 3) {
                unsigned imm8 = 0;

                /* Each byte all ones or none. */
                for (k = 0; k < 8; ++k) {
                    unsigned byte = (unsigned)(u >> (8 * k)) & 0xffu;

                    if (byte != 0 && byte != 0xff) {
                        return fail(v, "'%s': each byte of the number is 0 or 0xff", rq->mnemonic);
                    }
                    imm8 |= (byte & 1u) << k;
                }
                return word(v, 0xf2800e30u | q | VD(&a) | neon_imm8(imm8));
            }
            if (size == 1) {
                u &= 0xffffu;
            } else {
                u &= 0xffffffffu;
            }
            for (k = 0; k < (size == 1 ? 2u : 4u); ++k) {
                if ((u & ~((uint64_t)0xff << (8 * k))) == 0) {
                    return word(v, 0xf2800010u | ((size == 1 ? 8u + 2 * k : 2 * k) << 8) | q | VD(&a) |
                                       neon_imm8((unsigned)(u >> (8 * k))));
                }
            }
            /* A byte with ones below it. */
            if (size == 2 && (u & 0xffff00ffu) == 0xffu) {
                return word(v, 0xf2800c10u | q | VD(&a) | neon_imm8((unsigned)(u >> 8)));
            }
            if (size == 2 && (u & 0xff00ffffu) == 0xffffu) {
                return word(v, 0xf2800d10u | q | VD(&a) | neon_imm8((unsigned)(u >> 16)));
            }
            return fail(v, "'%s': %lld is not a number it can hold: a byte, shifted by whole bytes", rq->mnemonic,
                        n);
        }
    }
    return fail(v, "'%s': what is moved, and to what", rq->mnemonic);
}

/* vldr and vstr. */
static int vfp_load_store(simd_t *v) {
    const as_arm_req_t *rq = v->rq;
    uint32_t w = v->base[1] == 'l' ? 0x0d100a00u : 0x0d000a00u;
    char buf[64];
    char *close;
    char *comma;
    long long imm = 0;
    vreg_t r;
    int rn;

    if (rq->op_count != 2 || vreg(rq->ops[0], &r) != 0 || r.kind == 'q') {
        return fail(v, "'%s': a register, s or d, and an address", rq->mnemonic);
    }
    w |= (v->cond << 28) | (r.kind == 'd' ? 0x100u : 0u) | VD(&r);
    text(rq->ops[1], buf, sizeof(buf));
    if (buf[0] != '[') {
        char name[128];
        long long addend;
        uint64_t at = 0;

        if (symbol_ref(rq->ops[1], name, sizeof(name), &addend) != 0) {
            return fail(v, "'%s': '%s' is not an address", rq->mnemonic, rq->ops[1]);
        }
        if (rq->label != NULL) {
            if (rq->label(rq->cookie, name, &at) != 0) {
                return fail(v, "'%s': '%s' is not a label of this section", rq->mnemonic, name);
            }
            imm = (long long)at + addend - (long long)(rq->pc + 8);
        }
        rn = 15;
    } else {
        close = strchr(buf, ']');
        if (close == NULL || close[1] != '\0') {
            return fail(v, "'%s': '%s' is not a base and an offset", rq->mnemonic, rq->ops[1]);
        }
        *close = '\0';
        comma = strchr(buf, ',');
        if (comma != NULL) {
            *comma = '\0';
            if (number(comma + 1, &imm) != 0) {
                return fail(v, "'%s': the offset is a number", rq->mnemonic);
            }
        }
        rn = core_reg(buf + 1);
        if (rn < 0) {
            return fail(v, "'%s': the base of an address is a register", rq->mnemonic);
        }
    }
    if ((imm & 3) != 0 || imm < -1020 || imm > 1020) {
        return fail(v, "'%s': an offset of %lld, and it is a multiple of four up to 1020", rq->mnemonic, imm);
    }
    return word(v, w | (imm >= 0 ? 0x00800000u : 0u) | ((uint32_t)rn << 16) | (uint32_t)((imm >= 0 ? imm : -imm) >> 2));
}

/* {d8-d15}: the first register and how many. */
static int vreg_list(const char *s, vreg_t *first, unsigned *count) {
    char buf[96];
    char *close;
    char *p;
    unsigned next = 0;

    text(s, buf, sizeof(buf));
    *count = 0;
    if (buf[0] != '{' || (close = strchr(buf, '}')) == NULL) {
        return -1;
    }
    *close = '\0';
    for (p = buf + 1; *p != '\0';) {
        char *comma = strchr(p, ',');
        char *dash;
        vreg_t lo;
        vreg_t hi;

        if (comma != NULL) {
            *comma = '\0';
        }
        dash = strchr(p, '-');
        if (dash != NULL) {
            *dash = '\0';
        }
        if (vreg(p, &lo) != 0 || (dash != NULL && vreg(dash + 1, &hi) != 0)) {
            return -1;
        }
        if (dash == NULL) {
            hi = lo;
        }
        if (hi.kind != lo.kind || hi.n < lo.n || lo.kind == 'q') {
            return -1;
        }
        if (*count == 0) {
            *first = lo;
        } else if (lo.kind != first->kind || lo.n != next) {
            return -1; /* (they are one after another) */
        }
        *count += hi.n - lo.n + 1;
        next = hi.n + 1;
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    return *count != 0 ? 0 : -1;
}

/* vpush, vpop, and the loads and stores of several. */
static int vfp_multiple(simd_t *v) {
    const as_arm_req_t *rq = v->rq;
    const char *b = v->base;
    vreg_t first;
    unsigned count;
    uint32_t w;
    size_t list_at = 0;
    int rn = 13;
    int wb = 1;

    if (strcmp(b, "vpush") == 0) {
        w = 0x0d200a00u;
    } else if (strcmp(b, "vpop") == 0) {
        w = 0x0c900a00u;
    } else {
        char buf[16];
        size_t n;

        text(rq->ops[0], buf, sizeof(buf));
        n = strlen(buf);
        wb = n != 0 && buf[n - 1] == '!';
        if (wb) {
            buf[n - 1] = '\0';
        }
        rn = core_reg(buf);
        if (rq->op_count != 2 || rn < 0) {
            return fail(v, "'%s': a base register and a list", rq->mnemonic);
        }
        list_at = 1;
        if (strcmp(b + 4, "db") == 0) {
            if (!wb) {
                return fail(v, "'%s': going down, the base is written back", rq->mnemonic);
            }
            w = b[1] == 'l' ? 0x0d300a00u : 0x0d200a00u;
            wb = 0; /* (in the pattern) */
        } else {
            w = b[1] == 'l' ? 0x0c900a00u : 0x0c800a00u;
        }
    }
    if (rq->op_count != list_at + 1 || vreg_list(rq->ops[list_at], &first, &count) != 0) {
        return fail(v, "'%s': a list of s or d registers, one after another", rq->mnemonic);
    }
    if (first.kind == 'd') {
        if (count > 16) {
            return fail(v, "'%s': sixteen d registers at most", rq->mnemonic);
        }
        count *= 2;
        w |= 0x100u;
    }
    if (strcmp(b, "vpush") == 0 || strcmp(b, "vpop") == 0) {
        wb = strcmp(b, "vpop") == 0;
    }
    return word(v, (v->cond << 28) | w | (wb ? 0x00200000u : 0u) | ((uint32_t)rn << 16) | VD(&first) | count);
}

/* The vector instructions of three registers. */
static int neon_three(simd_t *v) {
    static const struct {
        const char *name;
        char kind;      /* the letter the size must have: i, s (s or u), f, or 0 for no size */
        uint32_t w;     /* with `u` the bit 24 is set, for those of kind s */
    } ops[] = {
        {"vadd", 'i', 0xf2000800u}, {"vsub", 'i', 0xf3000800u}, {"vmul", 'i', 0xf2000910u}, {"vceq", 'i', 0xf3000810u},
        {"vtst", 'n', 0xf2000810u}, {"vand", 0, 0xf2000110u},   {"vbic", 0, 0xf2100110u},   {"vorr", 0, 0xf2200110u},
        {"vorn", 0, 0xf2300110u},   {"veor", 0, 0xf3000110u},   {"vcgt", 's', 0xf2000300u}, {"vcge", 's', 0xf2000310u},
        {"vmax", 's', 0xf2000600u}, {"vmin", 's', 0xf2000610u}, {"vqadd", 's', 0xf2000010u}, {"vqsub", 's', 0xf2000210u},
        {"vshl", 's', 0xf2000400u},
    };
    static const struct {
        const char *name;
        uint32_t w;
    } fl[] = {{"vadd", 0xf2000d00u}, {"vsub", 0xf2200d00u}, {"vmul", 0xf3000d10u}, {"vmax", 0xf2000f00u},
              {"vmin", 0xf2200f00u}, {"vceq", 0xf2000e00u}, {"vcge", 0xf3000e00u}, {"vcgt", 0xf3200e00u}};
    const as_arm_req_t *rq = v->rq;
    char kind;
    int size = vec_size(v, &kind);
    vreg_t r[3];
    uint32_t q;
    size_t i;

    if (three(v, r) != 0 || r[0].kind == 's') {
        return 1;
    }
    if (v->cond != COND_AL) {
        return fail(v, "'%s': a vector instruction has no condition", rq->mnemonic);
    }
    q = r[0].kind == 'q' ? 0x40u : 0u;
    if (kind == 'f') {
        for (i = 0; i < sizeof(fl) / sizeof(fl[0]); ++i) {
            if (strcmp(v->base, fl[i].name) == 0) {
                if (size != 2) {
                    return fail(v, "'%s': vectors of floats are of .f32", rq->mnemonic);
                }
                return word(v, fl[i].w | q | VD(&r[0]) | VN(&r[1]) | VM(&r[2]));
            }
        }
        return 1;
    }
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        uint32_t w;

        if (strcmp(v->base, ops[i].name) != 0) {
            continue;
        }
        w = ops[i].w | q;
        if (ops[i].kind == 0) {
            /* (Bit by bit, whatever size is written.) */
            return word(v, w | VD(&r[0]) | VN(&r[1]) | VM(&r[2]));
        }
        if (size < 0) {
            return fail(v, "'%s': the size of the elements is not said", rq->mnemonic);
        }
        if (ops[i].kind == 's') {
            if (kind != 's' && kind != 'u') {
                return fail(v, "'%s': signed or unsigned, .s8 or .u8 and so on", rq->mnemonic);
            }
            if (kind == 'u') {
                w |= 0x01000000u;
            }
        } else if (ops[i].kind == 'i' && kind != 'i' && !(strcmp(v->base, "vmul") == 0 && kind == 'p')) {
            return fail(v, "'%s': .i8, .i16, .i32 or .i64", rq->mnemonic);
        }
        if (size == 3 && (strcmp(v->base, "vmul") == 0 || strcmp(v->base, "vceq") == 0 ||
                          strcmp(v->base, "vcgt") == 0 || strcmp(v->base, "vcge") == 0 ||
                          strcmp(v->base, "vmax") == 0 || strcmp(v->base, "vmin") == 0 ||
                          strcmp(v->base, "vtst") == 0)) {
            return fail(v, "'%s': it has no elements of 64 bits", rq->mnemonic);
        }
        /* A shift by a register has the shifted before the count. */
        if (strcmp(v->base, "vshl") == 0) {
            return word(v, w | ((uint32_t)size << 20) | VD(&r[0]) | VM(&r[1]) | VN(&r[2]));
        }
        return word(v, w | ((uint32_t)size << 20) | VD(&r[0]) | VN(&r[1]) | VM(&r[2]));
    }
    return 1;
}

/* The vector instructions of two registers, and the shifts by a
 * number. */
static int neon_two(simd_t *v) {
    static const struct {
        const char *name;
        char kind; /* s: .s8 and so on; i; f handled apart; 0: none */
        uint32_t w;
    } ops[] = {{"vmvn", 0, 0xf3b00580u},   {"vswp", 0, 0xf3b20000u},  {"vcnt", 'n', 0xf3b00500u},
               {"vclz", 'i', 0xf3b00480u}, {"vneg", 's', 0xf3b10380u}, {"vabs", 's', 0xf3b10300u},
               {"vrev64", 'n', 0xf3b00000u}};
    const as_arm_req_t *rq = v->rq;
    char kind;
    int size = vec_size(v, &kind);
    vreg_t d;
    vreg_t m;
    uint32_t q;
    size_t i;

    if (rq->op_count < 2 || vreg(rq->ops[0], &d) != 0 || d.kind == 's') {
        return 1;
    }
    q = d.kind == 'q' ? 0x40u : 0u;
    if (strcmp(v->base, "vshl") == 0 || strcmp(v->base, "vshr") == 0) {
        long long n;
        unsigned bits;

        if (rq->op_count == 2) {
            m = d;
        } else if (rq->op_count != 3 || vreg(rq->ops[1], &m) != 0 || m.kind != d.kind) {
            return 1;
        }
        if (number(rq->ops[rq->op_count - 1], &n) != 0) {
            return 1;
        }
        if (size < 0 || v->cond != COND_AL) {
            return fail(v, "'%s': the size of the elements, and no condition", rq->mnemonic);
        }
        bits = 8u << size;
        if (v->base[3] == 'l') {
            if (n < 0 || n >= (long long)bits) {
                return fail(v, "'%s': a shift of %lld in elements of %u bits", rq->mnemonic, n, bits);
            }
            return word(v, 0xf2800510u | q | (size == 3 ? 0x80u : 0u) | (((bits & 63u) + (uint32_t)n) << 16) |
                               VD(&d) | VM(&m));
        }
        if (n < 1 || n > (long long)bits || (kind != 's' && kind != 'u')) {
            return fail(v, "'%s': .s or .u, and a shift of 1 to %u", rq->mnemonic, bits);
        }
        return word(v, (kind == 'u' ? 0xf3800010u : 0xf2800010u) | q | (size == 3 ? 0x80u : 0u) |
                           ((((size == 3 ? 64u : 2 * bits) - (uint32_t)n) & 63u) << 16) | VD(&d) | VM(&m));
    }
    if (rq->op_count != 2 || vreg(rq->ops[1], &m) != 0 || m.kind != d.kind) {
        return 1;
    }
    if (v->cond != COND_AL) {
        return fail(v, "'%s': a vector instruction has no condition", rq->mnemonic);
    }
    if (kind == 'f' && (strcmp(v->base, "vneg") == 0 || strcmp(v->base, "vabs") == 0)) {
        if (size != 2) {
            return fail(v, "'%s': vectors of floats are of .f32", rq->mnemonic);
        }
        return word(v, (v->base[1] == 'n' ? 0xf3b90780u : 0xf3b90700u) | q | VD(&d) | VM(&m));
    }
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (strcmp(v->base, ops[i].name) != 0) {
            continue;
        }
        if (ops[i].kind == 0) {
            return word(v, ops[i].w | q | VD(&d) | VM(&m));
        }
        if (size < 0 || size == 3 || (ops[i].kind == 's' && kind != 's') || (ops[i].kind == 'i' && kind != 'i')) {
            return fail(v, "'%s': the size of the elements is not one it has", rq->mnemonic);
        }
        return word(v, ops[i].w | q | ((uint32_t)size << 18) | VD(&d) | VM(&m));
    }
    return 1;
}

/* vdup: every element the value of a core register. */
static int neon_dup(simd_t *v) {
    const as_arm_req_t *rq = v->rq;
    char kind;
    int size = vec_size(v, &kind);
    vreg_t d;
    int rt;

    if (rq->op_count != 2 || vreg(rq->ops[0], &d) != 0 || d.kind == 's' || (rt = core_reg(rq->ops[1])) < 0 ||
        size < 0 || size > 2) {
        return fail(v, "'%s': a vector, the size of its elements (.8, .16 or .32) and a core register", rq->mnemonic);
    }
    return word(v, (v->cond << 28) | 0x0e800b10u | (size == 0 ? 0x00400000u : 0u) | (size == 1 ? 0x20u : 0u) |
                       (d.kind == 'q' ? 0x00200000u : 0u) | field(&d, 16, 7) | ((uint32_t)rt << 12));
}

/* vld1 and vst1 of whole registers. */
static int neon_load_store(simd_t *v) {
    static const unsigned types[] = {0, 7, 10, 6, 2};
    const as_arm_req_t *rq = v->rq;
    char kind;
    int size = vec_size(v, &kind);
    vreg_t first;
    unsigned count;
    char buf[32];
    char *close;
    unsigned rm = 15;
    int rn;

    if (rq->op_count < 2 || rq->op_count > 3 || size < 0 || vreg_list(rq->ops[0], &first, &count) != 0 ||
        first.kind != 'd' || count > 4 || v->cond != COND_AL) {
        return fail(v, "'%s': the size of the elements, a list of one to four d registers, and an address",
                    rq->mnemonic);
    }
    text(rq->ops[1], buf, sizeof(buf));
    close = strchr(buf, ']');
    if (buf[0] != '[' || close == NULL) {
        return fail(v, "'%s': '%s' is not a base register in brackets", rq->mnemonic, rq->ops[1]);
    }
    if (close[1] == '!') {
        rm = 13;
    } else if (close[1] != '\0') {
        return fail(v, "'%s': what follows the ] of '%s'", rq->mnemonic, rq->ops[1]);
    }
    *close = '\0';
    rn = core_reg(buf + 1);
    if (rn < 0) {
        return fail(v, "'%s': the base of an address is a register", rq->mnemonic);
    }
    if (rq->op_count == 3) {
        int r = core_reg(rq->ops[2]);

        if (r < 0 || r == 13 || r == 15 || rm != 15) {
            return fail(v, "'%s': what is added to the base afterwards is a register", rq->mnemonic);
        }
        rm = (unsigned)r;
    }
    return word(v, (v->base[1] == 'l' ? 0xf4200000u : 0xf4000000u) | ((uint32_t)rn << 16) | VD(&first) |
                       (types[count] << 8) | ((uint32_t)size << 6) | rm);
}

int as_arm_vfp_neon(const as_arm_req_t *rq, const char *base, unsigned cond, as_arm_out_t *out, char *errbuf,
                    size_t errbuf_sz) {
    simd_t v;
    const char *dot = strchr(rq->mnemonic, '.');
    int rc;

    memset(&v, 0, sizeof(v));
    v.rq = rq;
    v.out = out;
    v.errbuf = errbuf;
    v.errbuf_sz = errbuf_sz;
    v.base = base;
    v.cond = cond;
    if (dot != NULL) {
        text(dot + 1, v.suffix, sizeof(v.suffix));
    }
    if (strcmp(base, "vmov") == 0) {
        return vmov(&v);
    }
    if (strcmp(base, "vcvt") == 0) {
        return vfp_convert(&v);
    }
    if (strcmp(base, "vldr") == 0 || strcmp(base, "vstr") == 0) {
        return vfp_load_store(&v);
    }
    if (strcmp(base, "vpush") == 0 || strcmp(base, "vpop") == 0 || strncmp(base, "vldm", 4) == 0 ||
        strncmp(base, "vstm", 4) == 0) {
        return vfp_multiple(&v);
    }
    if (strcmp(base, "vmrs") == 0 || strcmp(base, "vmsr") == 0) {
        char a[16];
        char b[16];
        int rt;

        if (rq->op_count != 2) {
            return fail(&v, "'%s': a register and fpscr", rq->mnemonic);
        }
        text(rq->ops[base[2] == 'r' ? 0 : 1], a, sizeof(a));
        text(rq->ops[base[2] == 'r' ? 1 : 0], b, sizeof(b));
        rt = strcmp(a, "apsr_nzcv") == 0 && base[2] == 'r' ? 15 : core_reg(a);
        if (rt < 0 || strcmp(b, "fpscr") != 0) {
            return fail(&v, "'%s': a core register and fpscr", rq->mnemonic);
        }
        return word(&v, (cond << 28) | (base[2] == 'r' ? 0x0ef10a10u : 0x0ee10a10u) | ((uint32_t)rt << 12));
    }
    if (strcmp(base, "vdup") == 0) {
        return neon_dup(&v);
    }
    if (strcmp(base, "vld1") == 0 || strcmp(base, "vst1") == 0) {
        return neon_load_store(&v);
    }
    if (is_scalar(&v)) {
        rc = vfp_arith(&v);
        if (rc <= 0) {
            return rc;
        }
    }
    rc = neon_two(&v);
    if (rc <= 0) {
        return rc;
    }
    rc = neon_three(&v);
    if (rc <= 0) {
        return rc;
    }
    return fail(&v, "'%s': not an instruction of VFP or NEON that this assembler has, or not with these operands",
                rq->mnemonic);
}
