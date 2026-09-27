/*
 * torture_mknod_args.c -- mknod(1) checks its operands.
 *
 * mknod read argv[4] whenever it had 4 or more arguments, so an omitted
 * minor number crashed it; the numbers went through atoi() unchecked, and
 * a minor over 255 spilled into the major.  This test runs /bin/mknod as
 * root -- so a wrongly accepted operand really creates a node -- and
 * checks that each bad invocation exits 1 without crashing or creating
 * anything, and that a valid one creates the node it names.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

#define NODE "/tmp/mknod-test"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static int run(const char *type, const char *maj, const char *min) {
    pid_t pid = fork();
    if (pid == 0) {
        const char *argv[6] = { "mknod", NODE, type, maj, min, NULL };
        execv("/bin/mknod", (char **)argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return WEXITSTATUS(status);
}

static void rejects(const char *name, const char *type, const char *maj, const char *min) {
    unlink(NODE);
    int rc = run(type, maj, min);
    struct stat st;
    int made = lstat(NODE, &st) == 0;
    char why[96];
    snprintf(why, sizeof(why), "exit %d%s", rc, made ? ", node created" : "");
    check(name, rc == 1 && !made, why);
    unlink(NODE);
}

int main(void) {
    printf("torture_mknod_args\n");
    mkdir("/tmp", 01777);

    rejects("minor omitted", "c", "1", NULL);
    rejects("major and minor omitted", "b", NULL, NULL);
    rejects("minor over 255", "c", "1", "256");
    rejects("major over 255", "c", "256", "1");
    rejects("negative minor", "c", "1", "-1");
    rejects("non-numeric major", "c", "one", "1");
    rejects("trailing garbage", "c", "1", "3x");

    unlink(NODE);
    int rc = run("c", "1", "255");
    struct stat st;
    int ok = rc == 0 && lstat(NODE, &st) == 0 && S_ISCHR(st.st_mode) &&
             major(st.st_rdev) == 1 && minor(st.st_rdev) == 255;
    check("c 1 255 creates that node", ok, "wrong node or not created");
    unlink(NODE);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
