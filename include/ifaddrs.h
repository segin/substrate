#ifndef _IFADDRS_H
#define _IFADDRS_H

/*
 * getifaddrs(3) — enumerate network interface addresses.  POSIX/BSD API,
 * also expected by libtirpc and other portable network code.
 */

#include <sys/socket.h>

struct ifaddrs {
    struct ifaddrs  *ifa_next;     /* next entry, or NULL */
    char            *ifa_name;     /* interface name */
    unsigned int     ifa_flags;    /* interface flags (SIOCGIFFLAGS) */
    struct sockaddr *ifa_addr;     /* interface address */
    struct sockaddr *ifa_netmask;  /* netmask */
    union {
        struct sockaddr *ifu_broadaddr; /* broadcast address */
        struct sockaddr *ifu_dstaddr;   /* point-to-point peer */
    } ifa_ifu;
#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr
    void            *ifa_data;     /* address-family-specific data */
};

int  getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#endif /* _IFADDRS_H */
