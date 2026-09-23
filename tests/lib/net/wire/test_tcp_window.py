#!/usr/bin/env python3
"""
RFC 793 3.7 window management and retransmission
(docs/ip-audit-2026-09-22.md, TCP-C).  Each case names the checklist item
it guards.

    persist     TCP-WIN-01: a peer that holds a zero window, answering
                every probe, for longer than the retransmission abort budget
                (~2 minutes) does not make the sender give up; once the
                window reopens the whole write goes through.
    nb-persist  TCP-WIN-02: a non-blocking write facing a zero window sends
                the one-octet probe (instead of EAGAIN and silence); poll()
                then withholds POLLOUT until the peer opens the window, and
                is woken when it does.
    reopen      TCP-WIN-03: the peer fills the receive window to zero; the
                application drains it with 512-octet reads, and a window
                update is sent (the 0 -> non-zero transition is always
                announced, and after that every MSS freed).  It used to
                need one single read() to free an MSS.
    fast-retx   TCP-WIN-04: seven duplicate-ACK episodes (fast
                retransmits, capped per segment) must not consume the RTO backoff or the abort
                budget: when the peer then goes quiet the next timeout
                retransmission comes after the base RTO, not the 60 s cap,
                and the connection survives to deliver everything.
    dupack      TCP-WIN-05: only RFC 5681's duplicate ACK counts -- no data,
                no SYN/FIN, ACK = SND.UNA with data outstanding, and an
                unchanged window.  Three ACK-only-repeating data segments,
                and three window updates, trigger no fast retransmit; three
                true duplicates do.
    sender-sws  TCP-WIN-06: with the peer's window at 1000 and 900 octets
                unacknowledged, an ACK that frees 100 octets does not draw
                a 100-octet segment (sender silly-window avoidance); the ACK
                of everything then releases a full window.
    receiver-sws TCP-WIN-07: after a zero window, draining 512 octets does
                not advertise a 512-octet window; the window reopens only
                once at least an MSS is free.
    reorder     TCP-WIN-08: segments arriving out of order are queued, not
                dropped.  The second segment first draws a duplicate ACK;
                the first then completes both, acknowledged in one step,
                and a FIN that arrived ahead of a gap is honoured once the
                gap fills.
    partial-ack TCP-WIN-09: after the peer acknowledges part of a segment,
                the retransmission starts at SND.UNA and carries only the
                unacknowledged octets.
    shut-rd     TCP-WIN-10: after shutdown(SHUT_RD) arriving data is
                acknowledged and discarded, so the window stays open: the
                peer can send more than a ring's worth.
    stale-wnd   TCP-WIN-12: SND.WL1/WL2.  A newer segment closes the window;
                an older one (lower SEG.SEQ, delivered late) advertising a
                large window must not reopen it -- the sender probes with
                one octet instead of sending into the stale window.
    user-timeout TCP-WIN-13: TCP_USER_TIMEOUT (RFC 793 3.8's per-connection
                user timeout) is honoured: with 3000 ms set and the peer
                silent, the connection is aborted with ETIMEDOUT after about
                3 s instead of retransmitting for ~2 minutes.
    rtt         TCP-WIN-14: the RTO is derived from the measured RTT (RFC
                6298).  With the handshake and one segment each answered
                after 0.8 s, RTO = SRTT + 4*RTTVAR = 2.0 s; the next,
                unacknowledged, segment is retransmitted after about that,
                not after the fixed 1 s.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_window.py [case...]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, FIN, RST, PSH  # noqa: E402

PORT = 7060
PISS = 110000


def handshake(w, win=65535):
    syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
    if not syn:
        return None, 'no SYN from guest'
    w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK, win=win))
    if not w.expect(lambda s: s.flags & ACK and not s.flags & SYN, 5, 'ACK'):
        return None, 'handshake ACK missing'
    if not w.wait_serial('guest: connected', 10):
        return None, 'guest did not report connected'
    return syn, None


def case_persist():
    msg = 'persist-through-a-long-zero-window'
    with Wire.boot('connect 10.0.2.2 %d write:%s sleep:60' % (PORT, msg)) as w:
        syn, err = handshake(w, win=0)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        probes = 0
        end = time.time() + 150
        while time.time() < end:
            s = w.expect(lambda s: s.data or s.flags & RST, 5, 'probe')
            if s is None:
                continue
            if s.flags & RST:
                return 'the sender reset the connection after %d probes' % probes, w
            probes += 1
            w.send(Seg(PORT, gp, PISS + 1, g, ACK, win=0))    # still closed
            w.rx.clear()
            if 'guest: write' in w.serial():
                break
        if 'guest: write' in w.serial():
            line = [l for l in w.serial().splitlines() if 'guest: write' in l][0]
            return 'write finished while the window was closed: %s' % line.strip(), w
        # Reopen and take everything.
        w.send(Seg(PORT, gp, PISS + 1, g, ACK, win=65535))
        got = b''
        end = time.time() + 30
        while len(got) < len(msg) and time.time() < end:
            s = w.expect(lambda s: s.data, 3, 'data')
            if not s:
                continue
            off = (s.seq - g) & 0xFFFFFFFF
            if off <= len(got) < off + len(s.data):
                got += s.data[len(got) - off:]
            w.send(Seg(PORT, gp, PISS + 1, g + len(got), ACK, win=65535))
        if got != msg.encode():
            return 'after reopening got %r (%d probes)' % (got, probes), w
        if not w.wait_serial('guest: write ok n=%d' % len(msg), 10):
            return 'write did not complete: %s' % w.serial()[-200:], w
        return None, w


def case_nb_persist():
    with Wire.boot('connect 10.0.2.2 %d nbwrite:abcdef pollout:8000 '
                   'nbwrite:bcdef sleep:60' % PORT) as w:
        syn, err = handshake(w, win=0)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        if not w.wait_serial('guest: nbwrite', 10):
            return 'guest never wrote', w
        line = [l for l in w.serial().splitlines() if 'guest: nbwrite' in l][0]
        probe = w.expect(lambda s: s.data, 3, 'probe')
        if 'n=1' not in line or not probe or probe.data != b'a':
            return 'non-blocking write sent no probe: %s, %r' % (line.strip(), probe), w
        w.send(Seg(PORT, gp, PISS + 1, g, ACK, win=0))
        w.pump(2.0)
        if 'guest: pollout' in w.serial():
            line = [l for l in w.serial().splitlines() if 'guest: pollout' in l][0]
            return 'POLLOUT asserted against a zero window: %s' % line.strip(), w
        w.rx.clear()                               # drop probe retransmissions
        w.send(Seg(PORT, gp, PISS + 1, g + 1, ACK, win=65535))   # ACK probe, open
        t0 = time.time()
        if not w.wait_serial('guest: pollout ok', 5):
            return 'opening the window did not wake poll(): %s' % w.serial()[-200:], w
        if time.time() - t0 > 2:
            return 'poll() woke only after %.1fs' % (time.time() - t0), w
        d = w.expect(lambda s: s.data and s.seq == g + 1, 5, 'data')
        if not d or d.data != b'bcdef':
            return 'no data after the window opened: %r' % d, w
        return None, w


def fill(w, gp, g, total):
    """Send `total` octets as the guest's window allows; return RCV.NXT."""
    nxt, wnd = PISS + 1, 1460
    end = time.time() + 20
    while nxt - (PISS + 1) < total and time.time() < end:
        n = min(1460, wnd, total - (nxt - PISS - 1))
        if n > 0:
            w.rx.clear()
            w.send(Seg(PORT, gp, nxt, g, ACK, data=b'x' * n))
        a = w.expect(lambda s: s.flags & ACK and s.ack >= nxt + max(n, 0), 2, 'ACK')
        if a:
            nxt, wnd = a.ack, a.win
    return nxt


