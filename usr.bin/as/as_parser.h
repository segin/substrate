#ifndef SUBSTRATE_AS_PARSER_H
#define SUBSTRATE_AS_PARSER_H

#include "as_lexer.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_PARSER_ARCH_GENERIC = 0,
    AS_PARSER_ARCH_X86,
    AS_PARSER_ARCH_ARM,
} as_parser_arch_t;

typedef struct {
    int intel_syntax;
    as_parser_arch_t arch;
} as_parser_cfg_t;

typedef enum {
    AS_EXPR_CONST = 0,
    AS_EXPR_SYMBOL,
    AS_EXPR_LOCAL_REF,
    AS_EXPR_UNARY,
    AS_EXPR_BINARY,
} as_expr_kind_t;

typedef enum {
    AS_EXPR_OP_NONE = 0,
    AS_EXPR_OP_ADD,
    AS_EXPR_OP_SUB,
    AS_EXPR_OP_MUL,
    AS_EXPR_OP_DIV,
    AS_EXPR_OP_MOD,
    AS_EXPR_OP_OR,
    AS_EXPR_OP_AND,
    AS_EXPR_OP_XOR,
    AS_EXPR_OP_SHL,
    AS_EXPR_OP_SHR,
    AS_EXPR_OP_NEG,
    AS_EXPR_OP_BNOT,
} as_expr_op_t;

typedef struct as_expr as_expr_t;
struct as_expr {
    as_expr_kind_t kind;
    as_expr_op_t op;
    long long value;
    char *symbol;
    int local_digit;
    int local_forward;
    int local_resolved;
    unsigned local_target_line;
    unsigned src_line;
    char *src_file;
    as_expr_t *lhs;
    as_expr_t *rhs;
};

typedef enum {
    AS_OPERAND_INVALID = 0,
    AS_OPERAND_REGISTER,
    AS_OPERAND_IMMEDIATE,
    AS_OPERAND_MEMORY,
    AS_OPERAND_LABEL_REF,
    AS_OPERAND_SHIFTED_REGISTER,
    AS_OPERAND_REGISTER_LIST,
    AS_OPERAND_COPROCESSOR,
} as_operand_kind_t;

typedef enum {
    AS_SHIFT_NONE = 0,
    AS_SHIFT_LSL,
    AS_SHIFT_LSR,
    AS_SHIFT_ASR,
    AS_SHIFT_ROR,
    AS_SHIFT_RRX,
} as_shift_kind_t;

typedef struct {
    char *base_reg;
    char *index_reg;
    int scale;
    char *segment_reg;
    int size_bits;
    as_expr_t *disp;
} as_mem_operand_t;

typedef struct {
    char *reg;
    as_shift_kind_t shift;
    int amount_is_reg;
    char *amount_reg;
    as_expr_t *amount_expr;
} as_shift_operand_t;

typedef struct {
    as_operand_kind_t kind;
    char *raw;
    union {
        char *reg;
        as_expr_t *expr;
        as_mem_operand_t mem;
        as_shift_operand_t shifted;
        struct {
            char **regs;
            size_t count;
        } reg_list;
        char *coproc;
    } u;
} as_operand_t;

#define AS_PREFIX_LOCK (1u << 0)
#define AS_PREFIX_REP (1u << 1)
#define AS_PREFIX_REPE (1u << 2)
#define AS_PREFIX_REPNE (1u << 3)
#define AS_PREFIX_SEG_OVERRIDE (1u << 4)
#define AS_PREFIX_REX (1u << 5)
#define AS_PREFIX_DATA16 (1u << 6)
#define AS_PREFIX_ADDR16 (1u << 7)

typedef struct {
    char *mnemonic;
    char *arm_condition;
    unsigned syntax_intel;
    unsigned prefixes;
    char *segment_override;
    as_operand_t *operands;
    size_t operand_count;
} as_instruction_t;

typedef enum {
    AS_STMT_EMPTY = 0,
    AS_STMT_LABEL_ONLY,
    AS_STMT_DIRECTIVE,
    AS_STMT_INSTRUCTION,
} as_stmt_kind_t;

typedef struct {
    char *name;
    unsigned line;
    char *file;
} as_label_def_t;

typedef struct {
    char *name;
    char **args;
    size_t arg_count;
} as_directive_t;

typedef struct {
    as_stmt_kind_t kind;
    char *file;
    unsigned line;
    as_label_def_t *labels;
    size_t label_count;
    union {
        as_directive_t directive;
        as_instruction_t instr;
    } u;
} as_stmt_t;

typedef struct {
    as_stmt_t *items;
    size_t count;
    size_t cap;
} as_parse_result_t;

void as_parse_result_init(as_parse_result_t *r);
void as_parse_result_free(as_parse_result_t *r);

int as_parse_tokens(const as_token_vec_t *tokens, const as_parser_cfg_t *cfg,
                    as_parse_result_t *out, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
