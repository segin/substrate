#!/usr/bin/env python3
"""
UDP-ICMP-02 (docs/ip-audit-2026-09-22.md): RFC 1122 4.1.3.1 -- a UDP
datagram for a port with no listener is answered with ICMP Destination
Unreachable, code 3 (port), quoting the invoking IP header and the first 8
octets of its data.  udp_input() computed whether anything took the
datagram and threw the answer away, so nothing was ever sent.

    closed-port   a datagram to a closed port draws a Port Unreachable from
                  the address it was sent to, quoting the UDP header.
    broadcast     one sent to the subnet broadcast address draws nothing
                  (RFC 1122 3.2.2: never an ICMP error about a broadcast).
    rate-limit    a burst of 40 draws no more than the per-second budget.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_udp_icmp.py
"""
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, GUEST_IP, PEER_IP  # noqa: E402

CLOSED = 9999


def boot():
    # The guest dials the peer first, which resolves the peer's MAC.  The
    # error is generated in RX (interrupt) context, where ip4_output() cannot
    # wait for ARP and drops the packet on a cache miss (NET-05), so an
    # unprimed cache would lose the errors this test is looking for.
    w = Wire.boot('connect 10.0.2.2 7011 sleep:120')
    if not w.expect(lambda s: s.dport == 7011, 90, 'SYN'):
        return w, 'guest never came up'
    w.pump(0.5)
    w.ip_rx.clear()
    return w, None


def is_port_unreach(d):
    proto, src, dst, ip = d
    if proto != 1:
        return False
    ihl = (ip[0] & 0xF) * 4
    return ip[ihl] == 3 and ip[ihl + 1] == 3


def case_closed_port():
    w, err = boot()
    with w:
        if err:
            return err, w
        w.send_udp(40000, CLOSED, b'hello')
        d = w.expect_ip(is_port_unreach, 3)
        if not d:
            return 'no ICMP Port Unreachable for a closed port', w
        proto, src, dst, ip = d
        ihl = (ip[0] & 0xF) * 4
        icmp = ip[ihl:]
        if src != GUEST_IP or dst != PEER_IP:
            return 'error sent %s -> %s' % (src, dst), w
        q = icmp[8:]                                  # quoted datagram
        qihl = (q[0] & 0xF) * 4
        qsrc = socket.inet_ntoa(q[12:16])
        sport, dport = struct.unpack('!HH', q[qihl:qihl + 4])
        if qsrc != PEER_IP or (sport, dport) != (40000, CLOSED):
            return 'quote wrong: %s %d->%d' % (qsrc, sport, dport), w
        if len(q) != qihl + 8:
            return 'quote is %d octets, want IP header + 8' % len(q), w
        c = 0
        data = icmp + (b'\0' if len(icmp) % 2 else b'')
        for i in range(0, len(data), 2):
            c += (data[i] << 8) | data[i + 1]
        while c >> 16:
            c = (c & 0xFFFF) + (c >> 16)
        if c != 0xFFFF:
            return 'ICMP checksum does not verify', w
        return None, w


def case_broadcast():
    w, err = boot()
    with w:
        if err:
            return err, w
        w.send_udp(40001, CLOSED, b'bcast', dst='10.0.2.255')
        if w.expect_ip(is_port_unreach, 3):
            return 'ICMP error sent about a broadcast datagram', w
        return None, w


def case_rate_limit():
    w, err = boot()
    with w:
        if err:
            return err, w
        for i in range(40):
            w.send_udp(41000 + i, CLOSED, b'x')
        w.pump(0.5)
        n = sum(1 for d in w.ip_rx if is_port_unreach(d))
        if n == 0:
            return 'no errors at all', w
        if n > 12:
            return '%d errors for a 40-datagram burst: not rate limited' % n, w
        return None, w


def main():
    failed = 0
    only = sys.argv[1:]
    for name, fn in (('closed-port', case_closed_port), ('broadcast', case_broadcast),
                     ('rate-limit', case_rate_limit)):
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
