#include "as_arm_int.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The floating-point and vector instructions of AArch64.  See as_arm.h.
 */

#define text as_arm_text
#define number as_arm_number

typedef as_a64_reg_t reg_t;

typedef struct {
    const as_arm_req_t *rq;
    as_arm_out_t *out;
    char *errbuf;
    size_t errbuf_sz;
    const char *m;
} simd_t;

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

/* Every operand a register: 0 with them. */
static int regs(simd_t *v, reg_t *r, size_t count) {
    size_t i;

    if (v->rq->op_count != count) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (as_a64_parse_reg(v->rq->ops[i], &r[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int is_scalar_fp(const reg_t *r) {
    return r->fp && (r->bits == 16 || r->bits == 32 || r->bits == 64);
}

static int is_gpr(const reg_t *r) {
    return !r->fp && !r->vec && !r->is_sp;
}

/* The two bits that say which float a scalar is. */
static uint32_t fp_type(const reg_t *r) {
    return r->bits == 64 ? 1u : r->bits == 16 ? 3u : 0u;
}

/* An eight-bit float: the sign, three bits of exponent and four of
 * fraction. */
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

static int same_vectors(const reg_t *r, size_t count) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (!r[i].vec || r[i].index >= 0 || r[i].q != r[0].q || r[i].esize != r[0].esize) {
            return 0;
        }
    }
    return 1;
}

/* The arithmetic of floats that are not vectors. */
static int scalar_fp(simd_t *v) {
    static const struct {
        const char *name;
        uint32_t w;
    } two[] = {{"fmul", 0x1e200800u},   {"fdiv", 0x1e201800u},   {"fadd", 0x1e202800u},
               {"fsub", 0x1e203800u},   {"fmax", 0x1e204800u},   {"fmin", 0x1e205800u},
               {"fmaxnm", 0x1e206800u}, {"fminnm", 0x1e207800u}, {"fnmul", 0x1e208800u}};
    static const struct {
        const char *name;
        uint32_t w;
    } one[] = {{"fmov", 0x1e204000u},   {"fabs", 0x1e20c000u},   {"fneg", 0x1e214000u},   {"fsqrt", 0x1e21c000u},
               {"frintn", 0x1e244000u}, {"frintp", 0x1e24c000u}, {"frintm", 0x1e254000u}, {"frintz", 0x1e25c000u},
               {"frinta", 0x1e264000u}, {"frintx", 0x1e274000u}, {"frinti", 0x1e27c000u}};
    static const struct {
        const char *name;
        uint32_t w;
    } four[] = {{"fmadd", 0x1f000000u}, {"fmsub", 0x1f008000u}, {"fnmadd", 0x1f200000u}, {"fnmsub", 0x1f208000u}};
    static const struct {
        const char *name;
        uint32_t w;
    } to_int[] = {{"fcvtns", 0x1e200000u}, {"fcvtnu", 0x1e210000u}, {"fcvtas", 0x1e240000u}, {"fcvtau", 0x1e250000u},
                  {"fcvtps", 0x1e280000u}, {"fcvtpu", 0x1e290000u}, {"fcvtms", 0x1e300000u}, {"fcvtmu", 0x1e310000u},
                  {"fcvtzs", 0x1e380000u}, {"fcvtzu", 0x1e390000u}};
    const as_arm_req_t *rq = v->rq;
    const char *m = v->m;
    reg_t r[4];
    size_t i;

    for (i = 0; i < sizeof(two) / sizeof(two[0]); ++i) {
        if (strcmp(m, two[i].name) == 0) {
            if (regs(v, r, 3) != 0 || !is_scalar_fp(&r[0]) || r[1].bits != r[0].bits || r[2].bits != r[0].bits ||
                !r[1].fp || !r[2].fp) {
                return 1;
            }
            return word(v, two[i].w | (fp_type(&r[0]) << 22) | (r[2].n << 16) | (r[1].n << 5) | r[0].n);
        }
    }
    for (i = 0; i < sizeof(four) / sizeof(four[0]); ++i) {
        if (strcmp(m, four[i].name) == 0) {
            if (regs(v, r, 4) != 0 || !is_scalar_fp(&r[0]) || !r[1].fp || !r[2].fp || !r[3].fp ||
                r[1].bits != r[0].bits || r[2].bits != r[0].bits || r[3].bits != r[0].bits) {
                return fail(v, "'%s': four registers of floats of one size", rq->mnemonic);
            }
            return word(v, four[i].w | (fp_type(&r[0]) << 22) | (r[2].n << 16) | (r[3].n << 10) | (r[1].n << 5) |
                               r[0].n);
        }
    }
    if (strcmp(m, "fmov") == 0 && rq->op_count == 2 && as_a64_parse_reg(rq->ops[0], &r[0]) == 0) {
        if (as_a64_parse_reg(rq->ops[1], &r[1]) != 0) {
            const char *p = rq->ops[1];
            unsigned imm8;
            char *end;
            double d;

            while (*p == '#' || isspace((unsigned char)*p)) {
                p++;
            }
            d = strtod(p, &end);
            if (!is_scalar_fp(&r[0]) || end == p || *end != '\0' || float_imm8(d, &imm8) != 0) {
                return fail(v, "'%s': '%s' is not a float of eight bits", rq->mnemonic, rq->ops[1]);
            }
            return word(v, 0x1e201000u | (fp_type(&r[0]) << 22) | (imm8 << 13) | r[0].n);
        }
        /* Between a general register and a float's, bit for bit. */
        if (is_gpr(&r[0]) && is_scalar_fp(&r[1]) && r[0].bits == r[1].bits) {
            return word(v, (r[0].bits == 64 ? 0x9e660000u : 0x1e260000u) | (r[1].n << 5) | r[0].n);
        }
        if (is_scalar_fp(&r[0]) && is_gpr(&r[1]) && r[0].bits == r[1].bits) {
            return word(v, (r[0].bits == 64 ? 0x9e670000u : 0x1e270000u) | (r[1].n << 5) | r[0].n);
        }
    }
    for (i = 0; i < sizeof(one) / sizeof(one[0]); ++i) {
        if (strcmp(m, one[i].name) == 0) {
            if (regs(v, r, 2) != 0 || !is_scalar_fp(&r[0]) || !r[1].fp || r[1].bits != r[0].bits) {
                return 1;
            }
            return word(v, one[i].w | (fp_type(&r[0]) << 22) | (r[1].n << 5) | r[0].n);
        }
    }
    if (strcmp(m, "fcvt") == 0) {
        if (regs(v, r, 2) != 0 || !is_scalar_fp(&r[0]) || !is_scalar_fp(&r[1]) || r[0].bits == r[1].bits) {
            return fail(v, "'%s': two registers of floats of two sizes", rq->mnemonic);
        }
        return word(v, 0x1e224000u | (fp_type(&r[1]) << 22) | (fp_type(&r[0]) << 15) | (r[1].n << 5) | r[0].n);
    }
    if (strcmp(m, "fcmp") == 0 || strcmp(m, "fcmpe") == 0) {
        uint32_t w = m[4] == 'e' ? 0x1e202010u : 0x1e202000u;

        if (rq->op_count != 2 || as_a64_parse_reg(rq->ops[0], &r[0]) != 0 || !is_scalar_fp(&r[0])) {
            return fail(v, "'%s': a float's register, and another or #0.0", rq->mnemonic);
        }
        if (as_a64_parse_reg(rq->ops[1], &r[1]) != 0) {
            const char *p = rq->ops[1];
            char *end;

            while (*p == '#' || isspace((unsigned char)*p)) {
                p++;
            }
            if (strtod(p, &end) != 0.0 || end == p || *end != '\0') {
                return fail(v, "'%s': the only number compared with is #0.0", rq->mnemonic);
            }
            return word(v, w | 8u | (fp_type(&r[0]) << 22) | (r[0].n << 5));
        }
        if (!r[1].fp || r[1].bits != r[0].bits) {
            return fail(v, "'%s': two registers of floats of one size", rq->mnemonic);
        }
        return word(v, w | (fp_type(&r[0]) << 22) | (r[1].n << 16) | (r[0].n << 5));
    }
    if (strcmp(m, "fcsel") == 0) {
        unsigned c;

        if (rq->op_count != 4 || as_a64_parse_reg(rq->ops[0], &r[0]) != 0 ||
            as_a64_parse_reg(rq->ops[1], &r[1]) != 0 || as_a64_parse_reg(rq->ops[2], &r[2]) != 0 ||
            as_a64_cond(rq->ops[3], &c) != 0 || !is_scalar_fp(&r[0]) || r[1].bits != r[0].bits ||
            r[2].bits != r[0].bits || !r[1].fp || !r[2].fp) {
            return fail(v, "'%s': three registers of floats of one size and a condition", rq->mnemonic);
        }
        return word(v, 0x1e200c00u | (fp_type(&r[0]) << 22) | (r[2].n << 16) | (c << 12) | (r[1].n << 5) | r[0].n);
    }
    if (strcmp(m, "scvtf") == 0 || strcmp(m, "ucvtf") == 0) {
        if (regs(v, r, 2) != 0 || !is_scalar_fp(&r[0]) || !is_gpr(&r[1])) {
            return 1;
        }
        return word(v, (m[0] == 's' ? 0x1e220000u : 0x1e230000u) | ((r[1].bits == 64 ? 1u : 0u) << 31) |
                           (fp_type(&r[0]) << 22) | (r[1].n << 5) | r[0].n);
    }
    for (i = 0; i < sizeof(to_int) / sizeof(to_int[0]); ++i) {
        if (strcmp(m, to_int[i].name) == 0) {
            if (regs(v, r, 2) != 0 || !is_gpr(&r[0]) || !is_scalar_fp(&r[1])) {
                return 1;
            }
            return word(v, to_int[i].w | ((r[0].bits == 64 ? 1u : 0u) << 31) | (fp_type(&r[1]) << 22) |
                               (r[1].n << 5) | r[0].n);
        }
    }
    return 1;
}

/* The vector instructions of three registers. */
static int vector_three(simd_t *v) {
    static const struct {
        const char *name;
        uint32_t w;
        char kind; /* i: elements of 8 to 64 bits; n: not of 64; l: bit by bit; f: floats */
    } ops[] = {
        {"add", 0x0e208400u, 'i'},   {"sub", 0x2e208400u, 'i'},   {"mul", 0x0e209c00u, 'n'},
        {"cmeq", 0x2e208c00u, 'i'},  {"cmtst", 0x0e208c00u, 'i'}, {"cmgt", 0x0e203400u, 'i'},
        {"cmhi", 0x2e203400u, 'i'},  {"cmge", 0x0e203c00u, 'i'},  {"cmhs", 0x2e203c00u, 'i'},
        {"smax", 0x0e206400u, 'n'},  {"umax", 0x2e206400u, 'n'},  {"smin", 0x0e206c00u, 'n'},
        {"umin", 0x2e206c00u, 'n'},  {"addp", 0x0e20bc00u, 'i'},  {"sqadd", 0x0e200c00u, 'i'},
        {"uqadd", 0x2e200c00u, 'i'}, {"sqsub", 0x0e202c00u, 'i'}, {"uqsub", 0x2e202c00u, 'i'},
        {"sshl", 0x0e204400u, 'i'},  {"ushl", 0x2e204400u, 'i'},  {"and", 0x0e201c00u, 'l'},
        {"bic", 0x0e601c00u, 'l'},   {"orr", 0x0ea01c00u, 'l'},   {"orn", 0x0ee01c00u, 'l'},
        {"eor", 0x2e201c00u, 'l'},   {"bsl", 0x2e601c00u, 'l'},   {"bit", 0x2ea01c00u, 'l'},
        {"bif", 0x2ee01c00u, 'l'},   {"fadd", 0x0e20d400u, 'f'},  {"fsub", 0x0ea0d400u, 'f'},
        {"fmul", 0x2e20dc00u, 'f'},  {"fdiv", 0x2e20fc00u, 'f'},  {"fmax", 0x0e20f400u, 'f'},
        {"fmin", 0x0ea0f400u, 'f'},  {"fcmeq", 0x0e20e400u, 'f'}, {"fcmge", 0x2e20e400u, 'f'},
        {"fcmgt", 0x2ea0e400u, 'f'}, {"fmla", 0x0e20cc00u, 'f'},  {"fmls", 0x0ea0cc00u, 'f'},
    };
    const as_arm_req_t *rq = v->rq;
    reg_t r[3];
    size_t i;

    if (regs(v, r, 3) != 0 || !same_vectors(r, 3)) {
        return 1;
    }
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        uint32_t w;

        if (strcmp(v->m, ops[i].name) != 0) {
            continue;
        }
        w = ops[i].w | (r[0].q << 30) | (r[2].n << 16) | (r[1].n << 5) | r[0].n;
        switch (ops[i].kind) {
        case 'l':
            if (r[0].esize != 0) {
                return fail(v, "'%s': bit by bit, the vectors are of bytes", rq->mnemonic);
            }
            return word(v, w);
        case 'f':
            if (r[0].esize < 2 || (r[0].esize == 3 && !r[0].q)) {
                return fail(v, "'%s': vectors of floats are .2s, .4s or .2d", rq->mnemonic);
            }
            return word(v, w | ((r[0].esize == 3 ? 1u : 0u) << 22));
        default:
            if (r[0].esize == 3 && (ops[i].kind == 'n' || !r[0].q)) {
                return fail(v, "'%s': it has no such vector of 64-bit elements", rq->mnemonic);
            }
            return word(v, w | (r[0].esize << 22));
        }
    }
    return 1;
}

/* The vector instructions of two registers, the comparisons with
 * nothing, and the shifts by a number. */
static int vector_two(simd_t *v) {
    static const struct {
        const char *name;
        uint32_t w;
        char kind; /* i, f, b (bytes only: no size) */
    } ops[] = {
        {"abs", 0x0e20b800u, 'i'},   {"neg", 0x2e20b800u, 'i'},    {"not", 0x2e205800u, 'b'},
        {"mvn", 0x2e205800u, 'b'},   {"cnt", 0x0e205800u, 'b'},    {"rev64", 0x0e200800u, 'i'},
        {"rev32", 0x2e200800u, 'i'}, {"rev16", 0x0e201800u, 'i'},  {"cls", 0x0e204800u, 'i'},
        {"clz", 0x2e204800u, 'i'},   {"fabs", 0x0ea0f800u, 'f'},   {"fneg", 0x2ea0f800u, 'f'},
        {"fsqrt", 0x2ea1f800u, 'f'}, {"scvtf", 0x0e21d800u, 'f'},  {"ucvtf", 0x2e21d800u, 'f'},
        {"fcvtzs", 0x0ea1b800u, 'f'}, {"fcvtzu", 0x2ea1b800u, 'f'},
    };
    static const struct {
        const char *name;
        uint32_t w;
    } zero[] = {{"cmeq", 0x0e209800u}, {"cmgt", 0x0e208800u}, {"cmge", 0x2e208800u},
                {"cmlt", 0x0e20a800u}, {"cmle", 0x2e209800u}};
    const as_arm_req_t *rq = v->rq;
    reg_t r[2];
    size_t i;

    if (rq->op_count < 2 || as_a64_parse_reg(rq->ops[0], &r[0]) != 0 || as_a64_parse_reg(rq->ops[1], &r[1]) != 0 ||
        !same_vectors(r, 2)) {
        return 1;
    }
    if (rq->op_count == 3) {
        long long n;
        unsigned bits = 8u << r[0].esize;

        if (number(rq->ops[2], &n) != 0) {
            return 1;
        }
        if (r[0].esize == 3 && !r[0].q) {
            return fail(v, "'%s': it has no such vector of 64-bit elements", rq->mnemonic);
        }
        for (i = 0; i < sizeof(zero) / sizeof(zero[0]); ++i) {
            if (strcmp(v->m, zero[i].name) == 0) {
                if (n != 0) {
                    return fail(v, "'%s': the only number compared with is #0", rq->mnemonic);
                }
                return word(v, zero[i].w | (r[0].q << 30) | (r[0].esize << 22) | (r[1].n << 5) | r[0].n);
            }
        }
        if (strcmp(v->m, "shl") == 0) {
            if (n < 0 || n >= (long long)bits) {
                return fail(v, "'%s': a shift of %lld in elements of %u bits", rq->mnemonic, n, bits);
            }
            return word(v, 0x0f005400u | (r[0].q << 30) | ((bits + (uint32_t)n) << 16) | (r[1].n << 5) | r[0].n);
        }
        if (strcmp(v->m, "sshr") == 0 || strcmp(v->m, "ushr") == 0) {
            if (n < 1 || n > (long long)bits) {
                return fail(v, "'%s': a shift of 1 to %u", rq->mnemonic, bits);
            }
            return word(v, (v->m[0] == 'u' ? 0x2f000400u : 0x0f000400u) | (r[0].q << 30) |
                               ((2 * bits - (uint32_t)n) << 16) | (r[1].n << 5) | r[0].n);
        }
        return 1;
    }
    if (strcmp(v->m, "mov") == 0) {
        /* A vector moved is ORed with itself. */
        if (r[0].esize != 0) {
            return fail(v, "'%s': a whole vector is moved as bytes, .8b or .16b", rq->mnemonic);
        }
        return word(v, 0x0ea01c00u | (r[0].q << 30) | (r[1].n << 16) | (r[1].n << 5) | r[0].n);
    }
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        uint32_t w;

        if (strcmp(v->m, ops[i].name) != 0) {
            continue;
        }
        w = ops[i].w | (r[0].q << 30) | (r[1].n << 5) | r[0].n;
        if (ops[i].kind == 'b') {
            if (r[0].esize != 0) {
                return fail(v, "'%s': its vectors are of bytes", rq->mnemonic);
            }
            return word(v, w);
        }
        if (ops[i].kind == 'f') {
            if (r[0].esize < 2 || (r[0].esize == 3 && !r[0].q)) {
                return fail(v, "'%s': vectors of floats are .2s, .4s or .2d", rq->mnemonic);
            }
            return word(v, w | ((r[0].esize == 3 ? 1u : 0u) << 22));
        }
        if (r[0].esize == 3 && (!r[0].q || strncmp(v->m, "rev", 3) == 0 || strncmp(v->m, "cl", 2) == 0)) {
            return fail(v, "'%s': it has no such vector of 64-bit elements", rq->mnemonic);
        }
        return word(v, w | (r[0].esize << 22));
    }
    return 1;
}

