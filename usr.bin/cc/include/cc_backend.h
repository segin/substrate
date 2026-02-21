#ifndef CC_BACKEND_H
#define CC_BACKEND_H

#include "cc_frontend.h"
#include "cc_ssa.h"
#include "cc_target.h"

int cc_emit_gas(const cc_ssa_module_t *m, const char *path, const char *src_path,
                int emit_debug, cc_target_t target, cc_diag_t *diag);

#endif
