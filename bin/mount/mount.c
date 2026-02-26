#include <stdio.h>
#include <stdlib.h>

extern int mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data);

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("usage: mount dev dir type\n");
        return 1;
    }

    if (mount(argv[1], argv[2], argv[3], 0, NULL) < 0) {
        perror("mount");
        return 1;
    }
    return 0;
}

