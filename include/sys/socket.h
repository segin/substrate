/*
 * <sys/socket.h> — BSD-style sockets, stub.
 *
 * Substrate doesn't yet implement BSD sockets in the kernel.  This
 * header exists so userspace TUs that #include it (gcc's libcody,
 * etc.) compile against a known shape; the calls themselves return
 * -1 with errno=ENOSYS at runtime.
 */
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char        __pad[126];
};

struct msghdr {
    void         *msg_name;
    socklen_t     msg_namelen;
    struct iovec *msg_iov;
    int           msg_iovlen;
    void         *msg_control;
    socklen_t     msg_controllen;
    int           msg_flags;
};

struct cmsghdr {
    socklen_t cmsg_len;
    int       cmsg_level;
    int       cmsg_type;
};

/* Ancillary-data (cmsghdr) types — SOL_SOCKET level. */
#define SCM_RIGHTS       1   /* fd passing (Linux/BSD-compatible) */
#define SCM_CREDENTIALS  2   /* peer pid/uid/gid */

/* CMSG accessor macros.  Align to size_t (BSD convention).  These
 * mirror glibc/BSD layouts so existing code (OpenSSH's monitor_fdpass,
 * sftp-server, ...) doesn't need #ifdef. */
#define CMSG_ALIGN(n)     (((n) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(len)   (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len)     (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg)   ((unsigned char *)((struct cmsghdr *)(cmsg) + 1))
#define CMSG_FIRSTHDR(mhdr) \
    ((mhdr)->msg_controllen >= sizeof(struct cmsghdr) \
        ? (struct cmsghdr *)(mhdr)->msg_control : (struct cmsghdr *)0)
#define CMSG_NXTHDR(mhdr, cmsg) \
    (((cmsg) == NULL || (cmsg)->cmsg_len < sizeof(struct cmsghdr)) \
        ? (struct cmsghdr *)0 \
        : ((char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) + sizeof(struct cmsghdr) \
            > (char *)(mhdr)->msg_control + (mhdr)->msg_controllen \
                ? (struct cmsghdr *)0 \
                : (struct cmsghdr *)((char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len))))

/* MSG_* flags — match Linux values so AF_UNIX cmsghdr buffers and
 * sendmsg/recvmsg flags pass through unchanged. */
#ifndef MSG_OOB
#define MSG_OOB         0x0001
#define MSG_PEEK        0x0002
#define MSG_DONTROUTE   0x0004
#define MSG_CTRUNC      0x0008
#define MSG_TRUNC       0x0020
#define MSG_DONTWAIT    0x0040
#define MSG_EOR         0x0080
#define MSG_WAITALL     0x0100
#define MSG_NOSIGNAL    0x4000
#define MSG_CMSG_CLOEXEC 0x40000000
#endif

#define AF_UNSPEC      0
#define AF_UNIX        1
#define AF_LOCAL       AF_UNIX
#define AF_INET        2
#define AF_INET6      10

#define PF_UNSPEC      AF_UNSPEC
#define PF_UNIX        AF_UNIX
#define PF_LOCAL       AF_LOCAL
#define PF_INET        AF_INET
#define PF_INET6       AF_INET6

#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_RDM       4
#define SOCK_SEQPACKET 5

/* socket()/socketpair()/accept4() type flag bits — OR'd into the
 * `type` argument.  Linux values, identical to O_NONBLOCK /
 * O_CLOEXEC.  OpenSSH and most modern network code always set
 * SOCK_CLOEXEC. */
#define SOCK_NONBLOCK  0x0800
#define SOCK_CLOEXEC   0x80000

#define SOL_SOCKET     1
#define SO_DEBUG       1     /* Linux value */
#define SO_REUSEADDR   2
#define SO_TYPE        3
#define SO_ERROR       4
#define SO_DONTROUTE   5
#define SO_BROADCAST   6
#define SO_SNDBUF      7
#define SO_RCVBUF      8
#define SO_KEEPALIVE   9
#define SO_OOBINLINE   10
#define SO_NO_CHECK    11
#define SO_PRIORITY    12
#define SO_LINGER      13
#define SO_BSDCOMPAT   14
#define SO_REUSEPORT   15
#define SO_PASSCRED    16
#define SO_PEERCRED    17

/* Userspace struct ucred returned by SO_PEERCRED on AF_UNIX.  Wire-
 * compatible with the Linux ABI; substrate's AF_UNIX layer fills it
 * with the peer's pid/uid/gid at connect() time.  Not to be confused
 * with the kernel-internal struct ucred in <sys/ucred.h>. */
struct ucred {
    pid_t pid;
    uid_t uid;
    gid_t gid;
};

#define SO_RCVLOWAT    18
#define SO_SNDLOWAT    19
#define SO_RCVTIMEO    20
#define SO_SNDTIMEO    21
#define SO_ACCEPTCONN  30

/* SO_LINGER companion struct.  Used by setsockopt() to control how
 * close() handles in-flight data: l_onoff=0 closes immediately and
 * the kernel drains in the background; l_onoff!=0 blocks close()
 * for up to l_linger seconds waiting for queued data to flush. */
struct linger {
    int l_onoff;
    int l_linger;
};

/* MSG_* flags are defined once, near struct cmsghdr above, under an
 * #ifndef MSG_OOB guard. */

#define SHUT_RD        0
#define SHUT_WR        1
#define SHUT_RDWR      2

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int shutdown(int sockfd, int how);
int socketpair(int domain, int type, int protocol, int sv[2]);

#ifdef __cplusplus
}
#endif
#endif
