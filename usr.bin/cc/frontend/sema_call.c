/*
 * sema_call.c: checking a call -- the builtins the compiler implements
 * itself (sema_check_builtin_call()), and an ordinary call: its callee,
 * its arguments converted to the parameters, the printf-format and
 * transparent-union rules, its type (sema_check_call()).
 */

#include "cc_frontend.h"
#include "cc_asm.h"
#include "cc_consteval.h"
#include "cc_ctype.h"
#include "cc_decimal.h"
#include "cc_diag.h"
#include "cc_lang.h"
#include "cc_options.h"
#include "cc_types.h"
#include "cc_util.h"
#include "cc_sema.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** sema_check_builtin_call() found no builtin to check: the call is an ordinary one. */
#define SEMA_NOT_BUILTIN 1

/** The C11 memory orders, as the __atomic builtins number them. */
enum {
    CC_ATOMIC_RELAXED = 0,
    CC_ATOMIC_CONSUME = 1,
    CC_ATOMIC_ACQUIRE = 2,
    CC_ATOMIC_RELEASE = 3,
    CC_ATOMIC_ACQ_REL = 4,
    CC_ATOMIC_SEQ_CST = 5
};

static const cc_ctype_t *sema_callee_ret_ctype(const cc_ctype_t *t);
static int sx_is_arith(const cc_expr_t *x);
static int printf_like_format_index(const char *name, size_t *idx_out);
static char *rewrite_printf_long_double_format(const char *literal);
static const cc_function_t *generic_selected_function(const cc_translation_unit_t *tu, const cc_expr_t *e);
static const cc_function_t *infer_callee_function_expr(const cc_translation_unit_t *tu, const cc_expr_t *e);
static int builtin_bswap_bits(const char *name);
static builtin_kind_t builtin_kind(const char *name);
static int atomic_memorder_is_valid(long mo);
static int atomic_memorder_valid_for_load(long mo);
static int atomic_memorder_valid_for_store(long mo);
static int atomic_memorder_check_arg(const cc_translation_unit_t *tu, cc_expr_t *arg, const char *builtin_name,
                                     const char *op_kind, int allow_load_orders, int allow_store_orders,
                                     cc_diag_t *diag);
static int sema_builtin_complex(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count,
                                int depth, cc_diag_t *diag);
static const cc_ctype_t *sema_atomic_elem(const cc_expr_t *ptr);
static int sema_transparent_union_accepts(const cc_translation_unit_t *tu, int sid, const cc_expr_t *arg);
static int sema_convert_call_arg(const cc_translation_unit_t *tu, cc_expr_t *e, const cc_function_t *callee,
                                 size_t i);
static int sema_check_builtin_call(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars,
                                   size_t var_count, int depth, builtin_kind_t bk, int bswap_bits,
                                   const cc_function_t *callee, cc_diag_t *diag);

/**
 * What a call through a callee of type @p t gives: the return type of the
 * function @p t is, or points to.  A pointer to a function typedef
 * (`typedef T fn_t(...); fn_t *p`) is recorded as a pointer to T, so a
 * pointer to anything else gives what it points to.
 *
 * @return the type, or NULL if @p t is not known or is neither
 */
static const cc_ctype_t *sema_callee_ret_ctype(const cc_ctype_t *t) {
    if (t != NULL && t->unqual->kind == CC_CT_POINTER) {
        t = t->unqual->of;
        if (t->unqual->kind != CC_CT_FUNCTION) {
            return t;
        }
    }
    return (t != NULL && t->unqual->kind == CC_CT_FUNCTION) ? t->unqual->of : NULL;
}

/** Whether checked expression @p x has an arithmetic type. */
static int sx_is_arith(const cc_expr_t *x) {
    return cc_ct_is_arithmetic(sx_vtype(x));
}

static int printf_like_format_index(const char *name, size_t *idx_out) {
    if (name == NULL || idx_out == NULL) {
        return 0;
    }
    if (strcmp(name, "printf") == 0 || strcmp(name, "vprintf") == 0) {
        *idx_out = 0;
        return 1;
    }
    if (strcmp(name, "fprintf") == 0 || strcmp(name, "sprintf") == 0 || strcmp(name, "vfprintf") == 0 ||
        strcmp(name, "vsprintf") == 0) {
        *idx_out = 1;
        return 1;
    }
    if (strcmp(name, "snprintf") == 0 || strcmp(name, "vsnprintf") == 0) {
        *idx_out = 2;
        return 1;
    }
    return 0;
}

static char *rewrite_printf_long_double_format(const char *literal) {
    /*
     * Keep literal format strings intact. Rewriting `%Lf` to `%f` breaks
     * varargs ABI and causes argument desynchronization at runtime.
     */
    (void)literal;
    return NULL;
}

static const cc_function_t *generic_selected_function(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    const cc_expr_t *sel;
    if (tu == NULL || e == NULL || e->kind != CC_EXPR_GENERIC || e->args == NULL || e->arg_count == 0) {
        return NULL;
    }
    if (e->generic_selected < 0 || (size_t)e->generic_selected >= e->arg_count) {
        return NULL;
    }
    sel = e->args[e->generic_selected];
    if (sel == NULL || sel->kind != CC_EXPR_IDENT || sel->ident == NULL) {
        return NULL;
    }
    return find_function(tu, sel->ident);
}

static const cc_function_t *infer_callee_function_expr(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    const cc_function_t *a;
    const cc_function_t *b;

    if (tu == NULL || e == NULL) {
        return NULL;
    }
    switch (e->kind) {
    case CC_EXPR_IDENT:
        if (e->ident == NULL) {
            return NULL;
        }
        return find_function(tu, e->ident);
    case CC_EXPR_ADDR:
    case CC_EXPR_DEREF:
    case CC_EXPR_CAST:
        return infer_callee_function_expr(tu, e->lhs);
    case CC_EXPR_TERNARY:
        if (e->rhs == NULL || e->third == NULL) {
            return NULL;
        }
        a = infer_callee_function_expr(tu, e->rhs);
        b = infer_callee_function_expr(tu, e->third);
        if (a == NULL || b == NULL) {
            return NULL;
        }
        if (a->ret_ctype != b->ret_ctype) {
            return NULL;
        }
        return a;
    case CC_EXPR_GENERIC:
        return generic_selected_function(tu, e);
    default:
        return NULL;
    }
}

