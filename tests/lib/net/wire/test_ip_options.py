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
    srcroute a datagram carrying a loose source route with a hop still to
            visit is not delivered (a host does not forward, RFC 1122
            3.3.5) and draws no error; one whose route is exhausted is.
    send    IP_OPTIONS: with a Record Route set, the guest's datagram carries
            it (IHL 7, a valid header checksum, the payload after it) and
            getsockopt reads it back; cleared, the next datagram has none;
            a malformed list is refused EINVAL.
    echo    an echo request carrying Record Route and Timestamp is answered
            with both, updated with this host: its address in the next
            Record Route slot and a time in the next Timestamp slot (RFC
            1122 3.2.2.6).

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_options.py
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, csum, GUEST_IP  # noqa: E402

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


def case_srcroute():
    hop = bytes([10, 0, 2, 99])
    pending = b'\x83\x07\x04' + hop + b'\x00'      # LSRR, one hop to go
    done = b'\x83\x07\x08' + hop + b'\x00'         # LSRR, route exhausted
    with Wire.boot('udpany %d sendto:10.0.2.2:9:arp recvsum:100 sleep:60'
                   % PORT) as w:
        if not w.wait_serial('guest: sendto', 90):
            return 'guest never sent its ARP-priming datagram', w
        w.pump(0.5)
        w.send_udp(40031, PORT, b'via-route', dst=GUEST_IP, opts=pending)
        w.pump(0.5)
        w.send_udp(40032, PORT, b'route-done', dst=GUEST_IP, opts=done)
        if not w.wait_serial('guest: recvsum', 10):
            return 'the datagram whose route was exhausted was not ' \
                   'delivered', w
        w.pump(0.5)
        got = [l for l in w.serial().splitlines() if 'guest: recvsum' in l][0]
        if 'sport=40031' in got:
            return 'a datagram with a source-route hop left was delivered', w
        if 'sport=40032' not in got:
            return 'unexpected delivery: %s' % got, w
        if any(p[1] == 40031 for p in param_problems(w)):
            return 'the pending source route drew a Parameter Problem', w
        return None, w


def case_send():
    rr = '0707040000000000'
    with Wire.boot('udpany %d ipopts:%s getipopts sendto:10.0.2.2:9:rr '
                   'ipopts: sendto:10.0.2.2:9:plain ipopts:0701 sleep:60'
                   % (PORT, rr)) as w:
        end = time.time() + 90
        while time.time() < end and w.serial().count('guest: ipopts') < 3:
            w.pump(0.2)
        if w.serial().count('guest: ipopts') < 3:
            return 'guest never finished its sends', w
        w.pump(1.0)
        lines = [l for l in w.serial().splitlines() if 'guest: ' in l]
        sets = [l for l in lines if 'guest: ipopts' in l]
        if len(sets) < 3 or 'ok' not in sets[0] or 'ok' not in sets[1]:
            return 'IP_OPTIONS set/clear: %r' % sets, w
        if 'invalid' not in sets[2].lower():
            return 'a malformed option list was accepted: %r' % sets[2], w
        got = [l for l in lines if 'getipopts' in l]
        if not got or '|%s|' % rr not in got[0]:
            return 'getsockopt(IP_OPTIONS): %r, want %s' % (got, rr), w
        udp = [fr[14:] for mac, et, fr in w.frames
               if et == 0x0800 and fr[14 + 9] == 17 and
               fr[14 + 16:14 + 20] == bytes([10, 0, 2, 2])]
        if len(udp) < 2:
            return 'saw %d datagrams, want 2' % len(udp), w
        with_opts, plain = udp[0], udp[1]
        ihl = (with_opts[0] & 0xF) * 4
        if ihl != 28 or with_opts[20:28].hex() != rr:
            return 'first datagram: IHL %d, options %s; want 28 and %s' % (
                ihl, with_opts[20:ihl].hex(), rr), w
        if csum(bytes(with_opts[:ihl])) != 0:
            return 'the header with options has a bad checksum', w
        if with_opts[ihl + 8:ihl + 10] != b'rr':
            return 'the payload moved: %r' % bytes(with_opts[ihl + 8:]), w
        if (plain[0] & 0xF) != 5:
            return 'options still sent after they were cleared', w
        return None, w


def case_echo():
    import struct
    rr = b'\x07\x0b\x04' + b'\0' * 8                  # Record Route, 2 slots
    ts = b'\x44\x0c\x05\x00' + b'\0' * 8              # Timestamp, flag 0, 2 slots
    opts = rr + ts + b'\x00'                           # 11 + 12 + EOL = 24
    with Wire.boot('udpany %d sendto:10.0.2.2:9:arp sleep:60' % PORT) as w:
        if not w.wait_serial('guest: sendto', 90):
            return 'guest never sent its ARP-priming datagram', w
        w.pump(0.5)
        echo = bytearray(struct.pack('!BBHHH', 8, 0, 0, 0x1234, 1) + b'opts')
        echo[2:4] = struct.pack('!H', csum(bytes(echo)))
        w.send_ip(1, bytes(echo), dst=GUEST_IP, opts=opts)
        end = time.time() + 5
        while time.time() < end:
            w.pump(0.2)
            for proto, src, dst, ip in w.ip_rx:
                ihl = (ip[0] & 0xF) * 4
                if proto != 1 or ip[ihl] != 0:
                    continue
                o = bytes(ip[20:ihl])
                if csum(bytes(ip[:ihl])) != 0:
                    return 'the reply header checksum is bad', w
                if len(o) < 23 or o[0] != 7 or o[11] != 0x44:
                    return 'reply options %s, want Record Route then ' \
                           'Timestamp' % o.hex(), w
                if o[2] != 8 or o[3:7] != bytes([10, 0, 2, 15]):
                    return 'Record Route pointer %d, slot %s: want 8 and ' \
                           '10.0.2.15' % (o[2], o[3:7].hex()), w
                if o[13] != 9 or o[15:19] == b'\0' * 4:
                    return 'Timestamp pointer %d, stamp %s: want 9 and a ' \
                           'time' % (o[13], o[15:19].hex()), w
                return None, w
        return 'no echo reply', w


CASES = (('walk', case_walk), ('srcroute', case_srcroute), ('send', case_send),
         ('echo', case_echo))


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
