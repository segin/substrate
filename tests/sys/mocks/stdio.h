#ifndef _MOCK_STDIO_H
#define _MOCK_STDIO_H

// Minimal mock for stdio.h to satisfy vfs.c requirements
// without pulling in conflicting host types.

int sprintf(char *str, const char *format, ...);

// If vfs.c uses NULL, we need it.
#ifndef NULL
#define NULL ((void*)0)
#endif

#endif
