#!/usr/bin/env python3
"""
ICMP Protocol Unreachable (RFC 791 3.3 Interfaces; RFC 1122 3.2.2.1).  A
datagram for a protocol nothing on the host handles used to vanish
without a word.

    unknown     a datagram for protocol 253 (RFC 3692 experimental) draws
                Destination Unreachable, code 2, quoting it; the same
                datagram sent to the limited broadcast draws nothing (RFC
                1122 3.2.2).

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_proto.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, GUEST_IP  # noqa: E402

PROTO = 253


def unreach(w):
    """(code, quoted protocol, quoted destination) of each Destination
    Unreachable the guest sent."""
    out = []
    for proto, src, dst, ip in w.ip_rx:
        ihl = (ip[0] & 0xF) * 4
        if proto != 1 or len(ip) < ihl + 8 + 20 or ip[ihl] != 3:
            continue
        q = ip[ihl + 8:]
        out.append((ip[ihl + 1], q[9], bytes(q[16:20])))
    return out


def case_unknown():
    # The guest talks to the host first so the error, sent from the receive
    # path, finds an ARP entry.
    with Wire.boot('udpany 7417 sendto:10.0.2.2:9:arp sleep:60') as w:
        if not w.wait_serial('guest: sendto', 90):
            return 'guest never sent its ARP-priming datagram', w
        w.pump(0.5)
        w.send_ip(PROTO, b'experimental', dst='255.255.255.255',
                  eth_dst=b'\xff' * 6)
        w.pump(1.0)
        if any(u[1] == PROTO for u in unreach(w)):
            return 'a broadcast datagram drew an ICMP error', w
        w.send_ip(PROTO, b'experimental', dst=GUEST_IP)
        w.pump(1.5)
        hits = [u for u in unreach(w) if u[1] == PROTO]
        if not hits:
            return 'no Destination Unreachable for protocol %d' % PROTO, w
        if hits[0][0] != 2:
            return 'Destination Unreachable code %d, want 2 (protocol)' % \
                hits[0][0], w
        return None, w


CASES = (('unknown', case_unknown),)


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
