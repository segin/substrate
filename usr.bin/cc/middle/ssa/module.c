#include "cc_ssa.h"

#include <stdlib.h>
#include <string.h>

void cc_ssa_module_init(cc_ssa_module_t *m) {
    memset(m, 0, sizeof(*m));
}

void cc_ssa_module_free(cc_ssa_module_t *m) {
    size_t i;

    if (m == NULL) {
        return;
    }

    for (i = 0; i < m->global_count; ++i) {
        size_t j;
        free(m->globals[i].name);
        free(m->globals[i].attr_section);
        free(m->globals[i].attr_alias);
        free(m->globals[i].init_str);
        free(m->globals[i].init_sym);
        for (j = 0; j < m->globals[i].init_item_count; ++j) {
            free(m->globals[i].init_items[j].init_str);
            free(m->globals[i].init_items[j].init_sym);
        }
        free(m->globals[i].init_items);
    }
    free(m->globals);

    for (i = 0; i < m->func_count; ++i) {
        size_t j;
        free(m->funcs[i].name);
        free(m->funcs[i].attr_section);
        free(m->funcs[i].attr_alias);
        free(m->funcs[i].param_values);
        free(m->funcs[i].param_types);
        free(m->funcs[i].value_types);
        for (j = 0; j < m->funcs[i].instr_count; ++j) {
            size_t k;
            free(m->funcs[i].instrs[j].sym);
            free(m->funcs[i].instrs[j].args);
            free(m->funcs[i].instrs[j].asm_out_values);
            free(m->funcs[i].instrs[j].asm_in_values);
            if (m->funcs[i].instrs[j].asm_out_constraints != NULL) {
                for (k = 0; k < m->funcs[i].instrs[j].asm_out_count; ++k) {
                    free(m->funcs[i].instrs[j].asm_out_constraints[k]);
                }
            }
            free(m->funcs[i].instrs[j].asm_out_constraints);
            if (m->funcs[i].instrs[j].asm_out_names != NULL) {
                for (k = 0; k < m->funcs[i].instrs[j].asm_out_count; ++k) {
                    free(m->funcs[i].instrs[j].asm_out_names[k]);
                }
            }
            free(m->funcs[i].instrs[j].asm_out_names);
            if (m->funcs[i].instrs[j].asm_in_constraints != NULL) {
                for (k = 0; k < m->funcs[i].instrs[j].asm_in_count; ++k) {
                    free(m->funcs[i].instrs[j].asm_in_constraints[k]);
                }
            }
            free(m->funcs[i].instrs[j].asm_in_constraints);
            if (m->funcs[i].instrs[j].asm_in_names != NULL) {
                for (k = 0; k < m->funcs[i].instrs[j].asm_in_count; ++k) {
                    free(m->funcs[i].instrs[j].asm_in_names[k]);
                }
            }
            free(m->funcs[i].instrs[j].asm_in_names);
            if (m->funcs[i].instrs[j].asm_clobbers != NULL) {
                for (k = 0; k < m->funcs[i].instrs[j].asm_clobber_count; ++k) {
                    free(m->funcs[i].instrs[j].asm_clobbers[k]);
                }
            }
            free(m->funcs[i].instrs[j].asm_clobbers);
            free(m->funcs[i].instrs[j].asm_goto_labels);
            if (m->funcs[i].instrs[j].asm_goto_names != NULL) {
                for (k = 0; k < m->funcs[i].instrs[j].asm_goto_count; ++k) {
                    free(m->funcs[i].instrs[j].asm_goto_names[k]);
                }
            }
            free(m->funcs[i].instrs[j].asm_goto_names);
        }
        free(m->funcs[i].instrs);
    }
    free(m->funcs);
    memset(m, 0, sizeof(*m));
}
