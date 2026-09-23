#!/usr/bin/env python3
"""
The TCP urgent mechanism (docs/ip-audit-2026-09-22.md, TCP-E).  Each case
names the checklist item it guards.  Substrate uses the BSD pointer
convention on both sides: the urgent pointer is the offset of the octet
FOLLOWING the urgent data, so the urgent octet is SEG.SEQ + SEG.UP - 1 (see
TCP-URG-06).

    recv-mark   TCP-URG-01: the peer sends "abcdef" with URG and SEG.UP 3.
                The owner (F_SETOWN) gets SIGURG, the urgent octet 'c' is
                taken out of the stream, the first read stops at the mark
                ("ab", 2 octets), SIOCATMARK is then 1, and the next read
                returns "def".
    send-urg    TCP-URG-02: after write("ab"), send("X", MSG_OOB) puts 'X'
                on the wire in a segment with URG set and a pointer whose
                preceding octet is 'X'; the flag stays on retransmissions
                until the peer acknowledges it.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_urgent.py [case...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, RST, PSH  # noqa: E402

PORT = 7080
PISS = 150000
URG = 0x20


def handshake(w):
    syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
    if not syn:
        return None, 'no SYN from guest'
    w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
    if not w.expect(lambda s: s.flags & ACK and not s.flags & SYN, 5, 'ACK'):
        return None, 'handshake ACK missing'
    if not w.wait_serial('guest: connected', 10):
        return None, 'guest did not report connected'
    return syn, None


def line(w, prefix):
    ls = [l.strip() for l in w.serial().splitlines() if l.startswith('guest: ' + prefix)]
    return ls


def case_recv_mark():
    with Wire.boot('connect 10.0.2.2 %d setown sleep:3 sigurg read:64 atmark '
                   'read:64 sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        if not w.wait_serial('guest: setown', 5):
            return 'guest never set the owner', w
        w.send(Seg(PORT, gp, PISS + 1, g, ACK | PSH | URG, data=b'abcdef', urp=3))
        if not w.wait_serial('guest: read', 10) or len(line(w, 'read')) < 2:
            w.pump(3)
        reads = line(w, 'read')
        s = line(w, 'sigurg')
        a = line(w, 'atmark')
        if not s or 'count=0' in s[0]:
            return 'no SIGURG: %s' % s, w
        if len(reads) < 2 or 'n=2' not in reads[0] or 'n=3' not in reads[1]:
            return 'reads did not stop at the mark: %s' % reads, w
        if not a or 'value=1' not in a[0]:
            return 'SIOCATMARK not set at the mark: %s' % a, w
        return None, w


def case_send_urg():
    with Wire.boot('connect 10.0.2.2 %d write:ab oob:X sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        d = w.expect(lambda s: s.data and s.seq == g, 5, 'ab')
        if not d or d.data != b'ab':
            return 'no "ab": %r' % d, w
        w.send(Seg(PORT, gp, PISS + 1, g + 2, ACK))
        u = w.expect(lambda s: s.data == b'X', 5, 'X')
        if not u:
            return 'no urgent octet on the wire', w
        if not u.flags & URG or u.seq + u.urp - 1 != g + 2:
            return 'want URG with the pointer after "X", got %r urp=%d' % (u, u.urp), w
        w.rx.clear()
        r = w.expect(lambda s: s.data == b'X', 3, 'retransmission')
        if not r or not r.flags & URG:
            return 'retransmission lost URG: %r' % r, w
        return None, w


CASES = (('recv-mark', case_recv_mark),
         ('send-urg', case_send_urg))


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
