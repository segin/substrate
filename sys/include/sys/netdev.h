/*
 * sys/netdev.h — minimal kernel network-device abstraction.
 *
 * A driver fills a netdev_ops vtable, calls netdev_register() at
 * probe time, and notifies the upper layer via netdev_rx() when a
 * frame arrives.  AF_PACKET sockets register listeners via
 * netdev_subscribe() and pull frames out of their per-socket queue.
 *
 * Frame ownership: netdev_rx() copies the bytes into a per-listener
 * buffer; the caller's frame buffer remains owned by the driver and
 * is free to be returned to the NIC's rx ring immediately.
 */

#ifndef _SYS_NETDEV_H
#define _SYS_NETDEV_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/lock.h>

#define NETDEV_NAME_MAX 16
#define NETDEV_HWADDR_LEN 6
#define NETDEV_MTU_MAX 1600

struct netdev;

struct netdev_ops {
    /* Transmit one Ethernet frame.  Returns 0 on success, negative
     * errno on failure.  Driver may copy or DMA; the caller's buffer
     * is no longer referenced after return. */
    int (*xmit)(struct netdev *dev, const void *frame, size_t len);
};

typedef struct netdev {
    char     name[NETDEV_NAME_MAX];      /* "eth0", "eth1", ... */
    uint8_t  hwaddr[NETDEV_HWADDR_LEN];  /* MAC */
    uint32_t ifindex;                    /* assigned at register() */
    uint32_t mtu;                        /* default 1500 */
    uint32_t flags;                      /* IFF_UP etc. */

    /* IPv4 config — set by ifconfig ioctl or by static-config boot. */
    uint32_t ip4_addr;     /* network byte order, 0 = unconfigured */
    uint32_t ip4_netmask;  /* network byte order */
    uint32_t ip4_gateway;  /* network byte order, 0 = none */

    /* IPv6 config — single global-scope address + link-local + default
     * router.  Plenty for first-cut dual-stack. */
    uint8_t  ip6_addr[16];      /* link-local or global */
    uint8_t  ip6_netmask_bits;  /* CIDR prefix length */
    uint8_t  ip6_gateway[16];

    const struct netdev_ops *ops;
    void   *driver_data;

    /* Stats — drivers bump these on tx/rx events. */
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t rx_dropped;
    uint64_t tx_dropped;

    struct netdev *next;
} netdev_t;

/* IFF_ flags (subset of Linux's). */
#define NETDEV_IFF_UP           0x0001
#define NETDEV_IFF_BROADCAST    0x0002
#define NETDEV_IFF_LOOPBACK     0x0008
#define NETDEV_IFF_RUNNING      0x0040
#define NETDEV_IFF_PROMISC      0x0100

/* Register a netdev — assigns ifindex, links it onto the global
 * list, prints a registration line.  Driver must have set
 * name/hwaddr/ops/driver_data before calling. */
int netdev_register(netdev_t *dev);

/* Iteration helpers — used by AF_PACKET bind() and netstat-style
 * tools.  Both are O(N) in number of NICs (typically 1-2). */
netdev_t *netdev_first(void);
netdev_t *netdev_next(netdev_t *prev);
netdev_t *netdev_by_index(uint32_t ifindex);
netdev_t *netdev_by_name(const char *name);

/* Driver upcall: an Ethernet frame has been received.  Buffer is
 * driver-owned and remains valid only for the duration of this call
 * — netdev_rx copies into each listener's queue. */
void netdev_rx(netdev_t *dev, const void *frame, size_t len);

/* Userspace transmit path (AF_PACKET sendto). */
int netdev_xmit(netdev_t *dev, const void *frame, size_t len);

/* AF_PACKET listener interface.
 * subscribe() returns an opaque handle that the socket holds; that
 * handle is passed to recv()/poll()/unsubscribe().  ifindex == 0
 * means "every NIC" (PF_PACKET with no bind).
 *
 * Each subscriber gets its own bounded ring; if a frame arrives and
 * the ring is full, the frame is dropped (counted in rx_dropped). */
struct netdev_sub;
struct netdev_sub *netdev_subscribe(uint32_t ifindex);
void               netdev_unsubscribe(struct netdev_sub *sub);

/* Returns >0 = bytes copied (truncated to caller's buf size), 0 = no
 * frame currently available (caller should sleep on the channel
 * returned by netdev_sub_wait_chan), <0 = -errno. */
ssize_t netdev_sub_recv(struct netdev_sub *sub, void *buf, size_t size,
                        uint32_t *out_ifindex);
void   *netdev_sub_wait_chan(struct netdev_sub *sub);
int     netdev_sub_has_data(struct netdev_sub *sub);

#endif /* _SYS_NETDEV_H */
