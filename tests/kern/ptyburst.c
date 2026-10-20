/*
 * ptyburst -- a pty master write larger than the slave's input buffer
 * delivers every byte.
 *
 * The master fed each byte to the slave's line discipline without
 * waiting for room, so past the first 2 KiB of a burst the bytes were
 * dropped while write() reported them all sent.  A child writes 64 KiB
 * to the master in one call; the parent reads the raw slave slowly and
 * must get all 65536 bytes, in order.  Then, with the master
 * non-blocking and the slave full, a write must fail with EAGAIN (or
 * come back short) rather than claim success.  Portable POSIX.
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/wait.h>
#include <unistd.h>

#define BURST 65536

static void on_alarm(int sig)
{
    (void)sig;
    static const char msg[] = "ptyburst: reader starved, bytes lost: FAIL\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

static unsigned char pat(size_t i)
{
    return (unsigned char)(i * 7 + (i >> 8));
}

int main(void)
{
    static unsigned char buf[BURST];
    int fails = 0;

    signal(SIGALRM, on_alarm);
    alarm(30);
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
        printf("ptyburst: posix_openpt failed (errno %d): FAIL\n", errno);
        return 1;
    }
    int s = open(ptsname(m), O_RDWR | O_NOCTTY);
    if (s < 0) {
        printf("ptyburst: open slave failed (errno %d): FAIL\n", errno);
        return 1;
    }

    struct termios t;
    tcgetattr(s, &t);
    t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                   ICRNL | IXON | IXOFF);
    t.c_oflag &= ~OPOST;
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(s, TCSANOW, &t);

    pid_t pid = fork();
    if (pid == 0) {
        for (size_t i = 0; i < BURST; i++)
            buf[i] = pat(i);
        ssize_t w = write(m, buf, BURST);
        _exit(w == BURST ? 0 : 1);
    }

    size_t got = 0;
    while (got < BURST) {
        ssize_t r = read(s, buf + got, 1000);
        if (r <= 0) {
            printf("ptyburst: read returned %zd at %zu (errno %d)\n", r, got,
                   errno);
            break;
        }
        got += (size_t)r;
        if ((got / 1000) % 8 == 0)
            usleep(10000);      /* a slow reader */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (got != BURST) {
        printf("ptyburst: got %zu of %d bytes\n", got, BURST);
        fails++;
    } else {
        for (size_t i = 0; i < BURST; i++) {
            if (buf[i] != pat(i)) {
                printf("ptyburst: byte %zu is 0x%02x, want 0x%02x\n", i,
                       buf[i], pat(i));
                fails++;
                break;
            }
        }
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("ptyburst: writer's write came back short\n");
        fails++;
    }

    /* A full slave and a non-blocking master: no silent success. */
    fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
    for (size_t i = 0; i < BURST; i++)
        buf[i] = pat(i);
    ssize_t w = write(m, buf, BURST);
    if (w == BURST) {
        printf("ptyburst: non-blocking write of %d bytes into a full slave "
               "claimed success\n", BURST);
        fails++;
    } else if (w < 0 && errno != EAGAIN) {
        printf("ptyburst: non-blocking write failed with errno %d\n", errno);
        fails++;
    }

    printf("ptyburst: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
