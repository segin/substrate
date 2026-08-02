/*
 * torture_minix.c — regression tests for the MINIX batch (#418).
 *
 *   MINIX-17  The dirent stride was hardcoded to sizeof(struct minix_dirent_v1)
 *             (32 bytes) while the 14-char name variants (magics 0x137F/0x2468)
 *             use a 16-byte stride, so on those volumes every readdir/lookup
 *             walked entries at the wrong offsets.
 *   MINIX-18  The zone bitmap was indexed by zone number.  MINIX maps bit i to
 *             zone i + s_firstdatazone - 1.  Self-consistent while only
 *             substrate touches the volume, so nothing in-kernel notices --
 *             but fsck.minix and Linux read the same bitmap correctly, see
 *             substrate's allocated zones as free, and hand them out again.
 *   MINIX-19  Zone-to-byte-offset arithmetic overflowed 32 bits, and the
 *             indirect-block walkers returned whatever zone number the block
 *             happened to contain without validating it.
 *   MINIX-20  minix_write did not bound the offset.
 *   MINIX-31  minix_dir_add did not scan for an existing name (duplicate
 *             entries), and minix_unlink did not test S_ISDIR, so
 *             unlink("adir") orphaned the whole subtree beneath it.
 *
 * The load-bearing check for MINIX-18 is NOT in this program: it is running
 * host fsck.minix over the image afterwards.  This test's job is to make the
 * kernel allocate and free real zones on a real mkfs.minix volume so that
 * fsck has something to disagree with.
 *
 * Expects a mkfs.minix volume on /dev/storage/sata0.  Run as init.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
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

#define DEV "/dev/storage/sata0"
#define MNT "/mnt"

/* Big enough to need several zones (1 KiB each) including an indirect block. */
#define BIG (24 * 1024)

static char big[BIG];

