/*
 * dhclient — userspace DHCPv4 client (RFC 2131).
 *
 * Usage: dhclient <iface>
 *
 * Drives the four-way DISCOVER → OFFER → REQUEST → ACK handshake
 * against any standard DHCPv4 server reachable on `iface` (QEMU SLIRP
 * exposes one by default at 10.0.2.2:67).  All frames are built from
 * scratch and sent via AF_PACKET because the client has no IP yet —
 * AF_INET wouldn't have a valid source address to put in the IP
 * header at DISCOVER time.
 *
 * On ACK the address is ARP-probed (DHCPDECLINE if taken), then the lease
 * (address, netmask, router) is installed on the interface via the same
 * SIOC* ioctls /sbin/ifconfig uses and announced with a gratuitous ARP.
 * Unless the lease is infinite, dhclient then forks: the parent exits 0
 * so boot continues, and the child keeps the lease -- RENEWING at T1,
 * REBINDING at T2 (over an ordinary UDP socket, the host being configured
 * by then), and on a NAK or at expiry it removes the address and starts
 * again from INIT.  Conformance notes against RFC 2131:
 * docs/dhclient-audit-2026-09.md.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#define ETH_P_IP    0x0800
#define IPPROTO_UDP 17

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

/* DISCOVER retransmit policy: how many DISCOVERs to send before giving up
 * so boot can proceed without a lease.  The wait after each one is
 * retx_delay(): with 4 tries, about 4 + 8 + 16 + 32 = 60 s in all, the
 * RFC 2131 3.1 example. */
#define DHCP_DISCOVER_TRIES 4

/* DHC-03: DHCPREQUESTs per selected offer (4 x retx_delay(), about 60 s --
 * the RFC 2131 3.1 example), and how many times INIT is re-entered when
 * none is answered before dhclient gives up. */
#define DHCP_REQUEST_TRIES 4
#define DHCP_INIT_ATTEMPTS 3

/* DHC-02: after losing a lease, the pause between failed reacquisitions
 * in the background. */
#define DHCP_REACQUIRE_WAIT 60

/* RFC 2131 4.1 retransmission: 4 s before the first retransmission, doubled
 * each time up to 64 s, each randomized by a uniform -1..+1 s. */
#define DHCP_RETX_BASE 4.0
#define DHCP_RETX_MAX  64.0

/* DHCP message types (RFC 2132 §9.6) */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_DECLINE  4
#define DHCP_ACK      5
#define DHCP_NAK      6

/* DHCP option codes (RFC 2132) */
#define DHCP_OPT_SUBNET   1
#define DHCP_OPT_ROUTER   3
#define DHCP_OPT_DNS      6
#define DHCP_OPT_HOSTNAME 12
#define DHCP_OPT_DOMAIN   15   /* RFC 2132: Domain Name (single label) */
#define DHCP_OPT_REQ_IP   50
#define DHCP_OPT_LEASE    51
#define DHCP_OPT_MSGTYPE  53
#define DHCP_OPT_SRV_ID   54
#define DHCP_OPT_PARAMLST 55
#define DHCP_OPT_OVERLOAD 52   /* Option Overload: 1 file, 2 sname, 3 both */
#define DHCP_OPT_MAXSIZE  57   /* Maximum DHCP Message Size */
#define DHCP_OPT_T1       58   /* Renewal (T1) Time Value */
#define DHCP_OPT_T2       59   /* Rebinding (T2) Time Value */
#define DHCP_OPT_CLIENT_ID 61  /* RFC 2132 §9.14 — Client Identifier */
#define DHCP_OPT_SEARCH   119  /* RFC 3397: Domain Search */
#define DHCP_OPT_END      255

#define DHCP_MAGIC 0x63825363u

/* DHC-09: the Maximum DHCP Message Size we advertise (option 57). */
#define DHCP_MAX_MSG 1500

struct bootp {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
} __attribute__((packed));

struct eth_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} __attribute__((packed));

struct ip_hdr {
    uint8_t  vhl;
    uint8_t  tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
} __attribute__((packed));

struct udp_hdr {
    uint16_t src;
    uint16_t dst;
    uint16_t len;
    uint16_t check;
} __attribute__((packed));

/* ---- helpers ---- */

static uint16_t inet_csum(const void *data, size_t len) {
    uint32_t sum = 0;
    const uint8_t *p = data;
    while (len > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)__builtin_bswap16((uint16_t)~sum);
}

/* 1 if a UDP datagram's (non-zero) checksum verifies over its IPv4
 * pseudo-header, 0 otherwise. */
static int udp_csum_ok(const struct ip_hdr *ih, const struct udp_hdr *uh,
                       size_t udp_len) {
    uint32_t sum = 0;
    const uint8_t *a = (const uint8_t *)&ih->saddr;       /* saddr, daddr */
    for (int i = 0; i < 8; i += 2) sum += ((uint32_t)a[i] << 8) | a[i + 1];
    sum += IPPROTO_UDP;
    sum += (uint32_t)udp_len;
    const uint8_t *p = (const uint8_t *)uh;
    size_t n = udp_len;
    while (n > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return sum == 0xFFFF;
}

/* DHC-14: deadlines are measured on the monotonic clock.  They were taken
 * from gettimeofday(), so a wall-clock step during boot (an RTC read, NTP)
 * expired every wait at once -- all the DISCOVERs went out back to back and
 * dhclient gave up -- or, stepped backwards, stretched a wait out. */
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

/* DHC-05: the wait after transmission number `attempt` (0-based) -- the
 * randomized exponential backoff RFC 2131 4.1 requires.  It was a flat 2 s,
 * so clients that powered up together stayed in lockstep, and the whole
 * 8 s budget ran out before a server that probes the address (3.1 step 2)
 * had answered. */
static double retx_delay(int attempt) {
    double d = DHCP_RETX_BASE;
    for (int i = 0; i < attempt && d < DHCP_RETX_MAX; i++)
        d *= 2.0;
    if (d > DHCP_RETX_MAX) d = DHCP_RETX_MAX;
    return d + ((double)arc4random_uniform(2001) - 1000.0) / 1000.0;
}

static void get_hw_addr(const char *iface, uint8_t mac[6], int *ifindex) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); exit(1); }
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, iface, sizeof(r.ifr_name));
    if (ioctl(s, SIOCGIFHWADDR, &r) < 0) { perror("SIOCGIFHWADDR"); exit(1); }
    memcpy(mac, r.ifr_hwaddr.sa_data, 6);
    if (ioctl(s, SIOCGIFINDEX, &r) < 0) { perror("SIOCGIFINDEX"); exit(1); }
    *ifindex = r.ifr_ifindex;
    close(s);
}

