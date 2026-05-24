/*
 * bin/mv/mv.h — shared types for the modular mv implementation.
 *
 * Implements POSIX.1-2024 mv(1) plus BSD and GNU extensions per
 * docs/specs/mv-rm-spec.md.  Where GNU and BSD diverge, BSD wins.
 */
#ifndef BIN_MV_MV_H
#define BIN_MV_MV_H

#include <stdbool.h>
#include <stddef.h>

enum mv_prompt_mode {
    MV_PROMPT_FORCE     = 0,    /* -f: silently overwrite (POSIX default unless tty + writeable check) */
    MV_PROMPT_INTERACTIVE = 1,  /* -i: prompt before overwrite */
    MV_PROMPT_NOCLOBBER = 2,    /* -n: never overwrite, no prompt */
};

enum mv_backup_mode {
    MV_BACKUP_NONE      = 0,    /* default: no backups */
    MV_BACKUP_SIMPLE    = 1,    /* TARGET~ */
    MV_BACKUP_NUMBERED  = 2,    /* TARGET.~N~ */
    MV_BACKUP_EXISTING  = 3,    /* numbered iff numbered exist, else simple */
};

enum mv_update_mode {
    MV_UPDATE_ALL       = 0,    /* default: always overwrite */
    MV_UPDATE_OLDER     = 1,    /* --update / --update=older: only if src newer */
    MV_UPDATE_NONE      = 2,    /* --update=none: never overwrite (silent) */
    MV_UPDATE_NONE_FAIL = 3,    /* --update=none-fail: never overwrite, error */
};

struct mv_options {
    enum mv_prompt_mode prompt;
    enum mv_backup_mode backup;
    enum mv_update_mode update;
    const char *backup_suffix;
    const char *target_directory;   /* -t DIR; NULL if not set */
    bool no_target_directory;       /* -T */
    bool strip_trailing_slashes;    /* --strip-trailing-slashes */
    bool symlink_target_as_self;    /* BSD -h */
    bool verbose;
    bool debug;
    const char *progname;
};

void mv_options_init(struct mv_options *opts, const char *progname);
int  mv_parse_options(struct mv_options *opts, int argc, char **argv,
                      int *first_operand_index);

/* mv_rename.c */
int  mv_rename_one(const char *src, const char *dst,
                   const struct mv_options *opts);

#endif
