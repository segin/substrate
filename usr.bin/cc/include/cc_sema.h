#ifndef CC_SEMA_H
#define CC_SEMA_H

/*
 * cc_sema.h: what the files of semantic analysis share (frontend/sema.c,
 * frontend/sema_call.c): the variable table, the builtins, and the checks
 * and conversions one file defines and the other uses.  Nothing outside
 * sema includes it.
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

/** A variable or parameter in scope. */
typedef struct {
    char *name;
    int storage;             /**< its CC_STORAGE_* flags */
    int depth;               /**< the scope depth it was declared at */
    const cc_ctype_t *ctype; /**< its declared type (cc_ctype.h) */
} var_entry_t;

/** A builtin function the compiler checks (and lowering expands) itself. */
typedef enum {
    BUILTIN_NONE = 0,
    BUILTIN_VA_START,
    BUILTIN_VA_END,
    BUILTIN_VA_COPY,
    BUILTIN_VA_ARG,
    BUILTIN_EXPECT,
    BUILTIN_CONSTANT_P,
    BUILTIN_TRAP,
    BUILTIN_UNREACHABLE,
    BUILTIN_ASSUME,
    BUILTIN_ASSUME_ALIGNED,
    BUILTIN_UNPREDICTABLE,
    BUILTIN_CLZ,
    BUILTIN_CTZ,
    BUILTIN_FFS,
    BUILTIN_POPCOUNT,
    BUILTIN_PARITY,
    BUILTIN_ADD_OVERFLOW,
    BUILTIN_SUB_OVERFLOW,
    BUILTIN_MUL_OVERFLOW,
    BUILTIN_ADD_OVERFLOW_P,
    BUILTIN_SUB_OVERFLOW_P,
    BUILTIN_MUL_OVERFLOW_P,
    BUILTIN_OBJECT_SIZE,
    BUILTIN_RETURN_ADDRESS,
    BUILTIN_FRAME_ADDRESS,
    BUILTIN_MEMCMP,
    BUILTIN_PREFETCH,
    BUILTIN_CPU_SUPPORTS,
    BUILTIN_STRLEN,
    BUILTIN_STRCMP,
    BUILTIN_MEMCPY,
    BUILTIN_MEMMOVE,
    BUILTIN_MEMSET,
    BUILTIN_MEMCPY_CHK,
    BUILTIN_MEMMOVE_CHK,
    BUILTIN_MEMSET_CHK,
    BUILTIN_HUGE_VAL,
    BUILTIN_HUGE_VALF,
    BUILTIN_HUGE_VALL,
    BUILTIN_INF,
    BUILTIN_INFF,
    BUILTIN_INFL,
    BUILTIN_LDBL_MIN,
    BUILTIN_LDBL_DENORM_MIN,
    BUILTIN_LDBL_MAX,
    BUILTIN_ISFINITE,
    BUILTIN_ISINF,
    BUILTIN_ISINF_SIGN,
    BUILTIN_ISNAN,
    BUILTIN_SIGNBIT,
    BUILTIN_NANF,
    BUILTIN_NAN,
    BUILTIN_NANL,
    BUILTIN_SYNC_FETCH_ADD,
    BUILTIN_SYNC_FETCH_SUB,
    BUILTIN_SYNC_SUB_AND_FETCH,
    BUILTIN_SYNC_BOOL_CAS,
    BUILTIN_SYNC_LOCK_TEST_AND_SET,
    BUILTIN_SYNC_LOCK_RELEASE,
    BUILTIN_SYNC_SYNCHRONIZE,
    BUILTIN_ATOMIC_FETCH_ADD,
    BUILTIN_ATOMIC_FETCH_SUB,
    BUILTIN_ATOMIC_EXCHANGE_N,
    BUILTIN_ATOMIC_LOAD_N,
    BUILTIN_ATOMIC_STORE_N,
    BUILTIN_ATOMIC_THREAD_FENCE,
    BUILTIN_ATOMIC_SIGNAL_FENCE
} builtin_kind_t;

/* sema.c */

/**
 * Record a semantic error.  The first error recorded is the one
 * reported; with no position given, it is placed at the construct being
 * checked (set_diag_context).
 *
 * @param d    the diagnostic
 * @param msg  the message
 */
void sema_diag(cc_diag_t *d, const char *msg);

/** Report a warning the language requires (an error with -pedantic-errors or -Werror); 0, or -1 if it is an error. */
int emit_required_warning(cc_diag_t *diag, size_t line, size_t col, const char *msg);

