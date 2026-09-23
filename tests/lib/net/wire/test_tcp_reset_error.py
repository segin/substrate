#!/usr/bin/env python3
"""
TCP-SM-08 (docs/ip-audit-2026-09-22.md): a connection killed by a RST or by
the retransmission budget must be reported to read() as an error, not as a
clean end-of-file.

tcp_kill_pcb() records ECONNRESET / ETIMEDOUT in so_error and moves the PCB
to CLOSED, but tcp_recv_nb() treated CLOSED like any post-FIN state and
returned 0.  A client reading a response therefore took a reset for a
complete, cleanly terminated reply.

    reset     100 octets, then a RST at exactly RCV.NXT: read() delivers the
              100 octets and then fails ECONNRESET.
    timeout   the guest writes and the peer never acknowledges: once the
              retransmission budget is spent, read() fails ETIMEDOUT.
              (Slow: the budget is about two minutes.)

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_reset_error.py [reset|timeout]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, RST, PSH  # noqa: E402

PORT = 7002
PISS = 70000


def handshake(w):
    syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
    if not syn:
        return None, 'no SYN from guest'
    w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
    if not w.expect(lambda s: s.flags & ACK and not s.flags & SYN, 5, 'ACK'):
        return None, 'handshake ACK missing'
    return syn, None


def case_reset():
    with Wire.boot('connect 10.0.2.2 %d readeof' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g, rcv = syn.sport, syn.seq + 1, PISS + 1
        w.send(Seg(PORT, gp, rcv, g, ACK | PSH, data=b'r' * 100))
        if not w.expect(lambda s: s.flags & ACK and s.ack == rcv + 100, 3, 'ACK'):
            return 'data not acknowledged', w
        w.send(Seg(PORT, gp, rcv + 100, g, RST))
        if not w.wait_serial('guest: readeof', 10):
            return 'read() never returned after the RST', w
        line = [l for l in w.serial().splitlines() if 'guest: readeof' in l][0]
        if 'EOF' in line or 'total=100' not in line or 'reset' not in line.lower():
            return 'expected 100 octets then ECONNRESET, got: %s' % line.strip(), w
        return None, w


def case_timeout():
    with Wire.boot('connect 10.0.2.2 %d write:hello readeof' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        # Never acknowledge anything the guest sends.
        if not w.wait_serial('guest: readeof', 240):
            return 'read() never returned after the retransmission budget', w
        line = [l for l in w.serial().splitlines() if 'guest: readeof' in l][0]
        if 'EOF' in line or 'timed out' not in line.lower():
            return 'expected ETIMEDOUT, got: %s' % line.strip(), w
        return None, w


def main():
    failed = 0
    only = sys.argv[1:]
    for name, fn in (('reset', case_reset), ('timeout', case_timeout)):
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
