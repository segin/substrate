#!/usr/bin/env python3
"""
The link-layer destination of an IPv4 datagram must agree with its IP
destination (RFC 1122 3.3.6): a datagram that arrived in a link-layer
broadcast or multicast frame is discarded unless its IP destination is a
broadcast or multicast address, and a frame addressed to another host's MAC
is not ours at all (the NIC may be promiscuous, as qemu's virtio-net is).

    link-dest   a unicast-IP datagram to the guest sent in a broadcast frame,
                in a 01:00:5e multicast frame and to a foreign unicast MAC is
                not delivered; the same datagram to the guest's MAC is.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_linkaddr.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, GUEST_IP, GUEST_MAC  # noqa: E402

PORT = 7410
BAD = ((40001, b'\xff' * 6, 'link broadcast'),
       (40002, bytes.fromhex('01005e000001'), 'link multicast'),
       (40003, bytes.fromhex('525400999999'), 'foreign MAC'))
GOOD = 40009


def case_link_dest():
    # recvsum reports the first datagram the socket gets
    with Wire.boot('udpany %d recvsum:100 sleep:60' % PORT) as w:
        if not w.wait_serial('guest: udpany', 90):
            return 'guest never bound', w
        w.pump(0.5)
        for sport, mac, _ in BAD:
            w.send_udp(sport, PORT, b'wrong-link', dst=GUEST_IP, eth_dst=mac)
            w.pump(0.3)
        w.send_udp(GOOD, PORT, b'right-link', dst=GUEST_IP, eth_dst=GUEST_MAC)
        if not w.wait_serial('guest: recvsum', 10):
            return 'the correctly addressed datagram was not delivered', w
        w.pump(0.3)
        got = [l for l in w.serial().splitlines() if 'guest: recvsum' in l][0]
        for sport, _, what in BAD:
            if 'sport=%d' % sport in got:
                return 'a unicast datagram in a %s frame was delivered: %s' % \
                    (what, got), w
        if 'sport=%d' % GOOD not in got:
            return 'unexpected delivery: %s' % got, w
        return None, w


CASES = (('link-dest', case_link_dest),)


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
