/*
 * torture_ldd_setuid.c -- ldd lists a setuid program without running it.
 *
 * For a native dynamic program ldd executes it with LD_TRACE_LOADED_OBJECTS
 * set and lets ld.so print the libraries.  ld.so rightly ignores that
 * variable for a setuid or setgid program, so the program simply ran --
 * with its privileges, on the ldd user's behalf.  This test copies /bin/id
 * to a setuid-root file and, as uid 1000, runs ldd on it: the output must
 * list its libraries and contain nothing id printed.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define COPY "/tmp/ldd-setuid-id"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static int copy(const char *from, const char *to) {
    char buf[4096];
    ssize_t n;
    int in = open(from, O_RDONLY);
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (in < 0 || out < 0)
        return -1;
    while ((n = read(in, buf, sizeof(buf))) > 0)
        write(out, buf, (size_t)n);
    close(in);
    close(out);
    return 0;
}

int main(void) {
    printf("torture_ldd_setuid\n");
    mkdir("/tmp", 01777);
    if (copy("/bin/id", COPY) != 0 || chmod(COPY, 04755) != 0) {
        printf("setup failed\nResult: FAILED\n");
        return 1;
    }

    int out[2];
    pipe(out);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(out[1], 1);
        dup2(out[1], 2);
        close(out[0]);
        close(out[1]);
        if (setgid(1000) != 0 || setuid(1000) != 0)
            _exit(99);
        execl("/usr/bin/ldd", "ldd", COPY, (char *)NULL);
        _exit(127);
    }
    close(out[1]);
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
    }
    got[n] = '\0';
    int status;
    waitpid(pid, &status, 0);

    check("libraries listed", strstr(got, "libc.so.0") != NULL, "no libc.so.0 in output");
    check("the program did not run", strstr(got, "uid=") == NULL, "id's output appeared");
    if (failures)
        printf("--- ldd output ---\n%s---\n", got);

    unlink(COPY);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
