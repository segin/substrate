#ifndef ECHO_H
#define ECHO_H

#include <stdbool.h>

#define ECHO_VERSION "echo (Substrate) 0.1"

struct echo_options {
    const char *progname;
    bool no_newline;
    bool enable_escapes;
    bool show_help;
    bool show_version;
    bool posix_mode;
    int arg_index;
};

#endif