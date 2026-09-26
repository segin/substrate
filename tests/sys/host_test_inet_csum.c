/*
 * host_test_inet_csum.c — the Internet checksum stores in network order on
 * either byte order.
 *
 * inet_csum() and friends compute the checksum as a host-order number and
 * must hand back the value that, stored in a header field, puts the
 * checksum on the wire big-end first.  That needs a swap on a little-endian
 * host and none on a big-endian one; the swap used to be unconditional.
 *
 * Built twice (tests/sys/Makefile): as-is, and with __BYTE_ORDER__ forced
 * to big-endian so inet_csum.c takes its big-endian path.  On the
 * little-endian build the stored octets are checked; on the "big-endian"
 * one the numeric value is, since that is what a real big-endian host
 * would store.
 */
#include <stdio.h>
#include <string.h>

#include <net/inet_csum.h>

static int failures;

/* What the stored checksum must read as on the wire, from the value
 * returned: its octets on little-endian, its number on big-endian. */
static void expect_wire(const char *what, uint16_t got, uint16_t wire) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t b[2];
    memcpy(b, &got, 2);
    uint16_t seen = (uint16_t)(b[0] << 8 | b[1]);
#else
    uint16_t seen = got;
#endif
    if (seen != wire) {
        printf("FAIL %s: reads 0x%04x on the wire, want 0x%04x\n", what, seen,
               wire);
        failures++;
    } else {
        printf("ok   %s\n", what);
    }
}

int main(void) {
    /* An IPv4 header whose checksum is 0xb861. */
    uint8_t ip[20] = { 0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00,
                       0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x01,
                       0xc0, 0xa8, 0x00, 0xc7 };
    expect_wire("ipv4 header", inet_csum(ip, sizeof(ip)), 0xb861);

    /* RFC 1071 4.1: 00 01 f2 03 f4 f5 f6 f7 sums to 0xddf2. */
    uint8_t rfc[8] = { 0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7 };
    expect_wire("rfc 1071 example", inet_csum(rfc, sizeof(rfc)), 0x220d);

    /* An odd length pads with a zero octet: 01 02 03 -> 0x0102 + 0x0300. */
    uint8_t odd[3] = { 0x01, 0x02, 0x03 };
    expect_wire("odd length", inet_csum(odd, sizeof(odd)), (uint16_t)~0x0402);

    /* A header carrying its own checksum sums to zero either way. */
    ip[10] = 0xb8;
    ip[11] = 0x61;
    expect_wire("verify", inet_csum(ip, sizeof(ip)), 0x0000);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
