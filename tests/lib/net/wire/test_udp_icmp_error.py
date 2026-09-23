#!/usr/bin/env python3
"""
UDP-ICMP-01 (docs/ip-audit-2026-09-22.md): RFC 1122 4.1.3.3 -- UDP must
pass ICMP errors to the application.  icmp_input() returned on anything but
an echo request, and SO_ERROR was a hard 0 for every datagram socket, so a
resolver whose server port was closed learned nothing and sat out its whole
timeout.

    recv        a connected socket blocked in recv() is woken by a Port
                Unreachable about its datagram and fails ECONNREFUSED.
    so-error    the error is readable through SO_ERROR, once.
    other-tuple an error quoting a different 4-tuple leaves the socket
                alone: a real reply arriving afterwards is received.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_udp_icmp_error.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, csum, PEER_IP  # noqa: E402

PORT = 5353
ECONNREFUSED = 111


def guest_datagram(w):
    """The guest's first UDP datagram to the peer: (ip_bytes, sport)."""
    d = w.expect_ip(lambda d: d[0] == 17 and d[2] == PEER_IP, 90)
    if not d:
        return None, None
    ip = d[3]
    ihl = (ip[0] & 0xF) * 4
    return ip, struct.unpack('!H', ip[ihl:ihl + 2])[0]


def port_unreach(quoted_ip, dport=None):
    ihl = (quoted_ip[0] & 0xF) * 4
    q = bytearray(quoted_ip[:ihl + 8])
    if dport is not None:
        q[ihl + 2:ihl + 4] = struct.pack('!H', dport)
    msg = bytearray(struct.pack('!BBHI', 3, 3, 0, 0) + bytes(q))
    msg[2:4] = struct.pack('!H', csum(bytes(msg)))
    return bytes(msg)


def serial_line(w, prefix):
    for l in w.serial().splitlines():
        if l.startswith(prefix):
            return l.strip()
    return None


def case_recv():
    with Wire.boot('udp 10.0.2.2 %d write:query read:512' % PORT) as w:
        ip, sport = guest_datagram(w)
        if ip is None:
            return 'guest sent no datagram', w
        w.pump(0.5)                       # let the guest block in recv()
        w.send_ip(1, port_unreach(ip))
        if not w.wait_serial('guest: read ', 5):
            return 'recv() was not woken by the ICMP error', w
        line = serial_line(w, 'guest: read ')
        if 'refused' not in line:
            return 'expected ECONNREFUSED, got: %s' % line, w
        return None, w


def case_so_error():
    with Wire.boot('udp 10.0.2.2 %d write:query sleep:3 soerror soerror' % PORT) as w:
        ip, sport = guest_datagram(w)
        if ip is None:
            return 'guest sent no datagram', w
        w.send_ip(1, port_unreach(ip))
        if not w.wait_serial('guest: slept', 10):
            return 'guest stalled', w
        w.wait_serial('value=0', 5)
        lines = [l.strip() for l in w.serial().splitlines() if l.startswith('guest: soerror')]
        if len(lines) < 2:
            return 'SO_ERROR not read twice: %r' % lines, w
        if 'value=%d' % ECONNREFUSED not in lines[0] or 'value=0' not in lines[1]:
            return 'expected ECONNREFUSED then 0, got %r' % lines, w
        return None, w


def case_other_tuple():
    with Wire.boot('udp 10.0.2.2 %d write:query read:512' % PORT) as w:
        ip, sport = guest_datagram(w)
        if ip is None:
            return 'guest sent no datagram', w
        w.pump(0.5)
        w.send_ip(1, port_unreach(ip, dport=PORT + 1))   # not this socket's peer
        w.pump(0.5)
        w.send_udp(PORT, sport, b'reply')
        if not w.wait_serial('guest: read ', 5):
            return 'the real reply was not received', w
        line = serial_line(w, 'guest: read ')
        if 'ok n=5' not in line:
            return 'expected the 5-octet reply, got: %s' % line, w
        return None, w


def main():
    failed = 0
    only = sys.argv[1:]
    for name, fn in (('recv', case_recv), ('so-error', case_so_error),
                     ('other-tuple', case_other_tuple)):
        if only and name not in only:
            continue
        err, w = fn()
        if err:
            failed += 1
            print('FAIL  %s: %s' % (name, err))
            print(w.dump()[-1500:])
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
