#include <stdio.h>
#include <unistd.h> // for rename usually in stdio or unistd, assuming unistd via stdio stub or similar. libc/stdio.h has rename prototype.

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: mv <source> <dest>\n");
        return 1;
    }
    if (rename(argv[1], argv[2]) != 0) {
        printf("mv: cannot move '%s' to '%s'\n", argv[1], argv[2]);
        return 1;
    }
    return 0;
}