/* Returns 0, or -1 (with the error reported) if the ioctl failed. */
static int set_ipv4(const char *iface, unsigned long req, uint32_t addr) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); exit(1); }
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, iface, sizeof(r.ifr_name));
    struct sockaddr_in *sin = (struct sockaddr_in *)&r.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = addr;
    int rc = 0;
    if (ioctl(s, req, &r) < 0) {
        fprintf(stderr, "dhclient: ioctl 0x%lx: %s\n", req, strerror(errno));
        rc = -1;
    }
    close(s);
    return rc;
}

/* DHC-12: a letter, digit or hyphen -- what a DNS label may hold. */
static int ldh_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-';
}

/* DHC-11: can a host be assigned this address?  Not 0, the limited
 * broadcast, 0/8, loopback 127/8, multicast 224/4 or class E 240/4. */
static int usable_addr(uint32_t addr) {
    uint8_t a = ((const uint8_t *)&addr)[0];
    return addr != 0 && addr != 0xFFFFFFFFu && a != 0 && a != 127 && a < 224;
}

/* The netmask for an address's class (RFC 1122 3.3.1.1 behaviour), used
 * when the server sends no subnet mask option. */
static uint32_t classful_mask(uint32_t addr) {
    uint8_t a = ((const uint8_t *)&addr)[0];
    uint32_t m = a < 128 ? 0xFF000000u : a < 192 ? 0xFFFF0000u : 0xFFFFFF00u;
    return __builtin_bswap32(m);
}

/* ---- DHCP frame builder ---- */

/* Read /etc/hostname.  Returns the trimmed value (without trailing
 * newline) into `buf`, max `bufsz - 1` bytes.  Falls back to
 * gethostname(2) if the file is unreadable.  Returns the length
 * or 0 if no usable hostname is available.  Empty strings and
 * "localhost" are treated as "no usable hostname" so we don't
 * advertise a useless identifier — the server falls back to
 * keying on the MAC. */
static size_t read_system_hostname(char *buf, size_t bufsz) {
    if (bufsz < 2) return 0;
    buf[0] = '\0';

    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        if (fgets(buf, (int)bufsz, f) == NULL) {
            buf[0] = '\0';
        }
        fclose(f);
    }
    if (buf[0] == '\0') {
        if (gethostname(buf, bufsz - 1) != 0) buf[0] = '\0';
        buf[bufsz - 1] = '\0';
    }

    /* Trim trailing whitespace / newline. */
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' '  || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }

    if (n == 0) return 0;
    if (strcmp(buf, "localhost") == 0 ||
        strcmp(buf, "(none)") == 0) {
        buf[0] = '\0';
        return 0;
    }
    /* RFC 2132 option-12 length is 1..255; the option payload field
     * itself is a single byte.  Clamp. */
    if (n > 63) {
        buf[63] = '\0';
        n = 63;
    }
    return n;
}

/* Build an Ethernet + IP + UDP + BOOTP frame.  `ciaddr` is our address in
 * BOUND/RENEWING/REBINDING (0 before); 'requested IP address' and 'server
 * identifier' are included only when non-zero, since RFC 2131 Table 4
 * forbids both in RENEWING and REBINDING.  A caller sending through an
 * AF_INET socket takes the BOOTP body at BOOTP_OFF. */
#define BOOTP_OFF (sizeof(struct eth_hdr) + sizeof(struct ip_hdr) + \
                   sizeof(struct udp_hdr))
