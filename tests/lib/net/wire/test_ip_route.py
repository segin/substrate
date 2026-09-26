#!/usr/bin/env python3
"""
IPv4 route selection when an interface's netmask is 0.

    zero-mask   eth0 keeps its address and gateway but its netmask is set to
                0.0.0.0.  An off-link datagram must still leave through the
                gateway -- a zero mask used to make the on-link test match
                every destination, so the guest ARPed for the far host
                itself and the send failed.
    addr-only   with the mask cleared, setting the address alone installs
                the address's classful mask (10.x: /8), so 10.9.9.9 is
                on-link and ARPed for directly, not sent to the gateway.
    this-net    0/8 is never a destination (RFC 791 3.2): sendto 0.1.2.3
                fails EINVAL and nothing leaves (it used to go to the
                gateway), while 0.0.0.0 names the local host, as it does
                for TCP.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_route.py [case...]
"""
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, PEER_MAC  # noqa: E402

OFFLINK = '198.51.100.1'


def arp_requests_for(w, ip):
    return [fr for dst, et, fr in w.frames
            if et == 0x0806 and len(fr) >= 42 and fr[20:22] == b'\x00\x01'
            and socket.inet_ntoa(fr[38:42]) == ip]


def sent_via_gateway(w, ip):
    return [fr for dst, et, fr in w.frames
            if et == 0x0800 and dst == PEER_MAC and
            socket.inet_ntoa(fr[30:34]) == ip]


def case_zero_mask():
    # two sends a second apart: the first can be lost while the guest
    # resolves the gateway's MAC
    with Wire.boot('udpany 7401 netmask:0.0.0.0 sendto:%s:9:x sleep:1 '
                   'sendto:%s:9:y sleep:60' % (OFFLINK, OFFLINK)) as w:
        if not w.wait_serial('guest: slept', 90):
            return 'guest never got to its sends', w
        w.wait_serial('guest: sendto', 5)
        w.pump(2.5)
        if arp_requests_for(w, OFFLINK):
            return 'the guest ARPed for the off-link host itself', w
        if not sent_via_gateway(w, OFFLINK):
            return 'no datagram to %s went to the gateway' % OFFLINK, w
        return None, w


def case_addr_only():
    target = '10.9.9.9'
    with Wire.boot('udpany 7401 netmask:0.0.0.0 ifaddr:10.0.2.15 '
                   'sendto:%s:9:x sleep:60' % target) as w:
        if not w.wait_serial('guest: sendto', 90):
            return 'guest never sent', w
        w.pump(1.5)
        if sent_via_gateway(w, target):
            return '%s went to the gateway: no mask was installed' % target, w
        if not arp_requests_for(w, target):
            return 'no ARP for %s: not treated as on-link under /8' % target, w
        return None, w


def case_this_net():
    with Wire.boot('udpany 7414 sendto:0.1.2.3:9:far sendto:0.0.0.0:7414:self '
                   'recvsum:100 sleep:60') as w:
        if not w.wait_serial('guest: recvsum', 90):
            s = [l for l in w.serial().splitlines() if 'guest: sendto' in l]
            return 'the datagram to 0.0.0.0 never reached the guest ' \
                   '(%r)' % s, w
        w.pump(0.5)
        s = [l for l in w.serial().splitlines() if 'guest: sendto' in l]
        if not s or ' ok ' in s[0] or 'invalid' not in s[0].lower():
            return 'sendto 0.1.2.3: %r, want EINVAL' % s[:1], w
        for dst, et, fr in w.frames:
            if et == 0x0800 and socket.inet_ntoa(fr[30:34]).startswith('0.'):
                return 'a datagram to %s left the guest' % \
                    socket.inet_ntoa(fr[30:34]), w
        return None, w


CASES = (('zero-mask', case_zero_mask), ('addr-only', case_addr_only),
         ('this-net', case_this_net))


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