static int builtin_bswap_bits(const char *name) {
    return(cc_builtin_bswap_bits(name));
}

static builtin_kind_t builtin_kind(const char *name) {
    if (name == NULL) {
        return BUILTIN_NONE;
    }
    if (strcmp(name, "__builtin_va_start") == 0 || strcmp(name, "__builtin_c23_va_start") == 0) {
        return BUILTIN_VA_START;
    }
    if (strcmp(name, "__builtin_va_end") == 0 || strcmp(name, "__builtin_c23_va_end") == 0) {
        return BUILTIN_VA_END;
    }
    if (strcmp(name, "__builtin_va_copy") == 0 || strcmp(name, "__builtin_c23_va_copy") == 0) {
        return BUILTIN_VA_COPY;
    }
    if (strcmp(name, "__builtin_va_arg") == 0 || strcmp(name, "__builtin_c23_va_arg") == 0) {
        return BUILTIN_VA_ARG;
    }
    if (strcmp(name, "__builtin_expect") == 0) {
        return BUILTIN_EXPECT;
    }
    if (strcmp(name, "__builtin_constant_p") == 0) {
        return BUILTIN_CONSTANT_P;
    }
    if (strcmp(name, "__builtin_trap") == 0) {
        return BUILTIN_TRAP;
    }
    if (strcmp(name, "__builtin_unreachable") == 0) {
        return BUILTIN_UNREACHABLE;
    }
    if (strcmp(name, "__builtin_assume") == 0) {
        return BUILTIN_ASSUME;
    }
    if (strcmp(name, "__builtin_assume_aligned") == 0) {
        return BUILTIN_ASSUME_ALIGNED;
    }
    if (strcmp(name, "__builtin_unpredictable") == 0) {
        return BUILTIN_UNPREDICTABLE;
    }
    if (strcmp(name, "__builtin_clz") == 0 || strcmp(name, "__builtin_clzl") == 0 ||
        strcmp(name, "__builtin_clzll") == 0) {
        return BUILTIN_CLZ;
    }
    if (strcmp(name, "__builtin_ctz") == 0 || strcmp(name, "__builtin_ctzl") == 0 ||
        strcmp(name, "__builtin_ctzll") == 0) {
        return BUILTIN_CTZ;
    }
    if (strcmp(name, "__builtin_ffs") == 0 || strcmp(name, "__builtin_ffsl") == 0 ||
        strcmp(name, "__builtin_ffsll") == 0) {
        return BUILTIN_FFS;
    }
    if (strcmp(name, "__builtin_popcount") == 0 || strcmp(name, "__builtin_popcountl") == 0 ||
        strcmp(name, "__builtin_popcountll") == 0) {
        return BUILTIN_POPCOUNT;
    }
    if (strcmp(name, "__builtin_parity") == 0 || strcmp(name, "__builtin_parityl") == 0 ||
        strcmp(name, "__builtin_parityll") == 0) {
        return BUILTIN_PARITY;
    }
    if (strcmp(name, "__builtin_add_overflow") == 0) {
        return BUILTIN_ADD_OVERFLOW;
    }
    if (strcmp(name, "__builtin_sub_overflow") == 0) {
        return BUILTIN_SUB_OVERFLOW;
    }
    if (strcmp(name, "__builtin_mul_overflow") == 0) {
        return BUILTIN_MUL_OVERFLOW;
    }
    if (strcmp(name, "__builtin_add_overflow_p") == 0) {
        return BUILTIN_ADD_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_sub_overflow_p") == 0) {
        return BUILTIN_SUB_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_mul_overflow_p") == 0) {
        return BUILTIN_MUL_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_object_size") == 0) {
        return BUILTIN_OBJECT_SIZE;
    }
    if (strcmp(name, "__builtin_return_address") == 0) {
        return BUILTIN_RETURN_ADDRESS;
    }
    if (strcmp(name, "__builtin_frame_address") == 0) {
        return BUILTIN_FRAME_ADDRESS;
    }
    if (strcmp(name, "__builtin_memcmp") == 0) {
        return BUILTIN_MEMCMP;
    }
    if (strcmp(name, "__builtin_prefetch") == 0) {
        return BUILTIN_PREFETCH;
    }
    if (strcmp(name, "__builtin_cpu_supports") == 0) {
        return BUILTIN_CPU_SUPPORTS;
    }
    if (strcmp(name, "__builtin_strlen") == 0) {
        return BUILTIN_STRLEN;
    }
    if (strcmp(name, "__builtin_memcpy") == 0) {
        return BUILTIN_MEMCPY;
    }
    if (strcmp(name, "__builtin_memmove") == 0) {
        return BUILTIN_MEMMOVE;
    }
    if (strcmp(name, "__builtin_memset") == 0) {
        return BUILTIN_MEMSET;
    }
    if (strcmp(name, "__builtin___memcpy_chk") == 0) {
        return BUILTIN_MEMCPY_CHK;
    }
    if (strcmp(name, "__builtin___memmove_chk") == 0) {
        return BUILTIN_MEMMOVE_CHK;
    }
    if (strcmp(name, "__builtin___memset_chk") == 0) {
        return BUILTIN_MEMSET_CHK;
    }
    if (strcmp(name, "__builtin_huge_val") == 0) {
        return BUILTIN_HUGE_VAL;
    }
    if (strcmp(name, "__builtin_huge_valf") == 0) {
        return BUILTIN_HUGE_VALF;
    }
    if (strcmp(name, "__builtin_huge_vall") == 0) {
        return BUILTIN_HUGE_VALL;
    }
    if (strcmp(name, "__builtin_inf") == 0) {
        return BUILTIN_INF;
    }
    if (strcmp(name, "__builtin_inff") == 0) {
        return BUILTIN_INFF;
    }
    if (strcmp(name, "__builtin_infl") == 0) {
        return BUILTIN_INFL;
    }
    if (strcmp(name, "__builtin_ldbl_min") == 0) {
        return BUILTIN_LDBL_MIN;
    }
    if (strcmp(name, "__builtin_ldbl_denorm_min") == 0) {
        return BUILTIN_LDBL_DENORM_MIN;
    }
    if (strcmp(name, "__builtin_ldbl_max") == 0) {
        return BUILTIN_LDBL_MAX;
    }
    if (strcmp(name, "__builtin_isfinite") == 0) {
        return BUILTIN_ISFINITE;
    }
    if (strcmp(name, "__builtin_isinf") == 0 || strcmp(name, "__builtin_isinff") == 0 ||
        strcmp(name, "__builtin_isinfl") == 0) {
        return BUILTIN_ISINF;
    }
    if (strcmp(name, "__builtin_isinf_sign") == 0) {
        return BUILTIN_ISINF_SIGN;
    }
    if (strcmp(name, "__builtin_isnan") == 0 || strcmp(name, "__builtin_isnanf") == 0 ||
        strcmp(name, "__builtin_isnanl") == 0) {
        return BUILTIN_ISNAN;
    }
    if (strcmp(name, "__builtin_signbit") == 0 || strcmp(name, "__builtin_signbitf") == 0 ||
        strcmp(name, "__builtin_signbitl") == 0) {
        return BUILTIN_SIGNBIT;
    }
    if (strcmp(name, "__builtin_nanf") == 0 || strcmp(name, "__builtin_nansf") == 0) {
        return BUILTIN_NANF;
    }
    if (strcmp(name, "__builtin_nan") == 0 || strcmp(name, "__builtin_nans") == 0) {
        return BUILTIN_NAN;
    }
    if (strcmp(name, "__builtin_nanl") == 0 || strcmp(name, "__builtin_nansl") == 0) {
        return BUILTIN_NANL;
    }
    if (strcmp(name, "__sync_fetch_and_add") == 0) {
        return BUILTIN_SYNC_FETCH_ADD;
    }
    if (strcmp(name, "__sync_fetch_and_sub") == 0) {
        return BUILTIN_SYNC_FETCH_SUB;
    }
    if (strcmp(name, "__sync_sub_and_fetch") == 0) {
        return BUILTIN_SYNC_SUB_AND_FETCH;
    }
    if (strcmp(name, "__sync_bool_compare_and_swap") == 0) {
        return BUILTIN_SYNC_BOOL_CAS;
    }
    if (strcmp(name, "__sync_lock_test_and_set") == 0) {
        return BUILTIN_SYNC_LOCK_TEST_AND_SET;
    }
    if (strcmp(name, "__sync_lock_release") == 0) {
        return BUILTIN_SYNC_LOCK_RELEASE;
    }
    if (strcmp(name, "__sync_synchronize") == 0) {
        return BUILTIN_SYNC_SYNCHRONIZE;
    }
    if (strcmp(name, "__atomic_fetch_add") == 0) {
        return BUILTIN_ATOMIC_FETCH_ADD;
    }
    if (strcmp(name, "__atomic_fetch_sub") == 0) {
        return BUILTIN_ATOMIC_FETCH_SUB;
    }
    if (strcmp(name, "__atomic_exchange_n") == 0) {
        return BUILTIN_ATOMIC_EXCHANGE_N;
    }
    if (strcmp(name, "__atomic_load_n") == 0) {
        return BUILTIN_ATOMIC_LOAD_N;
    }
    if (strcmp(name, "__atomic_store_n") == 0) {
        return BUILTIN_ATOMIC_STORE_N;
    }
    if (strcmp(name, "__atomic_thread_fence") == 0) {
        return BUILTIN_ATOMIC_THREAD_FENCE;
    }
    if (strcmp(name, "__atomic_signal_fence") == 0) {
        return BUILTIN_ATOMIC_SIGNAL_FENCE;
    }
    return BUILTIN_NONE;
}

