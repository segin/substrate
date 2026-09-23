#!/usr/bin/env python3
"""
TCP-MEM-11 (docs/ip-audit-2026-09-22.md): listen() on a listening socket
that shrinks the backlog must not orphan the children queued beyond the new
cap.  tcp_listen() just truncated accept_count, so those connections --
fully established from the peer's point of view -- were never accepted,
never reset and never reaped; the peer believed them up forever.  (It also
did all of this without tcp_lock, racing the RX path that appends to the
same queue.)

    shrink  the guest listens with backlog 4; three peers complete the
            handshake; the guest calls listen() again with backlog 1.  The
            two connections queued beyond the new cap are reset, and the
            one that fits is still accepted.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_backlog.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, RST, PEER_MAC, PEER_IP, GUEST_IP  # noqa: E402

PORT = 7040
PEERS = (41001, 41002, 41003)
PISS = 70000


def case_shrink():
    with Wire.boot('relisten %d 4 8 1 sleep:60' % PORT) as w:
        if not w.wait_serial('guest: listening', 90):
            return 'guest never listened', w
        # Teach the guest our MAC so its IRQ-context replies are not dropped
        # on an ARP miss.
        w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP)
        w.pump(0.5)
        for hp in PEERS:
            w.send(Seg(hp, PORT, PISS, 0, SYN))
            sa = w.expect(lambda s, hp=hp: s.dport == hp and
                          s.flags & (SYN | ACK) == SYN | ACK, 5, 'SYN-ACK')
            if not sa:
                return 'no SYN-ACK for peer %d' % hp, w
            w.send(Seg(hp, PORT, PISS + 1, sa.seq + 1, ACK))
        w.pump(0.5)
        if not w.wait_serial('guest: relistened', 20):
            return 'guest never relistened: %s' % w.serial()[-300:], w
        w.pump(1.0)
        reset = sorted({s.dport for s in w.rx if s.flags & RST})
        if reset != list(PEERS[1:]):
            return 'want RST to %r, got RST to %r' % (list(PEERS[1:]), reset), w
        if not w.wait_serial('guest: accepted', 5):
            return 'the connection within the new backlog was not accepted', w
        return None, w


def main():
    err, w = case_shrink()
    if err:
        print('FAIL  shrink: %s' % err)
        print(w.dump()[-2000:])
    else:
        print('ok    shrink')
    print('Result: %s' % ('FAILED' if err else 'PASSED'))
    return 1 if err else 0


if __name__ == '__main__':
    sys.exit(main())
