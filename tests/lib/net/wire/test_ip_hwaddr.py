#!/usr/bin/env python3
"""
SIOCSIFHWADDR must reprogram the NIC's receive filter, not just the MAC the
stack advertises (RFC 791 2.3: the local-net address is the interface's).
When only the software copy changed, the host sent every frame from the new
MAC and ARP answered with it, while the NIC kept accepting unicast only to
the old one -- so all unicast to the host was lost.

    <nic>   for e1000 and virtio-net: after the guest sets MAC
            52:54:00:12:34:99, a UDP datagram to that MAC is delivered, and
            the guest's own next datagram leaves from it.

qemu's e1000 filters unicast by the programmed station address; virtio-net
is promiscuous by default, so its case exercises only the software side.
rtl8139 is not exercised: under this harness it receives nothing at all,
unicast to its original MAC included, on the baseline kernel too (see
test_ip_multicast.py), so its IDR programming is review-only.  qemu has no
r8168 model.

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_hwaddr.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, GUEST_IP  # noqa: E402

PORT = 7411
NEW_MAC = bytes.fromhex('525400123499')
NEW_MAC_STR = ':'.join('%02x' % b for b in NEW_MAC)


def case_nic(nic):
    with Wire.boot('udpany %d hwaddr:%s recvsum:100 sendto:10.0.2.2:9:after '
                   'sleep:60' % (PORT, NEW_MAC_STR), nic=nic) as w:
        if not w.wait_serial('guest: hwaddr', 90):
            return 'guest never set the address', w
        line = [l for l in w.serial().splitlines() if 'guest: hwaddr' in l][0]
        if 'hwaddr ok' not in line:
            return 'SIOCSIFHWADDR failed: %s' % line, w
        w.pump(0.5)
        w.send_udp(40011, PORT, b'to-new-mac', dst=GUEST_IP, eth_dst=NEW_MAC)
        if not w.wait_serial('guest: recvsum', 10):
            return 'unicast to the new MAC was not received', w
        w.wait_serial('guest: sendto', 5)
        w.pump(1.0)
        srcs = {fr[6:12] for mac, et, fr in w.frames if et == 0x0800}
        if srcs != {NEW_MAC}:
            return 'guest IPv4 frames came from %s' % \
                sorted(s.hex(':') for s in srcs), w
        return None, w


CASES = tuple((nic, (lambda n=nic: case_nic(n)))
              for nic in ('e1000', 'virtio-net-pci'))


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
