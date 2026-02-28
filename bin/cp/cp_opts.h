#ifndef CP_OPTS_H
#define CP_OPTS_H

#include <stddef.h>

#define CP_DEFAULT_BUFSIZE 65536u

enum cp_overwrite_mode {
    CP_OVERWRITE_FORCE = 0,
    CP_OVERWRITE_INTERACTIVE = 1,
    CP_OVERWRITE_NOCLOBBER = 2,
};

enum cp_link_mode {
    CP_LINKMODE_COPY = 0,
    CP_LINKMODE_HARD = 1,
    CP_LINKMODE_SYM = 2,
};

enum cp_symlink_mode {
    CP_SYMLINK_AUTO = 0,
    CP_SYMLINK_PHYSICAL = 1,
    CP_SYMLINK_FOLLOW_CMDLINE = 2,
    CP_SYMLINK_FOLLOW_ALL = 3,
};

enum cp_sparse_mode {
    CP_SPARSE_AUTO = 0,
    CP_SPARSE_ALWAYS = 1,
    CP_SPARSE_NEVER = 2,
};

enum cp_non_tty_prompt_default {
    CP_PROMPT_DEFAULT_NO = 0,
    CP_PROMPT_DEFAULT_YES = 1,
};

enum cp_backup_control {
    CP_BACKUP_NONE = 0,
    CP_BACKUP_SIMPLE = 1,
    CP_BACKUP_NUMBERED = 2,
    CP_BACKUP_EXISTING = 3,
};

enum cp_reflink_mode {
    CP_REFLINK_NEVER = 0,
    CP_REFLINK_AUTO = 1,
    CP_REFLINK_ALWAYS = 2,
};

struct cp_options {
    int recursive;
    enum cp_symlink_mode symlink_mode;
    enum cp_overwrite_mode overwrite_mode;
    enum cp_link_mode link_mode;
    enum cp_sparse_mode sparse_mode;

    int preserve_mode;
    int preserve_owner;
    int preserve_timestamps;
    int preserve_links;
    int preserve_xattr;
    int preserve_acl;
    int preserve_flags;
    int preserve_all;

    int archive;
    int force_silent;
    int verbose;
    int progress;
    int atomic_replace;
    int remove_destination;
    int update_only;

    enum cp_non_tty_prompt_default non_tty_default;
    enum cp_backup_control backup_control;
    const char *backup_suffix;
    enum cp_reflink_mode reflink_mode;

    size_t buffer_size;
    int buffer_size_explicit;

    const char *target_directory;
    int no_target_directory;

    int show_help;
    int show_version;

    int source_start;
    int source_count;
    const char *dest;
};

void cp_options_init(struct cp_options *opts);

int cp_parse_options(struct cp_options *opts, int argc, char **argv,
                     const char **err_msg);

int cp_parse_size(const char *text, size_t *out_size, const char **err_msg);

const char *cp_options_usage(const char *progname);

#endif
