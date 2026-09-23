#!/usr/bin/env python3
"""
UDP-IP-06 (docs/ip-audit-2026-09-22.md): IPv4 multicast end to end, per
RFC 1112 6 and RFC 1122 3.3.7.  Every piece was missing -- no route, no
01:00:5e mapping, no input path, no membership state, and NIC receive
filters that rejected every group frame -- while IP_ADD_MEMBERSHIP returned
success.

    receive       a datagram to a joined group is delivered; one to a group
                  nobody joined is not.  Run on virtio-net and on e1000, whose
                  receive filter (RCTL.MPE) is part of the fix.  rtl8139 is
                  not exercised: under this harness it receives nothing at
                  all, unicast included, on the baseline kernel too -- a
                  separate driver bug -- so its MAR change is review-only.
    send          a datagram to a group leaves as 01:00:5e:<low 23 bits>,
                  addressed to the group, with TTL 1 (RFC 1112 6.1).
    loop          a member sending to its own group hears it back (the
                  default IP_MULTICAST_LOOP of RFC 1112 6.1).

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_multicast.py
"""
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire  # noqa: E402

GROUP, OTHER, PORT = '239.1.2.3', '239.9.9.9', 5000


def group_mac(g):
    b = socket.inet_aton(g)
    return bytes([0x01, 0x00, 0x5e, b[1] & 0x7F, b[2], b[3]])


def case_receive(nic):
    with Wire.boot('mcast %s %d read:100' % (GROUP, PORT), nic=nic) as w:
        if not w.wait_serial('guest: joined', 90):
            return 'guest could not join: %s' % w.serial()[-200:], w
        w.pump(0.5)
        w.send_udp(40000, PORT, b'notmine', dst=OTHER, eth_dst=group_mac(OTHER))
        w.pump(0.3)
        w.send_udp(40000, PORT, b'mine', dst=GROUP, eth_dst=group_mac(GROUP))
        if not w.wait_serial('guest: read ', 5):
            return 'the group datagram was not delivered', w
        line = [l for l in w.serial().splitlines() if l.startswith('guest: read ')][0]
        if 'n=4' not in line:
            return 'wrong datagram delivered: %s' % line.strip(), w
        return None, w


def case_send():
    with Wire.boot('mcast %s %d sendto:%s:%d:hello sleep:60' % (GROUP, PORT, GROUP, PORT)) as w:
        if not w.wait_serial('guest: sendto', 90):
            return 'guest never sent', w
        w.pump(0.5)
        for mac, etype, fr in w.frames:
            if etype == 0x0800 and fr[14 + 9] == 17 and socket.inet_ntoa(fr[30:34]) == GROUP:
                if mac != group_mac(GROUP):
                    return 'sent to %s, want %s' % (mac.hex(':'), group_mac(GROUP).hex(':')), w
                if fr[14 + 8] != 1:
                    return 'TTL %d, want 1' % fr[14 + 8], w
                return None, w
        line = [l for l in w.serial().splitlines() if l.startswith('guest: sendto')]
        return 'no datagram to the group left the guest (%s)' % (line[0].strip() if line else '?'), w


def case_loop():
    with Wire.boot('mcast %s %d sendto:%s:%d:loop read:100' % (GROUP, PORT, GROUP, PORT)) as w:
        if not w.wait_serial('guest: read ', 90):
            return 'the member never heard its own datagram', w
        line = [l for l in w.serial().splitlines() if l.startswith('guest: read ')][0]
        if 'n=4' not in line:
            return 'expected its own 4-octet datagram, got: %s' % line.strip(), w
        return None, w


def main():
    failed = 0
    only = sys.argv[1:]
    cases = [('receive-virtio', lambda: case_receive('virtio-net-pci')),
             ('receive-e1000', lambda: case_receive('e1000')),
             ('send', case_send), ('loop', case_loop)]
    for name, fn in cases:
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
