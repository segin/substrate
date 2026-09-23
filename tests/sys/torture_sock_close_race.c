/*
 * torture_sock_close_race.c -- UDP-API-16 (docs/ip-audit-2026-09-22.md):
 * write() racing close() on an AF_INET socket.
 *
 * afinet_node_write() loaded node->impl with no lock and only then took its
 * reference, so a close() on another thread could free the socket between
 * the two: the writer incremented freed memory and later freed it again (a
 * UMA double-free kernel panic).  A write that lost the race to a torn-down
 * socket also "succeeded" with 0 bytes, which spins write-until-done loops.
 * 300 rounds of a writer thread against a close(); run as init.
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
static volatile int fd, zero_writes, done;
static void *writer(void *a) { (void)a; char b[512]; memset(b, 'x', sizeof b);
    while (!done) { ssize_t n = write(fd, b, sizeof b); if (n == 0) zero_writes++; if (n < 0 && errno == EBADF) break; }
    return NULL; }
int main(void) {
    printf("torture_wrace\n");
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(31949) };
    a.sin_addr.s_addr = htonl(0x7f000001);
    int rounds = 0;
    for (int i = 0; i < 300; i++) {
        fd = socket(AF_INET, SOCK_DGRAM, 0); connect(fd, (void *)&a, sizeof a);
        done = 0; pthread_t t; pthread_create(&t, NULL, writer, NULL);
        usleep(2000); close(fd); usleep(1000); done = 1; pthread_join(t, NULL); rounds++;
    }
    printf("rounds %d, zero-byte writes %d\nResult: %s\n", rounds, zero_writes, zero_writes ? "FAILED" : "PASSED");
    return 0;
}
