#ifndef BIN_MV_MV_PROMPT_H
#define BIN_MV_MV_PROMPT_H

#include <stdbool.h>

/* Read a yes/no answer from stdin.  Returns true iff the response
 * begins with 'y' or 'Y'.  Returns false on EOF or any other answer.
 * The prompt is written to stderr. */
bool mv_prompt_yn(const char *fmt, ...);

#endif
