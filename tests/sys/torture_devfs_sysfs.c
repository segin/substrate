/*
 * torture_devfs_sysfs.c — regression tests for the devfs/sysfs batch (#421).
 *
 *   DEVFS-12  node->inode and readdir's d_ino were the devfs_entry_t's heap
 *             ADDRESS, and sys_stat copies node->inode into st_ino -- so
 *             `stat /dev/tty` handed any user a kernel-heap pointer, a
 *             reliable layout oracle for aiming use-after-frees.
 *   DEVFS-21  readdir emitted no "." or ".." while finddir resolved them, so
 *             enumeration and lookup disagreed about /dev.
 *   SYSFS-14  /sys/bus listed bus, class and devices, and so did
 *             /sys/bus/bus, without limit -- any recursive walk of /sys ran
 *             forever.
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_devfs_sysfs"
 */
#include <dirent.h>
#include <errno.h>
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

/*
 * DEVFS-12.  A kernel heap address on this target is a high value (the
 * direct map starts at 0xC0000000) and, more tellingly, several devfs
 * entries would be scattered across a wide range.  A monotonic counter gives
 * small, densely packed values.  Check both properties: no inode may look
 * like a kernel pointer, and the spread across /dev must be modest.
 */
static void test_no_heap_address_in_ino(void)
{
    printf("DEVFS-12: st_ino is not a kernel heap address\n");

    DIR *d = opendir("/dev");
    if (!d) { ok("opendir /dev", 0, "cannot open /dev"); return; }

    unsigned long long lo = ~0ULL, hi = 0;
    int n = 0, kernelish = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        char path[320];
        struct stat st;
        if (de->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "/dev/%s", de->d_name);
        if (stat(path, &st) != 0) continue;
        unsigned long long ino = (unsigned long long)st.st_ino;
        if (ino == 0) continue;
        n++;
        if (ino > 0x40000000ULL) kernelish++;      /* looks like an address */
        if (ino < lo) lo = ino;
        if (ino > hi) hi = ino;
    }
    closedir(d);

    char msg[128];
    snprintf(msg, sizeof(msg), "%d entries, inode range %llu..%llu", n, lo, hi);
    ok(msg, n > 0, "no /dev entries could be stat'ed");
    ok("no inode looks like a kernel pointer", kernelish == 0,
       "st_ino carried a kernel-heap address");
    /* Densely packed identifiers, not scattered allocations. */
    ok("inode values are densely packed", n == 0 || (hi - lo) < 4096,
       "inode values are spread like heap addresses");
}

/* DEVFS-21.  readdir must offer the dot entries finddir already resolves. */
static void test_dot_entries(void)
{
    printf("DEVFS-21: /dev enumerates \".\" and \"..\"\n");

    static const char *dirs[] = { "/dev", "/dev/pts" };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        DIR *d = opendir(dirs[i]);
        if (!d) { printf("  skip  %s: cannot open\n", dirs[i]); continue; }
        int dot = 0, dotdot = 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0) dot = 1;
            else if (strcmp(de->d_name, "..") == 0) dotdot = 1;
        }
        closedir(d);
        char msg[96];
        snprintf(msg, sizeof(msg), "%s has both dot entries", dirs[i]);
        ok(msg, dot && dotdot, "readdir did not emit . and ..");
    }
}

/*
 * SYSFS-14.  Walk /sys recursively with a depth cap well above anything a
 * bounded tree needs.  A self-referential /sys/bus/bus/bus/... blows through
 * it immediately.
 */
#define MAXDEPTH 12
static int walk(const char *path, int depth, int *deepest)
{
    if (depth > *deepest) *deepest = depth;
    if (depth > MAXDEPTH) return 1;          /* too deep -- unbounded */

    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *de;
    int bad = 0;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        char child[512];
        struct stat st;
        snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            bad += walk(child, depth + 1, deepest);
            if (bad) break;
        }
    }
    closedir(d);
    return bad;
}

static void test_sysfs_terminates(void)
{
    printf("SYSFS-14: a recursive walk of /sys terminates\n");

    int deepest = 0;
    int bad = walk("/sys", 0, &deepest);
    char msg[96];
    snprintf(msg, sizeof(msg), "walked /sys, deepest nesting %d", deepest);
    ok(msg, bad == 0,
       "/sys is self-referential -- the walk hit the depth cap");
}

int main(void)
{
    printf("torture_devfs_sysfs: devfs inode/dots + sysfs depth (#421)\n\n");

    test_no_heap_address_in_ino();
    test_dot_entries();
    test_sysfs_terminates();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
