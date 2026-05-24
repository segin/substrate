#ifndef RM_H
#define RM_H

#include <stdbool.h>
#include <signal.h>
#include <stdio.h>

#define RM_VERSION "rm (Substrate) 0.1"

enum rm_prompt_mode {
    RM_PROMPT_NEVER = 0,
    RM_PROMPT_ONCE,
    RM_PROMPT_ALWAYS,
};

struct rm_options {
    const char *progname;
    bool force;
    bool recursive;
    bool dir_mode;
    bool verbose;
    bool one_file_system;
    bool preserve_root;
    bool preserve_root_all;   /* --preserve-root=all */
    bool scrub;               /* BSD -P : multi-pass overwrite */
    bool show_help;
    bool show_version;
    enum rm_prompt_mode prompt_mode;
    int operand_start;
    int operand_count;
};

struct rm_walk_state {
    const struct rm_options *opts;
    FILE *prompt_input;
    volatile sig_atomic_t *interrupted;
};

enum rm_walk_result {
    RM_WALK_FAILED = -1,
    RM_WALK_REMOVED = 0,
    RM_WALK_SKIPPED = 1,
};

#endif