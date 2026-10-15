#ifndef CC_PIPELINE_H
#define CC_PIPELINE_H

#include "cc_frontend.h"
#include "cc_options.h"

/**
 * Compile one preprocessed C file to assembly: parse, check, lower to
 * SSA, optimize, and emit GNU as syntax for the target in @p opts.
 *
 * @param in_c         the preprocessed source to read
 * @param display_src  the name to report in diagnostics and debug
 *                     info; NULL or "" for none
 * @param out_s        the assembly file to write
 * @param opts         the settings; they are selected for the
 *                     duration of the compilation
 * @param diag         receives the first error; may be NULL
 * @return             0, or -1 on error
 */
int cc_compile_c_to_s(const char *in_c, const char *display_src, const char *out_s, const cc_options_t *opts,
                      cc_diag_t *diag);

#endif
