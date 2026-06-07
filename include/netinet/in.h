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
#define INADDR_NONE       ((in_addr_t)0xffffffff)  /* inet_addr error return */

/* IPv6 address tests — POSIX / RFC 3493.  substrate's struct
 * in6_addr exposes only the s6_addr[16] byte array, so these are
 * spelled against that rather than the s6_addr32 union glibc uses. */
static inline int __in6_is_addr_loopback(const struct in6_addr *__a)
{
    int __i;
    for (__i = 0; __i < 15; __i++)
        if (__a->s6_addr[__i] != 0)
            return 0;
    return __a->s6_addr[15] == 1;
}
static inline int __in6_is_addr_v4mapped(const struct in6_addr *__a)
{
    int __i;
    for (__i = 0; __i < 10; __i++)
        if (__a->s6_addr[__i] != 0)
            return 0;
    return __a->s6_addr[10] == 0xff && __a->s6_addr[11] == 0xff;
}
#define IN6_IS_ADDR_LOOPBACK(a)  __in6_is_addr_loopback((const struct in6_addr *)(a))
#define IN6_IS_ADDR_V4MAPPED(a)  __in6_is_addr_v4mapped((const struct in6_addr *)(a))

/* Multicast tests.  Substrate's networking stack doesn't actually
 * deliver multicast traffic — these macros exist so ported software
 * (xorg-server, mDNS clients, ...) compiles; runtime behaviour on
 * the predicate paths is "no address is multicast", which is fine
 * for the no-multicast world the kernel implements. */
#define IN_MULTICAST(a)            (((in_addr_t)(a) & 0xf0000000U) == 0xe0000000U)
#define IN_CLASSD(a)               IN_MULTICAST(a)
#define IN_CLASSD_NET              0xf0000000U
#define IN_CLASSD_NSHIFT           28
#define IN_CLASSD_HOST             0x0fffffffU
#define IN_EXPERIMENTAL(a)         (((in_addr_t)(a) & 0xf0000000U) == 0xf0000000U)
#define IN_BADCLASS(a)             IN_EXPERIMENTAL(a)

#define IN6_IS_ADDR_MULTICAST(a)   (((const struct in6_addr *)(a))->s6_addr[0] == 0xff)
/* IPv6 multicast scope tests: low nibble of the second byte is the scope. */
#define __IN6_MC_SCOPE(a) (((const struct in6_addr *)(a))->s6_addr[1] & 0x0f)
#define IN6_IS_ADDR_MC_NODELOCAL(a) (IN6_IS_ADDR_MULTICAST(a) && __IN6_MC_SCOPE(a) == 0x1)
#define IN6_IS_ADDR_MC_LINKLOCAL(a) (IN6_IS_ADDR_MULTICAST(a) && __IN6_MC_SCOPE(a) == 0x2)
#define IN6_IS_ADDR_MC_SITELOCAL(a) (IN6_IS_ADDR_MULTICAST(a) && __IN6_MC_SCOPE(a) == 0x5)
#define IN6_IS_ADDR_MC_ORGLOCAL(a)  (IN6_IS_ADDR_MULTICAST(a) && __IN6_MC_SCOPE(a) == 0x8)
#define IN6_IS_ADDR_MC_GLOBAL(a)    (IN6_IS_ADDR_MULTICAST(a) && __IN6_MC_SCOPE(a) == 0xe)
/* Compare two IPv6 addresses for equality (POSIX/RFC 2553). */
#define IN6_ARE_ADDR_EQUAL(a, b) \
    (__builtin_memcmp(((const struct in6_addr *)(a))->s6_addr, \
                      ((const struct in6_addr *)(b))->s6_addr, 16) == 0)
#define IN6_IS_ADDR_UNSPECIFIED(a) \
    (((const struct in6_addr *)(a))->s6_addr[0]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[1]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[2]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[3]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[4]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[5]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[6]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[7]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[8]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[9]  == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[10] == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[11] == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[12] == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[13] == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[14] == 0 && \
     ((const struct in6_addr *)(a))->s6_addr[15] == 0)
