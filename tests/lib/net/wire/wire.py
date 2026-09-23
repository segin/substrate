"""
wire.py -- host half of the wire-level TCP test harness.

Boots substrate under qemu with its NIC attached to a `-netdev dgram`
backend, so every Ethernet frame the guest sends arrives here as one UDP
datagram, and every frame sent here is injected into the guest.  The host
plays the guest's gateway, 10.0.2.2: it answers ARP itself and gives each
test full control over the TCP segments it sends, including deliberately
out-of-order, out-of-window or combined-flag segments that no real stack
would produce on demand.

The guest runs tests/lib/net/wire/wireguest (built for substrate) as init,
with a scenario script passed through initarg.

Nothing here writes rootfs.img: each boot uses a reflink copy.

Usage from a test script:

    from wire import Wire, Seg, ACK, SYN, FIN, RST
    with Wire.boot('connect 10.0.2.2 7000 readeof close') as w:
        syn = w.expect(lambda s: s.flags & SYN, 10, 'guest SYN')
        ...
"""
import os
import select
import shutil
import socket
import struct
import subprocess
import time

TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))

FIN, SYN, RST, PSH, ACK, URG = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20

PEER_MAC = bytes.fromhex('525400000002')
GUEST_MAC = bytes.fromhex('525400123456')
PEER_IP = '10.0.2.2'
GUEST_IP = '10.0.2.15'
PEER_IP6 = 'fec0::2'         # inet_init's IPv6 defaults
GUEST_IP6 = 'fec0::3'
ROOT_P2_OFFSET = 104448 * 512           # ext2 root partition in rootfs.img


def flagstr(f):
    return ''.join(c for b, c in ((SYN, 'S'), (ACK, 'A'), (FIN, 'F'), (RST, 'R'),
                                   (PSH, 'P'), (URG, 'U')) if f & b) or '.'


