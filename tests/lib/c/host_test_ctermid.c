/*
 * host_test_ctermid.c
 *
 * Verifies Substrate's ctermid(3) from lib/c/src/ctermid.c by compiling that
 * source directly into the test: the name it returns, that a caller-supplied
 * buffer of exactly L_ctermid bytes is used and not overrun, and that the
 * NULL form hands back the same internal buffer on every call.
 *
 *     make -C tests host_test_ctermid && tests/host_test_ctermid
 */

#include <stdio.h>
#include <string.h>

#include "../../../lib/c/src/ctermid.c"

static int fails;

#define CHECK(cond, what) do { \
    if (cond) printf("  ok   %s\n", what); \
    else { printf("  FAIL %s\n", what); fails++; } \
} while (0)

int main(void)
{
    char buf[L_ctermid + 1];
    char *p, *q;

    CHECK(L_ctermid >= sizeof("/dev/tty"), "L_ctermid holds \"/dev/tty\" and its NUL");

    memset(buf, 'X', sizeof(buf));
    p = ctermid(buf);
    CHECK(p == buf, "ctermid(buf) returns buf");
    CHECK(strcmp(buf, "/dev/tty") == 0, "ctermid(buf) writes \"/dev/tty\"");
    CHECK(buf[L_ctermid] == 'X', "ctermid(buf) stays within L_ctermid bytes");

    p = ctermid(NULL);
    q = ctermid(NULL);
    CHECK(p != NULL && strcmp(p, "/dev/tty") == 0, "ctermid(NULL) returns \"/dev/tty\"");
    CHECK(p == q, "ctermid(NULL) reuses one internal buffer");
    CHECK(p != buf, "ctermid(NULL) does not use the caller's last buffer");

    printf("host_test_ctermid: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
