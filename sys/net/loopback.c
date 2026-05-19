/*
 * loopback.c — "lo" netdev that bounces frames back to the IP layer
 * without any actual hardware involved.  Used to test the IP stack
 * end-to-end in environments where the test peer isn't cooperative
 * (e.g. QEMU SLIRP's IPv6 ND).
 *
 * Loopback semantics: xmit hands the frame straight to netdev_rx,
 * which dispatches via inet_eth_input → ip{4,6}_input.  The IP
 * layer's destination check matches because lo's addresses include
 * 127.0.0.1 and ::1.
 *
 * The ARP/ND bypass lives in inet.c — if the chosen route's netdev
 * has NETDEV_IFF_LOOPBACK set, ip output skips arp_lookup / nd6_lookup
 * and uses a zero dst MAC.  Same for the responding side: when icmp
 * replies need to reach the original sender (which is also us via
 * loopback), the lookup is skipped.
 */

#include <sys/netdev.h>
#include <net/inet.h>
#include <kern/console.h>
#include <string.h>
#include <stddef.h>

/* Loopback recursion: an IP packet sent via lo bounces back through
 * ip_input, which may trigger a *reply* that also goes through lo.
 * If we recursed straight into netdev_rx we'd blow the kernel stack
 * (each ip_output has a ~1.6 KiB on-stack buffer; depth 3 is enough
 * to overflow).  Instead the outermost lo_xmit drains a small ring of
 * frames queued by inner calls. */

#define LO_RING 8
#define LO_FRAME_MAX 1700

static int      lo_depth;
static uint8_t  lo_ring_buf[LO_RING][LO_FRAME_MAX];
static uint16_t lo_ring_len[LO_RING];
static unsigned lo_ring_head;
static unsigned lo_ring_tail;

static int lo_xmit(netdev_t *dev, const void *frame, size_t len) {
    if (len > LO_FRAME_MAX) len = LO_FRAME_MAX;
    if (lo_depth > 0) {
        unsigned next = (lo_ring_head + 1) % LO_RING;
        if (next != lo_ring_tail) {
            memcpy(lo_ring_buf[lo_ring_head], frame, len);
            lo_ring_len[lo_ring_head] = (uint16_t)len;
            lo_ring_head = next;
        }
        return 0;
    }
    lo_depth++;
    netdev_rx(dev, frame, len);
    while (lo_ring_tail != lo_ring_head) {
        netdev_rx(dev, lo_ring_buf[lo_ring_tail], lo_ring_len[lo_ring_tail]);
        lo_ring_tail = (lo_ring_tail + 1) % LO_RING;
    }
    lo_depth--;
    return 0;
}

static const struct netdev_ops lo_ops = { .xmit = lo_xmit };

static netdev_t lo_netdev;

void loopback_init(void) {
    if (lo_netdev.ifindex) return;
    strncpy(lo_netdev.name, "lo", NETDEV_NAME_MAX - 1);
    /* Use a recognisable zero MAC.  Real loopback doesn't have one. */
    memset(lo_netdev.hwaddr, 0, NETDEV_HWADDR_LEN);
    lo_netdev.mtu   = 16384;
    lo_netdev.flags = NETDEV_IFF_UP | NETDEV_IFF_LOOPBACK | NETDEV_IFF_RUNNING;
    lo_netdev.ops   = &lo_ops;
    /* 127.0.0.1 / 255.0.0.0 */
    lo_netdev.ip4_addr    = 0x0100007Fu;
    lo_netdev.ip4_netmask = 0x000000FFu;
    /* ::1 / 128 */
    memset(lo_netdev.ip6_addr, 0, 16);
    lo_netdev.ip6_addr[15] = 0x01;
    lo_netdev.ip6_netmask_bits = 128;
    netdev_register(&lo_netdev);
}
