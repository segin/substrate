#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#include "mode_parser.h"

#ifndef S_ISUID
#define S_ISUID 0004000
#endif
#ifndef S_ISGID
#define S_ISGID 0002000
#endif
#ifndef S_ISVTX
#define S_ISVTX 0001000
#endif
#ifndef S_IFDIR
#define S_IFDIR 0040000
#endif
#ifndef S_IFREG
#define S_IFREG 0100000
#endif

#define MODE_BITS (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)

static void
expect_success(const char *spec, mode_t old_mode, mode_t expected_mode)
{
    char errbuf[128];
    struct chmod_mode *m = chmod_setmode(spec, errbuf, sizeof(errbuf));
    mode_t got;

    if (m == NULL) {
        fprintf(stderr, "expected mode parse success for '%s': %s\n", spec,
            errbuf);
        abort();
    }

    got = chmod_getmode(m, old_mode) & MODE_BITS;
    if (got != (expected_mode & MODE_BITS)) {
        fprintf(stderr,
            "mode mismatch for '%s': old=%#o expected=%#o got=%#o\n",
            spec, (unsigned)(old_mode & MODE_BITS),
            (unsigned)(expected_mode & MODE_BITS), (unsigned)got);
        abort();
    }

    chmod_freemode(m);
}

static void
expect_invalid(const char *spec)
{
    char errbuf[128];
    struct chmod_mode *m = chmod_setmode(spec, errbuf, sizeof(errbuf));

    if (m != NULL) {
        fprintf(stderr, "expected mode parse failure for '%s'\n", spec);
        chmod_freemode(m);
        abort();
    }
}

static void
test_numeric_modes(void)
{
    expect_success("755", 0644, 0755);
    expect_success("0000", 0777, 0000);
    expect_success("1777", 0600, 01777);
}

static void
test_symbolic_modes(void)
{
    expect_success("u+rwx,g-w", 0644, 0744);
    expect_success("u=rwX,g+w,o-r", 0644, 0660);
    expect_success("g=u", 0640, 0660);
    expect_success("u+s,g+s,+t", 0644, 07644);

    expect_success("a+X", S_IFDIR | 0644, 0755);
    expect_success("a+X", S_IFREG | 0644, 0644);
}

static void
test_umask_interaction(void)
{
    mode_t old_umask = umask(0022);

    expect_success("+w", 0444, 0644);
    expect_success("=rw", 0777, 0666);

    (void)umask(old_umask);
}

static void
test_invalid_inputs(void)
{
    expect_invalid("");
    expect_invalid("u");
    expect_invalid("u+z");
    expect_invalid("888");
    expect_invalid("a++r");
}

int
main(void)
{
    test_numeric_modes();
    test_symbolic_modes();
    test_umask_interaction();
    test_invalid_inputs();

    puts("unit_mode_parser: ok");
    return 0;
}
