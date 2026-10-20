/*
 * ptyvmin -- a raw read with VMIN=1 waits for a byte, whatever its length.
 *
 * tty_read took the smaller of VMIN and the read length after casting the
 * length to cc_t (unsigned char), so a read of 256, 512, 2048 ... bytes
 * wanted 0 characters and returned 0 -- end of file -- at once instead of
 * waiting.  Each read here starts on an empty slave while a child writes
 * one byte to the master a little later; every read must return that
 * byte.  Portable POSIX.
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

int main(void)
{
    static const int lens[] = { 1, 255, 256, 512, 2048, 4096 };
    static char buf[4096];
    int fails = 0;

    alarm(20);
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
        printf("ptyvmin: posix_openpt failed (errno %d): FAIL\n", errno);
        return 1;
    }
    int s = open(ptsname(m), O_RDWR | O_NOCTTY);
    if (s < 0) {
        printf("ptyvmin: open slave failed (errno %d): FAIL\n", errno);
        return 1;
    }

    struct termios t;
    tcgetattr(s, &t);
    t.c_iflag &= ~(ICRNL | IXON);
    t.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(s, TCSANOW, &t);

    for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        pid_t pid = fork();
        if (pid == 0) {
            usleep(200000);
            write(m, "x", 1);
            _exit(0);
        }
        ssize_t r = read(s, buf, (size_t)lens[i]);
        waitpid(pid, NULL, 0);
        if (r != 1 || buf[0] != 'x') {
            printf("ptyvmin: read of %d bytes returned %zd (errno %d)\n",
                   lens[i], r, r < 0 ? errno : 0);
            fails++;
            /* Take the byte that arrived after the early return. */
            usleep(100000);
            if (r == 0)
                read(s, buf, 1);
        }
    }

    printf("ptyvmin: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
