#ifndef MKDIR_H
#define MKDIR_H

#include <stdbool.h>
#include <sys/types.h>

#define MKDIR_VERSION "mkdir (Substrate) 0.1"

struct mkdir_options {
    const char *progname;
    bool parents;
    bool verbose;
    bool show_help;
    bool show_version;
    bool have_mode;
    bool selinux_context_requested;
    const char *mode_string;
    const char *selinux_context;
    int operand_start;
    int operand_count;
};

void mkdir_options_init(struct mkdir_options *opts, const char *progname);
int mkdir_parse_options(struct mkdir_options *opts, int argc, char **argv,
    const char **err_msg);

int mkdir_create_parents(const struct mkdir_options *opts, const char *path,
    mode_t create_mode, bool apply_final_mode, mode_t final_mode,
    char **error_path, int *error_errno);

#endif