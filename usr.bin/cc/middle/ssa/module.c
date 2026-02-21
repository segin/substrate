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

    for (i = 0; i < m->func_count; ++i) {
        size_t j;
        free(m->funcs[i].name);
        free(m->funcs[i].param_values);
        free(m->funcs[i].param_types);
        free(m->funcs[i].value_types);
        for (j = 0; j < m->funcs[i].instr_count; ++j) {
            free(m->funcs[i].instrs[j].sym);
            free(m->funcs[i].instrs[j].args);
        }
        free(m->funcs[i].instrs);
    }
    free(m->funcs);
    memset(m, 0, sizeof(*m));
}