static int atomic_memorder_is_valid(long mo) {
    return mo >= CC_ATOMIC_RELAXED && mo <= CC_ATOMIC_SEQ_CST;
}

static int atomic_memorder_valid_for_load(long mo) {
    return mo == CC_ATOMIC_RELAXED || mo == CC_ATOMIC_CONSUME || mo == CC_ATOMIC_ACQUIRE ||
           mo == CC_ATOMIC_SEQ_CST;
}

static int atomic_memorder_valid_for_store(long mo) {
    return mo == CC_ATOMIC_RELAXED || mo == CC_ATOMIC_RELEASE || mo == CC_ATOMIC_SEQ_CST;
}

static int atomic_memorder_check_arg(const cc_translation_unit_t *tu, cc_expr_t *arg, const char *builtin_name,
                                     const char *op_kind, int allow_load_orders, int allow_store_orders,
                                     cc_diag_t *diag) {
    long long mo = -1;
    char msg[192];

    if (arg == NULL) {
        snprintf(msg, sizeof(msg), "%s %s memory-order argument is missing", builtin_name, op_kind);
        sema_diag(diag, msg);
        return -1;
    }
    if (eval_const_int_expr(tu, arg, &mo) != 0) {
        snprintf(msg, sizeof(msg), "%s %s memory-order argument must be an integer constant expression", builtin_name,
                 op_kind);
        sema_diag(diag, msg);
        return -1;
    }
    if (!atomic_memorder_is_valid(mo)) {
        snprintf(msg, sizeof(msg), "%s %s memory-order argument must be in range [0,5]", builtin_name, op_kind);
        sema_diag(diag, msg);
        return -1;
    }
    if (allow_load_orders && !atomic_memorder_valid_for_load(mo)) {
        snprintf(msg, sizeof(msg), "%s load memory order must be relaxed/consume/acquire/seq_cst", builtin_name);
        sema_diag(diag, msg);
        return -1;
    }
    if (allow_store_orders && !atomic_memorder_valid_for_store(mo)) {
        snprintf(msg, sizeof(msg), "%s store memory order must be relaxed/release/seq_cst", builtin_name);
        sema_diag(diag, msg);
        return -1;
    }
    return 0;
}

