/*
 * torture_mount_noatime.c -- a filesystem-specific -o option takes effect.
 *
 * For a filesystem on a device the kernel replaced mount(2)'s data (the
 * option string) with the device node, so options such as ext2's
 * "noatime" never reached the filesystem.  Needs a second disk holding an
 * ext2 filesystem labelled "noatimetest" (mke2fs -L noatimetest).  The
 * test creates a file there, sets its atime to
 * 0 (older than its mtime, so ext2's relatime rule would refresh it on the
 * next read), then:
 *
 *   noatime   mounted with -o noatime, a read leaves the atime at 0;
 *   default   mounted without it, a read refreshes the atime;
 *   badopt    an option ext2 does not know makes the mount fail.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEV  "LABEL=noatimetest"
#define MNT  "/mnt/noatime"
#define FILE_ MNT "/f"

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
            execl("/bin/mount", "mount", "-o", opts, "-t", "ext2", DEV, MNT, (char *)NULL);
        else
            execl("/bin/mount", "mount", "-t", "ext2", DEV, MNT, (char *)NULL);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

/* Mount with `opts`, set the file's atime to 0, remount the same way,
 * read the file, and return its atime afterwards (-1 on error). */
static long atime_after_read(const char *opts) {
    char buf[16];
    struct stat st;
    struct timeval tv[2] = { { 0, 0 }, { 0, 0 } };

    if (run_mount(opts) != 0)
        return -1;
    int fd = open(FILE_, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(fd, "data\n", 5);
    close(fd);
    if (stat(FILE_, &st) != 0)
        return -1;
    tv[1].tv_sec = st.st_mtime;               /* keep mtime; atime 0 */
    utimes(FILE_, tv);
    umount(MNT);

    if (run_mount(opts) != 0)
        return -1;
    fd = open(FILE_, O_RDONLY);
    read(fd, buf, sizeof(buf));
    close(fd);
    long at = stat(FILE_, &st) == 0 ? (long)st.st_atime : -1;
    umount(MNT);
    return at;
}

int main(void) {
    char why[64];
    printf("torture_mount_noatime\n");
    mkdir("/mnt", 0755);
    mkdir(MNT, 0755);

    long at = atime_after_read("noatime");
    snprintf(why, sizeof(why), "atime %ld after the read", at);
    check("noatime: a read leaves the atime alone", at == 0, why);

    at = atime_after_read(NULL);
    snprintf(why, sizeof(why), "atime %ld after the read", at);
    check("default: a read refreshes the atime", at > 0, why);

    int rc = run_mount("nosuchoption");
    check("badopt: an unknown ext2 option fails the mount", rc != 0, "mount succeeded");
    if (rc == 0)
        umount(MNT);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
