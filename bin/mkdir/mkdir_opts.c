#include <stddef.h>
#include <string.h>

#include "mkdir_opts.h"

void
mkdir_options_init(struct mkdir_options *opts, const char *progname)
{
    memset(opts, 0, sizeof(*opts));
    opts->progname = (progname != NULL && progname[0] != '\0') ? progname :
        "mkdir";
}

static int
parse_long_option(struct mkdir_options *opts, const char *arg, int argc,
    char **argv, int *index, const char **err_msg)
{
    const char *name;
    const char *eq;
    size_t name_len;

    (void)argc;
    (void)argv;
    (void)index;

    name = arg + 2;
    eq = strchr(name, '=');
    name_len = eq ? (size_t)(eq - name) : strlen(name);

    if (name_len == 7 && strncmp(name, "parents", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--parents does not accept an argument";
            return -1;
        }
        opts->parents = true;
        return 0;
    }
    if (name_len == 7 && strncmp(name, "verbose", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--verbose does not accept an argument";
            return -1;
        }
        opts->verbose = true;
        return 0;
    }
    if (name_len == 4 && strncmp(name, "help", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--help does not accept an argument";
            return -1;
        }
        opts->show_help = true;
        return 0;
    }
    if (name_len == 7 && strncmp(name, "version", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--version does not accept an argument";
            return -1;
        }
        opts->show_version = true;
        return 0;
    }
    if (name_len == 4 && strncmp(name, "mode", name_len) == 0) {
        if (eq == NULL || eq[1] == '\0') {
            *err_msg = "--mode requires an argument";
            return -1;
        }
        opts->have_mode = true;
        opts->mode_string = eq + 1;
        return 0;
    }
    if (name_len == 7 && strncmp(name, "context", name_len) == 0) {
        opts->selinux_context_requested = true;
        opts->selinux_context = (eq != NULL) ? (eq + 1) : NULL;
        return 0;
    }

    *err_msg = "invalid option";
    return -1;
}

static int
parse_short_options(struct mkdir_options *opts, const char *arg, int argc,
    char **argv, int *index, const char **err_msg)
{
    size_t j;

    for (j = 1; arg[j] != '\0'; ++j) {
        switch (arg[j]) {
        case 'p':
            opts->parents = true;
            break;
        case 'v':
            opts->verbose = true;
            break;
        case 'm':
            opts->have_mode = true;
            if (arg[j + 1] != '\0') {
                opts->mode_string = &arg[j + 1];
                return 0;
            }
            if (*index + 1 >= argc) {
                *err_msg = "option requires an argument -- 'm'";
                return -1;
            }
            ++(*index);
            opts->mode_string = argv[*index];
            return 0;
        case 'Z':
            opts->selinux_context_requested = true;
            if (arg[j + 1] != '\0') {
                opts->selinux_context = &arg[j + 1];
            }
            return 0;
        default:
            *err_msg = "invalid option";
            return -1;
        }
    }

    return 0;
}

int
mkdir_parse_options(struct mkdir_options *opts, int argc, char **argv,
    const char **err_msg)
{
    int index;
    bool end_of_options = false;

    *err_msg = NULL;
    for (index = 1; index < argc; ++index) {
        const char *arg = argv[index];

        if (end_of_options || arg[0] != '-' || arg[1] == '\0') {
            break;
        }
        if (strcmp(arg, "--") == 0) {
            end_of_options = true;
            continue;
        }
        if (arg[1] == '-') {
            if (parse_long_option(opts, arg, argc, argv, &index, err_msg) != 0) {
                return -1;
            }
            continue;
        }
        if (parse_short_options(opts, arg, argc, argv, &index, err_msg) != 0) {
            return -1;
        }
    }

    opts->operand_start = index;
    opts->operand_count = argc - index;

    if (!opts->show_help && !opts->show_version && opts->operand_count == 0) {
        *err_msg = "missing operand";
        return -1;
    }

    return 0;
}