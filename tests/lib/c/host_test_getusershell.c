/*
 * host_test_getusershell.c
 *
 * Verifies Substrate's getusershell(3) family from
 * lib/c/src/getusershell.c by compiling that source directly into the
 * test, against the host's libc, under other names (the host has its own
 * getusershell) and reading a file the test writes instead of /etc/shells:
 * comments, blank lines, leading blanks, trailing junk and over-long lines
 * are handled; setusershell() rewinds; and without the file the list is
 * /bin/sh alone.
 *
 *     make -C tests host_test_getusershell && tests/host_test_getusershell
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SHELLS_FILE test_shells_path
static char test_shells_path[64];

#define getusershell sub_getusershell
#define setusershell sub_setusershell
#define endusershell sub_endusershell
/* Declared before the source uses them, since <unistd.h> here is the
 * host's and names the host's functions. */
char *sub_getusershell(void);
void sub_setusershell(void);
void sub_endusershell(void);

#include "../../../lib/c/src/getusershell.c"

static int fails;

#define CHECK(cond, what) do { \
    if (cond) printf("  ok   %s\n", what); \
    else { printf("  FAIL %s\n", what); fails++; } \
} while (0)

static int next_is(const char *want)
{
    char *s = sub_getusershell();

    if (want == NULL) return s == NULL;
    return s != NULL && strcmp(s, want) == 0;
}

int main(void)
{
    int fd;
    FILE *f;

    strlcpy(test_shells_path, "/tmp/host_test_shells.XXXXXX",
            sizeof(test_shells_path));
    fd = mkstemp(test_shells_path);
    if (fd < 0 || (f = fdopen(fd, "w")) == NULL) {
        perror("mkstemp");
        return 1;
    }
    fputs("# the permitted shells\n", f);
    fputs("\n", f);
    fputs("/bin/sh\n", f);
    fputs("  \t/bin/zsh   # the system shell\n", f);
    fputs("relative/path\n", f);
    fputs("/", f);
    for (int i = 0; i < 3000; i++) fputc('x', f);    /* too long to be a path */
    fputs("\n", f);
    fputs("/bin/ksh#no blank before the comment\n", f);
    fputs("/usr/bin/last-without-newline", f);
    fclose(f);

    CHECK(next_is("/bin/sh"), "first shell, after a comment and a blank line");
    CHECK(next_is("/bin/zsh"), "leading blanks and a trailing comment are dropped");
    CHECK(next_is("/bin/ksh"), "relative and over-long lines skipped; '#' ends a path");
    CHECK(next_is("/usr/bin/last-without-newline"), "last line without a newline");
    CHECK(next_is(NULL), "NULL at the end of the list");
    CHECK(next_is(NULL), "and NULL again");

    sub_setusershell();
    CHECK(next_is("/bin/sh"), "setusershell() rewinds");

    sub_endusershell();
    CHECK(next_is("/bin/sh"), "the list reopens after endusershell()");
    sub_endusershell();

    unlink(test_shells_path);
    CHECK(next_is("/bin/sh"), "without the file, /bin/sh");
    CHECK(next_is(NULL), "and nothing else");
    sub_setusershell();
    CHECK(next_is("/bin/sh"), "setusershell() rewinds the default list");
    sub_endusershell();

    if (fails) {
        printf("host_test_getusershell: %d FAILED\n", fails);
        return 1;
    }
    puts("host_test_getusershell: PASS");
    return 0;
}
