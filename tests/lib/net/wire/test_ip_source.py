#!/usr/bin/env python3
"""
The IPv4 source address of outgoing datagrams (RFC 791 3.3: the sender's
source address must be one of the host's own).

    bcast-bound   a UDP socket bound to the subnet broadcast address sends
                  with the interface's own address as the source, never
                  the broadcast address.
    stale-source  after the interface's address changes, a socket still
                  bound to the old address cannot send from it: the send
                  fails EADDRNOTAVAIL and nothing leaves with that source.
    bad-ifaddr    SIOCSIFADDR refuses a multicast address for an interface.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_source.py [case...]
"""
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire  # noqa: E402

GUEST = '10.0.2.15'


def udp_sources(w, dst):
    return [socket.inet_ntoa(fr[26:30]) for d, et, fr in w.frames
            if et == 0x0800 and fr[23] == 17 and socket.inet_ntoa(fr[30:34]) == dst]


def sends(w):
    return [l for l in w.serial().splitlines() if l.startswith('guest: sendto')]


def case_bcast_bound():
    # two sends a second apart: the first can be lost resolving the peer
    with Wire.boot('udpbind 10.0.2.255 7402 sendto:10.0.2.2:9:x sleep:1 '
                   'sendto:10.0.2.2:9:y sleep:60') as w:
        if not w.wait_serial('guest: slept', 90):
            return 'guest never got to its sends: %r' % sends(w), w
        w.wait_serial('guest: sendto', 5)
        w.pump(1.5)
        srcs = udp_sources(w, '10.0.2.2')
        if not srcs:
            return 'no datagram reached the peer (%r)' % sends(w), w
        if any(s != GUEST for s in srcs):
            return 'datagram left with source %s, want %s' % (srcs, GUEST), w
        return None, w


def case_stale_source():
    with Wire.boot('udpbind %s 7402 ifaddr:10.0.2.16 sendto:10.0.2.2:9:x '
                   'sleep:1 sendto:10.0.2.2:9:y sleep:60' % GUEST) as w:
        if not w.wait_serial('guest: slept', 90):
            return 'guest never got to its sends: %r' % sends(w), w
        w.wait_serial('guest: sendto', 5)
        w.pump(1.5)
        if GUEST in udp_sources(w, '10.0.2.2'):
            return 'a datagram left from the address no longer configured', w
        s = sends(w)
        if len(s) != 2 or any('assign requested address' not in l for l in s):
            return 'sends: %r, want EADDRNOTAVAIL' % s, w
        return None, w


def case_bad_ifaddr():
    with Wire.boot('udpany 7402 ifaddr:224.0.0.5 sleep:60') as w:
        if not w.wait_serial('guest: ifaddr', 90):
            return 'guest never set the address', w
        line = [l for l in w.serial().splitlines() if l.startswith('guest: ifaddr')][0]
        if 'ok' in line:
            return 'SIOCSIFADDR accepted a multicast address: %s' % line, w
        return None, w


CASES = (('bcast-bound', case_bcast_bound), ('stale-source', case_stale_source),
         ('bad-ifaddr', case_bad_ifaddr))


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
