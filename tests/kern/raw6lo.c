/*
 * raw6lo -- an ICMPv6 raw socket can ping ::1; v6 stream and datagram
 * sockets are still refused.
 *
 * AF_INET6 sockets are refused at socket() because bind() and connect()
 * have no IPv6 support, so getaddrinfo callers (ssh) move straight on to
 * the IPv4 address.  That rule caught raw sockets too, though the raw
 * path does carry IPv6, and ping6-style callers lost ICMPv6 altogether.
 * Sends an echo request to ::1 on a raw ICMPv6 socket and expects the
 * echo reply; then checks AF_INET6 SOCK_STREAM and SOCK_DGRAM still fail
 * with EAFNOSUPPORT.  Substrate only (Linux has real v6 sockets); run as
 * root.
 */
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6 58
#endif

int main(void)
{
    int fails = 0;

    alarm(10);
    int fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    if (fd < 0) {
        printf("raw6lo: socket(AF_INET6, SOCK_RAW) failed (errno %d)\n",
               errno);
        fails++;
    } else {
        uint8_t req[8] = { 128, 0, 0, 0, 0x12, 0x34, 0x00, 0x01 };
        struct sockaddr_in6 dst;
        memset(&dst, 0, sizeof dst);
        dst.sin6_family = AF_INET6;
        dst.sin6_addr.s6_addr[15] = 1;          /* ::1 */
        if (sendto(fd, req, sizeof req, 0, (struct sockaddr *)&dst,
                   sizeof dst) != (ssize_t)sizeof req) {
            printf("raw6lo: sendto ::1 failed (errno %d)\n", errno);
            fails++;
        } else {
            int replied = 0;
            uint8_t buf[256];
            /* Loopback shows our own request first. */
            for (int i = 0; i < 4 && !replied; i++) {
                ssize_t n = recv(fd, buf, sizeof buf, 0);
                if (n < 8)
                    break;
                replied = buf[0] == 129;
            }
            if (!replied) {
                printf("raw6lo: no echo reply from ::1\n");
                fails++;
            }
        }
        close(fd);
    }

    static const int types[] = { SOCK_STREAM, SOCK_DGRAM };
    for (int i = 0; i < 2; i++) {
        errno = 0;
        int s = socket(AF_INET6, types[i], 0);
        if (s >= 0 || errno != EAFNOSUPPORT) {
            printf("raw6lo: socket(AF_INET6, %s) was not refused with "
                   "EAFNOSUPPORT\n", i ? "SOCK_DGRAM" : "SOCK_STREAM");
            fails++;
            if (s >= 0)
                close(s);
        }
    }
    printf("raw6lo: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