static size_t build_dhcp_packet(uint8_t *out,
                                const uint8_t hw[6],
                                uint32_t xid,
                                uint8_t msg_type,
                                uint32_t ciaddr,
                                uint32_t request_ip,
                                uint32_t server_id) {
    struct eth_hdr *eh = (struct eth_hdr *)out;
    memset(eh->dst, 0xff, 6);
    memcpy(eh->src, hw, 6);
    eh->ethertype = __builtin_bswap16(ETH_P_IP);

    struct ip_hdr *ih = (struct ip_hdr *)(out + sizeof(*eh));
    struct udp_hdr *uh = (struct udp_hdr *)(out + sizeof(*eh) + sizeof(*ih));
    struct bootp *bp =
        (struct bootp *)(out + sizeof(*eh) + sizeof(*ih) + sizeof(*uh));

    /* BOOTP / DHCP body */
    memset(bp, 0, sizeof(*bp));
    bp->op    = 1;     /* BOOTREQUEST */
    bp->htype = 1;
    bp->hlen  = 6;
    bp->xid   = xid;
    /* DHC-13: the BROADCAST flag stays clear.  RFC 2131 4.1: a client that
     * can receive unicast before it is configured SHOULD clear it, and
     * this one can -- it reads replies off an AF_PACKET socket, which
     * netdev_rx() feeds every frame for our MAC whatever its IP
     * destination.  Setting it made every OFFER and ACK a broadcast to
     * the whole segment. */
    bp->flags = 0;
    bp->ciaddr = ciaddr;
    memcpy(bp->chaddr, hw, 6);
    bp->magic = __builtin_bswap32(DHCP_MAGIC);

    /* Options */
    uint8_t *op = bp->options;
    *op++ = DHCP_OPT_MSGTYPE; *op++ = 1; *op++ = msg_type;
    if (request_ip) {
        *op++ = DHCP_OPT_REQ_IP; *op++ = 4;
        memcpy(op, &request_ip, 4); op += 4;
    }
    if (server_id) {
        *op++ = DHCP_OPT_SRV_ID; *op++ = 4;
        memcpy(op, &server_id, 4); op += 4;
    }
    /* RFC 2131 Table 5: a DHCPDECLINE carries no parameter request list
     * and no other options ("All others: MUST NOT") beyond the requested
     * address, server identifier and client identifier. */
    int decline = (msg_type == DHCP_DECLINE);
    if (!decline) {
        *op++ = DHCP_OPT_PARAMLST; *op++ = 6;
        *op++ = DHCP_OPT_SUBNET;
        *op++ = DHCP_OPT_ROUTER;
        *op++ = DHCP_OPT_DNS;
        *op++ = DHCP_OPT_DOMAIN;       /* RFC 2132 — domain name */
        *op++ = DHCP_OPT_SEARCH;       /* RFC 3397 — search list */
        *op++ = DHCP_OPT_LEASE;
        /* DHC-09: RFC 2131 3.5 SHOULD -- the largest message we accept:
         * a full-MTU IP datagram (rxbuf holds a 1500-octet one plus its
         * Ethernet header).  Without it the server must assume 576 and
         * overloads 'file'/'sname' that much sooner. */
        *op++ = DHCP_OPT_MAXSIZE; *op++ = 2;
        *op++ = (uint8_t)(DHCP_MAX_MSG >> 8);
        *op++ = (uint8_t)(DHCP_MAX_MSG & 0xFF);
    }

    /*
     * Include the system hostname both as RFC 2132 option 12
     * (Host Name — a suggestion to the server / DDNS) and as RFC
     * 2132 option 61 (Client Identifier — uniquely keys this
     * client across MAC changes, useful with sticky-lease ISPs
     * and DHCP-driven DNS).  The client-ID payload starts with a
     * single type byte; type 0 means "the rest is an opaque
     * ASCII string" per the ISC convention.
     */
    {
        char hn[64];
        size_t hn_len = read_system_hostname(hn, sizeof(hn));
        if (hn_len > 0) {
            /* Option 12: Host Name */
            if (!decline) {
                *op++ = DHCP_OPT_HOSTNAME;
                *op++ = (uint8_t)hn_len;
                memcpy(op, hn, hn_len);
                op += hn_len;
            }

            /* Option 61: Client Identifier = type(0) + hostname */
            *op++ = DHCP_OPT_CLIENT_ID;
            *op++ = (uint8_t)(hn_len + 1);
            *op++ = 0;  /* type 0 — opaque identifier */
            memcpy(op, hn, hn_len);
            op += hn_len;
        }
    }

    *op++ = DHCP_OPT_END;
    size_t optlen = (size_t)(op - bp->options);
    /* Round options up to a sane size (≥ 64 padding for legacy BOOTP). */
    size_t bootp_len = (size_t)((const uint8_t *)bp->options - (const uint8_t *)bp) + optlen;

    /* UDP header */
    size_t udp_len = sizeof(*uh) + bootp_len;
    uh->src = __builtin_bswap16(68);
    uh->dst = __builtin_bswap16(67);
    uh->len = __builtin_bswap16((uint16_t)udp_len);
    uh->check = 0;  /* optional in IPv4 */

    /* IP header */
    size_t ip_len = sizeof(*ih) + udp_len;
    ih->vhl = (4 << 4) | 5;
    ih->tos = 0;
    ih->tot_len = __builtin_bswap16((uint16_t)ip_len);
    ih->id = 0;
    ih->frag_off = 0;
    ih->ttl = 64;
    ih->proto = IPPROTO_UDP;
    ih->check = 0;
    ih->saddr = ciaddr;             /* 0.0.0.0 until configured (4.1) */
    ih->daddr = 0xFFFFFFFFu;        /* 255.255.255.255 */
    ih->check = inet_csum(ih, sizeof(*ih));

    return sizeof(*eh) + ip_len;
}

/* ---- option parsing ---- */

/* Append every instance of `code` in one option field of `max` octets to
 * val[*n]; note an 'option overload' (52) value on the way. */
static void scan_opts(const uint8_t *opts, size_t max, uint8_t code,
                      uint8_t *val, size_t cap, size_t *n, int *found,
                      int *overload) {
    size_t i = 0;
    while (i < max) {
        uint8_t c = opts[i++];
        if (c == 0) continue;                       /* pad */
        if (c == DHCP_OPT_END) break;
        if (i >= max) break;
        uint8_t l = opts[i++];
        if (i + l > max) break;     /* must lie wholly inside its field */
        if (c == DHCP_OPT_OVERLOAD && l == 1 && overload)
            *overload = opts[i];
        if (c == code) {
            size_t k = l;
            if (k > cap - *n) k = cap - *n;
            memcpy(val + *n, opts + i, k);
            *n += k;
            *found = 1;
        }
        i += l;
    }
}

/*
 * DHC-09: RFC 2131 4.1 -- options are read from the 'options' field and
 * then, as an 'option overload' option there directs, from 'file' and
 * then 'sname'.  The client "concatenates the values of multiple instances
 * of the same option".  Only the first instance in 'options' used to be
 * seen, so options a server overloaded into 'file'/'sname', or split
 * across instances, were lost.  The value is assembled in a static buffer:
 * the pointer is valid until the next call.
 */
static const uint8_t *find_opt(const struct bootp *bp, size_t len,
                               uint8_t code, size_t *out_len) {
    static uint8_t val[1024];
    size_t body_off = (size_t)((const uint8_t *)bp->options - (const uint8_t *)bp);
    if (len <= body_off) return NULL;
    size_t n = 0;
    int found = 0, overload = 0;
    scan_opts(bp->options, len - body_off, code, val, sizeof(val), &n,
              &found, &overload);
    if (overload & 1)
        scan_opts(bp->file, sizeof(bp->file), code, val, sizeof(val), &n,
                  &found, NULL);
    if (overload & 2)
        scan_opts(bp->sname, sizeof(bp->sname), code, val, sizeof(val), &n,
                  &found, NULL);
    if (!found) return NULL;
    if (out_len) *out_len = n;
    return val;
}

/* Wait until `deadline` for a BOOTREPLY carrying `xid`.  Returns its DHCP
 * message type, with *bpp / *lenp pointing at the BOOTP body inside rxbuf,
 * or 0 once the deadline passes.  Replies of the wrong type are the
 * caller's to skip: it calls again with the same deadline. */
