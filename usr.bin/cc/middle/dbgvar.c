/*
 * dbgvar.c: what lowering records of a function's variables for debug
 * information (backend/dwarf.c): each declared variable, and the stack
 * allocation that is its storage.
 */

#include "cc_lower.h"
#include "cc_options.h"
#include "cc_ssa.h"
#include "cc_util.h"

#include <stdlib.h>
#include <string.h>

/**
 * Record a declared variable for debug information and mark its storage
 * (see cc_lower.h).
 *
 * @return 0, or -1 when out of memory
 */
int lower_dbg_note_var(cc_ssa_module_t *m, const cc_function_t *fn, cc_ssa_function_t *sf, const var_entry_t *v,
                       const char *name, const cc_ctype_t *ctype, const struct cc_dtype *dtype, size_t line,
                       int src_file, int is_param, int is_extern, const char *sym) {
    cc_ssa_dbg_var_t d;
    int id;

    if (cc_options_current() == NULL || !cc_options_current()->debug || m == NULL || name == NULL) {
        return 0;
    }
    memset(&d, 0, sizeof(d));
    d.fn = fn;
    d.name = name;
    d.ctype = ctype;
    d.dtype = dtype;
    d.line = line;
    d.src_file = src_file;
    d.is_param = (unsigned char)(is_param != 0);
    d.is_extern = (unsigned char)(is_extern != 0);
    if (sym != NULL && (d.sym = cc_strdup(sym)) == NULL) {
        return -1;
    }
    id = cc_ssa_module_add_dbg_var(m, &d);
    if (id == 0) {
        return -1;
    }
    if (sf != NULL && v != NULL && v->addr_value >= 0) {
        size_t i = sf->instr_count;

        /* its allocation, made just now as a rule; a hoisted one at the function's entry */
        while (i > 0) {
            cc_ssa_instr_t *in = &sf->instrs[--i];

            if (in->dst == v->addr_value) {
                if (in->op == CC_SSA_STACKALLOC && in->dbg_var == 0) {
                    in->dbg_var = id;
                }
                break;
            }
        }
    }
    return 0;
}
