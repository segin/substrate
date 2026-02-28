#ifndef CC_MIR_H
#define CC_MIR_H

#include "cc_frontend.h"
#include "cc_ssa.h"

typedef enum {
    CC_MIR_NOP = 0,
    CC_MIR_PARAM,
    CC_MIR_CONST,
    CC_MIR_STR,
    CC_MIR_ADDR,
    CC_MIR_LOAD,
    CC_MIR_STORE,
    CC_MIR_ALU,
    CC_MIR_CMP,
    CC_MIR_CAST,
    CC_MIR_LABEL,
    CC_MIR_BR,
    CC_MIR_BR_COND,
    CC_MIR_VA_START,
    CC_MIR_CALL,
    CC_MIR_CALLI,
    CC_MIR_ASM,
    CC_MIR_TRAP,
    CC_MIR_RET
} cc_mir_opcode_t;

typedef enum {
    CC_MIR_ALU_ADD = 0,
    CC_MIR_ALU_SUB,
    CC_MIR_ALU_MUL,
    CC_MIR_ALU_DIV,
    CC_MIR_ALU_AND,
    CC_MIR_ALU_OR,
    CC_MIR_ALU_XOR,
    CC_MIR_ALU_SHL,
    CC_MIR_ALU_SHR
} cc_mir_alu_op_t;

typedef enum {
    CC_MIR_CAST_MOV = 0,
    CC_MIR_CAST_I2F,
    CC_MIR_CAST_F2I,
    CC_MIR_CAST_FROUND32
} cc_mir_cast_op_t;

typedef struct {
    cc_mir_opcode_t op;
    int dst;
    int lhs;
    int rhs;
    long imm;
    double fimm;
    int label;
    int true_label;
    int false_label;
    int param_index;
    cc_mir_alu_op_t alu_op;
    cc_mir_cast_op_t cast_op;
    cc_cmp_kind_t cmp_kind;
    int is_unsigned;
    int call_is_variadic;
    int call_fixed_count;
    char *sym;
    int *args;
    size_t arg_count;
    cc_ssa_instr_t *asm_shadow;
} cc_mir_instr_t;

typedef struct {
    char *name;
    cc_value_type_t ret_type;
    int storage;
    int attr_flags;
    long attr_align;
    char *attr_section;
    char *attr_alias;
    int is_variadic;

    size_t param_count;
    int *param_values;
    cc_value_type_t *param_types;

    cc_mir_instr_t *instrs;
    size_t instr_count;
    size_t instr_cap;

    cc_value_type_t *value_types;
    size_t value_cap;
    int value_count;
    int label_count;
} cc_mir_function_t;

typedef struct {
    cc_mir_function_t *funcs;
    size_t func_count;
} cc_mir_module_t;

void cc_mir_module_init(cc_mir_module_t *m);
void cc_mir_module_free(cc_mir_module_t *m);
int cc_backend_lower_to_mir(const cc_ssa_module_t *ssa, cc_mir_module_t *out, cc_diag_t *diag);
int cc_backend_mir_validate(const cc_mir_module_t *m, cc_diag_t *diag);

#endif