/* The five bits that say an element: its size and its number. */
static uint32_t element(const reg_t *r) {
    return ((uint32_t)r->index << (r->esize + 1)) | (1u << r->esize);
}

static int element_ok(const reg_t *r) {
    return r->vec && r->index >= 0 && (unsigned)r->index < (16u >> r->esize);
}

/* dup, ins, umov, smov and the moves that are those. */
static int elements(simd_t *v) {
    const as_arm_req_t *rq = v->rq;
    const char *m = v->m;
    reg_t r[2];

    if (regs(v, r, 2) != 0) {
        return 1;
    }
    if (strcmp(m, "dup") == 0 && r[0].vec && r[0].index < 0) {
        if (is_gpr(&r[1])) {
            if ((r[0].esize == 3) != (r[1].bits == 64) || (r[0].esize == 3 && !r[0].q)) {
                return fail(v, "'%s': elements of 64 bits from an x register, and others from a w", rq->mnemonic);
            }
            return word(v, 0x0e000c00u | (r[0].q << 30) | ((1u << r[0].esize) << 16) | (r[1].n << 5) | r[0].n);
        }
        if (element_ok(&r[1]) && r[1].esize == r[0].esize) {
            return word(v, 0x0e000400u | (r[0].q << 30) | (element(&r[1]) << 16) | (r[1].n << 5) | r[0].n);
        }
        return fail(v, "'%s': a vector, and a general register or an element of its size", rq->mnemonic);
    }
    if ((strcmp(m, "ins") == 0 || strcmp(m, "mov") == 0) && element_ok(&r[0])) {
        if (is_gpr(&r[1])) {
            if ((r[0].esize == 3) != (r[1].bits == 64)) {
                return fail(v, "'%s': an element of 64 bits from an x register, and others from a w", rq->mnemonic);
            }
            return word(v, 0x4e001c00u | (element(&r[0]) << 16) | (r[1].n << 5) | r[0].n);
        }
        if (element_ok(&r[1]) && r[1].esize == r[0].esize) {
            return word(v, 0x6e000400u | (element(&r[0]) << 16) | (((uint32_t)r[1].index << r[1].esize) << 11) |
                               (r[1].n << 5) | r[0].n);
        }
        return fail(v, "'%s': an element, and a general register or an element of its size", rq->mnemonic);
    }
    if ((strcmp(m, "umov") == 0 || strcmp(m, "mov") == 0 || strcmp(m, "smov") == 0) && is_gpr(&r[0]) &&
        element_ok(&r[1])) {
        if (m[0] == 's') {
            if (r[1].esize > (r[0].bits == 64 ? 2u : 1u)) {
                return fail(v, "'%s': an element smaller than the register", rq->mnemonic);
            }
            return word(v, 0x0e002c00u | ((r[0].bits == 64 ? 1u : 0u) << 30) | (element(&r[1]) << 16) |
                               (r[1].n << 5) | r[0].n);
        }
        /* (A move by that name is of a whole word or of two.) */
        if ((r[1].esize == 3) != (r[0].bits == 64) || (m[0] == 'm' && r[1].esize < 2)) {
            return fail(v, "'%s': an element of 64 bits to an x register, and others to a w", rq->mnemonic);
        }
        return word(v, 0x0e003c00u | ((r[0].bits == 64 ? 1u : 0u) << 30) | (element(&r[1]) << 16) | (r[1].n << 5) |
                           r[0].n);
    }
    if (strcmp(m, "mov") == 0 && r[0].fp && element_ok(&r[1]) && r[0].bits == (8u << r[1].esize)) {
        return word(v, 0x5e000400u | (element(&r[1]) << 16) | (r[1].n << 5) | r[0].n);
    }
    if (strcmp(m, "addv") == 0 && r[0].fp && r[1].vec && r[1].index < 0 && r[0].bits == (8u << r[1].esize) &&
        r[1].esize < 3 && !(r[1].esize == 2 && !r[1].q)) {
        return word(v, 0x0e31b800u | (r[1].q << 30) | (r[1].esize << 22) | (r[1].n << 5) | r[0].n);
    }
    return 1;
}

