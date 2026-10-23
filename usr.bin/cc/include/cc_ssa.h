#ifndef CC_SSA_H
#define CC_SSA_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cc_frontend.h"

typedef enum {
    CC_VAL_I64 = 0,
    CC_VAL_F64
} cc_value_type_t;

/** The kind of an IR value's type. */
typedef enum {
    CC_TY_INT = 0, /**< an integer of 1, 2, 4 or 8 bytes */
    CC_TY_FLOAT,   /**< a floating value: float (4 bytes), double (8), long double (10 or more) */
    CC_TY_AGG      /**< a structure, array or integer wider than 8 bytes, held as the address of its bytes */
} cc_ssa_tykind_t;

/** An IR value's type: its kind and size, and for an integer how it is held extended. */
typedef struct {
    unsigned char kind;        /**< a cc_ssa_tykind_t */
    unsigned char is_unsigned; /**< an integer held zero-extended, not sign-extended */
    uint32_t size;             /**< bytes; an aggregate's whole size */
} cc_ssa_type_t;

typedef enum {
    CC_CMP_EQ = 0,
    CC_CMP_NE,
    CC_CMP_LT,
    CC_CMP_LE,
    CC_CMP_GT,
    CC_CMP_GE
} cc_cmp_kind_t;

typedef enum {
    CC_CALL_ARG_ABI_DEFAULT = 0,
    CC_CALL_ARG_ABI_LDOUBLE = 1,
    CC_CALL_ARG_ABI_AGG_MEM = 2,
    CC_CALL_ARG_ABI_AGG_REG = 3,
    CC_CALL_ARG_ABI_ZERO = 4,
    CC_CALL_ARG_ABI_FLOAT = 5  /**< a prototyped float: single precision across the call (the value is a double) */
} cc_call_arg_abi_t;

/*
 * An x86-64 aggregate's psABI classes, for the eightbytes of one that
 * travels in registers.  In a call_arg_abi or param_abi byte they are
 * or'ed onto CC_CALL_ARG_ABI_AGG_REG (only); in agg_class they describe a
 * returned aggregate.  An eightbyte without its SSE bit is INTEGER.
 */
#define CC_ABI_KIND(b) ((b) & 0x0f)  /**< the cc_call_arg_abi_t in an ABI byte */
#define CC_ABI_SSE0 0x10             /**< the first eightbyte is SSE: it goes in an xmm register */
#define CC_ABI_SSE1 0x20             /**< the second eightbyte is SSE */
#define CC_ABI_RET_X87 0x40          /**< returned in %st0: `struct { long double; }` */
#define CC_ABI_RET_X87_COMPLEX 0x80  /**< returned in %st0 (real) and %st1 (imaginary): long double _Complex */
#define CC_ABI_RET_EAX_EDX 0x100     /**< i386: returned in %eax:%edx, not by hidden pointer: float _Complex */
#define CC_ABI_RET_MEMORY 0x200      /**< x86-64: returned by hidden pointer although it is 16 bytes or fewer */
#define CC_ABI_RET_FLOAT 0x400       /**< CALL: the callee returns a float, in single precision */
#define CC_ABI_VEC128 0x40           /**< with AGG_REG: a 16-byte vector, whole in one xmm register */
#define CC_ABI_VEC64 0x80            /**< with AGG_REG (i386): an 8-byte vector, whole in one MMX register */
#define CC_ABI_STACK_VEC 0x40        /**< with AGG_MEM: a vector, its stack copy aligned to its size from 16 up */
#define CC_ABI_STACK_16 0x80         /**< with AGG_MEM: its stack copy aligned to 16 (_Complex _Float128) */
/*
 * i386 -mregparm / regparm(N): with CC_CALL_ARG_ABI_DEFAULT or _AGG_MEM
 * (which never carry the SSE bits, x86-64's), the register an argument
 * travels in instead of the stack -- 1 %eax, 2 %edx, 3 %ecx; an argument
 * of two or three words fills that one and the ones after.  0: the stack.
 */
#define CC_ABI_REGPARM_MASK 0x30
#define CC_ABI_REGPARM(b) (((b) & CC_ABI_REGPARM_MASK) >> 4)
#define CC_ABI_WITH_REGPARM(b, r) ((unsigned char)(((b) & ~CC_ABI_REGPARM_MASK) | (((r) & 3) << 4)))
#define CC_ABI_RET_VEC128 0x800      /**< agg_class: a 16-byte vector, returned whole in %xmm0 */
#define CC_ABI_RET_VEC64 0x1000      /**< agg_class (i386): an 8-byte vector, returned in %mm0 */
#define CC_ABI_RET_SRET_REG 0x2000   /**< agg_class, CALL (i386 regparm): the hidden pointer goes in %eax, not on the stack */

