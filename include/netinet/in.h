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

/* Privileged port range (RFC 1340 / historical BSD).  Ports below
 * IPPORT_RESERVED traditionally require root; bind() on substrate
 * doesn't enforce this yet but the constants are part of the ABI. */
#define IPPORT_RESERVED   1024
#define IPPORT_USERRESERVED 5000

/* Classful net masks — historical (BSD), kept around for code that
 * checks "is this address in 127/8" via `IN_CLASSA_NET`.  Substrate
 * implements CIDR routing internally; these constants are only
 * informational. */
#define IN_CLASSA_NET     0xff000000U
#define IN_CLASSA_NSHIFT  24
#define IN_CLASSA_HOST    0x00ffffffU
#define IN_CLASSB_NET     0xffff0000U
#define IN_CLASSB_NSHIFT  16
#define IN_CLASSC_NET     0xffffff00U
#define IN_CLASSC_NSHIFT  8

#define IN_CLASSA(a)      (((in_addr_t)(a) & 0x80000000U) == 0)
#define IN_CLASSB(a)      (((in_addr_t)(a) & 0xC0000000U) == 0x80000000U)
#define IN_CLASSC(a)      (((in_addr_t)(a) & 0xE0000000U) == 0xC0000000U)

#define IN_LOOPBACKNET    127

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
