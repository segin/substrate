#include "ir.h"
#include <stdlib.h>
#include <string.h>

static void ir_instr_free(ir_instr_t *in) {
    size_t i;
    free(in->text);
    free(in->opcode);
    free(in->def);
    for (i = 0; i < in->use_count; ++i) {
        free(in->uses[i].name);
    }
    free(in->uses);
}

static void ir_block_free(ir_block_t *b) {
    size_t i;
    free(b->name);
    for (i = 0; i < b->instr_count; ++i) {
        ir_instr_free(&b->instrs[i]);
    }
    free(b->instrs);
    for (i = 0; i < b->pred_count; ++i) {
        free(b->preds[i]);
    }
    free(b->preds);
    for (i = 0; i < b->succ_count; ++i) {
        free(b->succs[i]);
    }
    free(b->succs);
}

static void ir_func_free(ir_func_t *f) {
    size_t i;
    free(f->name);
    for (i = 0; i < f->arg_count; ++i) {
        free(f->args[i]);
    }
    free(f->args);
    for (i = 0; i < f->block_count; ++i) {
        ir_block_free(&f->blocks[i]);
    }
    free(f->blocks);
}

void ir_module_init(ir_module_t *m) {
    memset(m, 0, sizeof(*m));
}

void ir_module_free(ir_module_t *m) {
    size_t i;
    if (!m) return;
    free(m->name);
    free(m->target);
    for (i = 0; i < m->func_count; ++i) {
        ir_func_free(&m->funcs[i]);
    }
    free(m->funcs);
    memset(m, 0, sizeof(*m));
}

void ir_error_free(ir_error_t *err) {
    if (err == NULL) {
        return;
    }
    free(err->msg);
    err->msg = NULL;
    err->line = 0;
}
