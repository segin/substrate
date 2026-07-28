#include <stddef.h>
#include <string.h>

#include "getopt.h"
#include "rm_opts.h"

enum {
    RM_OPT_INTERACTIVE = 256,
    RM_OPT_ONE_FILE_SYSTEM,
    RM_OPT_PRESERVE_ROOT,
    RM_OPT_NO_PRESERVE_ROOT,
    RM_OPT_HELP,
    RM_OPT_VERSION,
};

void
rm_options_init(struct rm_options *opts, const char *progname)
{
    memset(opts, 0, sizeof(*opts));
    opts->progname = (progname != NULL && progname[0] != '\0') ? progname :
        "rm";
    opts->preserve_root = true;
    opts->prompt_mode = RM_PROMPT_NEVER;
}

static int
parse_interactive_mode(struct rm_options *opts, const char *value,
    const char **err_msg)
{
    if (value == NULL || strcmp(value, "always") == 0) {
        opts->prompt_mode = RM_PROMPT_ALWAYS;
        opts->force = false;
        return 0;
    }
    if (strcmp(value, "once") == 0) {
        opts->prompt_mode = RM_PROMPT_ONCE;
        opts->force = false;
        return 0;
    }
    if (strcmp(value, "never") == 0) {
        opts->prompt_mode = RM_PROMPT_NEVER;
        return 0;
    }

    *err_msg = "invalid argument to --interactive (expected never, once, always)";
    return -1;
}

int
rm_parse_options(struct rm_options *opts, int argc, char **argv,
    const char **err_msg)
{
    static const struct option long_options[] = {
        { "force", no_argument, NULL, 'f' },
        { "interactive", optional_argument, NULL, RM_OPT_INTERACTIVE },
        { "recursive", no_argument, NULL, 'r' },
        { "dir", no_argument, NULL, 'd' },
        { "verbose", no_argument, NULL, 'v' },
        { "one-file-system", no_argument, NULL, RM_OPT_ONE_FILE_SYSTEM },
        { "preserve-root", optional_argument, NULL, RM_OPT_PRESERVE_ROOT },
        { "no-preserve-root", no_argument, NULL, RM_OPT_NO_PRESERVE_ROOT },
        { "help", no_argument, NULL, RM_OPT_HELP },
        { "version", no_argument, NULL, RM_OPT_VERSION },
        { NULL, 0, NULL, 0 },
    };
    int option;

    *err_msg = NULL;
    opterr = 0;
    optind = 1;

    while ((option = getopt_long(argc, argv, "+dfiIPrvRx", long_options,
                NULL)) != -1) {
        switch (option) {
        case 'd':
            opts->dir_mode = true;
            break;
        case 'f':
            opts->force = true;
            opts->prompt_mode = RM_PROMPT_NEVER;
            break;
        case 'i':
            opts->prompt_mode = RM_PROMPT_ALWAYS;
            opts->force = false;
            break;
        case 'I':
            opts->prompt_mode = RM_PROMPT_ONCE;
            opts->force = false;
            break;
        case 'r':
        case 'R':
            opts->recursive = true;
            break;
        case 'v':
            opts->verbose = true;
            break;
        case 'P':
            /* BSD: overwrite each file before unlinking. */
            opts->scrub = true;
            break;
        case 'x':
            /* BSD alias for --one-file-system. */
            opts->one_file_system = true;
            break;
        case RM_OPT_INTERACTIVE:
            if (parse_interactive_mode(opts, optarg, err_msg) != 0) {
                return -1;
            }
            break;
        case RM_OPT_ONE_FILE_SYSTEM:
            opts->one_file_system = true;
            break;
        case RM_OPT_PRESERVE_ROOT:
            opts->preserve_root = true;
            /* --preserve-root=all : also refuse to cross mount points
             * during recursion, regardless of --one-file-system. */
            if (optarg != NULL) {
                if (strcmp(optarg, "all") == 0) {
                    opts->preserve_root_all = true;
                    opts->one_file_system = true;
                } else {
                    *err_msg = "invalid argument to --preserve-root "
                               "(expected: all)";
                    return -1;
                }
            }
            break;
        case RM_OPT_NO_PRESERVE_ROOT:
            opts->preserve_root = false;
            opts->preserve_root_all = false;
            break;
        case RM_OPT_HELP:
            opts->show_help = true;
            break;
        case RM_OPT_VERSION:
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
    if (!opts->show_help && !opts->show_version && opts->operand_count == 0 &&
        !opts->force) {
        *err_msg = "missing operand";
        return -1;
    }

    return 0;
}