/*
 * torture_radvd_prefix.c -- radvd accepts only prefix lengths 0 to 128.
 *
 * radvd took the prefix length with atoi() and used it unchecked to clear
 * the host bits of the prefix: a negative length indexed before the
 * prefix field and one over 128 went unnoticed.  This test needs an eth0
 * (boot with a NIC) so that the interface lookup cannot fail first.  It
 * expects "fec0::/-1", "fec0::/200" and "fec0::/x" to be refused at once,
 * and "fec0::/64" to start advertising (still running after 2 s).
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

/* Run radvd on eth0 with `prefix`; returns its exit status, or -1 if it
 * is still running after 2 s (then it is killed). */
static int run(const char *prefix) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/sbin/radvd", "radvd", "eth0", prefix, (char *)NULL);
        _exit(127);
    }
    int status;
    for (int i = 0; i < 20; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -1;
}

int main(void) {
    printf("torture_radvd_prefix\n");
    system("/sbin/ifconfig eth0 up >/dev/null 2>&1");
    check("fec0::/-1 refused", run("fec0::/-1") == 1, "not refused");
    check("fec0::/200 refused", run("fec0::/200") == 1, "not refused");
    check("fec0::/x refused", run("fec0::/x") == 1, "not refused");
    check("fec0::/64 advertises", run("fec0::/64") == -1, "radvd did not keep running");
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
