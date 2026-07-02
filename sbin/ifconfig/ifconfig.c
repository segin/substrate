/*
 * ifconfig — display and configure network interfaces.
 *
 *   ifconfig                       — list all interfaces, long form
 *   ifconfig -a                    — list all (alias for no args)
 *   ifconfig <iface>               — show one interface
 *   ifconfig <iface> up|down       — toggle IFF_UP
 *   ifconfig <iface> <ipv4>        — set IPv4 address
 *   ifconfig <iface> netmask <m>   — set IPv4 netmask
 *   ifconfig <iface> broadcast <a> — set broadcast (computed by default)
 *   ifconfig <iface> mtu <n>       — set MTU
 *   ifconfig <iface> hw ether MAC  — set hardware address
 *   ifconfig <iface> gateway <ip>  — set IPv4 default gateway (substrate ext)
 *   ifconfig <iface> inet6 add <addr>[/prefix]
 *   ifconfig <iface> inet6 del <addr>
 *   ifconfig <iface> inet6 gw <addr>
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

static int sock_open(int family) {
    int fd = socket(family, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "ifconfig: socket: %s\n", strerror(errno));
        exit(1);
    }
    return fd;
}

static void usage(void) {
    fprintf(stderr,
        "usage: ifconfig [-a]\n"
        "       ifconfig <iface>\n"
        "       ifconfig <iface> up|down\n"
        "       ifconfig <iface> <addr> [netmask <mask>] [broadcast <addr>]\n"
        "       ifconfig <iface> netmask <mask>\n"
        "       ifconfig <iface> mtu <n>\n"
        "       ifconfig <iface> hw ether <MAC>\n"
        "       ifconfig <iface> gateway <addr>\n"
        "       ifconfig <iface> inet6 add <addr>[/prefix]\n"
        "       ifconfig <iface> inet6 del <addr>\n"
        "       ifconfig <iface> inet6 gw <addr>\n");
    exit(1);
}

static int parse_mac(const char *s, uint8_t mac[6]) {
    unsigned a, b, c, d, e, f;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &a, &b, &c, &d, &e, &f) != 6) return -1;
    mac[0]=a; mac[1]=b; mac[2]=c; mac[3]=d; mac[4]=e; mac[5]=f;
    return 0;
}

static int parse_ip4(const char *s, uint32_t *out) {
    unsigned a, b, c, d;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return -1;
    if (a > 255 || b > 255 || c > 255 || d > 255) return -1;
    *out = (uint32_t)a | ((uint32_t)b << 8) |
           ((uint32_t)c << 16) | ((uint32_t)d << 24);
    return 0;
}

static void print_ip4(uint32_t addr) {
    uint8_t *p = (uint8_t *)&addr;
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

static void print_ip6(const uint8_t a[16]) {
    /* Simple grouped print; not RFC 5952-compliant compression. */
    for (int i = 0; i < 16; i += 2) {
        printf("%x", (a[i] << 8) | a[i+1]);
        if (i < 14) printf(":");
    }
}

static int prefixlen_from_mask(uint32_t m) {
    /* m is in network byte order: count leading 1-bits. */
    uint32_t h = __builtin_bswap32(m);
    int n = 0;
    while (h & 0x80000000u) { n++; h <<= 1; }
    return n;
}

