#ifndef CC_FRONTEND_H
#define CC_FRONTEND_H

#include <stddef.h>

typedef struct {
    size_t line;
    size_t col;
    char message[256];
} cc_diag_t;

typedef enum {
    CC_TYPE_VOID = 0,
    CC_TYPE_INT,
    CC_TYPE_DOUBLE
} cc_type_t;

typedef enum {
    CC_BIN_ADD = 0,
    CC_BIN_SUB,
    CC_BIN_MUL,
    CC_BIN_DIV,
    CC_BIN_EQ,
    CC_BIN_NE,
    CC_BIN_LT,
    CC_BIN_LE,
    CC_BIN_GT,
    CC_BIN_GE
} cc_binop_t;

typedef enum {
    CC_EXPR_INT = 0,
    CC_EXPR_FLOAT,
    CC_EXPR_IDENT,
    CC_EXPR_BIN,
    CC_EXPR_CALL,
    CC_EXPR_ASSIGN
} cc_expr_kind_t;

typedef struct cc_expr cc_expr_t;

struct cc_expr {
    cc_expr_kind_t kind;
    cc_type_t value_type;
    long int_val;
    double float_val;
    char *ident;
    cc_binop_t op;
    cc_expr_t *lhs;
    cc_expr_t *rhs;
    cc_expr_t **args;
    size_t arg_count;
};

typedef enum {
    CC_STMT_DECL = 0,
    CC_STMT_EXPR,
    CC_STMT_RETURN,
    CC_STMT_IF,
    CC_STMT_BLOCK,
    CC_STMT_WHILE,
    CC_STMT_FOR,
    CC_STMT_BREAK,
    CC_STMT_CONTINUE
} cc_stmt_kind_t;

typedef struct cc_stmt cc_stmt_t;

struct cc_stmt {
    cc_type_t type;
    cc_stmt_kind_t kind;
    char *decl_name;
    cc_expr_t *expr;
    cc_expr_t *init_expr;
    cc_expr_t *post_expr;
    cc_stmt_t *then_branch;
    cc_stmt_t *else_branch;
    cc_stmt_t *block_stmts;
    size_t block_count;
};

typedef struct {
    char *name;
    cc_type_t type;
} cc_param_t;

typedef struct {
    char *name;
    cc_type_t ret_type;
    int is_variadic;
    cc_param_t *params;
    size_t param_count;
    cc_stmt_t *stmts;
    size_t stmt_count;
} cc_function_t;

typedef struct {
    cc_function_t *funcs;
    size_t func_count;
} cc_translation_unit_t;

int cc_parse_file(const char *path, cc_translation_unit_t *out, cc_diag_t *diag);
int cc_sema_check(const cc_translation_unit_t *tu, cc_diag_t *diag);
void cc_tu_free(cc_translation_unit_t *tu);

#endif