/* movi: every element one number. */
static int move_immediate(simd_t *v) {
    const as_arm_req_t *rq = v->rq;
    long long n;
    long long shift = 0;
    uint64_t u;
    reg_t r;

    if (rq->op_count < 2 || rq->op_count > 3 || as_a64_parse_reg(rq->ops[0], &r) != 0 ||
        number(rq->ops[1], &n) != 0) {
        return fail(v, "'%s': a vector and a number", rq->mnemonic);
    }
    if (rq->op_count == 3) {
        char buf[16];

        text(rq->ops[2], buf, sizeof(buf));
        if (strncmp(buf, "lsl", 3) != 0 || number(buf + 3, &shift) != 0 || shift < 0 || (shift & 7) != 0) {
            return fail(v, "'%s': shifted left by whole bytes", rq->mnemonic);
        }
    }
    u = (uint64_t)n;
#define IMM8(b) ((((uint32_t)(b) >> 5) << 16) | (((uint32_t)(b)&31u) << 5))
    if (r.fp && r.bits == 64 && rq->op_count == 2) {
        unsigned imm8 = 0;
        unsigned k;

        for (k = 0; k < 8; ++k) {
            unsigned byte = (unsigned)(u >> (8 * k)) & 0xffu;

            if (byte != 0 && byte != 0xff) {
                return fail(v, "'%s': each byte of the number is 0 or 0xff", rq->mnemonic);
            }
            imm8 |= (byte & 1u) << k;
        }
        return word(v, 0x2f00e400u | IMM8(imm8) | r.n);
    }
    if (!r.vec || r.index >= 0) {
        return fail(v, "'%s': a vector and a number", rq->mnemonic);
    }
    if (r.esize == 3) {
        unsigned imm8 = 0;
        unsigned k;

        if (!r.q || rq->op_count != 2) {
            return fail(v, "'%s': of 64-bit elements, the vector is .2d and nothing is shifted", rq->mnemonic);
        }
        for (k = 0; k < 8; ++k) {
            unsigned byte = (unsigned)(u >> (8 * k)) & 0xffu;

            if (byte != 0 && byte != 0xff) {
                return fail(v, "'%s': each byte of the number is 0 or 0xff", rq->mnemonic);
            }
            imm8 |= (byte & 1u) << k;
        }
        return word(v, 0x6f00e400u | IMM8(imm8) | r.n);
    }
    if (n < -128 || n > 255) {
        return fail(v, "'%s': %lld is not a byte", rq->mnemonic, n);
    }
    u &= 0xffu;
    if (r.esize == 0) {
        if (shift != 0) {
            return fail(v, "'%s': a byte is not shifted", rq->mnemonic);
        }
        return word(v, 0x0f00e400u | (r.q << 30) | IMM8(u) | r.n);
    }
    if (shift >= (r.esize == 1 ? 16 : 32)) {
        return fail(v, "'%s': shifted past the element", rq->mnemonic);
    }
    return word(v, (r.esize == 1 ? 0x0f008400u : 0x0f000400u) | (r.q << 30) | ((uint32_t)(shift / 8) << 13) | IMM8(u) |
                       r.n);
#undef IMM8
}

