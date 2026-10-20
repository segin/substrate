/*
 * pktrebind — an AF_PACKET reader asleep in recv survives a bind.
 *
 * bind() freed the socket's netdev subscription and made a new one while
 * a reader was asleep on the old one's wait channel: the reader was left
 * on freed memory, never woken, and saw no frame again.  A thread here
 * blocks in recv; the socket is then rebound and frames sent through each
 * interface (one of them is lo, which hands them straight back); the
 * reader must get one.  (The old kernel usually passes too: the new
 * subscription tends to reuse the freed one's memory, wait channel and
 * all.)  Run as root.  Substrate only.
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_PACKET
#define AF_PACKET 17
#endif

struct sockaddr_ll {
    uint16_t sll_family;
    uint16_t sll_protocol;
    int32_t  sll_ifindex;
    uint16_t sll_hatype;
    uint8_t  sll_pkttype;
    uint8_t  sll_halen;
    uint8_t  sll_addr[8];
};

static int rfd;
static volatile int got = -1;

static void *reader(void *arg)
{
    unsigned char buf[1600];
    (void)arg;
    got = (int)recv(rfd, buf, sizeof(buf), 0);
    return NULL;
}

static int bind_to(int fd, int ifindex)
{
    struct sockaddr_ll a;
    memset(&a, 0, sizeof(a));
    a.sll_family = AF_PACKET;
    a.sll_protocol = 0x0300;            /* htons(ETH_P_ALL) */
    a.sll_ifindex = ifindex;
    return bind(fd, (struct sockaddr *)&a, sizeof(a));
}

int main(void)
{
    pthread_t t;
    struct timespec ms100 = { 0, 100000000 };

    rfd = socket(AF_PACKET, SOCK_RAW, 0x0300);
    if (rfd < 0) {
        printf("pktrebind: socket failed (errno %d): FAIL\n", errno);
        return 2;
    }
    pthread_create(&t, NULL, reader, NULL);
    nanosleep(&ms100, NULL);
    nanosleep(&ms100, NULL);            /* the reader is asleep by now */

    if (bind_to(rfd, 0) != 0) {
        printf("pktrebind: rebind failed (errno %d): FAIL\n", errno);
        return 2;
    }

    unsigned char frame[60];
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xff, 6);             /* broadcast */
    frame[12] = 0x88;                   /* a local-experimental ethertype */
    frame[13] = 0xb5;
    for (int round = 0; round < 20 && got < 0; round++) {
        for (int ifindex = 1; ifindex <= 4; ifindex++) {
            int sfd = socket(AF_PACKET, SOCK_RAW, 0x0300);
            if (sfd < 0) continue;
            if (bind_to(sfd, ifindex) == 0)
                send(sfd, frame, sizeof(frame), 0);
            close(sfd);
        }
        nanosleep(&ms100, NULL);
    }

    int ok = got > 0;
    printf("%s the reader, asleep across the rebind, got a frame (%d)\n",
           ok ? "ok   " : "FAIL ", got);
    printf("pktrebind: %s\n", ok ? "PASS" : "FAIL");
    fflush(stdout);
    _exit(ok ? 0 : 1);                  /* the reader may still be asleep */
}
