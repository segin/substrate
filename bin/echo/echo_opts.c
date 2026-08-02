#include <stdlib.h>
#include <string.h>

#include "echo_opts.h"

static int
echo_parse_short_bundle(const char *arg, struct echo_options *options)
{
    size_t index;
    bool no_newline;
    bool enable_escapes;

    no_newline = options->no_newline;
    enable_escapes = options->enable_escapes;
    for (index = 1; arg[index] != '\0'; ++index) {
        switch (arg[index]) {
        case 'n':
            no_newline = true;
            break;
        case 'e':
            enable_escapes = true;
            break;
        case 'E':
            enable_escapes = false;
            break;
        default:
            return 0;
        }
    }
    options->no_newline = no_newline;
    options->enable_escapes = enable_escapes;
    return 1;
}

void
echo_options_init(struct echo_options *options, const char *progname)
{
    memset(options, 0, sizeof(*options));
    options->progname = (progname != NULL && progname[0] != '\0') ? progname :
        "echo";
    options->posix_mode = getenv("POSIXLY_CORRECT") != NULL;
    options->arg_index = 1;
}

int
echo_parse_options(struct echo_options *options, int argc, char **argv)
{
    int index;

    for (index = 1; index < argc; ++index) {
        const char *arg;

        arg = argv[index];
        if (options->posix_mode) {
            if (strcmp(arg, "-n") == 0) {
                options->no_newline = true;
                continue;
            }
            break;
        }
        if (strcmp(arg, "--help") == 0) {
            options->show_help = true;
            ++index;
            break;
        }
        if (strcmp(arg, "--version") == 0) {
            options->show_version = true;
            ++index;
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            break;
        }
        if (!echo_parse_short_bundle(arg, options)) {
            break;
        }
    }

    options->arg_index = index;
    return 0;
}