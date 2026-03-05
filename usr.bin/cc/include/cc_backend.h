#ifndef CC_BACKEND_H
#define CC_BACKEND_H

#include "cc_frontend.h"
#include "cc_mir.h"
#include "cc_ssa.h"
#include "cc_target.h"

int cc_emit_gas(const cc_ssa_module_t *m, const char *path, const char *src_path,
                int emit_debug, cc_target_t target, int pic, cc_diag_t *diag);
int cc_backend_pick_spill_victim(const int *reg_values, const int *next_use, const unsigned char *dirty,
                                 int reg_count, int avoid, int prefer);
int cc_backend_checked_frame_add(int *raw_frame, int bytes, cc_diag_t *diag, const char *context);
int cc_backend_align_frame_size(int raw_frame, int stack_align);
void cc_backend_set_i386_isa_level(int level);
void cc_backend_set_i386_sse2(int enabled);
void cc_backend_set_i386_mmx(int enabled);
void cc_backend_set_i386_fp_math_mode(int mode);

#endif
