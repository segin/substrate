/*
 * consteval.c: integer constant expressions -- the one evaluator the
 * parser, sema and lowering share.
 */

#include "cc_consteval.h"
#include "cc_target.h"
#include "cc_types.h"

#include <ctype.h>
#include <float.h>
#include <string.h>

/**
 * A type without its qualifiers.
 *
 * @param t  the type, or NULL
 * @return   its unqualified version; NULL for NULL
 */
static const cc_ctype_t *const_unqual(const cc_ctype_t *t) {
    return t != NULL ? t->unqual : NULL;
}

/** @return `int` */
static const cc_ctype_t *const_int(void) {
    return cc_ct_basic(CC_CT_INT);
}

/** @return the unsigned type of a pointer's width: `size_t`'s type */
static const cc_ctype_t *const_uintptr(void) {
    return cc_ct_basic(cc_target_pointer_size() >= 8 ? CC_CT_ULONG : CC_CT_UINT);
}

/**
 * Whether values of a type read as unsigned: the unsigned integer types,
 * _Bool and pointers.
 *
 * @param t  the type
 * @return   1 if they do, else 0
 */
static int const_type_unsigned(const cc_ctype_t *t) {
    return cc_ct_is_unsigned(t) || (t != NULL && t->kind == CC_CT_POINTER);
}

/**
 * Whether a constant reads as unsigned.
 *
 * @param v  the constant
 * @return   1 if it does, else 0
 */
int cc_const_is_unsigned(const cc_const_t *v) {
    return v != NULL && const_type_unsigned(v->type);
}

int cc_const_fits(const cc_const_t *v, const cc_ctype_t *t) {
    long width = cc_ct_int_width(t);
    int neg = !cc_const_is_unsigned(v) && (int64_t)v->bits < 0;
    uint64_t mag = neg ? (uint64_t)0 - v->bits : v->bits;

    if (width <= 0 || width > 64) {
        return 1;
    }
    if (t->unqual->kind == CC_CT_BOOL || cc_ct_is_unsigned(t)) {
        return !neg && (width == 64 || mag <= ((uint64_t)1 << width) - 1);
    }
    /* signed: -2^(w-1) .. 2^(w-1) - 1 */
    return neg ? mag <= (uint64_t)1 << (width - 1) : mag <= ((uint64_t)1 << (width - 1)) - 1;
}

/**
 * The width in bits of an integer or pointer type.
 *
 * @param t  the type
 * @return   1 to 64, or 0 if @p t is neither an integer type nor a
 *           pointer, or is wider than 64 bits
 */
static int const_type_width(const cc_ctype_t *t) {
    long width = cc_ct_int_width(t);

    return width <= 64 ? (int)width : 0;
}

/**
 * Reduce 64 bits to a type's width and extend them back as the type
 * reads them.
 *
 * @param raw    the bits
 * @param width  the type's width, 1 to 64
 * @param is_unsigned  whether the type reads as unsigned
 * @return       the canonical bits of the value
 */
static uint64_t const_extend(uint64_t raw, int width, int is_unsigned) {
    uint64_t mask;

    if (width >= 64) {
        return raw;
    }
    mask = (1ULL << width) - 1ULL;
    raw &= mask;
    if (!is_unsigned && (raw & (1ULL << (width - 1))) != 0) {
        raw |= ~mask;
    }
    return raw;
}

/**
 * Make a constant of a type from 64 bits, wrapping them to its width.
 *
 * @param raw  the bits
 * @param t    the type
 * @param out  receives the constant
 * @return     0, or -1 if @p t is not an integer or pointer type
 */
static int const_make(uint64_t raw, const cc_ctype_t *t, cc_const_t *out) {
    int width = const_type_width(t);

    if (width <= 0) {
        return -1;
    }
    out->bits = const_extend(raw, width, const_type_unsigned(t));
    out->type = const_unqual(t);
    return 0;
}

