#ifndef CC_IR_H
#define CC_IR_H

#include <stddef.h>
#include <stdio.h>

typedef struct {
    char *name;
} ir_value_use_t;

typedef struct {
    char *text;
    char *opcode;
    char *def;
    ir_value_use_t *uses;
    size_t use_count;
    size_t use_cap;
    int is_terminator;
    int is_phi;
    int phi_incoming_count;
    size_t line;
} ir_instr_t;

typedef struct {
    char *name;
    ir_instr_t *instrs;
    size_t instr_count;
    size_t instr_cap;

    char **preds;
    size_t pred_count;
    size_t pred_cap;

    char **succs;
    size_t succ_count;
    size_t succ_cap;

    size_t line;
} ir_block_t;

typedef struct {
    char *name;
    char **args;
    size_t arg_count;
    size_t arg_cap;

    ir_block_t *blocks;
    size_t block_count;
    size_t block_cap;
} ir_func_t;

typedef struct {
    char *name;
    char *target;
    ir_func_t *funcs;
    size_t func_count;
    size_t func_cap;
} ir_module_t;

typedef struct {
    char *msg;
    size_t line;
} ir_error_t;

void ir_module_init(ir_module_t *m);
void ir_module_free(ir_module_t *m);

int ir_parse_file(const char *path, ir_module_t *out, ir_error_t *err);
int ir_verify_module(const ir_module_t *m, ir_error_t *err);
int ir_serialize_module(const ir_module_t *m, FILE *out, int normalize_ids);

void ir_error_free(ir_error_t *err);

#endif
