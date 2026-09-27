/*
 * torture_mount_opts.c -- mount -o options reach the filesystem.
 *
 * mount(8) passes its -o string to mount(2) as the data argument, but for
 * a filesystem on a device the kernel replaced the data pointer with the
 * device node, so the filesystem never saw the options.  This test mounts
 * the image's FAT boot partition with "mount -o ro" and checks that the
 * mount is read-only: creating a file fails with EROFS.  It also mounts it
 * read-write and checks a file can be created there.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEV "/dev/storage/virtio0p1"
#define MNT "/mnt/optstest"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static int run_mount(const char *opts) {
    pid_t pid = fork();
    if (pid == 0) {
        if (opts)
            execl("/bin/mount", "mount", "-o", opts, "-t", "fat", DEV, MNT, (char *)NULL);
        else
            execl("/bin/mount", "mount", "-t", "fat", DEV, MNT, (char *)NULL);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

int main(void) {
    char why[96];
    printf("torture_mount_opts\n");
    mkdir("/mnt", 0755);
    mkdir(MNT, 0755);

    check("mount -o ro succeeds", run_mount("ro") == 0, "mount failed");
    int fd = open(MNT "/rotest", O_WRONLY | O_CREAT, 0644);
    int err = errno;
    snprintf(why, sizeof(why), "create %s (%s)", fd >= 0 ? "succeeded" : "failed",
             fd >= 0 ? "-" : strerror(err));
    check("the ro mount refuses writes (EROFS)", fd < 0 && err == EROFS, why);
    if (fd >= 0) {
        close(fd);
        unlink(MNT "/rotest");
    }
    errno = 0;
    check("mkdir on the ro mount fails EROFS",
          mkdir(MNT "/rodir", 0755) < 0 && errno == EROFS, strerror(errno));

    /* An existing file (the image's GRUB configuration): it must not be
     * writable, renamable or removable. */
    const char *existing = MNT "/boot/grub/grub.cfg";
    struct stat est;
    if (stat(existing, &est) == 0) {
        errno = 0;
        fd = open(existing, O_WRONLY);
        err = errno;
        check("opening a file for writing fails EROFS", fd < 0 && err == EROFS,
              strerror(err));
        if (fd >= 0)
            close(fd);
        errno = 0;
        check("chmod fails EROFS", chmod(existing, 0600) < 0 && errno == EROFS,
              strerror(errno));
        errno = 0;
        check("rename fails EROFS",
              rename(existing, MNT "/renamed") < 0 && errno == EROFS, strerror(errno));
        errno = 0;
        check("unlink fails EROFS", unlink(existing) < 0 && errno == EROFS,
              strerror(errno));
        fd = open(existing, O_RDONLY);
        check("reading still works", fd >= 0, strerror(errno));
        if (fd >= 0)
            close(fd);
    } else {
        check("boot/grub/grub.cfg exists", 0, "missing");
    }
    umount(MNT);

    check("mount (rw) succeeds", run_mount(NULL) == 0, "mount failed");
    fd = open(MNT "/rwtest", O_WRONLY | O_CREAT, 0644);
    check("the rw mount accepts writes", fd >= 0, strerror(errno));
    if (fd >= 0) {
        close(fd);
        unlink(MNT "/rwtest");
    }
    umount(MNT);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
