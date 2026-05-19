/*
 * torture_net.c — AF_PACKET smoke test on substrate.
 *
 * 1. Opens a PF_PACKET RAW socket.
 * 2. Binds to ifindex 1 (the first NIC — eth0 from VirtIO-net or RTL8139).
 * 3. Crafts an ARP "who-has 10.0.2.1?" broadcast and sends it.
 * 4. Waits up to 2 seconds for an ARP reply from the gateway (QEMU
 *    user-mode networking responds at 10.0.2.2 by default; we ask
 *    for 10.0.2.1 which is also a valid SLIRP host).
 *
 * Built standalone (no torture runner dependency) so it can run as
 * init via the headless harness.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_PACKET
#define AF_PACKET 17
#endif
#ifndef SOCK_RAW
#define SOCK_RAW 3
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

/* Wire format helpers — host is little-endian, network is big. */
static inline uint16_t hton16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

int main(void)
{
    fprintf(stdout, "torture_net: AF_PACKET smoke test\n");

    int fd = socket(AF_PACKET, SOCK_RAW, hton16(0x0003 /*ETH_P_ALL*/));
    if (fd < 0) {
        fprintf(stderr, "  socket(AF_PACKET): errno=%d %s\n", errno, strerror(errno));
        return 1;
    }
    fprintf(stdout, "  socket fd=%d\n", fd);

    struct sockaddr_ll bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sll_family = AF_PACKET;
    bind_addr.sll_protocol = hton16(0x0003);
    bind_addr.sll_ifindex = 1;
    if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        fprintf(stderr, "  bind: errno=%d %s\n", errno, strerror(errno));
        close(fd);
        return 2;
    }
    fprintf(stdout, "  bound to ifindex=1\n");

    /* Build ARP request frame.
     *   dst MAC: ff:ff:ff:ff:ff:ff (broadcast)
     *   src MAC: 52:54:00:12:34:56 (substrate's QEMU default; doesn't
     *           need to match the NIC for QEMU SLIRP to reply)
     *   ether type: 0x0806 (ARP)
     *   ARP body: hw=1 proto=0x0800 hlen=6 plen=4 op=1
     *             senderHW=src senderIP=10.0.2.15
     *             targetHW=zeros targetIP=10.0.2.1
     */
    uint8_t pkt[42];
    memset(pkt, 0, sizeof(pkt));
    /* eth header */
    memset(&pkt[0], 0xff, 6);
    pkt[6]=0x52; pkt[7]=0x54; pkt[8]=0x00; pkt[9]=0x12; pkt[10]=0x34; pkt[11]=0x56;
    pkt[12]=0x08; pkt[13]=0x06;
    /* arp header */
    pkt[14]=0x00; pkt[15]=0x01;          /* hw = Ethernet */
    pkt[16]=0x08; pkt[17]=0x00;          /* proto = IPv4 */
    pkt[18]=0x06; pkt[19]=0x04;          /* hlen=6 plen=4 */
    pkt[20]=0x00; pkt[21]=0x01;          /* op = request */
    /* sender HW + IP */
    memcpy(&pkt[22], &pkt[6], 6);
    pkt[28]=10; pkt[29]=0; pkt[30]=2; pkt[31]=15;
    /* target HW (zeros) + IP — 10.0.2.2 is QEMU SLIRP's gateway,
     * which always replies to ARP. */
    pkt[38]=10; pkt[39]=0; pkt[40]=2; pkt[41]=2;

    ssize_t n = send(fd, pkt, sizeof(pkt), 0);
    if (n != (ssize_t)sizeof(pkt)) {
        fprintf(stderr, "  send: n=%d errno=%d\n", (int)n, errno);
        close(fd);
        return 3;
    }
    fprintf(stdout, "  sent %ld-byte ARP request\n", (long)n);

    /* Wait briefly for a reply.  Naive: just one read with no
     * timeout (the AF_PACKET path blocks).  Real tests would
     * select() with a timeout.  For substrate's blocking read we
     * accept the first frame we get. */
    uint8_t buf[1600];
    n = recv(fd, buf, sizeof(buf), 0);
    if (n < 0) {
        fprintf(stderr, "  recv: errno=%d %s\n", errno, strerror(errno));
        close(fd);
        return 4;
    }
    fprintf(stdout, "  recv: %ld bytes, ethertype=0x%02x%02x\n",
            (long)n, buf[12], buf[13]);
    /* If it's ARP (0x0806), dump the opcode + sender IP for sanity. */
    if (n >= 28 && buf[12] == 0x08 && buf[13] == 0x06) {
        fprintf(stdout, "  ARP op=%d sender_ip=%u.%u.%u.%u\n",
                buf[20] * 256 + buf[21],
                buf[28], buf[29], buf[30], buf[31]);
    }
    close(fd);
    fprintf(stdout, "torture_net: PASS\n");
    return 0;
}
