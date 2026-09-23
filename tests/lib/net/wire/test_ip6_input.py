#!/usr/bin/env python3
"""
UDP-IP-09 and UDP-IP-10 (docs/ip-audit-2026-09-22.md): what ip6_input()
accepts, and how far into a packet it looks.  ICMPv6 echo is the probe --
RFC 4443 4.2 says an echo request to a multicast group this node belongs to
should be answered -- because AF_INET6 sockets cannot be opened yet
(UDP-I-02), so no UDP socket can observe IPv6 delivery directly.

    all-nodes     an echo request to ff02::1 is answered: every node is a
                  member of the all-nodes group.
    other-group   one to ff05::1234, a group nobody joined, is not.  Every
                  ff00::/8 destination used to be accepted.
    hop-by-hop    an echo request behind a Hop-by-Hop options header is
                  answered.  The upper-layer switch used to dispatch on the
                  fixed header's Next Header alone, so it was dropped (as
                  was UDP, and every MLD message, behind any extension
                  header).

Run from the repo root after building sys/ and wireguest:
    python3 tests/lib/net/wire/test_ip6_input.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wire import Wire, GUEST_IP6, PEER_IP6  # noqa: E402


def mc_mac(tail4):
    return bytes([0x33, 0x33]) + tail4


def echo_req(ident, dst):
    return Wire.icmp6(128, 0, struct.pack('!HH', ident, 1) + b'probe', PEER_IP6, dst)


def is_reply(ident):
    def pred(d):
        nh, src, dst, pkt = d
        return nh == 58 and pkt[40] == 129 and struct.unpack('!H', pkt[44:46])[0] == ident
    return pred


def boot():
    w = Wire.boot('listen 7020 sleep:120')
    if not w.wait_serial('guest: listening', 90):
        return w, 'guest never came up'
    w.prime_nd6()
    w.pump(1.0)
    w.ip6_rx.clear()
    return w, None


def case_all_nodes():
    w, err = boot()
    with w:
        if err:
            return err, w
        w.send_ip6(58, echo_req(0x1111, 'ff02::1'), dst='ff02::1',
                   eth_dst=mc_mac(b'\0\0\0\x01'))
        if not w.expect_ip6(is_reply(0x1111), 3):
            return 'no echo reply to ff02::1', w
        return None, w


def case_other_group():
    w, err = boot()
    with w:
        if err:
            return err, w
        w.send_ip6(58, echo_req(0x2222, 'ff05::1234'), dst='ff05::1234',
                   eth_dst=mc_mac(b'\0\0\x12\x34'))
        if w.expect_ip6(is_reply(0x2222), 3):
            return 'answered an echo to a group nobody joined', w
        return None, w


def case_hop_by_hop():
    w, err = boot()
    with w:
        if err:
            return err, w
        body = echo_req(0x3333, GUEST_IP6)
        hbh = bytes([58, 0, 1, 4, 0, 0, 0, 0])        # next=ICMPv6, PadN(4)
        w.send_ip6(0, hbh + body)
        if not w.expect_ip6(is_reply(0x3333), 3):
            return 'no echo reply to a request behind Hop-by-Hop options', w
        return None, w


def main():
    failed = 0
    only = sys.argv[1:]
    for name, fn in (('all-nodes', case_all_nodes), ('other-group', case_other_group),
                     ('hop-by-hop', case_hop_by_hop)):
        if only and name not in only:
            continue
        err, w = fn()
        if err:
            failed += 1
            print('FAIL  %s: %s' % (name, err))
            print(w.dump()[-1500:])
        else:
            print('ok    %s' % name)
    print('Result: %s' % ('FAILED' if failed else 'PASSED'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