/**
 * Convert a constant to another integer (or pointer) type, as a cast
 * does.
 *
 * @param v    the constant
 * @param t    the type to convert to
 * @param out  receives the result; may be @p v
 * @return     0, or -1 if @p t is not an integer or pointer type
 */
static int const_convert(const cc_const_t *v, const cc_ctype_t *t, cc_const_t *out) {
    uint64_t raw = v->bits;

    if (t != NULL && t->kind == CC_CT_BOOL) {
        raw = raw != 0 ? 1 : 0;
    }
    return const_make(raw, t, out);
}

/**
 * The type of an operand the evaluator does not look into: the type the
 * parser or sema recorded.
 *
 * @param e  the expression
 * @return   its type, unqualified; int where none is recorded yet
 */
static const cc_ctype_t *const_leaf_type(const cc_expr_t *e) {
    return e->ctype != NULL ? const_unqual(e->ctype) : const_int();
}

/**
 * The type a cast converts to.
 *
 * @param e  the cast
 * @return   the type, unqualified; int where none is known
 */
static const cc_ctype_t *const_cast_type(const cc_expr_t *e) {
    return e->ctype != NULL ? const_unqual(e->ctype) : const_int();
}

/**
 * The bit width a __builtin_clz/ctz/ffs/popcount/parity variant works
 * on, from its suffix.
 *
 * @param name  the builtin's name
 * @param base  the name without suffix, e.g. "__builtin_clz"
 * @return      32, 64 or the width of long; 0 if @p name is not @p base
 *              with an optional l or ll suffix
 */
static int const_bitop_width(const char *name, const char *base) {
    size_t n = strlen(base);

    if (strncmp(name, base, n) != 0) {
        return 0;
    }
    if (name[n] == '\0') {
        return 32;
    }
    if (strcmp(name + n, "l") == 0) {
        return cc_target_pointer_size() * 8;
    }
    if (strcmp(name + n, "ll") == 0) {
        return 64;
    }
    return 0;
}

/**
 * The bytes of narrow string literal @p e, its escapes decoded, up to its
 * first NUL (as strlen and strcmp see it), through any conversion of it.
 *
 * @param e    the expression
 * @param buf  receives the bytes; not NUL-terminated
 * @param cap  its size
 * @param len  receives how many
 * @return     0, or -1 if @p e is no narrow literal (or one with a universal character name, or too long)
 */
static int const_literal_bytes(const cc_expr_t *e, unsigned char *buf, size_t cap, size_t *len) {
    const unsigned char *s;
    size_t n;
    size_t i;
    size_t out = 0;

    while (e != NULL && e->kind == CC_EXPR_CAST && e->lhs != NULL) {
        e = e->lhs;
    }
    if (e == NULL || e->kind != CC_EXPR_STR || e->ident == NULL || cc_string_literal_is_wide(e)) {
        return -1;
    }
    s = (const unsigned char *)e->ident;
    n = strlen(e->ident);
    if (n < 2 || s[0] != '"' || s[n - 1] != '"') {
        return -1;
    }
    for (i = 1; i < n - 1; ++i) {
        unsigned v = s[i];

        if (s[i] == '\\' && i + 1 < n - 1) {
            unsigned char c = s[++i];

            if (c == 'x') {
                v = 0;
                while (i + 1 < n - 1 && isxdigit(s[i + 1])) {
                    c = s[++i];
                    v = v * 16 + (unsigned)(isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10));
                }
            } else if (c >= '0' && c <= '7') {
                int k = 1;

                v = c - '0';
                while (k < 3 && i + 1 < n - 1 && s[i + 1] >= '0' && s[i + 1] <= '7') {
                    v = v * 8 + (unsigned)(s[++i] - '0');
                    k++;
                }
            } else if (c == 'u' || c == 'U') {
                return -1;
            } else {
                v = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'a' ? '\a' : c == 'b' ? '\b'
                    : c == 'f' ? '\f' : c == 'v' ? '\v' : c == 'e' ? 27 : c;
            }
        }
        if ((v & 0xffu) == 0) {
            break;
        }
        if (out >= cap) {
            return -1;
        }
        buf[out++] = (unsigned char)v;
    }
    *len = out;
    return 0;
}

