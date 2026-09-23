#!/usr/bin/env python3
"""
UDP-IP-04 / UDP-IP-05 (docs/ip-audit-2026-09-22.md): IPv4 broadcast on the
wire.  RFC 1122 3.3.6: a datagram to a broadcast address is sent as a
link-layer broadcast.

    limited     a datagram to 255.255.255.255 leaves as an Ethernet broadcast.
                route_for_v4() matched no subnet and sent it to the default
                gateway's MAC as a unicast.
    directed    a datagram to the subnet broadcast (10.0.2.255) leaves as an
                Ethernet broadcast, without an ARP request for 10.0.2.255.
                It was ARPed for like a host, and the frame dropped.
    hijack      a forged ARP reply "10.0.2.255 is-at <some host>" must not
                be learned: before, any on-link host could answer that ARP
                and receive all of our subnet broadcasts for five minutes.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_broadcast.py
"""
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, GUEST_IP  # noqa: E402

BCAST_MAC = b'\xff' * 6
ROGUE_MAC = bytes.fromhex('525400000066')


def udp_frames_to(w, ip_dst):
    out = []
    for mac, etype, fr in w.frames:
        if etype == 0x0800 and fr[14 + 9] == 17 and socket.inet_ntoa(fr[30:34]) == ip_dst:
            out.append(mac)
    return out


def arp_requests_for(w, ip):
    return [fr for mac, etype, fr in w.frames
            if etype == 0x0806 and fr[21] == 1 and socket.inet_ntoa(fr[38:42]) == ip]


def run(dst, prologue=None):
    w = Wire.boot('udp %s 9 sleep:2 write:hello sleep:60' % dst)
    if not w.wait_serial('guest: udp connected', 90):
        return w, None
    if prologue:
        prologue(w)
    w.wait_serial('guest: write', 10)
    w.pump(1.0)
    return w, udp_frames_to(w, dst)


def case_limited():
    w, macs = run('255.255.255.255')
    with w:
        if macs is None:
            return 'guest never came up', w
        if not macs:
            return 'no datagram to 255.255.255.255 left the guest', w
        if macs[0] != BCAST_MAC:
            return 'sent to %s, not ff:ff:ff:ff:ff:ff' % macs[0].hex(':'), w
        return None, w


def case_directed():
    w, macs = run('10.0.2.255')
    with w:
        if macs is None:
            return 'guest never came up', w
        if arp_requests_for(w, '10.0.2.255'):
            return 'the guest ARPed for the broadcast address', w
        if not macs:
            return 'no datagram to 10.0.2.255 left the guest', w
        if macs[0] != BCAST_MAC:
            return 'sent to %s, not ff:ff:ff:ff:ff:ff' % macs[0].hex(':'), w
        return None, w


def case_hijack():
    def forge(w):
        # Unsolicited "10.0.2.255 is-at ROGUE", addressed to the guest.
        w.send_arp(2, ROGUE_MAC, '10.0.2.255', bytes.fromhex('525400123456'), GUEST_IP)
    w, macs = run('10.0.2.255', forge)
    with w:
        if macs is None:
            return 'guest never came up', w
        if not macs:
            return 'no datagram to 10.0.2.255 left the guest', w
        if macs[0] == ROGUE_MAC:
            return 'the broadcast went to the forged MAC', w
        if macs[0] != BCAST_MAC:
            return 'sent to %s, not ff:ff:ff:ff:ff:ff' % macs[0].hex(':'), w
        return None, w


def main():
    failed = 0
    only = sys.argv[1:]
    for name, fn in (('limited', case_limited), ('directed', case_directed),
                     ('hijack', case_hijack)):
        if only and name not in only:
            continue
        err, w = fn()
        if err:
            failed += 1
            print('FAIL  %s: %s' % (name, err))
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
