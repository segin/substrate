# Wire-level TCP test harness

Tests that need a peer which sends *exact* segments -- a FIN retransmit
that also ACKs, an out-of-window probe, a SYN on an established
connection, a RST with a chosen sequence number -- which neither slirp
nor loopback will produce on demand.

- `wire.py` boots substrate under qemu with its NIC on a `-netdev dgram`
  backend, so every Ethernet frame is a UDP datagram to the host. The host
  plays the guest's gateway `10.0.2.2`, answers ARP, and gives each test
  full control of the TCP segments it sends. Each boot uses a reflink copy
  of `rootfs.img`; the real image is never written.
- `wireguest.c` is the guest side, run as init: it connects or listens and
  then runs a scripted list of socket actions (`readeof`, `read:N`,
  `write:TEXT`, `close`, `shutwr`, `sleep:N`), logging each as `guest: ...`.
- `test_*.py` are the scenarios, one file per checklist item or cluster in
  `docs/ip-audit-2026-09-22.md`.

Build and run from the repo root:

    make -C sys
    i386-unknown-substrate-gcc -O2 -o tests/lib/net/wire/wireguest \
        tests/lib/net/wire/wireguest.c
    python3 tests/lib/net/wire/test_tcp_closing.py

`WIRE_WORKDIR` chooses where the scratch image and serial log go (default
`/tmp`); put it on the same btrfs volume as `rootfs.img` so the copy is a
reflink rather than a 4 GiB write.