/**
 * The result type of a builtin the evaluator knows.
 *
 * @param e  the call
 * @return   its type
 */
static const cc_ctype_t *const_builtin_type(const cc_expr_t *e) {
    const char *name = e->ident;

    if (strcmp(name, "__builtin_bswap16") == 0) {
        return cc_ct_basic(CC_CT_USHORT);
    }
    if (strcmp(name, "__builtin_bswap32") == 0) {
        return cc_ct_basic(CC_CT_UINT);
    }
    if (strcmp(name, "__builtin_bswap64") == 0) {
        return cc_ct_basic(cc_target_pointer_size() >= 8 ? CC_CT_ULONG : CC_CT_ULLONG);
    }
    if (strcmp(name, "__builtin_expect") == 0) {
        return cc_ct_basic(CC_CT_LONG);
    }
    if (strcmp(name, "__builtin_strlen") == 0) {
        return const_uintptr();
    }
    if (strcmp(name, "__builtin_choose_expr") == 0 && e->arg_count == 3) {
        cc_const_t c;
        if (cc_const_eval(NULL, e->args[0], &c) == 0) {
            return cc_const_type_of(c.bits != 0 ? e->args[1] : e->args[2]);
        }
    }
    return const_int();
}

/**
 * The type a constant expression has, without evaluating it.
 *
 * @param e  the expression
 * @return   its type, unqualified; int where it cannot be told
 */
const cc_ctype_t *cc_const_type_of(const cc_expr_t *e) {
    if (e == NULL) {
        return const_int();
    }
    switch (e->kind) {
    case CC_EXPR_CAST:
        return const_cast_type(e);
    case CC_EXPR_SIZEOF:
        return const_uintptr();
    case CC_EXPR_BIN:
        switch (e->op) {
        case CC_BIN_EQ:
        case CC_BIN_NE:
        case CC_BIN_LT:
        case CC_BIN_LE:
        case CC_BIN_GT:
        case CC_BIN_GE:
        case CC_BIN_LAND:
        case CC_BIN_LOR:
            return const_int();
        case CC_BIN_COMMA:
            return cc_const_type_of(e->rhs);
        case CC_BIN_SHL:
        case CC_BIN_SHR:
            return cc_ct_promote(cc_const_type_of(e->lhs));
        default:
            return cc_ct_common_int(cc_const_type_of(e->lhs), cc_const_type_of(e->rhs));
        }
    case CC_EXPR_TERNARY:
        return cc_ct_common_int(cc_const_type_of(e->rhs != NULL ? e->rhs : e->lhs), cc_const_type_of(e->third));
    case CC_EXPR_CALL:
        return e->ident != NULL ? const_builtin_type(e) : const_int();
    default:
        return const_leaf_type(e);
    }
}

/**
 * Evaluate a call to one of the builtins that fold to a constant:
 * __builtin_constant_p, __builtin_expect, __builtin_choose_expr,
 * __builtin_bswap16/32/64 and __builtin_clz/ctz/ffs/popcount/parity
 * with their l and ll forms.
 *
 * @param env  the context
 * @param e    the call
 * @param out  receives the value
 * @return     0, or -1 if the call is not a constant
 */