typedef enum {
    CC_SSA_PARAM = 0,
    CC_SSA_CONST,
    CC_SSA_STR,
    CC_SSA_GADDR,
    CC_SSA_LADDR,
    CC_SSA_ADD,
    CC_SSA_SUB,
    CC_SSA_MUL,
    CC_SSA_DIV,
    CC_SSA_AND,
    CC_SSA_OR,
    CC_SSA_XOR,
    CC_SSA_SHL,
    CC_SSA_SHR,
    CC_SSA_ADDR,
    CC_SSA_LOAD,
    CC_SSA_STORE,
    CC_SSA_MOV,
    CC_SSA_CMP,
    CC_SSA_I2F,
    CC_SSA_F2I,
    CC_SSA_FROUND32,
    CC_SSA_FROUND64,   /**< dst = lhs (a long double) rounded to double */
    CC_SSA_EXT,        /**< dst = the low imm bits of integer lhs, zero-extended if is_unsigned, else
                            sign-extended, to dst's size: every integer conversion that changes a value.
                            Legalization expands it for the target; rhs, if >= 0, is a value reserved for
                            the expansion (cc_ssa_ext_needs_scratch()), so that values keep their numbers. */
    CC_SSA_STACKALLOC,
    CC_SSA_DYNALLOC,   /**< dst = lhs bytes taken off the stack; rhs, if >= 0, a STACKALLOC save slot (below) */
    CC_SSA_CHAIN,      /**< dst = the static chain register as the function was entered (the first instruction) */
    CC_SSA_FRAMEADDR,  /**< dst = the frame pointer imm frames up (0: this function's), as __builtin_frame_address */
    CC_SSA_REGREAD,    /**< dst = machine register sym (no %), as a global register variable is read */
    CC_SSA_LABEL,
    CC_SSA_BR,
    CC_SSA_BR_COND,
    CC_SSA_VA_START,
    CC_SSA_CALL,
    CC_SSA_CALLI,
    CC_SSA_ASM,
    CC_SSA_TRAP,
    CC_SSA_RET,
    /*
     * Legal IR only, written by a target's legalization (i386: 64-bit
     * integers as two 32-bit words, legalize_i386.c).
     */
    CC_SSA_LO32,       /**< dst (4 bytes) = the low word of 64-bit lhs */
    CC_SSA_HI32,       /**< dst (4 bytes) = the high word of 64-bit lhs */
    CC_SSA_PAIR,       /**< dst (8 bytes) = lhs as the low word, rhs as the high word */
    CC_SSA_UMULW       /**< dst (8 bytes) = the full product of words lhs and rhs, unsigned */
} cc_ssa_opcode_t;

/**
 * The width of the word an EXT of an x86 target works in: 64 bits on
 * x86-64, and on i386 for a result of 8 bytes (a pair of words); else 32.
 *
 * @param size          the result's size in bytes
 * @param pointer_size  the target's pointer size
 */
static inline int cc_ssa_ext_word_bits(long size, long pointer_size) {
    return (size >= 8 || pointer_size >= 8) ? 64 : 32;
}

/**
 * Whether expanding an EXT needs an intermediate value: all but a sign
 * extension that keeps the whole word, which is a move.
 */
static inline int cc_ssa_ext_needs_scratch(int bits, int is_unsigned, long size, long pointer_size) {
    return is_unsigned || bits < cc_ssa_ext_word_bits(size, pointer_size);
}

#define CC_SSA_ASM_MEM_INDIRECT_ENCODE(v) (-(int)((v) + 2))
#define CC_SSA_ASM_MEM_INDIRECT_P(v) ((v) <= -2)
#define CC_SSA_ASM_MEM_INDIRECT_DECODE(v) (-(int)(v) - 2)

