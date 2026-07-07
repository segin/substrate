/*
 * netdev.c — global registry + AF_PACKET fan-out plumbing.
 */

#include <sys/netdev.h>
#include <sys/lock.h>
#include <net/inet.h>
#include <kern/sched.h>
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* netdev registry                                                    */
/* ------------------------------------------------------------------ */

static netdev_t *g_netdev_head;
static mutex_t   g_netdev_lock;
static int       g_netdev_lock_init;
static uint32_t  g_next_ifindex = 1;   /* loopback would be 0; not provided yet */

static void netdev_lock_init(void) {
    if (!g_netdev_lock_init) {
        mutex_init(&g_netdev_lock, "netdev");
        g_netdev_lock_init = 1;
    }
}

int netdev_register(netdev_t *dev) {
    if (!dev || !dev->ops || !dev->ops->xmit) return -EINVAL;
    if (dev->mtu == 0) dev->mtu = 1500;

    netdev_lock_init();
    mutex_lock(&g_netdev_lock);
    dev->ifindex = g_next_ifindex++;
    dev->next = g_netdev_head;
    g_netdev_head = dev;
    mutex_unlock(&g_netdev_lock);

    kprintf("netdev: %s (ifindex=%u) registered, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            dev->name, dev->ifindex,
            dev->hwaddr[0], dev->hwaddr[1], dev->hwaddr[2],
            dev->hwaddr[3], dev->hwaddr[4], dev->hwaddr[5]);
    return 0;
}

netdev_t *netdev_first(void) { return g_netdev_head; }
netdev_t *netdev_next(netdev_t *prev) { return prev ? prev->next : NULL; }

netdev_t *netdev_by_index(uint32_t ifindex) {
    for (netdev_t *d = g_netdev_head; d; d = d->next) {
        if (d->ifindex == ifindex) return d;
    }
    return NULL;
}

netdev_t *netdev_by_name(const char *name) {
    if (!name) return NULL;
    for (netdev_t *d = g_netdev_head; d; d = d->next) {
        if (strncmp(d->name, name, NETDEV_NAME_MAX) == 0) return d;
    }
    return NULL;
}

