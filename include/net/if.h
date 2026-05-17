/*
 * <net/if.h> — interface configuration interface.
 *
 * Linux-compatible SIOC* ioctl numbers, struct ifreq, struct ifconf.
 * Used by ifconfig and any other userland tool that wants to drive
 * substrate's IPv4 interface config via an AF_INET socket.
 *
 * For IPv6, ifconfig adds/removes addresses via SIOC{S,D}IFADDR_IN6
 * with struct in6_ifreq, mirroring Linux <linux/ipv6.h>.
 */
#ifndef _NET_IF_H
#define _NET_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>
#include <netinet/in.h>

#define IFNAMSIZ 16
#define IF_NAMESIZE IFNAMSIZ

/* POSIX-2008 interface name/index table.  Used by if_nametoindex(),
 * if_indextoname(), if_nameindex(), if_freenameindex().  Substrate
 * doesn't have a kernel implementation yet — libc currently provides
 * stubs — but the struct must be visible so packages that probe for
 * it (autoconf HAVE_STRUCT_IF_NAMEINDEX, gnulib) can compile.  */
struct if_nameindex {
    unsigned int  if_index;
    char         *if_name;
};

struct ifmap {
    unsigned long mem_start;
    unsigned long mem_end;
    unsigned short base_addr;
    unsigned char  irq;
    unsigned char  dma;
    unsigned char  port;
};

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_dstaddr;
        struct sockaddr ifru_broadaddr;
        struct sockaddr ifru_netmask;
        struct sockaddr ifru_hwaddr;
        short           ifru_flags;
        int             ifru_ivalue;
        int             ifru_mtu;
        struct ifmap    ifru_map;
        char            ifru_slave[IFNAMSIZ];
        char            ifru_newname[IFNAMSIZ];
        char           *ifru_data;
    } ifr_ifru;
};

#define ifr_addr      ifr_ifru.ifru_addr
#define ifr_dstaddr   ifr_ifru.ifru_dstaddr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_netmask   ifr_ifru.ifru_netmask
#define ifr_hwaddr    ifr_ifru.ifru_hwaddr
#define ifr_flags     ifr_ifru.ifru_flags
#define ifr_mtu       ifr_ifru.ifru_mtu
#define ifr_ifindex   ifr_ifru.ifru_ivalue
#define ifr_data      ifr_ifru.ifru_data

struct ifconf {
    int   ifc_len;
    union {
        char         *ifcu_buf;
        struct ifreq *ifcu_req;
    } ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

/* For IPv6 add/del/list (Linux <linux/ipv6.h>). */
struct in6_ifreq {
    struct in6_addr ifr6_addr;
    unsigned int    ifr6_prefixlen;
    int             ifr6_ifindex;
};

/* Interface flags (matches kernel-side NETDEV_IFF_*). */
#define IFF_UP          0x0001
#define IFF_BROADCAST   0x0002
#define IFF_LOOPBACK    0x0008
#define IFF_POINTOPOINT 0x0010
#define IFF_RUNNING     0x0040
#define IFF_PROMISC     0x0100
#define IFF_MULTICAST   0x1000

/* Socket ioctls — Linux numbers. */
#define SIOCGIFNAME     0x8910
#define SIOCGIFCONF     0x8912
#define SIOCGIFFLAGS    0x8913
#define SIOCSIFFLAGS    0x8914
#define SIOCGIFADDR     0x8915
#define SIOCSIFADDR     0x8916
#define SIOCGIFDSTADDR  0x8917
#define SIOCSIFDSTADDR  0x8918
#define SIOCGIFBRDADDR  0x8919
#define SIOCSIFBRDADDR  0x891A
#define SIOCGIFNETMASK  0x891B
#define SIOCSIFNETMASK  0x891C
#define SIOCGIFMETRIC   0x891D
#define SIOCSIFMETRIC   0x891E
#define SIOCGIFMEM      0x891F
#define SIOCSIFMEM      0x8920
#define SIOCGIFMTU      0x8921
#define SIOCSIFMTU      0x8922
#define SIOCSIFNAME     0x8923
#define SIOCSIFHWADDR   0x8924
#define SIOCGIFHWADDR   0x8927
#define SIOCGIFINDEX    0x8933
#define SIOCSIFGATEWAY  0x8980   /* substrate extension */
#define SIOCGIFGATEWAY  0x8981   /* substrate extension */
#define SIOCSIFADDR_IN6 0x8990   /* substrate: set IPv6 addr + prefix */
#define SIOCDIFADDR_IN6 0x8991   /* substrate: clear IPv6 addr */
#define SIOCGIFADDR_IN6 0x8992   /* substrate: get IPv6 addr + prefix */
#define SIOCSIFGW_IN6   0x8993   /* substrate: set IPv6 gateway */

unsigned int if_nametoindex(const char *ifname);
char        *if_indextoname(unsigned int ifindex, char *ifname);
struct if_nameindex *if_nameindex(void);
void                 if_freenameindex(struct if_nameindex *ptr);

#ifdef __cplusplus
}
#endif
#endif
