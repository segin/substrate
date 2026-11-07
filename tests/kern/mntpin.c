/*
 * mntpin -- a mount point stays a mount point while ext2's node cache turns.
 *
 * The mount let go of the directory it covers, keeping only the pointer.
 * ext2 hands out nodes from a fixed cache of 256; once enough other lookups
 * had passed through it, the covered directory's slot was reused, its
 * FS_MOUNTPOINT flag went with it, and umount -- looking the directory up
 * afresh -- found no mount there (EINVAL).  Here procfs is mounted on a
 * new directory, 400 other files are looked up, and the umount must work.
 *
 * Run as init, as root, on an ext2 root holding an empty directory /mpdir
 * and a directory /mpdir.d of files f0 .. f399 (the root may be read-only):
 *
 *   mkdir -p img/mpdir img/mpdir.d
 *   for i in $(seq 0 399); do : > img/mpdir.d/f$i; done
 *   mke2fs -t ext2 -d img mntpin.img 32M
 *
 * Substrate only.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define NFILES 400

int main(void)
{
    char path[64];
    struct stat st;
    int ok;

    if (mount("proc", "/mpdir", "procfs", 0, NULL) != 0) {
        printf("mntpin: mount: errno %d (is /mpdir in the image?): FAIL\n", errno);
        return 1;
    }
    int found = 0;
    for (int i = 0; i < NFILES; i++) {
        snprintf(path, sizeof(path), "/mpdir.d/f%d", i);
        found += stat(path, &st) == 0;
    }
    if (found != NFILES) {
        printf("mntpin: %d of %d files in /mpdir.d: FAIL\n", found, NFILES);
        return 1;
    }

    int crossed = stat("/mpdir/self", &st) == 0;
    int rc = umount("/mpdir");
    int err = rc ? errno : 0;
    ok = rc == 0 && crossed;
    printf("%s the mount was still crossed (%d) and umount found it "
           "(%d, errno %d) after %d other lookups\n",
           ok ? "ok   " : "FAIL ", crossed, rc, err, NFILES);
    printf("mntpin: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
