#ifndef CC_SSA_H
#define CC_SSA_H

#include <stddef.h>
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
    CC_SSA_PARAM = 0,
    CC_SSA_CONST,
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
    CC_SSA_LABEL,
    CC_SSA_BR,
    CC_SSA_BR_COND,
    CC_SSA_CALL,
    CC_SSA_RET
} cc_ssa_opcode_t;

typedef struct {
    cc_ssa_opcode_t op;
    int dst;
    int lhs;
    int rhs;
    long imm;
    double fimm;
    int label;
    int true_label;
    int false_label;
    int param_index;
    cc_cmp_kind_t cmp_kind;
    int is_unsigned;
    int call_is_variadic;
    char *sym;
    int *args;
    size_t arg_count;
} cc_ssa_instr_t;

typedef struct {
    char *name;
    cc_value_type_t ret_type;
    int is_variadic;

    size_t param_count;
    int *param_values;
    cc_value_type_t *param_types;

    cc_ssa_instr_t *instrs;
    size_t instr_count;
    size_t instr_cap;

    cc_value_type_t *value_types;
    size_t value_cap;
    int value_count;
    int label_count;
} cc_ssa_function_t;

typedef struct {
    cc_ssa_function_t *funcs;
    size_t func_count;
} cc_ssa_module_t;

void cc_ssa_module_init(cc_ssa_module_t *m);
void cc_ssa_module_free(cc_ssa_module_t *m);
int cc_ast_to_ssa(const cc_translation_unit_t *tu, cc_ssa_module_t *out, cc_diag_t *diag);

#endif
