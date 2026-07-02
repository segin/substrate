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
 * On ACK, the lease (address, netmask, router) is installed on the
 * interface via the same SIOC* ioctls /sbin/ifconfig uses.
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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
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

/* DISCOVER retransmit policy: how many DISCOVERs to send, and how long to
 * wait for an OFFER after each, before giving up so boot can proceed without
 * a lease.  Bounded total wait = DHCP_DISCOVER_TRIES * DHCP_DISCOVER_WAIT. */
#define DHCP_DISCOVER_TRIES 4
#define DHCP_DISCOVER_WAIT  2.0

/* DHCP message types (RFC 2132 §9.6) */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

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
#define DHCP_OPT_CLIENT_ID 61  /* RFC 2132 §9.14 — Client Identifier */
#define DHCP_OPT_SEARCH   119  /* RFC 3397: Domain Search */
#define DHCP_OPT_END      255

#define DHCP_MAGIC 0x63825363u

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

static double now_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
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

static void set_ipv4(const char *iface, unsigned long req, uint32_t addr) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); exit(1); }
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, iface, sizeof(r.ifr_name));
    struct sockaddr_in *sin = (struct sockaddr_in *)&r.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = addr;
    if (ioctl(s, req, &r) < 0) {
        fprintf(stderr, "dhclient: ioctl 0x%lx: %s\n", req, strerror(errno));
    }
    close(s);
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

static size_t build_dhcp_packet(uint8_t *out,
                                const uint8_t hw[6],
                                uint32_t xid,
                                uint8_t msg_type,
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
    bp->flags = __builtin_bswap16(0x8000);  /* BROADCAST flag */
    memcpy(bp->chaddr, hw, 6);
    bp->magic = __builtin_bswap32(DHCP_MAGIC);

    /* Options */
    uint8_t *op = bp->options;
    *op++ = DHCP_OPT_MSGTYPE; *op++ = 1; *op++ = msg_type;
    if (msg_type == DHCP_REQUEST) {
        *op++ = DHCP_OPT_REQ_IP; *op++ = 4;
        memcpy(op, &request_ip, 4); op += 4;
        *op++ = DHCP_OPT_SRV_ID; *op++ = 4;
        memcpy(op, &server_id, 4); op += 4;
    }
    *op++ = DHCP_OPT_PARAMLST; *op++ = 6;
    *op++ = DHCP_OPT_SUBNET;
    *op++ = DHCP_OPT_ROUTER;
    *op++ = DHCP_OPT_DNS;
    *op++ = DHCP_OPT_DOMAIN;       /* RFC 2132 — domain name */
    *op++ = DHCP_OPT_SEARCH;       /* RFC 3397 — search list */
    *op++ = DHCP_OPT_LEASE;

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
            *op++ = DHCP_OPT_HOSTNAME;
            *op++ = (uint8_t)hn_len;
            memcpy(op, hn, hn_len);
            op += hn_len;

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
    ih->saddr = 0;                  /* 0.0.0.0 */
    ih->daddr = 0xFFFFFFFFu;        /* 255.255.255.255 */
    ih->check = inet_csum(ih, sizeof(*ih));

    return sizeof(*eh) + ip_len;
}

/* ---- option parsing ---- */

static const uint8_t *find_opt(const struct bootp *bp, size_t len, uint8_t code, uint8_t *out_len) {
    size_t body_off = (size_t)((const uint8_t *)bp->options - (const uint8_t *)bp);
    if (len <= body_off) return NULL;
    const uint8_t *opts = bp->options;
    size_t i = 0;
    size_t max = len - body_off;
    while (i < max) {
        uint8_t c = opts[i++];
        if (c == 0) continue;
        if (c == DHCP_OPT_END) break;
        if (i >= max) break;
        uint8_t l = opts[i++];
        if (i + l > max) break;
        if (c == code) {
            if (out_len) *out_len = l;
            return &opts[i];
        }
        i += l;
    }
    return NULL;
}

