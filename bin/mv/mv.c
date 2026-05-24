#include "mv.h"
#include "mv_path.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *progname_from_argv0(const char *argv0)
{
    const char *s;
    if (argv0 == NULL || *argv0 == '\0') return "mv";
    s = strrchr(argv0, '/');
    return s ? s + 1 : argv0;
}

int main(int argc, char *argv[])
{
    struct mv_options opts;
    int first;
    int rc = 0;
    int operands;

    mv_options_init(&opts, progname_from_argv0(argv[0]));
    if (mv_parse_options(&opts, argc, argv, &first) != 0) {
        return 1;
    }

    operands = argc - first;

    /* `-t DIR src...` form: all positional args are sources. */
    if (opts.target_directory != NULL) {
        struct stat st;
        if (operands < 1) {
            fprintf(stderr,
                "%s: missing source operand after -t '%s'\n",
                opts.progname, opts.target_directory);
            return 1;
        }
        if (stat(opts.target_directory, &st) != 0 ||
            !S_ISDIR(st.st_mode)) {
            fprintf(stderr,
                "%s: target '%s' is not a directory\n",
                opts.progname, opts.target_directory);
            return 1;
        }
        for (int i = first; i < argc; i++) {
            char *dst;
            mv_path_strip_trailing_slashes(argv[i]);
            dst = mv_path_join(opts.target_directory,
                               mv_path_basename(argv[i]));
            if (dst == NULL) {
                fprintf(stderr, "%s: out of memory\n", opts.progname);
                rc = 1;
                continue;
            }
            if (mv_rename_one(argv[i], dst, &opts) != 0) rc = 1;
            free(dst);
        }
        return rc;
    }

    if (operands < 2) {
        fprintf(stderr,
            "usage: %s [-finvhT] [-S SUFFIX] [-t DIR] "
            "[--backup[=CTL]] [--update[=WHEN]] "
            "SOURCE... DEST\n", opts.progname);
        return 1;
    }

    if (operands == 2) {
        const char *src = argv[first];
        const char *dst = argv[first + 1];
        struct stat dst_st;
        bool target_is_dir;

        target_is_dir = (lstat(dst, &dst_st) == 0) &&
                        S_ISDIR(dst_st.st_mode);

        if (opts.symlink_target_as_self && target_is_dir) {
            /* BSD -h: a symlink-to-dir destination is treated as
             * a regular file — the symlink itself is the target. */
            struct stat lst;
            if (lstat(dst, &lst) == 0 && S_ISLNK(lst.st_mode)) {
                target_is_dir = false;
            }
        }
        if (opts.no_target_directory) {
            target_is_dir = false;
        }

        if (target_is_dir) {
            char *full;
            mv_path_strip_trailing_slashes(argv[first]);
            full = mv_path_join(dst, mv_path_basename(src));
            if (full == NULL) {
                fprintf(stderr, "%s: out of memory\n", opts.progname);
                return 1;
            }
            rc = (mv_rename_one(src, full, &opts) == 0) ? 0 : 1;
            free(full);
            return rc;
        }
        return (mv_rename_one(src, dst, &opts) == 0) ? 0 : 1;
    }

    /* operands > 2 -> last is the target directory */
    {
        const char *dst_dir = argv[argc - 1];
        struct stat st;

        if (opts.no_target_directory) {
            fprintf(stderr,
                "%s: extra operand '%s' (with -T, exactly two "
                "operands expected)\n",
                opts.progname, argv[first + 2]);
            return 1;
        }
        if (lstat(dst_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr,
                "%s: target '%s' is not a directory\n",
                opts.progname, dst_dir);
            return 1;
        }
        for (int i = first; i < argc - 1; i++) {
            char *dst;
            mv_path_strip_trailing_slashes(argv[i]);
            dst = mv_path_join(dst_dir, mv_path_basename(argv[i]));
            if (dst == NULL) {
                fprintf(stderr, "%s: out of memory\n", opts.progname);
                rc = 1;
                continue;
            }
            if (mv_rename_one(argv[i], dst, &opts) != 0) rc = 1;
            free(dst);
        }
    }
    return rc;
}
