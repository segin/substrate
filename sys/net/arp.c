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

#include <net/inet.h>
#include <sys/netdev.h>
#include <netinet/if_arp.h>
#include <netinet/ip.h>
#include <kern/console.h>
#include <string.h>
#include <stddef.h>

#define ARP_CACHE_SIZE 32

struct arp_entry {
    uint32_t ip;             /* network byte order; 0 = unused */
    uint8_t  mac[6];
    uint32_t ifindex;
};

static struct arp_entry g_arp_cache[ARP_CACHE_SIZE];
static unsigned          g_arp_next;   /* LRU pointer */

/* ------------------------------------------------------------------ */

int arp_lookup(netdev_t *dev, uint32_t ip, uint8_t mac[6]) {
    if (!dev) return -1;
    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        struct arp_entry *e = &g_arp_cache[i];
        if (e->ip == ip && e->ifindex == dev->ifindex) {
            memcpy(mac, e->mac, 6);
            return 0;
        }
    }
    return -1;
}

void arp_insert(netdev_t *dev, uint32_t ip, const uint8_t mac[6]) {
    if (!dev || !ip) return;
    /* Update if already present. */
    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        struct arp_entry *e = &g_arp_cache[i];
        if (e->ip == ip && e->ifindex == dev->ifindex) {
            memcpy(e->mac, mac, 6);
            return;
        }
    }
    struct arp_entry *slot = &g_arp_cache[g_arp_next % ARP_CACHE_SIZE];
    g_arp_next++;
    slot->ip = ip;
    slot->ifindex = dev->ifindex;
    memcpy(slot->mac, mac, 6);
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

    /* Snoop the sender — always useful for the next outbound packet. */
    arp_insert(dev, sender_ip, arp->ar_sha);

    /* If a request is directed at us, reply. */
    uint16_t op = __builtin_bswap16(arp->ar_op);
    if (op == ARPOP_REQUEST && target_ip == dev->ip4_addr && dev->ip4_addr) {
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
