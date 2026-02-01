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

typedef uint16_t sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

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
