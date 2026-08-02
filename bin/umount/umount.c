/*
 * umount(8) — invoke the umount2(2) syscall.
 *
 * Usage: umount [-f] target
 *   -f   force unmount (MNT_FORCE) — drop the filesystem even if it
 *        still has open files.  Stale fds will start failing with EIO.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mount.h>

int main(int argc, char *argv[]) {
    int flags = 0;
    int opt;
    while ((opt = getopt(argc, argv, "f")) != -1) {
        switch (opt) {
        case 'f': flags |= MNT_FORCE; break;
        default:
            fprintf(stderr, "usage: umount [-f] target\n");
            return 1;
        }
    }
    if (argc - optind != 1) {
        fprintf(stderr, "usage: umount [-f] target\n");
        return 1;
    }
    const char *target = argv[optind];
    int rc = flags ? umount2(target, flags) : umount(target);
    if (rc < 0) {
        fprintf(stderr, "umount: %s: %s\n", target, strerror(errno));
        return 1;
    }
    return 0;
}
