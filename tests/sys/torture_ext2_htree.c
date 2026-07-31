/*
 * torture_ext2_htree.c — regression test for EXT2-08 (task #410).
 *
 * On a volume with dir_index (the mkfs.ext4 default, and set on substrate's
 * own rootfs), a directory that grows past one block gets EXT2_INDEX_FL and
 * an htree root: "." with rec_len 12, then ".." with
 * rec_len = block_size - 12, where that oversized ".." record HIDES
 * dx_root_info and the index array.
 *
 * Substrate honoured EXT2_INDEX_FL on the READ side only:
 *
 *  - ext2_add_entry saw a 12-byte "." and enormous slack, took the split
 *    path, and wrote a fresh dirent at byte offset 24 -- directly over
 *    h_hash_version / h_info_len / h_ind_levels and the first index
 *    entries, structurally corrupting the directory for every other ext2
 *    implementation.
 *
 *  - a name appended to a leaf was never inserted into the index, and
 *    ext2_finddir treated an htree miss as authoritative, so the file you
 *    just created was invisible (ENOENT).
 *
 * What this checks:
 *   1. an htree directory can be listed and its entries opened (the linear
 *      fallback works),
 *   2. creating in one fails cleanly instead of silently corrupting it.
 *
 * The load-bearing half is NOT here: it is running host e2fsck over the
 * image afterwards and finding the directory still intact.
 *
 * Expects the htree volume on /dev/storage/sata0.  Run as init.
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
#define BIG MNT "/big"

int main(void)
{
    printf("torture_ext2_htree: indexed-directory read + write refusal (#410)\n\n");

    if (mkdir(MNT, 0755) != 0 && errno != EEXIST) {
        printf("  FAIL  mkdir %s: errno=%d\n", MNT, errno);
        return 1;
    }
    if (mount(DEV, MNT, "ext2", 0, NULL) != 0) {
        printf("  FAIL  mount %s on %s as ext2: errno=%d\n", DEV, MNT, errno);
        printf("\nResult: 0 passed, 1 failed -- FAILED\n");
        return 1;
    }
    printf("  ok    mounted %s\n", DEV);
    passed++;

    /* 1. The htree directory must be enumerable. */
    DIR *d = opendir(BIG);
    ok("opendir " BIG, d != NULL, "could not open the indexed directory");
    int count = 0, saw_marker = 0;
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            count++;
            if (strcmp(de->d_name, "MARKER.txt") == 0) saw_marker = 1;
        }
        closedir(d);
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "listed %d entries", count);
    ok(msg, count > 2000, "far fewer entries than were created");
    ok("saw MARKER.txt in the listing", saw_marker, "marker missing from readdir");

    /* 2. A name in an htree directory must be openable.  This is what the
     *    authoritative-miss bug broke: the index lookup misses and the old
     *    code trusted it. */
    int fd = open(BIG "/MARKER.txt", O_RDONLY);
    ok("open " BIG "/MARKER.txt", fd >= 0,
       "lookup failed -- an htree miss is still being trusted");
    if (fd >= 0) {
        char buf[64];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) buf[n] = '\0'; else buf[0] = '\0';
        ok("and it reads back correctly",
           strncmp(buf, "hello-from-htree", 16) == 0, "wrong contents");
        close(fd);
    }

    /* Also prove a deep entry resolves, not just the first block. */
    fd = open(BIG "/file_2999.txt", O_RDONLY);
    ok("open a late entry (file_2999.txt)", fd >= 0,
       "a name beyond the first leaf did not resolve");
    if (fd >= 0) close(fd);

    /* 3. Creating in an indexed directory must FAIL rather than corrupt it. */
    errno = 0;
    fd = open(BIG "/NEWFILE.txt", O_RDWR | O_CREAT | O_EXCL, 0644);
    ok("create in an indexed directory is refused", fd < 0,
       "the create SUCCEEDED -- the htree root has probably been overwritten");
    if (fd >= 0) {
        close(fd);
    } else {
        ok("and it fails with EOPNOTSUPP", errno == EOPNOTSUPP,
           "refused, but with an unexpected errno");
    }

    sync();
    if (umount(MNT) != 0)
        printf("  note: umount failed (errno=%d)\n", errno);
    sync();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    printf("torture_ext2_htree: done -- now run host e2fsck over the image\n");
    return failed ? 1 : 0;
}