static void show_iface(const char *name) {
    int s = sock_open(AF_INET);
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));

    /* Flags + MTU + HW addr + addrs + gateway + v6 */
    if (ioctl(s, SIOCGIFFLAGS, &r) < 0) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        close(s);
        return;
    }
    short flags = r.ifr_flags;

    printf("%s: flags=%d<", name, flags);
    const char *first = "";
    if (flags & IFF_UP)        { printf("%sUP", first); first = ","; }
    if (flags & IFF_BROADCAST) { printf("%sBROADCAST", first); first = ","; }
    if (flags & IFF_LOOPBACK)  { printf("%sLOOPBACK", first); first = ","; }
    if (flags & IFF_RUNNING)   { printf("%sRUNNING", first); first = ","; }
    if (flags & IFF_PROMISC)   { printf("%sPROMISC", first); first = ","; }

    /* MTU */
    if (ioctl(s, SIOCGIFMTU, &r) == 0)
        printf(">  mtu %d\n", r.ifr_mtu);
    else
        printf(">\n");

    /* HW addr */
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));
    if (ioctl(s, SIOCGIFHWADDR, &r) == 0) {
        unsigned char *m = (unsigned char *)r.ifr_hwaddr.sa_data;
        printf("        ether %02x:%02x:%02x:%02x:%02x:%02x\n",
               m[0], m[1], m[2], m[3], m[4], m[5]);
    }

    /* IPv4 addr / netmask / broadcast */
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));
    if (ioctl(s, SIOCGIFADDR, &r) == 0) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&r.ifr_addr;
        uint32_t addr = sin->sin_addr.s_addr;
        if (addr) {
            printf("        inet ");
            print_ip4(addr);
            if (ioctl(s, SIOCGIFNETMASK, &r) == 0) {
                struct sockaddr_in *sm = (struct sockaddr_in *)&r.ifr_netmask;
                printf(" netmask ");
                print_ip4(sm->sin_addr.s_addr);
                printf("/%d", prefixlen_from_mask(sm->sin_addr.s_addr));
            }
            if (ioctl(s, SIOCGIFBRDADDR, &r) == 0) {
                struct sockaddr_in *sb =
                    (struct sockaddr_in *)&r.ifr_broadaddr;
                printf(" broadcast ");
                print_ip4(sb->sin_addr.s_addr);
            }
            printf("\n");
        }
    }

    /* IPv4 gateway (substrate ext) */
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));
    if (ioctl(s, SIOCGIFGATEWAY, &r) == 0) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&r.ifr_addr;
        if (sin->sin_addr.s_addr) {
            printf("        gateway ");
            print_ip4(sin->sin_addr.s_addr);
            printf("\n");
        }
    }

    /* IPv6 addr */
    struct in6_ifreq r6;
    memset(&r6, 0, sizeof(r6));
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));
    if (ioctl(s, SIOCGIFINDEX, &r) == 0) {
        r6.ifr6_ifindex = r.ifr_ifindex;
        if (ioctl(s, SIOCGIFADDR_IN6, &r6) == 0) {
            int any = 0;
            for (int i = 0; i < 16; i++) if (r6.ifr6_addr.s6_addr[i]) { any = 1; break; }
            if (any) {
                printf("        inet6 ");
                print_ip6(r6.ifr6_addr.s6_addr);
                printf("  prefixlen %u\n", r6.ifr6_prefixlen);
            }
        }
    }
    close(s);
}

static void list_all(void) {
    int s = sock_open(AF_INET);
    char buf[16 * sizeof(struct ifreq)];
    struct ifconf ifc;
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(s, SIOCGIFCONF, &ifc) < 0) {
        fprintf(stderr, "ifconfig: SIOCGIFCONF: %s\n", strerror(errno));
        close(s);
        exit(1);
    }
    close(s);
    int n = ifc.ifc_len / sizeof(struct ifreq);
    for (int i = 0; i < n; i++) {
        if (i) printf("\n");
        show_iface(ifc.ifc_req[i].ifr_name);
    }
}

/* ---- mutation helpers ---- */

static void set_flag(const char *name, int set, short flag) {
    int s = sock_open(AF_INET);
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));
    if (ioctl(s, SIOCGIFFLAGS, &r) < 0) {
        fprintf(stderr, "%s: SIOCGIFFLAGS: %s\n", name, strerror(errno));
        close(s); exit(1);
    }
    if (set) r.ifr_flags |= flag;
    else     r.ifr_flags &= ~flag;
    if (ioctl(s, SIOCSIFFLAGS, &r) < 0) {
        fprintf(stderr, "%s: SIOCSIFFLAGS: %s\n", name, strerror(errno));
        close(s); exit(1);
    }
    close(s);
}

static void set_ipv4(const char *name, unsigned long req, const char *val) {
    int s = sock_open(AF_INET);
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));
    struct sockaddr_in *sin = (struct sockaddr_in *)&r.ifr_addr;
    sin->sin_family = AF_INET;
    if (parse_ip4(val, &sin->sin_addr.s_addr) < 0) {
        fprintf(stderr, "ifconfig: invalid IPv4 address: %s\n", val);
        close(s); exit(1);
    }
    if (ioctl(s, req, &r) < 0) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        close(s); exit(1);
    }
    close(s);
}

