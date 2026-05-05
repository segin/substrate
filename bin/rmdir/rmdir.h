#ifndef RMDIR_H
#define RMDIR_H

#include <stdbool.h>

#define RMDIR_VERSION "rmdir (Substrate) 0.1"

struct rmdir_options {
    const char *progname;
    bool parents;
    bool verbose;
    bool ignore_fail_on_non_empty;
    bool show_help;
    bool show_version;
    int operand_start;
    int operand_count;
};

enum rmdir_result {
    RMDIR_RESULT_FAILED = -1,
    RMDIR_RESULT_REMOVED = 0,
    RMDIR_RESULT_STOP = 1,
};

#endif