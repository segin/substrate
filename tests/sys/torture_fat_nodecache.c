/*
 * torture_fat_nodecache.c — regression test for FAT/exFAT-F3 (task #413).
 *
 * fat_alloc_node() returned &fat_fs_node_cache[idx++ % 64] and
 * exfat_alloc_node() slot 1 + idx % 127, memsetting the slot on reuse.
 * sys_open stores that pointer DIRECTLY in f->f_data for regular files (only
 * character devices are cloned), so once enough further path lookups happened
 * on the mount the slot was recycled to a different file -- and the process
 * holding the original fd started reading and writing THAT file instead, with
 * no error.  Cross-file disclosure and corruption.
 *
 * The test is the thing that made impossible: hold an fd open across more
 * lookups than the cache has slots, then read it and check the bytes are
 * still the file's own.
 *
 * Expects a FAT filesystem mounted at /mnt containing secret.txt plus enough
 * other files to cycle the cache.  Run as init after mounting.
 */
#include <dirent.h>
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

#define MNT     "/mnt"
#define SECRET  MNT "/secret.txt"
static const char EXPECT[] = "SECRET-CONTENT-DO-NOT-RECYCLE\n";

int main(void)
{
    printf("torture_fat_nodecache: FAT node cache recycling (#413)\n\n");

    /* Baseline: the file reads correctly with nothing else going on. */
    int fd = open(SECRET, O_RDONLY);
    ok("opened " SECRET, fd >= 0, "open failed -- is the FAT image mounted?");
    if (fd < 0) {
        printf("\nResult: %d passed, %d failed -- FAILED\n", passed, failed + 1);
        return 1;
    }

    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) buf[n] = '\0'; else buf[0] = '\0';
    ok("baseline read returns its own content",
       n > 0 && strncmp(buf, EXPECT, strlen(EXPECT)) == 0,
       "the file did not read back correctly even before any churn");

    /* Rewind and hold the fd across a lot of lookups on the same mount.  The
     * FAT cache is 64 slots and exFAT's 127, so walking the directory and
     * stat'ing every entry several times over is far more than enough to
     * cycle any slot the old allocator would have handed out. */
    lseek(fd, 0, SEEK_SET);

    int lookups = 0;
    for (int pass = 0; pass < 4; pass++) {
        DIR *d = opendir(MNT);
        if (!d) break;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            char path[320];
            struct stat st;
            if (de->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), MNT "/%s", de->d_name);
            if (stat(path, &st) == 0) lookups++;
            /* Opening and closing churns the cache harder than stat alone. */
            int t = open(path, O_RDONLY);
            if (t >= 0) close(t);
        }
        closedir(d);
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "performed %d lookups while holding the fd", lookups);
    ok(msg, lookups >= 64, "not enough churn to cycle the cache -- test inconclusive");

    /* The load-bearing check: the fd must still be the file we opened. */
    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) buf[n] = '\0';
    ok("the held fd still reads its own file",
       n > 0 && strncmp(buf, EXPECT, strlen(EXPECT)) == 0,
       "the fd was silently redirected to a different file");
    if (n > 0 && strncmp(buf, EXPECT, strlen(EXPECT)) != 0) {
        printf("        got: '%.40s'\n", buf);
    }

    close(fd);

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
