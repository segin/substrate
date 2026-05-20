/*
 * socket_stubs.c — BSD-socket-family syscall wrappers + helpers.
 *
 * Substrate's in-kernel AF_UNIX implementation lives in sys/net/af_unix.c
 * and is reached via SYS_SOCKET / SYS_BIND / ... (see sys/arch/i386/syscall.h
 * for the assigned numbers).  These wrappers route the libc-level POSIX
 * calls into those syscalls.  AF_INET / IPv6 are not implemented in the
 * kernel — the kernel returns -EAFNOSUPPORT for those domains.
 *
 * The byte-order, addrinfo, and inet_pton/inet_ntop helpers below are
 * userspace-only and don't touch the kernel.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

long syscall(long number, ...);

static int set_errno(long rc) {
    if (rc < 0) { errno = (int)-rc; return -1; }
    return (int)rc;
}

static ssize_t set_errno_ssz(long rc) {
    if (rc < 0) { errno = (int)-rc; return -1; }
    return (ssize_t)rc;
}

int socket(int domain, int type, int protocol)
{
    return set_errno(syscall(SYS_SOCKET, (long)domain, (long)type, (long)protocol));
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    return set_errno(syscall(SYS_SOCKETPAIR, (long)domain, (long)type, (long)protocol, (long)sv));
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return set_errno(syscall(SYS_BIND, (long)sockfd, (long)addr, (long)addrlen));
}

int listen(int sockfd, int backlog)
{
    return set_errno(syscall(SYS_LISTEN, (long)sockfd, (long)backlog));
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return set_errno(syscall(SYS_ACCEPT, (long)sockfd, (long)addr, (long)addrlen));
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    return set_errno(syscall(SYS_ACCEPT4, (long)sockfd, (long)addr, (long)addrlen, (long)flags));
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return set_errno(syscall(SYS_CONNECT, (long)sockfd, (long)addr, (long)addrlen));
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return set_errno_ssz(syscall(SYS_SEND, (long)sockfd, (long)buf, (long)len, (long)flags));
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return set_errno_ssz(syscall(SYS_RECV, (long)sockfd, (long)buf, (long)len, (long)flags));
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return set_errno_ssz(syscall(SYS_SENDTO, (long)sockfd, (long)buf, (long)len,
                                 (long)flags, (long)dest_addr, (long)addrlen));
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    return set_errno_ssz(syscall(SYS_RECVFROM, (long)sockfd, (long)buf, (long)len,
                                 (long)flags, (long)src_addr, (long)addrlen));
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return set_errno_ssz(syscall(SYS_SENDMSG, (long)sockfd, (long)msg, (long)flags));
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return set_errno_ssz(syscall(SYS_RECVMSG, (long)sockfd, (long)msg, (long)flags));
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    return set_errno(syscall(SYS_GETSOCKOPT, (long)sockfd, (long)level, (long)optname,
                             (long)optval, (long)optlen));
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    return set_errno(syscall(SYS_SETSOCKOPT, (long)sockfd, (long)level, (long)optname,
                             (long)optval, (long)optlen));
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return set_errno(syscall(SYS_GETSOCKNAME, (long)sockfd, (long)addr, (long)addrlen));
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return set_errno(syscall(SYS_GETPEERNAME, (long)sockfd, (long)addr, (long)addrlen));
}

int shutdown(int sockfd, int how)
{
    return set_errno(syscall(SYS_SHUTDOWN, (long)sockfd, (long)how));
}

/* sockatmark — no out-of-band data on AF_UNIX, always 0. */
int sockatmark(int sockfd)
{
    (void)sockfd;
    return 0;
}

/* ------------------------------------------------------------------
 * getaddrinfo / getnameinfo
 *
 * Only AF_UNIX is supported.  For AF_INET/AF_INET6 we return EAI_FAMILY
 * since there's no resolver and no in-kernel IP stack.  AF_UNIX requests
 * pack the service path verbatim into a sockaddr_un.
 * ------------------------------------------------------------------ */

/* AF_INET path — used by every internet daemon that doesn't go
 * straight to socket()+bind() with a numeric port (inetutils inetd,
 * sshd, postfix, etc.).  Without this they all fall over with
 * EAI_FAMILY.  Caller fills in hints.ai_family = AF_INET, optionally
 * AI_PASSIVE for wildcard binds, optionally AI_NUMERICHOST/SERV. */
static int getaddrinfo_inet(const char *node, const char *service,
                            const struct addrinfo *hints,
                            struct addrinfo **res)
{
    int socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    int flags    = hints ? hints->ai_flags : 0;