/** The innermost variable named @p name visible at scope @p depth: its index, or -1. */
int vars_find_visible(var_entry_t *vars, size_t count, const char *name, int depth);

/** The function named @p name, its definition before a declaration; NULL if none. */
const cc_function_t *find_function(const cc_translation_unit_t *tu, const char *name);

/** The type of parameter @p p as adjusted. */
const cc_ctype_t *sema_param_ctype(const cc_param_t *p);

/** The file-scope variable named @p name; NULL if none. */
const cc_global_t *sema_find_global(const cc_translation_unit_t *tu, const char *name);

/** Structure, union or hidden structure @p struct_id; NULL if there is none. */
const cc_struct_def_t *find_struct_def(const cc_translation_unit_t *tu, int struct_id);

/**
 * The type of checked expression @p e as a value: its interned type, an
 * array or a function decayed to a pointer.
 *
 * @param e  the expression, or NULL
 * @return   the type, or NULL
 */
const cc_ctype_t *sx_vtype(const cc_expr_t *e);

/** Whether checked expression @p x has a pointer value (an array or function decays to one). */
int sx_is_ptr(const cc_expr_t *x);

/** Whether checked expression @p x has an integer type. */
int sx_is_int(const cc_expr_t *x);

/** Whether checked expression @p x has a scalar type: arithmetic or pointer. */
int sx_is_scalar(const cc_expr_t *x);

/**
 * The structure a value of type @p t is: a structure or union, or the
 * hidden structure standing for a complex, wide, decimal or vector type.
 *
 * @param t  the type, or NULL
 * @return   the structure's index, or -1
 */
int sx_ct_record(const cc_ctype_t *t);

/** Whether @p e is a null pointer constant (C11 6.3.2.3). */
int is_null_ptr_constant(const cc_expr_t *e);

/**
 * Evaluate an integer constant expression in sema's context.
 *
 * @param tu   the translation unit
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int eval_const_int_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long long *out);

/**
 * The floating rank a type contributes to a complex operation's result:
 * 1 float, 2 double, 3 long double; 0 for an integer (C11 6.3.1.8: the
 * complex operand's element decides then).
 */
int sema_float_rank(const cc_ctype_t *t);

/**
 * Convert the value in @p slot to type @p t where it has another type:
 * an implicit conversion (C11 6.3) made an explicit cast, so lowering
 * converts only where the tree says to.
 *
 * @param slot  a checked expression
 * @param t     the type it is converted to
 * @return      0, or -1 when out of memory
 */
int sema_convert(cc_expr_t **slot, const cc_ctype_t *t);

/** Convert *@p slot for assignment to complex type @p t (a real operand becomes the real part); 0, or -1. */
int sema_complex_convert_for_assign(const cc_translation_unit_t *tu, cc_expr_t **slot, const cc_ctype_t *t);

/**
 * Check a complex value built from its parts (CC_EXPR_COMPLEX, checked
 * children): its type is _Complex of the parts' floating type, the wider
 * if they differ (an integer part counts as double), and each part is
 * converted to that.
 *
 * @return 0, or -1 with @p diag set
 */
int sema_check_complex_parts(const cc_translation_unit_t *tu, cc_expr_t *e, cc_diag_t *diag);

/**
 * Check expression @p e and give it its interned type (see
 * check_expr_kind()).
 *
 * @return 0, or -1 with @p diag set
 */
int check_expr(const cc_translation_unit_t *tu, cc_expr_t *e, var_entry_t *vars, size_t var_count, int depth,
               cc_diag_t *diag);

/**
 * Give expression @p e type @p t.
 *
 * @param e  the expression
 * @param t  its type
 */
void sema_set_type(cc_expr_t *e, const cc_ctype_t *t);

/** Whether @p t (a value's type) is a pointer. */
int sema_is_ptr(const cc_ctype_t *t);

/**
 * Whether a value @p rhs may be assigned to an object of type @p dst (C11
 * 6.5.16.1), as leniently as the checker has always allowed: between
 * arithmetic types, between pointers that match (sema_ptrs_match()),
 * between a pointer and an integer, between structures of one type, and
 * an integer statement expression to a structure.
 *
 * @param tu   the translation unit
 * @param dst  the object's type
 * @param rhs  the value, checked
 * @return     1 if it may, else 0
 */
int sema_assignable(const cc_translation_unit_t *tu, const cc_ctype_t *dst, const cc_expr_t *rhs);

/* sema_call.c */

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
                    int depth, cc_diag_t *diag);

#endif
