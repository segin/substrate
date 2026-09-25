# IPv4 audit — 2026-09

This report audits substrate's IPv4 layer against **RFC 791**
(J. Postel, September 1981, vendored at `docs/rfc/rfc791.txt`). It covers
the host side of the protocol. Where RFC 1122 restates, relaxes or
sharpens a 791 requirement for hosts, the finding says so.

Files audited:

- `sys/net/inet.c` (840 lines): IPv4 input and output, route and source
  selection, reassembly, and the Internet checksum.
- `sys/net/icmp.c` (336 lines): the ICMP module that RFC 791 places
  inside IP.
- `sys/net/loopback.c` (149 lines).
- `sys/net/netdev.c`.
- The IP-facing parts of the following files:
  - `sys/net/af_inet.c`: source selection, the `SIOCSIF*` ioctls and
    the UDP/raw SEND paths.
  - `sys/net/af_unix.c`: `setsockopt` for every family.
  - `sys/net/tcp.c`: TCP's use of the IP SEND interface.
  - The receive-filter setup in `sys/drivers/net/{e1000,rtl8139,r8168}.c`
    and `sys/drivers/virtio/virtio_net.c`.

**Gateways are out of scope by design.** RFC 791 specifies gateways as
well as hosts. Substrate is a host and does not forward, so every
gateway-only requirement was excluded. That covers TTL decrement on
forwarding, fragmenting a datagram in transit, inserting a Record Route
entry while routing, and substituting a source-route hop in order to
forward. Where a gateway rule has a host counterpart, the host
counterpart is what is audited. For example, OPT-02 grades a host that
receives a source-routed datagram which is not addressed to it.

**Method.** Multiple analysis lenses covered:

- identification and fragmentation
- options
- addressing and source selection
- routing and the link interface
- the SEND/RECV user interface
- ICMP
- header construction and checksum

Two or three independent agents then re-verified every candidate. Each
verifier checked three things: that the code does what the finding
says, that the RFC requires what the finding says, and that the path is
reachable. 32 candidates were refuted and are excluded. The 41 that
survived include many near-duplicates raised by different lenses, and
they are merged below into **26 unique defects: 3 high, 11 medium,
9 low, 3 info.**

**Line numbers refer to `6d6279d8b` (2026-09-25).** The findings were
raised against `2d50587eb` (2026-09-22). Sixteen commits have changed the
IP layer since then:

- IPv4 reassembly (`6a570ce8f`, UDP-I-01).
- The UDP audit's IP-layer items UDP-IP-01..11.
- ICMP error input and Port Unreachable (UDP-ICMP-01/-02).
- Per-socket TTL/TOS/multicast options (UDP-API-12).
- The source-address plumbing (TCP-HDR-01).

Every finding was re-checked against the current source and re-cited at
current lines. **None of the 26 is fixed at `6d6279d8b`.** Seven had
part of their original evidence overtaken by those commits. Each of
those says so under **Since 2d50587eb**, and the finding is graded on
what remains. Where a finding overlaps an item already tracked in
`docs/ip-audit-2026-09-22.md`, it names that item.

**Spot-check.** The three highest-severity findings (ADDR-01, ADDR-02
and RT-01) were re-read against the current source before write-up, and
all three hold. One detail of ADDR-01's original failure scenario did
not hold as stated and is corrected in the finding. The claim was that
the ICMP reply "leaks eth0's address onto eth1's segment". In fact
`icmp_input` routes the reply by the requester's address, so it leaves
through whichever configured interface routing picks. RT-01 turned out
to be worse than reported: the original said the misconfigured
interface must be first in the netdev list, but it captures every
off-link destination in any list order.

**Two proposed fixes were corrected:**

- **ID-02.** The finding as raised seeded the counter under
  `random_get_bytes(...) == 0`. That function returns the byte count, not
  0, which is the inverted test that once broke TCP ephemeral ports.
- **ID-01.** The finding proposed intr_disable() as one alternative. On
  its own that does not survive SMP, so the fix given is the atomic.

---

## Findings

| ID | Sev | RFC 791 requirement | Summary | Site |
|----|-----|---------------------|---------|------|
| ADDR-01 | high | 3.2 Addressing | A datagram addressed to 0.0.0.0 is accepted as unicast-to-us on any interface with no address | `sys/net/inet.c:718` |
| ADDR-02 | high | 3.3 SEND (source check) | A caller-supplied source address is never checked; broadcast, multicast and stale sources leave on the wire | `sys/net/inet.c:374` |
| RT-01 | high | 1.4 Operation (routing) | An interface with netmask 0 claims every destination as on-link; an address-only `ifconfig` produces this | `sys/net/inet.c:262` |
| ID-01 | medium | 2.3, 3.2 Identification | The ID counter is a non-atomic RMW followed by a re-read; concurrent senders stamp the same ID | `sys/net/inet.c:405` |
| OPT-01 | medium | 3.1 Options, 3.2 Options | The option area is never parsed, validated or acted on | `sys/net/inet.c:664` |
| OPT-02 | medium | 3.1 LSRR/SSRR | A source-routed datagram whose route is not exhausted is consumed as if it were for us | `sys/net/inet.c:718` |
| ADDR-03 | medium | 3.3 SEND (source check) | The broadcast and multicast route arms pick an unaddressed interface and send from 0.0.0.0 | `sys/net/inet.c:218` |
| ADDR-04 | medium | 2.3 Addressing | UDP with `IP_MULTICAST_IF` leaves interface B with interface A's source | `sys/net/af_inet.c:833` |
| RT-02 | medium | 1.4 Operation | No Ethernet destination-MAC check; link-broadcast frames carrying unicast IP are taken as unicast | `sys/net/inet.c:772` |
| RT-03 | medium | 2.3 Addressing | `SIOCSIFHWADDR` changes the advertised MAC but not the NIC's filter; all unicast to the host is lost | `sys/net/af_inet.c:532` |
| API-01 | medium | 3.3 SEND | `IP_HDRINCL` reports success, and the caller's header is then sent as payload | `sys/net/af_unix.c:2881` |
| API-02 | medium | 3.3 SEND (result) | TCP discards `ip4_output`'s error; `connect()` to an unroutable host ends in ETIMEDOUT | `sys/net/tcp.c:675` |
| ICMP-01 | medium | Glossary "ICMP"; RFC 1122 3.2.2.6 | Echo from any 127/8 source is dropped, so `ping 127.0.0.1` never gets a reply | `sys/net/icmp.c:110` |
| ICMP-02 | medium | 3.2 Errors; RFC 1122 4.2.3.9 | ICMP errors reach UDP only; TCP never learns of Unreachable, Time Exceeded or Parameter Problem | `sys/net/icmp.c:55` |
| ID-02 | low | 3.2 Identification | The ID counter is unseeded BSS and restarts at 1 on every boot | `sys/net/inet.c:282` |
| ID-03 | low | 2.3, 3.2 Identification | One host-wide 16-bit counter, drained by loopback too, with DF never set | `sys/net/inet.c:405` |
| ADDR-05 | low | 3.2 Addressing, 3.2 robustness | UDP sends datagrams addressed to 0/8 via the default gateway | `sys/net/inet.c:269` |
| API-03 | low | 3.3 SEND (DF, Id) | DF cannot be set; `IP_MTU_DISCOVER` reports success | `sys/net/inet.c:406` |
| API-04 | low | 3.3 SEND (result) | `lo_xmit` returns success when its ring is full and the datagram is dropped | `sys/net/loopback.c:91` |
| ICMP-03 | low | 3.3 Interfaces (RECV) | No ICMP Protocol Unreachable; `afinet_deliver_v4`'s result is discarded | `sys/net/inet.c:759` |
| ICMP-04 | low | 3.1 Total Length; RFC 1122 3.2.2.6 | An echo request whose reply exceeds 1480 octets is dropped silently | `sys/net/icmp.c:117` |
| ICMP-05 | low | 3.2 Addressing; RFC 1122 3.2.2.6 | The echo reply's source is chosen by routing, not taken from the request's destination | `sys/net/icmp.c:124` |
| FRAG-01 | low | 3.2 Example Reassembly Procedure | A whole datagram does not flush a pending reassembly with the same BUFID | `sys/net/inet.c:724` |
| HDR-01 | info | 3.1 Header Checksum | `inet_csum()` hard-codes little-endian order in its return value | `sys/net/inet.c:43` |
| OPT-03 | info | 3.1 Options, Record Route, Timestamp | No option can be originated: IHL is fixed at 5 (acknowledged) | `sys/net/inet.c:402` |
| ADDR-06 | info | 3.2 Addressing | One IPv4 address per interface (acknowledged) | `sys/include/sys/netdev.h:52` |

