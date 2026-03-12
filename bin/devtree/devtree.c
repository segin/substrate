#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int cat_proc(const char *path) {
    char buf[512];
    int fd;
    ssize_t n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, (size_t)n) != n) {
            perror("write");
            close(fd);
            return 1;
        }
    }
    close(fd);
    return n < 0 ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *path = "/proc/devtree";
    if (argc > 2) {
        fprintf(stderr, "usage: %s [path]\n", argv[0]);
        return 1;
    }
    if (argc == 2) {
        path = argv[1];
    }
    return cat_proc(path);
}
