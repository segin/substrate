#include "modeparse.h"

#include <stdio.h>

#include <sys/stat.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int
test_numeric(void)
{
    mode_t out = 0;
    char err[128];

    CHECK(parse_mode("755", S_IFDIR | 0777, &out, err, sizeof(err)) == 0);
    CHECK((out & 07777) == 0755);
    return 0;
}

static int
test_symbolic(void)
{
    mode_t out = 0;
    char err[128];

    CHECK(parse_mode("u=rwx,go=", S_IFDIR | 0777, &out, err, sizeof(err)) == 0);
    CHECK((out & 07777) == 0700);
    CHECK(parse_mode("a+X", S_IFDIR | 0644, &out, err, sizeof(err)) == 0);
    CHECK((out & 07777) == 0755);
    return 0;
}

static int
test_invalid(void)
{
    mode_t out = 0;
    char err[128];

    CHECK(parse_mode("u+z", S_IFDIR | 0777, &out, err, sizeof(err)) != 0);
    CHECK(parse_mode("888", S_IFDIR | 0777, &out, err, sizeof(err)) != 0);
    return 0;
}

int
main(void)
{
    if (test_numeric() != 0) {
        return 1;
    }
    if (test_symbolic() != 0) {
        return 1;
    }
    if (test_invalid() != 0) {
        return 1;
    }
    puts("test_mode: ok");
    return 0;
}