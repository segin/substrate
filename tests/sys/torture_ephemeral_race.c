/*
 * torture_ephemeral_race.c -- concurrent implicit binds must never share
 * an ephemeral port (docs/ip-audit-2026-09-22.md).  Run as init.
 *
 *   udp  UDP-API-18: six threads each connect() 150 UDP sockets (which
 *        binds them to a free ephemeral port) and keep them open; every
 *        port must be distinct.  The allocator used to check a port under
 *        afi_lock and record it only after dropping the lock.
 *   tcp  TCP-MEM-12: the same with non-blocking TCP connect()s to a
 *        black-holed address, so every PCB stays in SYN-SENT holding its
 *        port.  tcp_port_taken() walked the PCB list with no lock, and the
 *        port was assigned while the PCB was still CLOSED -- invisible to
 *        a concurrent scan.
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define T 6
#define N 150
static unsigned short ports[T][N];
static int fds[T][N];
static int stype;

static void *worker(void *a) {
    int t = (int)(long)a;
    struct sockaddr_in d = { .sin_family = AF_INET, .sin_port = htons(9) };
    /* UDP: loopback.  TCP: TEST-NET-1 via the default route, which nobody
     * answers, so the connection stays in SYN-SENT. */
    d.sin_addr.s_addr = htonl(stype == SOCK_DGRAM ? 0x7f000001 : 0xc0000201);
    for (int i = 0; i < N; i++) {
        int s = socket(AF_INET, stype, 0);
        fds[t][i] = s;
        if (stype == SOCK_STREAM)
            fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
        connect(s, (void *)&d, sizeof d);          /* implicit ephemeral bind */
        struct sockaddr_in me; socklen_t ml = sizeof me;
        memset(&me, 0, sizeof me);
        getsockname(s, (void *)&me, &ml);
        ports[t][i] = ntohs(me.sin_port);          /* all kept open: must differ */
    }
    return NULL;
}

static int run(const char *name, int type) {
    static unsigned char seen[65536];
    pthread_t th[T];
    int dup = 0, zero = 0;
    stype = type;
    memset(ports, 0, sizeof ports);
    memset(seen, 0, sizeof seen);
    for (long t = 0; t < T; t++) pthread_create(&th[t], NULL, worker, (void *)t);
    for (int t = 0; t < T; t++) pthread_join(th[t], NULL);
    for (int t = 0; t < T; t++) for (int i = 0; i < N; i++) {
        if (!ports[t][i]) { zero++; continue; }
        if (seen[ports[t][i]]++) dup++;
    }
    for (int t = 0; t < T; t++) for (int i = 0; i < N; i++)
        if (fds[t][i] >= 0) close(fds[t][i]);
    printf("%s: %d sockets, %d duplicate ports, %d unbound -- %s\n", name,
           T * N, dup, zero, (dup || zero) ? "FAIL" : "ok");
    return dup || zero;
}

int main(void) {
    printf("torture_ephrace\n");
    int failed = run("udp", SOCK_DGRAM);
    failed += run("tcp", SOCK_STREAM);
    printf("Result: %s\n", failed ? "FAILED" : "PASSED");
    return 0;
}
