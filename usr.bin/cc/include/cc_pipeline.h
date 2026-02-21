#ifndef CC_PIPELINE_H
#define CC_PIPELINE_H

#include "cc_frontend.h"

int cc_compile_c_to_s(const char *in_c, const char *display_src, const char *out_s, int emit_debug, cc_diag_t *diag);

#endif
