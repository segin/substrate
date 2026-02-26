#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

extern int mknod(const char *pathname, mode_t mode, dev_t dev);

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("usage: mknod name type major minor\n");
        return 1;
    }

    mode_t mode = 0666;
    if (argv[2][0] == 'c') mode |= S_IFCHR;
    else if (argv[2][0] == 'b') mode |= S_IFBLK;
    else { printf("unknown type %s\n", argv[2]); return 1; }

    int major = atoi(argv[3]);
    int minor = atoi(argv[4]);
    // dev_t dev = (major << 8) | minor;
    // We don't have dev_t defined properly yet, use unsigned long
    unsigned long dev = (major << 8) | minor;

    if (mknod(argv[1], mode, dev) < 0) {
        perror("mknod");
        return 1;
    }
    return 0;
}

