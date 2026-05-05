#include "rmdir.h"
#include "rmdir_parents.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int
test_remove_with_parents(void)
{
    struct rmdir_options opts;

    mkdir("a", 0755);
    mkdir("a/b", 0755);
    mkdir("a/b/c", 0755);

    opts.progname = "rmdir";
    opts.parents = true;
    opts.verbose = false;
    opts.ignore_fail_on_non_empty = false;
    opts.show_help = false;
    opts.show_version = false;
    opts.operand_start = 0;
    opts.operand_count = 0;

    CHECK(rmdir_remove_path(&opts, "a/b/c") == RMDIR_RESULT_REMOVED);
    CHECK(access("a", F_OK) != 0);
    return 0;
}

static int
test_ignore_fail_on_non_empty(void)
{
    struct rmdir_options opts;

    mkdir("keep", 0755);
    mkdir("keep/sub", 0755);
    mkdir("keep/sub/child", 0755);
    mkdir("keep/sibling", 0755);

    opts.progname = "rmdir";
    opts.parents = true;
    opts.verbose = false;
    opts.ignore_fail_on_non_empty = true;
    opts.show_help = false;
    opts.show_version = false;
    opts.operand_start = 0;
    opts.operand_count = 0;

    CHECK(rmdir_remove_path(&opts, "keep/sub/child") == RMDIR_RESULT_REMOVED);
    CHECK(access("keep", F_OK) == 0);
    CHECK(access("keep/sub", F_OK) != 0);
    CHECK(access("keep/sibling", F_OK) == 0);
    return 0;
}

static int
test_remove_empty_directory(void)
{
    struct rmdir_options opts;

    mkdir("empty", 0755);

    opts.progname = "rmdir";
    opts.parents = false;
    opts.verbose = false;
    opts.ignore_fail_on_non_empty = false;
    opts.show_help = false;
    opts.show_version = false;
    opts.operand_start = 0;
    opts.operand_count = 0;

    CHECK(rmdir_remove_path(&opts, "empty") == RMDIR_RESULT_REMOVED);
    CHECK(access("empty", F_OK) != 0);
    return 0;
}

static int
test_reject_symlink(void)
{
    struct rmdir_options opts;

    mkdir("real", 0755);
    CHECK(symlink("real", "linkdir") == 0);

    opts.progname = "rmdir";
    opts.parents = false;
    opts.verbose = false;
    opts.ignore_fail_on_non_empty = false;
    opts.show_help = false;
    opts.show_version = false;
    opts.operand_start = 0;
    opts.operand_count = 0;

    CHECK(rmdir_remove_path(&opts, "linkdir") == RMDIR_RESULT_FAILED);
    CHECK(access("linkdir", F_OK) == 0);
    CHECK(access("real", F_OK) == 0);
    return 0;
}

int
main(void)
{
    char template[] = "/tmp/rmdir-parents-XXXXXX";
    char *temporary_directory;
    char *original_cwd;
    int status;

    status = 0;
    temporary_directory = mkdtemp(template);
    CHECK(temporary_directory != NULL);
    original_cwd = getcwd(NULL, 0);
    CHECK(original_cwd != NULL);
    CHECK(chdir(temporary_directory) == 0);

    if (test_remove_with_parents() != 0 ||
        test_ignore_fail_on_non_empty() != 0 ||
        test_remove_empty_directory() != 0 ||
        test_reject_symlink() != 0) {
        status = 1;
    }

    CHECK(chdir(original_cwd) == 0);
    free(original_cwd);
    printf("test_parents: %s\n", status == 0 ? "ok" : "fail");
    return status;
}