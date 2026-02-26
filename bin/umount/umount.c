#include <stdio.h>
#include <stdlib.h>

extern int umount(const char *target);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: umount dir\n");
        return 1;
    }
    if (umount(argv[1]) < 0) {
        perror("umount");
        return 1;
    }
    return 0;
}

