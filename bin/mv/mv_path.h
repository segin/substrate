#ifndef BIN_MV_MV_PATH_H
#define BIN_MV_MV_PATH_H

#include <stdbool.h>
#include <stddef.h>

/* Strip trailing slashes from a writable string, in place.  Keeps a
 * single leading slash so "/" stays "/". */
void mv_path_strip_trailing_slashes(char *path);

/* Read-only basename — points into `path`; never NULL for non-NULL
 * input. */
const char *mv_path_basename(const char *path);

/* Join two path components with exactly one '/' between them.  Caller
 * frees.  Returns NULL on malloc failure. */
char *mv_path_join(const char *dir, const char *name);

/* True iff `a` and `b` resolve to the same on-disk object.  Uses
 * lstat dev/ino comparison (lighter than realpath; matches BSD). */
bool mv_path_same_file(const char *a, const char *b);

#endif
