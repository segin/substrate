#!/usr/bin/env python3
"""
sbin/dhclient against a scripted DHCP server on the wire
(docs/dhclient-audit-2026-09.md).  The guest runs the freshly built
sbin/dhclient/dhclient (written into the image as /sbin/dhclient) under
'wireguest run'; this side plays the server, answering each DHCP message
exactly as the case needs.

    bound         DISCOVER -> OFFER -> REQUEST -> ACK: dhclient binds the
                  offered address, and the guest then answers ARP for it.
    clock-step    DHC-14: the wall clock is stepped forward an hour every
                  200 ms while dhclient waits for an OFFER; its DISCOVERs
                  stay spaced by the retransmission delay instead of all
                  timing out at once.
    backoff       DHC-05: unanswered, the four DISCOVERs are spaced by the
                  RFC 2131 4.1 randomized exponential backoff (4, 8, 16 s,
                  each +-1 s) and dhclient gives up 32 +- 1 s after the last.
    request-retx  DHC-03: an unanswered DHCPREQUEST is retransmitted with
                  the same xid on the backoff, and an ACK to the third binds.
    request-restart DHC-03: after four unanswered REQUESTs the client goes
                  back to INIT -- a new DISCOVER with a new xid -- and binds
                  from that exchange.
    nak           DHC-04: a DHCPNAK to the REQUEST sends the client back to
                  INIT at once (new DISCOVER, new xid), not after a timeout.

Run from the repo root after building sys/, wireguest and sbin/dhclient:
    python3 tests/lib/net/wire/test_dhclient.py [case...]
"""
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, TOP, PEER_IP, PEER_MAC, GUEST_MAC  # noqa: E402

DHCLIENT = os.path.join(TOP, 'sbin', 'dhclient', 'dhclient')
LEASED = '10.0.2.50'
MASK = '255.255.255.0'
BCAST_MAC = b'\xff' * 6

DISCOVER, OFFER, REQUEST, DECLINE, ACK, NAK, RELEASE = 1, 2, 3, 4, 5, 6, 7
MAGIC = 0x63825363


class Msg:
    """One DHCP message from the guest."""

    def __init__(self, t, ip):
        ihl = (ip[0] & 0xF) * 4
        self.t = t
        self.src = socket.inet_ntoa(ip[12:16])
        self.dst = socket.inet_ntoa(ip[16:20])
        self.sport, self.dport = struct.unpack('!HH', ip[ihl:ihl + 4])
        b = ip[ihl + 8:]
        (self.op, self.htype, self.hlen, self.hops, self.xid, self.secs,
         self.flags) = struct.unpack('!BBBBIHH', b[:12])
        self.ciaddr = socket.inet_ntoa(b[12:16])
        self.yiaddr = socket.inet_ntoa(b[16:20])
        self.chaddr = b[28:44]
        self.opts = {}
        if len(b) >= 240 and struct.unpack('!I', b[236:240])[0] == MAGIC:
            i, o = 0, b[240:]
            while i < len(o):
                c = o[i]
                if c == 0:
                    i += 1
                    continue
                if c == 255 or i + 1 >= len(o):
                    break
                n = o[i + 1]
                self.opts[c] = self.opts.get(c, b'') + o[i + 2:i + 2 + n]
                i += 2 + n
        mt = self.opts.get(53, b'\0')
        self.type = mt[0] if mt else 0

    def ip_opt(self, code):
        v = self.opts.get(code)
        return socket.inet_ntoa(v[:4]) if v and len(v) >= 4 else None


def dhcp_msgs(w):
    """Every DHCP client message received so far (consumed from ip_rx)."""
    out, keep = [], []
    for d in w.ip_rx:
        proto, src, dst, ip = d
        ihl = (ip[0] & 0xF) * 4
        if proto == 17 and len(ip) >= ihl + 8 and \
                struct.unpack('!H', ip[ihl + 2:ihl + 4])[0] == 67:
            out.append(Msg(time.time(), ip))
        else:
            keep.append(d)
    w.ip_rx[:] = keep
    return out


