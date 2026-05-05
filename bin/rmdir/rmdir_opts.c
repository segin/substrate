#include "rmdir_opts.h"

#include <getopt.h>
#include <string.h>

enum {
    RMDIR_OPT_IGNORE_FAIL_ON_NON_EMPTY = 256,
    RMDIR_OPT_HELP,
    RMDIR_OPT_VERSION,
};

void
rmdir_options_init(struct rmdir_options *opts, const char *progname)
{
    memset(opts, 0, sizeof(*opts));
    opts->progname = (progname != NULL && progname[0] != '\0') ? progname :
        "rmdir";
}

int
rmdir_parse_options(struct rmdir_options *opts, int argc, char **argv,
    const char **err_msg)
{
    static const struct option long_options[] = {
        { "parents", no_argument, NULL, 'p' },
        { "verbose", no_argument, NULL, 'v' },
        { "ignore-fail-on-non-empty", no_argument, NULL,
            RMDIR_OPT_IGNORE_FAIL_ON_NON_EMPTY },
        { "help", no_argument, NULL, RMDIR_OPT_HELP },
        { "version", no_argument, NULL, RMDIR_OPT_VERSION },
        { NULL, 0, NULL, 0 },
    };
    int option;

    *err_msg = NULL;
    opterr = 0;
    optind = 1;

    while ((option = getopt_long(argc, argv, "+pv", long_options,
                NULL)) != -1) {
        switch (option) {
        case 'p':
            opts->parents = true;
            break;
        case 'v':
            opts->verbose = true;
            break;
        case RMDIR_OPT_IGNORE_FAIL_ON_NON_EMPTY:
            opts->ignore_fail_on_non_empty = true;
            break;
        case RMDIR_OPT_HELP:
            opts->show_help = true;
            break;
        case RMDIR_OPT_VERSION:
            opts->show_version = true;
            break;
        case '?':
        default:
            *err_msg = "invalid option";
            return -1;
        }
    }

    opts->operand_start = optind;
    opts->operand_count = argc - optind;
    if (!opts->show_help && !opts->show_version && opts->operand_count == 0) {
        *err_msg = "missing operand";
        return -1;
    }
    return 0;
}