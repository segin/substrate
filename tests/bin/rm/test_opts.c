#include "rm_opts.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int
test_basic_parse(void)
{
    struct rm_options opts;
    const char *err_msg;
    char *argv[] = { "rm", "file", NULL };

    rm_options_init(&opts, argv[0]);
    err_msg = NULL;
    CHECK(rm_parse_options(&opts, 2, argv, &err_msg) == 0);
    CHECK(opts.operand_start == 1);
    CHECK(opts.operand_count == 1);
    CHECK(opts.force == false);
    CHECK(opts.recursive == false);
    return 0;
}

static int
test_last_option_wins(void)
{
    struct rm_options opts;
    const char *err_msg;
    char *argv[] = { "rm", "-if", "file", NULL };
    char *argv2[] = { "rm", "-f", "-i", "file", NULL };

    rm_options_init(&opts, argv[0]);
    err_msg = NULL;
    CHECK(rm_parse_options(&opts, 3, argv, &err_msg) == 0);
    CHECK(opts.force == true);
    CHECK(opts.prompt_mode == RM_PROMPT_NEVER);

    rm_options_init(&opts, argv2[0]);
    err_msg = NULL;
    CHECK(rm_parse_options(&opts, 4, argv2, &err_msg) == 0);
    CHECK(opts.force == false);
    CHECK(opts.prompt_mode == RM_PROMPT_ALWAYS);
    return 0;
}

static int
test_long_options(void)
{
    struct rm_options opts;
    const char *err_msg;
    char *argv[] = {
        "rm",
        "--interactive=once",
        "--recursive",
        "--one-file-system",
        "--no-preserve-root",
        "--verbose",
        "file",
        NULL,
    };

    rm_options_init(&opts, argv[0]);
    err_msg = NULL;
    CHECK(rm_parse_options(&opts, 7, argv, &err_msg) == 0);
    CHECK(opts.prompt_mode == RM_PROMPT_ONCE);
    CHECK(opts.recursive == true);
    CHECK(opts.one_file_system == true);
    CHECK(opts.preserve_root == false);
    CHECK(opts.verbose == true);
    return 0;
}

static int
test_help_and_force_without_operands(void)
{
    struct rm_options opts;
    const char *err_msg;
    char *argv_help[] = { "rm", "--help", NULL };
    char *argv_force[] = { "rm", "-f", NULL };

    rm_options_init(&opts, argv_help[0]);
    err_msg = NULL;
    CHECK(rm_parse_options(&opts, 2, argv_help, &err_msg) == 0);
    CHECK(opts.show_help == true);

    rm_options_init(&opts, argv_force[0]);
    err_msg = NULL;
    CHECK(rm_parse_options(&opts, 2, argv_force, &err_msg) == 0);
    CHECK(opts.operand_count == 0);
    return 0;
}

static int
test_invalid_interactive_mode(void)
{
    struct rm_options opts;
    const char *err_msg;
    char *argv[] = { "rm", "--interactive=bogus", "file", NULL };

    rm_options_init(&opts, argv[0]);
    err_msg = NULL;
    CHECK(rm_parse_options(&opts, 3, argv, &err_msg) != 0);
    CHECK(err_msg != NULL);
    return 0;
}

int
main(void)
{
    if (test_basic_parse() != 0 || test_last_option_wins() != 0 ||
        test_long_options() != 0 ||
        test_help_and_force_without_operands() != 0 ||
        test_invalid_interactive_mode() != 0) {
        return 1;
    }
    puts("test_opts: ok");
    return 0;
}