/*
 * torture_ephemeral_race.c -- UDP-API-18 (docs/ip-audit-2026-09-22.md):
 * concurrent implicit binds must never share an ephemeral port.  Six
 * threads each connect() 150 UDP sockets (which binds them to a free
 * ephemeral port) and keep them open; every port must be distinct.  The
 * allocator used to check a port under afi_lock and record it only after
 * dropping the lock.  Run as init.
 */
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
static void *worker(void *a) {
    int t = (int)(long)a;
    struct sockaddr_in d = { .sin_family = AF_INET, .sin_port = htons(9) };
    d.sin_addr.s_addr = htonl(0x7f000001);
    for (int i = 0; i < N; i++) {
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        connect(s, (void *)&d, sizeof d);          /* implicit ephemeral bind */
        struct sockaddr_in me; socklen_t ml = sizeof me;
        getsockname(s, (void *)&me, &ml);
        ports[t][i] = ntohs(me.sin_port);          /* all kept open: must differ */
    }
    return NULL;
}
int main(void) {
    printf("torture_ephrace\n");
    pthread_t th[T];
    for (long t = 0; t < T; t++) pthread_create(&th[t], NULL, worker, (void *)t);
    for (int t = 0; t < T; t++) pthread_join(th[t], NULL);
    static unsigned char seen[65536]; int dup = 0, zero = 0;
    for (int t = 0; t < T; t++) for (int i = 0; i < N; i++) {
        if (!ports[t][i]) { zero++; continue; }
        if (seen[ports[t][i]]++) dup++;
    }
    printf("%d sockets, %d duplicate ports, %d unbound\nResult: %s\n", T * N, dup, zero, (dup || zero) ? "FAILED" : "PASSED");
    return 0;
}
