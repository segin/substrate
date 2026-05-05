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

    assert(ps_parse_options(3, argv2, &opts, &error) == 0);
    assert(opts.flag_a && opts.flag_x && opts.flag_b);

    assert(ps_parse_options(2, argv3, &opts, &error) != 0);
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
    test_fields();
    puts("PASS: test_ps");
    return 0;
}
