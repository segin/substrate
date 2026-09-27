#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

/* Parse a device number operand: decimal digits only.  Returns 0 and sets
 * *out, or -1 for anything else. */
static int parse_num(const char *s, unsigned long *out) {
    char *end;
    if (s[0] < '0' || s[0] > '9') return -1;
    errno = 0;
    *out = strtoul(s, &end, 10);
    if (errno != 0 || *end != '\0') return -1;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("usage: mknod name type major minor\n");
        return 1;
    }

    mode_t mode = 0666;
    if (argv[2][0] == 'c') mode |= S_IFCHR;
    else if (argv[2][0] == 'b') mode |= S_IFBLK;
    else { printf("unknown type %s\n", argv[2]); return 1; }

    unsigned long major_n, minor_n;
    if (parse_num(argv[3], &major_n) < 0 || parse_num(argv[4], &minor_n) < 0) {
        printf("mknod: major and minor must be decimal numbers\n");
        return 1;
    }

    /* A number the device encoding cannot hold would spill into the other
     * field: accept only what survives the round trip. */
    dev_t dev = makedev(major_n, minor_n);
    if (major(dev) != major_n || minor(dev) != minor_n) {
        printf("mknod: major %s or minor %s out of range\n", argv[3], argv[4]);
        return 1;
    }

    if (mknod(argv[1], mode, dev) < 0) {
        perror("mknod");
        return 1;
    }
    return 0;
}
