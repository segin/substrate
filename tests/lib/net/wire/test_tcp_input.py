#!/usr/bin/env python3
"""
RFC 793 3.9 SEGMENT ARRIVES rules for synchronized states
(docs/ip-audit-2026-09-22.md, TCP-B).  Each case names the checklist item
it guards.

    data-after-fin  TCP-SM-03: text the peer sends after its own FIN is not
                    delivered, and RCV.NXT does not move (seventh step:
                    CLOSE-WAIT ignores segment text).
    syn-rcvd-rst    TCP-SM-04: during a passive open, a RST one octet off
                    RCV.NXT draws a challenge ACK and one far outside the
                    window is dropped; neither kills the embryonic
                    connection, which the peer's ACK then completes.
    syn-sync        TCP-SM-06: an in-window SYN on an ESTABLISHED
                    connection draws a challenge ACK (RFC 5961 4) and the
                    connection survives it.
    listen-ack      TCP-SM-07: a bare ACK, and a SYN|ACK, sent to a
                    listening port each draw <SEQ=SEG.ACK><CTL=RST>.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_input.py [case...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, RST, PSH, PEER_MAC, PEER_IP, GUEST_IP  # noqa: E402

PORT = 7050
PISS = 90000


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


def passive(w, hp):
    """Guest listening on PORT: prime ARP, send our SYN, return its SYN-ACK."""
    if not w.wait_serial('guest: listening', 90):
        return None, 'guest never listened'
    w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP)
    w.pump(0.5)
    w.send(Seg(hp, PORT, PISS, 0, SYN))
    sa = w.expect(lambda s: s.dport == hp and s.flags & (SYN | ACK) == SYN | ACK,
                  5, 'SYN-ACK')
    if not sa:
        return None, 'no SYN-ACK'
    return sa, None


def case_data_after_fin():
    with Wire.boot('connect 10.0.2.2 %d sleep:4 readeof sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        w.send(Seg(PORT, gp, PISS + 1, g, FIN | ACK))
        if not w.expect(lambda s: s.flags & ACK and s.ack == PISS + 2, 5, 'ACK of FIN'):
            return 'guest did not ACK our FIN', w
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 2, g, ACK | PSH, data=b'after-the-fin'))
        w.pump(1.0)
        if any(s.ack != PISS + 2 for s in w.rx if s.flags & ACK):
            return 'RCV.NXT moved past the FIN: %r' % w.rx, w
        if not w.wait_serial('guest: readeof', 10):
            return 'guest never finished reading', w
        line = [l for l in w.serial().splitlines() if 'guest: readeof' in l][0]
        if 'total=0' not in line:
            return 'text after the FIN was delivered: %s' % line.strip(), w
        return None, w


def case_syn_rcvd_rst():
    hp = 42001
    with Wire.boot('listen %d sleep:60' % PORT) as w:
        sa, err = passive(w, hp)
        if err:
            return err, w
        w.rx.clear()
        w.send(Seg(hp, PORT, PISS + 1 + 0x40000000, 0, RST))    # far off: drop
        w.send(Seg(hp, PORT, PISS + 2, 0, RST))                 # in window: challenge
        if not w.expect(lambda s: s.dport == hp and s.flags & ACK and
                        not s.flags & RST, 3, 'challenge ACK'):
            return 'in-window RST drew no challenge ACK', w
        w.send(Seg(hp, PORT, PISS + 1, sa.seq + 1, ACK))
        if not w.wait_serial('guest: accepted', 5):
            return 'a RST off RCV.NXT killed the embryonic connection', w
        return None, w


def case_syn_sync():
    with Wire.boot('connect 10.0.2.2 %d sleep:4 read:64 sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 1, 0, SYN))
        ch = w.expect(lambda s: s.flags & ACK, 3, 'challenge ACK')
        if not ch:
            return 'in-window SYN drew nothing', w
        if ch.flags & RST or ch.ack != PISS + 1:
            return 'want a challenge ACK at RCV.NXT, got %r' % ch, w
        w.send(Seg(PORT, gp, PISS + 1, g, ACK | PSH, data=b'still-here'))
        if not w.wait_serial('guest: read', 10):
            return 'guest never read', w
        line = [l for l in w.serial().splitlines() if 'guest: read' in l][0]
        if 'n=10' not in line:
            return 'connection did not survive the SYN: %s' % line.strip(), w
        return None, w


def case_listen_ack():
    hp = 42002
    with Wire.boot('listen %d sleep:60' % PORT) as w:
        if not w.wait_serial('guest: listening', 90):
            return 'guest never listened', w
        w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP)
        w.pump(0.5)
        for flags, ackno in ((ACK, 0x12345678), (SYN | ACK, 0x23456789)):
            w.rx.clear()
            w.send(Seg(hp, PORT, PISS, ackno, flags))
            r = w.expect(lambda s: s.dport == hp, 3, 'reply')
            if not r:
                return 'no reply to %#x at a listener' % flags, w
            if not r.flags & RST or r.seq != ackno or r.flags & SYN:
                return 'want RST seq=%d, got %r' % (ackno, r), w
        return None, w


CASES = (('data-after-fin', case_data_after_fin),
         ('syn-rcvd-rst', case_syn_rcvd_rst),
         ('syn-sync', case_syn_sync),
         ('listen-ack', case_listen_ack))


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
