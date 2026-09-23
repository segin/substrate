#!/usr/bin/env python3
"""
TCP-SM-01 (docs/ip-audit-2026-09-22.md): a segment that retransmits the
peer's FIN and also acknowledges ours must complete CLOSING and LAST-ACK.

tcp_in_established() processed the ACK field of a retransmitted FIN and then
returned, before the tests that complete CLOSING -> TIME-WAIT and LAST-ACK
-> CLOSED.  With our FIN acknowledged the retransmit queue was empty, so the
timer had nothing to do either: one lost ACK left the PCB, its ring and its
port wedged for good.  The peer below sends exactly that combined segment.

Since TCP-SM-02 the combined segment is unacceptable (its FIN lies below
RCV.NXT), so it is answered and dropped whole and our FIN stays queued; the
guest must then retransmit its FIN, which the peer acknowledges properly.
Either way the guest must not be left in a closing state with nothing to
send -- which is exactly what the wedge looked like on the wire.

    last-ack  guest closes passively (peer FIN first).  Once complete the PCB
              is gone, so a stray ACK draws a RST.
    closing   simultaneous close.  Once complete the PCB is in TIME-WAIT,
              where an in-window SYN draws the RFC 5961 challenge ACK.
              (CLOSING answers an in-window SYN with nothing -- TCP-SM-06; if
              that changes, this discriminator has to change with it.)

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_closing.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, RST  # noqa: E402

PORT = 7000
PISS = 50000


def finish(w, gp, g):
    """After the combined segment: acknowledge a retransmission of the
    guest's FIN if one comes.  A wedged guest sends nothing at all."""
    fin = w.expect(lambda s: s.flags & FIN and s.seq == g, 8, 'FIN retransmit')
    if fin:
        w.send(Seg(PORT, gp, PISS + 2, g + 1, ACK))
    w.pump(1.0)


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


def case_last_ack():
    with Wire.boot('connect 10.0.2.2 %d readeof close sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1          # guest port, guest snd_nxt
        # Peer closes first: guest -> CLOSE-WAIT.
        w.send(Seg(PORT, gp, PISS + 1, g, FIN | ACK))
        if not w.expect(lambda s: s.flags & ACK and s.ack == PISS + 2, 5, 'ACK of FIN'):
            return 'guest did not ACK our FIN', w
        # Guest reads EOF and closes: its FIN -> LAST-ACK.
        fin = w.expect(lambda s: s.flags & FIN, 10, 'guest FIN')
        if not fin or fin.seq != g:
            return 'guest FIN missing or misnumbered: %r' % (fin,), w
        # The combined segment: our FIN again (retransmit), now ACKing theirs.
        w.send(Seg(PORT, gp, PISS + 1, g + 1, FIN | ACK))
        finish(w, gp, g)
        # A stale ACK: a live LAST-ACK PCB swallows it, a closed one RSTs it.
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 2, g, ACK))
        rst = w.expect(lambda s: s.flags & RST, 3, 'RST')
        if not rst:
            return 'LAST-ACK not completed: stray ACK drew no RST', w
        return None, w


def case_closing():
    with Wire.boot('connect 10.0.2.2 %d sleep:3 close sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        fin = w.expect(lambda s: s.flags & FIN, 15, 'guest FIN')     # FIN-WAIT-1
        if not fin or fin.seq != g:
            return 'guest FIN missing or misnumbered: %r' % (fin,), w
        # Our FIN, NOT acknowledging theirs: simultaneous close -> CLOSING.
        w.send(Seg(PORT, gp, PISS + 1, g, FIN | ACK))
        if not w.expect(lambda s: s.flags & ACK and s.ack == PISS + 2, 5, 'ACK of FIN'):
            return 'guest did not ACK our FIN', w
        # The combined segment: FIN retransmit that also ACKs theirs.
        w.send(Seg(PORT, gp, PISS + 1, g + 1, FIN | ACK))
        finish(w, gp, g)
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 2, 0, SYN))
        if not w.expect(lambda s: s.flags & ACK and not s.flags & (SYN | RST), 3,
                        'challenge ACK'):
            return 'CLOSING not completed: SYN drew no TIME-WAIT challenge ACK', w
        return None, w


def main():
    failed = 0
    for name, fn in (('last-ack', case_last_ack), ('closing', case_closing)):
        err, w = fn()
        if err:
            failed += 1
            print('FAIL  %s: %s' % (name, err))
            print(w.dump())
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
