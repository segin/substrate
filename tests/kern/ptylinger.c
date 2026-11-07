/*
 * ptylinger -- a slave's last output survives a second session.
 *
 * When the slave closes with output still queued behind a full master
 * ring, the tail is kept for the master to read before end-of-file.  A
 * slave opened, written and closed again before the master read anything
 * replaced that kept tail with its own: the first session's tail was lost
 * and its buffer leaked.  Here session one writes 3048 'A's (2048 fill
 * the ring, 1000 are left over) and session two 500 'B's; the master,
 * reading only then, must get every 'A' and every 'B'.
 *
 * Run as init (or as anyone).  Substrate only: the ring sizes are its.
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define RING    2048            /* PTY_MASTER_BUF_SIZE */
#define A_COUNT (RING + 1000)
#define B_COUNT 500

static void on_alarm(int sig)
{
    (void)sig;
    static const char msg[] = "ptylinger: hung: FAIL\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

/* One slave session: open raw, write `count` of `c`, close. */
static int session(const char *name, char c, int count)
{
    char buf[A_COUNT];
    struct termios t;
    int s = open(name, O_RDWR | O_NOCTTY);

    if (s < 0) {
        printf("ptylinger: open %s: errno %d\n", name, errno);
        return -1;
    }
    if (tcgetattr(s, &t) == 0) {
        t.c_oflag &= ~OPOST;
        t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        tcsetattr(s, TCSANOW, &t);
    }
    memset(buf, c, (size_t)count);
    ssize_t w = write(s, buf, (size_t)count);
    close(s);
    if (w != count) {
        printf("ptylinger: session '%c' wrote %zd of %d\n", c, w, count);
        return -1;
    }
    return 0;
}

int main(void)
{
    signal(SIGALRM, on_alarm);
    alarm(20);

    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
        printf("ptylinger: no pty: FAIL\n");
        return 1;
    }
    char name[64];
    snprintf(name, sizeof(name), "%s", ptsname(m));

    if (session(name, 'A', A_COUNT) != 0 || session(name, 'B', B_COUNT) != 0) {
        printf("ptylinger: FAIL\n");
        return 1;
    }

    int a = 0, b = 0, other = 0;
    char buf[1024];
    ssize_t n;
    while ((n = read(m, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == 'A')
                a++;
            else if (buf[i] == 'B')
                b++;
            else
                other++;
        }
    }

    int ok = a == A_COUNT && b == B_COUNT && other == 0;
    printf("%s the master read %d of %d 'A's and %d of %d 'B's (%d other)\n",
           ok ? "ok   " : "FAIL ", a, A_COUNT, b, B_COUNT, other);
    printf("ptylinger: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
