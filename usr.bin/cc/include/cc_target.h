#ifndef CC_TARGET_H
#define CC_TARGET_H

/** The machines cc writes code for. */
typedef enum {
    CC_TARGET_X86_64 = 0,
    CC_TARGET_I386
} cc_target_t;

/**
 * The size and alignment of one kind of scalar object on a target.
 * Alignment is the alignment the psABI gives the type as a structure
 * member, which is also what gcc reports for `_Alignof`.
 */
typedef struct {
    unsigned char size;  /**< sizeof, in bytes */
    unsigned char align; /**< alignment, in bytes */
} cc_target_scalar_layout_t;

/**
 * What the compiler must know about a target's data: the C type
 * sizes and alignments the System V psABI for that machine fixes.
 */
/**
 * A standard integer type, as a target's typedefs (size_t, int64_t, ...)
 * name one.  Each unsigned type follows its signed one.
 */
typedef enum {
    CC_TGT_INT = 0,
    CC_TGT_UINT,
    CC_TGT_LONG,
    CC_TGT_ULONG,
    CC_TGT_LLONG,
    CC_TGT_ULLONG
} cc_target_int_t;

typedef struct {
    cc_target_t id;                               /**< which target this is */
    const char *name;                             /**< "x86_64" or "i386" */
    int pointer_size;                             /**< pointers, `long`, `size_t` */
    cc_target_scalar_layout_t int64;              /**< `long long` */
    cc_target_scalar_layout_t float64;            /**< `double` */
    cc_target_scalar_layout_t long_double;        /**< x87 extended precision */
    cc_target_scalar_layout_t decimal64;         /**< `_Decimal64` */
    cc_target_scalar_layout_t decimal128;         /**< `_Decimal128` */
    cc_target_int_t size_type;                    /**< `size_t` */
    cc_target_int_t ptrdiff_type;                 /**< `ptrdiff_t` and `intptr_t` */
    cc_target_int_t wchar_type;                   /**< `wchar_t` */
    cc_target_int_t int64_type;                   /**< `int64_t` and `intmax_t` */
    cc_target_int_t int_fast16_type;              /**< `int_fast16_t` and `int_fast32_t` */
    int has_int128;                               /**< `__int128` exists */
    int sse_fp;                                   /**< floating point is always SSE, never x87 */
    const char *const *predefined;                /**< the target's own macros, each defined to 1; NULL-ended */
} cc_target_desc_t;

/**
 * The size of standard integer type @p t on target @p tgt.
 *
 * @return 4 or 8
 */
int cc_target_int_size(const cc_target_desc_t *tgt, cc_target_int_t t);

/** Whether standard integer type @p t is unsigned. */
static inline int cc_target_int_is_unsigned(cc_target_int_t t) {
    return (int)t % 2 == 1;
}

/** The unsigned type of the same rank as @p t. */
static inline cc_target_int_t cc_target_int_unsigned(cc_target_int_t t) {
    return (cc_target_int_t)((int)t | 1);
}

/**
 * Return the description of a target.
 *
 * @param id  the target
 * @return    its description; never NULL (an unknown id gives x86-64)
 */
const cc_target_desc_t *cc_target_get(cc_target_t id);

/**
 * Make @p id the target the compilation in progress is for.  Every
 * layout question asked of cc_target_current() is answered for it.
 *
 * @param id  the target to compile for
 */
void cc_target_select(cc_target_t id);

/**
 * Return the target the compilation in progress is for: x86-64 until
 * cc_target_select() says otherwise.
 *
 * @return the selected target's description; never NULL
 */
const cc_target_desc_t *cc_target_current(void);

/**
 * The size in bytes of a pointer (and of `long` and `size_t`) on the
 * target being compiled for.
 *
 * @return 4 or 8
 */
int cc_target_pointer_size(void);

#endif
