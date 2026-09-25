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
    connect-unspec TCP-API-10: connect() to port 0 fails EADDRNOTAVAIL with
                   no SYN on the wire; connect() to 0.0.0.0 goes to the local
                   host (refused at once by loopback), not onto the wire.
    linger-abort   TCP-API-11: with SO_LINGER {1, 0}, close() is an ABORT:
                   one RST at SND.NXT, no FIN, and the unacknowledged data
                   is not retransmitted.
    unread-close   TCP-API-12: close() with received data still unread is
                   an abort (RFC 1122 4.2.2.13): RST, not FIN.
    accept-emfile  TCP-API-12: accept() failing EMFILE on an established
                   child that already holds the peer's acknowledged request
                   resets it rather than sending a FIN.
    write-closing  TCP-API-13: after shutdown(SHUT_WR), write() fails EPIPE
                   (not ENOTCONN) and raises SIGPIPE; send(MSG_NOSIGNAL)
                   fails EPIPE without one.
    early-write    TCP-API-14: a write while the handshake is outstanding
                   returns EAGAIN on a non-blocking socket and, on a
                   blocking one, waits and goes out once established --
                   it used to fail ENOTCONN either way.
    shut-connecting TCP-API-15: shutdown(SHUT_WR) in SYN-SENT aborts the
                   open (SO_ERROR ECONNABORTED; the late SYN|ACK draws a
                   RST, no connection forms); in SYN-RECEIVED (reached by
                   simultaneous open) it sends the FIN.
    send-dontwait  TCP-API-16: send(MSG_DONTWAIT) on a blocking socket facing
                   a closed window returns (the probe octet, then EAGAIN)
                   instead of blocking.
    read-listener  TCP-API-17: read() on a listening socket fails ENOTCONN
                   (as recv() does) instead of blocking forever.
    close-synrcvd  TCP-API-20: close() in SYN-RECEIVED (via simultaneous
                   open) sends a FIN at ISS+1 instead of silently dropping
                   the connection.
    synrst-flood   TCP-API-21: 32 SYN+RST pairs sent back to back at a
                   backlog-4 listener; the dead children count against the
                   backlog until reaped, so far fewer than 32 draw a SYN|ACK
                   (each used to get a fresh child and a SYN|ACK).
    peer-early     TCP-API-22: getpeername() on a socket still in SYN-SENT
                   (a non-blocking connect) fails ENOTCONN; once the
                   handshake completes it reports the peer.
    close-synsent  TCP-API-23: close() while another thread is blocked in
                   connect() (SYN-SENT) wakes it at once with
                   ECONNABORTED -- not ECONNREFUSED on the next poll.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_api.py [case...]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, RST, PSH, PEER_MAC, PEER_IP, GUEST_IP  # noqa: E402

PORT = 7090
PISS = 170000


def line(w, prefix, timeout=3.0):
    """The COMPLETE guest lines starting with prefix.  wait_serial() can
    match a line whose tail has not reached the serial log yet, so a
    trailing line without its newline is not returned; wait up to
    `timeout` for at least one complete match."""
    end = time.time() + timeout
    while True:
        out = w.serial()
        lines = out.splitlines()
        if lines and not out.endswith('\n'):
            lines = lines[:-1]
        got = [l.strip() for l in lines if l.startswith('guest: ' + prefix)]
        if got or time.time() >= end:
            return got
        w.pump(0.2)


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


