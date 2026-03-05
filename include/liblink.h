#ifndef LIBLINK_H
#define LIBLINK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LN_DEREF_LOGICAL = 0,
    LN_DEREF_PHYSICAL = 1
} ln_source_deref_mode_t;

typedef enum {
    LN_REPLACE_DEFAULT = 0,
    LN_REPLACE_FORCE = 1,
    LN_REPLACE_INTERACTIVE = 2
} ln_replace_mode_t;

typedef enum {
    LN_BACKUP_NONE = 0,
    LN_BACKUP_SIMPLE,
    LN_BACKUP_NUMBERED,
    LN_BACKUP_EXISTING
} ln_backup_mode_t;

typedef struct {
    const char *progname;

    bool symbolic;
    bool verbose;
    bool warn_missing;

    bool no_target_deref;
    bool bsd_remove_target_dir;

    bool relative;
    bool allow_hardlink_dir;

    bool no_target_directory;
    const char *target_directory;

    ln_source_deref_mode_t source_deref;
    ln_replace_mode_t replace_mode;

    ln_backup_mode_t backup_mode;
    const char *backup_suffix;
} ln_options_t;

void ln_options_init(ln_options_t *opts, const char *progname);
const char *ln_usage(void);
int ln_parse_options(ln_options_t *opts, int argc, char **argv, int *operand_index, int *show_help);
int ln_execute(const ln_options_t *opts, int argc, char **argv, int operand_index);

#ifdef __cplusplus
}
#endif

#endif