class Server:
    def __init__(self, w):
        self.w = w
        self.seen = []          # every client message, oldest first

    def poll(self, timeout):
        """Pump for `timeout` seconds in short slices, so each message is
        stamped close to when it arrived."""
        new = []
        end = time.time() + timeout
        while True:
            self.w.pump(min(0.02, max(0.0, end - time.time())))
            new += dhcp_msgs(self.w)
            if time.time() >= end:
                break
        self.seen += new
        return new

    def expect(self, mtype, timeout):
        """The next client message of type mtype, or None."""
        end = time.time() + timeout
        while time.time() < end:
            for m in self.poll(0.1):
                if m.type == mtype:
                    return m
        return None

    def reply(self, req, mtype, yiaddr=LEASED, opts=None,
              server_id=PEER_IP, dst='255.255.255.255', eth_dst=BCAST_MAC):
        body = struct.pack('!BBBBIHH4s4s4s4s', 2, 1, 6, 0, req.xid, 0,
                           req.flags, b'\0' * 4,
                           socket.inet_aton(yiaddr if mtype != NAK else '0.0.0.0'),
                           b'\0' * 4, b'\0' * 4)
        body += req.chaddr + b'\0' * 64 + b'\0' * 128
        o = bytes([53, 1, mtype])
        if server_id:
            o += bytes([54, 4]) + socket.inet_aton(server_id)
        for code, val in (opts or {}).items():
            o += bytes([code, len(val)]) + val
        body += struct.pack('!I', MAGIC) + o + b'\xff'
        self.w.send_udp(67, 68, body, src=PEER_IP, dst=dst, eth_dst=eth_dst)

    def std_opts(self, lease=3600):
        return {1: socket.inet_aton(MASK), 3: socket.inet_aton(PEER_IP),
                51: struct.pack('!I', lease)}


def boot(mode='run', args='eth0'):
    return Wire.boot('%s /sbin/dhclient %s' % (mode, args),
                     files=[(DHCLIENT, '/sbin/dhclient')])


def arp_answers(w, ip, timeout=3):
    """Does the guest answer an ARP request for ip?"""
    w.frames.clear()
    w.send_arp(1, PEER_MAC, PEER_IP, b'\0' * 6, ip, eth_dst=BCAST_MAC)
    end = time.time() + timeout
    while time.time() < end:
        w.pump(0.2)
        for dst, et, fr in w.frames:
            if et == 0x0806 and fr[20:22] == b'\x00\x02' and \
                    socket.inet_ntoa(fr[28:32]) == ip:
                return True
    return False


def case_bound():
    with boot() as w:
        s = Server(w)
        d = s.expect(DISCOVER, 90)
        if not d:
            return 'no DHCPDISCOVER', w
        s.reply(d, OFFER, opts=s.std_opts())
        r = s.expect(REQUEST, 10)
        if not r:
            return 'no DHCPREQUEST after the OFFER', w
        if r.ip_opt(50) != LEASED or r.ip_opt(54) != PEER_IP:
            return 'REQUEST asks for %s from %s' % (r.ip_opt(50), r.ip_opt(54)), w
        s.reply(r, ACK, opts=s.std_opts())
        if not w.wait_serial('dhclient: bound', 10):
            return 'dhclient never reported bound', w
        if not arp_answers(w, LEASED):
            return 'guest does not answer ARP for the leased address', w
        return None, w


def case_clock_step():
    with boot('runclock') as w:
        s = Server(w)
        if not s.expect(DISCOVER, 90):
            return 'no DHCPDISCOVER', w
        s.poll(6.0)                     # answer nothing: let it retransmit
        ts = [m.t for m in s.seen if m.type == DISCOVER]
        if len(ts) < 2:
            return 'only %d DISCOVER(s) in 6 s' % len(ts), w
        gap = ts[1] - ts[0]
        if gap < 1.5:
            return ('DISCOVERs %.2f s apart while the clock was stepped: '
                    'the waits time out on the wall clock' % gap), w
        return None, w


