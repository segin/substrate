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

struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);

#ifdef __cplusplus
}
#endif
#endif
