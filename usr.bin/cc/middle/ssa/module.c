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
        cc_ssa_function_free(&m->funcs[i]);
    }
    free(m->funcs);
    memset(m, 0, sizeof(*m));
}
