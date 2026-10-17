#ifndef CC_MIDDLE_H
#define CC_MIDDLE_H

#include "cc_frontend.h"
#include "cc_ssa.h"

int cc_run_middle_passes(cc_ssa_module_t *m, int opt_level, cc_diag_t *diag);
int cc_middle_legalize_module(cc_ssa_module_t *m, cc_diag_t *diag);

/**
 * i386 legalization of one function (legalize_i386.c): every operation on
 * a 64-bit integer becomes operations on its two 32-bit words, and a
 * 64-bit division a call of libgcc's __divdi3 or __udivdi3.
 *
 * @param f     the function, rewritten in place
 * @param diag  receives an error
 * @return      0, or -1 when out of memory
 */
int cc_legalize_i386_function(cc_ssa_function_t *f, cc_diag_t *diag);

#endif