    struct sockaddr_in *sin = (struct sockaddr_in *)malloc(sizeof(*sin));
    if (!sin) return EAI_MEMORY;
    struct addrinfo *ai = (struct addrinfo *)malloc(sizeof(*ai));
    if (!ai) { free(sin); return EAI_MEMORY; }
    memset(sin, 0, sizeof(*sin));
    memset(ai, 0, sizeof(*ai));

    sin->sin_family = AF_INET;

    /* Address: NULL + AI_PASSIVE → wildcard; NULL + no flag → 127.0.0.1;
     * dotted-quad → parse with inet_pton; anything else → gethostbyname. */
    if (!node) {
        sin->sin_addr.s_addr = (flags & AI_PASSIVE) ? 0 : htonl(0x7f000001);
    } else {
        if (inet_pton(AF_INET, node, &sin->sin_addr) != 1) {
            if (flags & AI_NUMERICHOST) {
                free(ai); free(sin); return EAI_NONAME;
            }
            struct hostent *h = gethostbyname(node);
            if (!h || h->h_addrtype != AF_INET || !h->h_addr_list[0]) {
                free(ai); free(sin); return EAI_NONAME;
            }
            memcpy(&sin->sin_addr, h->h_addr_list[0], 4);
        }
    }

    /* Port: numeric, or service name via getservbyname. */
    if (!service) {
        sin->sin_port = 0;
    } else {
        char *end;
        unsigned long p = strtoul(service, &end, 10);
        if (end != service && *end == '\0') {
            sin->sin_port = htons((uint16_t)p);
        } else if (flags & AI_NUMERICSERV) {
            free(ai); free(sin); return EAI_NONAME;
        } else {
            const char *proto = (socktype == SOCK_DGRAM) ? "udp" : "tcp";
            struct servent *sv = getservbyname(service, proto);
            if (!sv) { free(ai); free(sin); return EAI_SERVICE; }
            sin->sin_port = sv->s_port;   /* already in net order */
        }
    }

    ai->ai_family    = AF_INET;
    ai->ai_socktype  = socktype;
    ai->ai_protocol  = hints ? hints->ai_protocol : 0;
    ai->ai_addrlen   = sizeof(*sin);
    ai->ai_addr      = (struct sockaddr *)sin;
    ai->ai_canonname = NULL;
    ai->ai_next      = NULL;
    *res = ai;
    return 0;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    if (!res) return EAI_FAIL;
    int family = hints ? hints->ai_family : AF_UNSPEC;

    /* AF_INET6 is not supported — substrate has no v6 bind/connect.
     * Return EAI_ADDRFAMILY rather than handing back a mistyped
     * AF_INET result, so AF_UNSPEC callers iterate to the v4 entry
     * and AF_INET6-pinned callers get a clean failure. */
    if (family == AF_INET6)
        return EAI_ADDRFAMILY;

    /* AF_INET → real internet resolver. */
    if (family == AF_INET)
        return getaddrinfo_inet(node, service, hints, res);

    /* AF_UNSPEC with a non-NULL node is unambiguously an internet
     * query — AF_UNIX has no notion of a "node" (host).  Resolve it
     * as INET and return whatever that produces, success OR failure.
     *
     * The previous code fell through to the AF_UNIX branch on a
     * failed INET lookup, where `path = service` built a bogus
     * sockaddr_un with sun_path set to the *port string* ("22").
     * ssh then "connected" to a garbage AF_UNIX path and printed
     * "connect to host 22 port".  A failed hostname lookup must
     * surface as EAI_NONAME, not a fake unix socket. */
    if (family == AF_UNSPEC && node)
        return getaddrinfo_inet(node, service, hints, res);

    /* AF_UNSPEC, node == NULL: a service-only query.  A service that
     * isn't a filesystem path is a passive internet bind; try INET
     * first and only fall through to AF_UNIX if that path is taken
     * for a genuinely path-shaped service. */
    if (family == AF_UNSPEC && service && service[0] != '/') {
        if (getaddrinfo_inet(node, service, hints, res) == 0) return 0;
    }

