/*
 * torture_lseek_espipe.c -- lseek() on something unseekable is ESPIPE.
 *
 * Sockets are created as DTYPE_VNODE files, so lseek's DTYPE_SOCKET check
 * never fired and lseek(socket) "succeeded".  Checks sockets from socket()
 * (AF_UNIX and AF_INET), both ends of a socketpair, an accept()ed
 * connection, a pipe and a FIFO -- all ESPIPE -- and that a regular file
 * still seeks.
 *
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

static int failures;

static void expect_espipe(const char *what, int fd)
{
    off_t r;

    if (fd < 0) {
        printf("FAIL: %s: could not create (errno %d)\n", what, errno);
        failures++;
        return;
    }
    errno = 0;
    r = lseek(fd, 0, SEEK_CUR);
    if (r != -1 || errno != ESPIPE) {
        printf("FAIL: lseek(%s) returned %lld errno %d, want -1/ESPIPE\n",
               what, (long long)r, errno);
        failures++;
    }
}

int main(void)
{
    int sv[2], p[2], fd, lfd, cfd, afd;
    struct sockaddr_un sun;

    expect_espipe("AF_UNIX socket", socket(AF_UNIX, SOCK_STREAM, 0));
    expect_espipe("AF_INET socket", socket(AF_INET, SOCK_STREAM, 0));
    expect_espipe("AF_INET datagram socket", socket(AF_INET, SOCK_DGRAM, 0));

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        expect_espipe("socketpair end 0", sv[0]);
        expect_espipe("socketpair end 1", sv[1]);
    } else {
        expect_espipe("socketpair", -1);
    }

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, "/tmp/lseek.sock");
    unlink(sun.sun_path);
    lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    cfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd >= 0 && cfd >= 0 &&
        bind(lfd, (struct sockaddr *)&sun, sizeof(sun)) == 0 &&
        listen(lfd, 1) == 0 &&
        connect(cfd, (struct sockaddr *)&sun, sizeof(sun)) == 0) {
        afd = accept(lfd, NULL, NULL);
        expect_espipe("accepted connection", afd);
        expect_espipe("connected client", cfd);
        expect_espipe("listening socket", lfd);
    } else {
        expect_espipe("AF_UNIX connection setup", -1);
    }
    unlink(sun.sun_path);

    if (pipe(p) == 0) {
        expect_espipe("pipe read end", p[0]);
        expect_espipe("pipe write end", p[1]);
    }
    unlink("/tmp/lseek.fifo");
    if (mkfifo("/tmp/lseek.fifo", 0600) == 0)
        expect_espipe("FIFO", open("/tmp/lseek.fifo", O_RDWR));
    unlink("/tmp/lseek.fifo");

    fd = open("/tmp/lseek.file", O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0 || write(fd, "abcdef", 6) != 6 || lseek(fd, 2, SEEK_SET) != 2) {
        printf("FAIL: regular file does not seek\n");
        failures++;
    }
    unlink("/tmp/lseek.file");

    printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
