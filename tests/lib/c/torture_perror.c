/*
 * torture_perror.c -- perror() prints strerror(errno), not "error N".
 *
 * perror("x") used to print "x: error 2".  It must print the prefix, ": ",
 * the strerror() text and a newline; with a NULL or empty prefix, only
 * the text.  errno must be left as it was: ISO C does not demand that
 * (glibc's perror can change it), but Substrate's perror.3 promises it.
 *
 * stderr is redirected into a pipe to capture what perror writes.
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

/* Run perror(prefix) with errno = err; return what it wrote to fd 2. */
static const char *capture(const char *prefix, int err, int *errno_after)
{
    static char buf[256];
    int p[2], saved;
    ssize_t n;

    fflush(stderr);
    if (pipe(p) != 0)
        return "(pipe failed)";
    saved = dup(2);
    dup2(p[1], 2);
    close(p[1]);

    errno = err;
    perror(prefix);
    *errno_after = errno;
    fflush(stderr);

    dup2(saved, 2);
    close(saved);
    n = read(p[0], buf, sizeof(buf) - 1);
    close(p[0]);
    buf[n > 0 ? n : 0] = '\0';
    return buf;
}

static void check(const char *prefix, int err, const char *want)
{
    int after = -1;
    const char *got = capture(prefix, err, &after);

    if (strcmp(got, want) != 0) {
        printf("FAIL: perror(%s%s%s) with errno %d: wrote \"%s\", want \"%s\"\n",
               prefix ? "\"" : "", prefix ? prefix : "NULL",
               prefix ? "\"" : "", err, got, want);
        failures++;
    }
    if (after != err) {
        printf("FAIL: perror changed errno from %d to %d\n", err, after);
        failures++;
    }
}

int main(void)
{
    char want[256];

    snprintf(want, sizeof(want), "x: %s\n", strerror(ENOENT));
    check("x", ENOENT, want);
    if (strstr(want, "error 2") != NULL) {
        printf("FAIL: strerror(ENOENT) is \"%s\"\n", strerror(ENOENT));
        failures++;
    }

    snprintf(want, sizeof(want), "%s\n", strerror(EACCES));
    check(NULL, EACCES, want);
    check("", EACCES, want);

    snprintf(want, sizeof(want), "open /tmp/f: %s\n", strerror(EISDIR));
    check("open /tmp/f", EISDIR, want);

    printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
