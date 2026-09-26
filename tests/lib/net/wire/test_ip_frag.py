#!/usr/bin/env python3
"""
UDP-I-01 (docs/ip-audit-2026-09-22.md): IPv4 reassembly, RFC 791 3.2 /
RFC 1122 3.3.2.  ip4_input() dropped every fragment, so the usable UDP
Length range was 8..1472 instead of RFC 768's 8..65507.

    in-order      a 3000-octet datagram in three fragments arrives whole
                  (recvfrom), with the right length, source port and bytes.
    read          the same through read(2).
    reorder       the fragments last-first, one of them twice.
    overlap       fragments whose ranges overlap by 8 octets.
    incomplete    a datagram missing its middle fragment is never
                  delivered; a complete one sent after it is.
    oversize      a fragment reaching past octet 65535 (the "ping of
                  death") is dropped, so its datagram never completes; a
                  small datagram sent after it is delivered.
    flood         twenty incomplete datagrams overflow the reassembly
                  table; the oldest are evicted and a complete datagram
                  sent after them is still delivered.
    max           a 65507-octet datagram (the RFC 768 maximum) in 45
                  fragments, with SO_RCVBUF raised to hold it.
    big-echo      a 3000-octet ICMP echo request in fragments draws a reply
                  truncated to one MTU (1480 octets of ICMP) with a valid
                  checksum -- we do not fragment on output, and RFC 1122
                  3.2.2.6 says truncate rather than drop.
    bufid-reuse   a first fragment, then a whole datagram with the same
                  source, destination, protocol and ID, then the first
                  datagram's remaining fragment: only the whole datagram is
                  delivered -- it discards the stale reassembly (RFC 791
                  3.2), which would otherwise complete from the old pieces.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_frag.py [case...]
"""
import os
import re
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, csum, PEER_IP, GUEST_IP  # noqa: E402

PORT = 7300
MF = 0x2000


def payload(n, seed=7):
    return bytes((i * seed + 3) & 0xFF for i in range(n))


def udp(sport, data):
    """A whole UDP datagram (header + data), checksummed over all of it."""
    hdr = struct.pack('!HHHH', sport, PORT, 8 + len(data), 0)
    pseudo = (socket.inet_aton(PEER_IP) + socket.inet_aton(GUEST_IP) +
              struct.pack('!BBH', 0, 17, 8 + len(data)))
    c = csum(pseudo + hdr + data) or 0xFFFF
    return hdr[:6] + struct.pack('!H', c) + data


def pieces(dg, size=1480):
    """(offset, bytes, last) fragments of dg, each `size` octets but the
    last."""
    out = []
    for off in range(0, len(dg), size):
        out.append((off, dg[off:off + size], off + size >= len(dg)))
    return out


