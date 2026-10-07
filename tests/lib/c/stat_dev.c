/* stat_dev -- stat(2) says which filesystem a file is in.
 *
 * st_dev used to be 0 for every file, so two files with the same inode
 * number on different mounts were, by (st_dev, st_ino), the same file.
 * Every mount now has a number of its own.
 *
 *     i386-unknown-substrate-gcc -o stat_dev stat_dev.c
 *     ./stat_dev [MOUNTPOINT]
 *
 * With a MOUNTPOINT (a filesystem mounted by hand) it is checked as well.
 * Prints a line per check and "stat_dev: PASS" or "FAIL".  Run on
 * substrate.
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

static void check(const char *what, int ok) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

int main(int argc, char **argv) {
    struct stat root, bin, sh, dev, null, proc, file, open_file, mnt, mnt_dot;
    char path[256];
    int fd;

    stat("/", &root);
    stat("/bin", &bin);
    stat("/bin/sh", &sh);
    stat("/dev", &dev);
    stat("/dev/null", &null);
    stat("/proc", &proc);
    printf("  st_dev: / %u  /bin/sh %u  /dev %u  /dev/null %u  /proc %u\n",
           (unsigned)root.st_dev, (unsigned)sh.st_dev, (unsigned)dev.st_dev,
           (unsigned)null.st_dev, (unsigned)proc.st_dev);
    check("/ has a device number", root.st_dev != 0);
    check("/bin and /bin/sh are on /'s",
          bin.st_dev == root.st_dev && sh.st_dev == root.st_dev);
    check("/dev is another filesystem",
          dev.st_dev != 0 && dev.st_dev != root.st_dev);
    check("/dev/null is on /dev's", null.st_dev == dev.st_dev);
    check("/proc is a third", proc.st_dev != 0 &&
          proc.st_dev != root.st_dev && proc.st_dev != dev.st_dev);

    fd = open("/tmp/stat_dev.tmp", O_RDWR | O_CREAT, 0600);
    fstat(fd, &open_file);
    stat("/tmp/stat_dev.tmp", &file);
    check("fstat and stat agree", open_file.st_dev != 0 &&
          open_file.st_dev == file.st_dev && open_file.st_ino == file.st_ino);
    close(fd);
    unlink("/tmp/stat_dev.tmp");

    if (argc > 1 && stat(argv[1], &mnt) == 0) {
        snprintf(path, sizeof(path), "%s/.", argv[1]);
        stat(path, &mnt_dot);
        check("the mounted filesystem has its own number",
              mnt.st_dev != 0 && mnt.st_dev != root.st_dev);
        check("the mount point is the mounted root",
              mnt.st_dev == mnt_dot.st_dev && mnt.st_ino == mnt_dot.st_ino);
    }

    printf("stat_dev: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