/**
 * Check GNU __builtin_complex(re, im) and make it the CC_EXPR_COMPLEX it
 * stands for.  As in gcc, both arguments must have the same real floating
 * type.
 *
 * @return 0, or -1 with @p diag set
 */
static int sema_builtin_complex(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count,
                                int depth, cc_diag_t *diag) {
    cc_expr_t **args = e->args;

    if (e->arg_count != 2) {
        sema_diag(diag, "__builtin_complex takes two arguments");
        return -1;
    }
    if (check_expr(tu, args[0], vars, var_count, depth, diag) != 0 ||
        check_expr(tu, args[1], vars, var_count, depth, diag) != 0) {
        return -1;
    }
    if (sema_float_rank(sx_vtype(args[0])) == 0 || sx_vtype(args[0])->unqual != sx_vtype(args[1])->unqual) {
        sema_diag(diag, "both arguments to '__builtin_complex' must have the same real floating type");
        return -1;
    }
    free(e->ident);
    e->ident = NULL;
    e->kind = CC_EXPR_COMPLEX;
    e->lhs = args[0];
    e->rhs = args[1];
    e->args = NULL;
    e->arg_count = 0;
    free(args);
    return sema_check_complex_parts(tu, e, diag);
}

/**
 * The type of the object an atomic builtin's pointer argument @p ptr
 * points to; int for a `void *`, as gcc takes it.
 */
static const cc_ctype_t *sema_atomic_elem(const cc_expr_t *ptr) {
    const cc_ctype_t *t = sx_vtype(ptr)->of->unqual;

    return t->kind == CC_CT_VOID ? cc_ct_basic(CC_CT_INT) : t;
}

/**
 * Whether argument @p arg may be passed for a parameter of union type
 * @p sid as one of its members (GCC's transparent_union; a pointer is
 * accepted for any union whose member it converts to).
 *
 * @return 1 if it may, else 0
 */
static int sema_transparent_union_accepts(const cc_translation_unit_t *tu, int sid, const cc_expr_t *arg) {
    const cc_struct_def_t *sd = find_struct_def(tu, sid);
    size_t i;

    if (sd == NULL || !sd->complete || !sd->is_union ||
        ((sd->attr_flags & CC_ATTR_TRANSPARENT_UNION) == 0 && !sx_is_ptr(arg))) {
        return 0;
    }
    for (i = 0; i < sd->member_count; ++i) {
        if (sema_assignable(tu, sd->members[i].ctype, arg)) {
            return 1;
        }
    }
    return 0;
}

/**
 * Make a call argument's conversion explicit (C11 6.5.2.2p6-7): to the
 * parameter's type where a prototype gives one, else the default
 * argument promotions -- the integer promotions, and float to double.
 * The function's type comes from the declaration called or from the
 * pointer called through; an undeclared name (a builtin, or C89's
 * implicit declaration) converts nothing here.
 *
 * @param tu      the translation unit
 * @param e       the call, its arguments checked
 * @param callee  the function the call names, or NULL
 * @param i       the argument
 * @return        0, or -1 when out of memory
 */
static int sema_convert_call_arg(const cc_translation_unit_t *tu, cc_expr_t *e, const cc_function_t *callee,
                                 size_t i) {
    const cc_ctype_t *a = sx_vtype(e->args[i]);
    int prototyped;
    size_t nparams;
    const cc_ctype_t *param = NULL;

    if (callee != NULL) {
        prototyped = callee->has_prototype;
        nparams = callee->param_count;
        if (prototyped && i < nparams) {
            param = sema_param_ctype(&callee->params[i]);
        }
    } else {
        const cc_ctype_t *fp = e->lhs != NULL ? sx_vtype(e->lhs) : NULL;
        const cc_ctype_t *ft = fp != NULL && fp->unqual->kind == CC_CT_POINTER ? fp->unqual->of : NULL;

        if (ft == NULL || ft->kind != CC_CT_FUNCTION) {
            return 0;
        }
        prototyped = ft->prototyped;
        nparams = ft->param_count;
        if (prototyped && i < nparams) {
            param = ft->params[i];
        }
    }
    if (param != NULL) {
        return sema_complex_convert_for_assign(tu, &e->args[i], param->unqual) < 0 ? -1 : 0;
    }
    if (a == NULL) {
        return 0;
    }
    if (a->unqual->kind == CC_CT_FLOAT) {
        return sema_convert(&e->args[i], cc_ct_basic(CC_CT_DOUBLE));
    }
    if (cc_ct_is_integer(a)) {
        return sema_convert(&e->args[i], cc_ct_promote(a->unqual));
    }
    return 0;
}

/**
 * Check a call of a builtin the compiler implements itself: its operands
 * and its type.
 *
 * @param tu          the translation unit
 * @param e           the call
 * @param vars        the variables in scope
 * @param var_count   their number
 * @param depth       the scope depth
 * @param bk          which builtin it names, BUILTIN_NONE for none
 * @param bswap_bits  the width of a __builtin_bswapN it names, else 0
 * @param callee      the function its name declares, or NULL
 * @param diag        receives an error
 * @return            0, -1 with @p diag set, or SEMA_NOT_BUILTIN
 */
