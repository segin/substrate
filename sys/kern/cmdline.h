#ifndef _KERN_CMDLINE_H
#define _KERN_CMDLINE_H

#include <stddef.h>

void cmdline_init(const char *cmdline);
/* Non-zero once cmdline_init() has run.  Lets a caller distinguish "option
 * absent" from "asked too early", which matters when caching the answer. */
int cmdline_is_initialized(void);
int cmdline_has(const char *key);
int cmdline_last_index(const char *key);
int cmdline_get(const char *key, char *buf, size_t buf_len);
int cmdline_get_full(char *buf, size_t buf_len);
int cmdline_debug_enabled(const char *channel);

#endif
