/*
 * chrootesc — an absolute symlink inside a chroot stays inside it.
 *
 * Symlink targets beginning with '/' were resolved from the system's root
 * even for a chrooted process, so a link planted in the jail -- to "/", to
 * "/tmp", to any outside name -- walked straight out of it.  (Under a
 * personality, whose absolute names are tried under /perso/<name>/ first,
 * that lookup ignored the chroot as well.)
 *
 * As root: /tmp/chrootesc.d is the jail, with tmp/marker = "IN"; the real
 * /tmp/marker.chrootesc says "OUT".  In the jail:
 *   x -> /tmp/marker.chrootesc   (whole-path symlink)
 *   d -> /tmp                    (mid-path symlink)
 * After chroot, /x and /d/marker.chrootesc must read "IN".
 *
 * Built native (chrootesc) and as a static i386 Linux program
 * (chrootesc.linux); runs on the host too (sudo).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define JAIL "/tmp/chrootesc.d"

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static void put(const char *path, const char *text)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd >= 0) {
        write(fd, text, strlen(text));
        close(fd);
    }
}

/* What the file says, or "" if it cannot be read. */
static const char *says(const char *path)
{
    static char buf[16];
    int fd = open(path, O_RDONLY);
    ssize_t n = fd >= 0 ? read(fd, buf, sizeof(buf) - 1) : -1;
    if (fd >= 0)
        close(fd);
    buf[n > 0 ? n : 0] = '\0';
    return buf;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (geteuid() != 0) {
        printf("chrootesc: SKIP (needs root)\n");
        return 0;
    }
    mkdir("/tmp", 01777);
    mkdir(JAIL, 0755);
    mkdir(JAIL "/tmp", 01777);
    put("/tmp/marker.chrootesc", "OUT");
    put(JAIL "/tmp/marker.chrootesc", "IN");
    unlink(JAIL "/x");
    unlink(JAIL "/d");
    check(symlink("/tmp/marker.chrootesc", JAIL "/x") == 0 &&
          symlink("/tmp", JAIL "/d") == 0, "symlinks planted in the jail");

    pid_t pid = fork();
    if (pid == 0) {
        if (chroot(JAIL) != 0 || chdir("/") != 0)
            _exit(0x40);
        int bad = 0;
        bad |= (strcmp(says("/x"), "IN") != 0) << 0;
        bad |= (strcmp(says("/d/marker.chrootesc"), "IN") != 0) << 1;
        bad |= (strcmp(says("/tmp/marker.chrootesc"), "IN") != 0) << 2;
        _exit(bad);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    int bad = WIFEXITED(st) ? WEXITSTATUS(st) : 0xff;
    printf("      jail result bits %#x\n", bad);
    check(!(bad & 0x40), "chroot");
    check(!(bad & 1), "whole-path absolute symlink resolves in the jail");
    check(!(bad & 2), "mid-path absolute symlink resolves in the jail");
    check(!(bad & 4), "plain absolute name resolves in the jail");

    printf("chrootesc: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
