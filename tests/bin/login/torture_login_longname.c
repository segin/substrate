/*
 * torture_login_longname.c -- an over-long user name is rejected whole.
 *
 * login read the user name into a 64-byte buffer and stopped when it was
 * full, leaving the rest of the line unread; the password prompt then took
 * that remainder as the password.  This test types a 100-byte name at the
 * "login:" prompt (and nothing else) and checks that login rejects it
 * without ever asking for a password.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

int main(void) {
    printf("torture_login_longname\n");

    int in[2], out[2];
    pipe(in);
    pipe(out);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execl("/bin/login", "login", (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);

    char name[102];
    memset(name, 'u', 100);
    name[100] = '\n';
    name[101] = '\0';
    write(in[1], name, 101);        /* the write end stays open */

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
        got[n] = '\0';
        if (strstr(got, "incorrect"))
            break;
    }
    got[n] = '\0';
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    check("the name is rejected", strstr(got, "incorrect") != NULL,
          "no rejection within 5 s");
    check("no password is read from the name's tail", strstr(got, "Password") == NULL,
          "login went on to the password prompt");
    if (failures)
        printf("--- login output ---\n%s\n---\n", got);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
