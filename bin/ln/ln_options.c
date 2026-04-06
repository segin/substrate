#include "ln.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char g_ln_usage[] =
    "Usage: ln [OPTION]... [-T] TARGET LINK_NAME\n"
    "       ln [OPTION]... TARGET\n"
    "       ln [OPTION]... TARGET... DIRECTORY\n"
    "       ln [OPTION]... -t DIRECTORY TARGET...\n"
    "\n"
    "Options:\n"
    "  -f, --force                  remove existing destination\n"
    "  -i, --interactive            prompt before overwrite\n"
    "  -s, --symbolic               make symbolic links\n"
    "  -L, --logical                dereference SOURCE symlinks (hard links)\n"
    "  -P, --physical               hard-link SOURCE symlink itself\n"
    "  -h, -n, --no-dereference     do not follow destination symlink-to-dir\n"
    "  -F                           with -s, remove destination directory\n"
    "  -w                           with -s, warn if SOURCE does not exist\n"
    "  -v, --verbose                print each link action\n"
    "  -b, --backup[=METHOD]        make destination backups\n"
    "  -S, --suffix=SUFFIX          backup suffix\n"
    "  -t, --target-directory=DIR   use DIR for all destination paths\n"
    "  -T, --no-target-directory    treat destination as a normal file\n"
    "  -r, --relative               with -s, create relative symlink targets\n"
    "  -d, --directory              attempt hard links to directories\n"
    "      --help                   display this help and exit\n";

struct ln_long_opt {
    const char *name;
    int arg_mode;
    char short_opt;
};

static const struct ln_long_opt g_long_opts[] = {
    {"help", 0, '\0'},
    {"backup", 2, 'b'},
    {"suffix", 1, 'S'},
    {"target-directory", 1, 't'},
    {"no-target-directory", 0, 'T'},
    {"relative", 0, 'r'},
    {"directory", 0, 'd'},
    {"force", 0, 'f'},
    {"interactive", 0, 'i'},
    {"symbolic", 0, 's'},
    {"logical", 0, 'L'},
    {"physical", 0, 'P'},
    {"no-dereference", 0, 'h'},
    {"verbose", 0, 'v'},
    {NULL, 0, 0}
};

