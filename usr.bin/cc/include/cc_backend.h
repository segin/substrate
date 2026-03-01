#ifndef CC_BACKEND_H
#define CC_BACKEND_H

#include "cc_frontend.h"
#include "cc_mir.h"
#include "cc_ssa.h"
#include "cc_target.h"

int cc_emit_gas(const cc_ssa_module_t *m, const char *path, const char *src_path,
                int emit_debug, cc_target_t target, int pic, cc_diag_t *diag);
void cc_backend_set_i386_isa_level(int level);
void cc_backend_set_i386_sse2(int enabled);
void cc_backend_set_i386_mmx(int enabled);
void cc_backend_set_i386_fp_math_mode(int mode);

#endif
