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
    zero-dest      with eth0 unaddressed, a datagram to 0.0.0.0 is not
                   delivered to a wildcard socket (RFC 791 3.2: 0/8 is
                   never a destination); one to 255.255.255.255 still is.
    mcast-zero-src with eth0 unaddressed, a multicast send is refused rather
                   than sent from 0.0.0.0 (only the limited broadcast, for
                   DHCP, may leave an unaddressed interface).

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


def case_zero_dest():
    # recvsum reports the first datagram the wildcard socket gets
    with Wire.boot('udpany 7400 ifaddr0 recvsum:100 sleep:60') as w:
        if not w.wait_serial('guest: ifaddr0 ok', 90):
            return 'could not clear the address', w
        w.pump(0.5)
        w.send_udp(40001, 7400, b'to-zero', src='10.0.2.2', dst='0.0.0.0')
        w.pump(0.5)
        w.send_udp(40002, 7400, b'to-bcast', src='10.0.2.2',
                   dst='255.255.255.255', eth_dst=b'\xff' * 6)
        if not w.wait_serial('guest: recvsum', 10):
            return 'the limited broadcast was not delivered either', w
        w.pump(0.3)
        got = [l for l in w.serial().splitlines() if 'guest: recvsum' in l][0]
        if 'sport=40001' in got:
            return 'a datagram to 0.0.0.0 was delivered: %s' % got, w
        if 'sport=40002' not in got:
            return 'unexpected delivery: %s' % got, w
        return None, w


def case_mcast_zero_src():
    group = '224.0.0.9'
    with Wire.boot('udpany 7403 ifaddr0 sendto:%s:9:x sleep:1 sendto:%s:9:y '
                   'sleep:60' % (group, group)) as w:
        if not w.wait_serial('guest: slept', 90):
            return 'guest never got to its sends', w
        w.wait_serial('guest: sendto', 5)
        w.pump(1.0)
        for mac, etype, fr in w.frames:
            if etype == 0x0800 and socket.inet_ntoa(fr[30:34]) == group:
                return 'a multicast datagram left from %s' % \
                    socket.inet_ntoa(fr[26:30]), w
        s = [l for l in w.serial().splitlines() if l.startswith('guest: sendto')]
        if len(s) != 2 or any(' ok ' in l for l in s):
            return 'sends: %r, want both refused' % s, w
        return None, w


CASES = (('dhcp-discover', case_dhcp_discover), ('zero-dest', case_zero_dest),
         ('mcast-zero-src', case_mcast_zero_src))


def main():
    failed = 0
    only = sys.argv[1:]
    for name, fn in CASES:
        if only and name not in only:
            continue
        err, w = fn()
        if err:
            failed += 1
            print('FAIL  %s: %s' % (name, err))
            print(w.dump()[-2000:])
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
