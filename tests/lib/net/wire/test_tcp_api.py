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
    bind-unique    TCP-API-04: the PCB layer enforces local-socket
                   uniqueness.  With SO_REUSEADDR a second socket may bind
                   the port a connected socket holds, but connecting it to
                   the same peer (a duplicate 4-tuple) fails EADDRINUSE;
                   once the first socket is closed and in TIME-WAIT, a plain
                   bind() of its port fails EADDRINUSE while a SO_REUSEADDR
                   bind() + listen() succeeds.
    find-specific  TCP-API-05: with an address-specific and a (newer)
                   wildcard listener on one port, a SYN to the specific
                   address reaches the specific listener, not the newest.
    listen-connected TCP-API-06: listen() on a connected socket fails
                   EINVAL and the connection keeps working.
    listen-unbound TCP-API-07: listen() on a never-bound socket binds an
                   ephemeral port, getsockname() reports it, and a SYN to
                   it is accepted.

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


def case_bind_unique():
    lport = 45200
    with Wire.boot('bindtest 10.0.2.2 %d %d' % (PORT, lport)) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.sport == lport, 90, 'SYN')
        if not syn:
            return 'no SYN from socket A', w
        w.send(Seg(PORT, lport, PISS, syn.seq + 1, SYN | ACK))
        if not w.wait_serial('guest: connectA ok', 10):
            return 'A did not connect: %s' % line(w, 'connectA'), w
        if not w.wait_serial('guest: connectB', 10):
            return 'no report from B', w
        bb, cb = line(w, 'bindB')[0], line(w, 'connectB')[0]
        if 'bindB ok' not in bb:
            return 'B (SO_REUSEADDR) could not bind beside a connected socket: %s' % bb, w
        if 'already in use' not in cb.lower():
            return 'duplicate 4-tuple connect: %s (want EADDRINUSE)' % cb, w
        fin = w.expect(lambda s: s.flags & FIN and s.sport == lport, 10, 'FIN')
        if not fin:
            return 'A never sent its FIN', w
        w.send(Seg(PORT, lport, PISS + 1, fin.seq + 1, FIN | ACK))
        if not w.expect(lambda s: s.flags & ACK and s.ack == PISS + 2, 5, 'ACK'):
            return 'A did not reach TIME-WAIT', w
        if not w.wait_serial('guest: listenD', 15):
            return 'no report from D', w
        bc, bd, ld = line(w, 'bindC')[0], line(w, 'bindD')[0], line(w, 'listenD')[0]
        if 'already in use' not in bc.lower():
            return 'bind over a TIME-WAIT PCB: %s (want EADDRINUSE)' % bc, w
        if 'bindD ok' not in bd or 'listenD ok' not in ld:
            return 'SO_REUSEADDR rebind over TIME-WAIT: %s / %s' % (bd, ld), w
        return None, w


def dial(w, hp, port):
    """Prime ARP and complete a handshake from host port hp to guest port."""
    w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP)
    w.pump(0.5)
    w.send(Seg(hp, port, PISS, 0, SYN))
    sa = w.expect(lambda s: s.dport == hp and s.flags & SYN, 3, 'SYN-ACK')
    if not sa:
        return 'no SYN|ACK from port %d' % port
    w.send(Seg(hp, port, PISS + 1, sa.seq + 1, ACK))
    return None


def case_find_specific():
    with Wire.boot('listen2 %d' % PORT) as w:
        if not w.wait_serial('guest: listening', 90):
            return 'listeners not up: %s' % line(w, 'listen2'), w
        err = dial(w, 44002, PORT)
        if err:
            return err, w
        if not w.wait_serial('guest: accepted on', 5):
            return 'nobody accepted', w
        a = line(w, 'accepted on')[0]
        if 'specific' not in a:
            return 'the wildcard listener took the SYN: %s' % a, w
        return None, w


def case_listen_connected():
    with Wire.boot('listenafter 10.0.2.2 %d write:still sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
        if not w.wait_serial('guest: listen', 10):
            return 'no listen() report', w
        l = line(w, 'listen ')[0]
        if 'invalid argument' not in l.lower():
            return 'listen() on a connected socket: %s (want EINVAL)' % l, w
        d = w.expect(lambda s: s.data == b'still', 5, 'data')
        if not d:
            return 'the connection did not survive listen()', w
        return None, w


def case_listen_unbound():
    with Wire.boot('listen0 sleep:60') as w:
        if not w.wait_serial('guest: listening', 90):
            return 'guest never listened: %s' % line(w, 'listen'), w
        port = int(line(w, 'port')[0].split()[-1])
        if port == 0:
            return 'listen() without bind() left the port at 0', w
        err = dial(w, 44003, port)
        if err:
            return err, w
        if not w.wait_serial('guest: accepted', 5):
            return 'accept() did not return', w
        return None, w


CASES = (('reconnect', case_reconnect),
         ('connect-twice', case_connect_twice),
         ('listen-connect', case_listen_connect),
         ('bind-unique', case_bind_unique),
         ('find-specific', case_find_specific),
         ('listen-connected', case_listen_connected),
         ('listen-unbound', case_listen_unbound))


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
