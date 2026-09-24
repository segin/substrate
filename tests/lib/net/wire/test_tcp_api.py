#!/usr/bin/env python3
"""
The user interface: OPEN/CLOSE/SEND semantics on a live socket
(docs/ip-audit-2026-09-22.md, TCP-F).  Each case names the checklist item
it guards.

    reconnect      TCP-API-01: after a connect() the peer refused with a RST,
                   connect() on the same socket starts a clean new
                   connection: a fresh SYN with a new ISS and no stray
                   retransmission of the old one, and it completes.
    connect-twice  TCP-API-02: a second connect() while the first is still
                   in SYN-SENT fails EALREADY; once established, EISCONN;
                   the handshake is not restarted (one ISS on the wire).
    listen-connect TCP-API-03: connect() on a listening socket fails
                   EOPNOTSUPP and the socket still accepts.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_api.py [case...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, RST, PSH, PEER_MAC, PEER_IP, GUEST_IP  # noqa: E402

PORT = 7090
PISS = 170000


def line(w, prefix):
    return [l.strip() for l in w.serial().splitlines() if l.startswith('guest: ' + prefix)]


def case_reconnect():
    with Wire.boot('reconnect 10.0.2.2 %d write:again sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no first SYN', w
        # Refuse it.
        w.send(Seg(PORT, syn.sport, 0, syn.seq + 1, RST | ACK))
        if not w.wait_serial('guest: connect1 Connection refused', 10):
            return 'first connect not refused: %s' % line(w, 'connect1'), w
        w.rx.clear()
        syn2 = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 10, 'second SYN')
        if not syn2:
            return 'no SYN for the second connect', w
        if syn2.seq == syn.seq:
            return 'second connect reused the old ISS', w
        w.send(Seg(PORT, syn2.sport, PISS, syn2.seq + 1, SYN | ACK))
        if not w.wait_serial('guest: connect2 ok', 10):
            return 'second connect did not complete: %s' % line(w, 'connect2'), w
        d = w.expect(lambda s: s.data == b'again', 5, 'data')
        if not d or d.seq != syn2.seq + 1:
            return 'data not sequenced from the new ISS: %r' % d, w
        w.pump(3.0)
        stale = [s for s in w.rx if s.flags & SYN and s.seq == syn.seq]
        if stale:
            return 'the old SYN is still being retransmitted', w
        return None, w


def case_connect_twice():
    with Wire.boot('nbconnect 10.0.2.2 %d sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        if not w.wait_serial('guest: connect2', 10):
            return 'no second connect', w
        c1, c2 = line(w, 'connect1')[0], line(w, 'connect2')[0]
        if 'Operation now in progress' not in c1 and 'in progress' not in c1.lower():
            return 'first connect: %s' % c1, w
        if 'already in progress' not in c2.lower():
            return 'second connect in SYN-SENT: %s (want EALREADY)' % c2, w
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
        if not w.wait_serial('guest: connect3', 10):
            return 'no third connect', w
        c3 = line(w, 'connect3')[0]
        if 'already connected' not in c3.lower():
            return 'connect on an established socket: %s (want EISCONN)' % c3, w
        # expect() consumed the first SYN; anything still queued (a
        # retransmission, or a restarted handshake) must carry the same ISS.
        w.pump(0.5)
        syns = {s.seq for s in w.rx if s.flags & SYN} | {syn.seq}
        if len(syns) != 1:
            return 'the handshake was restarted: ISSs %r' % syns, w
        return None, w


def case_listen_connect():
    with Wire.boot('listenconnect %d sleep:60' % PORT) as w:
        if not w.wait_serial('guest: connect1', 90):
            return 'connect() on the listener did not return within 90 s', w
        c1 = line(w, 'connect1')[0]
        if 'not supported' not in c1.lower():
            return 'connect on a listener: %s (want EOPNOTSUPP)' % c1, w
        w.pump(1.0)
        if any(s.flags & SYN and s.dport == PORT for s in w.rx):
            return 'the listener sent a SYN', w
        w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP)
        w.pump(0.5)
        w.send(Seg(44001, PORT, PISS, 0, SYN))
        sa = w.expect(lambda s: s.dport == 44001 and s.flags & SYN, 3, 'SYN-ACK')
        if not sa:
            return 'the listener no longer accepts', w
        w.send(Seg(44001, PORT, PISS + 1, sa.seq + 1, ACK))
        if not w.wait_serial('guest: accepted', 5):
            return 'accept() did not return', w
        return None, w


CASES = (('reconnect', case_reconnect),
         ('connect-twice', case_connect_twice),
         ('listen-connect', case_listen_connect))


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
