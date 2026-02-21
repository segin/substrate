#include "ir.h"

#include <stdio.h>

int ir_serialize_module(const ir_module_t *m, FILE *out, int normalize_ids) {
    size_t i;

    (void)normalize_ids;

    if (m == NULL || out == NULL) {
        return -1;
    }

    fprintf(out, "module \"%s\" {\n", m->name ? m->name : "");
    if (m->target != NULL) {
        fprintf(out, "  target %s;\n", m->target);
    }

    for (i = 0; i < m->func_count; ++i) {
        size_t b;
        const ir_func_t *f = &m->funcs[i];

        fprintf(out, "\n  func @%s {\n", f->name ? f->name : "");
        for (b = 0; b < f->block_count; ++b) {
            size_t in;
            const ir_block_t *blk = &f->blocks[b];
            fprintf(out, "  %s:\n", blk->name ? blk->name : "");
            for (in = 0; in < blk->instr_count; ++in) {
                fprintf(out, "    %s;\n", blk->instrs[in].text ? blk->instrs[in].text : "");
            }
        }
        fprintf(out, "  }\n");
    }

    fprintf(out, "}\n");
    return ferror(out) ? -1 : 0;
}
