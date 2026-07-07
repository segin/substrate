/*
 * loopback.c — "lo" netdev that bounces frames back to the IP layer
 * without any actual hardware involved.  Used to test the IP stack
 * end-to-end in environments where the test peer isn't cooperative
 * (e.g. QEMU SLIRP's IPv6 ND).
 *
 * Loopback semantics: xmit hands the frame to a ring; a dedicated
 * kthread drains the ring and feeds each frame to netdev_rx, which
 * dispatches via inet_eth_input -> ip{4,6}_input.  The IP layer's
 * destination check matches because lo's addresses include 127.0.0.1
 * and ::1.
 *
 * Why a kthread rather than delivering inline from lo_xmit():
 * delivering a frame can trigger a *reply* (e.g. a TCP segment is
 * received and immediately ACKed) that is itself transmitted via lo.
 * Done inline, lo_xmit -> netdev_rx -> ... -> lo_xmit recurses on the
 * caller's kernel stack, and each pass carries the TX path's nested
 * MTU-sized stack buffers (tcp_xmit_raw + ip4_output + eth_send,
 * ~4.7 KiB combined).  Two passes overflow a 16 KiB kernel stack and
 * corrupt adjacent kernel memory.  Routing every delivery through the
 * kthread keeps the call chain exactly one TX deep at all times: a
 * reply generated while draining is just appended to the ring and
 * picked up by the next drain iteration.
 *
 * The ARP/ND bypass lives in inet.c — if the chosen route's netdev
 * has NETDEV_IFF_LOOPBACK set, ip output skips arp_lookup / nd6_lookup
 * and uses a zero dst MAC.  Same for the responding side: when icmp
 * replies need to reach the original sender (which is also us via
 * loopback), the lookup is skipped.
 */

#include <sys/netdev.h>
#include <sys/kthread.h>
#include <net/inet.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <string.h>
#include <stddef.h>
#include <arch/i386/intr.h>

/*
 * Ring depth.  A TCP sender bursts up to a full receive window
 * (TCP_RING_LEN, 32 KiB ~= 23 MSS segments) into lo_xmit() before
 * flow control makes it block — and the echo path runs the same
 * burst in the opposite direction, plus ACKs.  With only 16 slots
 * the burst overflowed, frames were dropped, and the transfer
 * crawled along on RTO retransmissions.  128 slots comfortably
 * absorb both directions of a full window.
 */
#define LO_RING      128
#define LO_FRAME_MAX 1700

static uint8_t  lo_ring_buf[LO_RING][LO_FRAME_MAX];
static uint16_t lo_ring_len[LO_RING];
static unsigned lo_ring_head;          /* producer index */
static unsigned lo_ring_tail;          /* consumer index */
static int      lo_thread_started;

static netdev_t lo_netdev;

static int lo_xmit(netdev_t *dev, const void *frame, size_t len);
static const struct netdev_ops lo_ops = { .xmit = lo_xmit };

/*
 * lo_xmit — append the frame to the ring and wake the drain kthread.
 * Runs in whatever context called the TX path (process, tcp timer
 * kthread, or the loopback kthread itself when a reply is generated
 * mid-drain).  The ring indices are touched with interrupts off so
 * those contexts can't corrupt head/tail against each other.
 */
static int lo_xmit(netdev_t *dev, const void *frame, size_t len) {
    (void)dev;
    if (len > LO_FRAME_MAX) len = LO_FRAME_MAX;
    uint32_t f = intr_disable();
    unsigned next = (lo_ring_head + 1) % LO_RING;
    if (next != lo_ring_tail) {
        memcpy(lo_ring_buf[lo_ring_head], frame, len);
        lo_ring_len[lo_ring_head] = (uint16_t)len;
        lo_ring_head = next;
    }
    /* else: ring full — drop, same as a real NIC's TX overrun. */
    intr_restore(f);
    sched_wakeup(&lo_ring_head);
    return 0;
}

/*
 * lo_thread — drain the ring.  Each frame is copied out under the
 * lock into a private buffer, then delivered with interrupts enabled;
 * a reply transmitted during delivery lands back in the ring and is
 * picked up on the next iteration, so the stack never nests.
 */
static void lo_thread(void *arg) {
    (void)arg;
    static uint8_t scratch[LO_FRAME_MAX];
    for (;;) {
        uint32_t f = intr_disable();
        if (lo_ring_tail == lo_ring_head) {
            intr_restore(f);
            /* Wake on sched_wakeup() from lo_xmit; the short timeout
             * is a lost-wakeup safety net so a queued frame can never
             * sit undelivered. */
            sched_sleep_until(&lo_ring_head, get_ticks() + 2);
            continue;
        }
        unsigned t = lo_ring_tail;
        uint16_t len = lo_ring_len[t];
        if (len > LO_FRAME_MAX) len = LO_FRAME_MAX;
        memcpy(scratch, lo_ring_buf[t], len);
        lo_ring_tail = (t + 1) % LO_RING;
        intr_restore(f);

        netdev_rx(&lo_netdev, scratch, len);
    }
}

void loopback_init(void) {
    if (lo_netdev.ifindex) return;
    strlcpy(lo_netdev.name, "lo", NETDEV_NAME_MAX);
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

    if (!lo_thread_started) {
        lo_thread_started = 1;
        thread_t *t = NULL;
        kthread_create(lo_thread, NULL, &t, "loopback");
    }
}
