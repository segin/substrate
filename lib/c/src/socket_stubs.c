/*
 * socket_stubs.c — BSD-socket-family ENOSYS stubs.
 *
 * Substrate has no in-kernel sockets layer yet, but a handful of
 * userland programs (and several GCC subsystems — sarif-sink in
 * particular) reference socket(), connect(), etc. unconditionally.
 * Without symbol-level stubs the link fails outright; with these
 * stubs the call returns -1 / errno=ENOSYS at runtime, which
 * matches the documented "not implemented on this platform"
 * contract.
 *
 * Replace these per-function the moment a real sockets layer
 * lands — these are link-fillers only.
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define STUB_ENOSYS()  do { errno = ENOSYS; return -1; } while (0)

int socket(int domain, int type, int protocol)
{
    (void)domain; (void)type; (void)protocol;
    STUB_ENOSYS();
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    (void)domain; (void)type; (void)protocol; (void)sv;
    STUB_ENOSYS();
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)sockfd; (void)addr; (void)addrlen;
    STUB_ENOSYS();
}

int listen(int sockfd, int backlog)
{
    (void)sockfd; (void)backlog;
    STUB_ENOSYS();
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    (void)sockfd; (void)addr; (void)addrlen;
    STUB_ENOSYS();
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)sockfd; (void)addr; (void)addrlen;
    STUB_ENOSYS();
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    (void)sockfd; (void)buf; (void)len; (void)flags;
    errno = ENOSYS; return -1;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    (void)sockfd; (void)buf; (void)len; (void)flags;
    errno = ENOSYS; return -1;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    (void)sockfd; (void)buf; (void)len; (void)flags; (void)dest_addr; (void)addrlen;
    errno = ENOSYS; return -1;
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    (void)sockfd; (void)buf; (void)len; (void)flags; (void)src_addr; (void)addrlen;
    errno = ENOSYS; return -1;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    (void)sockfd; (void)msg; (void)flags;
    errno = ENOSYS; return -1;
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    (void)sockfd; (void)msg; (void)flags;
    errno = ENOSYS; return -1;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    (void)sockfd; (void)level; (void)optname; (void)optval; (void)optlen;
    STUB_ENOSYS();
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    (void)sockfd; (void)level; (void)optname; (void)optval; (void)optlen;
    STUB_ENOSYS();
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    (void)sockfd; (void)addr; (void)addrlen;
    STUB_ENOSYS();
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    (void)sockfd; (void)addr; (void)addrlen;
    STUB_ENOSYS();
}

int shutdown(int sockfd, int how)
{
    (void)sockfd; (void)how;
    STUB_ENOSYS();
}

/* netdb — name resolution */
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    (void)node; (void)service; (void)hints; (void)res;
    return EAI_FAIL;
}

void freeaddrinfo(struct addrinfo *res) { (void)res; }

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    (void)sa; (void)salen; (void)host; (void)hostlen;
    (void)serv; (void)servlen; (void)flags;
    return EAI_FAIL;
}

const char *gai_strerror(int errcode) { (void)errcode; return "name resolution unavailable"; }

/* arpa/inet — address conversion.  These could be implemented
 * properly without any kernel support but the link-time goal is
 * just to satisfy the references for now. */
int inet_pton(int af, const char *src, void *dst)
{
    (void)af; (void)src; (void)dst;
    return 0;  /* "src doesn't contain a character string representing a valid network address" */
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    (void)af; (void)src; (void)dst; (void)size;
    errno = ENOSPC;
    return 0;
}

/* Network byte-order helpers — real implementations, not stubs.
 * i386 is little-endian; net order is big-endian.  These compile
 * to a single bswap on modern GCC and are safe to inline-replace
 * when sys/socket.h gains the canonical macros. */
uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
uint16_t ntohs(uint16_t x) { return __builtin_bswap16(x); }
uint32_t ntohl(uint32_t x) { return __builtin_bswap32(x); }