static void set_mtu(const char *name, const char *val) {
    int s = sock_open(AF_INET);
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));
    r.ifr_mtu = atoi(val);
    if (ioctl(s, SIOCSIFMTU, &r) < 0) {
        fprintf(stderr, "%s: SIOCSIFMTU: %s\n", name, strerror(errno));
        close(s); exit(1);
    }
    close(s);
}

static void set_hwaddr(const char *name, const char *val) {
    int s = sock_open(AF_INET);
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, name, sizeof(r.ifr_name));
    if (parse_mac(val, (uint8_t *)r.ifr_hwaddr.sa_data) < 0) {
        fprintf(stderr, "ifconfig: invalid MAC: %s\n", val);
        close(s); exit(1);
    }
    r.ifr_hwaddr.sa_family = 1;
    if (ioctl(s, SIOCSIFHWADDR, &r) < 0) {
        fprintf(stderr, "%s: SIOCSIFHWADDR: %s\n", name, strerror(errno));
        close(s); exit(1);
    }
    close(s);
}

static int parse_ip6(const char *s, uint8_t out[16], unsigned *prefix_out) {
    char buf[64];
    strlcpy(buf, s, sizeof(buf));
    char *slash = strchr(buf, '/');
    if (slash) {
        *slash = '\0';
        if (prefix_out) *prefix_out = (unsigned)atoi(slash + 1);
    } else if (prefix_out) {
        *prefix_out = 64;
    }
    if (inet_pton(AF_INET6, buf, out) != 1) return -1;
    return 0;
}

static void v6_addr(const char *name, unsigned long req, const char *val) {
    int s = sock_open(AF_INET);
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        close(s); exit(1);
    }
    struct in6_ifreq r6;
    memset(&r6, 0, sizeof(r6));
    r6.ifr6_ifindex = ifr.ifr_ifindex;
    unsigned prefix = 64;
    if (parse_ip6(val, r6.ifr6_addr.s6_addr, &prefix) < 0) {
        fprintf(stderr, "ifconfig: invalid IPv6: %s\n", val);
        close(s); exit(1);
    }
    r6.ifr6_prefixlen = prefix;
    if (ioctl(s, req, &r6) < 0) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        close(s); exit(1);
    }
    close(s);
}

int main(int argc, char **argv) {
    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-a") == 0)) {
        list_all();
        return 0;
    }
    const char *iface = argv[1];
    if (argc == 2) { show_iface(iface); return 0; }

    int i = 2;
    while (i < argc) {
        const char *cmd = argv[i];
        if (strcmp(cmd, "up") == 0)        { set_flag(iface, 1, IFF_UP); i++; }
        else if (strcmp(cmd, "down") == 0) { set_flag(iface, 0, IFF_UP); i++; }
        else if (strcmp(cmd, "netmask") == 0 && i + 1 < argc) {
            set_ipv4(iface, SIOCSIFNETMASK, argv[i + 1]); i += 2;
        }
        else if (strcmp(cmd, "broadcast") == 0 && i + 1 < argc) {
            set_ipv4(iface, SIOCSIFBRDADDR, argv[i + 1]); i += 2;
        }
        else if (strcmp(cmd, "mtu") == 0 && i + 1 < argc) {
            set_mtu(iface, argv[i + 1]); i += 2;
        }
        else if (strcmp(cmd, "hw") == 0 && i + 2 < argc) {
            if (strcmp(argv[i + 1], "ether") != 0) usage();
            set_hwaddr(iface, argv[i + 2]); i += 3;
        }
        else if (strcmp(cmd, "gateway") == 0 && i + 1 < argc) {
            set_ipv4(iface, SIOCSIFGATEWAY, argv[i + 1]); i += 2;
        }
        else if (strcmp(cmd, "inet6") == 0 && i + 2 < argc) {
            const char *sub = argv[i + 1];
            if (strcmp(sub, "add") == 0)      v6_addr(iface, SIOCSIFADDR_IN6, argv[i + 2]);
            else if (strcmp(sub, "del") == 0) v6_addr(iface, SIOCDIFADDR_IN6, argv[i + 2]);
            else if (strcmp(sub, "gw")  == 0) v6_addr(iface, SIOCSIFGW_IN6,   argv[i + 2]);
            else usage();
            i += 3;
        }
        else {
            /* Otherwise treat as an IPv4 address. */
            set_ipv4(iface, SIOCSIFADDR, argv[i]);
            i++;
        }
    }
    return 0;
}