---

# Part 1 — Addressing and routing

### ADDR-01 (high). A datagram addressed to 0.0.0.0 is accepted as unicast-to-us on any interface with no address

Merges three verified reports (high, medium, medium).

**RFC 791 3.2**, Addressing (`docs/rfc/rfc791.txt:1616-1617`):

> A value of zero in the network field means this network.  This is
> only used in certain ICMP messages.

RFC 1122 3.2.1.3(a) confirms rather than relaxes this: `{0, 0}` may
appear only as a source. The host receive rule accepts only the host's
own addresses, broadcast and multicast.

**Code.** The only destination test is `if (ih->daddr != dev->ip4_addr
&& !for_bcast && !for_lo) return;` at `sys/net/inet.c:718`.
`dev->ip4_addr` is "0 = unconfigured" (`sys/include/sys/netdev.h:52`).
On such an interface a datagram to 0.0.0.0 therefore satisfies
`daddr == dev->ip4_addr` and is accepted:

- `for_bcast` is 0. The subnet-broadcast clause at `:706-707` is gated on
  `dev->ip4_netmask != 0`, and 0.0.0.0 is not 255.255.255.255.
- Because `for_bcast` is 0, the datagram gets full unicast treatment:
  - TCP is not discarded at `:756`.
  - UDP can answer with Port Unreachable.
  - The echo broadcast filter at `sys/net/icmp.c:97-100` passes it.

The martian filter at `sys/net/inet.c:680-696` looks only at the
source. The unaddressed state is ordinary, and three routes lead to it:

- `inet_init` configures only the first non-loopback NIC
  (`sys/net/inet.c:808-822`), while `sys/kern/main.c:742-744` probes
  rtl8139, e1000 and r8168 in turn, so every further NIC stays at 0.
- `SIOCSIFADDR` stores any value (`sys/net/af_inet.c:542-546`).
- `sbin/dhclient/dhclient.c:778-779` sets the address and gateway to 0
  whenever the client returns to INIT.

**Failure.** The host has two NICs, eth0 = 10.0.2.15/24 and eth1
unaddressed. An on-link attacker on eth1's segment sends a
link-broadcast frame, so no MAC is needed (see RT-02), carrying
daddr = 0.0.0.0, a valid header checksum and a TCP SYN to port 22.
`ip4_input` accepts it and `tcp_input` runs. `tcp_find`'s listener
scan skips only listeners bound to a different address
(`sys/net/tcp.c:1140`), so every INADDR_ANY service matches. The
SYN-ACK is routed by the attacker's source and leaves through eth0,
because the gateway arm at `sys/net/inet.c:269-273` requires an address
and eth1 has none. Two-way connectivity follows whenever that source is
routable.

With protocol 17 the payload reaches every wildcard-bound UDP socket:
`sock_score` compares the local address only when it is bound
(`sys/net/af_inet.c:2472`). With an ICMP echo the host answers from
eth0's address. The same frame, sent while `dhclient` is in INIT on a
single-NIC host, injects one-way into every wildcard UDP socket.

**Fix.** At `sys/net/inet.c:718`, make the unicast match
`dev->ip4_addr != 0 && ih->daddr == dev->ip4_addr`, and drop any
destination in 0/8 before that test, mirroring the source filter at
`:682`. An interface with no address still accepts 255.255.255.255,
which DHCP needs.

### ADDR-02 (high). A caller-supplied source address is never checked; broadcast, multicast and stale sources leave on the wire

This merges three verified reports: the missing check at send time, the
source being checked only at `bind()`, and the source going stale after
`SIOCSIFADDR`.

**RFC 791 3.3**, Interfaces (`docs/rfc/rfc791.txt:2153-2156`):

> The source address is included in the send call in case the sending
> host has several addresses (multiple physical connections or logical
> addresses).  The internet module must check to see that the source
> address is one of the legal address for this host.

RFC 1122 3.2.1.3 adds that a host MUST NOT send a datagram whose source
is a broadcast or multicast address.

**Code.** The only check `ip4_output_opts` applies to a nonzero `saddr`
refuses 127/8 on a non-loopback device (`sys/net/inet.c:374-377`).
Nothing rejects the following sources:

- 255.255.255.255
- a subnet-directed broadcast
- a class D group
- an address no interface holds

The transports pass their bound address straight through:

- **UDP.** `udp_src4` returns `s->local_addr` whenever it is nonzero
  (`sys/net/af_inet.c:833-837`, used at `:930` and `:2109`).
- **TCP.** `tcp_xmit_raw` uses `p->laddr` (`sys/net/tcp.c:541`,
  `:548`), and `tcp_bind` stores whatever it is given
  (`sys/net/tcp.c:2263-2274`).

`bind()` admits exactly those addresses, because receivers need them:
`afinet_addr_bindable4` accepts 255.255.255.255 (`sys/net/af_inet.c:1198`),
all of 224/4 (`:1199`) and every interface's broadcast (`:1203-1206`).

The check runs only at `bind()`. `SIOCSIFADDR` (`sys/net/af_inet.c:542-546`)
rewrites `dev->ip4_addr` without touching sockets or PCBs already bound
to the old address. It also accepts 255.255.255.255 or a class D value
as the interface address, which `route_src4` (`sys/net/inet.c:297-301`)
then stamps on every routed datagram. `SIOCSIFADDR` is root-only
(CFG-01, `sys/net/af_inet.c:489-497`). `bind()` and `sendto()` are not.

**Failure.** Three unprivileged cases, one per kind of bad source:

- **Multicast source.** A UDP socket binds 224.0.0.251:5353 to receive
  mDNS, then calls `sendto(10.0.2.2:5353)`. The unicast datagram leaves
  with IP source 224.0.0.251.
- **Broadcast source.** A socket binds 10.0.2.255, then calls
  `connect(10.0.2.2:80)`. The SYN's source is the subnet broadcast, so
  the peer either drops it as a martian or broadcasts its SYN-ACK to
  the segment.
- **Stale source.** A daemon binds 10.0.2.15, and the administrator
  re-addresses eth0 to 10.0.2.20. Every later send still carries
  10.0.2.15. The replies go to whichever host now holds that address.

**Fix.** In `ip4_output_opts`, when `saddr != 0`, accept it only if it
is one of the following, and otherwise return `-EADDRNOTAVAIL`:

- a 127/8 address on lo;
- some interface's current `ip4_addr`;
- the local address looped back through lo (`ip4_is_local_ifaddr`,
  `sys/net/inet.c:166`).

Then make `udp_src4` and `tcp_connect_start` fall back to routing
(`saddr = 0`) when the bound address is broadcast or multicast, as BSD
and Linux do. Finally, have `SIOCSIFADDR` refuse 255.255.255.255, 224/4
and 127/8 off lo.

**Tracking.** This extends UDP-API-06, which validates the address at
`bind()` time only.

### RT-01 (high). An interface with netmask 0 claims every destination as on-link; an address-only `ifconfig` produces this

**RFC 791 1.4**, Operation (`docs/rfc/rfc791.txt:310-312`):

> The internet modules use the addresses carried in the internet header
> to transmit internet datagrams toward their destinations.  The
> selection of a path for transmission is called routing.

The mask itself comes from RFC 950 and RFC 1122, but the route produced
here does not lead toward the destination.

