#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "getopt.h"
#include "mv.h"

enum {
    MV_OPT_BACKUP = 256,
    MV_OPT_TARGET_DIRECTORY,
    MV_OPT_NO_TARGET_DIRECTORY,
    MV_OPT_STRIP_TRAILING_SLASHES,
    MV_OPT_UPDATE,
    MV_OPT_DEBUG,
    MV_OPT_HELP,
    MV_OPT_VERSION,
};

static const struct option long_options[] = {
    { "force",                  no_argument,       NULL, 'f' },
    { "interactive",            no_argument,       NULL, 'i' },
    { "no-clobber",             no_argument,       NULL, 'n' },
    { "verbose",                no_argument,       NULL, 'v' },
    { "backup",                 optional_argument, NULL, MV_OPT_BACKUP },
    { "suffix",                 required_argument, NULL, 'S' },
    { "target-directory",       required_argument, NULL, 't' },
    { "no-target-directory",    no_argument,       NULL, 'T' },
    { "strip-trailing-slashes", no_argument,       NULL, MV_OPT_STRIP_TRAILING_SLASHES },
    { "update",                 optional_argument, NULL, MV_OPT_UPDATE },
    { "debug",                  no_argument,       NULL, MV_OPT_DEBUG },
    { "help",                   no_argument,       NULL, MV_OPT_HELP },
    { "version",                no_argument,       NULL, MV_OPT_VERSION },
    { NULL,                     0,                 NULL, 0 },
};

static void mv_usage(const struct mv_options *opts, FILE *out)
{
    fprintf(out,
        "usage: %s [-finvhT] [-S SUFFIX] [-t DIR] [--backup[=CONTROL]]\n"
        "       %s [--update[=WHEN]] [--strip-trailing-slashes] SOURCE... DEST\n",
        opts->progname, opts->progname);
}

static int parse_backup_control(struct mv_options *opts, const char *val)
{
    /* GNU CONTROL vocabulary. */
    if (val == NULL || strcmp(val, "existing") == 0 || strcmp(val, "nil") == 0) {
        opts->backup = MV_BACKUP_EXISTING;
        return 0;
    }
    if (strcmp(val, "numbered") == 0 || strcmp(val, "t") == 0) {
        opts->backup = MV_BACKUP_NUMBERED;
        return 0;
    }
    if (strcmp(val, "simple") == 0 || strcmp(val, "never") == 0) {
        opts->backup = MV_BACKUP_SIMPLE;
        return 0;
    }
    if (strcmp(val, "none") == 0 || strcmp(val, "off") == 0) {
        opts->backup = MV_BACKUP_NONE;
        return 0;
    }
    return -1;
}

static int parse_update_when(struct mv_options *opts, const char *val)
{
    if (val == NULL || strcmp(val, "older") == 0) {
        opts->update = MV_UPDATE_OLDER;
        return 0;
    }
    if (strcmp(val, "all") == 0) {
        opts->update = MV_UPDATE_ALL;
        return 0;
    }
    if (strcmp(val, "none") == 0) {
        opts->update = MV_UPDATE_NONE;
        return 0;
    }
    if (strcmp(val, "none-fail") == 0) {
        opts->update = MV_UPDATE_NONE_FAIL;
        return 0;
    }
    return -1;
}

void mv_options_init(struct mv_options *opts, const char *progname)
{
    memset(opts, 0, sizeof(*opts));
    opts->progname = (progname != NULL && progname[0] != '\0') ?
        progname : "mv";
    opts->prompt = MV_PROMPT_FORCE;
    opts->backup = MV_BACKUP_NONE;
    opts->update = MV_UPDATE_ALL;
    opts->backup_suffix = "~";
}

int mv_parse_options(struct mv_options *opts, int argc, char **argv,
                     int *first_operand_index)
{
    int c;
    bool saw_b_flag = false;
    const char *env;

    /* $SIMPLE_BACKUP_SUFFIX overrides the default '~'. */
    env = getenv("SIMPLE_BACKUP_SUFFIX");
    if (env != NULL && env[0] != '\0') {
        opts->backup_suffix = env;
    }

    optind = 1;
    opterr = 0;
    while ((c = getopt_long(argc, argv, "+finvhbS:t:TZ",
                            long_options, NULL)) != -1) {
        switch (c) {
        case 'f': opts->prompt = MV_PROMPT_FORCE;       break;
        case 'i': opts->prompt = MV_PROMPT_INTERACTIVE; break;
        case 'n': opts->prompt = MV_PROMPT_NOCLOBBER;   break;
        case 'v': opts->verbose = true;                 break;
        case 'h': opts->symlink_target_as_self = true;  break;
        case 'b':
            saw_b_flag = true;
            /* If -b alone and $VERSION_CONTROL is unset, default is
             * `existing` (GNU). */
            opts->backup = MV_BACKUP_EXISTING;
            break;
        case 'S':
            opts->backup_suffix = optarg;
            break;
        case 't':
            opts->target_directory = optarg;
            break;
        case 'T':
            opts->no_target_directory = true;
            break;
        case 'Z':
            /* SELinux context — accepted-and-ignored on substrate. */
            break;
        case MV_OPT_BACKUP:
            saw_b_flag = true;
            if (parse_backup_control(opts, optarg) != 0) {
                fprintf(stderr,
                    "%s: invalid backup type '%s'\n",
                    opts->progname, optarg);
                return -1;
            }
            break;
        case MV_OPT_STRIP_TRAILING_SLASHES:
            opts->strip_trailing_slashes = true;
            break;
        case MV_OPT_UPDATE:
            if (parse_update_when(opts, optarg) != 0) {
                fprintf(stderr,
                    "%s: invalid --update argument '%s'\n",
                    opts->progname, optarg);
                return -1;
            }
            break;
        case MV_OPT_DEBUG:
            opts->debug = true;
            opts->verbose = true;
            break;
        case MV_OPT_HELP:
            mv_usage(opts, stdout);
            exit(0);
        case MV_OPT_VERSION:
            printf("%s (substrate) 1.0\n", opts->progname);
            exit(0);
        case '?':
        default:
            mv_usage(opts, stderr);
            return -1;
        }
    }

    /* $VERSION_CONTROL governs `-b` if no explicit CONTROL was given.
     * If only `-b` was seen (no --backup=CTL), respect the env. */
    if (saw_b_flag && opts->backup == MV_BACKUP_EXISTING) {
        env = getenv("VERSION_CONTROL");
        if (env != NULL) {
            (void)parse_backup_control(opts, env);
        }
    }

    if (opts->target_directory != NULL && opts->no_target_directory) {
        fprintf(stderr,
            "%s: cannot combine --target-directory and "
            "--no-target-directory\n", opts->progname);
        return -1;
    }

    *first_operand_index = optind;
    return 0;
}
