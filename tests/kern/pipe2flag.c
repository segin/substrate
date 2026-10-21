/*
 * pipe2flag -- pipe2(O_NONBLOCK) shows in fcntl(F_GETFL) on both ends.
 *
 * sys_pipe2 or'ed the user-visible O_NONBLOCK (0x800) into f_flag, which
 * holds the kernel's F* bits, so the pipe ends really were non-blocking
 * but F_GETFL said they were not -- and a later F_SETFL that rewrote
 * f_flag from what F_GETFL returned left the stray bit behind.  Checks
 * F_GETFL on both ends after pipe2(O_NONBLOCK), that a read still gives
 * EAGAIN, and that F_SETFL can clear the flag again.  Portable POSIX
 * plus pipe2.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fds[2], fails = 0;
    char b;

    if (pipe2(fds, O_NONBLOCK) != 0) {
        printf("pipe2flag: pipe2 failed (errno %d): FAIL\n", errno);
        return 1;
    }
    for (int i = 0; i < 2; i++) {
        if (!(fcntl(fds[i], F_GETFL) & O_NONBLOCK)) {
            printf("pipe2flag: F_GETFL on the %s end lacks O_NONBLOCK\n",
                   i ? "write" : "read");
            fails++;
        }
    }
    if (read(fds[0], &b, 1) != -1 || errno != EAGAIN) {
        printf("pipe2flag: read of an empty non-blocking pipe did not "
               "fail with EAGAIN\n");
        fails++;
    }
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) & ~O_NONBLOCK);
    if (fcntl(fds[0], F_GETFL) & O_NONBLOCK) {
        printf("pipe2flag: F_SETFL could not clear O_NONBLOCK\n");
        fails++;
    }
    printf("pipe2flag: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
