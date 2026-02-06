#ifndef LS_COLORS_H
#define LS_COLORS_H

#include <sys/stat.h>

// Initialize colors from LS_COLORS environment variable
void ls_colors_init(void);

// Get ANSI color code for a file based on its mode and name
const char *ls_colors_get(const char *name, mode_t mode);

// Get reset code
const char *ls_colors_reset(void);

#endif // LS_COLORS_H
