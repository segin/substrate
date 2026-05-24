#ifndef BIN_MV_MV_BACKUP_H
#define BIN_MV_MV_BACKUP_H

#include "mv.h"

/* Compute the backup name for TARGET under the given CONTROL and
 * SUFFIX.  Returns a malloc'd path the caller must free, or NULL
 * if no backup is required (MV_BACKUP_NONE or TARGET nonexistent).
 * On allocation failure also returns NULL. */
char *mv_backup_name(const char *target,
                     enum mv_backup_mode mode,
                     const char *suffix);

/* Move TARGET to its backup name (rename(2)).  Returns 0 on success
 * or when no backup was required, -1 on failure with errno set.
 * If `backup_path_out` is non-NULL, the chosen path is returned via
 * it (caller frees) for diagnostic use. */
int mv_perform_backup(const char *target,
                      const struct mv_options *opts,
                      char **backup_path_out);

#endif
