#!/usr/bin/env python3
"""
sbin/dhclient against a scripted DHCP server on the wire
(docs/dhclient-audit-2026-09.md).  The guest runs the freshly built
sbin/dhclient/dhclient (written into the image as /sbin/dhclient) under
'wireguest run'; this side plays the server, answering each DHCP message
exactly as the case needs.

    bound         DISCOVER -> OFFER -> REQUEST -> ACK: dhclient binds the
                  offered address, and the guest then answers ARP for it.
    ack-config    DHC-06: the netmask and router come from the DHCPACK --
                  here the OFFER carries neither.
    no-options    DHC-08: an ACK with no mask or router: the mask defaults
                  to the address's class (/8 for 10.0.2.50) and the gateway
                  the kernel booted with is removed -- an off-link datagram
                  no longer leaves through 10.0.2.2.
    overload      DHC-09: with option 52 = 3 the mask is read from 'file'
                  and the router from 'sname'.
    concat        DHC-09: a router and a mask each split across two option
                  instances are concatenated.
    maxsize       DHC-09: DISCOVER and REQUEST carry a Maximum DHCP Message
                  Size of at least 576.
    bad-headers   DHC-10: OFFERs with a bad IP checksum, a bad UDP checksum,
                  a source port other than 67, the MF bit, IP version 6, a
                  16-octet IP header, a UDP length too short for the body,
                  or a wrong magic cookie are all ignored; a well-formed
                  OFFER after them is the one requested.
    probe-announce DHC-07: before using the address the client ARP-probes it
                  (sender IP 0), and once bound announces it with a
                  gratuitous ARP.
    declined      DHC-07: when another host answers the probe, the client
                  sends a DHCPDECLINE (requested address and server
                  identifier, ciaddr 0, no other options -- Table 5), does
                  not bind, and restarts no sooner than 10 s later.
    renew         DHC-02: with a 20 s lease the client renews at T1 (10 s
                  +-5%) by unicast to the server -- ciaddr set, no server
                  identifier or requested address (Table 4) -- takes the
                  ACK, and renews again T1 after that REQUEST.
    rebind        DHC-02: with the renewal ignored, a broadcast REBINDING
                  request at T2 (17.5 s +-5%); its ACK keeps the address.
    expire        DHC-02: with everything ignored, the address is dropped
                  when the lease ends (no more ARP replies) and INIT
                  restarts.
    nak-renew     DHC-02: a NAK to the renewal drops the address at once
                  and restarts INIT.
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
from wire import Wire, TOP, PEER_IP, PEER_MAC, GUEST_MAC, csum  # noqa: E402

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
              server_id=PEER_IP, dst='255.255.255.255', eth_dst=BCAST_MAC,
              sname=b'', file=b''):
        """opts: a dict, or a list of (code, value) pairs when a code must
        repeat.  sname/file: raw field contents (e.g. overloaded options),
        zero-padded to 64/128 octets."""
        body = self.body(req, mtype, yiaddr, opts, server_id, sname, file)
        self.w.send_udp(67, 68, body, src=PEER_IP, dst=dst, eth_dst=eth_dst)

    def body(self, req, mtype, yiaddr=LEASED, opts=None, server_id=PEER_IP,
             sname=b'', file=b''):
        """The BOOTP/DHCP body of a reply to req."""
        body = struct.pack('!BBBBIHH4s4s4s4s', 2, 1, 6, 0, req.xid, 0,
                           req.flags, b'\0' * 4,
                           socket.inet_aton(yiaddr if mtype != NAK else '0.0.0.0'),
                           b'\0' * 4, b'\0' * 4)
        body += req.chaddr + sname.ljust(64, b'\0') + file.ljust(128, b'\0')
        o = bytes([53, 1, mtype])
        if server_id:
            o += bytes([54, 4]) + socket.inet_aton(server_id)
        items = opts.items() if isinstance(opts, dict) else (opts or [])
        for code, val in items:
            o += bytes([code, len(val)]) + val
        body += struct.pack('!I', MAGIC) + o + b'\xff'
        return body

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


OTHER_MAC = bytes.fromhex('020000000099')


def arp_frames(w):
    """Guest ARP frames seen so far: (op, sha, spa, tpa, eth_dst)."""
    out = []
    for dst, et, fr in w.frames:
        if et == 0x0806 and len(fr) >= 42:
            out.append((struct.unpack('!H', fr[20:22])[0], fr[22:28],
                        socket.inet_ntoa(fr[28:32]),
                        socket.inet_ntoa(fr[38:42]), dst))
    return out


def wait_arp(w, pred, timeout):
    end = time.time() + timeout
    while time.time() < end:
        for a in arp_frames(w):
            if pred(a):
                return a
        w.pump(0.05)
    return None


def is_probe(a):
    op, sha, spa, tpa, dst = a
    return op == 1 and sha == GUEST_MAC and spa == '0.0.0.0' and tpa == LEASED


def case_probe_announce():
    with boot() as w:
        s = Server(w)
        r, d, err = offer_and_request(s)
        if err:
            return err, w
        w.frames.clear()
        s.reply(r, ACK, opts=s.std_opts())
        if not wait_arp(w, is_probe, 5):
            return 'no ARP probe (sender 0.0.0.0) for the leased address', w
        if not w.wait_serial('dhclient: bound', 10):
            return 'did not bind after an unanswered probe', w
        ann = wait_arp(w, lambda a: a[0] == 2 and a[2] == LEASED and
                       a[3] == LEASED and a[4] == BCAST_MAC, 3)
        if not ann:
            return 'no gratuitous ARP announcing the new address', w
        return None, w


def case_declined():
    with boot() as w:
        s = Server(w)
        r, d, err = offer_and_request(s)
        if err:
            return err, w
        w.frames.clear()
        s.reply(r, ACK, opts=s.std_opts())
        if not wait_arp(w, is_probe, 5):
            return 'no ARP probe for the leased address', w
        # another host already has it
        w.send_arp(2, OTHER_MAC, LEASED, GUEST_MAC, '0.0.0.0', eth_dst=BCAST_MAC)
        dec = s.expect(DECLINE, 5)
        if not dec:
            return 'no DHCPDECLINE for an address that answered ARP', w
        if (dec.ip_opt(50) != LEASED or dec.ip_opt(54) != PEER_IP or
                dec.ciaddr != '0.0.0.0' or 55 in dec.opts or 51 in dec.opts):
            return ('DHCPDECLINE fields are wrong (Table 5): requested %s '
                    'server %s ciaddr %s opts %s' % (dec.ip_opt(50),
                    dec.ip_opt(54), dec.ciaddr, sorted(dec.opts))), w
        if 'dhclient: bound' in w.serial():
            return 'bound the address anyway', w
        d2 = s.expect(DISCOVER, 15)
        if not d2:
            return 'no restart after declining', w
        if d2.t - dec.t < 9.5:
            return 'restarted %.1f s after declining, want >= 10' % (d2.t - dec.t), w
        return None, w


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


def case_ack_config():
    with boot() as w:
        s = Server(w)
        d = s.expect(DISCOVER, 90)
        if not d:
            return 'no DHCPDISCOVER', w
        s.reply(d, OFFER, opts={51: struct.pack('!I', 3600)})   # no mask/router
        r = s.expect(REQUEST, 10)
        if not r:
            return 'no DHCPREQUEST', w
        s.reply(r, ACK, opts=s.std_opts())
        if not w.wait_serial('dhclient: bound', 10):
            return 'dhclient never reported bound', w
        w.pump(0.3)
        want = 'dhclient: bound %s/%s via %s' % (LEASED, MASK, PEER_IP)
        if want not in w.serial():
            line = [l for l in w.serial().splitlines() if 'dhclient: bound' in l]
            return 'installed %r, want the ACK\'s %r' % (line[-1:], want), w
        return None, w


LEASE = 20          # short lease for the lifecycle cases: T1 ~10 s, T2 ~17.5 s


def bind_short(s, w):
    """Full exchange with a LEASE-second lease.  Returns the time the
    acknowledged REQUEST arrived (the client's lease start), or None."""
    r, d, err = offer_and_request(s)
    if err:
        return None
    s.reply(r, ACK, opts=s.std_opts(LEASE))
    if not w.wait_serial('dhclient: bound', 10):
        return None
    return r.t


def is_renewal(m):
    return (m.type == REQUEST and m.ciaddr == LEASED and
            50 not in m.opts and 54 not in m.opts)


def case_renew():
    with boot() as w:
        s = Server(w)
        t0 = bind_short(s, w)
        if t0 is None:
            return 'did not bind', w
        m = s.expect(REQUEST, LEASE)
        if not m:
            return 'no DHCPREQUEST before the lease ran out: nothing renews it', w
        if not is_renewal(m) or m.dst != PEER_IP:
            return ('first REQUEST after binding is not a RENEWING one '
                    '(dst %s ciaddr %s opts %s)' % (m.dst, m.ciaddr,
                                                    sorted(m.opts))), w
        at = m.t - t0
        if not 0.5 * LEASE * 0.95 - 0.5 <= at <= 0.5 * LEASE * 1.05 + 0.5:
            return 'renewed at %.1f s, want T1 = %.1f s +-5%%' % (at, 0.5 * LEASE), w
        s.reply(m, ACK, opts=s.std_opts(LEASE), dst=LEASED, eth_dst=GUEST_MAC)
        if not w.wait_serial('dhclient: renewed', 10):
            return 'the ACK to the renewal was not taken', w
        m2 = s.expect(REQUEST, LEASE)
        if not m2 or not is_renewal(m2):
            return 'no second renewal', w
        at2 = m2.t - m.t
        if not 0.5 * LEASE * 0.95 - 0.5 <= at2 <= 0.5 * LEASE * 1.05 + 0.5:
            return ('second renewal %.1f s after the first; the lease was not '
                    'restarted from the renewing REQUEST' % at2), w
        return None, w


def case_rebind():
    with boot() as w:
        s = Server(w)
        t0 = bind_short(s, w)
        if t0 is None:
            return 'did not bind', w
        # ignore the unicast renewal; the next REQUEST must be a broadcast
        # rebinding one at T2
        end = time.time() + LEASE
        m = None
        while time.time() < end:
            q = s.expect(REQUEST, max(0.1, end - time.time()))
            if q and q.dst == '255.255.255.255':
                m = q
                break
        if not m:
            return 'no broadcast REBINDING request before the lease ran out', w
        if not is_renewal(m):
            return 'REBINDING request has the wrong fields (Table 4)', w
        at = m.t - t0
        if not 0.875 * LEASE * 0.95 - 0.5 <= at <= 0.875 * LEASE * 1.05 + 0.5:
            return 'rebinding at %.1f s, want T2 = %.1f s +-5%%' % (at, 0.875 * LEASE), w
        s.reply(m, ACK, opts=s.std_opts(LEASE), dst=LEASED, eth_dst=GUEST_MAC)
        if not w.wait_serial('dhclient: rebound', 10):
            return 'the ACK to the rebinding request was not taken', w
        if not arp_answers(w, LEASED):
            return 'address lost after rebinding', w
        return None, w


def case_expire():
    with boot() as w:
        s = Server(w)
        t0 = bind_short(s, w)
        if t0 is None:
            return 'did not bind', w
        if not arp_answers(w, LEASED):
            return 'leased address not in use after binding', w
        # answer nothing: at LEASE s the address must go and INIT restart
        d = s.expect(DISCOVER, LEASE + 10)
        if not d:
            return 'no DISCOVER after the lease expired', w
        at = d.t - t0
        if at < LEASE - 0.5:
            return 'restarted at %.1f s, before the lease ended' % at, w
        if arp_answers(w, LEASED):
            return 'still answering ARP for the expired address', w
        return None, w


def case_nak_renew():
    with boot() as w:
        s = Server(w)
        t0 = bind_short(s, w)
        if t0 is None:
            return 'did not bind', w
        m = s.expect(REQUEST, LEASE)
        if not m or not is_renewal(m):
            return 'no renewal', w
        s.reply(m, NAK, dst=LEASED, eth_dst=GUEST_MAC)
        nak_t = time.time()
        d = s.expect(DISCOVER, 10)
        if not d:
            return 'no DISCOVER after the renewal was NAKed', w
        if d.t - nak_t > 2.0:
            return 'restarted %.1f s after the NAK' % (d.t - nak_t), w
        if arp_answers(w, LEASED):
            return 'still using the refused address', w
        return None, w


def bound_line(w):
    lines = [l for l in w.serial().splitlines() if 'dhclient: bound' in l]
    return lines[-1] if lines else ''


def exchange(s, w, ack_kw):
    """DISCOVER/OFFER/REQUEST, then an ACK built from ack_kw; returns an
    error string or None once bound."""
    d = s.expect(DISCOVER, 90)
    if not d:
        return 'no DHCPDISCOVER'
    s.reply(d, OFFER, opts={51: struct.pack('!I', 3600)})
    r = s.expect(REQUEST, 10)
    if not r:
        return 'no DHCPREQUEST'
    s.reply(r, ACK, **ack_kw)
    if not w.wait_serial('dhclient: bound', 10):
        return 'dhclient never reported bound'
    w.pump(0.3)
    return None


def case_overload():
    with boot() as w:
        s = Server(w)
        # mask in 'file', router in 'sname', as option 52 = 3 directs
        f = bytes([1, 4]) + socket.inet_aton(MASK) + b'\xff'
        sn = bytes([3, 4]) + socket.inet_aton(PEER_IP) + b'\xff'
        err = exchange(s, w, dict(opts=[(51, struct.pack('!I', 3600)),
                                        (52, b'\x03')], file=f, sname=sn))
        if err:
            return err, w
        want = 'dhclient: bound %s/%s via %s' % (LEASED, MASK, PEER_IP)
        if bound_line(w) != want:
            return 'bound %r, want %r (options overloaded into file/sname)' % (
                bound_line(w), want), w
        return None, w


def case_concat():
    with boot() as w:
        s = Server(w)
        r = socket.inet_aton(PEER_IP)
        m = socket.inet_aton(MASK)
        # the router and mask each split across two instances
        err = exchange(s, w, dict(opts=[(51, struct.pack('!I', 3600)),
                                        (3, r[:2]), (1, m[:3]),
                                        (3, r[2:]), (1, m[3:])]))
        if err:
            return err, w
        want = 'dhclient: bound %s/%s via %s' % (LEASED, MASK, PEER_IP)
        if bound_line(w) != want:
            return 'bound %r, want %r (split options concatenated)' % (
                bound_line(w), want), w
        return None, w


def case_maxsize():
    with boot() as w:
        s = Server(w)
        d = s.expect(DISCOVER, 90)
        if not d:
            return 'no DHCPDISCOVER', w
        v = d.opts.get(57)
        if not v or len(v) != 2 or struct.unpack('!H', v)[0] < 576:
            return 'DISCOVER has no usable Maximum DHCP Message Size (%r)' % v, w
        s.reply(d, OFFER, opts=s.std_opts())
        r = s.expect(REQUEST, 10)
        if not r or r.opts.get(57) != v:
            return 'REQUEST does not repeat the Maximum Message Size', w
        return None, w


def raw_reply(w, body, bad=None):
    """Send a DHCP reply as a hand-built frame, broken in the way `bad`
    names (None: well-formed)."""
    sport = 1067 if bad == 'sport' else 67
    if bad == 'magic':
        body = body[:236] + b'\x63\x82\x53\x00' + body[240:]
    ulen = 8 + len(body)
    uh = struct.pack('!HHHH', sport, 68, ulen, 0)
    pseudo = (socket.inet_aton(PEER_IP) + socket.inet_aton('255.255.255.255') +
              struct.pack('!BBH', 0, 17, ulen))
    c = csum(pseudo + uh + body) or 0xFFFF
    if bad == 'udpcsum':
        c ^= 0x5555
    if bad == 'udplen':                     # too short to hold the body
        uh = struct.pack('!HHHH', sport, 68, 8 + 200, 0)
    else:
        uh = uh[:6] + struct.pack('!H', c)
    vhl = {'ver': 0x65, 'hlen': 0x44}.get(bad, 0x45)
    frag = 0x2000 if bad == 'frag' else 0
    ip = struct.pack('!BBHHHBBH4s4s', vhl, 0, 20 + ulen, 0x4242, frag, 64, 17,
                     0, socket.inet_aton(PEER_IP),
                     socket.inet_aton('255.255.255.255'))
    ip = ip[:10] + struct.pack('!H', csum(ip)) + ip[12:]
    if bad == 'ipcsum':
        ip = ip[:10] + bytes([ip[10] ^ 0xFF, ip[11]]) + ip[12:]
    w._send_frame(BCAST_MAC + PEER_MAC + b'\x08\x00' + ip + uh + body)


def case_bad_headers():
    bads = ('ipcsum', 'udpcsum', 'sport', 'frag', 'ver', 'hlen', 'udplen',
            'magic')
    with boot() as w:
        s = Server(w)
        d = s.expect(DISCOVER, 90)
        if not d:
            return 'no DHCPDISCOVER', w
        # each malformed OFFER offers its own address; only the last,
        # well-formed one offers LEASED
        for k, bad in enumerate(bads):
            raw_reply(w, s.body(d, OFFER, '10.0.2.%d' % (60 + k),
                                opts=s.std_opts()), bad)
        w.pump(0.5)
        raw_reply(w, s.body(d, OFFER, LEASED, opts=s.std_opts()))
        r = s.expect(REQUEST, 10)
        if not r:
            return 'no REQUEST after a well-formed OFFER', w
        got = r.ip_opt(50)
        if got != LEASED:
            k = int(got.split('.')[-1]) - 60 if got else -1
            return 'accepted an OFFER with a bad %s' % (
                bads[k] if 0 <= k < len(bads) else got), w
        return None, w


OFFLINK = '198.51.100.1'


def case_no_options():
    # Two sends a second apart: through a gateway, the first can be lost
    # while ARP resolves it.
    with boot(args='eth0 -- sendto:%s:9:x sleep:1 sendto:%s:9:y'
              % (OFFLINK, OFFLINK)) as w:
        s = Server(w)
        d = s.expect(DISCOVER, 90)
        if not d:
            return 'no DHCPDISCOVER', w
        only_lease = {51: struct.pack('!I', 3600)}
        s.reply(d, OFFER, opts=only_lease)
        r = s.expect(REQUEST, 10)
        if not r:
            return 'no DHCPREQUEST', w
        s.reply(r, ACK, opts=only_lease)
        if not w.wait_serial('dhclient: bound', 10):
            return 'dhclient never reported bound', w
        w.pump(0.3)
        want = 'dhclient: bound %s/255.0.0.0\n' % LEASED
        if want not in w.serial():
            line = [l for l in w.serial().splitlines() if 'dhclient: bound' in l]
            return 'installed %r, want the class A default mask' % line[-1:], w
        # the boot-time gateway (10.0.2.2) must be gone: an off-link send
        # must not leave through it
        w.ip_rx.clear()
        w.wait_serial('guest: sleep', 10)
        w.pump(3.0)
        leaked = [x for x in w.ip_rx if x[2] == OFFLINK]
        if leaked:
            return 'an off-link datagram still went out via the old gateway', w
        sends = [l for l in w.serial().splitlines() if 'guest: sendto' in l]
        if len(sends) != 2 or any('unreachable' not in l for l in sends):
            return 'off-link sends: %r, want Network is unreachable' % sends, w
        return None, w


CASES = (('bound', case_bound), ('ack-config', case_ack_config),
         ('no-options', case_no_options), ('overload', case_overload),
         ('concat', case_concat), ('maxsize', case_maxsize),
         ('bad-headers', case_bad_headers),
         ('probe-announce', case_probe_announce), ('declined', case_declined),
         ('renew', case_renew), ('rebind', case_rebind),
         ('expire', case_expire), ('nak-renew', case_nak_renew), ('clock-step', case_clock_step),
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
