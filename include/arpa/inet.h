/*
 * <arpa/inet.h> — IPv4 address conversion helpers (stub).
 */
#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <netinet/in.h>

uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);
in_addr_t inet_addr(const char *cp);
char *inet_ntoa(struct in_addr in);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#ifdef __cplusplus
}
#endif
#endif
