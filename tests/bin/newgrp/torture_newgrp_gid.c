/*
 * torture_newgrp_gid.c -- newgrp refuses a numeric gid with no group.
 *
 * newgrp accepted any number as a gid without looking it up, although its
 * comment said the gid was confirmed to exist; a typo went unnoticed.
 * Root may take any gid, so the check is newgrp's own: this test runs it
 * as root with SHELL=/bin/true (the "shell" newgrp starts) and expects
 * "newgrp 99999" to fail while "newgrp 10" (wheel) and "newgrp wheel"
 * succeed.
 *
 * Runs as init (root); prints a "Result:" line.
 */
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

static int run(const char *group) {
    pid_t pid = fork();
    if (pid == 0) {
        setenv("SHELL", "/bin/true", 1);
        execl("/bin/newgrp", "newgrp", group, (char *)NULL);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

int main(void) {
    printf("torture_newgrp_gid\n");
    check("newgrp 99999 (no such group) fails", run("99999") == 1, "accepted");
    check("newgrp 10 (wheel) succeeds", run("10") == 0, "refused");
    check("newgrp wheel succeeds", run("wheel") == 0, "refused");
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
