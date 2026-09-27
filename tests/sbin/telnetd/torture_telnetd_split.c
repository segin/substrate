/*
 * torture_telnetd_split.c -- a TELNET command split across reads is parsed.
 *
 * telnetd parsed each read() from the network on its own, so an IAC at the
 * end of one read and its command at the start of the next reached the pty
 * as data, and so did a subnegotiation split in two.  This test runs
 * telnetd on loopback with itself as the "login" program, which puts the
 * pty in raw mode and records every byte it receives.  The client sends
 *
 *     "a" IAC | WILL ECHO "b" IAC SB NAWS 0 80 | 0 24 IAC SE "c"
 *
 * as three separate writes, and the pty must deliver exactly "abc".
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define PORT    2324
#define RECORD  "/tmp/telnetd-split.out"

static void record_pty_input(void) {
    struct termios t;
    unsigned char buf[64];
    size_t n = 0;

    if (tcgetattr(0, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(0, TCSANOW, &t);
    }
    alarm(10);
    while (n < sizeof(buf)) {
        ssize_t r = read(0, buf + n, sizeof(buf) - n);
        if (r <= 0)
            break;
        n += (size_t)r;
        if (memchr(buf, 'c', n))
            break;
    }
    int fd = open(RECORD, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(fd, buf, n);
    close(fd);
}

int main(int argc, char **argv) {
    (void)argc;
    if (strcmp(argv[0], "login") == 0) {
        record_pty_input();
        return 0;
    }

    printf("torture_telnetd_split\n");
    unlink(RECORD);
    char portarg[16];
    snprintf(portarg, sizeof(portarg), "%d", PORT);
    pid_t tpid = fork();
    if (tpid == 0) {
        execl("/sbin/telnetd", "telnetd", "-f", "-p", portarg, "-l", argv[0],
              (char *)NULL);
        _exit(127);
    }
    sleep(1);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(PORT);
    sa.sin_addr.s_addr = htonl(0x7f000001);
    int connected = 0;
    for (int i = 0; i < 20 && !connected; i++) {
        if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == 0)
            connected = 1;
        else
            usleep(250000);
    }

    /* Let the login program start and set raw mode; drain the greeting. */
    sleep(1);
    unsigned char junk[64];
    struct pollfd pfd = { s, POLLIN, 0 };
    while (poll(&pfd, 1, 200) > 0 && read(s, junk, sizeof(junk)) > 0)
        ;

    static const unsigned char seg1[] = { 'a', 255 };
    static const unsigned char seg2[] = { 251, 1, 'b', 255, 250, 31, 0, 80 };
    static const unsigned char seg3[] = { 0, 24, 255, 240, 'c' };
    write(s, seg1, sizeof(seg1));
    usleep(300000);
    write(s, seg2, sizeof(seg2));
    usleep(300000);
    write(s, seg3, sizeof(seg3));
    sleep(2);

    close(s);
    kill(tpid, SIGKILL);
    waitpid(tpid, NULL, 0);

    unsigned char got[64];
    ssize_t n = -1;
    int fd = open(RECORD, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, got, sizeof(got));
        close(fd);
    }
    printf("  pty received %zd bytes:", n);
    for (ssize_t i = 0; i < n; i++)
        printf(" %02x", got[i]);
    printf("\n");
    int ok = connected && n == 3 && memcmp(got, "abc", 3) == 0;
    printf("  %s split commands are consumed, only \"abc\" arrives\n",
           ok ? "ok  " : "FAIL");
    printf("Result: %s\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
