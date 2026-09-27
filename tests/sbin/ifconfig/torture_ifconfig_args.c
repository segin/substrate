/*
 * torture_ifconfig_args.c -- ifconfig reports bad requests in its status.
 *
 *   nosuch   "ifconfig nosuch" printed an error but exited 0;
 *   missing  a keyword with its argument missing ("ifconfig lo netmask")
 *            was parsed as an IPv4 address; it must fail with "missing
 *            argument" and leave the interface as it was.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <fcntl.h>
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

/* Run ifconfig with up to three arguments; capture stderr into `err`. */
static int run(char *err, size_t errsz, const char *a1, const char *a2, const char *a3) {
    int p[2];
    pipe(p);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(p[1], 2);
        int null = open("/dev/null", O_WRONLY);
        dup2(null, 1);
        close(p[0]);
        close(p[1]);
        const char *argv[] = { "ifconfig", a1, a2, a3, NULL };
        execv("/sbin/ifconfig", (char **)argv);
        _exit(127);
    }
    close(p[1]);
    ssize_t n = read(p[0], err, errsz - 1);
    err[n > 0 ? n : 0] = '\0';
    close(p[0]);
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

int main(void) {
    char err[512], why[600];
    printf("torture_ifconfig_args\n");

    int rc = run(err, sizeof(err), "nosuch", NULL, NULL);
    snprintf(why, sizeof(why), "exit %d", rc);
    check("nosuch: a nonexistent interface exits 1", rc == 1, why);

    const char *kw[] = { "netmask", "broadcast", "mtu", "gateway", "alias", "-alias" };
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        char name[64];
        rc = run(err, sizeof(err), "lo", kw[i], NULL);
        snprintf(name, sizeof(name), "missing: \"lo %s\" fails", kw[i]);
        snprintf(why, sizeof(why), "exit %d, stderr: %s", rc, err);
        check(name, rc == 1 && strstr(err, "missing argument") != NULL, why);
    }

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
