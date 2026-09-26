#!/usr/bin/env python3
"""
IPv4 Identification across reboots (RFC 791 3.2).  The counter was
unseeded and started from 1 on every boot, so datagrams sent shortly after
a reboot reused the IDs of those sent shortly after the previous one.

    reboot      the Identification of the guest's first datagram differs
                between two boots (a 1-in-65536 chance of a false failure).

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip_ident.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire  # noqa: E402


def first_id():
    """(Identification of the guest's first IPv4 datagram, wire) or
    (None, wire)."""
    w = Wire.boot('udpany 7416 sendto:10.0.2.2:9:first sleep:60')
    with w:
        if not w.wait_serial('guest: sendto', 90):
            return None, w
        w.pump(0.5)
        for mac, et, fr in w.frames:
            if et == 0x0800:
                return struct.unpack('!H', fr[14 + 4:14 + 6])[0], w
        return None, w


def case_reboot():
    a, w = first_id()
    if a is None:
        return 'no datagram from the first boot', w
    b, w = first_id()
    if b is None:
        return 'no datagram from the second boot', w
    if a == b:
        return 'both boots started with Identification %d' % a, w
    return None, w


CASES = (('reboot', case_reboot),)


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