    if (family != AF_UNIX && family != AF_LOCAL && family != AF_UNSPEC)
        return EAI_FAMILY;
    /* AF_UNSPEC without a node string still falls through to AF_UNIX. */
    const char *path = service ? service : node;
    if (!path) return EAI_NONAME;
    if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path))
        return EAI_NONAME;

    struct addrinfo *ai = (struct addrinfo *)malloc(sizeof(*ai));
    if (!ai) return EAI_MEMORY;
    struct sockaddr_un *sun = (struct sockaddr_un *)malloc(sizeof(*sun));
    if (!sun) { free(ai); return EAI_MEMORY; }
    memset(sun, 0, sizeof(*sun));
    sun->sun_family = AF_UNIX;
    strncpy(sun->sun_path, path, sizeof(sun->sun_path) - 1);

    memset(ai, 0, sizeof(*ai));
    ai->ai_family   = AF_UNIX;
    ai->ai_socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    ai->ai_protocol = 0;
    ai->ai_addrlen  = sizeof(*sun);
    ai->ai_addr     = (struct sockaddr *)sun;
    ai->ai_canonname = NULL;
    ai->ai_next     = NULL;
    *res = ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res) {
        struct addrinfo *next = res->ai_next;
        free(res->ai_addr);
        free(res->ai_canonname);
        free(res);
        res = next;
    }
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    (void)flags;
    if (!sa || salen < (socklen_t)sizeof(sa_family_t)) return EAI_FAIL;
    if (sa->sa_family != AF_UNIX && sa->sa_family != AF_LOCAL)
        return EAI_FAMILY;
    const struct sockaddr_un *sun = (const struct sockaddr_un *)sa;
    if (host && hostlen > 0) {
        strncpy(host, sun->sun_path, hostlen - 1);
        host[hostlen - 1] = '\0';
    }
    if (serv && servlen > 0) serv[0] = '\0';
    return 0;
}

const char *gai_strerror(int errcode)
{
    switch (errcode) {
    case 0:                return "no error";
    case EAI_AGAIN:        return "temporary resolution failure";
    case EAI_BADFLAGS:     return "invalid flags";
    case EAI_FAIL:         return "non-recoverable resolution failure";
    case EAI_FAMILY:       return "address family not supported";
    case EAI_ADDRFAMILY:   return "address family for node not supported";
    case EAI_NODATA:       return "no address associated with node";
    case EAI_OVERFLOW:     return "argument buffer overflow";
    case EAI_MEMORY:       return "out of memory";
    case EAI_NONAME:       return "node or service not known";
    case EAI_SERVICE:      return "service not supported for socket type";
    case EAI_SOCKTYPE:     return "socket type not supported";
    case EAI_SYSTEM:       return "system error";
    default:               return "unknown getaddrinfo error";
    }
}

/* ------------------------------------------------------------------
 * inet_pton / inet_ntop — real implementations for AF_INET (IPv4).
 * AF_INET6 returns 0 / NULL since we have no in-kernel v6.  These
 * functions don't touch the kernel.
 * ------------------------------------------------------------------ */

static int parse_ipv4(const char *src, uint8_t out[4])
{
    int dots = 0;
    int byte = 0;
    int digits = 0;
    for (const char *p = src; ; p++) {
        if (*p >= '0' && *p <= '9') {
            if (digits == 3) return 0;          /* >3 chars per octet */
            byte = byte * 10 + (*p - '0');
            if (byte > 255) return 0;
            digits++;
        } else if (*p == '.' || *p == '\0') {
            if (!digits) return 0;
            out[dots] = (uint8_t)byte;
            dots++;
            if (*p == '\0') return dots == 4 ? 1 : 0;
            if (dots == 4) return 0;             /* extra dot */
            byte = 0;
            digits = 0;
        } else {
            return 0;
        }
    }
}

/* RFC 3986 §3.2.2 + RFC 4291 §2.2 IPv6 text form, supporting `::`
 * abbreviation (anywhere) and embedded IPv4 in the last 32 bits. */