**Code.** The direct-delivery test in `route_for_v4` is
`(d->ip4_addr & d->ip4_netmask) == (daddr & d->ip4_netmask)`
(`sys/net/inet.c:262-263`). It is guarded on `ip4_addr` (`:261`) but not
on `ip4_netmask`, so a zero mask reduces the test to `0 == 0`. The
state is reachable in two ordinary ways:

- **An address-only `ifconfig`.** `SIOCSIFADDR` leaves `ip4_netmask`
  untouched (`sys/net/af_inet.c:542-546`), and `ifconfig` issues
  `SIOCSIFADDR` alone when no `netmask` keyword is given
  (`sbin/ifconfig/ifconfig.c:352`). Every NIC beyond the first starts
  with mask 0 (`sys/net/inet.c:808-822`).
- **An explicit zero mask.** `SIOCSIFNETMASK` accepts 0
  (`sys/net/af_inet.c:554-558`).

The on-link loop (`:258-267`) runs to completion before the gateway
loop (`:269-274`). So for any off-link destination the zero-mask
interface wins, whatever its position in the netdev list.

**Failure.** eth0 is 10.0.2.15/24 via 10.0.2.2. Root runs
`ifconfig eth1 192.168.1.5`. From then on:

1. `route_for_v4(8.8.8.8)` fails eth0's on-link test.
2. It matches eth1 with `via_gw = 0`.
3. `ip4_output_opts` ARPs for 8.8.8.8 itself on eth1's segment
   (`sys/net/inet.c:436-438`).
4. Nothing answers. After the 32-iteration wait (`:455-462`) the send
   fails `EHOSTUNREACH`.

Every outbound off-link connection on the host fails, including ones
that worked a moment earlier. On a single-NIC host,
`ifconfig eth0 netmask 0.0.0.0` reaches the same state.

**Fix.** Add `|| !d->ip4_netmask` to the `continue` at
`sys/net/inet.c:261`. Also have `SIOCSIFADDR` install the classful mask
when `ip4_netmask` is still 0. `dhclient.c:254` already computes one for
the same purpose.

### ADDR-03 (medium). The broadcast and multicast route arms pick an unaddressed interface and send from 0.0.0.0

**RFC 791 3.3** (`docs/rfc/rfc791.txt:2155-2156`):

> The internet module must check to see that the source address is one
> of the legal address for this host.

**Code.** The limited-broadcast arm (`sys/net/inet.c:218-227`) and the
multicast arm (`:234-243`) return the first UP interface with the
matching capability flag. Neither looks at `ip4_addr`, whereas the
unicast arms do (`:261`, `:271`). `route_src4` then returns that
device's `ip4_addr`, which is 0 (`:300`). The zero reaches the wire by
either of two paths, and nothing rejects it:

- **Routed source.** `ip4_source_for` returns 0 (`:306`) and `udp_src4`
  passes it on.
- **Unbound socket.** `ip4_output_opts` re-derives 0 at `:375`.

The comment at `:215-216` intends 0.0.0.0 for DHCP on an unconfigured
host, which RFC 1122 3.2.1.3(a) permits. The code also does it when the
host has a configured interface later in the list, and it does it for
multicast, where no bootstrap exception applies.

**Failure.** eth0 is unaddressed and first in the list (the list is
head-inserted, `sys/net/netdev.c:56-57`), and eth1 is 192.168.1.5.
`sendto(255.255.255.255)` with `SO_BROADCAST`, or `sendto(239.1.2.3)`
from an unbound socket, leaves eth0 with source 0.0.0.0. Receivers
cannot answer it.

**Fix.** In both arms, prefer an UP interface with a nonzero `ip4_addr`.
Fall back to an unaddressed one only when no interface has an address.
Refuse a zero source for class D destinations.

**Since 2d50587eb.** Both arms were added after the original audit, by
UDP-IP-04/-07 (`607637c04`) and UDP-IP-06 (`22641b8f9`). This is a
defect in those fixes.

### ADDR-04 (medium). UDP with `IP_MULTICAST_IF` leaves interface B with interface A's source

**RFC 791 2.3**, Addressing (`docs/rfc/rfc791.txt:638-640`):

> That is, provision must be made for a host to have several physical
> interfaces to the network with each having several logical internet
> addresses.

**Code.** `ip4_output_opts` picks the egress device from `o->mcast_if`
(`sys/net/inet.c:357-361`). The UDP send paths compute the source before
that, with `udp_src4()` → `ip4_source_for(daddr)`
(`sys/net/af_inet.c:836`, called at `:930` and `:2109`). That goes
through `route_for_v4`'s multicast arm, which returns the first
multicast-capable interface and knows nothing of `IP_MULTICAST_IF`
(`sys/net/inet.c:234-242`). The resulting nonzero `saddr` is used
unchanged, because only `saddr == 0` is substituted (`:374`). The raw
path passes `saddr = 0` and gets the right source, so the two paths
disagree.

**Failure.** eth0 = 10.0.2.15 and eth1 = 192.168.1.5, both multicast
capable. An unbound UDP socket sets `IP_MULTICAST_IF` = 192.168.1.5 and
sends to 239.1.2.3. The datagram leaves eth1 with source 10.0.2.15, an
address that is not on that link, and replies from 192.168.1.0/24
never arrive. If eth0 is unaddressed, the source is 0.0.0.0.

**Fix.** Pass the socket's `ip4_txopts` into source selection. For a
class D destination with `mcast_if` set, the source is `mcast_if`.

**Since 2d50587eb.** `IP_MULTICAST_IF` was added by UDP-API-12
(`144f66c9f`). This is a defect in that fix.

### ADDR-05 (low). UDP sends datagrams addressed to 0/8 via the default gateway

Merges two verified reports (medium, low).

**RFC 791 3.2**, Addressing (`docs/rfc/rfc791.txt:1616-1617`) and
robustness (`:1566-1568`):

> A value of zero in the network field means this network.  This is
> only used in certain ICMP messages.

> In general, an implementation must be conservative in its sending
> behavior, and liberal in its receiving behavior.  That is, it must be
> careful to send well-formed datagrams

**Code.** `afinet_sendto_k` copies `sin_addr` unchecked
(`sys/net/af_inet.c:2049-2054`). It refuses only destination port 0 and
a broadcast sent without `SO_BROADCAST` (`:2069-2074`).
`ip4_output_opts` does not classify `daddr`. In `route_for_v4`, 0.0.0.0
matches none of the following:

- limited broadcast (`sys/net/inet.c:218`)
- multicast (`:234`)
- 127/8 or a local address (`:248`; `ip4_is_local_ifaddr(0)` is 0 at
  `:167`)
- the on-link test for a nonzero mask

It therefore falls to the gateway arm (`:269-273`). The TCP connect path
maps INADDR_ANY to 127.0.0.1 (`sys/net/af_inet.c:1923`); the UDP and raw
paths do not.

**Failure.** An unprivileged
`sendto(udp, "x", 1, 0, {AF_INET, 53, 0.0.0.0})` returns 1. A frame with
IP destination 0.0.0.0 is sent to the router's MAC. It can never be
answered, and a strict segment logs it as a martian.

**Fix.** At the top of `ip4_output_opts`, return `-EINVAL` for a
destination in 0/8. Alternatively, map 0.0.0.0 to 127.0.0.1 in the
datagram send and connect paths, as `:1923` does for TCP.

**Since 2d50587eb.** The original report's other points no longer hold:

- Raw sockets are root-only (`sys/net/af_inet.c:1090`).
- Class D destinations are no longer ARPed (UDP-IP-06, `22641b8f9`).

### RT-02 (medium). No Ethernet destination-MAC check; link-broadcast frames carrying unicast IP are taken as unicast

**RFC 791 1.4**, Operation (`docs/rfc/rfc791.txt:554-556`), with
RFC 1122 3.3.6:

> At this destination host the datagram is stripped of the local net
> header by the local network interface and handed to the internet
> module.

