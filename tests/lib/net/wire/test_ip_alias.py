#!/usr/bin/env python3
"""
Several IPv4 addresses on one interface (RFC 791 3.2).  An interface held
exactly one address; SIOCAIFADDR/SIOCDIFADDR now add and remove more, each
on its own subnet.

    alias   with 10.0.3.15/24 added beside eth0's 10.0.2.15/24:
            - a datagram to 10.0.3.15 is delivered;
            - an ARP request for 10.0.3.15 is answered, from 10.0.3.15;
            - a datagram the guest sends to 10.0.3.2 (on the alias's subnet)
              is preceded by an ARP request sent from 10.0.3.15, not from
              the primary address;
            - once removed with SIOCDIFADDR, a datagram to 10.0.3.15 is no
              longer delivered.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_alias.py
"""
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, GUEST_MAC, PEER_MAC  # noqa: E402

PORT = 7420
ALIAS = '10.0.3.15'


def arp_frames(w, op):
    """(sender IP, target IP) of the guest's ARP frames with opcode op."""
    out = []
    for dst, et, fr in w.frames:
        if et == 0x0806 and len(fr) >= 42 and fr[20:22] == bytes([0, op]):
            out.append((socket.inet_ntoa(fr[28:32]), socket.inet_ntoa(fr[38:42])))
    return out


def case_alias():
    with Wire.boot('udpany %d alias:%s/24 recvsum:100 sendto:10.0.3.2:9:x '
                   'unalias:%s recvsum:100 sleep:60' % (PORT, ALIAS, ALIAS)) as w:
        if not w.wait_serial('guest: alias', 90):
            return 'guest never added the alias', w
        if 'alias ok' not in w.serial():
            return 'SIOCAIFADDR failed: %r' % [
                l for l in w.serial().splitlines() if 'guest: alias' in l], w
        w.pump(0.5)
        # ARP for the alias
        w.send_arp(1, PEER_MAC, '10.0.3.2', b'\0' * 6, ALIAS,
                   eth_dst=b'\xff' * 6)
        w.pump(0.5)
        if (ALIAS, '10.0.3.2') not in arp_frames(w, 2):
            return 'no ARP reply for %s (replies: %r)' % (
                ALIAS, arp_frames(w, 2)), w
        # a datagram to the alias
        w.send_udp(40041, PORT, b'to-alias', src='10.0.3.2', dst=ALIAS,
                   eth_dst=GUEST_MAC)
        if not w.wait_serial('guest: recvsum', 10):
            return 'a datagram to %s was not delivered' % ALIAS, w
        # the guest's own send onto the alias subnet
        if not w.wait_serial('guest: unalias', 10):
            return 'guest never removed the alias', w
        reqs = [r for r in arp_frames(w, 1) if r[1] == '10.0.3.2']
        if not reqs:
            # 10.0.3.2 was learned from our ARP request above: the send
            # needed no ARP, so check the datagram's source instead.
            srcs = [socket.inet_ntoa(fr[26:30]) for dst, et, fr in w.frames
                    if et == 0x0800 and fr[30:34] == bytes([10, 0, 3, 2])]
            if not srcs:
                return 'the guest sent nothing to 10.0.3.2', w
            if srcs[0] != ALIAS:
                return 'datagram to 10.0.3.2 came from %s, want %s' % (
                    srcs[0], ALIAS), w
        elif reqs[0][0] != ALIAS:
            return 'ARP for 10.0.3.2 was sent from %s, want %s' % (
                reqs[0][0], ALIAS), w
        if 'unalias ok' not in w.serial():
            return 'SIOCDIFADDR failed', w
        w.pump(0.5)
        w.send_udp(40042, PORT, b'after-removal', src='10.0.3.2', dst=ALIAS,
                   eth_dst=GUEST_MAC)
        end = time.time() + 2
        while time.time() < end:
            w.pump(0.2)
            if w.serial().count('guest: recvsum') > 1:
                return 'a datagram to %s was delivered after it was ' \
                       'removed' % ALIAS, w
        return None, w


CASES = (('alias', case_alias),)


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
