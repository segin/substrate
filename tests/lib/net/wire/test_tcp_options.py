#!/usr/bin/env python3
"""
TCP options and segment size (docs/ip-audit-2026-09-22.md, TCP-D).  Each
case names the checklist item it guards.

    peer-mss    TCP-HDR-02: an MSS option of 536 on the peer's SYN|ACK limits
                every data segment the guest sends to 536 octets.
    bad-options TCP-HDR-02: SYNs carrying a zero-length option, an option
                longer than the header, and an option list running exactly to
                the header's end are each answered with a SYN|ACK -- the
                option walk neither hangs nor over-reads.
    own-mss     TCP-HDR-03: the guest's SYN (active open) and SYN|ACK
                (passive open) each carry an MSS option of 1460, in a
                24-octet header.
    mtu-clamp   TCP-HDR-04: on an interface with a 1000-octet MTU the guest
                advertises MSS 960 and, although the peer offers 1460, sends
                no segment over 960 -- they used to fail EMSGSIZE in
                ip4_output() on every attempt and the transfer stalled.
    isn         TCP-HDR-05: RFC 6528 ISNs.  Two connections on the same
                4-tuple, some seconds apart, get ISNs that advance by the
                ~4 us clock (250000/s), not by a random amount.
    isn-boots   TCP-HDR-05: the first ISN differs between two boots.  The old
                code misread random_get_bytes()'s return value and drew every
                ISS from a fixed-seed LCG: identical on every boot.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_options.py [case...]
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, RST, PSH, PEER_MAC, PEER_IP, GUEST_IP  # noqa: E402

PORT = 7070
PISS = 130000


def mss_opt(n):
    return struct.pack('!BBH', 2, 4, n)


def drain(w, gp, g, total, win=65535):
    """ACK everything the guest sends until `total` octets arrived; return
    the list of data segment lengths."""
    got, sizes = 0, []
    end = time.time() + 20
    while got < total and time.time() < end:
        s = w.expect(lambda s: s.data, 3, 'data')
        if not s:
            continue
        off = (s.seq - g) & 0xFFFFFFFF
        if off <= got < off + len(s.data):
            sizes.append(len(s.data))
            got = off + len(s.data)
        w.send(Seg(PORT, gp, PISS + 1, g + got, ACK, win=win))
    return got, sizes


def case_peer_mss():
    with Wire.boot('connect 10.0.2.2 %d writen:3000 sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN from guest', w
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK, opts=mss_opt(536)))
        if not w.wait_serial('guest: connected', 10):
            return 'guest did not report connected', w
        got, sizes = drain(w, syn.sport, syn.seq + 1, 3000)
        if got != 3000:
            return 'got %d of 3000 octets' % got, w
        if max(sizes) > 536:
            return 'segments larger than the peer MSS: %r' % sizes, w
        return None, w


def case_bad_options():
    with Wire.boot('listen %d sleep:60' % PORT) as w:
        if not w.wait_serial('guest: listening', 90):
            return 'guest never listened', w
        w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP)
        w.pump(0.5)
        cases = (
            ('zero-length', b'\x01\x08\x00\x01'),          # NOP, kind 8 len 0
            ('over-long', b'\x02\x28\x05\xb4'),           # MSS kind, len 40
            ('to-the-end', b'\x01\x01\x02\x04\x05\xb4\x01\x03'),  # ends at 3rd byte of an option
        )
        for i, (name, opts) in enumerate(cases):
            hp = 43000 + i
            w.rx.clear()
            w.send(Seg(hp, PORT, PISS, 0, SYN, opts=opts))
            if not w.expect(lambda s, hp=hp: s.dport == hp and s.flags & SYN, 3, 'SYN-ACK'):
                return 'no SYN|ACK for the %s option list' % name, w
        return None, w


def has_mss(seg, want):
    return len(seg.opts) == 4 and seg.opts == mss_opt(want)


def case_own_mss():
    with Wire.boot('connect 10.0.2.2 %d sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn or not has_mss(syn, 1460):
            return 'active SYN without MSS 1460: %r opts=%r' % (syn, syn and syn.opts), w
    with Wire.boot('listen %d sleep:60' % PORT) as w:
        if not w.wait_serial('guest: listening', 90):
            return 'guest never listened', w
        w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP)
        w.pump(0.5)
        w.send(Seg(43100, PORT, PISS, 0, SYN, opts=mss_opt(1460)))
        sa = w.expect(lambda s: s.dport == 43100 and s.flags & SYN, 3, 'SYN-ACK')
        if not sa or not has_mss(sa, 1460):
            return 'SYN|ACK without MSS 1460: %r opts=%r' % (sa, sa and sa.opts), w
    return None, w


def case_mtu_clamp():
    with Wire.boot('mtu=1000 connect 10.0.2.2 %d writen:3000 sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN from guest', w
        if not has_mss(syn, 960):
            return 'SYN advertises %r, want MSS 960' % syn.opts, w
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK, opts=mss_opt(1460)))
        if not w.wait_serial('guest: connected', 10):
            return 'guest did not report connected', w
        got, sizes = drain(w, syn.sport, syn.seq + 1, 3000)
        if got != 3000:
            return 'got %d of 3000 octets (sizes %r)' % (got, sizes), w
        if max(sizes) > 960:
            return 'segments larger than the MTU allows: %r' % sizes, w
        return None, w


def case_isn():
    lport = 45123
    with Wire.boot('redial 10.0.2.2 %d %d' % (PORT, lport)) as w:
        isns, times = [], []
        for rnd in range(2):
            syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT and
                           s.sport == lport, 90 if rnd == 0 else 15, 'SYN')
            if not syn:
                return 'no SYN in round %d' % rnd, w
            isns.append(syn.seq)
            times.append(time.time())
            w.send(Seg(PORT, lport, PISS, syn.seq + 1, SYN | ACK))
            if not w.wait_serial('guest: connected %d' % rnd, 10):
                return 'round %d did not connect' % rnd, w
            w.pump(0.3)
            w.send(Seg(PORT, lport, PISS + 1, 0, RST))
            w.rx.clear()
        dt = times[1] - times[0]
        delta = (isns[1] - isns[0]) & 0xFFFFFFFF
        want = dt * 250000
        if not 0.5 * want <= delta <= 1.5 * want:
            return 'ISN advanced by %d over %.2f s; want ~%d' % (delta, dt, want), w
        return None, w


def case_isn_boots():
    isns = []
    for _ in range(2):
        with Wire.boot('connect 10.0.2.2 %d sleep:60' % PORT) as w:
            syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
            if not syn:
                return 'no SYN from guest', w
            isns.append(syn.seq)
    if isns[0] == isns[1]:
        return 'the same ISN (%d) on two boots' % isns[0], w
    return None, w


CASES = (('peer-mss', case_peer_mss),
         ('bad-options', case_bad_options),
         ('own-mss', case_own_mss),
         ('mtu-clamp', case_mtu_clamp),
         ('isn', case_isn),
         ('isn-boots', case_isn_boots))


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
            print(w.dump()[-2500:])
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
