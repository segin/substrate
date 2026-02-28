#ifndef CP_PATH_H
#define CP_PATH_H

#include <stddef.h>

char *cp_path_join(const char *left, const char *right);
char *cp_path_dirname(const char *path);
const char *cp_path_basename(const char *path);
int cp_path_is_dot_or_dotdot(const char *name);

#endif
