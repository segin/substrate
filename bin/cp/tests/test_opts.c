#include "cp_opts.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    return 1; \
} } while (0)

static int test_parse_basic(void)
{
    struct cp_options opts;
    const char *err = NULL;
    char *argv[] = { "cp", "src", "dst", NULL };

    CHECK(cp_parse_options(&opts, 3, argv, &err) == 0);
    CHECK(opts.source_start == 1);
    CHECK(opts.source_count == 1);
    CHECK(strcmp(opts.dest, "dst") == 0);
    CHECK(opts.recursive == 0);
    return 0;
}

static int test_parse_recursive_modes(void)
{
    struct cp_options opts;
    const char *err = NULL;
    char *argv[] = { "cp", "-RLP", "-i", "-b", "0x1000", "s", "d", NULL };

    CHECK(cp_parse_options(&opts, 7, argv, &err) == 0);
    CHECK(opts.recursive == 1);
    CHECK(opts.symlink_mode == CP_SYMLINK_PHYSICAL);
    CHECK(opts.overwrite_mode == CP_OVERWRITE_INTERACTIVE);
    CHECK(opts.buffer_size == 4096);
    CHECK(opts.buffer_size_explicit == 1);
    return 0;
}

static int test_parse_preserve(void)
{
    struct cp_options opts;
    const char *err = NULL;
    char *argv1[] = { "cp", "-pa", "s", "d", NULL };
    char *argv2[] = { "cp", "--preserve=mode,links", "s", "d", NULL };

    CHECK(cp_parse_options(&opts, 4, argv1, &err) == 0);
    CHECK(opts.preserve_all == 1);
    CHECK(opts.preserve_links == 1);

    CHECK(cp_parse_options(&opts, 4, argv2, &err) == 0);
    CHECK(opts.preserve_mode == 1);
    CHECK(opts.preserve_links == 1);
    CHECK(opts.preserve_owner == 0);
    return 0;
}

static int test_mutual_exclusive_links(void)
{
    struct cp_options opts;
    const char *err = NULL;
    char *argv[] = { "cp", "-ls", "s", "d", NULL };

    CHECK(cp_parse_options(&opts, 4, argv, &err) != 0);
    CHECK(err != NULL);
    return 0;
}

static int test_dash_filename(void)
{
    struct cp_options opts;
    const char *err = NULL;
    char *argv[] = { "cp", "--", "-", "dst", NULL };

    CHECK(cp_parse_options(&opts, 4, argv, &err) == 0);
    CHECK(opts.source_count == 1);
    return 0;
}

static int test_size_parser(void)
{
    size_t n;
    const char *err = NULL;

    CHECK(cp_parse_size("64k", &n, &err) == 0);
    CHECK(n == 65536);
    CHECK(cp_parse_size("1mb", &n, &err) == 0);
    CHECK(n == 1000000);
    CHECK(cp_parse_size("", &n, &err) != 0);
    return 0;
}

int main(void)
{
    if (test_parse_basic() != 0) return 1;
    if (test_parse_recursive_modes() != 0) return 1;
    if (test_parse_preserve() != 0) return 1;
    if (test_mutual_exclusive_links() != 0) return 1;
    if (test_dash_filename() != 0) return 1;
    if (test_size_parser() != 0) return 1;
    return 0;
}