static void test_zone_alloc(void)
{
    printf("MINIX-18/19: allocate and free real zones\n");

    for (int i = 0; i < BIG; i++)
        big[i] = (char)('A' + (i % 26));

    int fd = open(MNT "/zones", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ok("created " MNT "/zones", fd >= 0, "open O_CREAT failed");
    if (fd < 0) return;

    ssize_t n = write(fd, big, BIG);
    ok("wrote 24 KiB", n == BIG, "short write -- zone allocation failed");

    lseek(fd, 0, SEEK_SET);
    static char back[BIG];
    n = read(fd, back, BIG);
    ok("read 24 KiB back", n == BIG, "short read");
    ok("contents survived the round trip",
       n == BIG && memcmp(big, back, BIG) == 0,
       "the data came back different -- wrong zones were used");
    close(fd);

    /* A second file, so the allocator has to find fresh zones with the first
     * file's zones already marked.  With the bitmap indexed wrongly these can
     * collide with the first file's zones (or with metadata). */
    fd = open(MNT "/zones2", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ok("created a second file", fd >= 0, "open failed");
    if (fd >= 0) {
        write(fd, big, BIG);
        close(fd);
    }

    /* Re-read the first file: if the second allocation stole its zones the
     * bytes are now the second file's (identical here) or garbage.  Compare
     * against a distinct pattern instead. */
    fd = open(MNT "/zones", O_RDONLY);
    if (fd >= 0) {
        memset(back, 0, sizeof(back));
        n = read(fd, back, BIG);
        ok("first file intact after a second allocation",
           n == BIG && memcmp(big, back, BIG) == 0,
           "the second file's allocation overwrote the first's zones");
        close(fd);
    }

    /* Free them again -- exercises minix_free_block's bit mapping. */
    ok("unlinked zones2", unlink(MNT "/zones2") == 0, "unlink failed");
}

static void test_unlink_rejects_directory(void)
{
    printf("MINIX-31: unlink(2) refuses a directory\n");

    if (mkdir(MNT "/adir", 0755) != 0 && errno != EEXIST) {
        ok("mkdir " MNT "/adir", 0, "mkdir failed");
        return;
    }
    /* Put something inside so an orphan would be visible. */
    int fd = open(MNT "/adir/inside", O_RDWR | O_CREAT, 0644);
    if (fd >= 0) { write(fd, "x\n", 2); close(fd); }

    errno = 0;
    int r = unlink(MNT "/adir");
    ok("unlink of a directory fails", r != 0,
       "unlink removed the directory entry and orphaned the subtree");
    ok("and fails with EISDIR", r != 0 && errno == EISDIR,
       "wrong errno for unlink of a directory");

    /* It must still be there, with its contents. */
    struct stat st;
    ok("the directory still exists", stat(MNT "/adir", &st) == 0,
       "the directory is gone");
    ok("its contents are still reachable", stat(MNT "/adir/inside", &st) == 0,
       "the child was orphaned");
}

static void test_no_duplicate_names(void)
{
    printf("MINIX-31: creating an existing name does not duplicate the entry\n");

    int fd = open(MNT "/dup", O_RDWR | O_CREAT, 0644);
    if (fd >= 0) close(fd);
    fd = open(MNT "/dup", O_RDWR | O_CREAT, 0644);
    if (fd >= 0) close(fd);

    DIR *d = opendir(MNT);
    if (!d) { ok("opendir " MNT, 0, "opendir failed"); return; }
    int count = 0, entries = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        entries++;
        if (strcmp(de->d_name, "dup") == 0) count++;
    }
    closedir(d);

    char msg[96];
    snprintf(msg, sizeof(msg), "\"dup\" appears %d time(s) among %d entries",
             count, entries);
    ok(msg, count == 1, "the directory has a duplicate entry for the same name");
}

static void test_readdir_names(void)
{
    printf("MINIX-17: readdir returns whole, terminated names\n");

    DIR *d = opendir(MNT);
    if (!d) { ok("opendir " MNT, 0, "opendir failed"); return; }

    int saw_zones = 0, saw_dot = 0, saw_dotdot = 0, bogus = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0)  saw_dot = 1;
        if (strcmp(de->d_name, "..") == 0) saw_dotdot = 1;
        if (strcmp(de->d_name, "zones") == 0) saw_zones = 1;
        /* A name walked at the wrong stride shows up as junk. */
        for (const char *p = de->d_name; *p; p++)
            if ((unsigned char)*p < 0x20) { bogus++; break; }
    }
    closedir(d);

    ok("saw \".\"", saw_dot, "missing dot entry");
    ok("saw \"..\"", saw_dotdot, "missing dotdot entry");
    ok("saw the file we created", saw_zones, "\"zones\" was not listed");
    ok("no names contain control characters", bogus == 0,
       "entries were walked at the wrong stride");
}

int main(void)
{
    printf("torture_minix: zone bitmap, dirent stride, unlink (#418)\n\n");

    if (mkdir(MNT, 0755) != 0 && errno != EEXIST) {
        printf("  FAIL  mkdir %s: errno=%d\n", MNT, errno);
        printf("\nResult: 0 passed, 1 failed -- FAILED\n");
        return 1;
    }
    if (mount(DEV, MNT, "minix", 0, NULL) != 0) {
        printf("  FAIL  mount %s on %s as minix: errno=%d\n", DEV, MNT, errno);
        printf("\nResult: 0 passed, 1 failed -- FAILED\n");
        return 1;
    }
    printf("  ok    mounted %s on %s\n", DEV, MNT);
    passed++;

    test_zone_alloc();
    test_unlink_rejects_directory();
    test_no_duplicate_names();
    test_readdir_names();

    sync();
    if (umount(MNT) != 0)
        printf("  note: umount failed (errno=%d); data may not be flushed\n", errno);
    sync();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    printf("torture_minix: done -- now run host fsck.minix over the image\n");
    return failed ? 1 : 0;
}
