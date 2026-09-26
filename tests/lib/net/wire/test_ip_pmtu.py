#!/usr/bin/env python3
"""
Path MTU Discovery and Don't Fragment (RFC 1191; RFC 791 3.2 Flags).
Nothing ever set DF, and an ICMP "fragmentation needed" was treated as a
hard error rather than as news of a smaller path.

    tcp-df      every TCP segment the guest sends carries DF.
    udp-df      a UDP socket sends without DF by default and with it once
                IP_MTU_DISCOVER is set to IP_PMTUDISC_DO.
    tcp-pmtud   the guest sends full 1460-octet segments; an ICMP
                fragmentation-needed quoting one, with a next-hop MTU of
                1000, makes it resend that data at once in segments of at
                most 960 octets, still with DF.
    tcp-floor   a reported MTU of 200 is not believed below 552 (a forged
                report about any datagram could otherwise cripple every
                later connection to the host): the data is resent in
                512-octet segments, now without DF, so a real path that
                small still works through fragmentation by its routers.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_pmtu.py [case...]
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, Seg, SYN, ACK, csum, PEER_IP, GUEST_IP  # noqa: E402

PORT = 7418
PISS = 220000
MSS1460 = b'\x02\x04\x05\xb4'


def ip_frames(w, proto):
    """Raw IPv4 datagrams of `proto` the guest sent, oldest first."""
    return [fr[14:] for mac, et, fr in w.frames
            if et == 0x0800 and fr[14 + 9] == proto]


def has_df(ip):
    return bool(struct.unpack('!H', ip[6:8])[0] & 0x4000)


def tcp_seq(ip):
    ihl = (ip[0] & 0xF) * 4
    return struct.unpack('!I', ip[ihl + 4:ihl + 8])[0]


def tcp_dlen(ip):
    ihl = (ip[0] & 0xF) * 4
    tot = struct.unpack('!H', ip[2:4])[0]
    return tot - ihl - (ip[ihl + 12] >> 4) * 4


def handshake(w):
    syn = w.expect(lambda s: s.flags & SYN and s.dport == PORT, 90, 'SYN')
    if not syn:
        return None, 'no SYN from guest'
    w.send(Seg(PORT, syn.sport, PISS, syn.seq + 1, SYN | ACK, opts=MSS1460))
    if not w.wait_serial('guest: connected', 10):
        return None, 'guest did not report connected'
    return syn, None


def case_tcp_df():
    with Wire.boot('connect 10.0.2.2 %d write:hello sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        w.expect(lambda s: s.data, 5, 'data')
        w.pump(0.5)
        tcp = ip_frames(w, 6)
        if not tcp:
            return 'no TCP from the guest', w
        bare = [i for i, ip in enumerate(tcp) if not has_df(ip)]
        if bare:
            return '%d of %d TCP segments lack DF' % (len(bare), len(tcp)), w
        return None, w


def case_udp_df():
    with Wire.boot('udpany 7419 sendto:10.0.2.2:9:plain pmtudisc:2 '
                   'sendto:10.0.2.2:9:with-df sleep:60') as w:
        if not w.wait_serial('guest: pmtudisc', 90):
            return 'guest never set IP_MTU_DISCOVER', w
        if 'pmtudisc ok' not in w.serial():
            return 'setsockopt(IP_MTU_DISCOVER) failed', w
        w.wait_serial('with-df', 5)
        w.pump(1.0)
        udp = [ip for ip in ip_frames(w, 17)
               if ip[16:20] == bytes([10, 0, 2, 2])]
        if len(udp) < 2:
            return 'saw %d datagrams, want 2' % len(udp), w
        if has_df(udp[0]):
            return 'the default UDP datagram carries DF', w
        if not has_df(udp[1]):
            return 'the IP_PMTUDISC_DO datagram lacks DF', w
        return None, w


def frag_needed(w, ip, mtu):
    """ICMP Destination Unreachable, code 4, next-hop MTU `mtu`, quoting
    the header and first 8 octets of `ip`."""
    ihl = (ip[0] & 0xF) * 4
    msg = bytearray(struct.pack('!BBHHH', 3, 4, 0, 0, mtu) + ip[:ihl + 8])
    msg[2:4] = struct.pack('!H', csum(bytes(msg)))
    w.send_ip(1, bytes(msg), src=PEER_IP, dst=GUEST_IP)


def case_tcp_pmtud():
    with Wire.boot('connect 10.0.2.2 %d writen:4000 sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        first = w.expect(lambda s: len(s.data) == 1460, 10, 'full segment')
        if not first:
            return 'no 1460-octet segment from the guest', w
        w.pump(0.2)
        sent = [ip for ip in ip_frames(w, 6) if tcp_seq(ip) == first.seq]
        if not sent:
            return 'could not find the full segment on the wire', w
        n_before = len(ip_frames(w, 6))
        frag_needed(w, sent[0], 1000)
        end = time.time() + 3
        while time.time() < end:
            w.pump(0.2)
            later = ip_frames(w, 6)[n_before:]
            resent = [ip for ip in later if tcp_seq(ip) == first.seq]
            if resent:
                ip = resent[0]
                if not 0 < tcp_dlen(ip) <= 960:
                    return 'resent %d octets, want at most 960' % \
                        tcp_dlen(ip), w
                if not has_df(ip):
                    return 'the resent segment lacks DF', w
                big = [tcp_dlen(i) for i in later if tcp_dlen(i) > 960]
                if big:
                    return 'segments of %r octets still sent' % big, w
                return None, w
        return 'the data was not resent after fragmentation-needed', w


def case_tcp_floor():
    with Wire.boot('connect 10.0.2.2 %d writen:4000 sleep:60' % PORT) as w:
        syn, err = handshake(w)
        if err:
            return err, w
        first = w.expect(lambda s: len(s.data) == 1460, 10, 'full segment')
        if not first:
            return 'no 1460-octet segment from the guest', w
        w.pump(0.2)
        sent = [ip for ip in ip_frames(w, 6) if tcp_seq(ip) == first.seq]
        n_before = len(ip_frames(w, 6))
        frag_needed(w, sent[0], 200)
        end = time.time() + 3
        while time.time() < end:
            w.pump(0.2)
            resent = [ip for ip in ip_frames(w, 6)[n_before:]
                      if tcp_seq(ip) == first.seq]
            if resent:
                ip = resent[0]
                if tcp_dlen(ip) != 512:
                    return 'resent %d octets, want 512 (the 552 floor)' % \
                        tcp_dlen(ip), w
                if has_df(ip):
                    return 'a path pinned at the floor still gets DF', w
                return None, w
        return 'the data was not resent after fragmentation-needed', w


CASES = (('tcp-df', case_tcp_df), ('udp-df', case_udp_df),
         ('tcp-pmtud', case_tcp_pmtud), ('tcp-floor', case_tcp_floor))


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
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
