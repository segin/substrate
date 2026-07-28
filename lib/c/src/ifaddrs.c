/*
 * ifaddrs.c — getifaddrs(3) / freeifaddrs(3).
 *
 * Enumerates interfaces via SIOCGIFCONF (which the kernel fills with each
 * interface's name and primary IPv4 address), then queries the netmask and
 * flags per interface with SIOCGIFNETMASK / SIOCGIFFLAGS.  Builds the BSD
 * struct ifaddrs linked list.  IPv4 only, which is all SIOCGIFCONF reports.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

static struct sockaddr *dup_sa(const struct sockaddr *sa)
{
    struct sockaddr *p = malloc(sizeof(struct sockaddr));
    if (p) memcpy(p, sa, sizeof(struct sockaddr));
    return p;
}

int getifaddrs(struct ifaddrs **ifap)
{
    if (!ifap) { errno = EINVAL; return -1; }
    *ifap = NULL;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    /* Pull the interface list.  One generous buffer; the kernel reports a
     * handful of interfaces, so a resize loop is not needed in practice. */
    char buf[16 * sizeof(struct ifreq)];
    struct ifconf ifc;
    ifc.ifc_len = (int)sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) { close(fd); return -1; }

    int count = ifc.ifc_len / (int)sizeof(struct ifreq);
    struct ifreq *list = ifc.ifc_req;
    struct ifaddrs *head = NULL, *tail = NULL;

    for (int i = 0; i < count; i++) {
        struct ifaddrs *ifa = calloc(1, sizeof(*ifa));
        if (!ifa) continue;
        ifa->ifa_name = strdup(list[i].ifr_name);
        ifa->ifa_addr = dup_sa(&list[i].ifr_addr);   /* SIOCGIFCONF filled it */

        struct ifreq req;
        memset(&req, 0, sizeof req);
        strlcpy(req.ifr_name, list[i].ifr_name, sizeof(req.ifr_name));
        if (ioctl(fd, SIOCGIFNETMASK, &req) == 0)
            ifa->ifa_netmask = dup_sa(&req.ifr_netmask);

        memset(&req, 0, sizeof req);
        strlcpy(req.ifr_name, list[i].ifr_name, sizeof(req.ifr_name));
        if (ioctl(fd, SIOCGIFFLAGS, &req) == 0)
            ifa->ifa_flags = (unsigned int)(unsigned short)req.ifr_flags;

        if (tail) tail->ifa_next = ifa; else head = ifa;
        tail = ifa;
    }

    close(fd);
    *ifap = head;
    return 0;
}

/* if_nametoindex(3): interface name -> kernel ifindex via SIOCGIFINDEX. */
unsigned int if_nametoindex(const char *ifname)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    unsigned int idx = 0;
    if (ioctl(fd, SIOCGIFINDEX, &ifr) == 0)
        idx = (unsigned int)ifr.ifr_ifindex;
    close(fd);
    return idx;
}

/* if_indextoname(3): ifindex -> name (buffer must hold IF_NAMESIZE). */
char *if_indextoname(unsigned int ifindex, char *ifname)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    ifr.ifr_ifindex = (int)ifindex;
    char *ret = NULL;
    if (ioctl(fd, SIOCGIFNAME, &ifr) == 0) {
        strlcpy(ifname, ifr.ifr_name, IFNAMSIZ);
        ret = ifname;
    }
    close(fd);
    return ret;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    while (ifa) {
        struct ifaddrs *next = ifa->ifa_next;
        free(ifa->ifa_name);
        free(ifa->ifa_addr);
        free(ifa->ifa_netmask);
        free(ifa);
        ifa = next;
    }
}
