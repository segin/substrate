#ifndef CC_SSA_H
#define CC_SSA_H

#include <stddef.h>
#include <stdint.h>
#include "cc_frontend.h"

typedef enum {
    CC_VAL_I64 = 0,
    CC_VAL_F64
} cc_value_type_t;

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
#define CC_ABI_RET_VEC128 0x800      /**< agg_class: a 16-byte vector, returned whole in %xmm0 */
#define CC_ABI_RET_VEC64 0x1000      /**< agg_class (i386): an 8-byte vector, returned in %mm0 */

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
    CC_SSA_STACKALLOC,
    CC_SSA_DYNALLOC,   /**< dst = lhs bytes taken off the stack; rhs, if >= 0, a STACKALLOC save slot (below) */
    CC_SSA_CHAIN,      /**< dst = the static chain register as the function was entered (the first instruction) */
    CC_SSA_LABEL,
    CC_SSA_BR,
    CC_SSA_BR_COND,
    CC_SSA_VA_START,
    CC_SSA_CALL,
    CC_SSA_CALLI,
    CC_SSA_ASM,
    CC_SSA_TRAP,
    CC_SSA_RET
} cc_ssa_opcode_t;

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
    int agg_class;     /**< CALL, RET of an aggregate: CC_ABI_SSE0/SSE1/RET_* for how it is returned */
    char *sym;
    int *args;
    unsigned char *call_arg_abi;
    uint32_t *call_arg_sizes;  /**< bytes each argument takes; structures may be large */
    size_t arg_count;
    int asm_volatile;
    int asm_is_goto;
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

typedef struct {
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

    cc_ssa_instr_t *instrs;
    size_t instr_count;
    size_t instr_cap;

    cc_value_type_t *value_types;
    unsigned char *value_is_unsigned;
    uint32_t *value_sizes;     /**< bytes in each value; a structure value is its whole size */
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
} cc_ssa_module_t;

void cc_ssa_module_init(cc_ssa_module_t *m);
void cc_ssa_module_free(cc_ssa_module_t *m);
void cc_ssa_instr_free(cc_ssa_instr_t *in);
void cc_ssa_function_init(cc_ssa_function_t *f);
void cc_ssa_function_free(cc_ssa_function_t *f);
int cc_ssa_function_append_instr(cc_ssa_function_t *f, const cc_ssa_instr_t *in, cc_diag_t *diag);
int cc_ssa_function_has_label(const cc_ssa_function_t *f, int label);
size_t cc_ssa_function_basic_block_count(const cc_ssa_function_t *f);
int cc_ssa_find_label_instr_index(const cc_ssa_function_t *f, int label);
int cc_ssa_opcode_is_terminator(cc_ssa_opcode_t op);
int cc_ssa_opcode_has_side_effect(cc_ssa_opcode_t op);
int cc_ssa_instr_validate(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t index, cc_diag_t *diag);
int cc_ast_to_ssa(const cc_translation_unit_t *tu, cc_ssa_module_t *out, cc_diag_t *diag);

#endif