def case_reopen():
    with Wire.boot('connect 10.0.2.2 %d sleep:8 readn:512:4096 sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        nxt = fill(w, gp, g, 32768)
        if nxt != PISS + 1 + 32768:
            return 'could not fill the window: got to %d' % (nxt - PISS - 1), w
        w.rx.clear()
        if not w.wait_serial('guest: readn', 15):
            return 'guest never drained', w
        w.pump(1.0)
        upd = [s for s in w.rx if s.flags & ACK and s.win > 0]
        if not upd:
            return 'no window update after draining 4096 octets in 512s', w
        if max(u.win for u in upd) <= 4096 - 1460:   # within an MSS of the room
            return 'window updates stopped short of the drained room: %r' % upd, w
        return None, w


def case_fast_retx():
    msg = 'F' * 150                  # fits the kernel command line
    with Wire.boot('connect 10.0.2.2 %d write:%s sleep:60' % (PORT, msg)) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        if not w.expect(lambda s: s.data, 5, 'data'):
            return 'guest sent nothing', w
        for ep in range(7):
            w.rx.clear()
            for _ in range(3):
                w.send(Seg(PORT, gp, PISS + 1, g, ACK))
            # Fast retransmits per segment are capped, so only the first
            # episodes must produce one.
            if not w.expect(lambda s: s.data and s.seq == g, 2, 'fast retransmit') \
                    and ep == 0:
                return 'no fast retransmit on three duplicate ACKs', w
        w.rx.clear()
        t0 = time.time()
        r = w.expect(lambda s: s.data and s.seq == g, 8, 'RTO retransmit')
        if not r:
            return 'no timeout retransmission within 8 s of the last episode', w
        got = b''
        end = time.time() + 20
        while len(got) < len(msg) and time.time() < end:
            s = w.expect(lambda s: s.data, 3, 'data')
            if not s:
                continue
            off = (s.seq - g) & 0xFFFFFFFF
            if off <= len(got) < off + len(s.data):
                got += s.data[len(got) - off:]
            w.send(Seg(PORT, gp, PISS + 1, g + len(got), ACK))
        if len(got) != len(msg):
            return 'got %d of %d octets' % (len(got), len(msg)), w
        return None, w


def case_dupack():
    with Wire.boot('connect 10.0.2.2 %d write:outstanding sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        if not w.expect(lambda s: s.data, 5, 'data'):
            return 'guest sent nothing', w
        # Wait out the first RTO: the next one is 2 s away, room to test in.
        if not w.expect(lambda s: s.data and s.seq == g, 3, 'first RTO'):
            return 'no first RTO retransmission', w
        w.rx.clear()
        rn = PISS + 1
        for i in range(3):                         # data, repeating the ACK
            w.send(Seg(PORT, gp, rn, g, ACK | PSH, data=b'd%d' % i))
            rn += 2
        w.pump(0.4)
        if any(s.data for s in w.rx):
            return 'data segments counted as duplicate ACKs', w
        for win in (60000, 50000, 40000):          # window updates
            w.send(Seg(PORT, gp, rn, g, ACK, win=win))
        w.pump(0.4)
        if any(s.data for s in w.rx):
            return 'window updates counted as duplicate ACKs', w
        for _ in range(3):                         # true duplicates
            w.send(Seg(PORT, gp, rn, g, ACK, win=40000))
        if not w.expect(lambda s: s.data and s.seq == g, 0.5, 'fast retransmit'):
            return 'three true duplicate ACKs triggered nothing', w
        return None, w


def case_sender_sws():
    msg = 'S' * 200
    with Wire.boot('connect 10.0.2.2 %d write:%s sleep:60' % (PORT, msg)) as w:
        syn, err = handshake(w, win=150)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        d = w.expect(lambda s: s.data, 5, 'data')
        if not d or len(d.data) != 150:
            return 'want a first segment filling the 150 window, got %r' % d, w
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 1, g + 10, ACK, win=150))   # frees 10
        w.pump(0.5)
        small = [s for s in w.rx if s.data and s.seq == g + 150]
        if small:
            return 'sent a %d-octet silly segment' % len(small[0].data), w
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 1, g + 150, ACK, win=150))  # frees all
        d = w.expect(lambda s: s.data and s.seq == g + 150, 3, 'rest')
        if not d or len(d.data) != 50:
            return 'the rest did not follow the full ACK: %r' % d, w
        return None, w


