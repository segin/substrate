/*
 * <netinet/in.h> — IPv4/IPv6 socket addresses, stub.
 * Same status as <sys/socket.h>: shape only, no implementation.
 */
#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>
#include <stdint.h>

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

/* inet_ntop() output-buffer sizes — POSIX-2008.  */
#define INET_ADDRSTRLEN   16    /* "255.255.255.255\0" */
#define INET6_ADDRSTRLEN  46    /* IPv6 + scope id + NUL */

struct in_addr   { in_addr_t s_addr; };
struct in6_addr  { uint8_t s6_addr[16]; };

struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

struct sockaddr_in6 {
    sa_family_t      sin6_family;
    in_port_t        sin6_port;
    uint32_t         sin6_flowinfo;
    struct in6_addr  sin6_addr;
    uint32_t         sin6_scope_id;
};

#define INADDR_ANY        ((in_addr_t)0x00000000)
#define INADDR_LOOPBACK   ((in_addr_t)0x7f000001)
#define INADDR_BROADCAST  ((in_addr_t)0xffffffff)

#define IPPROTO_IP    0
#define IPPROTO_TCP   6
#define IPPROTO_UDP  17
#define IPPROTO_IPV6 41

/* Byte-order conversions.  POSIX places these in <arpa/inet.h> but
 * historically also via <netinet/in.h>; many ports (inetutils' ftp
 * among them) include only the latter. */
uint16_t htons(uint16_t hostshort);
uint32_t htonl(uint32_t hostlong);
uint16_t ntohs(uint16_t netshort);
uint32_t ntohl(uint32_t netlong);

#ifdef __cplusplus
}
#endif
#endif
