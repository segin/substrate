#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

// Basic stdio definitions for mocks
// If compiling for host, these will link against libc
int sprintf(char *str, const char *format, ...);
int printf(const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);

#endif
