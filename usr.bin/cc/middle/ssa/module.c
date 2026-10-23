#include "cc_ssa.h"

#include <stdlib.h>
#include <string.h>

void cc_ssa_module_init(cc_ssa_module_t *m) {
    memset(m, 0, sizeof(*m));
}

void cc_ssa_global_free(cc_ssa_global_t *g) {
    size_t j;

    if (g == NULL) {
        return;
    }
    free(g->name);
    free(g->attr_section);
    free(g->attr_alias);
    free(g->init_str);
    free(g->init_sym);
    for (j = 0; j < g->init_item_count; ++j) {
        free(g->init_items[j].init_str);
        free(g->init_items[j].init_sym);
    }
    free(g->init_items);
    memset(g, 0, sizeof(*g));
}

void cc_ssa_module_free(cc_ssa_module_t *m) {
    size_t i;

    if (m == NULL) {
        return;
    }

    for (i = 0; i < m->global_count; ++i) {
        cc_ssa_global_free(&m->globals[i]);
    }
    free(m->globals);

    for (i = 0; i < m->func_count; ++i) {
        cc_ssa_function_free(&m->funcs[i]);
    }
    free(m->funcs);
    for (i = 0; i < m->toplevel_asm_count; ++i) {
        free(m->toplevel_asm[i]);
    }
    free(m->toplevel_asm);
    for (i = 0; i < m->local_decl_count; ++i) {
        free(m->local_decls[i]);
    }
    free(m->local_decls);
    memset(m, 0, sizeof(*m));
}
