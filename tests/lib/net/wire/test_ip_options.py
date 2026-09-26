#!/usr/bin/env python3
"""
IPv4 options on input (RFC 791 3.1, 3.2 Options; RFC 1122 3.2.1.8,
3.2.2.5).  The option area used to be skipped unread: a malformed option
was accepted silently and any datagram with options was delivered as if it
had none.

    walk    two malformed datagrams -- an option length of 1, and a Record
            Route whose pointer is 3 -- are not delivered and each draws an
            ICMP Parameter Problem (type 12, code 0) whose pointer names
            the bad octet (21 and 22 from the start of the IP header).  Two
            well-formed ones -- NOP/NOP/NOP/EOL padding, and a Record Route
            with room for one address -- are delivered, in order.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_options.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, GUEST_IP  # noqa: E402

PORT = 7412
RR_OK = b'\x07\x07\x04' + b'\0' * 4 + b'\x00'
BAD = ((40021, b'\x07\x01\x00\x00', 21, 'option length 1'),
       (40022, b'\x07\x07\x03' + b'\0' * 4 + b'\x00', 22,
        'Record Route pointer 3'))
GOOD = ((40023, b'\x01\x01\x01\x00', 'NOP/EOL padding'),
        (40024, RR_OK, 'Record Route'))


def param_problems(w):
    """(pointer, quoted source port) of each ICMP Parameter Problem."""
    out = []
    for proto, src, dst, ip in w.ip_rx:
        ihl = (ip[0] & 0xF) * 4
        if proto != 1 or len(ip) < ihl + 8 or ip[ihl] != 12:
            continue
        q = ip[ihl + 8:]
        qihl = (q[0] & 0xF) * 4 if q else 0
        sport = int.from_bytes(q[qihl:qihl + 2], 'big') if len(q) >= qihl + 2 else -1
        out.append((ip[ihl + 4], sport, ip[ihl + 1]))
    return out


def case_walk():
    # The guest talks to the host first: an ICMP error sent from the receive
    # path with no ARP entry for its destination is dropped after firing
    # the ARP request, so the first Parameter Problem would be lost.
    with Wire.boot('udpany %d sendto:10.0.2.2:9:arp recvsum:100 recvsum:100 '
                   'sleep:60' % PORT) as w:
        if not w.wait_serial('guest: sendto', 90):
            return 'guest never sent its ARP-priming datagram', w
        w.pump(0.5)
        for sport, opts, _, _ in BAD:
            w.send_udp(sport, PORT, b'bad-opts', dst=GUEST_IP, opts=opts)
            w.pump(0.3)
        for sport, opts, _ in GOOD:
            w.send_udp(sport, PORT, b'good-opts', dst=GUEST_IP, opts=opts)
            w.pump(0.3)
        w.pump(1.0)
        got = [l for l in w.serial().splitlines() if 'guest: recvsum' in l]
        want = ['sport=%d' % s for s, _, _ in GOOD]
        if len(got) < 2 or not all(wnt in g for wnt, g in zip(want, got)):
            return 'delivered %r, want the two well-formed datagrams (%s) ' \
                   'in order' % (got, ', '.join(d for _, _, d in GOOD)), w
        pp = param_problems(w)
        for sport, _, ptr, what in BAD:
            hits = [p for p in pp if p[1] == sport]
            if not hits:
                return 'no Parameter Problem for the %s' % what, w
            if hits[0][0] != ptr or hits[0][2] != 0:
                return 'Parameter Problem for the %s: pointer %d code %d, ' \
                       'want pointer %d code 0' % (what, hits[0][0],
                                                   hits[0][2], ptr), w
        if any(p[1] in (s for s, _, _ in GOOD) for p in pp):
            return 'a well-formed datagram drew a Parameter Problem', w
        return None, w


CASES = (('walk', case_walk),)


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
