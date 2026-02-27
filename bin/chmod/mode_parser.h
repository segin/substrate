#ifndef CHMOD_MODE_PARSER_H
#define CHMOD_MODE_PARSER_H

#include <stddef.h>
#include <sys/types.h>

struct chmod_mode;

/*
 * Parse a numeric or symbolic chmod mode string.
 * Returns NULL on parse failure and writes a short error into errbuf.
 */
struct chmod_mode *chmod_setmode(const char *mode_string, char *errbuf,
    size_t errbuf_len);

/*
 * Compute a new mode from old_mode using a parsed chmod mode descriptor.
 */
mode_t chmod_getmode(const struct chmod_mode *mode, mode_t old_mode);

/*
 * Release a parsed mode descriptor.
 */
void chmod_freemode(struct chmod_mode *mode);

#endif
