/*
 * torture_procfs.c — regression tests for the procfs batch (#420).
 *
 *   PROCFS-29  /proc/self resolved to "/proc/<pid>/" WITH a trailing slash
 *              where Linux returns "/proc/<pid>", so appending produced
 *              "/proc/42//exe" and every comparison against the result failed.
 *   PROCFS-30  procfs_finddir returned the node itself for "..", so
 *              `cd /proc; cd ..` stayed in /proc and "../etc/passwd" from a
 *              /proc cwd resolved inside procfs.
 *   PROCFS-16  /proc/<pid>/maps was mode 0444 -- any user could read another
 *              process's address-space layout, which is what defeats ASLR
 *              when attacking a setuid binary.
 *   PROCFS-19  proc_pid_status_read clamped back to 1023 bytes even when the
 *              larger retry allocation had succeeded, so the retry was dead
 *              code and a long status file was silently truncated.
 *
 * The credential half of PROCFS-15/16 cannot be shown from a single-user
 * test: this runs as root, and root is allowed to inspect everything.  What
 * IS checked here is that the gate did not break self-inspection, which is
 * the case every real /proc consumer depends on.
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_procfs"
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passed, failed;

static void ok(const char *what, int cond, const char *why)
{
    if (cond) {
        printf("  ok    %s\n", what);
        passed++;
    } else {
        printf("  FAIL  %s: %s (errno=%d)\n", what, why, errno);
        failed++;
    }
}

static void test_self_no_trailing_slash(void)
{
    printf("PROCFS-29: /proc/self has no trailing slash\n");

    char link[64];
    ssize_t n = readlink("/proc/self", link, sizeof(link) - 1);
    if (n < 0) { ok("readlink /proc/self", 0, "readlink failed"); return; }
    link[n] = '\0';

    ok("readlink returned something", n > 0, "empty target");
    ok("no trailing slash", n > 0 && link[n - 1] != '/',
       "target ends in '/', so appending yields a doubled separator");

    /* It must also name this process. */
    char expect[64];
    snprintf(expect, sizeof(expect), "/proc/%d", (int)getpid());
    ok("names the calling process", strcmp(link, expect) == 0,
       "target is not /proc/<our pid>");

    /* And the classic use: append a component and open it. */
    char path[128];
    snprintf(path, sizeof(path), "%s/status", link);
    int fd = open(path, O_RDONLY);
    ok("appending a component opens", fd >= 0, "concatenated path did not open");
    if (fd >= 0) close(fd);
}

static void test_dotdot_leaves_procfs(void)
{
    printf("PROCFS-30: \"..\" at /proc leaves procfs\n");

    /* /proc/../etc must be /etc.  If ".." stays inside procfs this either
     * fails outright or resolves to something in /proc. */
    struct stat a, b;
    int ra = stat("/proc/../etc", &a);
    int rb = stat("/etc", &b);
    ok("/proc/../etc resolves", ra == 0, "stat failed -- .. stayed inside procfs");
    ok("and it is the real /etc",
       ra == 0 && rb == 0 && a.st_ino == b.st_ino,
       "resolved to a different directory than /etc");
}

static void test_maps_mode(void)
{
    printf("PROCFS-16: /proc/<pid>/maps is not world-readable\n");

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)getpid());
    struct stat st;
    if (stat(path, &st) != 0) { ok("stat maps", 0, "stat failed"); return; }

    ok("mode is owner-only", (st.st_mode & 0077) == 0,
       "group/other can read another process's address-space layout");

    /* The owner must still be able to read it -- this is the case every
     * profiler and crash handler needs. */
    int fd = open(path, O_RDONLY);
    ok("the owner can still read it", fd >= 0, "self-inspection broke");
    if (fd >= 0) {
        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf));
        ok("and it returns content", n > 0, "read returned nothing");
        close(fd);
    }
}

static void test_status_not_truncated(void)
{
    printf("PROCFS-19: /proc/<pid>/status is not clamped to 1023 bytes\n");

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", (int)getpid());
    int fd = open(path, O_RDONLY);
    if (fd < 0) { ok("open status", 0, "open failed"); return; }

    /* Drain the whole file.  The bug clamped every read to 1023 bytes total;
     * this cannot prove the file is longer than that on this kernel, but it
     * does prove reads are consistent and terminate, and that the last byte
     * is not a hard 1023 boundary artefact. */
    char buf[4096];
    size_t total = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        total += (size_t)n;
        if (total > sizeof(buf)) break;
    }
    close(fd);

    char msg[96];
    snprintf(msg, sizeof(msg), "read %lu bytes of status", (unsigned long)total);
    ok(msg, total > 0, "status was empty");
    /* A file that stops at exactly 1023 is the signature of the old clamp. */
    ok("not stopped at the old 1023-byte clamp", total != 1023,
       "status ended exactly at the dead-retry boundary");
}

int main(void)
{
    printf("torture_procfs: /proc self-link, .., maps mode, status size (#420)\n\n");

    test_self_no_trailing_slash();
    test_dotdot_leaves_procfs();
    test_maps_mode();
    test_status_not_truncated();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
