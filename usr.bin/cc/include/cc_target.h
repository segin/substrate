#ifndef CC_TARGET_H
#define CC_TARGET_H

#include <stddef.h>

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

struct cc_translation_unit;
struct cc_abi_regs;
struct cc_abi_va_type;
struct cc_ssa_function;
struct cc_diag;

/** What one store of a trampoline writes. */
typedef enum {
    CC_TRAMP_END = 0, /**< ends the list */
    CC_TRAMP_CONST,   /**< the constant `value` (instruction bytes) */
    CC_TRAMP_ENV,     /**< the static chain: the nested function's environment */
    CC_TRAMP_FN       /**< the nested function's address */
} cc_tramp_part_t;

/**
 * One store that builds a GNU nested function's trampoline in the
 * caller's frame: @c size bytes at offset @c off.
 */
typedef struct {
    cc_tramp_part_t what; /**< what is stored */
    unsigned char off;    /**< offset in the trampoline */
    unsigned char size;   /**< bytes stored */
    long long value;      /**< CC_TRAMP_CONST: the bytes, little-endian */
} cc_target_tramp_store_t;

typedef struct {
    cc_target_t id;                            /**< which target this is */
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
    const char *m_flag;                           /**< the gcc option that selects it: "-m64", "-m32" */
    const char *as_flag;                          /**< the GNU assembler's: "--64", "--32" */
    const char *ld_emulation;                     /**< the GNU linker's -m: "elf_x86_64", "elf_i386" */
    const char *gnu_triple;                       /**< -dumpmachine on a GNU host */
    const char *host_ldso;                        /**< a GNU/Linux host's dynamic linker */
    int pie;                                      /**< executables link as PIEs (i386 code is not RIP-relative) */
    const char *full_barrier_asm;                 /**< inline asm for a full memory barrier (seq_cst fence) */
    int va_list_record_size;                      /**< bytes of the record a va_list points at, which va_copy
                                                       duplicates; 0 when a va_list is a plain argument pointer */
    const cc_target_tramp_store_t *trampoline;    /**< the stores that build a trampoline, CC_TRAMP_END-ended */
    int ret_by_address_classes;                   /**< the agg_class bits (cc_ssa.h CC_ABI_RET_*) of a returned
                                                       aggregate the emitter reads from the structure itself */
    /** how an aggregate argument travels (cc_abi.h: cc_abi_aggregate_arg()) */
    int (*abi_aggregate_arg)(const struct cc_translation_unit *tu, int sid, long size, struct cc_abi_regs *regs);
    /** how an aggregate is returned (cc_abi.h: cc_abi_aggregate_return()) */
    int (*abi_aggregate_return)(const struct cc_translation_unit *tu, int sid, long size);
    /** the target's own legalization of a function, after the shared one; NULL for none */
    int (*legalize)(struct cc_ssa_function *f, struct cc_diag *diag);
    /** the IR of va_arg (cc_abi.h: cc_abi_va_arg()) */
    int (*abi_va_arg)(struct cc_ssa_function *sf, int ap, const struct cc_abi_va_type *t, int *next_ap,
                      struct cc_diag *diag);
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

/**
 * The x86 instruction-set extensions, as gcc names them (-m<name>): what
 * an -march has, which -m<name> and -mno-<name> then change, and which
 * the preprocessor's feature macros (__SSE2__, __POPCNT__, ...) say.
 */
typedef enum {
    CC_ISA_MMX = 1u << 0,
    CC_ISA_SSE = 1u << 1,
    CC_ISA_SSE2 = 1u << 2,
    CC_ISA_SSE3 = 1u << 3,
    CC_ISA_SSSE3 = 1u << 4,
    CC_ISA_SSE4_1 = 1u << 5,
    CC_ISA_SSE4_2 = 1u << 6,
    CC_ISA_POPCNT = 1u << 7,
    CC_ISA_CRC32 = 1u << 8,
    CC_ISA_AVX = 1u << 9,
    CC_ISA_AVX2 = 1u << 10,
    CC_ISA_FMA = 1u << 11,
    CC_ISA_F16C = 1u << 12,
    CC_ISA_BMI = 1u << 13,
    CC_ISA_BMI2 = 1u << 14,
    CC_ISA_LZCNT = 1u << 15,
    CC_ISA_MOVBE = 1u << 16,
    CC_ISA_XSAVE = 1u << 17,
    CC_ISA_FXSR = 1u << 18,
    CC_ISA_SAHF = 1u << 19, /**< lahf and sahf in 64-bit mode (32-bit mode always has them) */
    CC_ISA_CX16 = 1u << 20,
    CC_ISA_AVX512F = 1u << 21,
    CC_ISA_AVX512BW = 1u << 22,
    CC_ISA_AVX512CD = 1u << 23,
    CC_ISA_AVX512DQ = 1u << 24,
    CC_ISA_AVX512VL = 1u << 25,
    CC_ISA_SSE4A = 1u << 26,
    CC_ISA_ABM = 1u << 27
} cc_isa_t;

/**
 * One -march (or -mtune) gcc knows, as cc has it: the extensions it
 * has, how much of the i386 instruction set (3, the 386, to 7), and the
 * macros gcc defines for it.
 */
typedef struct {
    const char *name;               /**< the -march name: "pentium4", "x86-64-v2" */
    int level;                      /**< 3 (i386) .. 7: cmpxchg from 4, cmpxchg8b from 5, cmov from 6 */
    unsigned isa;                   /**< its extensions, cc_isa_t bits */
    const char *const *arch_macros; /**< as -march: "__pentium4", "__pentium4__"; NULL-ended */
    const char *const *tune_macros; /**< as -mtune (and -march without one): "__tune_pentium4__"; NULL-ended */
} cc_arch_t;

/**
 * The -march or -mtune called @p name.
 *
 * @param name  the name, as on the command line
 * @return      its description, or NULL for a name cc does not know
 */
const cc_arch_t *cc_arch_find(const char *name);

/**
 * Whether @p flag is an extension's option, -m<ext> or -mno-<ext>
 * (-msse4.2, -mno-avx, -mavx512f, ...).
 *
 * @param flag  the option
 * @return      1 if it is, else 0
 */
int cc_isa_is_flag(const char *flag);

/**
 * The extensions an -march with @p arch_isa has after options @p flags,
 * as gcc decides them.  An option given again later, either way, counts
 * only there (-mno-sse -msse is -msse).  -m<ext> turns on the extension
 * and those it needs (-msse4.1 SSSE3 down to SSE), -mno-<ext> turns off
 * it and those that need it (-mno-sse2 SSE3 up to AVX-512), and the
 * -march's own are then added where no option said otherwise, as are
 * those an extension brings: MMX with SSE, POPCNT and CRC32 with SSE4.2,
 * LZCNT and POPCNT with ABM.  Other options among @p flags are passed
 * over.
 *
 * @param arch_isa  the -march's extensions
 * @param flags     the options, in order
 * @param n         how many
 * @return          the extensions, cc_isa_t bits
 */
unsigned cc_isa_from_flags(unsigned arch_isa, const char *const *flags, size_t n);

/**
 * The feature macro gcc defines for extension @p bit: "__SSE4_2__".
 *
 * @param bit  one cc_isa_t
 * @return     the macro's name, or NULL for none (SAHF's depends on the mode)
 */
const char *cc_isa_macro(unsigned bit);

#endif
