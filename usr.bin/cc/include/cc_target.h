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
typedef struct {
    cc_target_t id;                               /**< which target this is */
    const char *name;                             /**< "x86_64" or "i386" */
    int pointer_size;                             /**< pointers, `long`, `size_t` */
    cc_target_scalar_layout_t int64;              /**< `long long` */
    cc_target_scalar_layout_t float64;            /**< `double` */
    cc_target_scalar_layout_t long_double;        /**< x87 extended precision */
    cc_target_scalar_layout_t complex_double;     /**< `double _Complex` */
    cc_target_scalar_layout_t imaginary_double;   /**< `double _Imaginary` */
    cc_target_scalar_layout_t decimal64;          /**< `_Decimal64` */
    cc_target_scalar_layout_t decimal128;         /**< `_Decimal128` */
} cc_target_desc_t;

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

#endif
