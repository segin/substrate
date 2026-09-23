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


CASES = (('persist', case_persist),
         ('nb-persist', case_nb_persist))


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
