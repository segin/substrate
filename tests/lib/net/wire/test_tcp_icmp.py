#!/usr/bin/env python3
"""
ICMP errors about TCP segments (RFC 791 3.2 Errors; RFC 1122 4.2.3.9;
RFC 5927 4.1).  ICMP errors used to reach UDP only: a TCP connect() to a
host that answered with Port Unreachable waited out the whole
retransmission budget.

    refused     the peer answers the guest's SYN with an ICMP Port
                Unreachable quoting it: connect() fails ECONNREFUSED at
                once.
    outwindow   the same error quoting a sequence number the guest never
                sent is ignored: connect() is still waiting seconds later.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_tcp_icmp.py
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, SYN, csum, PEER_IP, GUEST_IP  # noqa: E402

PORT = 7413


def syn_frame(w, timeout):
    """The raw IP datagram (header + TCP) of the guest's first SYN."""
    end = time.time() + timeout
    while time.time() < end:
        for mac, et, fr in w.frames:
            ip = fr[14:]
            if et != 0x0800 or ip[9] != 6:
                continue
            ihl = (ip[0] & 0xF) * 4
            if ip[ihl + 13] & SYN:
                return ip
        w.pump(0.2)
    return None


def send_unreach(w, ip, seq_delta=0):
    """ICMP Destination Unreachable, code 3 (port), quoting the header and
    first 8 octets of `ip`, its sequence number moved by seq_delta."""
    ihl = (ip[0] & 0xF) * 4
    quote = bytearray(ip[:ihl + 8])
    seq = struct.unpack('!I', quote[ihl + 4:ihl + 8])[0]
    quote[ihl + 4:ihl + 8] = struct.pack('!I', (seq + seq_delta) & 0xFFFFFFFF)
    msg = bytearray(struct.pack('!BBHI', 3, 3, 0, 0) + bytes(quote))
    msg[2:4] = struct.pack('!H', csum(bytes(msg)))
    w.send_ip(1, bytes(msg), src=PEER_IP, dst=GUEST_IP)


def case_refused():
    with Wire.boot('connect 10.0.2.2 %d' % PORT) as w:
        ip = syn_frame(w, 90)
        if ip is None:
            return 'no SYN from the guest', w
        t0 = time.time()
        send_unreach(w, ip)
        if not w.wait_serial('guest: connect failed', 5):
            return 'connect() did not fail after Port Unreachable', w
        c = [l for l in w.serial().splitlines() if 'connect failed' in l][0]
        if '(111)' not in c:
            return 'connect(): %s, want ECONNREFUSED' % c, w
        if time.time() - t0 > 5:
            return 'connect() took %.1f s to fail' % (time.time() - t0), w
    return None, w


def case_outwindow():
    with Wire.boot('connect 10.0.2.2 %d' % PORT) as w:
        ip = syn_frame(w, 90)
        if ip is None:
            return 'no SYN from the guest', w
        send_unreach(w, ip, seq_delta=100000)
        if w.wait_serial('guest: connect failed', 4):
            c = [l for l in w.serial().splitlines() if 'connect failed' in l][0]
            return 'an error quoting a foreign sequence number ended ' \
                   'connect(): %s' % c, w
    return None, w


CASES = (('refused', case_refused), ('outwindow', case_outwindow))


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
