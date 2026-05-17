/*
 * <netinet/in_systm.h> — 4.4BSD network-byte-order integer typedefs.
 *
 * Historical BSD header used by old IP/ICMP/UDP/TCP code to denote
 * fields that were already in network byte order, distinct from host
 * order.  Substrate's userland networking (af_inet, libicmp callers
 * inside inetutils) expects this header to exist.
 */
#ifndef _NETINET_IN_SYSTM_H
#define _NETINET_IN_SYSTM_H

#include <sys/types.h>

typedef uint16_t  n_short;      /* short as received from the net */
typedef uint32_t  n_long;       /* long as received from the net */
typedef uint32_t  n_time;       /* ms since 00:00 GMT, byte-rev */

#endif /* _NETINET_IN_SYSTM_H */