static int const_eval_builtin(const cc_const_env_t *env, const cc_expr_t *e, cc_const_t *out) {
    static const char *const bitops[] = {"__builtin_clz", "__builtin_ctz", "__builtin_ffs", "__builtin_popcount",
                                         "__builtin_parity"};
    const char *name = e->ident;
    cc_const_t arg;
    size_t k;

    if (strcmp(name, "__builtin_constant_p") == 0) {
        if (e->arg_count != 1) {
            return -1;
        }
        return const_make(cc_const_eval(env, e->args[0], &arg) == 0 ? 1 : 0, const_int(), out);
    }
    if (strcmp(name, "__builtin_expect") == 0) {
        if (e->arg_count != 2 || cc_const_eval(env, e->args[0], &arg) != 0) {
            return -1;
        }
        return const_convert(&arg, cc_ct_basic(CC_CT_LONG), out);
    }
    if (strcmp(name, "__builtin_choose_expr") == 0) {
        if (e->arg_count != 3 || cc_const_eval(env, e->args[0], &arg) != 0) {
            return -1;
        }
        return cc_const_eval(env, arg.bits != 0 ? e->args[1] : e->args[2], out);
    }
    if (strcmp(name, "__builtin_strlen") == 0 || strcmp(name, "__builtin_strcmp") == 0) {
        /*
         * Of string literals, constants, as gcc folds them: Linux's
         * MODULE_INFO asserts `sizeof(s) - 1 == __builtin_strlen(s)`, and
         * its sched comparisons pick an arm with `!__builtin_strcmp(">", "<")`.
         */
        unsigned char a[512];
        unsigned char b[512];
        size_t la;
        size_t lb;
        size_t i;
        int r = 0;

        if (strcmp(name, "__builtin_strlen") == 0) {
            if (e->arg_count != 1 || const_literal_bytes(e->args[0], a, sizeof(a), &la) != 0) {
                return -1;
            }
            return const_make((uint64_t)la, const_uintptr(), out);
        }
        if (e->arg_count != 2 || const_literal_bytes(e->args[0], a, sizeof(a), &la) != 0 ||
            const_literal_bytes(e->args[1], b, sizeof(b), &lb) != 0) {
            return -1;
        }
        for (i = 0; i < la && i < lb && a[i] == b[i]; ++i) {
        }
        if (i < la && i < lb) {
            r = a[i] < b[i] ? -1 : 1;
        } else if (la != lb) {
            r = la < lb ? -1 : 1;
        }
        return const_make((uint64_t)(int64_t)r, const_int(), out);
    }
    if (e->arg_count != 1 || cc_const_eval(env, e->args[0], &arg) != 0) {
        return -1;
    }
    if (strncmp(name, "__builtin_bswap", 15) == 0) {
        const cc_ctype_t *t = const_builtin_type(e);
        int width = const_type_width(t);
        uint64_t v = arg.bits;
        uint64_t r = 0;
        int i;

        if (strcmp(name + 15, "16") != 0 && strcmp(name + 15, "32") != 0 && strcmp(name + 15, "64") != 0) {
            return -1;
        }
        for (i = 0; i < width / 8; ++i) {
            r = (r << 8) | ((v >> (8 * i)) & 0xffu);
        }
        return const_make(r, t, out);
    }
    for (k = 0; k < sizeof(bitops) / sizeof(bitops[0]); ++k) {
        int width = const_bitop_width(name, bitops[k]);
        uint64_t v = arg.bits;
        int n = 0;
        int i;

        if (width == 0) {
            continue;
        }
        if (width < 64) {
            v &= (1ULL << width) - 1ULL;
        }
        switch (k) {
        case 0: /* clz: undefined for 0, so not a constant */
            if (v == 0) {
                return -1;
            }
            for (i = width - 1; (v & (1ULL << i)) == 0; --i) {
                ++n;
            }
            break;
        case 1: /* ctz */
            if (v == 0) {
                return -1;
            }
            for (i = 0; (v & (1ULL << i)) == 0; ++i) {
                ++n;
            }
            break;
        case 2: /* ffs: one more than ctz, 0 for 0 */
            if (v != 0) {
                for (i = 0; (v & (1ULL << i)) == 0; ++i) {
                    ++n;
                }
                ++n;
            }
            break;
        default: /* popcount, parity */
            for (i = 0; i < width; ++i) {
                n += (int)((v >> i) & 1u);
            }
            if (k == 4) {
                n &= 1;
            }
            break;
        }
        return const_make((uint64_t)n, const_int(), out);
    }
    return -1;
}

/**
 * Evaluate a cast whose operand is a floating constant, as (int)3.7 is:
 * C allows floating constants as the immediate operands of casts in an
 * integer constant expression.
 *
 * @param e    the cast
 * @param out  receives the value
 * @return     0, or -1 if the value does not fit the type
 */
