/*
 * arp.c — IPv4 Address Resolution Protocol (RFC 826).
 *
 * Per-interface cache of (ipv4, mac) tuples.  Filled by arp_input when
 * we see a reply, or by snooping legitimate requests addressed to us.
 * Outbound IPv4 frames look up the cache via arp_lookup; on miss the
 * caller issues an arp_request() and may retry.
 *
 * Cache is a small fixed table per netdev — 32 entries, LRU by
 * insertion order.  No timeouts in this first cut; an entry stays
 * until evicted.  Fine for SLIRP/LAN test scenarios.
 */

#include <stddef.h>
#include <string.h>

#include <kern/console.h>
#include <net/inet.h>
#include <netinet/if_arp.h>
#include <netinet/ip.h>
#include <sys/lock.h>
#include <sys/netdev.h>

#define ARP_CACHE_SIZE 32

struct arp_entry {
    uint32_t ip;             /* network byte order; 0 = unused */
    uint8_t  mac[6];
    uint32_t ifindex;
};

static struct arp_entry g_arp_cache[ARP_CACHE_SIZE];
static unsigned          g_arp_next;   /* LRU pointer */

/* NET-09: the cache is written by arp_input() in IRQ/RX context and read
 * by arp_lookup() in process context; guard it with an IRQ-safe spinlock
 * so a partially-written MAC is never read.  Critical sections are kept
 * tiny — the ARP reply transmit in arp_input() stays outside the lock. */
static spinlock_t g_arp_lock = SPINLOCK_INIT("arp_cache");

/* ------------------------------------------------------------------ */

int arp_lookup(netdev_t *dev, uint32_t ip, uint8_t mac[6]) {
    if (!dev) return -1;
    unsigned long f = spinlock_acquire_irq(&g_arp_lock);   /* NET-09 */
    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        struct arp_entry *e = &g_arp_cache[i];
        if (e->ip == ip && e->ifindex == dev->ifindex) {
            memcpy(mac, e->mac, 6);
            spinlock_release_irq(&g_arp_lock, f);
            return 0;
        }
    }
    spinlock_release_irq(&g_arp_lock, f);
    return -1;
}

/*
 * Update an existing cache entry's MAC if (and only if) one is already
 * present for (ip, ifindex).  Returns 1 if an entry was updated, 0 if
 * none existed.  Never inserts — this is the "merge" half of the RFC 826
 * algorithm and is safe to run for any received ARP packet.
 */
static int arp_update_existing(netdev_t *dev, uint32_t ip,
                               const uint8_t mac[6]) {
    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        struct arp_entry *e = &g_arp_cache[i];
        if (e->ip == ip && e->ifindex == dev->ifindex) {
            memcpy(e->mac, mac, 6);
            return 1;
        }
    }
    return 0;
}

/* Raw update-or-insert; caller must hold g_arp_lock (NET-09). */
static void arp_insert_raw(netdev_t *dev, uint32_t ip, const uint8_t mac[6]) {
    if (!dev || !ip) return;
    /* Update if already present. */
    if (arp_update_existing(dev, ip, mac)) return;
    struct arp_entry *slot = &g_arp_cache[g_arp_next % ARP_CACHE_SIZE];
    g_arp_next++;
    slot->ip = ip;
    slot->ifindex = dev->ifindex;
    memcpy(slot->mac, mac, 6);
}

void arp_insert(netdev_t *dev, uint32_t ip, const uint8_t mac[6]) {
    unsigned long f = spinlock_acquire_irq(&g_arp_lock);   /* NET-09 */
    arp_insert_raw(dev, ip, mac);
    spinlock_release_irq(&g_arp_lock, f);
}

/* ------------------------------------------------------------------ */

int arp_request(netdev_t *dev, uint32_t target_ip) {
    if (!dev) return -1;
    uint8_t broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    struct arphdr arp;
    arp.ar_hrd = __builtin_bswap16(ARPHRD_ETHER);
    arp.ar_pro = __builtin_bswap16(0x0800);
    arp.ar_hln = 6;
    arp.ar_pln = 4;
    arp.ar_op  = __builtin_bswap16(ARPOP_REQUEST);
    memcpy(arp.ar_sha, dev->hwaddr, 6);
    memcpy(arp.ar_spa, &dev->ip4_addr, 4);
    memset(arp.ar_tha, 0, 6);
    memcpy(arp.ar_tpa, &target_ip, 4);
    return eth_send(dev, broadcast, __builtin_bswap16(ETHERTYPE_ARP),
                    &arp, sizeof(arp));
}

/* ------------------------------------------------------------------ */

void arp_input(netdev_t *dev, const uint8_t *pkt, size_t len) {
    if (!dev || len < sizeof(struct arphdr)) return;
    const struct arphdr *arp = (const struct arphdr *)pkt;
    if (__builtin_bswap16(arp->ar_hrd) != ARPHRD_ETHER) return;
    if (__builtin_bswap16(arp->ar_pro) != 0x0800)      return;
    if (arp->ar_hln != 6 || arp->ar_pln != 4)          return;

    uint32_t sender_ip;
    uint32_t target_ip;
    memcpy(&sender_ip, arp->ar_spa, 4);
    memcpy(&target_ip, arp->ar_tpa, 4);

    /*
     * Anti-poisoning (RFC 826 "merge" rule): do NOT blindly insert the
     * sender into the cache — an off-path host could otherwise overwrite
     * any (or fill the whole) cache with a forged unsolicited reply.
     *
     *   - Always update an entry we already hold (a host whose MAC we are
     *     already tracking is allowed to refresh it).
     *   - Only INSERT a new entry when the packet is addressed to us
     *     (ar_tpa == our IP).  That covers exactly the two solicited
     *     cases: a reply to a request we sent (its ar_tpa is our own IP),
     *     and a request directed at us (we are about to reply and need
     *     the sender's MAC).  A request/reply for some other host, or a
     *     broadcast/unsolicited reply, can refresh but never create.
     */
    int target_is_me = (target_ip == dev->ip4_addr && dev->ip4_addr);
    /* NET-09: merge/insert under the cache lock (raw ops — we already hold
     * it, so do not call the locking arp_insert() here).  Kept tiny; the
     * reply transmit below runs outside the lock. */
    unsigned long af = spinlock_acquire_irq(&g_arp_lock);
    if (!arp_update_existing(dev, sender_ip, arp->ar_sha) && target_is_me)
        arp_insert_raw(dev, sender_ip, arp->ar_sha);
    spinlock_release_irq(&g_arp_lock, af);

    /* If a request is directed at us, reply. */
    uint16_t op = __builtin_bswap16(arp->ar_op);
    if (op == ARPOP_REQUEST && target_is_me) {
        struct arphdr reply;
        reply.ar_hrd = arp->ar_hrd;
        reply.ar_pro = arp->ar_pro;
        reply.ar_hln = 6;
        reply.ar_pln = 4;
        reply.ar_op  = __builtin_bswap16(ARPOP_REPLY);
        memcpy(reply.ar_sha, dev->hwaddr, 6);
        memcpy(reply.ar_spa, &dev->ip4_addr, 4);
        memcpy(reply.ar_tha, arp->ar_sha, 6);
        memcpy(reply.ar_tpa, arp->ar_spa, 4);
        eth_send(dev, arp->ar_sha, __builtin_bswap16(ETHERTYPE_ARP),
                 &reply, sizeof(reply));
    }
    /* op == ARPOP_REPLY needs no extra action — already snooped above. */
}