**Code.** `inet_eth_input` (`sys/net/inet.c:772-791`) looks only at the
ethertype. It never compares the destination MAC with `dev->hwaddr`,
with broadcast, or with a joined 01:00:5e group. `ip4_input` then
decides from the IP destination alone (`:705-720`), and `for_bcast` is
derived from the IP address only. Whether other stations' frames get
through depends entirely on the NIC's hardware filter, and virtio-net
has none: `virtio_net.c` negotiates only `VIRTIO_NET_F_MAC`
(`sys/drivers/virtio/virtio_net.c:344`), not `CTRL_RX`, so QEMU leaves
the device in its reset-default promiscuous mode. The driver notes this
itself at `:390-391`. virtio is the recommended boot path.

Independently of the NIC, RFC 1122 3.3.6 says a datagram with a unicast
IP destination that arrives in a link-layer broadcast SHOULD be
discarded. That RFC is already cited on the output side at
`sys/net/inet.c:419`.

**Failure.** eth0 is 10.0.2.15. A frame to `ff:ff:ff:ff:ff:ff`, or to
another host's MAC on virtio, carrying an ICMP echo or a UDP datagram to
10.0.2.15 is processed as unicast. The host answers it, and emits a Port
Unreachable if the port is closed. ADDR-01 uses the same property to
reach an interface whose MAC the attacker does not know.

**Fix.** In `inet_eth_input`, on non-loopback devices, accept only
frames addressed to `dev->hwaddr`, to broadcast, or to a 01:00:5e MAC.
Pass a link-broadcast/multicast flag into `ip4_input`, and drop a
datagram that arrived that way unless its IP destination is broadcast
or multicast.

### RT-03 (medium). `SIOCSIFHWADDR` changes the advertised MAC but not the NIC's filter; all unicast to the host is lost

**RFC 791 2.3**, Addressing (`docs/rfc/rfc791.txt:617-618`):

> The internet module maps internet addresses to local net addresses.

**Code.** `SIOCSIFHWADDR` is only `memcpy(dev->hwaddr, ...)`
(`sys/net/af_inet.c:532-534`). `struct netdev_ops` has no set-address
hook (`sys/include/sys/netdev.h:28-38`), and no driver reprograms its
station-address filter:

| Driver | Reads its MAC once at | Receives in physical-match mode at |
|---|---|---|
| e1000 | `RAL0`/`RAH0`, `sys/drivers/net/e1000.c:322-323` | `:402`, no `RCTL_UPE` |
| rtl8139 | `IDR0` | `RCR_APM`, `sys/drivers/net/rtl8139.c:129`, `:321` |
| r8168 | `IDR0`, `sys/drivers/net/r8168.c:456` | `RCR_APM`, `:585` |

ARP replies and `eth_send` (`sys/net/inet.c:147`) then advertise and
source the new MAC, while the hardware accepts only the old one.

**Failure.** On e1000, root sets 52:54:00:aa:bb:cc and the ioctl
returns 0. Peers learn the new MAC from ARP and send unicast IP to it.
The NIC drops every such frame in hardware. Only broadcast and multicast
still arrive, so TCP and unicast UDP stop working.

**Fix.** Add a `set_hwaddr` operation to `netdev_ops` and implement it
per driver: `RAL0`/`RAH0`, `IDR0` behind the config unlock, and virtio
`CTRL_MAC_ADDR`. `SIOCSIFHWADDR` should return `-EOPNOTSUPP` for a
driver without one.

### ADDR-06 (info). One IPv4 address per interface (acknowledged)

**RFC 791 3.2**, Addressing (`docs/rfc/rfc791.txt:1624-1627`):

> That is, there must be a mapping between internet host addresses and
> network/host interfaces that allows several internet addresses to
> correspond to one interface.

**Code.** `netdev_t` carries one `ip4_addr` and one netmask
(`sys/include/sys/netdev.h:52`). `SIOCSIFADDR` replaces the address
rather than adding one (`sys/net/af_inet.c:545`), and the is-for-me test
compares against that single address (`sys/net/inet.c:718`).

791's other half, treating several interfaces as one host, is
legitimately relaxed by RFC 1122 3.3.4.2's Strong ES model, so the
per-interface scoping is not a finding. The file header acknowledges the
limitation (`sys/net/inet.c:5-8`): "Plenty for a one-NIC test rig;
multi-NIC routing comes later."

**Failure.** `ifconfig eth0 10.0.2.16`, run to add a second service
address, replaces 10.0.2.15. Services on the old address go dark, and
every routed datagram is re-sourced from 10.0.2.16.

**Fix.** Replace the single pair with a small list of (addr, mask)
entries, and iterate it in the accept test (`:718`), in the
directed-broadcast computation (`:705`), and in `route_for_v4`.

---

# Part 2 — Identification

### ID-01 (medium). The ID counter is a non-atomic RMW followed by a re-read; concurrent senders stamp the same ID

Merges four verified reports (medium, low, medium, low).

**RFC 791 2.3** (`docs/rfc/rfc791.txt:683-687`) and **3.2**
(`:1902-1905`):

> The originating protocol module of an internet datagram sets the
> identification field to a value that must be unique for that
> source-destination pair and protocol for the time the datagram will
> be active in the internet system.

> Thus, the sender must choose the Identifier to be unique for this
> source, destination pair and protocol for the time the datagram (or
> any fragment of it) could be alive in the internet.

**Code.** `ih->id = __builtin_bswap16(++g_ip_id_counter);`
(`sys/net/inet.c:405`) operates on a plain `static uint16_t`
(`:282`). The current object (`objdump -d sys/net/inet.o`, inside
`<ip4_output_opts>`) compiles it to four separate memory operations:

```
b4d: 66 a1 ae 0c 00 00   mov  0xcae,%ax      ; load g_ip_id_counter
b53: 40                  inc  %eax
b54: 66 a3 ae 0c 00 00   mov  %ax,0xcae      ; store
b5a: 66 a1 ae 0c 00 00   mov  0xcae,%ax      ; RE-LOAD for the header
b60: 86 e0               xchg %ah,%al
b65: 66 89 42 04         mov  %ax,0x4(%edx)  ; ih->id
```

`ip4_output_opts` runs from both hard-IRQ and process context:

- **Hard IRQ.** NIC RX → `netdev_rx` (`sys/drivers/net/e1000.c:184`,
  `rtl8139.c:161`) → `ip4_input` → `icmp_input` echo replies, TCP ACKs
  and RSTs, and Port Unreachable. The STACK-01 comment at
  `sys/net/inet.c:99-101` describes this chain.
- **Process context,** with interrupts enabled: `sendto`/`write`
  (`sys/net/af_inet.c:932`, `:939`, `:2086`, `:2111`) and the
  loopback kthread.

That gives two windows:

- **Load–store (b4d–b54).** An interrupt here loses an update, and both
  senders use the same value.
- **Store–reload (b54–b5a).** An interrupt here makes the outer sender
  adopt the inner sender's ID verbatim. The collision is then
  guaranteed, not probabilistic.

Every datagram leaves with `frag_off = 0` (`:406`), so DF is clear and
a router may fragment it. RFC 6864's relaxation for atomic datagrams
therefore does not apply.

**Failure.** A process thread sends TCP to peer P and is between
`b54` and `b5a`. The NIC IRQ delivers a segment from P, and `tcp_input`
ACKs it through a nested `ip4_output_opts`, taking ID N+1. The outer
send resumes, re-reads N+1 and ships with it. Two datagrams now share
(src, dst, 6, N+1). If a smaller-MTU hop fragments both, P's reassembler
keys them to one buffer (`docs/rfc/rfc791.txt:723-726`) and splices
fragments of the two. The TCP checksum usually catches this, costing a
retransmit. A UDP peer with a zero checksum gets silently corrupted
data.

**Fix.** Allocate once, atomically, into a local:
`uint16_t id = (uint16_t)(__atomic_add_fetch(&g_ip_id_counter, 1,
__ATOMIC_RELAXED)); ih->id = __builtin_bswap16(id);`. Do not re-read
the global. Wrapping the increment in `intr_disable()`/`intr_restore()`
fixes UP only and must not be the fix: the SMP note at `:113-114`
applies.

