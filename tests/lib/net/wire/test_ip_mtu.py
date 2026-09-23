#!/usr/bin/env python3
"""
UDP-IP-01 (docs/ip-audit-2026-09-22.md): outbound datagrams are bounded by
the egress interface's MTU.  The only bound was the compile-time
NETDEV_MTU_MAX, so on a 1500-byte Ethernet a UDP payload of 1473..1572
octets left as a frame past the 1514-octet maximum -- which a conformant
switch or peer NIC discards -- and sendto() reported complete success.  We
do not fragment, so the send must fail EMSGSIZE instead.

    fits      a 1472-octet payload (the largest that fits in 1500) is sent,
              in one frame of exactly 1514 octets.
    too-big   a 1473-octet payload fails EMSGSIZE and puts nothing on the
              wire.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_mtu.py
"""
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire  # noqa: E402


def udp_frames(w):
    return [fr for mac, etype, fr in w.frames
            if etype == 0x0800 and fr[14 + 9] == 17 and
            socket.inet_ntoa(fr[30:34]) == '10.0.2.2']


def run(n):
    w = Wire.boot('udpany 9000 sendn:10.0.2.2:9001:%d sleep:60' % n)
    if not w.wait_serial('guest: sendn', 90):
        return w, None, 'guest never sent'
    w.pump(0.5)
    line = [l.strip() for l in w.serial().splitlines() if l.startswith('guest: sendn')][0]
    return w, line, None


def case_fits():
    w, line, err = run(1472)
    with w:
        if err:
            return err, w
        if 'ok n=1472' not in line:
            return 'send failed: %s' % line, w
        fr = udp_frames(w)
        if len(fr) != 1 or len(fr[0]) != 1514:
            return 'want one 1514-octet frame, got %r' % [len(f) for f in fr], w
        return None, w


def case_too_big():
    w, line, err = run(1473)
    with w:
        if err:
            return err, w
        if 'too long' not in line.lower() and 'emsgsize' not in line.lower():
            return 'expected EMSGSIZE, got: %s (frames %r)' % (
                line, [len(f) for f in udp_frames(w)]), w
        if udp_frames(w):
            return 'something was still put on the wire', w
        return None, w


def main():
    failed = 0
    for name, fn in (('fits', case_fits), ('too-big', case_too_big)):
        err, w = fn()
        if err:
            failed += 1
            print('FAIL  %s: %s' % (name, err))
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