static int sema_check_builtin_call(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars,
                                   size_t var_count, int depth, builtin_kind_t bk, int bswap_bits,
                                   const cc_function_t *callee, cc_diag_t *diag) {
    size_t i;
    const cc_ctype_t *atomic_elem;

    if (bk == BUILTIN_VA_START) {
        if (e->arg_count != 2) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "__builtin_va_start expects exactly 2 arguments");
            }
            return -1;
        }
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__builtin_va_start first argument must be a pointer");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_VA_END) {
        if (e->arg_count != 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "__builtin_va_end expects exactly 1 argument");
            }
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__builtin_va_end argument must be a pointer");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_HUGE_VAL || bk == BUILTIN_HUGE_VALF || bk == BUILTIN_HUGE_VALL ||
        bk == BUILTIN_INF || bk == BUILTIN_INFF || bk == BUILTIN_INFL) {
        if (e->arg_count != 0) {
            sema_diag(diag, "floating huge-value/infinity builtin expects no arguments");
            return -1;
        }
        sema_set_type(e, cc_ct_basic((bk == BUILTIN_HUGE_VALF || bk == BUILTIN_INFF)   ? CC_CT_FLOAT
                                     : (bk == BUILTIN_HUGE_VALL || bk == BUILTIN_INFL) ? CC_CT_LDOUBLE
                                                                                       : CC_CT_DOUBLE));
        return 0;
    }
    if (bk == BUILTIN_LDBL_MIN || bk == BUILTIN_LDBL_DENORM_MIN || bk == BUILTIN_LDBL_MAX) {
        if (e->arg_count != 0) {
            sema_diag(diag, "long double limit builtin expects no arguments");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_LDOUBLE));
        return 0;
    }
    if (bk == BUILTIN_ISFINITE || bk == BUILTIN_ISINF || bk == BUILTIN_ISINF_SIGN || bk == BUILTIN_ISNAN ||
        bk == BUILTIN_SIGNBIT) {
        if (e->arg_count != 1) {
            sema_diag(diag, "floating classification builtin expects exactly 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_arith(e->args[0])) {
            sema_diag(diag, "floating classification builtin expects numeric argument");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_NANF || bk == BUILTIN_NAN || bk == BUILTIN_NANL) {
        if (e->arg_count != 1) {
            sema_diag(diag, "floating NaN builtin expects exactly 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        sema_set_type(e, cc_ct_basic(bk == BUILTIN_NANF   ? CC_CT_FLOAT
                                     : bk == BUILTIN_NANL ? CC_CT_LDOUBLE
                                                          : CC_CT_DOUBLE));
        return 0;
    }
    if (bk == BUILTIN_VA_COPY) {
        if (e->arg_count != 2) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "__builtin_va_copy expects exactly 2 arguments");
            }
            return -1;
        }
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
            if (!sx_is_ptr(e->args[i])) {
                sema_diag(diag, "__builtin_va_copy arguments must be pointers");
                return -1;
            }
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_VA_ARG) {
        if (e->arg_count != 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "__builtin_va_arg expects va_list and a type");
            }
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__builtin_va_arg first operand must be a pointer");
            return -1;
        }
        if (e->ctype == NULL || e->ctype->unqual->kind == CC_CT_VOID) {
            sema_diag(diag, "__builtin_va_arg requires a non-void type");
            return -1;
        }
        sema_set_type(e, e->ctype);
        return 0;
    }
    if (bk == BUILTIN_EXPECT) {
        if (e->arg_count != 2) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "__builtin_expect expects exactly 2 arguments");
            }
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0 ||
            check_expr(tu, e->args[1], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_scalar(e->args[0]) || !sx_is_int(e->args[1])) {
            sema_diag(diag, "__builtin_expect expects (scalar, integer)");
            return -1;
        }
        sema_set_type(e, sx_vtype(e->args[0]));
        return 0;
    }
    if (bk == BUILTIN_CONSTANT_P) {
        if (e->arg_count != 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "__builtin_constant_p expects exactly 1 argument");
            }
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_TRAP) {
        if (e->arg_count != 0) {
            sema_diag(diag, "__builtin_trap expects exactly 0 arguments");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_UNREACHABLE) {
        if (e->arg_count != 0) {
            sema_diag(diag, "__builtin_unreachable expects exactly 0 arguments");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_ASSUME) {
        if (e->arg_count != 1) {
            sema_diag(diag, "__builtin_assume expects exactly 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_scalar(e->args[0])) {
            sema_diag(diag, "__builtin_assume argument must be scalar");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_ASSUME_ALIGNED) {
        if (e->arg_count < 2 || e->arg_count > 3) {
            sema_diag(diag, "__builtin_assume_aligned expects 2 or 3 arguments");
            return -1;
        }
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__builtin_assume_aligned first argument must be a pointer");
            return -1;
        }
        if (!sx_is_int(e->args[1]) ||
            (e->arg_count == 3 && !sx_is_int(e->args[2]))) {
            sema_diag(diag, "__builtin_assume_aligned alignment/offset must be integral");
            return -1;
        }
        sema_set_type(e, sx_vtype(e->args[0]));
        return 0;
    }
    if (bk == BUILTIN_UNPREDICTABLE) {
        if (e->arg_count != 1) {
            sema_diag(diag, "__builtin_unpredictable expects exactly 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_scalar(e->args[0])) {
            sema_diag(diag, "__builtin_unpredictable argument must be scalar");
            return -1;
        }
        sema_set_type(e, sx_vtype(e->args[0]));
        return 0;
    }
    if (bk == BUILTIN_CLZ || bk == BUILTIN_CTZ || bk == BUILTIN_FFS) {
        const char *builtin_name = "__builtin_clz";
        if (bk == BUILTIN_CTZ) {
            builtin_name = "__builtin_ctz";
        } else if (bk == BUILTIN_FFS) {
            builtin_name = "__builtin_ffs";
        }
        if (e->arg_count != 1) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s expects exactly 1 argument", builtin_name);
            sema_diag(diag, msg);
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_int(e->args[0])) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s argument must be integral", builtin_name);
            sema_diag(diag, msg);
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_POPCOUNT || bk == BUILTIN_PARITY) {
        const char *what = bk == BUILTIN_PARITY ? "__builtin_parity" : "__builtin_popcount";
        char msg[96];
        if (e->arg_count != 1) {
            snprintf(msg, sizeof(msg), "%s expects exactly 1 argument", what);
            sema_diag(diag, msg);
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_int(e->args[0])) {
            snprintf(msg, sizeof(msg), "%s argument must be integral", what);
            sema_diag(diag, msg);
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_ADD_OVERFLOW || bk == BUILTIN_SUB_OVERFLOW || bk == BUILTIN_MUL_OVERFLOW) {
        const cc_ctype_t *out_elem;
        if (e->arg_count != 3) {
            sema_diag(diag, "__builtin_*_overflow expects exactly 3 arguments");
            return -1;
        }
        for (i = 0; i < 3; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_ptr(e->args[2])) {
            sema_diag(diag, "overflow builtin third argument must be a pointer");
            return -1;
        }
        out_elem = sx_vtype(e->args[2])->of;
        if (!cc_ct_is_integer(out_elem)) {
            sema_diag(diag, "overflow builtin destination must point to an integer type");
            return -1;
        }
        if (!sx_is_int(e->args[0]) || !sx_is_int(e->args[1])) {
            sema_diag(diag, "overflow builtin operands must be integers");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_ADD_OVERFLOW_P || bk == BUILTIN_SUB_OVERFLOW_P || bk == BUILTIN_MUL_OVERFLOW_P) {
        if (e->arg_count != 3) {
            sema_diag(diag, "__builtin_*_overflow_p expects exactly 3 arguments");
            return -1;
        }
        for (i = 0; i < 3; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_int(e->args[0]) || !sx_is_int(e->args[1]) ||
            !sx_is_int(e->args[2])) {
            sema_diag(diag, "overflow_p builtin operands must be integral");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_OBJECT_SIZE) {
        if (e->arg_count != 2) {
            sema_diag(diag, "__builtin_object_size expects exactly 2 arguments");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0 ||
            check_expr(tu, e->args[1], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_int(e->args[1])) {
            sema_diag(diag, "__builtin_object_size second argument must be integral");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_ULLONG));
        return 0;
    }
    if (bk == BUILTIN_RETURN_ADDRESS || bk == BUILTIN_FRAME_ADDRESS) {
        if (e->arg_count != 1) {
            sema_diag(diag, bk == BUILTIN_RETURN_ADDRESS ? "__builtin_return_address expects 1 argument"
                                                        : "__builtin_frame_address expects 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_int(e->args[0])) {
            sema_diag(diag, bk == BUILTIN_RETURN_ADDRESS ? "__builtin_return_address argument must be integral"
                                                        : "__builtin_frame_address argument must be integral");
            return -1;
        }
        sema_set_type(e, cc_ct_pointer(cc_ct_basic(CC_CT_VOID)));
        return 0;
    }
    if (bk == BUILTIN_MEMCMP) {
        if (e->arg_count != 3) {
            sema_diag(diag, "__builtin_memcmp has wrong argument count");
            return -1;
        }
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_ptr(e->args[0]) || !sx_is_ptr(e->args[1])) {
            sema_diag(diag, "__builtin_memcmp first two arguments must be pointers");
            return -1;
        }
        if (!sx_is_int(e->args[2])) {
            sema_diag(diag, "__builtin_memcmp third argument must be integral");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_PREFETCH) {
        if (e->arg_count < 1 || e->arg_count > 3) {
            sema_diag(diag, "__builtin_prefetch expects 1 to 3 arguments");
            return -1;
        }
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__builtin_prefetch first argument must be a pointer");
            return -1;
        }
        if (e->arg_count >= 2 && !sx_is_int(e->args[1])) {
            sema_diag(diag, "__builtin_prefetch rw argument must be integral");
            return -1;
        }
        if (e->arg_count >= 3 && !sx_is_int(e->args[2])) {
            sema_diag(diag, "__builtin_prefetch locality argument must be integral");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_CPU_SUPPORTS) {
        if (e->arg_count != 1) {
            sema_diag(diag, "__builtin_cpu_supports expects exactly 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (e->args[0]->kind != CC_EXPR_STR) {
            sema_diag(diag, "__builtin_cpu_supports argument must be a string literal");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_STRLEN) {
        if (e->arg_count != 1) {
            sema_diag(diag, "__builtin_strlen expects exactly 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__builtin_strlen argument must be a pointer");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(cc_target_pointer_size() >= 8 ? CC_CT_ULONG : CC_CT_UINT));
        return 0;
    }
    if (bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMSET || bk == BUILTIN_MEMCPY_CHK ||
        bk == BUILTIN_MEMMOVE_CHK || bk == BUILTIN_MEMSET_CHK) {
        size_t min_args = (bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMSET) ? 3 : 4;
        if (e->arg_count < min_args) {
            sema_diag(diag, min_args == 3 ? "__builtin_mem* has wrong argument count"
                                         : "__builtin___mem*_chk has wrong argument count");
            return -1;
        }
        for (i = 0; i < e->arg_count; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "memory builtin first argument must be a pointer");
            return -1;
        }
        if ((bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMCPY_CHK ||
             bk == BUILTIN_MEMMOVE_CHK) &&
            !sx_is_ptr(e->args[1])) {
            sema_diag(diag, "memory builtin source argument must be a pointer");
            return -1;
        }
        if ((bk == BUILTIN_MEMSET || bk == BUILTIN_MEMSET_CHK) && !sx_is_int(e->args[1])) {
            sema_diag(diag, "memory builtin fill byte must be integral");
            return -1;
        }
        if (!sx_is_int(e->args[2])) {
            sema_diag(diag, "memory builtin size argument must be integral");
            return -1;
        }
        sema_set_type(e, sx_vtype(e->args[0]));
        return 0;
    }
    if (bk == BUILTIN_SYNC_SYNCHRONIZE) {
        if (e->arg_count != 0) {
            sema_diag(diag, "__sync_synchronize expects exactly 0 arguments");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_ATOMIC_THREAD_FENCE || bk == BUILTIN_ATOMIC_SIGNAL_FENCE) {
        const char *nm = bk == BUILTIN_ATOMIC_THREAD_FENCE ? "__atomic_thread_fence" : "__atomic_signal_fence";
        if (e->arg_count != 1) {
            sema_diag(diag, bk == BUILTIN_ATOMIC_THREAD_FENCE ? "__atomic_thread_fence expects exactly 1 argument"
                                                              : "__atomic_signal_fence expects exactly 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_int(e->args[0])) {
            sema_diag(diag, bk == BUILTIN_ATOMIC_THREAD_FENCE
                               ? "__atomic_thread_fence memory-order argument must be integral"
                               : "__atomic_signal_fence memory-order argument must be integral");
            return -1;
        }
        if (atomic_memorder_check_arg(tu, e->args[0], nm, "fence", 0, 0, diag) != 0) {
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_SYNC_LOCK_RELEASE) {
        if (e->arg_count != 1) {
            sema_diag(diag, "__sync_lock_release expects exactly 1 argument");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__sync_lock_release first argument must be a pointer");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (bk == BUILTIN_SYNC_FETCH_ADD || bk == BUILTIN_SYNC_FETCH_SUB || bk == BUILTIN_SYNC_SUB_AND_FETCH ||
        bk == BUILTIN_SYNC_LOCK_TEST_AND_SET || bk == BUILTIN_ATOMIC_FETCH_ADD ||
        bk == BUILTIN_ATOMIC_FETCH_SUB || bk == BUILTIN_ATOMIC_EXCHANGE_N) {
        int expect_argc = (bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB ||
                           bk == BUILTIN_ATOMIC_EXCHANGE_N)
                              ? 3
                              : 2;
        if (e->arg_count != (size_t)expect_argc) {
            sema_diag(diag, "atomic fetch/exchange builtin has wrong argument count");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0 ||
            check_expr(tu, e->args[1], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if ((bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB ||
             bk == BUILTIN_ATOMIC_EXCHANGE_N) &&
            check_expr(tu, e->args[2], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "atomic fetch/exchange first argument must be a pointer");
            return -1;
        }
        atomic_elem = sema_atomic_elem(e->args[0]);
        if (!sema_assignable(tu, atomic_elem, e->args[1])) {
            sema_diag(diag, "atomic fetch/exchange value type mismatch");
            return -1;
        }
        if (bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB || bk == BUILTIN_ATOMIC_EXCHANGE_N) {
            const char *nm = bk == BUILTIN_ATOMIC_EXCHANGE_N
                                 ? "__atomic_exchange_n"
                                 : (bk == BUILTIN_ATOMIC_FETCH_ADD ? "__atomic_fetch_add" : "__atomic_fetch_sub");
            if (!sx_is_int(e->args[2])) {
                sema_diag(diag, "atomic fetch/exchange memory-order argument must be integral");
                return -1;
            }
            if (atomic_memorder_check_arg(tu, e->args[2], nm, "operation", 0, 0, diag) != 0) {
                return -1;
            }
        }
        sema_set_type(e, atomic_elem);
        return 0;
    }
    if (bk == BUILTIN_SYNC_BOOL_CAS) {
        if (e->arg_count != 3) {
            sema_diag(diag, "__sync_bool_compare_and_swap expects exactly 3 arguments");
            return -1;
        }
        for (i = 0; i < 3; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__sync_bool_compare_and_swap first argument must be a pointer");
            return -1;
        }
        atomic_elem = sema_atomic_elem(e->args[0]);
        if (!sema_assignable(tu, atomic_elem, e->args[1]) || !sema_assignable(tu, atomic_elem, e->args[2])) {
            sema_diag(diag, "__sync_bool_compare_and_swap value type mismatch");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
        return 0;
    }
    if (bk == BUILTIN_ATOMIC_LOAD_N) {
        if (e->arg_count != 2) {
            sema_diag(diag, "__atomic_load_n expects exactly 2 arguments");
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0 ||
            check_expr(tu, e->args[1], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__atomic_load_n first argument must be a pointer");
            return -1;
        }
        atomic_elem = sema_atomic_elem(e->args[0]);
        if (!sx_is_int(e->args[1])) {
            sema_diag(diag, "__atomic_load_n memory-order argument must be integral");
            return -1;
        }
        if (atomic_memorder_check_arg(tu, e->args[1], "__atomic_load_n", "load", 1, 0, diag) != 0) {
            return -1;
        }
        sema_set_type(e, atomic_elem);
        return 0;
    }
    if (bk == BUILTIN_ATOMIC_STORE_N) {
        if (e->arg_count != 3) {
            sema_diag(diag, "__atomic_store_n expects exactly 3 arguments");
            return -1;
        }
        for (i = 0; i < 3; ++i) {
            if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
                return -1;
            }
        }
        if (!sx_is_ptr(e->args[0])) {
            sema_diag(diag, "__atomic_store_n first argument must be a pointer");
            return -1;
        }
        atomic_elem = sema_atomic_elem(e->args[0]);
        if (!sema_assignable(tu, atomic_elem, e->args[1])) {
            sema_diag(diag, "__atomic_store_n value type mismatch");
            return -1;
        }
        if (!sx_is_int(e->args[2])) {
            sema_diag(diag, "__atomic_store_n memory-order argument must be integral");
            return -1;
        }
        if (atomic_memorder_check_arg(tu, e->args[2], "__atomic_store_n", "store", 0, 1, diag) != 0) {
            return -1;
        }
        sema_set_type(e, cc_ct_basic(CC_CT_VOID));
        return 0;
    }
    if (e->ident != NULL && callee == NULL && bswap_bits != 0) {
        if (e->arg_count != 1) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "%s expects exactly 1 argument", e->ident);
            }
            return -1;
        }
        if (check_expr(tu, e->args[0], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (!sx_is_int(e->args[0])) {
            sema_diag(diag, "byte-swap builtin requires integral argument");
            return -1;
        }
        sema_set_type(e, cc_ct_basic(bswap_bits == 16 ? CC_CT_USHORT : bswap_bits == 32 ? CC_CT_UINT
                                                                                      : CC_CT_ULLONG));
        return 0;
    }
    return SEMA_NOT_BUILTIN;
}

/**
 * Check a call: a builtin's operands and type, or the callee (a function,
 * a function pointer, an old-style declaration), its arguments converted
 * to its parameters (the default promotions past them), and the result type.
 *
 * @param tu         the translation unit
 * @param e          the call
 * @param vars       the variables in scope
 * @param var_count  their number
 * @param depth      the scope depth
 * @param diag       receives an error
 * @return           0, or -1 with @p diag set
 */
int sema_check_call(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count,
                           int depth, cc_diag_t *diag) {
    size_t i;
    const cc_function_t *callee = NULL;
    builtin_kind_t bk = builtin_kind(e->ident);
    int bswap_bits = builtin_bswap_bits(e->ident);
    if (e->ident != NULL && strcmp(e->ident, "__builtin_complex") == 0) {
        return sema_builtin_complex(tu, e, vars, var_count, depth, diag);
    }
    if (e->ident != NULL) {
        callee = find_function(tu, e->ident);
    }
    {
        int rc = sema_check_builtin_call(tu, e, vars, var_count, depth, bk, bswap_bits, callee, diag);

        if (rc != SEMA_NOT_BUILTIN) {
            return rc;
        }
    }
    if (e->ident == NULL) {
        if (e->lhs == NULL) {
            sema_diag(diag, "malformed call expression");
            return -1;
        }
        if (check_expr(tu, e->lhs, vars, var_count, depth, diag) != 0) {
            return -1;
        }
        /* a function designator, *fp included, is used as a pointer to the function */
        if (!sx_is_ptr(e->lhs)) {
            sema_diag(diag, "call target must be a function pointer");
            return -1;
        }
    }
    if (callee != NULL && callee->has_prototype && !callee->is_variadic && e->arg_count != callee->param_count) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "call to %s has %zu args but %zu required", e->ident,
                     e->arg_count, callee->param_count);
        }
        return -1;
    }
    if (callee != NULL && callee->has_prototype && callee->is_variadic && e->arg_count < callee->param_count) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message),
                     "variadic call to %s has %zu args but needs at least %zu", e->ident, e->arg_count,
                     callee->param_count);
        }
        return -1;
    }
    for (i = 0; i < e->arg_count; ++i) {
        if (check_expr(tu, e->args[i], vars, var_count, depth, diag) != 0) {
            return -1;
        }
        if (callee != NULL && callee->has_prototype && i < callee->param_count &&
            sema_complex_convert_for_assign(tu, &e->args[i], sema_param_ctype(&callee->params[i])) < 0) {
            sema_diag(diag, "out of memory");
            return -1;
        }
        if (sema_convert_call_arg(tu, e, callee, i) != 0) {
            sema_diag(diag, "out of memory");
            return -1;
        }
        if (callee != NULL && callee->has_prototype && i < callee->param_count &&
            !sema_assignable(tu, sema_param_ctype(&callee->params[i]), e->args[i]) &&
            !sema_transparent_union_accepts(tu, sx_ct_record(sema_param_ctype(&callee->params[i])),
                                            e->args[i])) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "cannot convert arg %zu in call to %s", i + 1,
                         e->ident);
            }
            return -1;
        }
    }
    if (e->ident != NULL) {
        size_t fmt_i;
        if (printf_like_format_index(e->ident, &fmt_i) && fmt_i < e->arg_count && e->args[fmt_i] != NULL &&
            e->args[fmt_i]->kind == CC_EXPR_STR && e->args[fmt_i]->ident != NULL) {
            char *rw = rewrite_printf_long_double_format(e->args[fmt_i]->ident);
            if (rw != NULL) {
                free(e->args[fmt_i]->ident);
                e->args[fmt_i]->ident = rw;
            }
        }
    }
    if (callee != NULL && callee->has_prototype && (callee->attr_flags & CC_ATTR_NONNULL) != 0) {
        char buf[192];
        /*
         * We currently record only the presence of nonnull, not its index list.
         * Avoid false positives (e.g. strtok uses nonnull(2)) by checking
         * only unambiguous single-parameter prototypes.
         */
        if (e->arg_count > 0 && callee->param_count == 1 && sema_is_ptr(sema_param_ctype(&callee->params[0])) &&
            is_null_ptr_constant(e->args[0])) {
            snprintf(buf, sizeof(buf), "nonnull argument 1 in call to %s is null",
                     e->ident != NULL ? e->ident : "<indirect>");
            if (emit_required_warning(diag, e->line, e->col, buf) != 0) {
                return -1;
            }
        }
    }
    if (callee != NULL) {
        sema_set_type(e, callee->ret_ctype);
        if ((callee->attr_flags & CC_ATTR_DEPRECATED) != 0) {
            char buf[192];
            snprintf(buf, sizeof(buf), "deprecated function used: %s", callee->name != NULL ? callee->name : "<anon>");
            if (emit_required_warning(diag, e->line, e->col, buf) != 0) {
                return -1;
            }
        }
    } else if (e->ident != NULL) {
        int idx = vars_find_visible(vars, var_count, e->ident, depth);
        const cc_global_t *g = idx >= 0 ? NULL : sema_find_global(tu, e->ident);

        if (idx >= 0 || g != NULL) {
            /* a call through a pointer object named directly */
            const cc_ctype_t *ft = idx >= 0 ? vars[idx].ctype : g->ctype;

            if (!sema_is_ptr(cc_ct_decay(ft))) {
                sema_diag(diag, "call target must be a function pointer");
                return -1;
            }
            sema_set_type(e, sema_callee_ret_ctype(ft));
            return 0;
        }
        if (strncmp(e->ident, "__builtin_", 10) == 0 && !cc_builtin_is_recognized(e->ident)) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "unsupported builtin function: %s", e->ident);
            }
            return -1;
        }
        if (strncmp(e->ident, "__builtin_", 10) != 0 && !cc_options_implicit_funcdecl()) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "implicit function declaration is not allowed in this mode: %s", e->ident);
            }
            return -1;
        }
        /* a builtin with no rule of its own, or C89's undeclared function: extern int f(...) */
        sema_set_type(e, cc_ct_basic(CC_CT_INT));
    } else {
        const cc_function_t *fn = generic_selected_function(tu, e->lhs);

        if (fn == NULL) {
            fn = infer_callee_function_expr(tu, e->lhs);
        }
        sema_set_type(e, fn != NULL ? fn->ret_ctype : sema_callee_ret_ctype(e->lhs->ctype));
    }
    return 0;
}