static int recv_dhcp(int pkts, uint8_t *rxbuf, size_t cap, uint32_t xid,
                     double deadline, const struct bootp **bpp,
                     size_t *lenp) {
    while (now_sec() < deadline) {
        usleep(5000);
        ssize_t r = recv(pkts, rxbuf, cap, 0);
        if (r < (ssize_t)(sizeof(struct eth_hdr) + sizeof(struct ip_hdr) +
                          sizeof(struct udp_hdr) + 240)) continue;
        struct eth_hdr *eh = (struct eth_hdr *)rxbuf;
        if (__builtin_bswap16(eh->ethertype) != ETH_P_IP) continue;
        struct ip_hdr *ih = (struct ip_hdr *)(rxbuf + sizeof(*eh));
        size_t hlen = (ih->vhl & 0x0F) * 4;
        if (ih->proto != IPPROTO_UDP) continue;
        /* DHC-10: AF_PACKET hands us raw frames, bypassing every check the
         * kernel's IP and UDP input would make, so make them here: IPv4
         * with a sane header, not a fragment, a good header checksum, a
         * reply from the server port, a UDP length that fits the frame and
         * bounds the BOOTP body, a UDP checksum that is absent or right,
         * and the DHCP magic cookie.  Only op and xid were checked, and
         * the body was sized by the frame, not the UDP length. */
        size_t ip_avail = (size_t)r - sizeof(*eh);
        if ((ih->vhl >> 4) != 4 || hlen < sizeof(*ih) ||
            hlen + sizeof(struct udp_hdr) > ip_avail) continue;
        if (__builtin_bswap16(ih->frag_off) & 0x3FFF) continue;
        if (inet_csum(ih, hlen) != 0) continue;
        size_t ip_tot = __builtin_bswap16(ih->tot_len);
        if (ip_tot < hlen + sizeof(struct udp_hdr) || ip_tot > ip_avail)
            continue;
        struct udp_hdr *uh = (struct udp_hdr *)(rxbuf + sizeof(*eh) + hlen);
        if (__builtin_bswap16(uh->dst) != 68) continue;
        if (__builtin_bswap16(uh->src) != 67) continue;
        size_t udp_len = __builtin_bswap16(uh->len);
        if (udp_len < sizeof(*uh) + 240 || udp_len > ip_tot - hlen) continue;
        if (uh->check && udp_csum_ok(ih, uh, udp_len) == 0) continue;
        const struct bootp *bp =
            (const struct bootp *)(rxbuf + sizeof(*eh) + hlen + sizeof(*uh));
        size_t bootp_len = udp_len - sizeof(*uh);
        if (__builtin_bswap32(bp->magic) != DHCP_MAGIC) continue;
        if (bp->op != 2 || bp->xid != xid) continue;
        size_t mlen;
        const uint8_t *mt = find_opt(bp, bootp_len, DHCP_OPT_MSGTYPE, &mlen);
        if (!mt || mlen < 1 || *mt == 0) continue;
        *bpp = bp;
        *lenp = bootp_len;
        return *mt;
    }
    return 0;
}

/* Install the lease an ACK granted -- address, netmask, router -- and
 * write /etc/resolv.conf from its resolver options.
 *
 * DHC-06: everything comes from the DHCPACK.  RFC 2131 3.1 step 4: the
 * ACK carries the committed configuration and its 'yiaddr' the selected
 * address; a server only SHOULD keep it consistent with the OFFER.  The
 * address, netmask and router were taken from the OFFER, so a server that
 * sent the router or mask only in its ACK left the host without a default
 * route or with the wrong mask. */
static int install_lease(const char *iface, const struct bootp *bp,
                         size_t bootp_len, const char *what) {
    size_t mlen;
    uint32_t offered_ip = bp->yiaddr ? bp->yiaddr : bp->ciaddr;
    uint32_t subnet = 0, router = 0;
    const uint8_t *p = find_opt(bp, bootp_len, DHCP_OPT_SUBNET, &mlen);
    if (p && mlen == 4) memcpy(&subnet, p, 4);
    p = find_opt(bp, bootp_len, DHCP_OPT_ROUTER, &mlen);
    if (p && mlen >= 4) memcpy(&router, p, 4);

    /* Pull resolver-relevant options from the ACK (servers
     * frequently include them only at ACK time, not OFFER). */
    uint8_t dns_buf[64] = { 0 };
    unsigned dns_len = 0;
    uint8_t domain_buf[256] = { 0 };
    unsigned domain_len = 0;
    uint8_t search_buf[256] = { 0 };
    unsigned search_len = 0;

    const uint8_t *op_p;
    op_p = find_opt(bp, bootp_len, DHCP_OPT_DNS, &mlen);
    if (op_p && mlen >= 4) {
        if (mlen > sizeof(dns_buf)) mlen = sizeof(dns_buf);
        memcpy(dns_buf, op_p, mlen);
        dns_len = (unsigned)mlen;
    }
    op_p = find_opt(bp, bootp_len, DHCP_OPT_DOMAIN, &mlen);
    if (op_p && mlen > 0) {
        unsigned ml = mlen;
        if (ml > sizeof(domain_buf) - 1) ml = sizeof(domain_buf) - 1;
        memcpy(domain_buf, op_p, ml);
        domain_buf[ml] = '\0';
        domain_len = ml;
        /* DHC-12: a domain name is letters, digits, '-' and '.' (RFC 2132
         * 3.17 names it a domain name).  It was written into resolv.conf
         * as received, so a newline in it added arbitrary lines. */
        for (unsigned i = 0; i < ml; i++)
            if (!ldh_char(domain_buf[i]) && domain_buf[i] != '.')
                domain_len = 0;
        if (domain_len == 0)
            fprintf(stderr, "dhclient: ignoring a malformed domain name\n");
    }
    op_p = find_opt(bp, bootp_len, DHCP_OPT_SEARCH, &mlen);
    if (op_p && mlen > 0) {
        unsigned ml = mlen;
        if (ml > sizeof(search_buf)) ml = sizeof(search_buf);
        memcpy(search_buf, op_p, ml);
        search_len = ml;
    }

    /* Install lease.
     *
     * DHC-08: RFC 2131 3.5 -- a parameter the server does not send takes
     * its Host Requirements default.  A missing mask or router used to
     * leave whatever the interface already had -- at boot, the /24 and
     * 10.0.2.2 gateway inet_init() hard-codes for QEMU -- so a real
     * network's lease kept a route through a host that does not exist
     * there.  The mask defaults to the address's class and a missing
     * router clears the gateway.  And a failed install is a failure: it
     * used to be reported and then announced as "bound" with exit 0. */
    if (!subnet) subnet = classful_mask(offered_ip);
    int bad = set_ipv4(iface, SIOCSIFADDR, offered_ip) < 0;
    bad |= set_ipv4(iface, SIOCSIFNETMASK, subnet) < 0;
    bad |= set_ipv4(iface, SIOCSIFGATEWAY, router) < 0;
    if (bad) {
        fprintf(stderr, "dhclient: could not install the lease on %s\n", iface);
        return 1;
    }

    /* Write /etc/resolv.conf with DNS + search/domain.  Atomic-
     * via-rename so a partial write never blinds the resolver. */
    if (dns_len >= 4 || domain_len > 0 || search_len > 0) {
        FILE *rf = fopen("/etc/resolv.conf.new", "w");
        if (rf) {
            fprintf(rf, "# generated by dhclient on lease bind\n");
            for (unsigned i = 0; i + 3 < dns_len; i += 4) {
                fprintf(rf, "nameserver %u.%u.%u.%u\n",
                        dns_buf[i], dns_buf[i+1],
                        dns_buf[i+2], dns_buf[i+3]);
            }
            if (domain_len > 0) {
                fprintf(rf, "domain %s\n", domain_buf);
            }
            if (search_len > 0) {
                /* RFC 3397 encodes the search list as RFC 1035
                 * domain-name labels (length-prefixed) with
                 * compression pointers.  Decode each label
                 * sequence into a dotted name and emit them
                 * all on one `search` line. */
                fprintf(rf, "search");
                size_t i = 0;
                while (i < search_len) {
                    char  name[256];
                    size_t no = 0;
                    size_t j  = i;
                    int    safety = 256;
                    int    first  = 1;
                    int    bad    = 0;  /* DHC-12: a non-LDH octet */
                    while (j < search_len && safety-- > 0) {
                        uint8_t l = search_buf[j];
                        if (l == 0) { j++; break; }
                        if ((l & 0xC0) == 0xC0) {
                            /* RFC 1035 compression pointer — 14-bit
                             * back-reference to earlier in the search
                             * option buffer. */
                            if (j + 1 >= search_len) break;
                            size_t back = ((l & 0x3F) << 8) | search_buf[j+1];
                            if (back >= i) break;   /* must point backward */
                            j = back;
                            continue;
                        }
                        if (l > 63 || j + 1 + l > search_len) break;
                        if (!first && no + 1 < sizeof(name)) name[no++] = '.';
                        for (uint8_t k = 0; k < l && no + 1 < sizeof(name); k++) {
                            char ch = (char)search_buf[j + 1 + k];
                            if (!ldh_char(ch)) bad = 1;
                            name[no++] = ch;
                        }
                        j += 1 + l;
                        first = 0;
                    }
                    name[no] = '\0';
                    if (no > 0 && !bad) fprintf(rf, " %s", name);
                    /* Advance the OUTER cursor past the name we
                     * just decoded — find the terminating 0 from
                     * position i forward, skipping comp pointers. */
                    while (i < search_len) {
                        uint8_t l = search_buf[i];
                        if (l == 0) { i++; break; }
                        if ((l & 0xC0) == 0xC0) { i += 2; break; }
                        i += 1 + l;
                    }
                }
                fprintf(rf, "\n");
            }
            fclose(rf);
            rename("/etc/resolv.conf.new", "/etc/resolv.conf");
        }
    }

    uint8_t *yi = (uint8_t *)&offered_ip;
    uint8_t *m  = (uint8_t *)&subnet;
    uint8_t *r2 = (uint8_t *)&router;
    fprintf(stdout, "dhclient: %s %u.%u.%u.%u/%u.%u.%u.%u", what,
            yi[0], yi[1], yi[2], yi[3],
            m[0], m[1], m[2], m[3]);
    if (router) fprintf(stdout, " via %u.%u.%u.%u",
                        r2[0], r2[1], r2[2], r2[3]);
    fprintf(stdout, "\n");
    return 0;
}

