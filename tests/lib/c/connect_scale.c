/* connect_scale — time N concurrent AF_UNIX clients doing a request/reply
 * round-trip against one poll()-multiplexing server, for increasing N.
 * Reveals whether substrate's multi-client poll path scales (linear) or
 * melts down (super-linear → the g_poll_wake_chan thundering herd).
 * Portable: a real OS is roughly linear. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

static char g_path[96];

static long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static void set_nb(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
static int write1(int fd, char c) {
    for (;;) { ssize_t w = write(fd, &c, 1); if (w == 1) return 0; if (w < 0 && errno == EINTR) continue; return -1; }
}

struct cli { int idx; int ok; };
static void *cli_fn(void *p) {
    struct cli *a = p;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX; strncpy(sa.sun_path, g_path, sizeof(sa.sun_path) - 1);
    int conn = 0;
    for (int t = 0; t < 600; t++) { if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) { conn = 1; break; } usleep(3000); }
    if (!conn) { close(fd); return NULL; }
    char req = (char)(a->idx & 0x7f);
    if (write1(fd, req) == 0) {
        char rep = 0; struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 15000) > 0 && read(fd, &rep, 1) == 1 && rep == (char)(req + 1)) a->ok = 1;
    }
    close(fd);
    return NULL;
}

struct srv { int lfd; int n; volatile int stop; };
static void *srv_fn(void *p) {
    struct srv *s = p;
    int *cfd = calloc(s->n + 8, sizeof(int)); int nc = 0;
    set_nb(s->lfd);
    while (!s->stop) {
        struct pollfd *pf = calloc(nc + 1, sizeof(struct pollfd));
        pf[0].fd = s->lfd; pf[0].events = POLLIN;
        for (int i = 0; i < nc; i++) { pf[i + 1].fd = cfd[i]; pf[i + 1].events = POLLIN; }
        int r = poll(pf, nc + 1, 200);
        if (r > 0) {
            if (pf[0].revents & POLLIN) { int c; while ((c = accept(s->lfd, NULL, NULL)) >= 0) { set_nb(c); cfd[nc++] = c; if (nc >= s->n + 8) break; } }
            for (int i = 0; i < nc; i++) if (pf[i + 1].revents & POLLIN) { char q; if (read(cfd[i], &q, 1) == 1) { char rp = (char)(q + 1); (void)write1(cfd[i], rp); } }
        }
        free(pf);
    }
    for (int i = 0; i < nc; i++) close(cfd[i]);
    free(cfd);
    return NULL;
}

static long run_n(int n) {       /* returns ms, or -1 on failure */
    snprintf(g_path, sizeof g_path, "/tmp/cs_%ld_%d", (long)getpid(), n);
    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX; strncpy(sa.sun_path, g_path, sizeof(sa.sun_path) - 1);
    unlink(g_path);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof sa) != 0) return -1;
    if (listen(lfd, 128) != 0) return -1;
    struct srv s = { lfd, n, 0 }; pthread_t st; pthread_create(&st, NULL, srv_fn, &s);
    pthread_t *th = calloc(n, sizeof(pthread_t)); struct cli *ca = calloc(n, sizeof(struct cli));
    long t0 = now_ms();
    for (int i = 0; i < n; i++) { ca[i].idx = i; pthread_create(&th[i], NULL, cli_fn, &ca[i]); }
    int ok = 0;
    for (int i = 0; i < n; i++) { pthread_join(th[i], NULL); if (ca[i].ok) ok++; }
    long dt = now_ms() - t0;
    s.stop = 1; pthread_join(st, NULL);
    close(lfd); unlink(g_path);
    free(th); free(ca);
    return (ok == n) ? dt : -dt;   /* negative => some clients failed */
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int ns[] = { 4, 8, 16, 32, 64, 96, 128 };
    printf("connect_scale: N clients -> 1 poll server, request/reply round-trip\n");
    for (size_t i = 0; i < sizeof(ns)/sizeof(ns[0]); i++) {
        long ms = run_n(ns[i]);
        if (ms >= 0) printf("  N=%-3d  %6ld ms  (%.2f ms/client)\n", ns[i], ms, (double)ms / ns[i]);
        else         printf("  N=%-3d  %6ld ms  *** some clients FAILED ***\n", ns[i], -ms);
        fflush(stdout);
    }
    printf("done\n");
    return 0;
}
