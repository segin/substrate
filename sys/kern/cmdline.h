#ifndef _KERN_CMDLINE_H
#define _KERN_CMDLINE_H

#include <stddef.h>

void cmdline_init(const char *cmdline);
int cmdline_has(const char *key);
int cmdline_get(const char *key, char *buf, size_t buf_len);
int cmdline_get_full(char *buf, size_t buf_len);
int cmdline_debug_enabled(const char *channel);

#endif
