/*
 * lockyield -- a server polling sockets that busy clients keep locked is
 * still served promptly.
 *
 * A thread that found a kernel mutex held slept until the owner released
 * it, and the owner -- preempted by the timer in the middle of its
 * critical section -- got the CPU back only after every other runnable
 * thread had had a timeslice.  A server multiplexing N clients that hammer
 * their sockets meets such a lock on almost every syscall, so each one
 * cost it N timeslices: with 64 clients one poll() pass took seconds and
 * torture_poll_mc's quiet client went unanswered.  A blocked waiter now
 * hands the CPU straight to a preempted owner.
 *
 * Forks a poll() server and 64 flooder processes hammering it over
 * AF_UNIX, lets them run, then times one request from a quiet client: it
 * must be answered within 2 s (the kernel before the fix took 5-7 s at 64
 * flooders).  Portable POSIX.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FLOODERS 64

static struct sockaddr_un sa;

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void set_nb(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static int dial(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    for (int t = 0; t < 1000; t++) {
        if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0)
            return fd;
        usleep(2000);
    }
    close(fd);
    return -1;
}

/* Answer every byte with byte+1, polling all clients. */
static void server(int lfd)
{
    static int cfd[FLOODERS + 8];
    static struct pollfd pf[FLOODERS + 9];
    int nc = 0;

    set_nb(lfd);
    for (;;) {
        pf[0].fd = lfd;
        pf[0].events = POLLIN;
        for (int i = 0; i < nc; i++) {
            pf[i + 1].fd = cfd[i];
            pf[i + 1].events = POLLIN;
        }
        if (poll(pf, nc + 1, 200) <= 0)
            continue;
        if (pf[0].revents & POLLIN) {
            int c;
            while (nc < FLOODERS + 8 && (c = accept(lfd, NULL, NULL)) >= 0) {
                set_nb(c);
                cfd[nc++] = c;
            }
        }
        for (int i = 0; i < nc; i++) {
            char q, r;
            if ((pf[i + 1].revents & POLLIN) && read(cfd[i], &q, 1) == 1) {
                r = (char)(q + 1);
                write(cfd[i], &r, 1);
            }
        }
    }
}

static void flooder(void)
{
    int fd = dial();
    char b[64];

    if (fd < 0)
        _exit(1);
    set_nb(fd);
    for (;;) {
        char c = 'F';
        write(fd, &c, 1);
        while (read(fd, b, sizeof b) > 0)
            ;
    }
}

int main(void)
{
    pid_t kids[FLOODERS + 1];
    int nk = 0;

    alarm(60);
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "/tmp/lockyield.%d", (int)getpid());
    unlink(sa.sun_path);
    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(lfd, 128) != 0) {
        printf("lockyield: listen failed (errno %d): FAIL\n", errno);
        return 1;
    }
    if ((kids[nk++] = fork()) == 0)
        server(lfd);
    close(lfd);
    for (int i = 0; i < FLOODERS; i++)
        if ((kids[nk++] = fork()) == 0)
            flooder();
    usleep(500000);                     /* let the flood build */

    long waited = -1;
    int q = dial();
    if (q >= 0) {
        char c = 'Q';
        long t0 = now_ms();
        struct pollfd pf = { q, POLLIN, 0 };
        if (write(q, &c, 1) == 1 && poll(&pf, 1, 10000) == 1 &&
            read(q, &c, 1) == 1 && c == 'R')
            waited = now_ms() - t0;
        close(q);
    }

    for (int i = 0; i < nk; i++)
        kill(kids[i], SIGKILL);
    for (int i = 0; i < nk; i++)
        waitpid(kids[i], NULL, 0);
    unlink(sa.sun_path);

    if (waited < 0 || waited > 2000) {
        printf("lockyield: quiet client %s with %d flooders: FAIL\n",
               waited < 0 ? "not answered in 10 s" : "answered late", FLOODERS);
        if (waited >= 0)
            printf("lockyield: waited %ld ms\n", waited);
        return 1;
    }
    printf("lockyield: answered in %ld ms with %d flooders\n", waited, FLOODERS);
    printf("lockyield: PASS\n");
    return 0;
}