def csum(data):
    if len(data) % 2:
        data += b'\0'
    s = sum(struct.unpack('!%dH' % (len(data) // 2), data))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


class Seg:
    """One TCP segment, in host byte order."""

    def __init__(self, sport, dport, seq, ack, flags, win=65535, data=b'',
                 opts=b'', urp=0, src=PEER_IP, dst=GUEST_IP):
        self.sport, self.dport = sport, dport
        self.seq, self.ack = seq & 0xFFFFFFFF, ack & 0xFFFFFFFF
        self.flags, self.win, self.data = flags, win, data
        self.opts, self.urp, self.src, self.dst = opts, urp, src, dst

    def __repr__(self):
        return ('<%s %s:%d>%s:%d seq=%d ack=%d win=%d len=%d>' %
                (flagstr(self.flags), self.src, self.sport, self.dst,
                 self.dport, self.seq, self.ack, self.win, len(self.data)))

    @property
    def seqlen(self):
        """Sequence space consumed: data plus one each for SYN and FIN."""
        return len(self.data) + (1 if self.flags & SYN else 0) + \
            (1 if self.flags & FIN else 0)

    def encode(self):
        opts = self.opts + b'\0' * (-len(self.opts) % 4)
        doff = (20 + len(opts)) // 4
        hdr = struct.pack('!HHIIBBHHH', self.sport, self.dport, self.seq,
                          self.ack, doff << 4, self.flags, self.win, 0,
                          self.urp) + opts
        body = hdr + self.data
        pseudo = (socket.inet_aton(self.src) + socket.inet_aton(self.dst) +
                  struct.pack('!BBH', 0, 6, len(body)))
        c = csum(pseudo + body)
        return body[:16] + struct.pack('!H', c) + body[18:]

    @classmethod
    def decode(cls, src, dst, b):
        sport, dport, seq, ack, doff, flags, win, _, urp = \
            struct.unpack('!HHIIBBHHH', b[:20])
        hl = (doff >> 4) * 4
        return cls(sport, dport, seq, ack, flags, win, b[hl:], b[20:hl], urp,
                   src, dst)


class Wire:
    def __init__(self, proc, sock, qport, log, img):
        self.proc, self.sock, self.qport = proc, sock, qport
        self.log, self.img = log, img
        self.ip_id = 1
        self.rx = []            # TCP segments from the guest, oldest first
        self.ip_rx = []         # other IPv4 datagrams: (proto, src, dst, ip_bytes)
        self.frames = []        # every frame from the guest: (dst_mac, ethertype, bytes)
        self.ip6_rx = []        # IPv6 packets: (next_header, src, dst, bytes)
        self.trace = []         # everything seen/sent, for failure reports

    # -- boot -------------------------------------------------------------
    @classmethod
    def boot(cls, initarg, kernel=None, workdir=None, guest=None,
             nic='virtio-net-pci'):
        workdir = workdir or os.environ.get('WIRE_WORKDIR', '/tmp')
        kernel = kernel or os.path.join(TOP, 'sys', 'kernel.multiboot')
        guest = guest or os.path.join(os.path.dirname(__file__), 'wireguest')
        # One serial log per BOOT, not per process, and never pre-existing:
        # qemu truncates its -serial file only once it is running, so a
        # reused name let wait_serial() match the previous boot's output.
        cls._boots = getattr(cls, '_boots', 0) + 1
        tag = '%d.%d' % (os.getpid(), cls._boots)
        img = os.path.join(workdir, 'wire-rootfs.%s.img' % tag)
        log = os.path.join(workdir, 'wire-serial.%s.log' % tag)
        if os.path.exists(log):
            os.unlink(log)
        subprocess.run(['cp', '--reflink=auto', os.path.join(TOP, 'rootfs.img'), img],
                       check=True)
        dev = '%s?offset=%d' % (img, ROOT_P2_OFFSET)
        for req in ('write %s /wireguest' % guest, 'sif /wireguest mode 0100755'):
            subprocess.run(['debugfs', '-w', '-R', req, dev], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0))
        hport = sock.getsockname()[1]
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.bind(('127.0.0.1', 0))
        qport = probe.getsockname()[1]
        probe.close()
        cmd = ['qemu-system-i386', '-cpu', 'qemu32,+sse,+sse2', '-accel', 'kvm',
               '-m', '256M', '-kernel', kernel, '-display', 'none',
               '-serial', 'file:' + log, '-no-reboot',
               '-drive', 'file=%s,format=raw,if=virtio' % img,
               '-netdev', 'dgram,id=n0,local.type=inet,local.host=127.0.0.1,'
                          'local.port=%d,remote.type=inet,remote.host=127.0.0.1,'
                          'remote.port=%d' % (qport, hport),
               '-device', '%s,netdev=n0,mac=52:54:00:12:34:56' % nic,
               '-append', "serial_debug root=LABEL=sub-root init=/wireguest "
                          "initarg='%s'" % initarg]
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        return cls(proc, sock, qport, log, img)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.sock.close()
        if os.path.exists(self.img):
            os.unlink(self.img)

    def serial(self):
        try:
            with open(self.log, 'rb') as f:
                return f.read().decode('latin-1').replace('\r', '')
        except FileNotFoundError:
            return ''

    def wait_serial(self, text, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if text in self.serial():
                return True
            self.pump(0.2)
        return False

    # -- frames -----------------------------------------------------------
    def _send_frame(self, frame):
        self.sock.sendto(frame, ('127.0.0.1', self.qport))

    def _handle(self, frame):
        if len(frame) < 14:
            return
        etype = struct.unpack('!H', frame[12:14])[0]
        self.frames.append((bytes(frame[0:6]), etype, bytes(frame)))
        if etype == 0x0806 and len(frame) >= 42:              # ARP
            op = struct.unpack('!H', frame[20:22])[0]
            tpa = socket.inet_ntoa(frame[38:42])
            if op == 1 and tpa == PEER_IP:
                sha, spa = frame[22:28], frame[28:32]
                rep = (sha + PEER_MAC + b'\x08\x06' +
                       struct.pack('!HHBBH', 1, 0x0800, 6, 4, 2) +
                       PEER_MAC + socket.inet_aton(PEER_IP) + sha + spa)
                self._send_frame(rep)
            return
        if etype == 0x86DD and len(frame) >= 54:
            ip6 = frame[14:]
            plen = struct.unpack('!H', ip6[4:6])[0]
            src = socket.inet_ntop(socket.AF_INET6, ip6[8:24])
            dst = socket.inet_ntop(socket.AF_INET6, ip6[24:40])
            self.trace.append(('rx', time.time(), 'ipv6 nh %d %s>%s len %d' %
                               (ip6[6], src, dst, plen)))
            self.ip6_rx.append((ip6[6], src, dst, bytes(ip6[:40 + plen])))
            return
        if etype != 0x0800:
            return
        ip = frame[14:]
        ihl = (ip[0] & 0xF) * 4
        tot = struct.unpack('!H', ip[2:4])[0]
        src, dst = socket.inet_ntoa(ip[12:16]), socket.inet_ntoa(ip[16:20])
        if ip[9] != 6:
            self.trace.append(('rx', time.time(), 'proto %d %s>%s len %d' %
                               (ip[9], src, dst, tot)))
            self.ip_rx.append((ip[9], src, dst, bytes(ip[:tot])))
            return
        seg = Seg.decode(src, dst, ip[ihl:tot])
        self.trace.append(('rx', time.time(), seg))
        self.rx.append(seg)

    def pump(self, timeout):
        end = time.time() + timeout
        while True:
            left = end - time.time()
            r, _, _ = select.select([self.sock], [], [], max(0, left))
            if r:
                frame, _ = self.sock.recvfrom(4096)
                self._handle(frame)
                continue
            if left <= 0:
                return

    # -- TCP ----------------------------------------------------------------
    def send(self, seg):
        body = seg.encode()
        ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(body), self.ip_id,
                         0, 64, 6, 0, socket.inet_aton(seg.src),
                         socket.inet_aton(seg.dst))
        ip = ip[:10] + struct.pack('!H', csum(ip)) + ip[12:]
        self.ip_id = (self.ip_id + 1) & 0xFFFF
        self.trace.append(('tx', time.time(), seg))
        self._send_frame(GUEST_MAC + PEER_MAC + b'\x08\x00' + ip + body)

    def send_arp(self, op, sha, spa, tha, tpa, eth_dst=GUEST_MAC):
        """Inject an ARP packet (op 1 request, 2 reply) toward the guest."""
        pkt = (struct.pack('!HHBBH', 1, 0x0800, 6, 4, op) + sha +
               socket.inet_aton(spa) + tha + socket.inet_aton(tpa))
        self.trace.append(('tx', time.time(), 'arp op %d %s is-at %s -> %s' %
                           (op, spa, sha.hex(':'), tpa)))
        self._send_frame(eth_dst + sha + b'\x08\x06' + pkt)

    def send_ip6(self, nh, payload, src=PEER_IP6, dst=GUEST_IP6,
                 eth_dst=GUEST_MAC, hlim=64):
        """Send an IPv6 packet; payload is everything after the fixed
        header (extension headers included), nh the first Next Header."""
        hdr = (struct.pack('!IHBB', 0x60000000, len(payload), nh, hlim) +
               socket.inet_pton(socket.AF_INET6, src) +
               socket.inet_pton(socket.AF_INET6, dst))
        self.trace.append(('tx', time.time(), 'ipv6 nh %d %s>%s len %d' %
                           (nh, src, dst, len(payload))))
        self._send_frame(eth_dst + PEER_MAC + b'\x86\xdd' + hdr + payload)

    @staticmethod
    def icmp6(type_, code, body, src, dst):
        """An ICMPv6 message with its checksum."""
        msg = bytearray(struct.pack('!BBH', type_, code, 0) + body)
        pseudo = (socket.inet_pton(socket.AF_INET6, src) +
                  socket.inet_pton(socket.AF_INET6, dst) +
                  struct.pack('!IxxxB', len(msg), 58))
        msg[2:4] = struct.pack('!H', csum(pseudo + bytes(msg)))
        return bytes(msg)

    def prime_nd6(self):
        """Solicit the guest's address with our link-layer address attached,
        so the guest learns our MAC.  (It cannot learn it by soliciting us
        itself: nd6_solicit() creates no entry for the answer to refresh.)"""
        tgt = socket.inet_pton(socket.AF_INET6, GUEST_IP6)
        sol = socket.inet_ntop(socket.AF_INET6,
                               bytes.fromhex('ff0200000000000000000001ff') + tgt[13:])
        body = b'\0\0\0\0' + tgt + bytes([1, 1]) + PEER_MAC
        self.send_ip6(58, self.icmp6(135, 0, body, PEER_IP6, sol), dst=sol,
                      eth_dst=bytes([0x33, 0x33, 0xff]) + tgt[13:], hlim=255)

    def expect_ip6(self, pred, timeout):
        end = time.time() + timeout
        while True:
            while self.ip6_rx:
                d = self.ip6_rx.pop(0)
                if pred(d):
                    return d
            left = end - time.time()
            if left <= 0:
                return None
            self.pump(min(left, 0.2))

    def send_ip(self, proto, payload, src=PEER_IP, dst=GUEST_IP, ttl=64,
                ident=None, eth_dst=GUEST_MAC):
        ident = self.ip_id if ident is None else ident
        ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(payload), ident,
                         0, ttl, proto, 0, socket.inet_aton(src),
                         socket.inet_aton(dst))
        ip = ip[:10] + struct.pack('!H', csum(ip)) + ip[12:]
        self.ip_id = (self.ip_id + 1) & 0xFFFF
        self.trace.append(('tx', time.time(), 'proto %d %s>%s len %d' %
                           (proto, src, dst, 20 + len(payload))))
        self._send_frame(eth_dst + PEER_MAC + b'\x08\x00' + ip + payload)

    def send_udp(self, sport, dport, data, src=PEER_IP, dst=GUEST_IP,
                 checksum=True, eth_dst=GUEST_MAC):
        hdr = struct.pack('!HHHH', sport, dport, 8 + len(data), 0)
        c = 0
        if checksum:
            pseudo = (socket.inet_aton(src) + socket.inet_aton(dst) +
                      struct.pack('!BBH', 0, 17, 8 + len(data)))
            c = csum(pseudo + hdr + data) or 0xFFFF
        self.send_ip(17, hdr[:6] + struct.pack('!H', c) + data, src, dst,
                     eth_dst=eth_dst)

    def expect_ip(self, pred, timeout):
        """First non-TCP IPv4 datagram (proto, src, dst, bytes) matching
        pred, or None."""
        end = time.time() + timeout
        while True:
            while self.ip_rx:
                d = self.ip_rx.pop(0)
                if pred(d):
                    return d
            left = end - time.time()
            if left <= 0:
                return None
            self.pump(min(left, 0.2))

    def expect(self, pred, timeout, what):
        """Return the first queued-or-arriving guest segment matching pred,
        dropping the non-matching ones before it.  None on timeout."""
        end = time.time() + timeout
        while True:
            while self.rx:
                s = self.rx.pop(0)
                if pred(s):
                    return s
            left = end - time.time()
            if left <= 0:
                return None
            self.pump(min(left, 0.2))

    def quiet(self, secs):
        """Collect whatever the guest sends during the next secs seconds."""
        self.rx.clear()
        self.pump(secs)
        out, self.rx = self.rx, []
        return out

    def dump(self):
        t0 = self.trace[0][1] if self.trace else 0
        return '\n'.join('  %6.2f %s %s' % (t - t0, d, s if isinstance(s, str) else repr(s))
                         for d, t, s in self.trace)