### ID-02 (low). The ID counter is unseeded BSS and restarts at 1 on every boot

**RFC 791 3.2** (`docs/rfc/rfc791.txt:1902-1905`): the quote under ID-01.

**Code.** `static uint16_t g_ip_id_counter;` (`sys/net/inet.c:282`) has
no initialiser, and `inet_init` (`:806-840`) never seeds it. After every
boot the host emits IDs 1, 2, 3, and so on. TTL is 64 by default
(`:334`), which RFC 791 3.1 makes a lifetime bound of up to 64 seconds
(`docs/rfc/rfc791.txt:341-346`). A reboot inside that window, which
under QEMU takes seconds, re-issues the IDs of the host's own
still-live datagrams. A boot-deterministic global counter is also the
classic idle-scan side channel: one elicited datagram reveals the
host's packet count since boot.

**Failure.** The host sends a datagram that gets fragmented and
delayed, then reboots. An NFS or DNS client reconnects at once to the
same peer with the same protocol. Its datagram carries ID 1, which the
peer still holds partial fragments for, and the peer splices the two.

**Fix.** Seed the counter once in `inet_init` from the CSPRNG, as
`tcp_new_iss` does (`sys/net/tcp.c:423-434`). Use
`if (random_get_bytes(&seed, sizeof(seed)) == (int)sizeof(seed))
g_ip_id_counter = seed;`. **`random_get_bytes()` returns the byte
count, not 0.** The `== 0` test in the finding as originally raised
would never seed.

### ID-03 (low). One host-wide 16-bit counter, drained by loopback too, with DF never set

Merges three verified reports (low, low, info).

**RFC 791 3.2** (`docs/rfc/rfc791.txt:1911-1913`), which qualifies the
ID-01 quote:

> However, since the Identifier field allows 65,536 different values,
> some host may be able to simply use unique identifiers independent
> of destination.

**Code.** One counter serves every destination and protocol
(`sys/net/inet.c:282`, `:405`). That design is sanctioned by the
passage above, provided 65536 is large relative to the traffic in one
datagram lifetime. Two things break that proviso:

- **Loopback drains the counter.** The ID is assigned after the device
  is chosen (`:362`) but for every device, so loopback datagrams draw
  from it too. Loopback is the fastest producer on the host, and its
  datagrams never meet a router, so they never need an ID.
- **DF is never set.** `frag_off` is always 0 (`:406`), so every one of
  these datagrams remains fragmentable and the ID stays load-bearing.

**Failure.** A local UDP pair on 127.0.0.1 runs at 100 k datagrams/s,
which wraps the counter every 0.65 s. A TCP connection to a remote peer
across a 1400-MTU tunnel sends two full-size segments 3 s apart, and
their IDs can coincide. The tunnel router fragments both, and the peer
splices them. Both segments then fail the checksum, and the connection
stalls on loss caused by unrelated local traffic. Even without loopback
traffic, about 1024 full-size datagrams/s wraps the counter inside the
64-second TTL window.

**Fix.** Do not consume the counter for `NETDEV_IFF_LOOPBACK` egress:
use 0, or a separate counter, since lo never fragments. Set DF on TCP
segments (they are MSS-clamped already, TCP-HDR-04), which makes them
RFC 6864 atomic datagrams. Longer term, key the counter per
(daddr, protocol) through a small hash, as Linux and the BSDs do.

---

# Part 3 — Options

### OPT-01 (medium). The option area is never parsed, validated or acted on

Merges the input halves of two verified reports (medium, info).

**RFC 791 3.1**, Options (`docs/rfc/rfc791.txt:1067-1070`) and
**3.2**, Options (`:2012-2015`):

> The options may appear or not in datagrams.  They must be
> implemented by all IP modules (host and gateways).  What is optional
> is their transmission in any particular datagram, not their
> implementation.

> The options are optional in each datagram, but required in
> implementations.  That is, the presence or absence of an option is
> the choice of the sender, but each internet module must be able to
> parse every option.

**Code.** `ip4_input` uses IHL to bound, checksum and skip the header
(`sys/net/inet.c:664-670`, `:736`), and never reads a byte of
`pkt[20..hlen)`. There is no option walker and no `IPOPT_*` constant in
the tree (`grep -rn IPOPT sys/ include/` finds nothing). The
consequences:

- **No option is recognised.** EOL and NOP are never recognised, and
  Record Route (7), Timestamp (68), Stream ID and Security are ignored.
- **Nothing is validated.** A zero length octet, or a length running
  past `hlen`, is accepted without comment. That is harmless today only
  because nothing walks the area.
- **Nothing is passed up.** Received options never reach the transport,
  so a completed source route cannot be reversed for the reply, which
  RFC 1122 3.2.1.8(c) requires.
- **Echo replies lose the options.** `icmp_input` builds its reply with
  `ip4_output` (`sys/net/icmp.c:124`), whose header is IHL 5. RFC 1122
  3.2.2.6 says Record Route and Timestamp in an echo request SHOULD be
  updated and returned in the reply.

The bug the audit specifically looked for is absent: the options are
not handed up as transport data. `hlen` comes from IHL on every path
(`sys/net/inet.c:664`, `:736`; `sys/net/af_inet.c:2594-2597`), and the
checksum covers the full option area (`sys/net/inet.c:670`).

**Failure.** `ping -R 10.0.2.15` (IHL 15, type 7, pointer 4) comes back
with the route data all zeroes, and `ping -T tsonly` with no timestamp
from the host. A peer's TCP SYN carrying a completed LSRR is answered
along the normal route rather than the reversed one, so a host
reachable only through that route never sees the SYN-ACK.

**Fix.** Call an `ip_dooptions()`-style walker from `ip4_input` after
the checksum and before delivery:

- Type 0 ends the list and type 1 is one octet.
- For every other type, read the length and reject `len < 2` or
  `off + len > hlen` with ICMP Parameter Problem.
- Validate the Record Route and Timestamp pointers (minimum 4 and 5
  respectively).
- Hand the option block to the transport for echo reflection and
  source-route reversal.

**Tracking.** Overlaps UDP-API-13, which was closed by making
`setsockopt(IP_OPTIONS)` fail `ENOPROTOOPT` (`d85ed5a0c`,
`sys/net/af_unix.c:2815-2823`). That makes the absence detectable at
the socket API. It implements none of the IP-layer processing above.

### OPT-02 (medium). A source-routed datagram whose route is not exhausted is consumed as if it were for us

**RFC 791 3.1**, Loose Source and Record Route
(`docs/rfc/rfc791.txt:1302-1306`, repeated for Strict at `:1364`), and
the host clause at `:1380-1381`:

> If the address in destination address field has been reached and
> the pointer is not greater than the length, the next address in the
> source route replaces the address in the destination address field,
> and the recorded route address replaces the source address just used,
> and pointer is increased by four.

> This option is a strict source route because the gateway or host
> IP must send the datagram directly to the next address in the
> source route

**Code.** Reaching the destination field is treated as proof of final
delivery (`sys/net/inet.c:718`). Options are never parsed (OPT-01), so
a datagram with LSRR (131) or SSRR (137) and `pointer <= length`, which
is only passing through this host, is delivered by `ip4_deliver`
(`:733-765`) to ICMP, UDP, TCP and raw sockets. Substrate does not
forward, and RFC 1122 3.3.5 requires a host that is not a gateway to
discard such a datagram. The code does neither.

In such a datagram, earlier hops have already overwritten `ih->saddr`
with their own address. The host therefore acts on a hop address as
though it were the peer. It uses it in the UDP demux
(`sys/net/af_inet.c:2447-2475`) and in the echo reply
(`sys/net/icmp.c:124`).

