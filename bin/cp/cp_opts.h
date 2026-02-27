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
    int progress;
    int atomic_replace;

    enum cp_non_tty_prompt_default non_tty_default;

    size_t buffer_size;
    int buffer_size_explicit;

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
