#ifndef LS_COLORS_H
#define LS_COLORS_H

#include <sys/stat.h>

void ls_colors_init(void);
const char *ls_colors_get(const char *name, mode_t mode);
const char *ls_colors_reset(void);

#endif
