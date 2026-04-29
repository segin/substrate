#include "mkdir_opts.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int
test_parse_basic(void)
{
    struct mkdir_options opts;
    const char *err = NULL;
    char *argv[] = { "mkdir", "dir", NULL };

    mkdir_options_init(&opts, argv[0]);
    CHECK(mkdir_parse_options(&opts, 2, argv, &err) == 0);
    CHECK(opts.operand_start == 1);
    CHECK(opts.operand_count == 1);
    CHECK(opts.parents == false);
    CHECK(opts.have_mode == false);
    return 0;
}

static int
test_parse_short_options(void)
{
    struct mkdir_options opts;
    const char *err = NULL;
    char *argv[] = { "mkdir", "-pvm", "700", "dir", NULL };

    mkdir_options_init(&opts, argv[0]);
    CHECK(mkdir_parse_options(&opts, 4, argv, &err) == 0);
    CHECK(opts.parents == true);
    CHECK(opts.verbose == true);
    CHECK(opts.have_mode == true);
    CHECK(strcmp(opts.mode_string, "700") == 0);
    return 0;
}

static int
test_parse_long_options(void)
{
    struct mkdir_options opts;
    const char *err = NULL;
    char *argv[] = {
        "mkdir",
        "--parents",
        "--verbose",
        "--mode=u=rwx,go=",
        "--context=test_u:test_r:test_t",
        "dir",
        NULL
    };

    mkdir_options_init(&opts, argv[0]);
    CHECK(mkdir_parse_options(&opts, 6, argv, &err) == 0);
    CHECK(opts.parents == true);
    CHECK(opts.verbose == true);
    CHECK(opts.have_mode == true);
    CHECK(strcmp(opts.mode_string, "u=rwx,go=") == 0);
    CHECK(opts.selinux_context_requested == true);
    CHECK(strcmp(opts.selinux_context, "test_u:test_r:test_t") == 0);
    return 0;
}

static int
test_help_and_double_dash(void)
{
    struct mkdir_options opts;
    const char *err = NULL;
    char *argv_help[] = { "mkdir", "--help", NULL };
    char *argv_dash[] = { "mkdir", "--", "-name", NULL };

    mkdir_options_init(&opts, argv_help[0]);
    CHECK(mkdir_parse_options(&opts, 2, argv_help, &err) == 0);
    CHECK(opts.show_help == true);

    mkdir_options_init(&opts, argv_dash[0]);
    CHECK(mkdir_parse_options(&opts, 3, argv_dash, &err) == 0);
    CHECK(opts.operand_count == 1);
    CHECK(opts.operand_start == 2);
    return 0;
}

static int
test_invalid_inputs(void)
{
    struct mkdir_options opts;
    const char *err = NULL;
    char *argv_missing[] = { "mkdir", "-m", NULL };
    char *argv_invalid[] = { "mkdir", "--bogus", "dir", NULL };

    mkdir_options_init(&opts, argv_missing[0]);
    CHECK(mkdir_parse_options(&opts, 2, argv_missing, &err) != 0);
    CHECK(err != NULL);

    mkdir_options_init(&opts, argv_invalid[0]);
    CHECK(mkdir_parse_options(&opts, 3, argv_invalid, &err) != 0);
    CHECK(err != NULL);
    return 0;
}

int
main(void)
{
    if (test_parse_basic() != 0) {
        return 1;
    }
    if (test_parse_short_options() != 0) {
        return 1;
    }
    if (test_parse_long_options() != 0) {
        return 1;
    }
    if (test_help_and_double_dash() != 0) {
        return 1;
    }
    if (test_invalid_inputs() != 0) {
        return 1;
    }
    puts("test_opts: ok");
    return 0;
}