/* ---- lease ---- */

/*
 * DHC-02: the lease an ACK granted, on the monotonic clock.  RFC 2131
 * 4.4.1/4.4.5: the lease runs from the time the acknowledged DHCPREQUEST
 * was sent; T1 and T2 come from options 58/59 or default to 0.5 and 0.875
 * of it.
 */
struct lease {
    uint32_t addr;      /* network byte order */
    uint32_t server;    /* 'server identifier' of the leasing server */
    double   start;     /* now_sec() when the acknowledged REQUEST went out */
    double   t1, t2, len;
    int      infinite;  /* lease time 0xffffffff (3.3), or none given */
};

static uint32_t opt_u32(const struct bootp *bp, size_t len, uint8_t code,
                        int *found) {
    size_t mlen;
    uint32_t v = 0;
    const uint8_t *p = find_opt(bp, len, code, &mlen);
    *found = p && mlen == 4;
    if (*found) {
        memcpy(&v, p, 4);
        v = __builtin_bswap32(v);
    }
    return v;
}

static void lease_from_ack(const struct bootp *bp, size_t len, double sent_at,
                           struct lease *L) {
    size_t mlen;
    int has;
    L->addr = bp->yiaddr ? bp->yiaddr : L->addr;
    const uint8_t *p = find_opt(bp, len, DHCP_OPT_SRV_ID, &mlen);
    if (p && mlen == 4) memcpy(&L->server, p, 4);
    L->start = sent_at;

    uint32_t secs = opt_u32(bp, len, DHCP_OPT_LEASE, &has);
    L->infinite = !has || secs == 0xFFFFFFFFu;
    if (L->infinite)
        return;
    L->len = secs;
    int h1, h2;
    double t1 = opt_u32(bp, len, DHCP_OPT_T1, &h1);
    double t2 = opt_u32(bp, len, DHCP_OPT_T2, &h2);
    if (!h1) t1 = 0.5 * L->len;
    if (!h2) t2 = 0.875 * L->len;
    if (!(0 < t1 && t1 < t2 && t2 < L->len)) {  /* 4.4.5: T1 < T2 < lease */
        t1 = 0.5 * L->len;
        t2 = 0.875 * L->len;
    }
    /* 4.4.5: "some random fuzz around a fixed value, to avoid
     * synchronization of client reacquisition" -- +-5%, keeping the
     * order. */
    t1 *= 1.0 + ((double)arc4random_uniform(2001) - 1000.0) / 20000.0;
    t2 *= 1.0 + ((double)arc4random_uniform(2001) - 1000.0) / 20000.0;
    if (t2 >= L->len) t2 = 0.95 * L->len;
    if (t1 >= t2) t1 = 0.9 * t2;
    L->t1 = t1;
    L->t2 = t2;
}

/* Stop using the address: 3.7 / 4.4.5 "MUST immediately stop any other
 * network processing". */
static void drop_lease(const char *iface) {
    set_ipv4(iface, SIOCSIFGATEWAY, 0);
    set_ipv4(iface, SIOCSIFADDR, 0);
}

