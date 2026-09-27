/*
 * torture_su_eof.c -- su with no password input fails.
 *
 * su's read_password() returned the length read, never an error, so end
 * of file at the prompt (stdin closed or empty) became an empty password
 * -- which authenticates any account whose password field is empty.  This
 * test adds such an account (its shell /bin/true) and, as uid 1000, runs
 * "su sutest" with stdin at end of file: it must fail.  A real empty line
 * is still accepted, since that is the account's password.
 *
 * Runs as init (root); /etc/passwd and /etc/shadow are restored afterwards.
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_USER "sutest"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    char *buf = malloc(65536);
    *len = buf ? fread(buf, 1, 65536, f) : 0;
    fclose(f);
    return buf;
}

static int spit(const char *path, const char *data, size_t len, const char *extra) {
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fwrite(data, 1, len, f);
    if (extra)
        fputs(extra, f);
    return fclose(f);
}

/* Run "su sutest" as uid 1000 with `input` (NULL: end of file at once). */
static int run_su(const char *input) {
    int in[2];
    pipe(in);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], 0);
        close(in[0]);
        close(in[1]);
        if (setgid(1000) != 0 || setuid(1000) != 0)
            _exit(99);
        execl("/bin/su", "su", TEST_USER, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    if (input)
        write(in[1], input, strlen(input));
    close(in[1]);
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

int main(void) {
    printf("torture_su_eof\n");

    size_t plen = 0, slen = 0;
    char *passwd = slurp("/etc/passwd", &plen);
    char *shadow = slurp("/etc/shadow", &slen);
    struct stat sst;
    if (!passwd || !shadow || stat("/etc/shadow", &sst) != 0 ||
        spit("/etc/passwd", passwd, plen,
             TEST_USER ":x:4245:4245:su test:/:/bin/true\n") != 0 ||
        spit("/etc/shadow", shadow, slen, TEST_USER "::0:0:99999:7:::\n") != 0) {
        printf("setup failed: %s\nResult: FAILED\n", strerror(errno));
        return 1;
    }
    chmod("/etc/shadow", sst.st_mode & 07777);
    chown("/etc/shadow", sst.st_uid, sst.st_gid);

    check("end of file at the prompt fails", run_su(NULL) != 0,
          "su authenticated without reading a password");
    check("an empty line is the empty password", run_su("\n") == 0,
          "su refused the account's (empty) password");

    spit("/etc/passwd", passwd, plen, NULL);
    spit("/etc/shadow", shadow, slen, NULL);
    chmod("/etc/shadow", sst.st_mode & 07777);
    chown("/etc/shadow", sst.st_uid, sst.st_gid);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
