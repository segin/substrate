/*
 * sys/socket.h - Socket definitions
 *
 * This header provides socket types and constants similar to POSIX
 * to allow compatibility layers (and native networking later) to build.
 */

#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <sys/types.h>

/* Address families (subset required for linux_user.c) */
#define AF_UNSPEC   0
#define AF_LOCAL    1
#define AF_UNIX     AF_LOCAL
#define AF_INET     2
#define AF_INET6    10
#define AF_NETLINK  16
#define AF_PACKET   17
#define AF_LINK     18

/* Socket types */
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

/* socket()/socketpair()/accept4() type flag bits — OR'd into the
 * `type` argument.  Linux values; identical to O_NONBLOCK / O_CLOEXEC
 * so the fd-side application is a straight copy. */
#define SOCK_NONBLOCK 0x0800
#define SOCK_CLOEXEC  0x80000
#define SOCK_TYPE_MASK 0xFF       /* mask to recover the base type */

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[108];
};

#ifndef SOCK_SEQPACKET
#define SOCK_SEQPACKET 5
#endif

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define SOL_SOCKET 1

/* Ancillary message types + recvmsg/sendmsg flags — kernel subset
 * for the af_unix SCM_RIGHTS path.  Values match the user-facing
 * include/sys/socket.h; keep both in sync. */
#define SCM_RIGHTS       1
#define SCM_CREDENTIALS  2
#define MSG_PEEK         0x0002
#define MSG_CTRUNC       0x0008
#define MSG_TRUNC        0x0020
#define MSG_DONTWAIT     0x0040
#define MSG_NOSIGNAL     0x4000

#define SO_REUSEADDR 2
#define SO_TYPE      3
#define SO_ERROR     4
#define SO_BROADCAST 6
#define SO_SNDBUF    7
#define SO_RCVBUF    8
#define SO_KEEPALIVE 9

struct sockaddr_storage {
    sa_family_t ss_family;
    char        __ss_pad1[6];
    int64_t     __ss_align;
    char        __ss_pad2[112];
};

/* 
 * Structure used by kernel networking subsystem (referenced by linux_user.c) 
 */
struct msghdr {
    void         *msg_name;
    int           msg_namelen;
    void         *msg_iov;
    int           msg_iovlen;
    void         *msg_control;
    int           msg_controllen;
    int           msg_flags;
};

#endif /* _SYS_SOCKET_H */
