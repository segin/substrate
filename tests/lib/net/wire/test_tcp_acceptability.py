#!/usr/bin/env python3
"""
TCP-SM-02 and TCP-SM-05 (docs/ip-audit-2026-09-22.md): RFC 793 3.9's
"first check sequence number" -- the 3.3 acceptability test, the answer to
an unacceptable segment, and trimming an acceptable one to the window.

    keepalive     an empty ACK one octet below RCV.NXT (what BSD and Linux
                  send as a keepalive) must be answered with an ACK.
    far-data      data far beyond the window is answered with an ACK for
                  RCV.NXT and not delivered.
    zero-window   with the receive ring full, a one-octet probe at RCV.NXT is
                  answered with an ACK carrying window 0 and RCV.NXT unmoved.
    straddle      a segment half old, half new: the new half is accepted.
    partial       the stall: a segment accepted only in part because the ring
                  was nearly full, then retransmitted whole from its original
                  sequence number, must deliver its remainder.  Before the
                  trim it failed seq == RCV.NXT forever.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_acceptability.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, PSH  # noqa: E402

PORT = 7001
PISS = 90000
RING = 32 * 1024
MSS = 1460


def handshake(w):
    syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
    if not syn:
        return None, 'no SYN from guest'
    w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
    if not w.expect(lambda s: s.flags & ACK and not s.flags & SYN, 5, 'ACK'):
        return None, 'handshake ACK missing'
    return syn, None


def data(n, off=0):
    return bytes((off + i) & 0xFF for i in range(n))


def acks(w, want_ack, secs=3, win=None):
    return w.expect(lambda s: s.flags & ACK and s.ack == want_ack and
                    (win is None or s.win == win), secs, 'ACK %d' % want_ack)


def fill(w, gp, g, rcv, n):
    """Send n octets from rcv in MSS segments, pacing on the guest's ACKs.
    Returns the new RCV.NXT, or None."""
    sent = 0
    while sent < n:
        k = min(MSS, n - sent)
        w.send(Seg(PORT, gp, rcv + sent, g, ACK | PSH, data=data(k, sent)))
        sent += k
        if not acks(w, rcv + sent):
            return None
    return rcv + sent


def case_keepalive():
    with Wire.boot('connect 10.0.2.2 %d sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g, rcv = syn.sport, syn.seq + 1, PISS + 1
        w.rx.clear()
        w.send(Seg(PORT, gp, rcv - 1, g, ACK))
        if not acks(w, rcv):
            return 'keepalive (SEG.SEQ = RCV.NXT-1) was not answered', w
        return None, w


def case_far_data():
    with Wire.boot('connect 10.0.2.2 %d sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g, rcv = syn.sport, syn.seq + 1, PISS + 1
        w.rx.clear()
        w.send(Seg(PORT, gp, rcv + 200000, g, ACK | PSH, data=b'x' * 100))
        if not acks(w, rcv):
            return 'out-of-window data was not answered with ACK(RCV.NXT)', w
        return None, w


def case_zero_window():
    with Wire.boot('connect 10.0.2.2 %d sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g, rcv = syn.sport, syn.seq + 1, PISS + 1
        rcv = fill(w, gp, g, rcv, RING)          # the guest never reads
        if rcv is None:
            return 'could not fill the receive ring', w
        w.rx.clear()
        w.send(Seg(PORT, gp, rcv, g, ACK | PSH, data=b'p'))
        if not acks(w, rcv, win=0):
            return 'zero-window probe not answered with ACK(RCV.NXT, win 0)', w
        return None, w


def case_straddle():
    with Wire.boot('connect 10.0.2.2 %d readeof' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g, rcv = syn.sport, syn.seq + 1, PISS + 1
        rcv2 = fill(w, gp, g, rcv, 1000)
        if rcv2 is None:
            return 'first 1000 octets not acknowledged', w
        # [rcv+500, rcv+1500): 500 old octets, 500 new.
        w.send(Seg(PORT, gp, rcv + 500, g, ACK | PSH, data=data(1000, 500)))
        if not acks(w, rcv + 1500):
            return 'straddling segment: new half not accepted', w
        w.send(Seg(PORT, gp, rcv + 1500, g, FIN | ACK))
        if not w.wait_serial('readeof EOF total=1500', 10):
            return 'guest did not read exactly 1500 octets', w
        return None, w


def case_partial():
    with Wire.boot('connect 10.0.2.2 %d sleep:6 readeof' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g, rcv = syn.sport, syn.seq + 1, PISS + 1
        rcv = fill(w, gp, g, rcv, RING - 100)    # 100 octets of room left
        if rcv is None:
            return 'could not nearly fill the receive ring', w
        seg_seq = rcv
        w.send(Seg(PORT, gp, seg_seq, g, ACK | PSH, data=data(MSS, 7)))
        if not acks(w, seg_seq + 100):
            return 'the first 100 octets of the segment were not accepted', w
        # The guest starts reading after its sleep; the ring drains.
        if not w.wait_serial('slept 6', 15):
            return 'guest never woke up to read', w
        w.pump(1.0)
        # Retransmit the WHOLE segment from its original sequence number.
        w.rx.clear()
        w.send(Seg(PORT, gp, seg_seq, g, ACK | PSH, data=data(MSS, 7)))
        if not acks(w, seg_seq + MSS):
            return 'retransmitted segment was not trimmed and accepted (stall)', w
        w.send(Seg(PORT, gp, seg_seq + MSS, g, FIN | ACK))
        total = RING - 100 + MSS
        if not w.wait_serial('readeof EOF total=%d' % total, 10):
            return 'guest did not read exactly %d octets' % total, w
        return None, w


def main():
    failed = 0
    cases = (('keepalive', case_keepalive), ('far-data', case_far_data),
             ('zero-window', case_zero_window), ('straddle', case_straddle),
             ('partial', case_partial))
    only = sys.argv[1:]
    for name, fn in cases:
        if only and name not in only:
            continue
        err, w = fn()
        if err:
            failed += 1
            print('FAIL  %s: %s' % (name, err))
            print(w.dump()[-3000:])
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