void
ln_diag(const ln_options_t *opts, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", opts->progname ? opts->progname : "ln");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void
ln_diag_errno(const ln_options_t *opts, const char *path, const char *action)
{
    ln_diag(opts, "%s: %s: %s", path, action, strerror(errno));
}

void
ln_options_init(ln_options_t *opts, const char *progname)
{
    if (!opts) {
        return;
    }

    memset(opts, 0, sizeof(*opts));
    opts->progname = progname ? progname : "ln";
    opts->source_deref = LN_DEREF_LOGICAL;
    opts->replace_mode = LN_REPLACE_DEFAULT;
    opts->backup_mode = LN_BACKUP_NONE;
}

const char *
ln_usage(void)
{
    return g_ln_usage;
}

static int
ln_apply_option(ln_options_t *opts, char opt, const char *optval,
                bool *seen_force, bool *seen_interactive)
{
    switch (opt) {
    case 'b':
        return ln_apply_backup_option(opts, optval);
    case 'S':
        if (!optval) {
            ln_diag(opts, "-S/--suffix requires an argument");
            return -1;
        }
        opts->backup_suffix = optval;
        return 0;
    case 't':
        if (!optval) {
            ln_diag(opts, "-t/--target-directory requires an argument");
            return -1;
        }
        opts->target_directory = optval;
        return 0;
    case 'T':
        opts->no_target_directory = true;
        return 0;
    case 'r':
        opts->relative = true;
        return 0;
    case 'd':
        opts->allow_hardlink_dir = true;
        return 0;
    case 'F':
        opts->bsd_remove_target_dir = true;
        return 0;
    case 'f':
        opts->replace_mode = LN_REPLACE_FORCE;
        opts->warn_missing = false;
        *seen_force = true;
        return 0;
    case 'i':
        opts->replace_mode = LN_REPLACE_INTERACTIVE;
        *seen_interactive = true;
        return 0;
    case 'h':
    case 'n':
        opts->no_target_deref = true;
        return 0;
    case 'L':
        opts->source_deref = LN_DEREF_LOGICAL;
        return 0;
    case 'P':
        opts->source_deref = LN_DEREF_PHYSICAL;
        return 0;
    case 's':
        opts->symbolic = true;
        return 0;
    case 'v':
        opts->verbose = true;
        return 0;
    case 'w':
        opts->warn_missing = true;
        return 0;
    default:
        ln_diag(opts, "invalid option -- '%c'", opt);
        return -1;
    }
}

static int
ln_parse_long_option(ln_options_t *opts, const char *arg, int argc, char **argv, int *index,
                     int *show_help, bool *seen_force, bool *seen_interactive)
{
    const char *name;
    const char *eq;
    const char *value;
    size_t name_len;
    int i;

    name = arg + 2;
    eq = strchr(name, '=');
    name_len = eq ? (size_t)(eq - name) : strlen(name);
    value = eq ? (eq + 1) : NULL;

    for (i = 0; g_long_opts[i].name != NULL; ++i) {
        const struct ln_long_opt *opt = &g_long_opts[i];

        if (name_len != strlen(opt->name) ||
            strncmp(name, opt->name, name_len) != 0) {
            continue;
        }

        if (strcmp(opt->name, "help") == 0) {
            if (value) {
                ln_diag(opts, "--help does not accept an argument");
                return -1;
            }
            *show_help = 1;
            return 1;
        }

        if (opt->arg_mode == 0) {
            if (value) {
                ln_diag(opts, "--%s does not accept an argument", opt->name);
                return -1;
            }
            value = NULL;
        } else if (opt->arg_mode == 1 && !value) {
            if (*index + 1 >= argc) {
                ln_diag(opts, "--%s requires an argument", opt->name);
                return -1;
            }
            ++(*index);
            value = argv[*index];
        }

        return ln_apply_option(opts, opt->short_opt, value, seen_force, seen_interactive);
    }

    ln_diag(opts, "invalid option: %s", arg);
    return -1;
}

static int
ln_parse_short_options(ln_options_t *opts, int argc, char **argv, int *index,
                       bool *seen_force, bool *seen_interactive)
{
    char *arg;
    size_t j;

    arg = argv[*index];
    j = 1;
    while (arg[j] != '\0') {
        char opt;
        const char *optval;
        int rc;

        opt = arg[j];
        optval = NULL;

        if (opt == 'S' || opt == 't') {
            if (arg[j + 1] != '\0') {
                optval = &arg[j + 1];
                j = strlen(arg);
            } else {
                if (*index + 1 >= argc) {
                    ln_diag(opts, "option requires an argument -- '%c'", opt);
                    return -1;
                }
                ++(*index);
                optval = argv[*index];
            }
        } else if (opt == 'b' && arg[j + 1] != '\0') {
            optval = &arg[j + 1];
            j = strlen(arg);
        }

        rc = ln_apply_option(opts, opt, optval, seen_force, seen_interactive);
        if (rc != 0) {
            return -1;
        }

        if (opt == 'S' || opt == 't' || opt == 'b') {
            break;
        }

        ++j;
    }

    return 0;
}

int
ln_parse_options(ln_options_t *opts, int argc, char **argv, int *operand_index, int *show_help)
{
    bool seen_force;
    bool seen_interactive;
    int i;

    if (!opts || !operand_index || !show_help) {
        errno = EINVAL;
        return -1;
    }

    *show_help = 0;
    seen_force = false;
    seen_interactive = false;

    i = 1;
    while (i < argc) {
        char *arg;

        arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0') {
            break;
        }

        if (strcmp(arg, "--") == 0) {
            ++i;
            break;
        }

        if (arg[1] == '-') {
            int rc;

            rc = ln_parse_long_option(opts, arg, argc, argv, &i, show_help,
                                      &seen_force, &seen_interactive);
            if (rc < 0) {
                return -1;
            }
            if (rc > 0) {
                *operand_index = i + 1;
                return 0;
            }
            ++i;
            continue;
        }

        if (ln_parse_short_options(opts, argc, argv, &i, &seen_force, &seen_interactive) != 0) {
            return -1;
        }

        ++i;
    }

    if (opts->target_directory && opts->no_target_directory) {
        ln_diag(opts, "-t/--target-directory and -T/--no-target-directory are mutually exclusive");
        return -1;
    }

    if (opts->relative && !opts->symbolic) {
        ln_diag(opts, "-r/--relative requires -s/--symbolic");
        return -1;
    }

    if (opts->bsd_remove_target_dir && opts->symbolic &&
        !seen_force && !seen_interactive &&
        opts->replace_mode == LN_REPLACE_DEFAULT) {
        opts->replace_mode = LN_REPLACE_FORCE;
    }

    *operand_index = i;
    return 0;
}
