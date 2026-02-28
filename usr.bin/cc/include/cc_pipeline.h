#ifndef CC_PIPELINE_H
#define CC_PIPELINE_H

#include "cc_frontend.h"
#include "cc_target.h"

int cc_compile_c_to_s(const char *in_c, const char *display_src, const char *out_s, const char *std_mode,
                      int emit_debug, cc_target_t target, int opt_level, int wall, int werror, int pedantic,
                      int pedantic_errors, int gnu89_inline_mode, int gnu89_inline_override, int i386_isa_level,
                      int i386_has_sse2, int i386_has_mmx, int i386_fp_math_mode,
                      cc_diag_t *diag);

#endif