static void sleep_until(double t) {
    for (;;) {
        double left = t - now_sec();
        if (left <= 0) return;
        if (left > 60.0) left = 60.0;       /* re-check the clock regularly */
        struct timespec ts;
        ts.tv_sec = (time_t)left;
        ts.tv_nsec = (long)((left - (double)ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
    }
}

/*
 * RENEWING (rebinding == 0: unicast to the leasing server, until T2) or
 * REBINDING (broadcast, until the lease ends), per 4.4.5 and Table 4:
 * ciaddr set, no 'server identifier', no 'requested IP address'.  With no
 * answer the client waits half the remaining time, down to a minimum of
 * 60 s, before retransmitting.  The host is configured by now, so this
 * goes through an ordinary UDP socket on port 68 -- a unicast renewal needs
 * the kernel's routing and ARP.  Returns 1 on an ACK (lease updated and
 * reinstalled), -1 on a NAK, 0 when the phase ran out.
 */
static int extend(const char *iface, const uint8_t hw[6], struct lease *L,
                  int rebinding) {
    double deadline = L->start + (rebinding ? L->len : L->t2);
    if (now_sec() >= deadline) return 0;

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("dhclient: socket"); return 0; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    struct sockaddr_in me;
    memset(&me, 0, sizeof(me));
    me.sin_family = AF_INET;
    me.sin_port = htons(68);
    if (bind(s, (struct sockaddr *)&me, sizeof(me)) < 0) {
        perror("dhclient: bind port 68");
        close(s);
        return 0;
    }
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(67);
    to.sin_addr.s_addr = rebinding ? 0xFFFFFFFFu : L->server;

    uint32_t xid = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
    uint8_t pkt[1500], buf[1600];
    const char *phase = rebinding ? "rebinding" : "renewing";
    while (now_sec() < deadline) {
        size_t n = build_dhcp_packet(pkt, hw, xid, DHCP_REQUEST, L->addr, 0, 0);
        double sent_at = now_sec();
        if (sendto(s, pkt + BOOTP_OFF, n - BOOTP_OFF, 0,
                   (struct sockaddr *)&to, sizeof(to)) < 0)
            fprintf(stderr, "dhclient: sendto REQUEST (%s): %s\n", phase,
                    strerror(errno));
        fprintf(stdout, "dhclient: DHCPREQUEST (%s)\n", phase);

        double wait = (deadline - now_sec()) / 2.0;
        if (wait < 60.0) wait = 60.0;
        double until = now_sec() + wait;
        if (until > deadline) until = deadline;
        for (;;) {
            double left = until - now_sec();
            if (left <= 0) break;
            struct pollfd pfd = { .fd = s, .events = POLLIN };
            if (poll(&pfd, 1, (int)(left * 1000.0) + 1) <= 0) continue;
            ssize_t r = recv(s, buf, sizeof(buf), 0);
            if (r < 240) continue;
            const struct bootp *bp = (const struct bootp *)buf;
            /* The kernel checked IP and UDP on this path; the cookie is
             * ours to check (DHC-10). */
            if (__builtin_bswap32(bp->magic) != DHCP_MAGIC) continue;
            if (bp->op != 2 || bp->xid != xid) continue;
            size_t mlen;
            const uint8_t *mt = find_opt(bp, (size_t)r, DHCP_OPT_MSGTYPE, &mlen);
            if (!mt || mlen < 1) continue;
            if (*mt == DHCP_NAK) {
                fprintf(stdout, "dhclient: DHCPNAK (%s)\n", phase);
                close(s);
                return -1;
            }
            if (*mt != DHCP_ACK) continue;
            fprintf(stdout, "dhclient: DHCPACK (%s)\n", phase);
            lease_from_ack(bp, (size_t)r, sent_at, L);
            install_lease(iface, bp, (size_t)r,
                          rebinding ? "rebound" : "renewed");
            close(s);
            return 1;
        }
    }
    close(s);
    return 0;
}

/* ---- duplicate-address check (DHC-07) ---- */

#define ETH_P_ARP        0x0806
#define ARP_FRAME_LEN    42
#define ARP_PROBES       2        /* probes sent, ARP_PROBE_GAP apart */
#define ARP_PROBE_GAP    0.5
#define ARP_PROBE_LISTEN 1.0      /* listening time from the first probe */
#define DECLINE_WAIT     10       /* 3.1 step 5: at least 10 s, then INIT */

/* An ARP request (op 1) or reply (op 2) from `spa`, asking about / telling
 * of `tpa`, broadcast. */
static void send_arp(int pkts, const struct sockaddr_ll *sll,
                     const uint8_t hw[6], uint16_t op, uint32_t spa,
                     const uint8_t tha[6], uint32_t tpa) {
    uint8_t f[ARP_FRAME_LEN];
    memset(f, 0xff, 6);                             /* broadcast */
    memcpy(f + 6, hw, 6);
    f[12] = 0x08; f[13] = 0x06;
    f[14] = 0; f[15] = 1;                           /* Ethernet */
    f[16] = 0x08; f[17] = 0x00;                     /* IPv4 */
    f[18] = 6; f[19] = 4;
    f[20] = 0; f[21] = (uint8_t)op;
    memcpy(f + 22, hw, 6);
    memcpy(f + 28, &spa, 4);
    memcpy(f + 32, tha, 6);
    memcpy(f + 38, &tpa, 4);
    sendto(pkts, f, sizeof(f), 0, (const struct sockaddr *)sll, sizeof(*sll));
}

/*
 * RFC 2131 3.1 step 5 / 4.4.1: before using the address, ARP for it with
 * sender IP 0 (the RFC 5227 probe form, which confuses no ARP cache).  It
 * is in use if another host answers for it, or is itself probing for it.
 * Our own frames, and the kernel answering for an address it already has,
 * carry our MAC and are not a conflict.
 */
static int arp_in_use(int pkts, const struct sockaddr_ll *sll,
                      const uint8_t hw[6], uint32_t ip) {
    static const uint8_t zero[6];
    uint8_t buf[128];
    double start = now_sec(), next = start;
    int sent = 0;
    while (now_sec() < start + ARP_PROBE_LISTEN) {
        if (sent < ARP_PROBES && now_sec() >= next) {
            send_arp(pkts, sll, hw, 1, 0, zero, ip);
            sent++;
            next += ARP_PROBE_GAP;
        }
        usleep(5000);
        ssize_t r = recv(pkts, buf, sizeof(buf), 0);
        if (r < ARP_FRAME_LEN) continue;
        if (buf[12] != 0x08 || buf[13] != 0x06) continue;
        if (memcmp(buf + 22, hw, 6) == 0) continue;     /* ours */
        uint32_t spa, tpa;
        memcpy(&spa, buf + 28, 4);
        memcpy(&tpa, buf + 38, 4);
        if (spa == ip) return 1;                    /* someone has it */
        if (spa == 0 && tpa == ip) return 1;        /* someone wants it */
    }
    return 0;
}

/* ---- INIT through BOUND ---- */

/* Acquire a lease from scratch (INIT -> SELECTING -> REQUESTING -> BOUND)
 * and install it.  Returns 0 with *L filled in, or 1 if none was had. */
static int acquire(const char *iface, const uint8_t hw[6], int ifindex,
                   struct lease *L) {
    int pkts = socket(AF_PACKET, SOCK_RAW, __builtin_bswap16(0x0003));
    if (pkts < 0) { perror("socket(AF_PACKET)"); return 1; }
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = __builtin_bswap16(0x0003);
    sll.sll_ifindex = ifindex;
    if (bind(pkts, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("bind"); close(pkts); return 1;
    }

    /*
     * Non-blocking recv is mandatory.  The OFFER/ACK wait loops below poll
     * with a wall-clock deadline (usleep + recv), but a *blocking* recv on a
     * quiet link parks indefinitely on the very first call — the deadline is
     * never re-checked — so with no DHCP server present dhclient hangs forever
     * instead of failing in a few seconds, stalling rc.d/20-network at boot.
     * O_NONBLOCK makes recv return EAGAIN immediately so the loop can time out.
     */
    {
        int fl = fcntl(pkts, F_GETFL, 0);
        if (fl < 0) fl = 0;
        if (fcntl(pkts, F_SETFL, fl | O_NONBLOCK) < 0) {
            perror("fcntl O_NONBLOCK"); close(pkts); return 1;
        }
    }

    uint8_t pkt[1500];
    uint8_t rxbuf[1600];
    const struct bootp *bp;
    size_t bootp_len;
    int t;

    /*
     * DHC-03: RFC 2131 3.1 step 5 -- a client that gets neither DHCPACK nor
     * DHCPNAK retransmits the DHCPREQUEST (4.1 backoff), and if that fails
     * too "reverts to INIT state and restarts the initialization process".
     * The REQUEST went out once, with a flat 5 s wait, and on timeout
     * dhclient exited: one lost REQUEST or ACK frame meant no lease for the
     * rest of the boot, while the server held the binding for nobody.
     * INIT is re-entered up to DHCP_INIT_ATTEMPTS times.
     */
    for (int init = 0; init < DHCP_INIT_ATTEMPTS; init++) {
        if (init > 0)
            fprintf(stdout, "dhclient: restarting from INIT (%d/%d)\n",
                    init + 1, DHCP_INIT_ATTEMPTS);

        /* A new transaction: a fresh random XID (4.4.1). */
        uint32_t xid = ((uint32_t)rand() << 16) ^ (uint32_t)rand();

        /* ---- DISCOVER (retransmit a few times) ----
         *
         * RFC 2131 §4.1: a client that gets no response retransmits the
         * DISCOVER.  A single send is fragile — one dropped frame (common
         * right after link-up, before the switch learns the port / finishes
         * STP) means no lease.  Retransmit with the same xid, waiting
         * retx_delay() for an OFFER after each; then give up so boot
         * proceeds without a lease rather than stalling.  Combined with the
         * non-blocking socket above, the total wait is bounded (about a
         * minute for DHCP_DISCOVER_TRIES = 4). */
        uint32_t offered_ip = 0, server_id = 0;
        double started = now_sec();
        for (int dtry = 0; dtry < DHCP_DISCOVER_TRIES && !offered_ip; dtry++) {
            size_t n = build_dhcp_packet(pkt, hw, xid, DHCP_DISCOVER, 0, 0, 0);
            if (sendto(pkts, pkt, n, 0, (struct sockaddr *)&sll,
                       sizeof(sll)) != (ssize_t)n) {
                perror("sendto DISCOVER"); close(pkts); return 1;
            }
            fprintf(stdout, "dhclient: DHCPDISCOVER on %s, xid=0x%08x "
                    "(try %d/%d)\n", iface, xid, dtry + 1, DHCP_DISCOVER_TRIES);

            double deadline = now_sec() + retx_delay(dtry);
            while ((t = recv_dhcp(pkts, rxbuf, sizeof(rxbuf), xid, deadline,
                                  &bp, &bootp_len)) != 0) {
                if (t != DHCP_OFFER) continue;
                /* DHC-11: an OFFER MUST carry a 'server identifier' (Table
                 * 3), which the REQUEST must then name (3.1 step 3), and
                 * must offer an address a host can use.  One without it
                 * produced a REQUEST naming server 0.0.0.0; a broadcast,
                 * multicast, loopback or class E 'yiaddr' was installed. */
                size_t mlen;
                uint32_t sid = 0;
                const uint8_t *p = find_opt(bp, bootp_len, DHCP_OPT_SRV_ID, &mlen);
                if (p && mlen == 4) memcpy(&sid, p, 4);
                if (!sid || !usable_addr(bp->yiaddr)) continue;
                offered_ip = bp->yiaddr;
                server_id = sid;

                uint8_t *yi = (uint8_t *)&offered_ip;
                fprintf(stdout, "dhclient: DHCPOFFER %u.%u.%u.%u from ",
                        yi[0], yi[1], yi[2], yi[3]);
                uint8_t *si = (uint8_t *)&server_id;
                fprintf(stdout, "%u.%u.%u.%u\n", si[0], si[1], si[2], si[3]);
                break;
            }
        }
        if (!offered_ip) {
            fprintf(stderr, "dhclient: no OFFER after %d DISCOVER attempts "
                    "(%.0fs); giving up\n",
                    DHCP_DISCOVER_TRIES, now_sec() - started);
            close(pkts);
            return 1;
        }

        /* ---- REQUEST, retransmitted until ACK or NAK ---- */
        int naked = 0, declined = 0;
        for (int rtry = 0; rtry < DHCP_REQUEST_TRIES && !naked && !declined;
             rtry++) {
            size_t n = build_dhcp_packet(pkt, hw, xid, DHCP_REQUEST, 0,
                                         offered_ip, server_id);
            double sent_at = now_sec();
            if (sendto(pkts, pkt, n, 0, (struct sockaddr *)&sll,
                       sizeof(sll)) != (ssize_t)n) {
                perror("sendto REQUEST"); close(pkts); return 1;
            }
            fprintf(stdout, "dhclient: DHCPREQUEST (try %d/%d)\n",
                    rtry + 1, DHCP_REQUEST_TRIES);

            double deadline = now_sec() + retx_delay(rtry);
            while ((t = recv_dhcp(pkts, rxbuf, sizeof(rxbuf), xid, deadline,
                                  &bp, &bootp_len)) != 0) {
                /* DHC-04: RFC 2131 3.1 step 5 / Figure 5 -- a DHCPNAK sends
                 * the client straight back to INIT.  It was discarded like
                 * any other non-ACK, so dhclient sat out its timeout and
                 * gave up when an immediate restart would have worked. */
                if (t == DHCP_NAK) {
                    fprintf(stdout, "dhclient: DHCPNAK\n");
                    naked = 1;
                    break;
                }
                if (t != DHCP_ACK) continue;
                /* DHC-11: only the selected server's ACK, for an address
                 * a host can use. */
                {
                    size_t mlen;
                    uint32_t sid = 0;
                    const uint8_t *p = find_opt(bp, bootp_len,
                                                DHCP_OPT_SRV_ID, &mlen);
                    if (p && mlen == 4) memcpy(&sid, p, 4);
                    if ((sid && sid != server_id) || !usable_addr(bp->yiaddr))
                        continue;
                }
                fprintf(stdout, "dhclient: DHCPACK\n");
                /* DHC-07: RFC 2131 3.1 step 5 -- check the address is
                 * free; if it is taken the client MUST send a DHCPDECLINE
                 * and restart, after waiting at least ten seconds.  The
                 * address used to be installed unchecked, so a duplicate
                 * went unnoticed and the server kept handing it out. */
                uint32_t yi = bp->yiaddr;
                if (arp_in_use(pkts, &sll, hw, yi)) {
                    uint8_t *y = (uint8_t *)&yi;
                    fprintf(stdout, "dhclient: %u.%u.%u.%u is already in use; "
                            "DHCPDECLINE\n", y[0], y[1], y[2], y[3]);
                    uint32_t dxid = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
                    n = build_dhcp_packet(pkt, hw, dxid, DHCP_DECLINE, 0,
                                          yi, server_id);
                    sendto(pkts, pkt, n, 0, (struct sockaddr *)&sll,
                           sizeof(sll));
                    declined = 1;
                    break;
                }
                memset(L, 0, sizeof(*L));
                L->server = server_id;
                lease_from_ack(bp, bootp_len, sent_at, L);
                int rc = install_lease(iface, bp, bootp_len, "bound");
                /* 4.4.1: announce the new address, clearing stale ARP
                 * cache entries on the subnet. */
                send_arp(pkts, &sll, hw, 2, yi, hw, yi);
                close(pkts);
                return rc;
            }
        }
        if (declined)
            sleep(DECLINE_WAIT);
        else if (!naked)
            fprintf(stderr, "dhclient: no DHCPACK after %d DHCPREQUESTs; "
                    "initialization failed\n", DHCP_REQUEST_TRIES);
    }
    fprintf(stderr, "dhclient: no lease after %d attempts; giving up\n",
            DHCP_INIT_ATTEMPTS);
    close(pkts);
    return 1;
}

/*
 * DHC-13: with the BROADCAST flag clear, a server unicasts the OFFER and
 * ACK to 'yiaddr'.  AF_PACKET sees them either way, but if the kernel
 * already holds that address (10.0.2.15 at boot under QEMU) its UDP layer
 * receives them too and, with nothing on port 68, would answer the server
 * with ICMP port unreachable.  A socket on port 68 held for the exchange
 * takes those copies instead; nothing reads it.
 */
static int acquire_quietly(const char *iface, const uint8_t hw[6],
                           int ifindex, struct lease *L) {
    int sink = socket(AF_INET, SOCK_DGRAM, 0);
    if (sink >= 0) {
        int one = 1;
        setsockopt(sink, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in me;
        memset(&me, 0, sizeof(me));
        me.sin_family = AF_INET;
        me.sin_port = htons(68);
        if (bind(sink, (struct sockaddr *)&me, sizeof(me)) < 0) {
            close(sink);
            sink = -1;
        }
    }
    int rc = acquire(iface, hw, ifindex, L);
    if (sink >= 0) close(sink);
    return rc;
}

/*
 * DHC-02: keep the lease.  RFC 2131 4.4.5: at T1 renew with the leasing
 * server, at T2 rebind with any, and if the lease runs out "the client
 * moves to INIT state, MUST immediately stop any other network
 * processing"; 3.7 likewise.  dhclient requested the lease time and
 * never read it, then exited once bound, so nothing renewed the lease and
 * the address stayed in use after it expired -- by which time the server
 * could have given it to another host.
 */
static void maintain(const char *iface, const uint8_t hw[6], int ifindex,
                     struct lease *L) {
    for (;;) {
        sleep_until(L->start + L->t1);
        int r = extend(iface, hw, L, 0);            /* RENEWING */
        if (r == 0) r = extend(iface, hw, L, 1);    /* REBINDING */
        if (r > 0) {
            if (L->infinite) return;
            continue;                               /* BOUND again */
        }
        drop_lease(iface);
        fprintf(stdout, "dhclient: %s; address released, restarting "
                "from INIT\n", r < 0 ? "lease refused" : "lease expired");
        while (acquire_quietly(iface, hw, ifindex, L) != 0)
            sleep(DHCP_REACQUIRE_WAIT);
        if (L->infinite) return;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: dhclient <iface>\n");
        return 2;
    }
    const char *iface = argv[1];

    uint8_t hw[6];
    int ifindex;
    get_hw_addr(iface, hw, &ifindex);
    fprintf(stdout, "dhclient: %s ifindex=%d hw=%02x:%02x:%02x:%02x:%02x:%02x\n",
            iface, ifindex, hw[0], hw[1], hw[2], hw[3], hw[4], hw[5]);
    srand((unsigned)now_sec());

    struct lease L;
    if (acquire_quietly(iface, hw, ifindex, &L) != 0)
        return 1;
    if (L.infinite) {
        fprintf(stdout, "dhclient: infinite lease; nothing to renew\n");
        return 0;
    }

    /* Bound: let boot continue, and keep the lease from the background. */
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        perror("dhclient: fork; the lease will not be renewed");
        return 0;
    }
    if (pid > 0)
        return 0;
    setsid();
    maintain(iface, hw, ifindex, &L);
    return 0;
}
