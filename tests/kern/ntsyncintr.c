/*
 * ntsyncintr — an ntsync wait with no timeout ends on a signal.
 *
 * NTSYNC_IOC_WAIT_ANY and WAIT_ALL with the timeout left at "never" slept
 * without looking at signals, so a thread waiting on an object nobody
 * would signal could be neither interrupted nor killed.  Waits on an
 * unsignaled event, each way, with a one-second alarm: both must come
 * back with EINTR.
 *
 * Substrate only (its own ntsync ioctls, <sys/ntsync.h>).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ntsync.h>
#include <unistd.h>

static void on_alarm(int sig)
{
    (void)sig;
}

static int wait_on(int dev, int ev, unsigned long op)
{
    struct ntsync_wait_args w;
    int objs[1] = { ev };

    memset(&w, 0, sizeof(w));
    w.timeout = UINT64_MAX;
    w.objs = (uintptr_t)objs;
    w.count = 1;
    w.owner = (uint32_t)getpid();       /* any nonzero thread id */
    alarm(1);
    errno = 0;
    int r = ioctl(dev, op, &w);
    int e = errno;
    alarm(0);
    return r == -1 ? e : 0;
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);

    int dev = open("/dev/ntsync", O_RDWR);
    if (dev < 0) {
        printf("ntsyncintr: no /dev/ntsync (errno %d): FAIL\n", errno);
        return 2;
    }
    struct ntsync_event_args ea = { 0, 0 };
    int ev = ioctl(dev, NTSYNC_IOC_CREATE_EVENT, &ea);
    if (ev < 0) {
        printf("ntsyncintr: CREATE_EVENT failed (errno %d): FAIL\n", errno);
        return 2;
    }

    int e1 = wait_on(dev, ev, NTSYNC_IOC_WAIT_ANY);
    printf("%s WAIT_ANY with no timeout ends on a signal (errno %d)\n",
           e1 == EINTR ? "ok   " : "FAIL ", e1);
    int e2 = wait_on(dev, ev, NTSYNC_IOC_WAIT_ALL);
    printf("%s WAIT_ALL with no timeout ends on a signal (errno %d)\n",
           e2 == EINTR ? "ok   " : "FAIL ", e2);

    int fail = e1 != EINTR || e2 != EINTR;
    printf("ntsyncintr: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}
