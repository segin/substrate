/*
 * torture_login_noshell.c -- login ends when the user's shell cannot be run.
 *
 * Once login had become the authenticated user, a failed exec of the shell
 * returned to main()'s prompt loop: the next "login:" prompt -- and every
 * later attempt -- ran with that user's uid.  This test adds an account
 * whose shell does not exist (and needs no password), runs
 * "/bin/login logintest" on pipes, answers the password prompt, and checks
 * that login exits with a failure status and prints no further prompt.
 *
 * Runs as init (root); the passwd and shadow files are restored afterwards.
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_USER "logintest"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

/* Read a whole small file into a malloc'd buffer; *len gets its size. */
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

int main(void) {
    printf("torture_login_noshell\n");

    size_t plen = 0, slen = 0;
    char *passwd = slurp("/etc/passwd", &plen);
    char *shadow = slurp("/etc/shadow", &slen);
    if (!passwd || !shadow ||
        spit("/etc/passwd", passwd, plen,
             TEST_USER ":x:4242:4242:login test:/:/nonexistent/shell\n") != 0 ||
        spit("/etc/shadow", shadow, slen, TEST_USER "::0:0:99999:7:::\n") != 0) {
        printf("setup failed: %s\n", strerror(errno));
        printf("Result: FAILED\n");
        return 1;
    }

    int in[2], out[2];
    pipe(in);
    pipe(out);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execl("/bin/login", "login", TEST_USER, (char *)NULL);
        _exit(99);
    }
    close(in[0]);
    close(out[1]);

    /* Empty password.  The write end stays open, so a login that goes back
     * to its prompt blocks there instead of seeing end of file. */
    write(in[1], "\n", 1);

    char got[4096];
    size_t n = 0;
    for (;;) {
        struct pollfd pfd = { out[0], POLLIN, 0 };
        if (poll(&pfd, 1, 5000) <= 0)
            break;
        ssize_t r = read(out[0], got + n, sizeof(got) - 1 - n);
        if (r <= 0)
            break;
        n += (size_t)r;
        if (n == sizeof(got) - 1)
            break;
    }
    got[n] = '\0';

    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }

    char *after = strstr(got, "exec");
    check("exec failure reported", after != NULL, "no exec error in output");
    check("no prompt after exec failure", after == NULL || strstr(after, "login:") == NULL,
          "login went back to its prompt as the user");
    check("login exited", w == pid, "login still running");
    check("exit status is failure",
          w == pid && WIFEXITED(status) && WEXITSTATUS(status) != 0,
          "login did not exit with a failure status");
    if (failures)
        printf("--- login output ---\n%s\n---\n", got);

    close(in[1]);
    close(out[0]);
    spit("/etc/passwd", passwd, plen, NULL);
    spit("/etc/shadow", shadow, slen, NULL);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
