#!/usr/bin/env python3
"""
RFC 793 3.9 SEGMENT ARRIVES rules for synchronized states
(docs/ip-audit-2026-09-22.md, TCP-B).  Each case names the checklist item
it guards.

    data-after-fin  TCP-SM-03: text the peer sends after its own FIN is not
                    delivered, and RCV.NXT does not move (seventh step:
                    CLOSE-WAIT ignores segment text).

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_input.py [case...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, RST, PSH  # noqa: E402

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


CASES = (('data-after-fin', case_data_after_fin),)


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
