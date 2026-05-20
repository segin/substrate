/*
 * <netdb.h> — host/service database, stub.
 * Substrate doesn't provide DNS resolution today; this header
 * shapes things for build-time consumers.  Functions return -1.
 */
#ifndef _NETDB_H
#define _NETDB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <netinet/in.h>

struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
};
#define h_addr h_addr_list[0]

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    socklen_t        ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};

struct servent {
    char  *s_name;
    char **s_aliases;
    int    s_port;
    char  *s_proto;
};

struct protoent {
    char  *p_name;
    char **p_aliases;
    int    p_proto;
};

struct netent {
    char    *n_name;
    char   **n_aliases;
    int      n_addrtype;
    uint32_t n_net;
};

#define AI_PASSIVE     0x01
#define AI_CANONNAME   0x02
#define AI_NUMERICHOST 0x04
#define AI_NUMERICSERV 0x08

#define EAI_AGAIN      -3
#define EAI_BADFLAGS   -1
#define EAI_FAIL       -4
#define EAI_FAMILY     -6
#define EAI_MEMORY    -10
#define EAI_NODATA     -5
#define EAI_NONAME     -2
#define EAI_SERVICE    -8
#define EAI_SOCKTYPE   -7
#define EAI_SYSTEM    -11
#define EAI_OVERFLOW  -12
#define EAI_ADDRFAMILY -9   /* address family for node not supported */

struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
/* getnameinfo() buffer sizes + flag bits — POSIX-2008.  */
#define NI_MAXHOST      1025
#define NI_MAXSERV      32

#define NI_NUMERICHOST  0x01    /* fill host buf with numeric address */
#define NI_NUMERICSERV  0x02    /* fill serv buf with numeric port */
#define NI_NOFQDN       0x04    /* return only nodename portion */
#define NI_NAMEREQD     0x08    /* fail if name not reverse-resolvable */
#define NI_DGRAM        0x10    /* service is datagram (port mapping diff) */

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);

/* Legacy / setXent / endXent iteration interfaces.  All four
 * families (hosts / networks / protocols / services) read from
 * the corresponding /etc/<file> on each invocation; substrate has
 * no DNS resolver yet so gethostbyname is local-files-only. */
struct hostent *gethostent(void);
void           sethostent(int stayopen);
void           endhostent(void);

struct netent *getnetent(void);
struct netent *getnetbyname(const char *name);
struct netent *getnetbyaddr(uint32_t net, int type);
void           setnetent(int stayopen);
void           endnetent(void);

struct protoent *getprotoent(void);
struct protoent *getprotobyname(const char *name);
struct protoent *getprotobynumber(int proto);
void             setprotoent(int stayopen);
void             endprotoent(void);

struct servent *getservent(void);
struct servent *getservbyname(const char *name, const char *proto);
struct servent *getservbyport(int port, const char *proto);
void            setservent(int stayopen);
void            endservent(void);

/* h_errno + values for gethostbyname-family callers. */
extern int h_errno;
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4
#define NO_ADDRESS     NO_DATA

/* Decode the value of h_errno (or the eai* values).  Substrate's
 * gethostbyname is currently /etc/hosts-only so the only h_errno
 * that ever surfaces is HOST_NOT_FOUND — but ports still expect the
 * decoder to exist.  */
const char *hstrerror(int err);
void        herror(const char *s);

#ifdef __cplusplus
}
#endif
#endif
