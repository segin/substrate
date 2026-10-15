/*
 * consteval.c: integer constant expressions -- the one evaluator the
 * parser, sema and lowering share.
 */

#include "cc_consteval.h"
#include "cc_target.h"
#include "cc_types.h"

#include <float.h>
#include <string.h>

/**
 * Whether values of a type read as unsigned: the unsigned integer types,
 * _Bool and pointers.
 *
 * @param t  the type
 * @return   1 if they do, else 0
 */
static int const_type_unsigned(cc_type_t t) {
    return cc_type_is_unsigned_integral(t) || t == CC_TYPE_BOOL || cc_type_is_pointer(t);
}

/**
 * The width in bits of an integral or pointer type.
 *
 * @param env        the context
 * @param t          the type
 * @param struct_id  its struct id, or -1
 * @return           1 to 64, or 0 if @p t is neither integral nor a
 *                   pointer, or its size is not known
 */
static int const_type_width(const cc_const_env_t *env, cc_type_t t, int struct_id) {
    long size = 0;

    if (t == CC_TYPE_BOOL) {
        return 1;
    }
    if (cc_type_is_pointer(t)) {
        return cc_target_pointer_size() * 8;
    }
    if (!cc_type_is_integral(t)) {
        return 0;
    }
    if (env != NULL && env->type_size != NULL) {
        size = env->type_size(env->ctx, t, struct_id);
    }
    if (size <= 0) {
        size = cc_type_scalar_size(t);
    }
    if (size <= 0 || size > 8) {
        return 0;
    }
    return (int)size * 8;
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
 * @param env        the context
 * @param raw        the bits
 * @param t          the type
 * @param struct_id  its struct id, or -1
 * @param out        receives the constant
 * @return           0, or -1 if @p t is not integral or a pointer
 */
static int const_make(const cc_const_env_t *env, uint64_t raw, cc_type_t t, int struct_id, cc_const_t *out) {
    int width = const_type_width(env, t, struct_id);

    if (width <= 0) {
        return -1;
    }
    out->bits = const_extend(raw, width, const_type_unsigned(t));
    out->type = t;
    out->struct_id = struct_id;
    return 0;
}

/**
 * Convert a constant to another integral (or pointer) type.
 *
 * @param env        the context
 * @param v          the constant
 * @param t          the type to convert to
 * @param struct_id  its struct id, or -1
 * @param out        receives the result; may be @p v
 * @return           0, or -1 if @p t is not an integral or pointer type
 */
int cc_const_convert(const cc_const_env_t *env, const cc_const_t *v, cc_type_t t, int struct_id, cc_const_t *out) {
    uint64_t raw = v->bits;

    if (t == CC_TYPE_BOOL) {
        raw = raw != 0 ? 1 : 0;
    }
    return const_make(env, raw, t, struct_id, out);
}

/**
 * The type an enum or bit-precise operand takes part in arithmetic as.
 *
 * @param t  the type
 * @return   @p t after the integer promotions
 */
static cc_type_t const_promote(cc_type_t t) {
    if (t == CC_TYPE_ENUM) {
        return CC_TYPE_INT;
    }
    if (cc_type_is_pointer(t)) {
        return cc_target_pointer_size() >= 8 ? CC_TYPE_ULONG : CC_TYPE_UINT;
    }
    return cc_type_promote(t);
}

/**
 * The unsigned type of the same rank as a signed one.
 *
 * @param t  a signed integer type
 * @return   its unsigned counterpart
 */
static cc_type_t const_unsigned_of(cc_type_t t) {
    switch (t) {
    case CC_TYPE_LONG:
        return CC_TYPE_ULONG;
    case CC_TYPE_LONG_LONG:
        return CC_TYPE_ULONG_LONG;
    default:
        return CC_TYPE_UINT;
    }
}

/**
 * The common type of two integer operands (C11 6.3.1.8).
 *
 * @param a  one operand's type
 * @param b  the other's
 * @return   the type both are converted to
 */
cc_type_t cc_const_common_type(cc_type_t a, cc_type_t b) {
    cc_type_t u;
    cc_type_t s;

    a = const_promote(a);
    b = const_promote(b);
    if (!cc_type_is_integral(a) || !cc_type_is_integral(b)) {
        return CC_TYPE_INT;
    }
    if (a == b) {
        return a;
    }
    if (cc_type_is_unsigned_integral(a) == cc_type_is_unsigned_integral(b)) {
        return cc_type_integer_rank(a) >= cc_type_integer_rank(b) ? a : b;
    }
    u = cc_type_is_unsigned_integral(a) ? a : b;
    s = cc_type_is_unsigned_integral(a) ? b : a;
    if (cc_type_integer_rank(u) >= cc_type_integer_rank(s)) {
        return u;
    }
    if (cc_type_scalar_size(s) > cc_type_scalar_size(u)) {
        return s;
    }
    return const_unsigned_of(s);
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
 * The result type of a builtin the evaluator knows.
 *
 * @param e  the call
 * @return   its type
 */
static cc_type_t const_builtin_type(const cc_expr_t *e) {
    const char *name = e->ident;

    if (strcmp(name, "__builtin_bswap16") == 0) {
        return CC_TYPE_USHORT;
    }
    if (strcmp(name, "__builtin_bswap32") == 0) {
        return CC_TYPE_UINT;
    }
    if (strcmp(name, "__builtin_bswap64") == 0) {
        return cc_target_pointer_size() >= 8 ? CC_TYPE_ULONG : CC_TYPE_ULONG_LONG;
    }
    if (strcmp(name, "__builtin_expect") == 0) {
        return CC_TYPE_LONG;
    }
    if (strcmp(name, "__builtin_choose_expr") == 0 && e->arg_count == 3) {
        cc_const_t c;
        if (cc_const_eval(NULL, e->args[0], &c) == 0) {
            return cc_const_type_of(c.bits != 0 ? e->args[1] : e->args[2]);
        }
    }
    return CC_TYPE_INT;
}

/**
 * The type a constant expression has, without evaluating it.
 *
 * @param e  the expression
 * @return   its type; int where it cannot be told
 */
cc_type_t cc_const_type_of(const cc_expr_t *e) {
    if (e == NULL) {
        return CC_TYPE_INT;
    }
    switch (e->kind) {
    case CC_EXPR_CAST:
        return e->aux_type;
    case CC_EXPR_SIZEOF:
        return cc_target_pointer_size() >= 8 ? CC_TYPE_ULONG : CC_TYPE_UINT;
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
            return CC_TYPE_INT;
        case CC_BIN_COMMA:
            return cc_const_type_of(e->rhs);
        case CC_BIN_SHL:
        case CC_BIN_SHR:
            return const_promote(cc_const_type_of(e->lhs));
        default:
            return cc_const_common_type(cc_const_type_of(e->lhs), cc_const_type_of(e->rhs));
        }
    case CC_EXPR_TERNARY:
        return cc_const_common_type(cc_const_type_of(e->rhs != NULL ? e->rhs : e->lhs), cc_const_type_of(e->third));
    case CC_EXPR_CALL:
        return e->ident != NULL ? const_builtin_type(e) : CC_TYPE_INT;
    default:
        return e->value_type != CC_TYPE_VOID ? e->value_type : CC_TYPE_INT;
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
        return const_make(env, cc_const_eval(env, e->args[0], &arg) == 0 ? 1 : 0, CC_TYPE_INT, -1, out);
    }
    if (strcmp(name, "__builtin_expect") == 0) {
        if (e->arg_count != 2 || cc_const_eval(env, e->args[0], &arg) != 0) {
            return -1;
        }
        return cc_const_convert(env, &arg, CC_TYPE_LONG, -1, out);
    }
    if (strcmp(name, "__builtin_choose_expr") == 0) {
        if (e->arg_count != 3 || cc_const_eval(env, e->args[0], &arg) != 0) {
            return -1;
        }
        return cc_const_eval(env, arg.bits != 0 ? e->args[1] : e->args[2], out);
    }
    if (e->arg_count != 1 || cc_const_eval(env, e->args[0], &arg) != 0) {
        return -1;
    }
    if (strncmp(name, "__builtin_bswap", 15) == 0) {
        cc_type_t t = const_builtin_type(e);
        int width = const_type_width(env, t, -1);
        uint64_t v = arg.bits;
        uint64_t r = 0;
        int i;

        if (strcmp(name + 15, "16") != 0 && strcmp(name + 15, "32") != 0 && strcmp(name + 15, "64") != 0) {
            return -1;
        }
        for (i = 0; i < width / 8; ++i) {
            r = (r << 8) | ((v >> (8 * i)) & 0xffu);
        }
        return const_make(env, r, t, -1, out);
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
        return const_make(env, (uint64_t)n, CC_TYPE_INT, -1, out);
    }
    return -1;
}

/**
 * Evaluate a cast whose operand is a floating constant, as (int)3.7 is:
 * C allows floating constants as the immediate operands of casts in an
 * integer constant expression.
 *
 * @param env  the context
 * @param e    the cast
 * @param out  receives the value
 * @return     0, or -1 if the value does not fit the type
 */
static int const_eval_float_cast(const cc_const_env_t *env, const cc_expr_t *e, cc_const_t *out) {
    long double d = e->lhs->float_val_ld != 0.0L ? e->lhs->float_val_ld : (long double)e->lhs->float_val;
    cc_type_t t = e->aux_type;
    int width = const_type_width(env, t, e->aux_struct_id);
    long double lo;
    long double hi;

    if (width <= 0) {
        return -1;
    }
    if (t == CC_TYPE_BOOL) {
        return const_make(env, d != 0.0L ? 1 : 0, t, e->aux_struct_id, out);
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
        return const_make(env, (uint64_t)(int64_t)d, t, e->aux_struct_id, out);
    }
    return const_make(env, (uint64_t)d, t, e->aux_struct_id, out);
}

/**
 * Evaluate a binary operator other than && || and the comma.
 *
 * @param env  the context
 * @param e    the expression
 * @param a    its left operand's value
 * @param b    its right operand's value
 * @param out  receives the value
 * @return     0, or -1 if it is not a constant (division by zero)
 */
static int const_eval_arith(const cc_const_env_t *env, const cc_expr_t *e, const cc_const_t *a, const cc_const_t *b,
                            cc_const_t *out) {
    int shift = e->op == CC_BIN_SHL || e->op == CC_BIN_SHR;
    cc_type_t t = shift ? const_promote(a->type) : cc_const_common_type(a->type, b->type);
    cc_const_t x;
    cc_const_t y;
    int is_unsigned;
    uint64_t r;

    if (cc_const_convert(env, a, t, -1, &x) != 0) {
        return -1;
    }
    if (shift) {
        y = *b;
    } else if (cc_const_convert(env, b, t, -1, &y) != 0) {
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
        return const_make(env, x.bits == y.bits, CC_TYPE_INT, -1, out);
    case CC_BIN_NE:
        return const_make(env, x.bits != y.bits, CC_TYPE_INT, -1, out);
    case CC_BIN_LT:
        return const_make(env, is_unsigned ? x.bits < y.bits : (int64_t)x.bits < (int64_t)y.bits, CC_TYPE_INT, -1,
                          out);
    case CC_BIN_LE:
        return const_make(env, is_unsigned ? x.bits <= y.bits : (int64_t)x.bits <= (int64_t)y.bits, CC_TYPE_INT,
                          -1, out);
    case CC_BIN_GT:
        return const_make(env, is_unsigned ? x.bits > y.bits : (int64_t)x.bits > (int64_t)y.bits, CC_TYPE_INT, -1,
                          out);
    case CC_BIN_GE:
        return const_make(env, is_unsigned ? x.bits >= y.bits : (int64_t)x.bits >= (int64_t)y.bits, CC_TYPE_INT,
                          -1, out);
    default:
        return -1;
    }
    return const_make(env, r, t, -1, out);
}

/**
 * Evaluate an integer constant expression.
 *
 * @param env  the context; its callbacks may be NULL
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
        return const_make(env, (uint64_t)e->int_val, e->value_type != CC_TYPE_VOID ? e->value_type : CC_TYPE_INT,
                          e->struct_id, out);
    case CC_EXPR_CAST:
        if (e->lhs != NULL && e->lhs->kind == CC_EXPR_FLOAT) {
            return const_eval_float_cast(env, e, out);
        }
        if (cc_const_eval(env, e->lhs, &a) != 0) {
            return -1;
        }
        return cc_const_convert(env, &a, e->aux_type, e->aux_struct_id, out);
    case CC_EXPR_SIZEOF: {
        long n = env != NULL && env->sizeof_bytes != NULL ? env->sizeof_bytes(env->ctx, e) : -1;
        if (n < 0) {
            return -1;
        }
        return const_make(env, (uint64_t)n, cc_const_type_of(e), -1, out);
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
                return const_make(env, e->op == CC_BIN_LOR, CC_TYPE_INT, -1, out);
            }
            if (cc_const_eval(env, e->rhs, &b) != 0) {
                return -1;
            }
            return const_make(env, b.bits != 0, CC_TYPE_INT, -1, out);
        }
        if (cc_const_eval(env, e->rhs, &b) != 0) {
            return -1;
        }
        return const_eval_arith(env, e, &a, &b, out);
    case CC_EXPR_TERNARY: {
        cc_type_t t = cc_const_type_of(e);
        const cc_expr_t *arm;

        if (cc_const_eval(env, e->lhs, &a) != 0) {
            return -1;
        }
        if (a.bits != 0 && e->rhs == NULL) {
            return cc_const_convert(env, &a, t, -1, out);
        }
        arm = a.bits != 0 ? e->rhs : e->third;
        if (cc_const_eval(env, arm, &b) != 0) {
            return -1;
        }
        return cc_const_convert(env, &b, t, -1, out);
    }
    case CC_EXPR_CALL:
        if (e->ident == NULL) {
            return -1;
        }
        return const_eval_builtin(env, e, out);
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
