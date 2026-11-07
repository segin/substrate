/*
 * ptyclose -- a master read asleep while another thread closes the master.
 *
 * The read slept on the pair with nothing holding it, and the close freed
 * the pair (its slave was never opened) under it.  The reader then woke
 * on freed memory -- which, once a new pair had taken it, looked like a
 * live pair, so the reader slept again on the new pair and took the data
 * written to it.  A thread blocks reading master one; master one is closed
 * and master two opened; a byte written to slave two must reach master
 * two, and the first reader must return without data.  (Timing-dependent:
 * the old kernel can pass when the new pair lands elsewhere.)
 *
 * Run as init (or as anyone).  Substrate only.
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int m1;
static volatile ssize_t r1 = -2;

static void on_alarm(int sig)
{
    (void)sig;
    static const char msg[] = "ptyclose: hung: FAIL\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

static void *reader(void *arg)
{
    char c;
    (void)arg;
    r1 = read(m1, &c, 1);
    return NULL;
}

static int open_master(char *name, size_t cap)
{
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0)
        return -1;
    snprintf(name, cap, "%s", ptsname(m));
    return m;
}

int main(void)
{
    char n1[64], n2[64];
    struct timespec ms100 = { 0, 100000000 };
    pthread_t t;

    signal(SIGALRM, on_alarm);
    alarm(20);

    m1 = open_master(n1, sizeof(n1));
    if (m1 < 0) {
        printf("ptyclose: no pty: FAIL\n");
        return 1;
    }
    pthread_create(&t, NULL, reader, NULL);
    nanosleep(&ms100, NULL);
    nanosleep(&ms100, NULL);            /* the reader is asleep by now */

    close(m1);
    int m2 = open_master(n2, sizeof(n2));
    if (m2 < 0) {
        printf("ptyclose: no second pty: FAIL\n");
        return 1;
    }
    int s2 = open(n2, O_RDWR | O_NOCTTY);
    struct termios tio;
    if (s2 >= 0 && tcgetattr(s2, &tio) == 0) {
        tio.c_oflag &= ~OPOST;
        tio.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        tcsetattr(s2, TCSANOW, &tio);
    }
    write(s2, "X", 1);

    char c = 0;
    int fl = fcntl(m2, F_GETFL);
    fcntl(m2, F_SETFL, fl | O_NONBLOCK);
    ssize_t r2 = -1;
    for (int i = 0; i < 20 && r2 != 1; i++) {
        r2 = read(m2, &c, 1);
        if (r2 != 1)
            nanosleep(&ms100, NULL);
    }
    pthread_join(t, NULL);

    int ok = r2 == 1 && c == 'X' && r1 <= 0;
    printf("%s master two read %zd ('%c'); the reader of the closed master "
           "returned %zd\n", ok ? "ok   " : "FAIL ", r2, c ? c : '-', (ssize_t)r1);
    printf("ptyclose: %s\n", ok ? "PASS" : "FAIL");
    fflush(stdout);
    _exit(ok ? 0 : 1);
}
