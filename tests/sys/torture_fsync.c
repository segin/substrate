/*
 * torture_fsync.c -- fsync() and fdatasync() really reach the device.
 *
 * libc's fsync was a no-op and the kernel's sys_fsync a stub, so nothing
 * an editor or a database "synced" left the buffer cache, let alone the
 * disk's write cache.
 *
 * Normal boot:
 *   ebadf    fsync(-1) and fsync(closed fd) fail with EBADF;
 *   einval   a pipe, a socketpair end and a FIFO fail with EINVAL;
 *   ok       a written regular file, a directory, fdatasync: 0.
 *
 * With an extra disk holding an ext2 file system, attached through QEMU's
 * blkdebug driver so every flush fails, and its device path written to
 * /fsync-flush-dev (the harness knows which name it got):
 *   eio      fsync() of a file written on it fails with EIO -- which is
 *            only possible if the flush actually went to the device.
 *
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>

static int failures;

static void expect(int rc, int want_errno, const char *what)
{
    if (want_errno == 0) {
        if (rc != 0) {
            printf("FAIL: %s: returned %d, errno %d\n", what, rc, errno);
            failures++;
        }
    } else if (rc != -1 || errno != want_errno) {
        printf("FAIL: %s: returned %d, errno %d; want -1/%d\n",
               what, rc, rc == -1 ? errno : 0, want_errno);
        failures++;
    }
}

static void normal_cases(void)
{
    int p[2], sv[2], fd;
    char buf[4096];

    expect(fsync(-1), EBADF, "fsync(-1)");
    fd = open("/tmp/fsync.closed", O_CREAT | O_RDWR | O_TRUNC, 0600);
    close(fd);
    expect(fsync(fd), EBADF, "fsync(closed fd)");

    if (pipe(p) == 0) {
        expect(fsync(p[0]), EINVAL, "fsync(pipe read end)");
        expect(fsync(p[1]), EINVAL, "fsync(pipe write end)");
        close(p[0]);
        close(p[1]);
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        expect(fsync(sv[0]), EINVAL, "fsync(socket)");
        close(sv[0]);
        close(sv[1]);
    }
    unlink("/tmp/fsync.fifo");
    if (mkfifo("/tmp/fsync.fifo", 0600) == 0) {
        fd = open("/tmp/fsync.fifo", O_RDWR);
        expect(fsync(fd), EINVAL, "fsync(FIFO)");
        close(fd);
        unlink("/tmp/fsync.fifo");
    }

    fd = open("/tmp/fsync.data", O_CREAT | O_RDWR | O_TRUNC, 0600);
    memset(buf, 'x', sizeof(buf));
    if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
        printf("FAIL: write\n");
        failures++;
    }
    expect(fsync(fd), 0, "fsync(written file)");
    expect(fdatasync(fd), 0, "fdatasync(written file)");
    close(fd);
    unlink("/tmp/fsync.data");
    unlink("/tmp/fsync.closed");

    fd = open("/tmp", O_RDONLY);
    expect(fsync(fd), 0, "fsync(directory)");
    close(fd);
}

static void flush_error_case(const char *dev)
{
    char buf[4096], cmd[160];
    int fd;

    mkdir("/mnt", 0755);
    mkdir("/mnt/f", 0755);
    snprintf(cmd, sizeof(cmd), "mount %s /mnt/f ext2", dev);
    if (system(cmd) != 0) {
        printf("FAIL: %s\n", cmd);
        failures++;
        return;
    }
    fd = open("/mnt/f/data", O_CREAT | O_RDWR | O_TRUNC, 0600);
    memset(buf, 'y', sizeof(buf));
    if (fd < 0 || write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
        printf("FAIL: write on the blkdebug disk\n");
        failures++;
    }
    expect(fsync(fd), EIO, "fsync with a failing device flush");
    close(fd);
}

int main(void)
{
    char dev[64] = "";
    FILE *f = fopen("/fsync-flush-dev", "r");

    if (f != NULL) {
        if (fgets(dev, sizeof(dev), f) != NULL)
            dev[strcspn(dev, "\n")] = '\0';
        fclose(f);
    }
    if (dev[0] != '\0') {
        printf("mode: device flush fails (blkdebug) on %s\n", dev);
        flush_error_case(dev);
    } else {
        printf("mode: normal\n");
        normal_cases();
    }
    printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
