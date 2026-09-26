/*
 * net/inet_csum.h — the Internet checksum (RFC 1071).
 *
 * Kept apart from <net/inet.h> so it can be built and tested on its own:
 * it needs nothing but the standard integer types.  Each function returns
 * the checksum in network byte order, ready to store in a header field.
 */
#ifndef _NET_INET_CSUM_H
#define _NET_INET_CSUM_H

#include <stddef.h>
#include <stdint.h>

uint16_t inet_csum(const void *data, size_t len);
uint16_t inet_csum_pseudo4(uint32_t saddr, uint32_t daddr,
                           uint8_t proto, uint16_t len,
                           const void *data);
uint16_t inet_csum_pseudo6(const uint8_t saddr[16], const uint8_t daddr[16],
                           uint8_t proto, uint32_t len, const void *data);

#endif