**Failure.** An attacker behind a router that honours LSRR sends a UDP
datagram with daddr 10.0.2.15 and LSRR {10.0.2.15, 192.0.2.7}, with the
pointer at the second entry. The datagram is really addressed onward to
192.0.2.7, but `udp_input` hands its payload to the local service on
that port. Its source is the last relaying hop, not the attacker, so
the service's peer-address checks match the wrong host.

**Fix.** In the OPT-01 walker, drop a datagram that carries LSRR or SSRR
with `pointer <= length` before `ip4_deliver`.

### OPT-03 (info). No option can be originated: IHL is fixed at 5 (acknowledged)

Merges the output halves of two verified reports (info, info).

**RFC 791 3.1**, Options (`docs/rfc/rfc791.txt:1068-1070`) and Record
Route (`:1421-1424`):

> What is optional is their transmission in any particular datagram,
> not their implementation.

> The originating host must compose this option with a large
> enough route data area to hold all the address expected.

**Code.** `ih->ihl_version = (4 << 4) | 5;` (`sys/net/inet.c:402`). The
payload is copied at `sizeof(struct iphdr)` (`:415`), and neither
`ip4_output_opts` nor `struct ip4_txopts`
(`sys/include/net/inet.h:96-102`) has room for an option block. Neither
socket path can supply one:

- `setsockopt(IP_OPTIONS)` fails `ENOPROTOOPT`
  (`sys/net/af_unix.c:2820-2823`).
- The raw send paths always synthesize the header
  (`sys/net/af_inet.c:2077-2087`, `:936-941`).

The deviation is acknowledged in both places. It is graded info for that
reason. The silent-success half of the raw case is API-01.

**Fix.** Give `ip4_output_opts` an (options, optlen) pair, set
`IHL = 5 + (optlen + 3) / 4`, zero-pad to a 32-bit boundary
(`docs/rfc/rfc791.txt:1557`), and checksum the full header. Then wire
`IP_OPTIONS`, or `IP_HDRINCL` on root-only raw sockets, to reach it.

---

# Part 4 — The SEND/RECV interface

### API-01 (medium). `IP_HDRINCL` reports success, and the caller's header is then sent as payload

**RFC 791 3.3** (`docs/rfc/rfc791.txt:2129-2134`):

> The internet protocol module, on receiving this call, checks the
> arguments and prepares and sends the message.  If the arguments are
> good and the datagram is accepted by the local network, the call
> returns successfully.  If either the arguments are bad, or the
> datagram is not accepted by the local network, the call returns
> unsuccessfully.