/* ld1 and st1 of whole registers. */
static int load_store(simd_t *v) {
    static const uint32_t opcode[] = {0, 0x7000u, 0xa000u, 0x6000u, 0x2000u};
    const as_arm_req_t *rq = v->rq;
    char buf[96];
    char *close;
    char *p;
    reg_t first;
    reg_t base;
    unsigned count = 0;
    uint32_t w = v->m[0] == 'l' ? 0x0c400000u : 0x0c000000u;

    if (rq->op_count < 2 || rq->op_count > 3) {
        return fail(v, "'%s': a list of vectors and an address", rq->mnemonic);
    }
    text(rq->ops[0], buf, sizeof(buf));
    close = strchr(buf, '}');
    if (buf[0] != '{' || close == NULL) {
        return fail(v, "'%s': '%s' is not a list of vectors", rq->mnemonic, rq->ops[0]);
    }
    *close = '\0';
    memset(&first, 0, sizeof(first));
    for (p = buf + 1; *p != '\0';) {
        char *comma = strchr(p, ',');
        char *dash;
        reg_t lo;
        reg_t hi;

        if (comma != NULL) {
            *comma = '\0';
        }
        dash = strchr(p, '-');
        if (dash != NULL) {
            *dash = '\0';
        }
        if (as_a64_parse_reg(p, &lo) != 0 || !lo.vec || lo.index >= 0 ||
            (dash != NULL && (as_a64_parse_reg(dash + 1, &hi) != 0 || !hi.vec || hi.esize != lo.esize ||
                              hi.q != lo.q))) {
            return fail(v, "'%s': '%s' is not a list of vectors", rq->mnemonic, rq->ops[0]);
        }
        if (dash == NULL) {
            hi = lo;
        }
        if (count == 0) {
            first = lo;
        } else if (lo.n != ((first.n + count) & 31u) || lo.esize != first.esize || lo.q != first.q) {
            return fail(v, "'%s': the vectors are one after another, and of one kind", rq->mnemonic);
        }
        count += ((hi.n - lo.n) & 31u) + 1;
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    if (count == 0 || count > 4) {
        return fail(v, "'%s': one to four vectors", rq->mnemonic);
    }
    text(rq->ops[1], buf, sizeof(buf));
    close = strchr(buf, ']');
    if (buf[0] != '[' || close == NULL || close[1] != '\0') {
        return fail(v, "'%s': '%s' is not a base register in brackets", rq->mnemonic, rq->ops[1]);
    }
    *close = '\0';
    if (as_a64_parse_reg(buf + 1, &base) != 0 || base.bits != 64 || base.is_zr || base.fp || base.vec) {
        return fail(v, "'%s': the base of an address is a register of 64 bits or the stack pointer", rq->mnemonic);
    }
    w |= (first.q << 30) | opcode[count] | (first.esize << 10) | (base.n << 5) | first.n;
    if (rq->op_count == 3) {
        reg_t rm;
        long long n;

        /* Afterwards the base has a register added, or the bytes that
         * were moved. */
        if (as_a64_parse_reg(rq->ops[2], &rm) == 0) {
            if (rm.bits != 64 || rm.fp || rm.vec || rm.is_sp || rm.is_zr) {
                return fail(v, "'%s': what is added to the base is a register of 64 bits", rq->mnemonic);
            }
            return word(v, w | 0x00800000u | (rm.n << 16));
        }
        if (number(rq->ops[2], &n) != 0 || n != (long long)(count * (first.q ? 16u : 8u))) {
            return fail(v, "'%s': the number added to the base is the bytes moved, %u", rq->mnemonic,
                        count * (first.q ? 16u : 8u));
        }
        return word(v, w | 0x00800000u | (31u << 16));
    }
    return word(v, w);
}

int as_a64_simd(const as_arm_req_t *rq, const char *m, as_arm_out_t *out, char *errbuf, size_t errbuf_sz) {
    simd_t v;
    reg_t r[3];
    int rc;

    memset(&v, 0, sizeof(v));
    v.rq = rq;
    v.out = out;
    v.errbuf = errbuf;
    v.errbuf_sz = errbuf_sz;
    v.m = m;
    if (strcmp(m, "ld1") == 0 || strcmp(m, "st1") == 0) {
        return load_store(&v);
    }
    if (strcmp(m, "movi") == 0) {
        return move_immediate(&v);
    }
    rc = scalar_fp(&v);
    if (rc <= 0) {
        return rc;
    }
    /* Two 64-bit numbers in the vector registers, added. */
    if ((strcmp(m, "add") == 0 || strcmp(m, "sub") == 0) && regs(&v, r, 3) == 0 && r[0].fp && r[0].bits == 64 &&
        r[1].fp && r[1].bits == 64 && r[2].fp && r[2].bits == 64) {
        return word(&v, (m[0] == 'a' ? 0x5ee08400u : 0x7ee08400u) | (r[2].n << 16) | (r[1].n << 5) | r[0].n);
    }
    rc = elements(&v);
    if (rc <= 0) {
        return rc;
    }
    rc = vector_two(&v);
    if (rc <= 0) {
        return rc;
    }
    rc = vector_three(&v);
    if (rc <= 0) {
        return rc;
    }
    return fail(&v, "'%s' is not an instruction of AArch64 that this assembler has, or not with these operands",
                rq->mnemonic);
}
