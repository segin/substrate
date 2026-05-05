#ifndef MODEPARSE_H
#define MODEPARSE_H

#include <stddef.h>
#include <sys/types.h>

struct mode_change;

/*
 * Compile a numeric or symbolic mode expression for repeated application.
 * Returns NULL on parse failure and writes a short error into errbuf.
 */
struct mode_change *modeparse_compile(const char *mode_string, char *errbuf,
    size_t errbuf_len);

/*
 * Apply a compiled mode expression to old_mode.
 */
mode_t modeparse_apply(const struct mode_change *mode, mode_t old_mode);

/*
 * Release a compiled mode expression.
 */
void modeparse_free(struct mode_change *mode);

/*
 * Convenience wrapper for one-shot callers.
 * Returns 0 on success and -1 on parse failure.
 */
int parse_mode(const char *mode_string, mode_t old_mode, mode_t *out_mode,
    char *errbuf, size_t errbuf_len);

#endif