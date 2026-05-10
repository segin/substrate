#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ps_impl.h"

static void test_parse(void) {
    ps_options_t opts;
    const char *error;
    char *argv1[] = {"ps", "auxleb"};
    char *argv2[] = {"ps", "-ax", "-b"};
    char *argv3[] = {"ps", "q"};

    assert(ps_parse_options(2, argv1, &opts, &error) == 0);
    assert(opts.flag_a && opts.flag_u && opts.flag_x && opts.flag_l && opts.flag_e && opts.flag_b);
    assert(opts.pid_filter_n == 0 && opts.uid_filter_n == 0);

    assert(ps_parse_options(3, argv2, &opts, &error) == 0);
    assert(opts.flag_a && opts.flag_x && opts.flag_b);

    assert(ps_parse_options(2, argv3, &opts, &error) != 0);
}

static void test_pid_filter(void) {
    ps_options_t opts;
    const char *error;

    /* Single PID */
    char *argv1[] = {"ps", "-p", "42"};
    assert(ps_parse_options(3, argv1, &opts, &error) == 0);
    assert(opts.pid_filter_n == 1 && opts.pid_filter[0] == 42);

    /* Comma-separated list */
    char *argv2[] = {"ps", "-p", "1,2,3,1000"};
    assert(ps_parse_options(3, argv2, &opts, &error) == 0);
    assert(opts.pid_filter_n == 4);
    assert(opts.pid_filter[0] == 1 && opts.pid_filter[1] == 2);
    assert(opts.pid_filter[2] == 3 && opts.pid_filter[3] == 1000);

    /* Long-form alias */
    char *argv3[] = {"ps", "--pid=7"};
    assert(ps_parse_options(2, argv3, &opts, &error) == 0);
    assert(opts.pid_filter_n == 1 && opts.pid_filter[0] == 7);

    /* Garbage rejected */
    char *argv4[] = {"ps", "-p", "abc"};
    assert(ps_parse_options(3, argv4, &opts, &error) != 0);

    /* Trailing comma is silently accepted (procps behavior). */
    char *argv5[] = {"ps", "-p", "1,"};
    assert(ps_parse_options(3, argv5, &opts, &error) == 0);
    assert(opts.pid_filter_n == 1 && opts.pid_filter[0] == 1);
}

static void test_uid_filter(void) {
    ps_options_t opts;
    const char *error;

    /* Numeric uid */
    char *argv1[] = {"ps", "-U", "0"};
    assert(ps_parse_options(3, argv1, &opts, &error) == 0);
    assert(opts.uid_filter_n == 1 && opts.uid_filter[0] == 0);

    /* Multiple numeric */
    char *argv2[] = {"ps", "-U", "0,1000,2000"};
    assert(ps_parse_options(3, argv2, &opts, &error) == 0);
    assert(opts.uid_filter_n == 3);
    assert(opts.uid_filter[0] == 0);
    assert(opts.uid_filter[1] == 1000);
    assert(opts.uid_filter[2] == 2000);

    /* Named lookup — getpwnam("root") must always resolve to uid 0
     * on a POSIX host (and on Substrate's mock libc).  Other names
     * vary by /etc/passwd; we just assert that root made it through. */
    char *argv3[] = {"ps", "-U", "root"};
    assert(ps_parse_options(3, argv3, &opts, &error) == 0);
    assert(opts.uid_filter_n == 1 && opts.uid_filter[0] == 0);
}

static void test_no_headers(void) {
    ps_options_t opts;
    const char *error;
    char *argv[] = {"ps", "--no-headers"};
    assert(ps_parse_options(2, argv, &opts, &error) == 0);
    assert(opts.flag_no_headers);
}

static void test_fields(void) {
    ps_options_t opts;
    ps_field_t fields[16];
    size_t count = 0;

    memset(&opts, 0, sizeof(opts));
    ps_build_fields(&opts, fields, &count);
    assert(count > 0);
    assert(fields[0].id == PS_FIELD_PID);

    memset(&opts, 0, sizeof(opts));
    opts.flag_u = true;
    opts.flag_b = true;
    ps_build_fields(&opts, fields, &count);
    assert(count > 0);
    assert(fields[0].id == PS_FIELD_USER);
}

int main(void) {
    test_parse();
    test_pid_filter();
    test_uid_filter();
    test_no_headers();
    test_fields();
    puts("PASS: test_ps");
    return 0;
}