typedef struct {
    cc_ssa_opcode_t op;
    int dst;
    int lhs;
    int rhs;
    long long imm;
    double fimm;
    long double fimm_ld;
    int label;
    int true_label;
    int false_label;
    int param_index;
    cc_cmp_kind_t cmp_kind;
    int is_unsigned;
    int call_is_variadic;
    int call_fixed_count;
    int call_ret_x87;
    int call_chain;    /**< CALL: one more than the value to pass in the static chain register; 0 for none */
    int call_binds_local; /**< CALL: the callee is static or hidden, protected or internal by its declaration,
                               so -fPIC calls it directly, not through the PLT */
    int agg_class;     /**< CALL, RET of an aggregate: CC_ABI_SSE0/SSE1/RET_* for how it is returned */
    char *sym;
    int *args;
    unsigned char *call_arg_abi;
    uint32_t *call_arg_sizes;  /**< bytes each argument takes; structures may be large */
    size_t arg_count;
    int asm_volatile;
    int asm_is_goto;
    int asm_is_basic; /**< basic asm: emit the template as it is, no operand substitution */
    int *asm_out_values;
    unsigned char *asm_out_sizes;
    char **asm_out_constraints;
    char **asm_out_names;
    size_t asm_out_count;
    int *asm_in_values;
    unsigned char *asm_in_sizes;
    char **asm_in_constraints;
    char **asm_in_names;
    size_t asm_in_count;
    char **asm_clobbers;
    size_t asm_clobber_count;
    int *asm_goto_labels;
    char **asm_goto_names;
    size_t asm_goto_count;
} cc_ssa_instr_t;

typedef struct {
    long init_size;
    long long init_i;
    double init_f;
    long double init_f_ld;
    int init_is_zero_fill;
    int init_is_float;
    int init_is_string;
    int init_is_symbol;
    char *init_str;
    char *init_sym;
} cc_ssa_global_init_item_t;

/** What a global's data (an array's elements) is, as its initializer is laid out. */
typedef enum {
    CC_SSA_DATA_OTHER = 0, /**< a structure or anything else laid out as bytes */
    CC_SSA_DATA_INT,       /**< an integer other than those below */
    CC_SSA_DATA_BOOL,      /**< _Bool: a floating initializer is true when non-zero */
    CC_SSA_DATA_CHAR,      /**< char or unsigned char: a string may initialize an array of them */
    CC_SSA_DATA_FLOAT,     /**< float, double or long double */
    CC_SSA_DATA_POINTER    /**< a pointer */
} cc_ssa_data_kind_t;

typedef struct {
    char *name;
    int is_array;                 /**< an array, whose elements are data_kind of elem_size bytes each */
    cc_ssa_data_kind_t data_kind; /**< what the object, or an array's element, is */
    long elem_size;               /**< the size of the object, or of an array's element; <= 0 if not known */
    long array_len;               /**< an array's element count; 0 if not known; -1 for no array */
    long size_bytes;
    long type_align;              /**< the alignment its type needs; 0 if not known */
    int storage;
    cc_attr_flags_t attr_flags;
    long attr_align;
    char *attr_section;
    char *attr_alias;
    int has_init;
    long long init_i;
    double init_f;
    long double init_f_ld;
    int init_is_float;
    int init_is_string;
    int init_is_symbol;
    char *init_str;
    char *init_sym;
    cc_ssa_global_init_item_t *init_items;
    size_t init_item_count;
    size_t init_item_cap;
} cc_ssa_global_t;

typedef struct cc_ssa_function {
    char *name;
    cc_value_type_t ret_type;
    int storage;
    cc_attr_flags_t attr_flags;
    long attr_align;
    char *attr_section;
    char *attr_alias;
    int is_variadic;

    size_t param_count;
    int *param_values;
    cc_value_type_t *param_types;
    unsigned char *param_abi;
    unsigned char ret_abi;
    int sret_reg; /**< i386 regparm: the hidden pointer of a structure result came in %eax, not on the stack */

    cc_ssa_instr_t *instrs;
    size_t instr_count;
    size_t instr_cap;

    cc_ssa_type_t *value_ty;   /**< each value's type: the accessors below read and write it */
    size_t value_cap;
    int value_count;
    int label_count;
} cc_ssa_function_t;

typedef struct {
    cc_ssa_global_t *globals;
    size_t global_count;
    size_t global_cap;
    cc_ssa_function_t *funcs;
    size_t func_count;
    int needs_exec_stack; /**< a trampoline is built on the stack (a nested function's address) */
    char **toplevel_asm;       /**< each file-scope `asm("...")`'s text, emitted as written (owned) */
    size_t toplevel_asm_count; /**< the number of them */
    /** the functions the module only declares that bind within the object -- static, or hidden, protected or
     *  internal by an attribute or a visibility pragma -- for position-independent code to reach directly (owned) */
    char **local_decls;
    size_t local_decl_count;
} cc_ssa_module_t;

