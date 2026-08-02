#include <stdio.h>

#include "rmdir.h"
#include "rmdir_opts.h"
#include "rmdir_parents.h"

static void
rmdir_print_usage(FILE *stream, const char *progname)
{
    fprintf(stream,
        "Usage: %s [OPTION]... DIRECTORY...\n"
        "Remove empty DIRECTORY(ies).\n",
        progname);
}

static void
rmdir_print_help(const char *progname)
{
    rmdir_print_usage(stdout, progname);
    fputs(
        "\n"
        "Options:\n"
        "  -p, --parents                  remove DIRECTORY and its ancestors\n"
        "  -v, --verbose                  output a diagnostic for every directory removed\n"
        "      --ignore-fail-on-non-empty ignore failures caused by non-empty directories\n"
        "      --help                     display this help and exit\n"
        "      --version                  output version information and exit\n",
        stdout);
}

static void
rmdir_print_version(void)
{
    puts(RMDIR_VERSION);
}

int
main(int argc, char *argv[])
{
    const char *err_msg;
    struct rmdir_options opts;
    int index;
    int status;

    err_msg = NULL;
    status = 0;

    rmdir_options_init(&opts, argv[0]);
    if (rmdir_parse_options(&opts, argc, argv, &err_msg) != 0) {
        rmdir_print_usage(stderr, opts.progname);
        if (err_msg != NULL) {
            fprintf(stderr, "%s: %s\n", opts.progname, err_msg);
        }
        return 1;
    }
    if (opts.show_help) {
        rmdir_print_help(opts.progname);
        return 0;
    }
    if (opts.show_version) {
        rmdir_print_version();
        return 0;
    }

    for (index = opts.operand_start; index < argc; ++index) {
        if (rmdir_remove_path(&opts, argv[index]) != RMDIR_RESULT_REMOVED) {
            status = 1;
        }
    }
    return status;
}