def case_receiver_sws():
    with Wire.boot('connect 10.0.2.2 %d sleep:8 readn:512:512 sleep:3 '
                   'readn:512:1536 sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        nxt = fill(w, gp, g, 32768)
        if nxt != PISS + 1 + 32768:
            return 'could not fill the window: got to %d' % (nxt - PISS - 1), w
        w.rx.clear()
        if not w.wait_serial('guest: readn ok total=512', 15):
            return 'guest never drained', w
        w.pump(1.0)
        silly = [s for s in w.rx if s.flags & ACK and 0 < s.win < 1460]
        if silly:
            return 'advertised a silly window: %r' % silly, w
        if not w.wait_serial('guest: readn ok total=1536', 10):
            return 'guest never drained the rest', w
        w.pump(1.0)
        if not [s for s in w.rx if s.flags & ACK and s.win >= 1460]:
            return 'window never reopened after an MSS was free: %r' % w.rx, w
        return None, w


def case_reorder():
    with Wire.boot('connect 10.0.2.2 %d readeof sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        a, b, c = b'A' * 100, b'B' * 100, b'C' * 100
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 101, g, ACK, data=b))           # 2nd first
        d = w.expect(lambda s: s.flags & ACK, 2, 'dup ACK')
        if not d or d.ack != PISS + 1:
            return 'out-of-order segment: want a dup ACK %d, got %r' % (PISS + 1, d), w
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 1, g, ACK, data=a))             # fills the gap
        if not w.expect(lambda s: s.flags & ACK and s.ack == PISS + 201, 2, 'ACK'):
            return 'the queued segment was not delivered with the first: %r' % w.rx, w
        w.send(Seg(PORT, gp, PISS + 301, g, ACK | FIN, data=b'D' * 10))  # FIN early
        w.pump(0.5)
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 201, g, ACK, data=c))
        if not w.expect(lambda s: s.flags & ACK and s.ack == PISS + 312, 2, 'ACK of FIN'):
            return 'queued FIN not honoured when the gap filled: %r' % w.rx, w
        if not w.wait_serial('guest: readeof', 5):
            return 'no EOF', w
        line = [l for l in w.serial().splitlines() if 'guest: readeof' in l][0]
        if 'EOF total=310' not in line:
            return 'reassembled stream: %s' % line.strip(), w
        return None, w


