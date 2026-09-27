/*
 * torture_hostname_print.c -- "hostname" with no arguments only prints.
 *
 * When the kernel's name was empty or "localhost", hostname read
 * /etc/hostname and called sethostname() with it before printing, so
 * merely asking the name could change it.  This test (as root, which may
 * set it) sets the name to "localhost", writes a different /etc/hostname,
 * runs /bin/hostname and checks that it printed "localhost" and the name
 * is unchanged.
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

int main(void) {
    char saved[512] = "", name[256] = "", out[256] = "";
    ssize_t saved_len = 0;

    printf("torture_hostname_print\n");
    int fd = open("/etc/hostname", O_RDONLY);
    if (fd >= 0) {
        saved_len = read(fd, saved, sizeof(saved));
        close(fd);
    }
    fd = open("/etc/hostname", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(fd, "fromfile\n", 9);
    close(fd);
    sethostname("localhost", 9);

    int p[2];
    pipe(p);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(p[1], 1);
        close(p[0]);
        close(p[1]);
        execl("/bin/hostname", "hostname", (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    ssize_t n = read(p[0], out, sizeof(out) - 1);
    out[n > 0 ? n : 0] = '\0';
    close(p[0]);
    waitpid(pid, NULL, 0);

    gethostname(name, sizeof(name));
    check("printed the kernel's name", strcmp(out, "localhost\n") == 0, out);
    check("the name is unchanged", strcmp(name, "localhost") == 0, name);

    fd = open("/etc/hostname", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (saved_len > 0)
        write(fd, saved, (size_t)saved_len);
    close(fd);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
