#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: cp <source> <dest>\n");
        return 1;
    }

    int src = open(argv[1], O_RDONLY, 0);
    if (src < 0) {
        printf("cp: cannot open source '%s'\n", argv[1]);
        return 1;
    }

    int dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst < 0) {
        printf("cp: cannot create dest '%s'\n", argv[2]);
        close(src);
        return 1;
    }

    char buf[1024];
    int n;
    while ((n = read(src, buf, sizeof(buf))) > 0) {
        write(dst, buf, n);
    }

    close(src);
    close(dst);
    return 0;
}