def case_partial_ack():
    msg = 'P' * 150
    with Wire.boot('connect 10.0.2.2 %d write:%s sleep:60' % (PORT, msg)) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        if not w.expect(lambda s: s.data, 5, 'data'):
            return 'guest sent nothing', w
        w.rx.clear()
        w.send(Seg(PORT, gp, PISS + 1, g + 50, ACK))
        r = w.expect(lambda s: s.data, 4, 'retransmission')
        if not r:
            return 'no retransmission', w
        if r.seq != g + 50 or len(r.data) != 100:
            return 'want seq %d len 100, got %r' % (g + 50, r), w
        return None, w


def case_shut_rd():
    with Wire.boot('connect 10.0.2.2 %d shutrd sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        if not w.wait_serial('guest: shutrd ok', 10):
            return 'shutdown(SHUT_RD) failed: %s' % w.serial()[-200:], w
        nxt = fill(w, gp, g, 40000)
        if nxt != PISS + 1 + 40000:
            return 'window closed after %d octets despite SHUT_RD' % (nxt - PISS - 1), w
        return None, w


def case_stale_wnd():
    msg = 'W' * 200
    with Wire.boot('connect 10.0.2.2 %d sleep:3 write:%s sleep:60' % (PORT, msg)) as w:
        syn, err = handshake(w, win=150)
        if err:
            return err, w
        gp, g = syn.sport, syn.seq + 1
        d = w.expect(lambda s: s.data, 8, 'data')
        if not d or len(d.data) != 150:
            return 'want 150 octets into the 150 window, got %r' % d, w
        w.rx.clear()
        # Newer segment (seq PISS+11) first: acks everything, window 0.
        w.send(Seg(PORT, gp, PISS + 11, g + 150, ACK, data=b'n' * 10, win=0))
        w.pump(0.3)
        w.rx.clear()
        # Older segment (seq PISS+1) late: same ACK, stale open window.  (3000,
        # so it is also the largest window offered and the sender's
        # silly-window rule would let the tail through if it were believed.)
        w.send(Seg(PORT, gp, PISS + 1, g + 150, ACK, data=b'o' * 10, win=3000))
        d = w.expect(lambda s: s.data, 3, 'next data')
        if not d:
            return 'sender sent nothing at all', w
        if len(d.data) != 1:
            return 'sent %d octets into a stale window' % len(d.data), w
        return None, w


def case_user_timeout():
    with Wire.boot('connect 10.0.2.2 %d utimeout:3000 write:unanswered '
                   'sleep:7 soerror sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        if not w.wait_serial('guest: utimeout', 10):
            return 'guest never set the option', w
        line = [l for l in w.serial().splitlines() if 'guest: utimeout' in l][0]
        if 'ok ms=3000' not in line:
            return 'TCP_USER_TIMEOUT not accepted: %s' % line.strip(), w
        if not w.wait_serial('guest: soerror', 20):
            return 'guest never checked SO_ERROR', w
        line = [l for l in w.serial().splitlines() if 'guest: soerror' in l][0]
        if 'value=110' not in line:                     # ETIMEDOUT
            return 'not aborted by the user timeout: %s' % line.strip(), w
        return None, w


def case_rtt():
    with Wire.boot('connect 10.0.2.2 %d sleep:3 write:one sleep:3 write:two '
                   'sleep:60' % PORT) as w:
        # Every RTT is 0.8 s, the handshake's included (it is sampled too).
        syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
        if not syn:
            return 'no SYN from guest', w
        w.pump(0.8)
        w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK))
        if not w.wait_serial('guest: connected', 10):
            return 'guest did not report connected', w
        gp, g = syn.sport, syn.seq + 1
        d = w.expect(lambda s: s.data == b'one', 10, 'one')
        if not d:
            return 'no first segment', w
        w.pump(0.8)                                   # the RTT sample
        w.send(Seg(PORT, gp, PISS + 1, g + 3, ACK))
        d = w.expect(lambda s: s.data == b'two', 6, 'two')
        if not d:
            return 'no second segment', w
        t0 = time.time()
        r = w.expect(lambda s: s.data == b'two', 6, 'retransmission')
        if not r:
            return 'no retransmission within 6 s', w
        dt = time.time() - t0
        if not 1.7 <= dt <= 3.5:
            return 'retransmitted after %.2f s; want ~2.0 s from 0.8 s samples' % dt, w
        return None, w


CASES = (('persist', case_persist),
         ('nb-persist', case_nb_persist),
         ('reopen', case_reopen),
         ('fast-retx', case_fast_retx),
         ('dupack', case_dupack),
         ('sender-sws', case_sender_sws),
         ('receiver-sws', case_receiver_sws),
         ('reorder', case_reorder),
         ('partial-ack', case_partial_ack),
         ('shut-rd', case_shut_rd),
         ('stale-wnd', case_stale_wnd),
         ('user-timeout', case_user_timeout),
         ('rtt', case_rtt))


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