#define IN6_IS_ADDR_LINKLOCAL(a) \
    ((((const struct in6_addr *)(a))->s6_addr[0] == 0xfe) && \
     ((((const struct in6_addr *)(a))->s6_addr[1] & 0xc0) == 0x80))
#define IN6_IS_ADDR_SITELOCAL(a) \
    ((((const struct in6_addr *)(a))->s6_addr[0] == 0xfe) && \
     ((((const struct in6_addr *)(a))->s6_addr[1] & 0xc0) == 0xc0))

/* The "any" / "loopback" addresses as compile-time constants.  Ported
 * code uses these as initializers (e.g. `&in6addr_any`).  Substrate's
 * libc supplies the symbols; declare here. */
extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;
/* substrate's struct in6_addr is `struct { uint8_t s6_addr[16]; }`
 * (no anonymous union, unlike glibc's), so the initializer wraps
 * the array once.  Ported code that uses `(struct in6_addr)
 * IN6ADDR_ANY_INIT` still compiles. */
#define IN6ADDR_ANY_INIT      { { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 } }
#define IN6ADDR_LOOPBACK_INIT { { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 } }

/* IPv4 / IPv6 socket-level option codes — values match Linux.
 * substrate's network stack doesn't act on most of them yet, but
 * setsockopt accepts them without erroring so callers compile and
 * run. */
#define IP_TOS                  1
#define IP_TTL                  2
#define IP_HDRINCL              3
#define IP_OPTIONS              4
#define IP_RECVOPTS             6
#define IP_RETOPTS              7
#define IP_MULTICAST_IF         32
#define IP_MULTICAST_TTL        33
#define IP_MULTICAST_LOOP       34
#define IP_ADD_MEMBERSHIP       35
#define IP_DROP_MEMBERSHIP      36
#define IP_UNBLOCK_SOURCE       37
#define IP_BLOCK_SOURCE         38
#define IP_ADD_SOURCE_MEMBERSHIP   39
#define IP_DROP_SOURCE_MEMBERSHIP  40

#define IPV6_UNICAST_HOPS       16
#define IPV6_MULTICAST_IF       17
#define IPV6_MULTICAST_HOPS     18
#define IPV6_MULTICAST_LOOP     19
#define IPV6_ADD_MEMBERSHIP     20
#define IPV6_DROP_MEMBERSHIP    21
/* RFC 3493 names for the join/leave options (same values as ADD/DROP). */
#define IPV6_JOIN_GROUP         IPV6_ADD_MEMBERSHIP
#define IPV6_LEAVE_GROUP        IPV6_DROP_MEMBERSHIP
#define IPV6_V6ONLY             26

struct ip_mreq {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
};

/* Source-specific multicast request (IP_ADD_SOURCE_MEMBERSHIP &c). */
struct ip_mreq_source {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
    struct in_addr imr_sourceaddr;
};

struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;
    unsigned int    ipv6mr_interface;
};

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

/* IP / IPv6 packet-info options + ancillary-data structs (Linux values).
 * Ported servers (libtirpc's datagram service) set these to learn which
 * local address a datagram arrived on.  Substrate's kernel may not honour
 * them yet; setsockopt then fails harmlessly and the info is just absent. */
#define IP_PKTINFO       8
#define IPV6_RECVPKTINFO 49
#define IPV6_PKTINFO     50
struct in_pktinfo {
    int            ipi_ifindex;    /* interface index */
    struct in_addr ipi_spec_dst;   /* local address */
    struct in_addr ipi_addr;       /* header destination address */
};
struct in6_pktinfo {
    struct in6_addr ipi6_addr;     /* src/dst address */
    unsigned int    ipi6_ifindex;  /* interface index */
};

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