def case_connect_unspec():
    with Wire.boot('connect 10.0.2.2 0') as w:
        if not w.wait_serial('guest: connect failed', 90):
            return 'connect() to port 0 did not fail', w
        c = line(w, 'connect failed')[0]
        if 'cannot assign' not in c.lower():
            return 'connect() to port 0: %s (want EADDRNOTAVAIL)' % c, w
        if any(s.flags & SYN for s in w.rx):
            return 'a SYN to port 0 went out', w
    with Wire.boot('connect 0.0.0.0 %d' % PORT) as w:
        if not w.wait_serial('guest: connect failed', 90):
            return 'connect() to 0.0.0.0 did not fail', w
        c = line(w, 'connect failed')[0]
        if 'refused' not in c.lower():
            return 'connect() to 0.0.0.0: %s (want ECONNREFUSED from lo)' % c, w
        if any(s.flags & SYN for s in w.rx):
            return 'a SYN to 0.0.0.0 went out on the wire', w
    return None, w


def case_linger_abort():
    with Wire.boot('connect 10.0.2.2 %d linger0 write:unacked close sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
        if not w.wait_serial('guest: close', 10):
            return 'guest never closed', w
        l = line(w, 'linger0')[0]
        if 'ok onoff=1' not in l:
            return 'SO_LINGER not stored: %s' % l, w
        w.pump(3.0)                   # past the first RTO of the data
        g = syn.seq + 1
        fins = [s for s in w.rx if s.flags & FIN]
        rsts = [s for s in w.rx if s.flags & RST]
        if fins:
            return 'close() sent a FIN (graceful), want an abort', w
        if len(rsts) != 1 or rsts[0].seq != g + len('unacked'):
            return 'want one RST at SND.NXT=%d, got %r' % (g + 7, rsts), w
        after = [s for s in w.rx if s.data and s.seq == g and
                 w.rx.index(s) > w.rx.index(rsts[0])]
        if after:
            return 'the discarded data was retransmitted after the RST', w
        return None, w


def case_unread_close():
    with Wire.boot('connect 10.0.2.2 %d sleep:3 close sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        g = syn.seq + 1
        w.send(Seg(PORT, syn.sport, PISS, g, SYN | ACK))
        w.pump(0.5)
        w.send(Seg(PORT, syn.sport, PISS + 1, g, ACK | PSH, data=b'never-read'))
        if not w.wait_serial('guest: close', 10):
            return 'guest never closed', w
        w.pump(1.0)
        if any(s.flags & FIN for s in w.rx):
            return 'close() with unread data sent a FIN', w
        if not any(s.flags & RST for s in w.rx):
            return 'close() with unread data sent no RST', w
        return None, w


def case_accept_emfile():
    with Wire.boot('acceptfull %d' % PORT) as w:
        if not w.wait_serial('guest: listening', 90):
            return 'guest never listened', w
        err = dial(w, 44004, PORT)
        if err:
            return err, w
        w.send(Seg(44004, PORT, PISS + 1, 0, ACK | PSH, data=b'request'))
        w.pump(0.5)
        w.rx.clear()
        if not w.wait_serial('guest: accept', 15):
            return 'accept() never returned', w
        a = line(w, 'accept ')[0]
        if 'too many open files' not in a.lower():
            return 'accept() with a full fd table: %s' % a, w
        w.pump(1.0)
        mine = [s for s in w.rx if s.dport == 44004]
        if any(s.flags & FIN for s in mine):
            return 'the dropped child sent a FIN', w
        if not any(s.flags & RST for s in mine):
            return 'the dropped child sent no RST', w
        return None, w


def case_write_closing():
    with Wire.boot('connect 10.0.2.2 %d catchpipe shutwr sleep:2 write:late '
                   'sigpipe sendns:later sigpipe sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        g = syn.seq + 1
        w.send(Seg(PORT, syn.sport, PISS, g, SYN | ACK))
        fin = w.expect(lambda s: s.flags & FIN, 10, 'FIN')
        if not fin:
            return 'shutdown(SHUT_WR) sent no FIN', w
        w.send(Seg(PORT, syn.sport, PISS + 1, g + 1, ACK))     # -> FIN-WAIT-2
        if not w.wait_serial('guest: sendns', 15):
            return 'guest never finished: %s' % w.serial()[-300:], w
        wr, sp, ns = line(w, 'write')[0], line(w, 'sigpipe'), line(w, 'sendns')[0]
        if 'broken pipe' not in wr.lower():
            return 'write() after SHUT_WR: %s (want EPIPE)' % wr, w
        if len(sp) < 2 or 'count=1' not in sp[0]:
            return 'no SIGPIPE for the write: %s' % sp, w
        if 'broken pipe' not in ns.lower() or 'count=1' not in sp[1]:
            return 'send(MSG_NOSIGNAL): %s, then %s' % (ns, sp[1]), w
        return None, w


def case_early_write():
    with Wire.boot('earlywrite 10.0.2.2 %d early sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        if not w.wait_serial('guest: nbwrite', 10):
            return 'no non-blocking write report', w
        nb = line(w, 'nbwrite')[0]
        if 'temporarily unavailable' not in nb.lower():
            return 'non-blocking write in SYN-SENT: %s (want EAGAIN)' % nb, w
        w.pump(2.0)                              # the blocking write waits
        if line(w, 'bwrite', timeout=0):
            return 'blocking write returned before the handshake: %s' % line(w, 'bwrite')[0], w
        g = syn.seq + 1
        w.send(Seg(PORT, syn.sport, PISS, g, SYN | ACK))
        d = w.expect(lambda s: s.data == b'early', 5, 'data')
        if not d or d.seq != g:
            return 'the queued write never went out: %r' % d, w
        b = line(w, 'bwrite')
        if not b or 'ok n=5' not in b[0]:
            return 'blocking write: %s' % b, w
        return None, w


def case_shut_connecting():
    # SYN-SENT: shut down at once, answer the SYN only afterwards.
    with Wire.boot('shutconnect 10.0.2.2 %d 0' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        if not w.wait_serial('guest: shutwr', 10):
            return 'no shutdown report', w
        w.rx.clear()
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
        r = w.expect(lambda s: s.sport == syn.sport, 3, 'reply')
        if not r or not r.flags & RST:
            return 'SYN|ACK after SHUT_WR in SYN-SENT: %r (want RST)' % r, w
        if not w.wait_serial('guest: soerror', 5):
            return 'no SO_ERROR report', w
        e = line(w, 'soerror')[0]
        if 'value=103' not in e:
            return 'SO_ERROR %s, want ECONNABORTED (103)' % e, w
    # SYN-RECEIVED via simultaneous open: shut down after it is reached.
    with Wire.boot('shutconnect 10.0.2.2 %d 3' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        w.send(Seg(PORT, syn.sport, PISS, 0, SYN))         # -> SYN-RECEIVED
        if not w.expect(lambda s: s.flags & SYN and s.flags & ACK, 3, 'SYN|ACK'):
            return 'no simultaneous-open SYN|ACK', w
        fin = w.expect(lambda s: s.flags & FIN, 8, 'FIN')
        if not fin or fin.seq != syn.seq + 1:
            return 'SHUT_WR in SYN-RECEIVED: want a FIN at ISS+1, got %r' % fin, w
    return None, w


def case_send_dontwait():
    with Wire.boot('connect 10.0.2.2 %d senddw:ab senddw:cd sleep:60' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK, win=0))
        if not w.wait_serial('guest: senddw', 10):
            return 'the first send(MSG_DONTWAIT) never returned', w
        # Each probe is answered with the window still shut.
        w.expect(lambda s: s.data, 3, 'probe')
        w.send(Seg(PORT, syn.sport, PISS + 1, syn.seq + 1, ACK, win=0))
        s = line(w, 'senddw', timeout=5)
        if len(s) < 2:
            return 'send(MSG_DONTWAIT) blocked on a closed window: %s' % s, w
        if 'ok n=1' not in s[0] or 'temporarily unavailable' not in s[1].lower():
            return 'want the probe octet then EAGAIN, got %s' % s, w
        return None, w


def case_read_listener():
    with Wire.boot('listenread %d' % PORT) as w:
        if not w.wait_serial('guest: readlisten', 90):
            return 'read() on a listener never returned', w
        r = line(w, 'readlisten')[0]
        if 'not connected' not in r.lower():
            return 'read() on a listener: %s (want ENOTCONN)' % r, w
        return None, w


def case_close_synrcvd():
    with Wire.boot('shutconnect 10.0.2.2 %d 3 close' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        w.send(Seg(PORT, syn.sport, PISS, 0, SYN))         # -> SYN-RECEIVED
        if not w.expect(lambda s: s.flags & SYN and s.flags & ACK, 3, 'SYN|ACK'):
            return 'no simultaneous-open SYN|ACK', w
        fin = w.expect(lambda s: s.flags & (FIN | RST), 8, 'FIN')
        if not fin or not fin.flags & FIN or fin.seq != syn.seq + 1:
            return 'close() in SYN-RECEIVED: want a FIN at ISS+1, got %r' % fin, w
        return None, w


def case_synrst_flood():
    with Wire.boot('listen %d sleep:60' % PORT) as w:
        if not w.wait_serial('guest: listening', 90):
            return 'guest never listened', w
        w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, GUEST_IP)
        w.pump(0.5)
        w.rx.clear()
        for i in range(32):
            hp = 45000 + i
            w.send(Seg(hp, PORT, PISS, 0, SYN))
            w.send(Seg(hp, PORT, PISS + 1, 0, RST))     # exactly RCV.NXT
        w.pump(1.0)
        answered = {s.dport for s in w.rx if s.flags & SYN and s.flags & ACK}
        if len(answered) >= 16:
            return '%d of 32 SYN+RST pairs got a fresh child (SYN|ACK)' % len(answered), w
        return None, w


def case_peer_early():
    with Wire.boot('peerconnect 10.0.2.2 %d' % PORT) as w:
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN', w
        if not w.wait_serial('guest: peer1', 10):
            return 'no first getpeername report', w
        p1 = line(w, 'peer1')[0]
        if 'not connected' not in p1.lower():
            return 'getpeername() in SYN-SENT: %s (want ENOTCONN)' % p1, w
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
        if not w.wait_serial('guest: peer2', 10):
            return 'no second getpeername report', w
        p2 = line(w, 'peer2')[0]
        if 'ok port=%d' % PORT not in p2:
            return 'getpeername() once established: %s' % p2, w
        return None, w


def case_close_synsent():
    with Wire.boot('closeconnect 10.0.2.2 %d' % PORT) as w:
        if not w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN'):
            return 'no SYN', w
        if not w.wait_serial('guest: took', 30):
            return 'the blocked connect() never returned', w
        rep = line(w, 'connect')[0]
        if 'abort' not in rep.lower():
            return 'connect() after close(): %s (want ECONNABORTED)' % rep, w
        took = int(line(w, 'took')[0].split()[-1])
        if took > 3:
            return 'connect() took %d s to notice the close' % took, w
        return None, w


CASES = (('reconnect', case_reconnect),
         ('connect-twice', case_connect_twice),
         ('listen-connect', case_listen_connect),
         ('bind-unique', case_bind_unique),
         ('find-specific', case_find_specific),
         ('listen-connected', case_listen_connected),
         ('listen-unbound', case_listen_unbound),
         ('connect-unspec', case_connect_unspec),
         ('linger-abort', case_linger_abort),
         ('unread-close', case_unread_close),
         ('accept-emfile', case_accept_emfile),
         ('write-closing', case_write_closing),
         ('early-write', case_early_write),
         ('shut-connecting', case_shut_connecting),
         ('send-dontwait', case_send_dontwait),
         ('read-listener', case_read_listener),
         ('close-synrcvd', case_close_synrcvd),
         ('synrst-flood', case_synrst_flood),
         ('peer-early', case_peer_early),
         ('close-synsent', case_close_synsent))


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
