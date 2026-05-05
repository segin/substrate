#include "rmdir_opts.h"

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int
test_basic_parse(void)
{
    struct rmdir_options opts;
    const char *err_msg;
    char *argv[] = { "rmdir", "dir", NULL };

    rmdir_options_init(&opts, argv[0]);
    err_msg = NULL;
    CHECK(rmdir_parse_options(&opts, 2, argv, &err_msg) == 0);
    CHECK(opts.operand_count == 1);
    CHECK(opts.parents == false);
    return 0;
}

static int
test_long_options(void)
{
    struct rmdir_options opts;
    const char *err_msg;
    char *argv[] = {
        "rmdir",
        "--parents",
        "--verbose",
        "--ignore-fail-on-non-empty",
        "dir",
        NULL,
    };

    rmdir_options_init(&opts, argv[0]);
    err_msg = NULL;
    CHECK(rmdir_parse_options(&opts, 5, argv, &err_msg) == 0);
    CHECK(opts.parents == true);
    CHECK(opts.verbose == true);
    CHECK(opts.ignore_fail_on_non_empty == true);
    return 0;
}

static int
test_help_and_missing_operand(void)
{
    struct rmdir_options opts;
    const char *err_msg;
    char *argv_help[] = { "rmdir", "--help", NULL };
    char *argv_missing[] = { "rmdir", NULL };

    rmdir_options_init(&opts, argv_help[0]);
    err_msg = NULL;
    CHECK(rmdir_parse_options(&opts, 2, argv_help, &err_msg) == 0);
    CHECK(opts.show_help == true);

    rmdir_options_init(&opts, "rmdir");
    err_msg = NULL;
    CHECK(rmdir_parse_options(&opts, 1, argv_missing, &err_msg) != 0);
    CHECK(err_msg != NULL);
    return 0;
}

int
main(void)
{
    if (test_basic_parse() != 0 || test_long_options() != 0 ||
        test_help_and_missing_operand() != 0) {
        return 1;
    }
    puts("test_opts: ok");
    return 0;
}