static int const_eval_float_cast(const cc_expr_t *e, cc_const_t *out) {
    long double d = e->lhs->float_val_ld != 0.0L ? e->lhs->float_val_ld : (long double)e->lhs->float_val;
    const cc_ctype_t *t = const_cast_type(e);
    int width = const_type_width(t);
    long double lo;
    long double hi;

    if (width <= 0) {
        return -1;
    }
    if (t->kind == CC_CT_BOOL) {
        return const_make(d != 0.0L ? 1 : 0, t, out);
    }
    /* the value, truncated toward zero, must fit the type */
    if (const_type_unsigned(t)) {
        lo = -1.0L;
        hi = width >= 64 ? 18446744073709551616.0L : (long double)(1ULL << width);
    } else {
        lo = -(long double)(1ULL << (width - 1)) - 1.0L;
        hi = (long double)(1ULL << (width - 1));
    }
    if (!(d > lo && d < hi)) {
        return -1;
    }
    if (d < 0) {
        return const_make((uint64_t)(int64_t)d, t, out);
    }
    return const_make((uint64_t)d, t, out);
}

/**
 * Evaluate a binary operator other than && || and the comma.
 *
 * @param e    the expression
 * @param a    its left operand's value
 * @param b    its right operand's value
 * @param out  receives the value
 * @return     0, or -1 if it is not a constant (division by zero)
 */
static int const_eval_arith(const cc_expr_t *e, const cc_const_t *a, const cc_const_t *b, cc_const_t *out) {
    int shift = e->op == CC_BIN_SHL || e->op == CC_BIN_SHR;
    const cc_ctype_t *t = shift ? cc_ct_promote(a->type) : cc_ct_common_int(a->type, b->type);
    cc_const_t x;
    cc_const_t y;
    int is_unsigned;
    uint64_t r;

    if (const_convert(a, t, &x) != 0) {
        return -1;
    }
    if (shift) {
        y = *b;
    } else if (const_convert(b, t, &y) != 0) {
        return -1;
    }
    is_unsigned = const_type_unsigned(t);
    switch (e->op) {
    case CC_BIN_ADD:
        r = x.bits + y.bits;
        break;
    case CC_BIN_SUB:
        r = x.bits - y.bits;
        break;
    case CC_BIN_MUL:
        r = x.bits * y.bits;
        break;
    case CC_BIN_DIV:
    case CC_BIN_MOD:
        if (y.bits == 0) {
            return -1;
        }
        if (is_unsigned) {
            r = e->op == CC_BIN_DIV ? x.bits / y.bits : x.bits % y.bits;
        } else if ((int64_t)y.bits == -1) {
            /* x / -1 is -x; it only overflows for the minimum, which wraps */
            r = e->op == CC_BIN_DIV ? 0 - x.bits : 0;
        } else {
            r = e->op == CC_BIN_DIV ? (uint64_t)((int64_t)x.bits / (int64_t)y.bits)
                                    : (uint64_t)((int64_t)x.bits % (int64_t)y.bits);
        }
        break;
    case CC_BIN_SHL:
        r = x.bits << (y.bits & 63);
        break;
    case CC_BIN_SHR:
        r = is_unsigned ? x.bits >> (y.bits & 63) : (uint64_t)((int64_t)x.bits >> (y.bits & 63));
        break;
    case CC_BIN_BAND:
        r = x.bits & y.bits;
        break;
    case CC_BIN_BOR:
        r = x.bits | y.bits;
        break;
    case CC_BIN_BXOR:
        r = x.bits ^ y.bits;
        break;
    case CC_BIN_EQ:
        return const_make(x.bits == y.bits, const_int(), out);
    case CC_BIN_NE:
        return const_make(x.bits != y.bits, const_int(), out);
    case CC_BIN_LT:
        return const_make(is_unsigned ? x.bits < y.bits : (int64_t)x.bits < (int64_t)y.bits, const_int(), out);
    case CC_BIN_LE:
        return const_make(is_unsigned ? x.bits <= y.bits : (int64_t)x.bits <= (int64_t)y.bits, const_int(), out);
    case CC_BIN_GT:
        return const_make(is_unsigned ? x.bits > y.bits : (int64_t)x.bits > (int64_t)y.bits, const_int(), out);
    case CC_BIN_GE:
        return const_make(is_unsigned ? x.bits >= y.bits : (int64_t)x.bits >= (int64_t)y.bits, const_int(), out);
    default:
        return -1;
    }
    return const_make(r, t, out);
}