def case_backoff():
    with boot() as w:
        s = Server(w)
        if not s.expect(DISCOVER, 90):
            return 'no DHCPDISCOVER', w
        # 4 + 8 + 16 s between the four, then 32 s before giving up
        s.poll(34.0)
        ts = [m.t for m in s.seen if m.type == DISCOVER]
        if len(ts) != 4:
            return '%d DISCOVERs in 34 s, want 4' % len(ts), w
        for k, (lo, hi) in enumerate(((3, 5), (7, 9), (15, 17))):
            gap = ts[k + 1] - ts[k]
            if not lo - 0.4 <= gap <= hi + 0.4:
                return ('retransmission %d came %.2f s after the previous, '
                        'want %d..%d s' % (k + 1, gap, lo, hi)), w
        if not w.wait_serial('dhclient: no OFFER', 40):
            return 'never gave up after the fourth DISCOVER', w
        given_up = time.time() - ts[3]
        if not 31 - 0.4 <= given_up <= 33 + 1.0:
            return 'gave up %.1f s after the last DISCOVER, want 31..33' % given_up, w
        return None, w


def offer_and_request(s):
    """DISCOVER -> OFFER; returns the first REQUEST (or an error string)."""
    d = s.expect(DISCOVER, 90)
    if not d:
        return None, d, 'no DHCPDISCOVER'
    s.reply(d, OFFER, opts=s.std_opts())
    r = s.expect(REQUEST, 10)
    if not r:
        return None, d, 'no DHCPREQUEST after the OFFER'
    return r, d, None


def case_request_retx():
    with boot() as w:
        s = Server(w)
        r, d, err = offer_and_request(s)
        if err:
            return err, w
        reqs = [r]
        for _ in range(2):                  # ignore the first two
            nxt = s.expect(REQUEST, 12)
            if not nxt:
                return 'DHCPREQUEST %d never came' % (len(reqs) + 1), w
            reqs.append(nxt)
        for k, (lo, hi) in enumerate(((3, 5), (7, 9))):
            gap = reqs[k + 1].t - reqs[k].t
            if not lo - 0.4 <= gap <= hi + 0.4:
                return ('REQUEST retransmission %d after %.2f s, want %d..%d'
                        % (k + 1, gap, lo, hi)), w
        if any(q.xid != d.xid for q in reqs):
            return 'a retransmitted REQUEST changed its xid', w
        s.reply(reqs[-1], ACK, opts=s.std_opts())
        if not w.wait_serial('dhclient: bound', 10):
            return 'not bound after the ACK to the third REQUEST', w
        return None, w


def case_request_restart():
    with boot() as w:
        s = Server(w)
        r, d, err = offer_and_request(s)
        if err:
            return err, w
        # ignore every REQUEST (4, about 60 s); a new DISCOVER must follow
        d2 = s.expect(DISCOVER, 75)
        if not d2:
            return 'no new DISCOVER after the unanswered REQUESTs', w
        nreq = sum(1 for m in s.seen if m.type == REQUEST)
        if nreq != 4:
            return '%d REQUESTs before restarting, want 4' % nreq, w
        if d2.xid == d.xid:
            return 'the restarted DISCOVER reused the old xid', w
        s.reply(d2, OFFER, opts=s.std_opts())
        r2 = s.expect(REQUEST, 10)
        if not r2:
            return 'no REQUEST in the restarted exchange', w
        s.reply(r2, ACK, opts=s.std_opts())
        if not w.wait_serial('dhclient: bound', 10):
            return 'not bound after the restart', w
        return None, w


def case_nak():
    with boot() as w:
        s = Server(w)
        r, d, err = offer_and_request(s)
        if err:
            return err, w
        s.reply(r, NAK)
        nak_t = time.time()
        d2 = s.expect(DISCOVER, 10)
        if not d2:
            return 'no new DISCOVER after the DHCPNAK', w
        if d2.t - nak_t > 2.0:
            return 'restarted %.1f s after the NAK, not at once' % (d2.t - nak_t), w
        if d2.xid == d.xid:
            return 'the restarted DISCOVER reused the old xid', w
        s.reply(d2, OFFER, opts=s.std_opts())
        r2 = s.expect(REQUEST, 10)
        if not r2:
            return 'no REQUEST after the restart', w
        s.reply(r2, ACK, opts=s.std_opts())
        if not w.wait_serial('dhclient: bound', 10):
            return 'not bound after the restart', w
        return None, w


CASES = (('bound', case_bound), ('clock-step', case_clock_step),
         ('backoff', case_backoff), ('request-retx', case_request_retx),
         ('request-restart', case_request_restart), ('nak', case_nak))


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
            print(w.serial()[-1500:])
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