static int parse_ipv6(const char *src, uint8_t out[16])
{
    uint16_t groups[8] = { 0 };
    int gi = 0;
    int seen_double = -1;   /* index where :: was */
    const char *p = src;

    /* Leading "::" */
    if (p[0] == ':' && p[1] == ':') {
        seen_double = 0;
        p += 2;
        if (*p == '\0') {
            memset(out, 0, 16);
            return 1;
        }
    }

    while (*p && gi < 8) {
        /* Parse one hex group. */
        unsigned v = 0;
        int n = 0;
        while (*p) {
            char c = *p;
            unsigned d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            v = (v << 4) | d;
            n++;
            p++;
            if (n > 4) return 0;
        }
        /* Embedded IPv4 in last group? */
        if (*p == '.') {
            /* Rewind to start of this group and parse as v4. */
            const char *q = p;
            while (q > src && *(q - 1) != ':') q--;
            uint8_t v4[4];
            if (!parse_ipv4(q, v4)) return 0;
            if (gi > 6) return 0;
            groups[gi++] = ((uint16_t)v4[0] << 8) | v4[1];
            groups[gi++] = ((uint16_t)v4[2] << 8) | v4[3];
            while (*p) p++;   /* consume rest */
            break;
        }
        if (n == 0) return 0;
        groups[gi++] = (uint16_t)v;
        if (*p == ':') {
            p++;
            if (*p == ':') {
                if (seen_double >= 0) return 0;  /* multiple :: */
                seen_double = gi;
                p++;
                if (*p == '\0') break;
            }
        } else if (*p == '\0') {
            break;
        } else {
            return 0;
        }
    }

    if (seen_double < 0) {
        if (gi != 8) return 0;
    } else {
        /* Expand zeros. */
        int zeros = 8 - gi;
        if (zeros < 0) return 0;
        for (int i = gi - 1; i >= seen_double; i--)
            groups[i + zeros] = groups[i];
        for (int i = seen_double; i < seen_double + zeros; i++)
            groups[i] = 0;
    }

    for (int i = 0; i < 8; i++) {
        out[i * 2]     = (uint8_t)(groups[i] >> 8);
        out[i * 2 + 1] = (uint8_t)(groups[i] & 0xFF);
    }
    return 1;
}

int inet_pton(int af, const char *src, void *dst)
{
    if (!src || !dst) { errno = EINVAL; return -1; }
    if (af == AF_INET) {
        uint8_t b[4];
        if (!parse_ipv4(src, b)) return 0;
        memcpy(dst, b, 4);
        return 1;
    }
    if (af == AF_INET6) {
        uint8_t b[16];
        if (!parse_ipv6(src, b)) return 0;
        memcpy(dst, b, 16);
        return 1;
    }
    errno = EAFNOSUPPORT;
    return -1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (!src || !dst) { errno = EINVAL; return NULL; }
    if (af == AF_INET) {
        const uint8_t *b = (const uint8_t *)src;
        char buf[16];
        int n = 0;
        for (int i = 0; i < 4; i++) {
            unsigned v = b[i];
            char tmp[4]; int t = 0;
            do { tmp[t++] = '0' + (v % 10); v /= 10; } while (v);
            while (t > 0) buf[n++] = tmp[--t];
            if (i < 3) buf[n++] = '.';
        }
        buf[n] = '\0';
        if ((socklen_t)(n + 1) > size) { errno = ENOSPC; return NULL; }
        memcpy(dst, buf, n + 1);
        return dst;
    }
    if (af == AF_INET6) {
        const uint8_t *b = (const uint8_t *)src;
        /* Find the longest run of zero groups (≥2) for "::" compression. */
        uint16_t g[8];
        for (int i = 0; i < 8; i++) g[i] = ((uint16_t)b[i*2] << 8) | b[i*2+1];
        int best_i = -1, best_n = 0;
        for (int i = 0; i < 8;) {
            if (g[i] != 0) { i++; continue; }
            int j = i;
            while (j < 8 && g[j] == 0) j++;
            int n = j - i;
            if (n >= 2 && n > best_n) { best_i = i; best_n = n; }
            i = j;
        }
        char buf[48];
        int n = 0;
        for (int i = 0; i < 8;) {
            if (i == best_i) {
                buf[n++] = ':';
                if (i == 0) buf[n++] = ':';
                i += best_n;
                if (i == 8) buf[n++] = ':';
                continue;
            }
            unsigned v = g[i];
            const char hex[] = "0123456789abcdef";
            char tmp[5]; int t = 0;
            do { tmp[t++] = hex[v & 0xF]; v >>= 4; } while (v);
            while (t > 0) buf[n++] = tmp[--t];
            i++;
            if (i < 8 && i != best_i) buf[n++] = ':';
        }
        buf[n] = '\0';
        if ((socklen_t)(n + 1) > size) { errno = ENOSPC; return NULL; }
        memcpy(dst, buf, n + 1);
        return dst;
    }
    errno = EAFNOSUPPORT;
    return NULL;
}

/* Network byte-order helpers.  i386 is little-endian; net order is
 * big-endian.  These compile to a single bswap on modern GCC. */
uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
uint16_t ntohs(uint16_t x) { return __builtin_bswap16(x); }
uint32_t ntohl(uint32_t x) { return __builtin_bswap32(x); }