/**
 * Evaluate an integer constant expression.
 *
 * @param env  the context; its callback may be NULL
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int cc_const_eval(const cc_const_env_t *env, const cc_expr_t *e, cc_const_t *out) {
    cc_const_t a;
    cc_const_t b;

    if (e == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    switch (e->kind) {
    case CC_EXPR_INT:
        return const_make((uint64_t)e->int_val, const_leaf_type(e), out);
    case CC_EXPR_CAST:
        if (e->lhs != NULL && e->lhs->kind == CC_EXPR_FLOAT) {
            return const_eval_float_cast(e, out);
        }
        if (cc_const_eval(env, e->lhs, &a) != 0) {
            return -1;
        }
        return const_convert(&a, const_cast_type(e), out);
    case CC_EXPR_SIZEOF: {
        long n = env != NULL && env->sizeof_bytes != NULL ? env->sizeof_bytes(env->ctx, e) : -1;
        if (n < 0) {
            return -1;
        }
        return const_make((uint64_t)n, const_uintptr(), out);
    }
    case CC_EXPR_BIN:
        if (e->op == CC_BIN_COMMA) {
            return cc_const_eval(env, e->rhs, out);
        }
        if (cc_const_eval(env, e->lhs, &a) != 0) {
            return -1;
        }
        if (e->op == CC_BIN_LAND || e->op == CC_BIN_LOR) {
            /* the right operand is evaluated only when it decides */
            if ((a.bits != 0) == (e->op == CC_BIN_LOR)) {
                return const_make(e->op == CC_BIN_LOR, const_int(), out);
            }
            if (cc_const_eval(env, e->rhs, &b) != 0) {
                return -1;
            }
            return const_make(b.bits != 0, const_int(), out);
        }
        if (cc_const_eval(env, e->rhs, &b) != 0) {
            return -1;
        }
        return const_eval_arith(e, &a, &b, out);
    case CC_EXPR_TERNARY: {
        const cc_ctype_t *t = cc_const_type_of(e);
        const cc_expr_t *arm;

        if (cc_const_eval(env, e->lhs, &a) != 0) {
            return -1;
        }
        if (a.bits != 0 && e->rhs == NULL) {
            return const_convert(&a, t, out);
        }
        arm = a.bits != 0 ? e->rhs : e->third;
        if (cc_const_eval(env, arm, &b) != 0) {
            return -1;
        }
        return const_convert(&b, t, out);
    }
    case CC_EXPR_CALL:
        if (e->ident == NULL) {
            return -1;
        }
        return const_eval_builtin(env, e, out);
    case CC_EXPR_IDENT:
    case CC_EXPR_MEMBER:
        /* a named constant (C23 6.6p7): a constexpr object or member, its value recorded where it is named */
        if (e->named_const_type == NULL) {
            return -1;
        }
        a.bits = (uint64_t)e->int_val;
        a.type = const_unqual(e->named_const_type);
        return const_convert(&a, a.type, out);
    default:
        return -1;
    }
}

/**
 * Evaluate an integer constant expression into a long long.
 *
 * @param env  the context
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int cc_const_eval_ll(const cc_const_env_t *env, const cc_expr_t *e, long long *out) {
    cc_const_t v;

    if (cc_const_eval(env, e, &v) != 0) {
        return -1;
    }
    *out = (long long)v.bits;
    return 0;
}
