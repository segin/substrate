#!/usr/bin/env python3
"""
UDP-IP-07 (docs/ip-audit-2026-09-22.md): an interface with no IPv4 address
must still be able to send the limited broadcast, from 0.0.0.0 -- which is
exactly what a DHCP client does before it has an address (RFC 2131 4.1).
route_for_v4() skipped every interface with ip4_addr == 0, so nothing could
be sent at all and DHCP could never bootstrap.

    dhcp-discover  eth0's address is cleared; a datagram to
                   255.255.255.255:67 leaves as an Ethernet broadcast, from
                   IP 0.0.0.0, with a UDP checksum that verifies over that
                   source.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_unconfigured.py
"""
import os
import socket
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, csum  # noqa: E402


def case_dhcp_discover():
    with Wire.boot('udpany 68 ifaddr0 sendto:255.255.255.255:67:DISCOVER sleep:60') as w:
        if not w.wait_serial('guest: sendto', 90):
            return 'guest never sent', w
        line = [l for l in w.serial().splitlines() if l.startswith('guest: ')]
        if not any('ifaddr0 ok' in l for l in line):
            return 'could not clear the address: %r' % line, w
        w.pump(0.5)
        for mac, etype, fr in w.frames:
            if etype != 0x0800 or fr[14 + 9] != 17:
                continue
            ip = fr[14:]
            ihl = (ip[0] & 0xF) * 4
            src, dst = socket.inet_ntoa(ip[12:16]), socket.inet_ntoa(ip[16:20])
            if dst != '255.255.255.255':
                continue
            if mac != b'\xff' * 6:
                return 'not an Ethernet broadcast: %s' % mac.hex(':'), w
            if src != '0.0.0.0':
                return 'source %s, want 0.0.0.0' % src, w
            udp = ip[ihl:struct.unpack('!H', ip[2:4])[0]]
            pseudo = ip[12:20] + struct.pack('!BBH', 0, 17, len(udp))
            if csum(pseudo + udp) != 0:
                return 'UDP checksum does not verify over source 0.0.0.0', w
            return None, w
        return 'no datagram to 255.255.255.255 left the guest (%s)' % \
            [l for l in line if 'sendto' in l], w


def main():
    err, w = case_dhcp_discover()
    if err:
        print('FAIL  dhcp-discover: %s' % err)
    else:
        print('ok    dhcp-discover')
    print('Result: %s' % ('FAILED' if err else 'PASSED'))
    return 1 if err else 0


if __name__ == '__main__':
    sys.exit(main())
