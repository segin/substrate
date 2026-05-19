/*
 * <netinet/tcp.h> — TCP-level socket options (POSIX.1-2017 + the
 * Linux/glibc extensions every real network app references).
 *
 * Use with setsockopt(fd, IPPROTO_TCP, TCP_*, &val, sizeof(val)).
 * Substrate's TCP stack honors TCP_NODELAY (disable Nagle) at the
 * PCB level today; the rest are accepted with -ENOPROTOOPT so apps
 * detecting feature availability through setsockopt() don't crash.
 */
#ifndef _NETINET_TCP_H
#define _NETINET_TCP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_NODELAY              1   /* Disable Nagle algorithm */
#define TCP_MAXSEG               2   /* Maximum segment size */
#define TCP_CORK                 3   /* Coalesce writes */
#define TCP_KEEPIDLE             4   /* Idle time before keepalive */
#define TCP_KEEPINTVL            5   /* Interval between keepalives */
#define TCP_KEEPCNT              6   /* Keepalive count before drop */
#define TCP_SYNCNT               7   /* SYN retries */
#define TCP_LINGER2              8   /* FIN_WAIT_2 lifetime */
#define TCP_DEFER_ACCEPT         9   /* Wake on data, not on SYN */
#define TCP_WINDOW_CLAMP        10   /* Cap advertised window */
#define TCP_INFO                11   /* Return tcp_info struct */
#define TCP_QUICKACK            12   /* Force ACK immediately */
#define TCP_CONGESTION          13   /* Congestion-control alg name */
#define TCP_MD5SIG              14   /* RFC 2385 TCP MD5 signature */
#define TCP_FASTOPEN            23   /* TFO listen queue */
#define TCP_USER_TIMEOUT        18   /* RFC 5482 user timeout */
#define TCP_NOTSENT_LOWAT       25   /* Wake when buf has space */

/* TCP states reported by TCP_INFO.tcpi_state */
enum {
    TCP_ESTABLISHED  = 1,
    TCP_SYN_SENT,
    TCP_SYN_RECV,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_TIME_WAIT,
    TCP_CLOSE,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_LISTEN,
    TCP_CLOSING,
    TCP_NEW_SYN_RECV,
};

#ifdef __cplusplus
}
#endif

#endif /* _NETINET_TCP_H */
