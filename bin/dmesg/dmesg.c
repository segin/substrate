/*
 * dmesg — print the kernel message ring buffer.
 *
 * Substrate keeps every kprint()/kprintf() byte in an in-kernel ring
 * buffer (sys/drivers/console/console.c) and exposes it read-only at
 * /proc/kmsg.  dmesg simply streams that file to stdout.  Reads are
 * non-consuming, so the log survives repeated invocations.
 *
 *   dmesg            dump the kernel log
 *   dmesg -V         print version and exit
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define KMSG_PATH "/proc/kmsg"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("dmesg (substrate)\n");
            return 0;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("usage: dmesg\n");
            return 0;
        }
        fprintf(stderr, "dmesg: unknown option '%s'\n", argv[i]);
        return 1;
    }

    int fd = open(KMSG_PATH, O_RDONLY);
    if (fd < 0) {
        perror("dmesg: " KMSG_PATH);
        return 1;
    }

    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                perror("dmesg: write");
                close(fd);
                return 1;
            }
            if (w == 0) {          /* no progress: avoid an infinite loop (DMESG-02) */
                fprintf(stderr, "dmesg: write: no progress\n");
                close(fd);
                return 1;
            }
            off += w;
        }
    }
    if (n < 0) {
        perror("dmesg: read");
        close(fd);
        return 1;
    }
    if (close(fd) != 0) {          /* check close (DMESG-03) */
        perror("dmesg: close");
        return 1;
    }
    return 0;
}