def send(w, frags, ident):
    for off, part, last in frags:
        w.send_ip(17, part, ident=ident, frag=(0 if last else MF) | (off // 8))


def boot(actions):
    w = Wire.boot('udpany %d %s' % (PORT, actions))
    if not w.wait_serial('guest: udpany bound', 90):
        return w, 'guest never bound'
    w.pump(0.5)
    return w, None


def recvsum(w, timeout=10):
    """The first 'recvsum' report as (n, sport, sum), or None."""
    if not w.wait_serial('guest: recvsum', timeout):
        return None
    w.pump(0.3)
    m = re.search(r'guest: recvsum n=(-?\d+) sport=(\d+) sum=(\d+)', w.serial())
    return tuple(int(x) for x in m.groups()) if m else None


def check(w, got, sport, data):
    if got is None:
        return 'nothing delivered'
    n, sp, sm = got
    if (n, sp, sm) != (len(data), sport, sum(data)):
        return 'delivered n=%d sport=%d sum=%d, want n=%d sport=%d sum=%d' % (
            n, sp, sm, len(data), sport, sum(data))
    return None


def case_in_order():
    w, err = boot('recvsum:70000')
    with w:
        if err:
            return err, w
        data = payload(3000)
        send(w, pieces(udp(40000, data)), 0x1111)
        return check(w, recvsum(w), 40000, data), w


def case_read():
    w, err = boot('read:4096')
    with w:
        if err:
            return err, w
        send(w, pieces(udp(40000, payload(3000))), 0x1112)
        if not w.wait_serial('guest: read', 10):
            return 'nothing delivered', w
        w.pump(0.3)
        if 'guest: read ok n=3000' not in w.serial():
            return 'read() did not return the whole datagram', w
        return None, w


def case_reorder():
    w, err = boot('recvsum:70000')
    with w:
        if err:
            return err, w
        data = payload(3000, 11)
        f = pieces(udp(40000, data))
        send(w, [f[2], f[1], f[1], f[0]], 0x2222)
        return check(w, recvsum(w), 40000, data), w


def case_overlap():
    w, err = boot('recvsum:70000')
    with w:
        if err:
            return err, w
        data = payload(3000, 13)
        dg = udp(40000, data)
        # three frame-sized pieces, each starting 8 octets before the
        # previous one ended
        send(w, [(0, dg[:1480], False), (1472, dg[1472:2952], False),
                 (2944, dg[2944:], True)], 0x3333)
        return check(w, recvsum(w), 40000, data), w


def case_incomplete():
    w, err = boot('recvsum:70000')
    with w:
        if err:
            return err, w
        f = pieces(udp(40000, payload(3000, 17)))
        send(w, [f[0], f[2]], 0x4444)                  # no middle
        good = payload(2000, 19)
        send(w, pieces(udp(40001, good)), 0x4445)
        return check(w, recvsum(w), 40001, good), w


def case_oversize():
    w, err = boot('recvsum:70000')
    with w:
        if err:
            return err, w
        f = pieces(udp(40000, payload(3000, 23)))
        send(w, [f[0]], 0x5555)
        # offset 65472 + 1480 octets runs past the largest datagram
        w.send_ip(17, b'\x5a' * 1480, ident=0x5555, frag=65472 // 8)
        small = b'after the oversize fragment'
        w.send_ip(17, udp(40002, small))
        return check(w, recvsum(w), 40002, small), w


def case_flood():
    w, err = boot('recvsum:70000')
    with w:
        if err:
            return err, w
        for i in range(20):                             # 8 slots, 20 ids
            f = pieces(udp(41000 + i, payload(3000, 31)))
            send(w, [f[0]], 0x7000 + i)
        good = payload(3000, 37)
        send(w, pieces(udp(40003, good)), 0x7100)
        return check(w, recvsum(w), 40003, good), w


def case_max():
    w, err = boot('rcvbuf:262144 recvsum:70000')
    with w:
        if err:
            return err, w
        if not w.wait_serial('guest: rcvbuf ok', 10):
            return 'SO_RCVBUF refused', w
        data = payload(65507, 29)
        f = pieces(udp(40000, data))
        if len(f) != 45:
            return 'test built %d fragments, want 45' % len(f), w
        send(w, f, 0x6666)
        return check(w, recvsum(w, 20), 40000, data), w


def case_big_echo():
    # The guest talks to the host first so the reply, sent from the receive
    # path, finds an ARP entry rather than being dropped behind a request.
    w, err = boot('sendto:10.0.2.2:9:arp sleep:60')
    with w:
        if err:
            return err, w
        ident, seq = 0x4242, 9
        data = payload(3000 - 8, seed=11)
        echo = bytearray(struct.pack('!BBHHH', 8, 0, 0, ident, seq) + data)
        echo[2:4] = struct.pack('!H', csum(bytes(echo)))
        for off, part, last in pieces(bytes(echo)):
            w.send_ip(1, part, ident=0x7777,
                      frag=(0 if last else MF) | (off // 8))
        end = time.time() + 5
        while time.time() < end:
            w.pump(0.2)
            for proto, src, dst, ip in w.ip_rx:
                ihl = (ip[0] & 0xF) * 4
                icmp = ip[ihl:]
                if proto != 1 or icmp[0] != 0:
                    continue
                rid, rseq = struct.unpack('!HH', icmp[4:8])
                if (rid, rseq) != (ident, seq):
                    continue
                if len(icmp) != 1480:
                    return 'reply carries %d octets of ICMP, want 1480 ' \
                           '(truncated to the MTU)' % len(icmp), w
                if csum(bytes(icmp)) != 0:
                    return 'truncated reply has a bad checksum', w
                if icmp[8:] != data[:1480 - 8]:
                    return 'reply data does not match the request', w
                return None, w
        return 'no reply to a 3000-octet echo request', w


def case_bufid_reuse():
    w, err = boot('recvsum:4000 recvsum:4000 sleep:60')
    with w:
        if err:
            return err, w
        old = pieces(udp(40051, payload(2000)))
        whole = udp(40052, payload(100, seed=5))
        ident = 0x6161
        first, rest = old[0], old[1:]
        send(w, [first], ident)                 # old datagram, first piece
        w.pump(0.3)
        w.send_ip(17, whole, ident=ident)       # whole datagram, same BUFID
        w.pump(0.3)
        send(w, rest, ident)                    # the old datagram's tail
        w.pump(2.0)
        got = [l for l in w.serial().splitlines() if 'guest: recvsum' in l]
        if not got or 'sport=40052' not in got[0]:
            return 'the whole datagram was not delivered: %r' % got, w
        if len(got) > 1:
            return 'a stale reassembly completed after a whole datagram ' \
                   'reused its ID: %r' % got[1:], w
        return None, w


CASES = (('in-order', case_in_order), ('read', case_read),
         ('reorder', case_reorder), ('overlap', case_overlap),
         ('incomplete', case_incomplete), ('oversize', case_oversize),
         ('flood', case_flood), ('max', case_max), ('big-echo', case_big_echo),
         ('bufid-reuse', case_bufid_reuse))


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