**Code.** `sys_setsockopt` handles `IPPROTO_IP` options 1, 2, 8 and
32–36, and refuses 4 and 37–40 (`sys/net/af_unix.c:2820-2823`). Every
other option falls to `return 0;` at `sys/net/af_unix.c:2881`, and that
includes `IP_HDRINCL`. It is 3 (`include/netinet/in.h:144`), and BSD's
2 is mapped to it at `sys/exec/perso/compat.c:597`. The option is
reported as set and recorded nowhere. Both raw send arms then prepend a
fresh 20-byte header to the caller's buffer: the `sendto` arm
(`sys/net/af_inet.c:2086`) and the `write` arm (`:939`). The comment at
`:2077-2079` concedes this ("not supported yet; we always synthesize
the v4 IP header").

**Failure.** Root opens `socket(AF_INET, SOCK_RAW, IPPROTO_UDP)`, sets
`IP_HDRINCL` (which returns 0) and sends a hand-built header with TTL 5,
DF set and ID 0x1234, followed by UDP. The wire carries IP {proto 17,
TTL 64, DF clear} wrapping the caller's header. The receiver parses
that header as UDP and drops it, while `sendto` returns the full length.
traceroute, hping and packet generators fail silently instead of taking
their fallback.

**Fix.** Return `-ENOPROTOOPT` for `IP_HDRINCL` until it is implemented,
and make the default for unknown `IPPROTO_IP` options `-ENOPROTOOPT`
rather than 0. That second change also covers API-03's
`IP_MTU_DISCOVER`.

### API-02 (medium). TCP discards `ip4_output`'s error; `connect()` to an unroutable host ends in ETIMEDOUT

**RFC 791 3.3** (`docs/rfc/rfc791.txt:2134-2136`):

> On unsuccessful returns, a reasonable report must be made as to the
> cause of the problem, but the details of such reports are up to
> individual implementations.

**Code.** `ip4_output_opts` reports the cause of a failed send:

- `-ENETUNREACH` when there is no route (`sys/net/inet.c:363`)
- `-EMSGSIZE` for an oversize datagram (`:372-373`, `:393`)
- `-ENOMEM` when no buffer can be had (`:399`)
- `-EHOSTUNREACH` when ARP fails (`:453`, `:461`)

`tcp_xmit_raw` returns that code (`sys/net/tcp.c:548`), but its callers
use it only to decide whether to call `tcp_note_sent`:

- `tcp_seg_emit` (`:673-677`)
- `tcp_retx_flush` (`:850-858`)
- the partial-segment path (`:802`)

Nothing stores the code in `so_error`. `tcp_connect_start` moves the PCB
to SYN-SENT without checking for a route (`sys/net/tcp.c:2453-2520`),
although `ip4_path_mtu(raddr) == 0` would say there is none
(`sys/net/inet.c:311-315`). The PCB therefore ends only through the
retransmission budget, as ETIMEDOUT. UDP and raw sends do propagate the
code (`sys/net/af_inet.c:935`, `:941`, `:2087`, `:2114`).

**Failure.** With only 10.0.2.0/24 configured and no gateway,
`connect(192.0.2.1:80)` queues a SYN. Every transmit returns
`-ENETUNREACH` and the error is dropped. After the full SYN backoff,
tens of seconds, `connect()` fails `ETIMEDOUT`. Linux and the BSDs fail
at once with `ENETUNREACH`. An unanswered ARP has the same shape with
`EHOSTUNREACH`.

**Fix.** Fail `tcp_connect_start` with `-ENETUNREACH` when
`ip4_path_mtu(raddr) == 0`. In `tcp_seg_emit` and `tcp_retx_flush`,
latch `-ENETUNREACH` or `-EHOSTUNREACH` as a soft error, and report it
instead of `ETIMEDOUT` when the budget expires.

**Tracking.** Not covered by TCP-API-08, which made `connect()` report
allocation failure and port exhaustion only.

### API-03 (low). DF cannot be set; `IP_MTU_DISCOVER` reports success

**RFC 791 3.3**, the example SEND call (`docs/rfc/rfc791.txt:2081`):

> SEND (src, dst, prot, TOS, TTL, BufPTR, len, Id, DF, opt => result)

**Code.** `struct ip4_txopts` holds ttl, tos, mcast_ttl, mcast_loop and
mcast_if (`sys/include/net/inet.h:96-102`), and has no DF or Id.
`frag_off` is always 0 (`sys/net/inet.c:406`). Linux `IP_MTU_DISCOVER`
(10) reaches the `return 0` at `sys/net/af_unix.c:2881`, so the caller
is told DF was arranged when it was not. Id is legitimately the module's
to choose (3.2), so only DF matters here.

**Failure.** `tracepath`, or a DTLS or QUIC stack, sets
`IP_PMTUDISC_DO`, gets 0, and sends 1400-byte datagrams across a
1280-byte link. A router fragments them instead of returning
Fragmentation Needed, so the application's path-MTU logic never fires.

**Fix.** Add a `df` field to `ip4_txopts`, set `IP_DF` from it at
`:406`, and honour `IP_MTU_DISCOVER` (DO/PROBE set DF, DONT clears it).
Until then, return `-ENOPROTOOPT` for option 10 (see API-01's fix).

### API-04 (low). `lo_xmit` returns success when its ring is full and the datagram is dropped

Merges two verified reports (info, low).

**RFC 791 3.3** (`docs/rfc/rfc791.txt:2132-2134`):

> If either the arguments are bad, or the datagram is not accepted by
> the local network, the call returns unsuccessfully.

**Code.** When `next == lo_ring_tail`, `lo_xmit` skips the copy
(`sys/net/loopback.c:83-88`) and still returns 0 (`:91`). `netdev_xmit`
then counts the frame in `tx_packets` instead of `tx_dropped`, and
`eth_send` and `ip4_output_opts` report success upward. The drop is
commented as deliberate at `:88`; the success return is not. Loopback
carries all of 127/8 and every datagram to the host's own addresses
(`sys/net/inet.c:248`).

**Failure.** A UDP sender bursts more than 127 datagrams to 127.0.0.1
while the lo kthread is busy. The excess vanishes, `sendto()` returns
the full length for each, and `tx_dropped` stays 0.

**Fix.** Return `-ENOBUFS` from `lo_xmit` when the ring is full, and
keep the `sched_wakeup`.

**Tracking.** Adjacent to UDP-IP-11 (`1b99750d0`), which fixed the
same silent-success pattern for oversize frames in this function
(`:80`) but not for a full ring.

---

# Part 5 — ICMP

### ICMP-01 (medium). Echo from any 127/8 source is dropped, so `ping 127.0.0.1` never gets a reply

Merges two verified reports (medium, medium).

**RFC 791**, Glossary, "ICMP" (`docs/rfc/rfc791.txt:2643-2645`). The
echo-server MUST comes from RFC 1122 3.2.2.6, which this file already
cites at `sys/net/icmp.c:95`.

> Internet Control Message Protocol, implemented in the internet
> module, the ICMP is used from gateways to hosts and between hosts to
> report errors and make routing suggestions.

**Code.** `icmp_input` returns for `(s >> 24) == 127`
(`sys/net/icmp.c:110`) without looking at `dev`, although its comment
says "127/8 arriving on a NIC". `ip4_input` already drops 127/8 sources
off lo (`sys/net/inet.c:687`), so this test only ever fires on
legitimate loopback traffic. `icmp_port_unreach` in the same file gates
the identical test on the device correctly (`sys/net/icmp.c:169`).

**Failure.** `ping 127.0.0.1` is routed to lo with source 127.0.0.1
(`route_src4`, `sys/net/inet.c:297-300`), reaches `icmp_input` and is
discarded at `:110`. The raw socket sees only its own request, so ping
reports 100% loss. Pinging the NIC address works only because its source
is not 127/8.

**Fix.** Make the 127/8 test
`(s >> 24) == 127 && !(dev && (dev->flags & NETDEV_IFF_LOOPBACK))`,
as at `:169`, or drop it as redundant with `sys/net/inet.c:687`.

### ICMP-02 (medium). ICMP errors reach UDP only; TCP never learns of Unreachable, Time Exceeded or Parameter Problem

**RFC 791 3.2**, Errors (`docs/rfc/rfc791.txt:2042`). The operative
host rule is RFC 1122 4.2.3.9, under which TCP MUST act on an ICMP
error passed up from IP.

> Internet protocol errors may be reported via the ICMP messages [3].

**Code.** `icmp_error_input` returns unless the quoted datagram is UDP
(`sys/net/icmp.c:55`). Its only consumer, `afinet_icmp_error_v4`, walks
`SOCK_DGRAM` sockets only (`sys/net/af_inet.c:2671-2685`), and
`sys/net/tcp.c` has no ICMP hook at all. The following are all
discarded unread for TCP:

- Destination Unreachable: net, host, protocol, port, and
  administratively prohibited.
- Fragmentation Needed.
- Time Exceeded.
- Parameter Problem.

Separately, Parameter Problem is mapped to `EPROTO` for UDP (`:61`),
where RFC 1122 treats it as a soft error.

**Failure.** A gateway answers `connect(10.9.9.9:80)` with ICMP Host
Unreachable or Admin Prohibited. The message passes its checksum
(`:79`) and is dropped at `:55`. The SYN retransmits to the full
timeout, and the application gets `ETIMEDOUT` instead of
`EHOSTUNREACH`.

**Fix.** For `q->protocol == IPPROTO_TCP`, extract the ports and
sequence number from the 8 quoted octets and pass them to a
`tcp_icmp_error()` hook. Accept the error only if the sequence number is
within SND.UNA..SND.NXT. Abort a SYN-SENT connection on codes 2–4, and
latch a soft error otherwise, for API-02's reporting.

**Tracking.** The TCP counterpart of UDP-ICMP-01 (`bf15202e5`). It is
not tracked in the TCP section.

### ICMP-03 (low). No ICMP Protocol Unreachable; `afinet_deliver_v4`'s result is discarded

Merges three verified reports (medium, low, low).

**RFC 791 3.3** (`docs/rfc/rfc791.txt:2142-2144`):

> If the user addressed does not exist, an ICMP error message is
> returned to the sender, and the data is discarded.

**Code.** `ip4_deliver` handles protocols 1, 17 and 6, and otherwise
`default: break;` (`sys/net/inet.c:759-760`). It then offers the
datagram to raw sockets (`:763-764`) and discards the return value.
That value is `afinet_deliver_v4`'s `delivered` flag
(`sys/net/af_inet.c:2577`, `:2661`), which is exactly the "does the user
addressed exist" answer. A raw socket for the protocol counts as the
addressed user, so the defect is limited to the case with no handler
and no raw listener. `icmp.c` can send Port Unreachable only
(`sys/net/icmp.c:160-184`); code 2 is decoded on receive (`:42`) but
never sent. RFC 1122 3.2.2.1 makes code 2 a host SHOULD, which is why
this is graded low.

**Failure.** A peer sends protocol 47 (GRE) or 132 (SCTP) to 10.0.2.15
with no raw listener. The datagram vanishes, and the peer retries to its
own timeout instead of failing at once.

**Fix.** Capture `afinet_deliver_v4`'s result. In the `default` arm,
send code 2 when all of the following hold, reusing
`icmp_port_unreach`'s source checks and rate limit through an
`icmp_dest_unreach(dev, code, …)` generalisation:

- the result is 0
- `!for_bcast`
- the datagram is not a non-initial fragment

**Since 2d50587eb.** When the finding was raised the kernel had no ICMP
error generator and dropped every ICMP message but echo. UDP-ICMP-02
(`b33ddeb93`) added Port Unreachable and UDP-ICMP-01 (`bf15202e5`) added
error input. Protocol Unreachable remains absent.

### ICMP-04 (low). An echo request whose reply exceeds 1480 octets is dropped silently

**RFC 791 3.1**, Total Length (`docs/rfc/rfc791.txt:958-959`). RFC 1122
3.2.2.6 is the operative rule: an echo reply too large to send MUST be
truncated and sent.

> This field allows the length of a datagram to be up to 65,535 octets.

**Code.** Reassembly delivers echo requests up to 65515 octets whole
(`sys/net/inet.c:514`, `:627-652`). `icmp_input` copies the request into
`uint8_t reply[1500]` and returns when `len > 1500`
(`sys/net/icmp.c:116-117`). For 1481–1500 octets it calls `ip4_output`,
which returns `-EMSGSIZE` against the 1500 MTU (`sys/net/inet.c:372-373`).
`icmp_input` ignores that return (`sys/net/icmp.c:124`).

**Failure.** `ping -s 2000 10.0.2.15` gets no reply, because the
2008-octet message fails `:117`. `ping -s 1480` also gets none: the
1488-octet message passes `:117` but fails `EMSGSIZE`. `ping -s 1460`
works.

**Fix.** Clamp the reply to `ip4_path_mtu(saddr) - 20` and checksum the
truncated message. Move the 1500-byte buffer off the IRQ stack, as
STACK-01 (`sys/net/inet.c:96-115`) did for the output buffers.

**Tracking.** A consequence of UDP-IP-01's decision not to fragment on
output. Reassembly on input (`6a570ce8f`) is what made requests above
one MTU reach `icmp_input` at all.

### ICMP-05 (low). The echo reply's source is chosen by routing, not taken from the request's destination

**RFC 791 3.2**, Addressing (`docs/rfc/rfc791.txt:1622-1623`). RFC 1122
3.2.2.6 is the operative rule: the reply's source is the request's
specific destination.

> The local address, assigned by the local network, must allow for a
> single physical host to act as several distinct internet hosts.

**Code.** `icmp_input` replies with `ip4_output(saddr, …)`
(`sys/net/icmp.c:124`), so the source comes from `route_src4` for the
requester. It does this although `daddr` is in hand. `icmp_port_unreach`
in the same file does it right with `ip4_output_from(ih->daddr, …)`
(`:183`).

**Failure.** The host has A = 10.0.2.15 (the default route) and
B = 192.168.1.5. A ping from 172.16.0.9 to 192.168.1.5 arrives on B,
and the reply leaves A with source 10.0.2.15. The pinger, NAT and
stateful firewalls match replies on source, so they discard it.

**Fix.** For a unicast `daddr`, meaning neither `ip4_is_mcast` nor a
broadcast, call `ip4_output_from(daddr, saddr, IPPROTO_ICMP, …)`.
Otherwise pass 0.

---

# Part 6 — Fragmentation and header

### FRAG-01 (low). A whole datagram does not flush a pending reassembly with the same BUFID

**RFC 791 3.2**, An Example Reassembly Procedure
(`docs/rfc/rfc791.txt:1787-1792`):

> If this is a whole datagram (that is both the fragment offset and the
> more fragments  fields are zero), then any reassembly resources
> associated with this buffer identifier are released and the datagram
> is forwarded to the next step in datagram processing.

**Code.** Only a datagram with MF set or a nonzero offset reaches
`ip4_reasm_input` (`sys/net/inet.c:724-726`). A whole datagram goes
straight to `ip4_deliver` (`:728`) and leaves any entry with the same
(src, dst, id, proto) in `g_reasm`. Entry matching (`:579-581`) ignores
age except through the 30 s sweep (`:573-574`). The UDP-I-01
reassembly checked bounds and ordering but not this step. The procedure
is an example, and RFC 1122 does not restate this step as a MUST, which
is why this is graded low.

**Failure.** Host A sends {id 0x1234, off 0, MF} and loses the tail.
It then sends an unfragmented datagram with id 0x1234, which is
delivered and leaves the entry in place. Within 30 s the ID wraps (see
ID-03) and a new fragmented 0x1234 loses its offset-0 piece. The stale
offset-0 block fills the gap, and the host delivers a datagram built
from two unrelated sends. An off-path attacker who guesses the ID can
set this up on purpose.

**Fix.** When a whole datagram is accepted, look up its BUFID under
`g_reasm_lock` and `ip4_reasm_drop_locked` it before `ip4_deliver`.

### HDR-01 (info). `inet_csum()` hard-codes little-endian order in its return value

**RFC 791 3.1**, Header Checksum (`docs/rfc/rfc791.txt:1037-1039`):

> The checksum field is the 16 bit one's complement of the one's
> complement sum of all 16 bit words in the header.  For purposes of
> computing the checksum, the value of the checksum field is zero.

**Code.** The sum is formed in wire order (`sys/net/inet.c:37`), and the
complement is then unconditionally byte-swapped (`:43`). The same swap
appears in `inet_csum_pseudo4` (`:67`) and `inet_csum_pseudo6` (`:89`).
The result is correct only when it is stored into a network-order field
on a little-endian host. That is how every caller uses it:
`sys/net/inet.c:414` and `:637`, `sys/net/icmp.c:123` and `:181`.
Verification compares against 0, which is swap-invariant (`:670`). On
i386 and x86_64 the emitted checksum was confirmed byte-for-byte against
BSD `in_cksum`. The whole stack uses `__builtin_bswap16` as its `htons`,
so this is a stack-wide convention, not a live defect.

**Failure.** Only on a big-endian port. Every emitted header checksum
would be byte-reversed and dropped at the first hop, while loopback
still validates because its check compares against 0.

**Fix.** Return host order and convert at the store sites, or make the
swap conditional on `__BYTE_ORDER__`.

---

## What conforms

Checked against RFC 791 during this audit and found correctly
implemented at `6d6279d8b`:

- **Header validation before use**, in order: version 4
  (`sys/net/inet.c:663`); IHL ≥ 5 and within the received length
  (`:664-665`, RFC 791 "the minimum value for a correct header is 5",
  `docs/rfc/rfc791.txt:866`); Total Length ≤ the received length and
  ≥ IHL (`:666-667`); and the header checksum over the full IHL, options
  included (`:670`). A failing datagram is discarded at once
  (`docs/rfc/rfc791.txt:365`).
- **Link padding is trimmed**: delivery uses Total Length, not the frame
  length (`:728`, `:737`), so Ethernet's minimum-frame padding never
  reaches a transport.
- **Options are never handed up as data**: the transport payload starts
  at `pkt + IHL*4` on every path (`sys/net/inet.c:736`;
  `sys/net/af_inet.c:2594-2597`). A 20-byte header is never assumed on
  input.
- **The header is built correctly**: version and IHL, TOS from the
  socket (`sys/net/inet.c:403`), Total Length in network order (`:404`),
  TTL from the socket and never 0 (`:409`; `IP_TTL` validated 1..255 at
  `sys/net/af_inet.c:1390-1393`), multicast TTL 1 by default, protocol,
  and addresses. The checksum is computed with the field zeroed
  (`sys/net/inet.c:411-414`) and is numerically correct.
- **No datagram exceeds the egress MTU** (`sys/net/inet.c:372-373`,
  `eth_send` at `:140`). The host does not fragment, which RFC 1122 3.3.3
  permits, and fails the send with `EMSGSIZE` rather than emitting a
  frame the link cannot carry (UDP-IP-01).
- **Reassembly** (`sys/net/inet.c:511-658`):
  - It keys on exactly the four fields RFC 791 names (`:579-581`;
    `docs/rfc/rfc791.txt:723-726`) and counts offsets in 8-octet units
    (`:555`, `docs/rfc/rfc791.txt:1644`).
  - It accepts fragments in any order and lets overlaps overwrite.
  - It rejects a non-final fragment off an 8-octet boundary and anything
    past 65535 (`:560`), and it spoils a datagram whose final length is
    contradicted (`:610-618`).
  - It rebuilds the header from the offset-0 fragment with Total Length
    and flags rewritten and the checksum recomputed (`:631-637`).
  - It bounds memory at 8 × 64 KiB with a 30 s lifetime and
    oldest-first eviction.
  - It meets RFC 791's "All hosts must be prepared to accept datagrams
    of up to 576 octets (whether they arrive whole or in fragments)"
    (`docs/rfc/rfc791.txt:961-963`) up to the 65535 maximum.
- **Martian sources are rejected** (`sys/net/inet.c:680-696`): 0.0.0.0,
  224/4, the limited broadcast, the link's directed broadcast, and 127/8
  off lo.
- **Broadcast and multicast destinations are handled as such**:
  - They are sent as link broadcast or 01:00:5e multicast without ARP
    (`:426-434`).
  - They are accepted only for joined groups (`:715`).
  - They are never delivered to TCP (`:756`) and never answered with
    echo (`sys/net/icmp.c:97-100`) or Port Unreachable (`sys/net/udp.c:93`).
- **ICMP errors are not generated about ICMP-unsafe datagrams**:
  `icmp_port_unreach` refuses non-initial fragments and bad sources and
  is rate-limited (`sys/net/icmp.c:165-173`), and it quotes the header
  plus 8 octets and sources the error from the address the datagram was
  sent to (`:175-183`).
- **Received ICMP is checksummed before any quote is trusted**
  (`sys/net/icmp.c:79`), and a quoted header is length-checked before it
  is read (`:51-54`).
- **The transmit path cannot overrun its buffer** on any `size_t`
  length (`sys/net/inet.c:393`), and IRQ-context transmits use static
  buffers instead of the interrupt stack (`:116-131`).
- **Transmission order**: every multi-octet header field is written and
  read in network (big-endian) order (RFC 791 Appendix B,
  `docs/rfc/rfc791.txt:2481`).
