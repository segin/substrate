#ifndef CC_MIDDLE_H
#define CC_MIDDLE_H

#include "cc_frontend.h"
#include "cc_ssa.h"

int cc_run_middle_passes(cc_ssa_module_t *m, int opt_level, cc_diag_t *diag);
int cc_middle_legalize_module(cc_ssa_module_t *m, cc_diag_t *diag);

#endif