/* ---- main ---- */

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

    /* Generate a random XID. */
    srand((unsigned)now_sec());
    uint32_t xid = ((uint32_t)rand() << 16) ^ (uint32_t)rand();

    uint8_t pkt[1500];

    /* ---- DISCOVER (retransmit a few times) ----
     *
     * RFC 2131 §4.1: a client that gets no response retransmits the DISCOVER.
     * A single send is fragile — one dropped frame (common right after
     * link-up, before the switch learns the port / finishes STP) means no
     * lease.  Retransmit with the same xid a handful of times, each with its
     * own short OFFER window; then give up so boot proceeds without a lease
     * rather than stalling.  Combined with the non-blocking socket above, the
     * total wait is bounded (DHCP_DISCOVER_TRIES * DHCP_DISCOVER_WAIT). */
    uint32_t offered_ip = 0, server_id = 0, subnet = 0, router = 0;
    uint8_t rxbuf[1600];
    size_t n;
    double deadline;
    for (int dtry = 0; dtry < DHCP_DISCOVER_TRIES && !offered_ip; dtry++) {
    n = build_dhcp_packet(pkt, hw, xid, DHCP_DISCOVER, 0, 0);
    if (sendto(pkts, pkt, n, 0, (struct sockaddr *)&sll, sizeof(sll)) != (ssize_t)n) {
        perror("sendto DISCOVER"); close(pkts); return 1;
    }
    fprintf(stdout, "dhclient: DHCPDISCOVER on %s, xid=0x%08x (try %d/%d)\n",
            iface, xid, dtry + 1, DHCP_DISCOVER_TRIES);

    /* ---- await OFFER ---- */
    deadline = now_sec() + DHCP_DISCOVER_WAIT;
    while (now_sec() < deadline) {
        usleep(5000);
        ssize_t r = recv(pkts, rxbuf, sizeof(rxbuf), 0);
        if (r < (ssize_t)(sizeof(struct eth_hdr) + sizeof(struct ip_hdr) +
                          sizeof(struct udp_hdr) + 240)) continue;
        struct eth_hdr *eh = (struct eth_hdr *)rxbuf;
        if (__builtin_bswap16(eh->ethertype) != ETH_P_IP) continue;
        struct ip_hdr *ih = (struct ip_hdr *)(rxbuf + sizeof(*eh));
        size_t hlen = (ih->vhl & 0x0F) * 4;
        if (ih->proto != IPPROTO_UDP) continue;
        struct udp_hdr *uh = (struct udp_hdr *)(rxbuf + sizeof(*eh) + hlen);
        if (__builtin_bswap16(uh->dst) != 68) continue;
        struct bootp *bp =
            (struct bootp *)(rxbuf + sizeof(*eh) + hlen + sizeof(*uh));
        size_t bootp_len = (size_t)r - sizeof(*eh) - hlen - sizeof(*uh);
        if (bp->op != 2 || bp->xid != xid) continue;
        uint8_t mlen;
        const uint8_t *mt = find_opt(bp, bootp_len, DHCP_OPT_MSGTYPE, &mlen);
        if (!mt || *mt != DHCP_OFFER) continue;

        offered_ip = bp->yiaddr;
        const uint8_t *p = find_opt(bp, bootp_len, DHCP_OPT_SRV_ID, &mlen);
        if (p && mlen == 4) memcpy(&server_id, p, 4);
        p = find_opt(bp, bootp_len, DHCP_OPT_SUBNET, &mlen);
        if (p && mlen == 4) memcpy(&subnet, p, 4);
        p = find_opt(bp, bootp_len, DHCP_OPT_ROUTER, &mlen);
        if (p && mlen == 4) memcpy(&router, p, 4);
        /* DNS, domain, search-list are carried through to ACK, but
         * we may already have them on the OFFER too — stash for the
         * resolv.conf writer at install time. */

        uint8_t *yi = (uint8_t *)&offered_ip;
        fprintf(stdout, "dhclient: DHCPOFFER %u.%u.%u.%u from ",
                yi[0], yi[1], yi[2], yi[3]);
        uint8_t *si = (uint8_t *)&server_id;
        fprintf(stdout, "%u.%u.%u.%u\n", si[0], si[1], si[2], si[3]);
        break;
    }
    }   /* end DISCOVER retransmit loop */
    if (!offered_ip) {
        fprintf(stderr, "dhclient: no OFFER after %d DISCOVER attempts "
                "(%.0fs); giving up\n",
                DHCP_DISCOVER_TRIES,
                DHCP_DISCOVER_TRIES * DHCP_DISCOVER_WAIT);
        close(pkts);
        return 1;
    }

    /* ---- REQUEST ---- */
    n = build_dhcp_packet(pkt, hw, xid, DHCP_REQUEST, offered_ip, server_id);
    if (sendto(pkts, pkt, n, 0, (struct sockaddr *)&sll, sizeof(sll)) != (ssize_t)n) {
        perror("sendto REQUEST"); close(pkts); return 1;
    }
    fprintf(stdout, "dhclient: DHCPREQUEST\n");

    /* ---- await ACK ---- */
    deadline = now_sec() + 5.0;
    while (now_sec() < deadline) {
        usleep(5000);
        ssize_t r = recv(pkts, rxbuf, sizeof(rxbuf), 0);
        if (r < (ssize_t)(sizeof(struct eth_hdr) + sizeof(struct ip_hdr) +
                          sizeof(struct udp_hdr) + 240)) continue;
        struct eth_hdr *eh = (struct eth_hdr *)rxbuf;
        if (__builtin_bswap16(eh->ethertype) != ETH_P_IP) continue;
        struct ip_hdr *ih = (struct ip_hdr *)(rxbuf + sizeof(*eh));
        size_t hlen = (ih->vhl & 0x0F) * 4;
        if (ih->proto != IPPROTO_UDP) continue;
        struct udp_hdr *uh = (struct udp_hdr *)(rxbuf + sizeof(*eh) + hlen);
        if (__builtin_bswap16(uh->dst) != 68) continue;
        struct bootp *bp =
            (struct bootp *)(rxbuf + sizeof(*eh) + hlen + sizeof(*uh));
        size_t bootp_len = (size_t)r - sizeof(*eh) - hlen - sizeof(*uh);
        if (bp->op != 2 || bp->xid != xid) continue;
        uint8_t mlen;
        const uint8_t *mt = find_opt(bp, bootp_len, DHCP_OPT_MSGTYPE, &mlen);
        if (!mt || *mt != DHCP_ACK) continue;

        fprintf(stdout, "dhclient: DHCPACK\n");

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
        if (op_p && mlen >= 4 && mlen <= (uint8_t)sizeof(dns_buf)) {
            memcpy(dns_buf, op_p, mlen);
            dns_len = mlen;
        }
        op_p = find_opt(bp, bootp_len, DHCP_OPT_DOMAIN, &mlen);
        if (op_p && mlen > 0) {
            unsigned ml = mlen;
            if (ml > sizeof(domain_buf) - 1) ml = sizeof(domain_buf) - 1;
            memcpy(domain_buf, op_p, ml);
            domain_buf[ml] = '\0';
            domain_len = ml;
        }
        op_p = find_opt(bp, bootp_len, DHCP_OPT_SEARCH, &mlen);
        if (op_p && mlen > 0) {
            unsigned ml = mlen;
            if (ml > sizeof(search_buf)) ml = sizeof(search_buf);
            memcpy(search_buf, op_p, ml);
            search_len = ml;
        }

        close(pkts);

        /* Install lease. */
        set_ipv4(iface, SIOCSIFADDR,    offered_ip);
        if (subnet) set_ipv4(iface, SIOCSIFNETMASK, subnet);
        if (router) set_ipv4(iface, SIOCSIFGATEWAY, router);

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
                                name[no++] = (char)search_buf[j + 1 + k];
                            }
                            j += 1 + l;
                            first = 0;
                        }
                        name[no] = '\0';
                        if (no > 0) fprintf(rf, " %s", name);
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
        fprintf(stdout, "dhclient: bound %u.%u.%u.%u/%u.%u.%u.%u",
                yi[0], yi[1], yi[2], yi[3],
                m[0], m[1], m[2], m[3]);
        if (router) fprintf(stdout, " via %u.%u.%u.%u",
                            r2[0], r2[1], r2[2], r2[3]);
        fprintf(stdout, "\n");
        return 0;
    }
    fprintf(stderr, "dhclient: no ACK received within 5s\n");
    close(pkts);
    return 1;
}
