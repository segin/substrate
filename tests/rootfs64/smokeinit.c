/*
 * smokeinit.c - init stand-in for the 64-bit image's boot test.
 *
 * tests/rootfs64/boot-test.sh boots rootfs64.img with
 * init=/usr/libexec/rootfs64/smokeinit.  As PID 1 this attaches the
 * console, runs smoke.sh from the same directory under /bin/sh and
 * prints one line the test waits for:
 *
 *     SMOKE64: PASS
 *     SMOKE64: FAIL (status N)
 *
 * and then stays alive, since an init that exits panics the kernel.  It
 * is built with the 64-bit cross compiler, so that the first program the
 * image runs is itself a product of that toolchain.
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define SMOKE_SCRIPT "/usr/libexec/rootfs64/smoke.sh"

int main(void) {
    int fd = open("/dev/console", O_RDWR);
    int status = 0;
    pid_t pid;

    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) close(fd);
    }

    pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", SMOKE_SCRIPT, (char *)0);
        _exit(127);
    }
    if (pid < 0 || waitpid(pid, &status, 0) < 0) {
        status = -1;
    }

    if (status == 0) {
        printf("SMOKE64: PASS\n");
    } else {
        printf("SMOKE64: FAIL (status %d)\n", status);
    }
    fflush(stdout);
    for (;;) {
        pause();
    }
}
