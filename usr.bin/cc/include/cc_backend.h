#ifndef CC_BACKEND_H
#define CC_BACKEND_H

#include "cc_frontend.h"
#include "cc_ssa.h"
#include "cc_target.h"

/**
 * Write a legalized module as GNU assembler source.
 *
 * @param m           the module
 * @param path        the assembly file to write
 * @param src_path    the source file, for debug info; may be NULL
 * @param emit_debug  write line information
 * @param tgt         the target, whose emitter writes the code
 * @param pic         position-independent code
 * @param diag        receives the first error
 * @return            0, or -1
 */
int cc_emit_gas(const cc_ssa_module_t *m, const char *path, const char *src_path,
                int emit_debug, const cc_target_desc_t *tgt, int pic, cc_diag_t *diag);
int cc_backend_pick_spill_victim(const int *reg_values, const int *next_use, const unsigned char *dirty,
                                 int reg_count, int avoid, int prefer);
int cc_backend_checked_frame_add(int *raw_frame, int bytes, cc_diag_t *diag, const char *context);
int cc_backend_align_frame_size(int raw_frame, int stack_align);

#endif