/** The register class value @p v lives in: CC_VAL_F64 for a floating value, else CC_VAL_I64. */
static inline cc_value_type_t cc_ssa_value_class(const cc_ssa_function_t *f, int v) {
    return f->value_ty[v].kind == CC_TY_FLOAT ? CC_VAL_F64 : CC_VAL_I64;
}

/** How many bytes value @p v holds. */
static inline uint32_t cc_ssa_value_size(const cc_ssa_function_t *f, int v) {
    return f->value_ty[v].size;
}

/** Whether integer value @p v is held zero-extended. */
static inline int cc_ssa_value_is_unsigned(const cc_ssa_function_t *f, int v) {
    return f->value_ty[v].is_unsigned;
}

/** The kind a value of class @p vt and @p size bytes has. */
static inline unsigned char cc_ssa_tykind_of(cc_value_type_t vt, uint32_t size) {
    if (vt == CC_VAL_F64) {
        return CC_TY_FLOAT;
    }
    return (size == 1 || size == 2 || size == 4 || size == 8) ? CC_TY_INT : CC_TY_AGG;
}

/** Make value @p v of register class @p vt, keeping its size. */
static inline void cc_ssa_value_set_class(cc_ssa_function_t *f, int v, cc_value_type_t vt) {
    f->value_ty[v].kind = cc_ssa_tykind_of(vt, f->value_ty[v].size);
}

/** Make value @p v @p size bytes, keeping its class. */
static inline void cc_ssa_value_set_size(cc_ssa_function_t *f, int v, uint32_t size) {
    f->value_ty[v].size = size;
    f->value_ty[v].kind = cc_ssa_tykind_of(cc_ssa_value_class(f, v), size);
}

/** Say whether integer value @p v is held zero-extended. */
static inline void cc_ssa_value_set_unsigned(cc_ssa_function_t *f, int v, int is_unsigned) {
    f->value_ty[v].is_unsigned = (unsigned char)(is_unsigned ? 1 : 0);
}

void cc_ssa_module_init(cc_ssa_module_t *m);
void cc_ssa_module_free(cc_ssa_module_t *m);
void cc_ssa_instr_free(cc_ssa_instr_t *in);
void cc_ssa_function_init(cc_ssa_function_t *f);
void cc_ssa_function_free(cc_ssa_function_t *f);

/** Free what global @p g owns, and clear it. */
void cc_ssa_global_free(cc_ssa_global_t *g);

/**
 * Add a value to a function: signed, and a word (the pointer size), or a
 * double for CC_VAL_F64, until its definition says otherwise.
 *
 * @param f   the function
 * @param vt  its register class
 * @return    the new value, or -1 when out of memory
 */
int cc_ssa_function_new_value(cc_ssa_function_t *f, cc_value_type_t vt);
int cc_ssa_function_append_instr(cc_ssa_function_t *f, const cc_ssa_instr_t *in, cc_diag_t *diag);
int cc_ssa_function_has_label(const cc_ssa_function_t *f, int label);
size_t cc_ssa_function_basic_block_count(const cc_ssa_function_t *f);
int cc_ssa_find_label_instr_index(const cc_ssa_function_t *f, int label);
int cc_ssa_opcode_is_terminator(cc_ssa_opcode_t op);
int cc_ssa_opcode_has_side_effect(cc_ssa_opcode_t op);
int cc_ssa_instr_validate(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t index, cc_diag_t *diag);
int cc_ast_to_ssa(const cc_translation_unit_t *tu, cc_ssa_module_t *out, cc_diag_t *diag);

/**
 * Write a module as text in the IR form the ir/ tools read (ir-verifier,
 * ir-normalize, ir-diff); see middle/ssa_dump.c for how lowering's IR
 * maps onto it.
 *
 * @param m             the module
 * @param name          the module's name (the source file)
 * @param pointer_size  the target's pointer size, for its `target` line
 * @param legal         the IR has been legalized for the target, for its
 *                      `stage` line: the verifier holds neutral IR to the
 *                      integer width rules, which legal IR need not meet
 * @param out           where to write
 * @return              0, or -1 when out of memory or on a write error
 */
int cc_ssa_dump_module(const cc_ssa_module_t *m, const char *name, int pointer_size, int legal, FILE *out);

#endif