int netdev_xmit(netdev_t *dev, const void *frame, size_t len) {
    if (!dev || !dev->ops || !dev->ops->xmit) return -ENODEV;
    if (len == 0 || len > NETDEV_MTU_MAX + 14) return -EINVAL;
    int rc = dev->ops->xmit(dev, frame, len);
    if (rc == 0) {
        dev->tx_packets++;
        dev->tx_bytes += len;
    } else {
        dev->tx_dropped++;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* AF_PACKET listener queue                                           */
/* ------------------------------------------------------------------ */

#define NETDEV_SUB_RING_FRAMES 64
#define NETDEV_SUB_FRAME_MAX   NETDEV_MTU_MAX + 14    /* MTU + ETH header */

typedef struct netdev_frame {
    uint16_t len;
    uint32_t ifindex;
    uint8_t  data[NETDEV_SUB_FRAME_MAX];
} netdev_frame_t;

typedef struct netdev_sub {
    uint32_t        ifindex_filter; /* 0 = all */
    netdev_frame_t *ring;           /* NETDEV_SUB_RING_FRAMES slots */
    uint32_t        head;
    uint32_t        tail;
    uint32_t        count;
    uint64_t        dropped;
    void           *wait_chan;      /* address of self for sleep/wake */
    /* Spinlock, NOT mutex.  netdev_rx() is invoked from NIC IRQ
     * context (rtl8139 / loopback / virtio-net all call it from
     * their hard-IRQ handlers), so any lock guarding a sub's ring
     * must be safe to acquire with IRQs disabled — a mutex panics
     * with "Deadlock: recursive mutex_lock" when an IRQ lands on a
     * CPU that already owns the same mutex via a syscall-context
     * netdev_sub_recv().  Critical sections are pure ring math, no
     * sleeping, so a spinlock is appropriate. */
    spinlock_t      lock;
    int             lock_inited;
    struct netdev_sub *next;
} netdev_sub_t;

static netdev_sub_t *g_sub_head;
/* g_sub_lock guards the global subscriber list.  Same IRQ rule
 * applies — netdev_rx() walks the list from IRQ context. */
static spinlock_t    g_sub_lock;
static int           g_sub_lock_init;

static void sub_lock_init(void) {
    if (!g_sub_lock_init) {
        spinlock_init(&g_sub_lock, "netdev_sub");
        g_sub_lock_init = 1;
    }
}

netdev_sub_t *netdev_subscribe(uint32_t ifindex) {
    sub_lock_init();
    netdev_sub_t *s = (netdev_sub_t *)kmalloc(sizeof(*s));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->ring = (netdev_frame_t *)kmalloc(sizeof(netdev_frame_t) * NETDEV_SUB_RING_FRAMES);
    if (!s->ring) { kfree(s, sizeof(*s)); return NULL; }
    s->ifindex_filter = ifindex;
    s->wait_chan = &s->count;
    spinlock_init(&s->lock, "netdev_sub_q");
    s->lock_inited = 1;

    unsigned long fl = spinlock_acquire_irq(&g_sub_lock);
    s->next = g_sub_head;
    g_sub_head = s;
    spinlock_release_irq(&g_sub_lock, fl);
    return s;
}

void netdev_unsubscribe(netdev_sub_t *sub) {
    if (!sub) return;
    unsigned long fl = spinlock_acquire_irq(&g_sub_lock);
    netdev_sub_t **link = &g_sub_head;
    while (*link && *link != sub) link = &(*link)->next;
    if (*link == sub) *link = sub->next;
    spinlock_release_irq(&g_sub_lock, fl);
    if (sub->ring) kfree(sub->ring, sizeof(netdev_frame_t) * NETDEV_SUB_RING_FRAMES);
    kfree(sub, sizeof(*sub));
}

void netdev_rx(netdev_t *dev, const void *frame, size_t len) {
    if (!dev || !frame || len == 0) return;
    dev->rx_packets++;
    dev->rx_bytes += len;
    if (len > NETDEV_SUB_FRAME_MAX) len = NETDEV_SUB_FRAME_MAX;

    /* Hand to the IP layer first so ARP / ICMP / NS responses go out
     * before AF_PACKET sniffers see them — matters for tcpdump-style
     * tools tied up reading frames. */
    inet_eth_input(dev, (const uint8_t *)frame, len);

    sub_lock_init();
    /* Walk subscribers under the registry lock; per-sub queue mutates
     * under its own lock so we don't hold g_sub_lock while copying.
     * IRQ-safe — see netdev_sub_t.lock comment. */
    unsigned long fl_g = spinlock_acquire_irq(&g_sub_lock);
    for (netdev_sub_t *s = g_sub_head; s; s = s->next) {
        if (s->ifindex_filter && s->ifindex_filter != dev->ifindex) continue;
        unsigned long fl_s = spinlock_acquire_irq(&s->lock);
        if (s->count >= NETDEV_SUB_RING_FRAMES) {
            s->dropped++;
            dev->rx_dropped++;
            spinlock_release_irq(&s->lock, fl_s);
            continue;
        }
        netdev_frame_t *slot = &s->ring[s->head];
        slot->len = (uint16_t)len;
        slot->ifindex = dev->ifindex;
        memcpy(slot->data, frame, len);
        s->head = (s->head + 1) % NETDEV_SUB_RING_FRAMES;
        s->count++;
        spinlock_release_irq(&s->lock, fl_s);
        sched_wakeup(s->wait_chan);
    }
    spinlock_release_irq(&g_sub_lock, fl_g);
}

ssize_t netdev_sub_recv(netdev_sub_t *sub, void *buf, size_t size, uint32_t *out_ifindex) {
    if (!sub) return -EINVAL;
    unsigned long fl = spinlock_acquire_irq(&sub->lock);
    if (sub->count == 0) { spinlock_release_irq(&sub->lock, fl); return 0; }
    netdev_frame_t *slot = &sub->ring[sub->tail];
    size_t n = slot->len < size ? slot->len : size;
    if (buf) memcpy(buf, slot->data, n);
    if (out_ifindex) *out_ifindex = slot->ifindex;
    sub->tail = (sub->tail + 1) % NETDEV_SUB_RING_FRAMES;
    sub->count--;
    spinlock_release_irq(&sub->lock, fl);
    return (ssize_t)n;
}

void *netdev_sub_wait_chan(netdev_sub_t *sub) {
    return sub ? sub->wait_chan : NULL;
}
int netdev_sub_has_data(netdev_sub_t *sub) {
    return sub && sub->count > 0;
}
