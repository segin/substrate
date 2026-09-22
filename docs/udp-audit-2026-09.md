# UDP audit — 2026-09

Audit of substrate's UDP implementation against **RFC 768** (J. Postel,
28 August 1980, vendored at `docs/rfc/rfc768.txt`), with the
host-requirement layer it sits on measured against **RFC 1122**,
**RFC 1112**, **RFC 8200** and **RFC 791** where 768 is silent.

Files audited: `sys/net/udp.c` (58 lines), `sys/net/af_inet.c` (the
AF_INET socket layer, demux and datagram ring), `sys/net/inet.c` and
`sys/net/inet6.c` (the IP transmit/receive paths UDP depends on),
`sys/net/icmp.c`, `sys/net/arp.c`, `sys/net/loopback.c`,
`sys/net/netdev.c`, the socket syscall layer in `sys/net/af_unix.c`
(which hosts `sendmsg`/`recvmsg`/`setsockopt` for *all* families), the
`writev` paths in `sys/exec/perso/compat.c` and
`sys/exec/perso/perso_linux.c`, and the multicast-filter initialisation
in `sys/drivers/net/{e1000,r8168,rtl8139}.c`.

**Read the RFC 768 result with the right expectations.** RFC 768 is
three pages. It defines four header fields, one pseudo-header checksum,
a two-sentence sketch of a user interface, and a three-sentence note on
the IP interface. It states no MUST, specifies no error reporting, no
ICMP interaction, no buffering, no port allocation, no fragmentation,
no multicast, no broadcast, and no socket options. Passing RFC 768 is a
very low bar, and substrate does not clear even that — but the great
majority of the defects below are in what RFC 768 declines to specify
and RFC 1122 does. They are reported in a separate part for that reason,
not because they matter less; the two worst defects in this audit are
memory-safety bugs that no RFC addresses at all.

Method: multiple analysis lenses over the send path, the receive path,
the demux, the IP interface, the socket-option surface, the raw-socket
surface and resource handling, each finding re-verified by independent
agents against both the code and the RFC text. 9 candidate findings were
refuted during verification and are excluded. Near-duplicates raised by
different lenses are merged below. **53 unique defects: 2 critical,
14 high, 23 medium, 8 low, 6 info.** Line numbers refer to the tree at
`2d50587eb`.

The three highest-severity findings (MEM-01, MEM-02, API-02) were
re-read against the source before write-up; all three hold as stated.

---

## Findings

| ID | Sev | Requirement | Summary | Site |
|----|-----|-------------|---------|------|
| MEM-01 | critical | none (memory safety) | `MSG_TRUNC` makes `copyout()` read the wire datagram length out of a smaller kernel buffer | `sys/net/af_unix.c:1837` |
| MEM-02 | critical | none (memory safety) | `recvmsg()` scatter path hands raw user pointers to a kernel sockaddr writer | `sys/net/af_unix.c:2337` |
| U-01 | high | RFC 768 User Interface | connected `sendto()` silently discards the destination the caller named | `sys/net/af_inet.c:1281` |
| API-01 | high | BSD/POSIX port ownership | any unprivileged process can steal a bound UDP port | `sys/net/af_inet.c:899` |
| API-02 | high | POSIX `sendmsg(2)` | multi-iovec `sendmsg()` on a UDP socket always fails `EFAULT` | `sys/net/af_unix.c:2260` |
| API-03 | high | POSIX `writev(2)` | `writev()` emits one datagram per iovec, destroying framing | `sys/exec/perso/compat.c:572` |
| API-04 | high | POSIX `SO_RCVTIMEO` | receive timeout accepted and discarded; `recv()` blocks forever | `sys/net/af_inet.c:1531` |
| API-05 | high | POSIX `setsockopt(2)` | option numbering is hardcoded Linux; every BSD-personality option is dropped | `sys/net/af_unix.c:2607` |
| ICMP-01 | high | RFC 1122 4.1.3.3 | ICMP errors never reach a UDP socket; `SO_ERROR` is hardcoded 0 | `sys/net/icmp.c:32`, `sys/net/af_inet.c:1168` |
| IP-01 | high | RFC 791 / RFC 1122 3.3.3 | over-MTU datagrams are emitted as baby giants and reported as sent | `sys/net/inet.c:231` |
| IP-02 | high | RFC 1122 3.2.1.3(g) | only 2 of the 16.7M addresses in 127/8 are deliverable | `sys/net/inet.c:340` |
| IP-03 | high | RFC 1122 3.3.4.2 | a datagram to the host's own NIC address is ARPed for on the wire, then `EHOSTUNREACH` | `sys/net/inet.c:256` |
| IP-04 | high | RFC 1122 3.3.6 | limited broadcast 255.255.255.255 is unicast to the default gateway | `sys/net/inet.c:158` |
| IP-05 | high | RFC 1122 3.3.6 / RFC 826 | directed broadcast is ARPed for — fails, and lets any host hijack our broadcasts for 5 minutes | `sys/net/inet.c:256`, `sys/net/arp.c:194` |
| IP-06 | high | RFC 1112 / RFC 1122 3.3.7 | IPv4 multicast is absent end to end: no route, no L2 mapping, no input, no membership, no NIC filter | `sys/net/inet.c:255/340` |
| RES-01 | high | RFC 1122 4.1.4 | receive queue is bounded by 32 *datagrams*, not bytes, and `SO_RCVBUF` is inert | `sys/net/af_inet.c:1601` |
| U-02 | medium | RFC 768 User Interface | the source address is not specifiable and `bind()`'s address is ignored on transmit | `sys/net/af_inet.c:627` |
| U-03 | medium | RFC 768 Fields/Checksum | the pseudo-header source and the IP header source come from two unsynchronised route lookups | `sys/net/af_inet.c:627` |
| ICMP-02 | medium | RFC 1122 4.1.3.1 | no ICMP Port Unreachable for an unmatched port; the `delivered` flag is computed and thrown away | `sys/net/udp.c:49` |
| IP-07 | medium | RFC 2131 4.1 | an interface with no address cannot transmit at all, so UDP DHCP cannot bootstrap | `sys/net/inet.c:173` |
| IP-08 | medium | RFC 1122 3.3.6 | a broadcast datagram is delivered to exactly one socket | `sys/net/af_inet.c:1675` |
| IP-09 | medium | RFC 4291 / RFC 1122 3.3.7 | `ip6_input` accepts every IPv6 multicast group with no membership check | `sys/net/inet6.c:327` |
| IP-10 | medium | RFC 8200 §4, §8.1 | UDP over IPv6 is undeliverable behind any extension header | `sys/net/inet6.c:332` |
| API-06 | medium | RFC 1122 3.2.1.3 | `bind()` accepts any address; a socket bound to a non-local address is permanently dead, silently | `sys/net/af_inet.c:910` |
| API-07 | medium | BSD/POSIX `connect(2)` | `connect()` on a UDP socket assigns no local port, so it can never receive until it sends | `sys/net/af_inet.c:1253` |
| API-08 | medium | POSIX `shutdown(2)` | `recv()` ignores `SHUT_RD` and blocks forever while `read()` returns EOF | `sys/net/af_inet.c:1426` |
| API-09 | medium | POSIX `recv(2)` | a zero-length receive does not consume the datagram, so a `poll()` drain loop spins | `sys/net/af_unix.c:1821` |
| API-10 | medium | POSIX `recvfrom(2)` | EOF leaves `*addrlen` at the 128-byte bounce capacity | `sys/net/af_inet.c:1535` |
| API-11 | medium | RFC 1122 4.1.3.5 | the specific-destination address is discarded; `IP_PKTINFO` is accepted and does nothing | `sys/net/af_inet.c:1676` |
| API-12 | medium | RFC 1122 4.1.4 / 3.2.1.7 | `IP_TTL`/`IP_TOS` ignored, TTL hardcoded 64, `getsockopt(IP_TTL)` answers 0 | `sys/net/inet.c:245` |
| API-13 | medium | RFC 1122 4.1.3.2 | IP options can be neither sent nor received | `sys/net/inet.c:344` |
| API-14 | medium | POSIX `getsockopt(2)` | `SO_RCVBUF` reports an invented 32768; `getsockopt` never checks the fd is a socket | `sys/net/af_unix.c:2708` |
| API-15 | medium | BSD `SO_BROADCAST` | neither storable nor enforced; `getsockopt` hardcodes 0 | `sys/net/af_unix.c:2616` |
| API-16 | medium | POSIX `write(2)` | `write()` on a torn-down socket returns a successful 0-byte transfer, spinning libc write loops | `sys/net/af_inet.c:666` |
| API-17 | medium | BSD/Linux raw demux | a bound or connected `SOCK_RAW` socket is not filtered by address | `sys/net/af_inet.c:1576` |
| API-18 | medium | RFC 768 Fields/Source Port | ephemeral port allocation is a non-atomic counter plus a check-then-record outside the lock | `sys/net/af_inet.c:188` |
| RES-02 | medium | none (resources) | every AF_INET socket eagerly allocates a ~50 KiB 13-page contiguous ring, TCP included | `sys/net/af_inet.c:839` |
| RES-03 | medium | RFC 1213 MIB-II | full-ring drops are entirely unaccounted: no counter, no `SO_ERROR`, no `/proc` | `sys/net/af_inet.c:1601` |
| RES-04 | medium | none (resources) | `enqueue()` runs a 1572-byte memcpy and a full thread-registry walk with interrupts disabled | `sys/net/af_inet.c:1614` |
| U-04 | low | RFC 768 Fields | destination port 0 is accepted from userland and emitted on the wire | `sys/net/af_inet.c:1288` |
| U-05 | low | RFC 768 Fields/Length | `sendmsg()` cannot emit a legal zero-payload datagram | `sys/net/af_unix.c:2248` |
| U-06 | low | RFC 8200 8.1 | `udp_csum6()` leaves `check == 0` on the wire when source selection fails | `sys/net/af_inet.c:637` |
| IP-11 | low | RFC 1122 3.3.3 | `lo` advertises MTU 16384; `lo_xmit` truncates at 1700 and reports success | `sys/net/loopback.c:75` |
| API-19 | low | POSIX `shutdown(2)` | `shutdown(SHUT_WR)` on a UDP socket is a silent no-op | `sys/net/af_inet.c:985` |
| API-20 | low | internal consistency | `sendto()` and `write()` disagree on the maximum raw datagram (1572 vs 1580) | `sys/net/af_inet.c:1393` |
| RES-05 | low | none (info leak) | `FIONREAD` reads the ring unlocked and can copy out an uninitialized slot's length | `sys/net/af_inet.c:242` |
| RES-06 | low | RFC 8200 8.1 / RFC 1213 | checksum-failure and zero-checksum drops leave no counter and no log line | `sys/net/udp.c:48` |
| I-01 | info | RFC 768 Fields/Length | no IP reassembly: the usable Length range is 8..1480, not 8..65535 | `sys/net/inet.c:308` |
| I-02 | info | — | `socket(AF_INET6, …)` is refused, so the entire v6 UDP demux is dead code | `sys/net/af_inet.c:808` |
| I-03 | info | — | the IPv6 send cap is computed from the IPv4 header length | `sys/net/af_inet.c:1350` |
| I-04 | info | RFC 1122 3.3.7 | `setsockopt(IP_ADD_MEMBERSHIP)` returns success while recording nothing | `sys/net/af_unix.c:2616` |
| I-05 | info | RFC 768 Fields/Source Port | a raw socket may forge any UDP source port (root only; matches BSD) | `sys/net/af_inet.c:1305` |
| I-06 | info | RFC 1122 4.1.3.4 | raw delivery is not gated on UDP checksum validity (matches BSD) | `sys/net/inet.c:370` |

---

# Part 0 — memory safety

No RFC is involved. These are the two worst defects found, and both are
driven by lengths an off-box peer chooses.

### MEM-01 (critical). `MSG_TRUNC` makes `copyout()` read past the end of a kernel allocation and into userspace

**Requirement.** None from RFC 768; this is a kernel/user boundary
defect on the receive operation RFC 768 mandates
(`docs/rfc/rfc768.txt`, "User Interface": *"receive operations on the
receive ports that return the data octets and an indication of source
port and source address"*).

**Code.** `afinet_recvfrom()` honours `MSG_TRUNC` by returning the
datagram's *arrival* length, not the number of bytes it wrote:

- bytes actually copied — `size_t n = p->len < len ? p->len : len;` (`sys/net/af_inet.c:1480`)
- length reported — `uint16_t ptrue = p->truelen;` (`sys/net/af_inet.c:1484`), returned at `sys/net/af_inet.c:1524`
- `p->truelen` is set from the delivered datagram length in `enqueue()` (`sys/net/af_inet.c:1611`), which for UDP is `ulen - sizeof(struct udphdr)` — i.e. chosen by the remote sender via the UDP Length field.

The sole in-kernel consumer does not clamp it. `do_recv()`:

```
1825      cap = len < RECV_BOUNCE_CAP ? len : RECV_BOUNCE_CAP;
1826      kbuf = kmalloc(cap);
1830      n = recv_into_kbuf(fd, kbuf, cap, flags, …);   /* -> afinet_recvfrom */
1837      if (n > 0 && copyout(kbuf, buf, (size_t)n) != 0) {
```

(`sys/net/af_unix.c:1825-1837`.) There is no `if (n > cap) n = cap;`
anywhere between 1830 and 1837. `copyout()` validates only the
*destination* range (`sys/kern/subr_copy.c:63-66`) and then copies
`size` bytes, so the kernel-side source is read unbounded. Both entry
points reach it: `sys_recv` and `sys_recvfrom` both call `do_recv`
(`sys/net/af_unix.c:1858`, `:2067`), as does the single-iovec arm of
`sys_recvmsg` (`sys/net/af_unix.c:2373`).

**Failure.** A process binds UDP/5353 and calls
`recv(fd, buf, 16, MSG_TRUNC)`. A remote host sends one 1500-byte
datagram. `enqueue` stores `p->len = 1472`, `p->truelen = 1472`.
`do_recv` computes `cap = 16`, `kmalloc(16)` — a 16-byte UMA slab item
(`sys/vm/vm_kmem.c:109-131`) — `afinet_recvfrom` writes 16 bytes and
returns 1472, and `copyout(kbuf, buf, 1472)` reads 1456 bytes of
neighbouring live kernel objects out of the slab and writes them into
userspace, 1456 bytes past the end of the caller's 16-byte buffer. The
attacker sizes the disclosure window with the datagram length (up to
`AFI_DATA_MAX` = 1572); the victim's `len` argument selects which
allocation the over-read starts from. Nothing faults and nothing is
logged. The same unclamped line also sits under the AF_UNIX
`MSG_TRUNC` return at `sys/net/af_unix.c:1802`.

**Fix.** Clamp in `do_recv()` before the copyout, keeping the byte
count produced separate from the `MSG_TRUNC` report:
`size_t ncopy = (size_t)n < cap ? (size_t)n : cap;` copy `ncopy`, still
`return n;`. The clamp must live in `do_recv()` — fixing only
`afinet_recvfrom` would break `MSG_TRUNC` semantics without closing
the hole, and would leave the AF_UNIX path open.

### MEM-02 (critical). The `recvmsg()` scatter path hands raw user pointers to a kernel sockaddr writer

**Requirement.** Same: none from RFC 768. The invariant is asserted by
the code itself at `sys/net/af_inet.c:1454` — *"`addr`/`addrlen` here
are the kernel bounce buffers supplied by `recv_into_kbuf()`, so the
copy is safe"*.

**Code.** `afinet_recvfrom()` writes the source address straight
through its `addr`/`addrlen` arguments with no `validate_user_addr()`,
no `copyout()` and no fault handler armed: it dereferences `*addrlen`
at `sys/net/af_inet.c:1502` and `memset`s and fills a `struct sin_kern`
through `addr` at `sys/net/af_inet.c:1503-1508`. `do_recv()` upholds
the invariant — kernel-stack `uint8_t kaddr[128]`
(`sys/net/af_unix.c:1811`), copied out afterwards under a
copyin-validated capacity (`sys/net/af_unix.c:1843-1855`).

The SOCK-04 multi-iovec datagram branch of `sys_recvmsg()` does not:

```
2337            r = recv_into_kbuf(fd, scat, cap, flags,
2338                               (struct sockaddr *)msg->msg_name,
2339                               (socklen_t *)&umsg->msg_namelen);
```

Both are raw userspace pointers lifted verbatim from the caller's
`msghdr`, and `recv_into_kbuf` forwards them unchanged to
`afinet_recvfrom` (`sys/net/af_unix.c:1743`). The single-iovec path at
`sys/net/af_unix.c:2373` is unaffected because it routes through
`do_recv()`.

**Failure.** An unprivileged process binds an AF_INET `SOCK_DGRAM`
socket and calls `recvmsg(fd, &msg, 0)` with `msg_iovlen == 2`,
`msg_name = (void *)0xC0300000` (kernel `.data`, above `KERN_BASE`) and
`msg_namelen = 16`. On the next inbound datagram `afinet_recvfrom`
memsets and writes 16 bytes of `sockaddr_in` — including the source
port and source IPv4 address taken from the attacker's own datagram —
directly into kernel memory at `0xC0300000`. An unmapped `msg_name`
instead takes an unrecoverable kernel page fault; an unmapped
`&umsg->msg_namelen` faults on the `*addrlen` read at
`sys/net/af_inet.c:1502`.

**Fix.** Stage the source address in a kernel sockaddr buffer in the
scatter branch exactly as `do_recv()` does, and copy it out with the
same validated sequence, rather than passing `msg_name` and
`&umsg->msg_namelen` into `recv_into_kbuf()`.

---

# Part 1 — RFC 768 conformance

Everything RFC 768 actually requires, and where substrate fails it.
Six findings. The header construction itself is correct and the
checksums are arithmetically right (see "What conforms"); what 768
asks for and does not get is a usable *interface* to the send and
receive operations.

### U-01 (high). A connected UDP socket silently ignores the destination given to `sendto()`

**RFC 768, "User Interface"** (`docs/rfc/rfc768.txt`):

> and an operation that allows a datagram to be sent, specifying the
> data, source and destination ports and addresses to be sent.

**Code.** `afinet_sendto_k()` resolves the destination as
`uint16_t dport = s->peer_port;` (`sys/net/af_inet.c:1279`) and then

```
1281    if (s->connected) {
1282        memcpy(daddr_buf, s->peer_addr, 16);
1283    } else if (addr) {
…
1288            dport = __builtin_bswap16(sin->sin_port);
1289            memcpy(daddr_buf, &sin->sin_addr, 4);
```

The caller's sockaddr is parsed **only** on the `else if` arm. Once
`connect()` has been called on a `SOCK_DGRAM` socket, the explicitly
named destination address and port are discarded with no error. The
datagram is then built with `uh->dest = __builtin_bswap16(dport)`
(`sys/net/af_inet.c:1326`) from the stale peer port and handed to
`ip4_output()` (`:1332`) with the stale peer address.
`sys_sendto_impl` does copy the caller's AF_INET sockaddr in and route
it down to this function (`sys/net/af_unix.c:1939-1957`), so the
address really does arrive here and really is thrown away.

**Failure.** A resolver does `connect(fd, {10.0.0.1:53})` to get errno
reporting, then `sendto(fd, query, n, 0, {10.0.0.2:53}, 16)` to query a
second server. The kernel returns success having sent the query to
10.0.0.1:53 — the second server is never queried and the first gets a
duplicate. The same applies to every retarget after `connect()`, and to
a connected socket sending to a different port on the same host.

**Fix.** When `addr` is non-NULL on a `SOCK_DGRAM`/`SOCK_RAW` socket,
parse and use it even if `s->connected` (POSIX, and what Linux's
`udp_sendmsg` does); fall back to the peer only when `addr` is NULL.
If BSD-style strictness is preferred, return `-EISCONN` — anything but
misdirecting the datagram.

### U-02 (medium). The source address is not specifiable, and `bind()`'s address is ignored on transmit

**RFC 768, "User Interface"**: the send operation specifies *"the data,
source and destination ports and addresses to be sent."* (RFC 1122
4.1.3.5 makes it a MUST: *"An application program MUST be able to
specify the IP source address to be used for sending a UDP datagram
or to leave it unspecified"*; RFC 1122 is not vendored here.)

**Code.** `bind()` records the address at `sys/net/af_inet.c:910`
(v4) and `:925` (v6), and it is read in exactly two places in the
file: the receive demux (`sys/net/af_inet.c:1586-1587`) and
`getsockname()` (`sys/net/af_inet.c:1182`). No transmit path reads it.
`udp_csum4()` derives the pseudo-header source from
`ip4_source_for(daddr)` — pure routing — at `sys/net/af_inet.c:627`,
and `ip4_output()` has no source parameter at all and writes
`ih->saddr = dev->ip4_addr` at `sys/net/inet.c:248`. The v6 twin is the
same via `ip6_source_for()` (`sys/net/af_inet.c:637`,
`sys/net/inet6.c:228-233`). Both UDP send paths are affected —
`afinet_node_write_body` at `sys/net/af_inet.c:698-699` and
`afinet_sendto_k` at `:1331-1332`. TCP works around the missing
parameter half-way (`sys/net/tcp.c:302`); UDP does not.

**Failure.** eth0 = 192.0.2.10/24, eth1 = 198.51.100.10/24. An
application binds 198.51.100.10:5000 and sends to a peer that routes
via eth0. The datagram leaves with source 192.0.2.10, so the peer
replies to 192.0.2.10:5000; `sock_score()` then requires
`daddr == s->local_addr == 198.51.100.10`
(`sys/net/af_inet.c:1586-1587`), no socket scores ≥ 0, and the reply is
dropped. The socket can transmit and can never receive an answer. A
stateful firewall on the path drops the outbound datagram for the same
reason.

**Fix.** Give `ip4_output()`/`ip6_output()` an explicit source-address
argument, pass `s->local_addr` down from `sys/net/af_inet.c:698` and
`:1331` when it is non-wildcard, and use the same value for the
pseudo-header in `udp_csum4()` so the two cannot diverge. Fall back to
`ip4_source_for()` for a wildcard bind.

### U-03 (medium). The checksum's pseudo-header and the IP header are built from two independent, unsynchronised route lookups

**RFC 768, "Fields"**:

> The pseudo header conceptually prefixed to the UDP header contains the
> source address, the destination address, the protocol, and the UDP
> length. This information gives protection against misrouted datagrams.

**Code.** `udp_csum4()` obtains the source through
`ip4_source_for()` (`sys/net/af_inet.c:627`), which runs
`route_for_v4()` at `sys/net/inet.c:207`. `ip4_output()` then runs
`route_for_v4()` again, independently, at `sys/net/inet.c:214`, and
stamps *that* result into `ih->saddr` at `sys/net/inet.c:248`. Nothing
pins the netdev list or the interface addresses across the two calls,
and the UDP send paths run in preemptible process context. The IPv6
twin has the same shape (`sys/net/af_inet.c:637` vs
`sys/net/inet6.c:239`).

**Failure.** A process sends to a destination reachable via the
default gateway. `ip4_source_for` picks eth0 (10.0.0.5) and the
checksum is computed over a pseudo-header containing 10.0.0.5. The
thread is preempted; another thread `ifconfig`s eth0 down or brings up
eth1 ahead of it. On resume, `ip4_output`'s own lookup selects eth1 and
writes `ih->saddr = 192.168.1.5`. The datagram carries a checksum
computed over a source address it does not carry, and a conformant
receiver discards it silently (RFC 1122 4.1.3.4), so the sender never
learns. This is the same class of defect the in-tree comment at
`sys/net/tcp.c:295-301` documents for TCP.

**Fix.** Resolve the route once per transmit: have the UDP send path
obtain the netdev, use its `ip4_addr` for the pseudo-header, and pass
the same netdev/source into `ip4_output()`. That also removes a
redundant route lookup from every transmit.

### U-04 (low). Destination port 0 is accepted from userland and emitted on the wire

**RFC 768, "Fields"**:

> Source Port is an optional field, when meaningful, it indicates the
> port of the sending process, and may be assumed to be the port to
> which a reply should be addressed in the absence of any other
> information. If not used, a value of zero is inserted.

768 reserves 0 as the "no port" sentinel and states no rule for a zero
*destination* port; the rejection rule is universal implementation
practice (Linux `udp_sendmsg`: `if (dport == 0) return -EINVAL`), not a
vendored requirement.

**Code.** No send path validates the destination port.
`afinet_sendto_k()` takes `dport = __builtin_bswap16(sin->sin_port)`
(`sys/net/af_inet.c:1288` v4, `:1294` v6) with no zero check and writes
it into the header at `:1326`/`:1353`. `afinet_connect()` likewise
accepts a zero peer port (`sys/net/af_inet.c:1219`, `:1250`), so a
later `write()` emits `uh->dest = 0` (`sys/net/af_inet.c:693`, `:725`).
The receive side closes the loop: `afinet_deliver_v4()` parses
`sport = __builtin_bswap16(uh->source)` (`sys/net/af_inet.c:1643`)
without treating 0 specially, `enqueue()` stores it, and
`afinet_recvfrom()` hands it back as `sin->sin_port` at
`sys/net/af_inet.c:1506` — a sockaddr with port 0, which RFC 768
defines as explicitly *not* a reply address.

**Failure.** A peer sends a legal datagram with Source Port 0.
`recvfrom()` returns it with `sin_port == 0`. The application does the
ordinary thing — `sendto(fd, resp, n, 0, &from, fromlen)` — and the
kernel builds and transmits a datagram with Destination Port 0 rather
than returning `EINVAL`. It is undeliverable everywhere, no error is
reported, and in a request/response protocol the application blocks
until its own timeout.

**Fix.** Reject a zero destination port with `-EINVAL` in
`afinet_sendto_k()` (both family arms, after the `sin_family` check)
and in `afinet_connect()` for `SOCK_DGRAM`.

### U-05 (low). `sendmsg()` cannot emit a legal zero-payload datagram

**RFC 768, "Fields"**:

> Length is the length in octets of this user datagram including this
> header and the data. (This means the minimum value of the length is
> eight.)

A datagram carrying no data is well-formed; its Length is exactly the
minimum of eight.

**Code.** `sendto()`/`send()` handle it —
`if (len == 0) return afinet_sendto_k(fd, ubuf, 0, …)` at
`sys/net/af_inet.c:1387-1388`, and `afinet_sendto_k`'s guard at
`sys/net/af_inet.c:1268` documents *"len==0 is a valid empty
datagram"*. `sendmsg()` does not. The datagram-gather branch
short-circuits on an empty total with `if (need == 0) return 0;`
(`sys/net/af_unix.c:2248`) before any transport call, and
`msg_iovlen == 0` passes the bounds check at
`sys/net/af_unix.c:2325-2326`, fails the `msg_iovlen > 1` gather test
at `:2245`, runs the per-iovec loop at `:2268` zero times, and falls
out to `return total;` with `total == 0`. Both report success having
put nothing on the wire.

**Failure.** A NAT keepalive written as
`msghdr{ .msg_name=&peer, .msg_namelen=16, .msg_iov=NULL, .msg_iovlen=0 }`
returns 0 — success — and no datagram is transmitted. The NAT binding
expires while the application believes it is sending keepalives. The
identical call via `sendto(fd, NULL, 0, 0, &peer, 16)` does transmit,
so the failure is invisible in testing that uses the other entry point.

**Fix.** Treat a datagram socket with a zero total payload as a
zero-length datagram: drop the `need == 0` early return, and route
`msg_iovlen == 0` on a datagram socket to
`sys_sendto_impl(fd, …, 0, …)`.

### I-01 (info). No IP reassembly: the usable Length range is 8..1480, not 8..65535

**RFC 768, "Fields"**: the Length field is 16 bits with a minimum of
eight, so 8..65535 is on-spec and a peer may legitimately send any of
it. (The reassembly requirement itself is RFC 1122 3.3.2.)

**Code.** `ip4_input` rejects any packet with a non-zero fragment
offset or MF bit —
`if ((__builtin_bswap16(ih->frag_off) & 0x3FFF) != 0) return;`
(`sys/net/inet.c:308`) — before `udp_input` is ever called, including
the first fragment, so there is not even a partial delivery or a
reassembly-timeout ICMP. Combined with the send-side ceiling at
`sys/net/af_inet.c:99`, the usable range is 8..1480.

The deviation *is* acknowledged in code: `sys/net/inet.c:307` says
"Drop fragments — we don't reassemble yet", and the `AFI_DATA_MAX`
comment at `sys/net/af_inet.c:81-98` states outright that "reaching it
requires IP fragmentation on send and reassembly on receive, and this
stack has neither". It is recorded here because the practical scope is
not stated there: EDNS0 responses over ~1472 bytes, NFS/RPC over UDP,
and TFTP with a large blksize are all silently unanswerable, and the
drop increments no counter (`sys/net/inet.c:308`), so nothing on the
box shows why.

**Fix.** Implement reassembly with a bounded queue and a timeout, and
raise the socket ceiling once it exists. As an interim measure bump
`dev->rx_dropped` at the fragment drop so the loss is observable.

---

# Part 2 — beyond RFC 768

RFC 768 is silent on every requirement in this part. The citations are
RFC 1122 (host requirements), RFC 1112 (IP multicast), RFC 8200 (IPv6),
RFC 791 (fragmentation, vendored at `docs/rfc/rfc791.txt`), and POSIX.
Where a requirement is quoted from RFC 1122 it is noted as such — RFC
1122 is not vendored in this repo.

## 2a. ICMP

### ICMP-01 (high). ICMP errors never reach a UDP socket, and `SO_ERROR` is hardcoded 0

**RFC 1122 4.1.3.3**: *"UDP MUST pass to the application layer all ICMP
error messages that it receives from the IP layer."* RFC 768 never
mentions ICMP.

**Code.** `icmp_input()` begins
`if (ih->type != ICMP_ECHO) return;` (`sys/net/icmp.c:32`). Destination
Unreachable (including Port Unreachable and Fragmentation Needed),
Time Exceeded, Source Quench and Parameter Problem are all discarded
before the embedded IP+UDP header is parsed, so no socket is ever
located. There is nowhere to record one if it were: `afi_sock_t`
(`sys/net/af_inet.c:115-148`) has no pending-error field, and
`afinet_so_error()` returns `tcp_take_so_error()` only when `s->tcp` is
set and a hard `return 0;` for every datagram socket
(`sys/net/af_inet.c:1164-1168`). `ICMP_DEST_UNREACH` is defined at
`sys/include/netinet/icmp.h:11` and referenced nowhere else in `sys/`.
Root-owned `SOCK_RAW`/ICMP sockets do get a copy, via the
unconditional `afinet_deliver_v4` at `sys/net/inet.c:370` — that is why
`ping(8)` works — but that is not a UDP socket's error channel, and it
has been root-only since UDP-07 (`sys/net/af_inet.c:820`).

**Failure.** A resolver `connect()`s a UDP socket to a nameserver with
no listener and sends a query. The peer answers ICMP type 3 code 3.
`icmp_input` returns at `sys/net/icmp.c:32`. The socket never becomes
readable, `recv()` blocks for the full query timeout, and
`getsockopt(SO_ERROR)` returns 0 from `sys/net/af_inet.c:1168` — so the
resolver burns its entire retry budget against a dead server instead of
failing over immediately. The same silent loss hides ICMP Fragmentation
Needed, making a UDP path-MTU black hole undiagnosable.

**Fix.** Handle types 3/4/11/12 in `icmp_input()`: parse the quoted IP
header plus the first 8 bytes of the offending transport header (for
UDP that is the whole `udphdr`, giving the 4-tuple), map it to the
originating socket, latch an errno (`ECONNREFUSED` for code 3,
`EHOSTUNREACH`/`ENETUNREACH` for codes 0-1, `EMSGSIZE` for code 4) in a
new `afi_sock_t::so_error` that `afinet_so_error()` returns and clears,
and wake any blocked reader.

### ICMP-02 (medium). No ICMP Port Unreachable for an unmatched port — the `delivered` result is computed and thrown away

**RFC 1122 4.1.3.1**: *"If a datagram arrives addressed to a UDP port
for which there is no pending LISTEN call, UDP SHOULD send an ICMP Port
Unreachable message."* RFC 768 specifies no action for an unmatched
port.

**Code.** `udp_input()` calls
`afinet_deliver_v4(s, d, IPPROTO_UDP_NUM, pkt, ulen, 1);`
(`sys/net/udp.c:49`) and `afinet_deliver_v6(…)` (`sys/net/udp.c:55`) as
void expressions. Both functions return an `int delivered` flag set
only when a socket matched (`sys/net/af_inet.c:1621` declares it,
`:1679` sets it, `:1682` returns it) — the information needed is
already computed and discarded at the call site. Nothing in the tree
transmits an ICMP Destination Unreachable: `icmp.c`'s only transmit
paths are the echo reply and the ICMPv6 ND/echo paths, and
`ICMP_DEST_UNREACH` has no producer.

**Failure.** A peer sends to UDP/9999 with nothing bound.
`sock_score()` rejects every socket at `sys/net/af_inet.c:1583`, `best`
stays NULL, `afinet_deliver_v4` returns 0, and `udp_input` falls off
the end. The peer's connected UDP socket never surfaces
`ECONNREFUSED` and blocks until its own timeout; a UDP traceroute aimed
at substrate never terminates, because the final-hop port-unreachable
that marks the destination reached is never sent.

**Fix.** Capture the return at `sys/net/udp.c:49` and `:55` and, when
it is 0, emit type 3 code 3 (v4) or ICMPv6 type 1 code 4 quoting the IP
header plus the first 8 octets. Suppress it for broadcast and multicast
destinations and rate-limit it (RFC 1122 3.2.2) — `ip4_input` already
computes `for_bcast` at `sys/net/inet.c:338` but does not pass it down
to `udp_input`, so that flag needs plumbing through.

## 2b. IP transmit and receive

### IP-01 (high). Outbound datagram size is bounded by a compile-time constant, not the egress MTU, and the stack never fragments

**RFC 791**, "An Example Fragmentation Procedure"
(`docs/rfc/rfc791.txt:1719-1724`):

> If the total length is less than or equal the maximum transmission
> unit then submit this datagram to the next step in datagram
> processing; otherwise cut the datagram into two fragments, the first
> fragment being the maximum size, and the second fragment being the
> rest of the datagram.

**Code.** Nothing in the transmit path consults the outgoing device's
MTU. `AFI_DATA_MAX` is `NETDEV_MTU_MAX - 20 - 8` = 1572
(`sys/net/af_inet.c:99`, `NETDEV_MTU_MAX` = 1600 at
`sys/include/sys/netdev.h:24`); `sys/net/af_inet.c:1327` writes UDP
Length up to 1580; `sys/net/inet.c:231` bounds `ip4_output`'s payload
by `NETDEV_MTU_MAX - sizeof(struct iphdr)` and `:242` writes IP
`tot_len` up to 1600; `eth_send` bounds by `NETDEV_MTU_MAX`
(`sys/net/inet.c:133`); `netdev_xmit` by `NETDEV_MTU_MAX + 14` = 1614
(`sys/net/netdev.c:70`). Every one of those is the compile-time worst
case, while every real NIC sets `dev->mtu = 1500`
(`sys/drivers/net/rtl8139.c:335`, `e1000.c:407`,
`sys/drivers/virtio/virtio_net.c:386`, `r8168.c:593`) and
`sys/net/netdev.c:34` defaults it to 1500. `dev->mtu` is read only by
`SIOCGIFMTU`/`SIOCSIFMTU` (`sys/net/af_inet.c:362-367`), so
`ifconfig eth0 mtu 576` is stored and then ignored by the whole send
path. The comment at `sys/net/af_inet.c:93-96` asserting the cap "is
now exactly MTU minus the IPv4 and UDP headers" is wrong. The IPv6 side
has the same defect at `sys/net/inet6.c:247`.

**Failure.** `sendto(fd, buf, 1572, …)` on an rtl8139 whose `dev->mtu`
is 1500: accepted at `sys/net/af_inet.c:1323`, UDP Length 1580 at
`:1327`, IP `tot_len` 1600 at `sys/net/inet.c:242`, and a 1614-byte
frame handed to `rtl8139.c:174` (whose own limit is 1792) — 100 bytes
past the 1514-byte Ethernet maximum. `sendto()` returns 1572, complete
success, while a conformant switch or peer NIC discards the frame:
silent, unreported loss. It is also NIC-dependent: at a 1480-byte
payload (1522-byte frame) `e1000.c:220` returns `-EMSGSIZE` (limit
1518) but `virtio_net.c:227` transmits it oversized (limit 1526), so an
identical socket call succeeds, silently corrupts, or fails by NIC
model. Payloads 1473..1476 go out oversized on every driver. Because
`ip4_input` drops every fragment (`sys/net/inet.c:308`) and
`ip4_output` never produces one, there is no correct path for such a
datagram at all.

**Fix.** Take the MTU from the netdev `route_for_v4`/`route_for_v6`
already returned and either fragment, or return `-EMSGSIZE` when
`sizeof(iphdr) + payload_len > dev->mtu`. Bound `eth_send` by
`dev->mtu` too. Until fragmentation exists, derive the socket layer's
per-send gate from the route's MTU rather than from `AFI_DATA_MAX`, so
`sendto()` fails loudly instead of emitting an uncarriable frame.

### IP-02 (high). Only 127.0.0.1 and 127.255.255.255 of 127/8 are deliverable

**RFC 1122 3.2.1.3(g)** designates the whole `{ 127, <any> }` block as
the internal host loopback address. RFC 768 only makes the destination
address part of the demux context.

**Code.** `loopback_init` gives `lo` exactly one address —
`ip4_addr = 0x0100007F`, `ip4_netmask = 0x000000FF`
(`sys/net/loopback.c:128-129`). `ip4_input`'s acceptance test is
`ih->daddr != dev->ip4_addr && !for_bcast` (`sys/net/inet.c:340`),
where `for_bcast` covers 255.255.255.255 and the interface broadcast
`(addr & mask) | ~mask` = 127.255.255.255 (`sys/net/inet.c:337-339`).
So of 16,777,216 addresses in 127/8, exactly two are accepted — while
`route_for_v4` sends *all* of 127/8 to `lo` (`sys/net/inet.c:160-166`),
`ip4_output` skips ARP for a loopback device (`sys/net/inet.c:255`) and
`lo_xmit` queues the frame and returns 0 (`sys/net/loopback.c:73-82`).
Note the asymmetry with the martian-source filter, which accepts *any*
127/8 source on `lo` (`sys/net/inet.c:325-327`).

**Failure.** A DNS stub resolver binds UDP 127.0.0.53:53 (the bind
succeeds — see API-06). A client `sendto`s 127.0.0.53:53 and gets the
full byte count back. `route_for_v4` picks `lo`, `ip4_output` emits the
datagram, `lo_thread` loops it back through `netdev_rx` →
`inet_eth_input` → `ip4_input`, and `sys/net/inet.c:340` discards it
because `0x3500007F != 0x0100007F` and it is not the broadcast.
`udp_input` never runs; the resolver never wakes; the client blocks in
`recvfrom` until its timeout with no error anywhere. Real services do
use these addresses (127.0.0.53 stub resolver, 127.0.1.1, per-service
127.0.0.x binds in test harnesses).

**Fix.** In `ip4_input`, accept when the receiving device is
`NETDEV_IFF_LOOPBACK` and `(ntohl(ih->daddr) >> 24) == 127`, alongside
the exact-address and broadcast tests. The martian-source filter
already assumes exactly that model.

### IP-03 (high). A datagram to the host's own NIC address is ARPed for on the wire and fails `EHOSTUNREACH`

**RFC 1122 3.3.4.2 / 3.2.1.3**: a datagram whose destination is one of
the host's own addresses is destined for the host. Every BSD/Linux
stack routes a self-addressed datagram to loopback. RFC 768 says
nothing about local delivery.

**Code.** There is no local-delivery short-circuit anywhere in the IPv4
output path, and no "is this address one of ours" helper in the tree —
`sys/net/inet.c` has only `route_for_v4`/`ip4_output`/`ip4_input`, and
the only `for_us` test is on the *input* side of IPv6
(`sys/net/inet6.c:325-328`). For `daddr` equal to the host's own NIC
address, `route_for_v4`'s subnet arm necessarily matches — `(addr &
mask) == (addr & mask)` is a tautology (`sys/net/inet.c:174-177`) — and
returns that NIC with `via_gw = 0`. `ip4_output` then takes the
non-loopback branch (`sys/net/inet.c:255`), computes
`nexthop = daddr` (`:256`), misses the ARP cache, broadcasts an ARP
request for our own IP (`:258`), and nothing ever answers it:
`arp_input` only inserts a binding for a peer's sender address
(`sys/net/arp.c:194-195`). In process context it then spins
`for (int i = 0; i < 32; i++) sched_yield();` (`sys/net/inet.c:275-278`)
and returns `-EHOSTUNREACH` (`:281`); in hard-IRQ context it returns
`-EHOSTUNREACH` immediately (`:271-273`). The IPv6 path has the same
hole via `route_for_v6`'s subnet arm (`sys/net/inet6.c:178-186`).

**Failure.** A tftpd binds UDP 0.0.0.0:69 on a box whose NIC is
10.0.2.15/24. A client process on the *same box* does
`sendto(fd, req, n, 0, {AF_INET, 69, 10.0.2.15}, 16)` — the ordinary
"connect to my own advertised IP" pattern. The send burns 32
`sched_yield()`s and returns `EHOSTUNREACH`. The service is reachable
from the rest of the network but not from its own machine.

**Fix.** Before the subnet scan in `route_for_v4`, test `daddr` against
every registered netdev's `ip4_addr`; on a match return the loopback
device with `*via_gw_out = 0`, and have `ip4_input` accept a
destination equal to any local interface address when the frame arrives
on `lo`. Mirror it in `route_for_v6`/`ip6_input`.

### IP-04 (high). Limited broadcast 255.255.255.255 is unicast to the default gateway

**RFC 1122 3.3.6** defines 255.255.255.255 as the limited broadcast
address, whose datagrams are received by every host on the connected
physical network and not forwarded outside it. The string "broadcast"
appears zero times in `docs/rfc/rfc768.txt`.

**Code.** `route_for_v4` (`sys/net/inet.c:158-188`) has three arms:
127/8 → loopback (`:160`), subnet match (`:170-179`), and "first UP NIC
with a gateway" (`:181-186`). `0xFFFFFFFF` matches neither of the first
two — `(0xFFFFFFFF & 0xFF) == 255 != 127`, and the subnet compare
reduces to `prefix == mask`, false for any sane configuration — so it
falls into the gateway arm and returns `via_gw = 1` (`:184`).
`ip4_output` then computes `nexthop = dev->ip4_gateway`
(`sys/net/inet.c:256`) and `eth_send`s a datagram carrying IP
destination 255.255.255.255 inside a frame addressed to the router's
*unicast* MAC (`:285`). `sendto()` returns success. Nothing in
`sys/net` tests for a broadcast destination on transmit: the only
`0xFFFFFFFF` comparisons in the directory are input-path checks
(`sys/net/inet.c:322`, `:338`) and `icmp.c:54`/`:67`.
`NETDEV_IFF_BROADCAST` (`sys/include/sys/netdev.h:69`) is set by every
NIC driver and read nowhere.

**Failure.** eth0 = 10.0.2.15/24, gw 10.0.2.2.
`sendto(s, discover, 300, 0, {AF_INET, 67, 255.255.255.255}, 16)`
returns 300. Exactly one host on the segment — the router — receives
the frame, and a conformant router discards a limited broadcast rather
than forwarding it. DHCP DISCOVER/REQUEST, NetBIOS name registration
and every "announce to the LAN" datagram are silently dropped while the
application is told the send succeeded.

**Fix.** Add a first arm to `route_for_v4` for `daddr == 0xFFFFFFFF`
selecting the first UP, non-loopback `NETDEV_IFF_BROADCAST` device with
`via_gw = 0`, and short-circuit next-hop resolution for it (IP-05).

### IP-05 (high). Directed broadcast is ARPed for — which fails, and lets any on-link host capture all of our broadcast traffic for five minutes

**RFC 1122 3.3.6** requires a directed broadcast (10.0.2.255 on a
10.0.2.0/24 link) to reach every host on that link, which on Ethernet
means the all-ones destination MAC (RFC 894). ARP (RFC 826) resolves
individual host addresses, not broadcast addresses.

**Code.** `ip4_output`'s next-hop block
(`sys/net/inet.c:253-284`) has exactly two cases: loopback (skip L2)
and everything else, which goes to `arp_lookup`/`arp_request` on
`nexthop` (`:256-258`). There is no broadcast case and no multicast
case; the only all-ones MAC literal in the networking tree is
`sys/net/arp.c:140`, used for ARP request framing. The IPv6 twin proves
the omission is accidental: `ip6_output` has precisely this branch for
multicast at `sys/net/inet6.c:269-273`, exactly where the v4 code has
nothing.

Two consequences. First, a directed broadcast matches its own subnet
(`via_gw = 0`), `nexthop` becomes the broadcast address,
`arp_request` puts a bogus "who-has 10.0.2.255" on the wire, the code
spins 32 `sched_yield()`s (`sys/net/inet.c:275-278`) and returns
`-EHOSTUNREACH` (`:281`). Every subnet-directed broadcast transmit
fails, at a cost of 32 scheduler round-trips each.

Second, that ARP request is answerable by anyone. `arp_input` accepts a
reply whose `ar_tpa` is our own address — `target_is_me` at
`sys/net/arp.c:189` — and inserts the binding at
`sys/net/arp.c:194-195`. A standard reply to our query carries
`ar_spa = 10.0.2.255`, `ar_tpa = 10.0.2.15`, so it passes the ARP-02
"only insert when addressed to us" gate cleanly: by the code's own
definition this is a *solicited* reply. The entry is then fresh for
`ARP_TTL_MS` = 5 minutes (`sys/net/arp.c:41`).

**Failure.** Host A (10.0.2.15/24) tries
`sendto(…, 10.0.2.255, port 137)`. Attacker B on the segment replies
`ar_spa=10.0.2.255, ar_sha=B`. `arp_input` caches
10.0.2.255 → B. For the next five minutes every subnet-directed
broadcast A sends is delivered as a unicast frame to B alone; no other
host sees them, and A's sends now report success, so the failure is
invisible to A. Unlike classic ARP spoofing there is no legitimate
owner to race — the reply is uncontested, and the victim solicited it.
A proxy-ARP box or a misconfigured appliance produces the same hijack
without malice.

**Fix.** Before the `arp_lookup` in `ip4_output`, test the destination
against `0xFFFFFFFF` and against
`(dev->ip4_addr & dev->ip4_netmask) | ~dev->ip4_netmask`; on a match
set the MAC to `ff:ff:ff:ff:ff:ff` and skip resolution, mirroring
`sys/net/inet6.c:269-273`. Additionally, refuse to insert a cache entry
whose IP is 255.255.255.255, a link broadcast address, or in 224/4, in
`arp_insert_raw` (`sys/net/arp.c:107`).

### IP-06 (high). IPv4 multicast is absent end to end

**RFC 1112 §6.3/§6.4 and RFC 1122 3.3.7** require a host to support
local IP multicasting on connected networks, with the Ethernet mapping
`01:00:5e:<low 23 bits of the group>`. RFC 768 is silent on multicast.
Five independent pieces are missing; they are one defect because no
subset of them is useful alone.

1. **No route.** `route_for_v4` has no Class D case
   (`sys/net/inet.c:158-188`): 224.0.0.251 fails the 127/8 test, fails
   every subnet compare, and lands in the gateway arm with
   `via_gw = 1` (`:184`) — or returns NULL if no gateway is configured,
   making even 224.0.0.1 `ENETUNREACH` (`:215`).
2. **No L2 mapping.** `ip4_output` computes
   `nexthop = via_gw ? dev->ip4_gateway : daddr` (`sys/net/inet.c:256`)
   and ARPs it, so the datagram leaves addressed to the router's
   unicast MAC (`:285`). Grepping `sys/net/inet.c` for `0x5e` returns
   nothing, while the IPv6 equivalent exists at
   `sys/net/inet6.c:269-273`.
3. **No input.** `ip4_input`'s acceptance filter
   (`sys/net/inet.c:336-342`) accepts only the exact interface address
   and the two broadcast forms, so a Class D destination returns at
   `:341` — before the protocol switch at `:346` that calls
   `udp_input`, and before the unconditional raw delivery at `:370`.
   Not even a `SOCK_RAW` socket can observe the traffic. (The IPv6 twin
   at `sys/net/inet6.c:327` has the opposite defect — see IP-09.)
4. **No membership state.** There is nowhere to record a join.
   `afi_sock_t` (`sys/net/af_inet.c:115-148`) has no group list;
   `netdev_t` (`sys/include/sys/netdev.h:34-65`) has none either and
   there is no `NETDEV_IFF_MULTICAST` among the flags at
   `sys/include/sys/netdev.h:67-72`; `struct netdev_ops`
   (`sys/include/sys/netdev.h:28-33`) has exactly one entry point,
   `xmit`, so the stack could not inform a driver of a group even if it
   tracked one. Userland advertises the full API anyway
   (`include/netinet/in.h:151-152`, `:168-171`).
5. **No NIC filter.** e1000 zeroes the 128-entry Multicast Table Array
   at `sys/drivers/net/e1000.c:315` and writes RCTL without `RCTL_MPE`
   at `:383-384` (`RCTL_MPE` is defined at `:69` and used nowhere), so
   the hash filter rejects every multicast frame. r8168 zeroes MAR0 at
   `sys/drivers/net/r8168.c:441-442` and then ORs in `RCR_AM` at
   `:567` — hash filtering against an all-zero hash. rtl8139 sets
   `RCR_AM` at `sys/drivers/net/rtl8139.c:128` and `:304` but never
   writes MAR0..7 at all, so which groups are accepted depends on the
   chip's reset state.

**Also.** `ip4_output` hardcodes `ih->ttl = 64`
(`sys/net/inet.c:245`) — it has no `ttl` parameter and no caller could
supply one — where RFC 1112 §6.1 requires a multicast default of 1 so a
group send does not leave the local link unless asked. `IP_MULTICAST_TTL`
exists at `include/netinet/in.h:149` and is dropped by
`sys_setsockopt` (`sys/net/af_unix.c:2616`).

**Failure.** An mDNS responder binds 0.0.0.0:5353, calls
`setsockopt(IP_ADD_MEMBERSHIP, {224.0.0.251, INADDR_ANY})` — which
returns 0 (I-04) — and enters its `select()` loop reporting itself
healthy. Outbound: its query is unicast to the router's MAC with IP
destination 224.0.0.251, so no listener on the link sees it and the
router drops a link-local-scope group it must not forward. Inbound: a
peer's query is dropped by the NIC hash filter on e1000/r8168, and by
`sys/net/inet.c:341` on any NIC that passes it. Identical for SSDP
(239.255.255.250), NTP (224.0.1.1), RIPv2 (224.0.0.9) and the all-hosts
group 224.0.0.1.

**Fix.** Add a Class D arm to `route_for_v4` (direct, `via_gw = 0`, on
the first UP multicast-capable device); add a branch in `ip4_output`
before `sys/net/inet.c:254` building
`mac = {0x01,0x00,0x5e, b1 & 0x7f, b2, b3}`; extend the accept test at
`sys/net/inet.c:336-342` with a Class D branch gated on a real
membership list (always accepting 224.0.0.1, per RFC 1122 3.3.7); add a
per-socket group list to `afi_sock_t` and a per-interface list to
`netdev_t` with a `set_rx_mode` op in `struct netdev_ops`; and give
`ip4_output` a `ttl` argument defaulting to 1 for Class D.

### IP-07 (medium). An interface with no IPv4 address cannot transmit at all, so UDP DHCP cannot bootstrap

**RFC 2131 4.1** requires a client with no lease to send from 0.0.0.0
to 255.255.255.255. RFC 768 is silent on address-less transmit.

**Code.** `route_for_v4` rejects any device with no configured address
in both of its non-loopback arms: `if (!d->ip4_addr) continue;`
(`sys/net/inet.c:173`) and
`if (!d->ip4_addr || !d->ip4_gateway) continue;` (`:183`). An interface
that is UP but unconfigured matches no route, `route_for_v4` returns
NULL, and `ip4_output` returns `-ENETUNREACH` (`:215`) — including for
255.255.255.255. `ip4_output` itself would do the right thing if it got
that far (`ih->saddr = dev->ip4_addr` = 0 at `:248`); only the route
lookup blocks it. The in-tree client is the evidence:
`sbin/dhclient/dhclient.c` hand-builds the entire Ethernet+IP+UDP frame
and sends it over AF_PACKET (`sbin/dhclient/dhclient.c:358`), setting
`ih->saddr = 0` and `ih->daddr = 0xFFFFFFFF` itself (`:312-313`), and
its header comment (`:9-11`) states the reason outright.

**Failure.** Boot with eth0 up and unconfigured. A conventional DHCP
client does `socket(AF_INET, SOCK_DGRAM)`, `bind(0.0.0.0:68)`,
`sendto(…, 255.255.255.255:67)` and gets `ENETUNREACH`. It can never
obtain a lease through the UDP socket API; it must be rewritten against
AF_PACKET, as the shipped `dhclient` was. Same for BOOTP and
PXE-style discovery.

**Fix.** Let the limited-broadcast route select an UP, non-loopback
`NETDEV_IFF_BROADCAST` device regardless of `ip4_addr`, and let
`ip4_output` emit `saddr` 0.0.0.0 in that case.

### IP-08 (medium). A broadcast datagram is delivered to exactly one socket

**RFC 1122 3.3.6**: a broadcast is received by every host on the link —
and, by universal BSD/Linux behaviour that broadcast protocols depend
on, by every matching socket on each host. RFC 768 is silent on
fan-out.

**Code.** `ip4_input` accepts subnet and limited broadcast
(`sys/net/inet.c:336-341`) and hands them to `udp_input` exactly like
unicast. `afinet_deliver_v4` then applies the best-match rule
unconditionally: it accumulates a single `best` in the walk
(`sys/net/af_inet.c:1672`) and enqueues to it alone (`:1675-1679`),
with no special case for a broadcast destination. Two sockets
wildcard-bound to the same port (which the `SO_REUSEADDR` path at
`sys/net/af_inet.c:899` permits) both score 0
(`sys/net/af_inet.c:1585`), and since `g_afi_head` is prepended at
socket() time (`:861-862`) and the replacement test is a strict
`score > best_score` (`:1672`), the *newest* socket takes it.

**Failure.** A NetBIOS/SSDP listener and a monitoring tool each bind
0.0.0.0:137 with `SO_REUSEADDR`. A broadcast datagram arrives for
255.255.255.255:137 and goes to whichever socket was created later; the
other never sees it. Broadcast discovery protocols silently work for
only one participant per host.

**Fix.** Pass a "destination was broadcast/multicast" flag from
`ip4_input`/`ip6_input` through `udp_input` into `afinet_deliver_v*`
and, in that case, enqueue a copy into every socket whose
`sock_score()` ≥ 0, keeping best-match selection for unicast.

### IP-09 (medium). `ip6_input` accepts every IPv6 multicast group, with no membership check and a comment claiming otherwise

**RFC 1122 3.3.7 / RFC 4291 §2.7**: a host accepts a multicast
destination only for groups it has joined. RFC 768 is silent.

**Code.** `sys/net/inet6.c:325-328` sets `for_us = 1` when
`memcmp(h->dst, dev->ip6_addr, 16) == 0` **or** when
`h->dst[0] == 0xff` — every group, at every scope, unconditionally. The
comment at `:323-324` reads "or any-multicast we joined", which is
inaccurate: there is no join state anywhere (see IP-06 item 4). So the
two families have exactly opposite defects: `sys/net/inet.c:340` drops
groups the host joined, `sys/net/inet6.c:327` accepts groups it never
did. The demux does not compensate — `sock_score` matches on
`s->local_port == dport` and, for a wildcard `local_addr`, does not
compare the destination address at all
(`sys/net/af_inet.c:1583-1589`).

**Failure.** On an interface whose L2 filter passes multicast, a
process binds `[::]:5353` for ordinary unicast use and never joins a
group. A neighbouring host's mDNS traffic (dst `ff02::fb`, dport 5353,
valid checksum) is accepted at `sys/net/inet6.c:327`, passes the
mandatory v6 checksum check at `sys/net/udp.c:52-54`, scores 0 on the
port match alone, and is enqueued. The application receives and must
parse attacker-chosen datagrams for a group it never joined, from any
sender on the segment, including site- and organization-scope groups
(ff05::/ff08::).

**Fix.** Replace the blanket `h->dst[0] == 0xff` accept with an
explicit test against the interface's solicited-node address and
`ff02::1`, plus a real membership list once one exists, and fix the
comment at `sys/net/inet6.c:323-324`.

### IP-10 (medium). UDP over IPv6 is undeliverable behind any extension header

**RFC 8200 §4** requires extension headers to be traversed to the
upper-layer header; **§8.1**: *"If the IPv6 packet contains a Routing
header, the Destination Address used in the pseudo-header is that of
the final destination."* RFC 768 predates IPv6.

**Code.** `ip6_input` dispatches on the IPv6 header's first Next Header
value alone (`sys/net/inet6.c:332-341`): only `IPPROTO_ICMPV6` and
`IPPROTO_UDP_NUM` are cased, everything else falls to
`default: break`. There is no extension-header walk anywhere in the
file — no Hop-by-Hop, Destination Options, Routing or Fragment
handling — so a UDP datagram carrying any extension header never
reaches `udp_input` and is discarded. The same gap leaves RFC 8200
§8.1's pseudo-header rules with no implementation behind them:
`ip6_input` passes `h->dst` verbatim (`sys/net/inet6.c:337`), which
`udp_input` uses as the pseudo-header destination
(`sys/net/udp.c:52-53`) where §8.1 requires the final destination; the
pseudo-header Next Header must be the upper-layer protocol, not the
IPv6 header's; and `l4_len` is the whole payload length
(`sys/net/inet6.c:331`), which would overstate the upper-layer length.
Those three are accidentally correct today only because such packets
are dropped before any checksum is attempted.

**Failure.** A peer sends a UDP/IPv6 datagram with a Destination
Options header (Next Header 60) or a Fragment header (44) — the latter
is what Linux emits for any v6 datagram that needed fragmentation.
`h->next_header` is 60/44, the switch at `sys/net/inet6.c:332` takes
`default: break`, `udp_input` is never called, and
`afinet_deliver_v6` at `:342` is invoked with protocol 60/44, which
`sock_score` rejects for every `SOCK_DGRAM` socket
(`sys/net/af_inet.c:1582`). The datagram vanishes with no ICMPv6
Parameter Problem and no counter.

**Fix.** Walk past Hop-by-Hop/Routing/Destination Options/Fragment to
the first upper-layer header and pass the resulting protocol, adjusted
payload pointer/length and (with a Routing header) final destination to
`udp_input`. Unrecognised Next Header values should produce an ICMPv6
Parameter Problem, not a silent drop.

### IP-11 (low). `lo` advertises MTU 16384; `lo_xmit` truncates at 1700 and reports success

**RFC 1122 3.3.3**: an interface's advertised MTU is what the layers
above it may use. RFC 768 is silent on MTU.

**Code.** `loopback_init` sets `lo_netdev.mtu = 16384`
(`sys/net/loopback.c:124`), handed to userland verbatim by
`SIOCGIFMTU` (`sys/net/af_inet.c:362-363`). Nothing can carry a frame
that large: `lo_xmit` clamps with
`if (len > LO_FRAME_MAX) len = LO_FRAME_MAX;`
(`sys/net/loopback.c:75`, `LO_FRAME_MAX` = 1700 at `:53`) and then
returns 0 — success, for a frame it truncated. The clamp is unreachable
today because `netdev_xmit` rejects `len > 1614`
(`sys/net/netdev.c:70`), `eth_send` rejects payloads > 1600
(`sys/net/inet.c:133`) and the UDP paths reject sizes > 1572
(`sys/net/af_inet.c:678`/`:710`/`:1323`/`:1350`). Were it reached,
`ip4_input`'s `if (tot > len) return;` (`sys/net/inet.c:302`) would
discard the short frame, so the failure mode would be a silent drop
reported as a successful send. The live defect is the false MTU.

**Failure.** An application queries `lo`'s MTU (16384), sizes an
8000-byte datagram accordingly, and `sendto(127.0.0.1)` returns
`EMSGSIZE` from the `AFI_DATA_MAX` check. The interface advertised a
capability the stack cannot provide.

**Fix.** Set `lo_netdev.mtu` to a value the path can carry, and make
`lo_xmit` return `-EMSGSIZE` for `len > LO_FRAME_MAX` instead of
truncating and reporting success.

## 2c. Socket API

### API-01 (high). Any unprivileged process can steal a bound UDP port

**Requirement.** Not an RFC matter — RFC 768 says only that
*"Destination Port has a meaning within the context of a particular
internet destination address."* The reserved-port rule (<1024 needs
root) is universal Unix practice, and BSD/Linux `SO_REUSEPORT` requires
matching euid.

**Code.** `afinet_bind()` rejects a duplicate port only when the
*calling* socket has not set `SO_REUSEADDR`:

```
899        if (req && !s->reuseaddr && afinet_port_taken(s, req))
900            return -EADDRINUSE;
```

The incumbent socket's own `reuseaddr` flag is never consulted, and
neither socket's uid is compared — `setsockopt(SO_REUSEADDR)` is
unconditionally available (`sys/net/af_unix.c:2607-2614`). There is no
reserved-port check anywhere in `sys/net/af_inet.c`; `euid` appears
only on `SIOCSIF*` (`:281`, `:337`) and `SOCK_RAW` creation (`:820`),
so an unprivileged process can bind 53/67/123/111 outright when free.

The demux then deterministically favours the intruder: sockets are
prepended at `socket()` time
(`s->next = g_afi_head; g_afi_head = s;`, `sys/net/af_inet.c:861-862`)
so the newest is walked first, and the best is replaced only on a
strict improvement (`if (score > best_score)`,
`sys/net/af_inet.c:1672`), so on an equal score the newest keeps the
datagram. Two wildcard-bound unconnected sockets on the same port both
score 0 (`sys/net/af_inet.c:1585`).

**Failure.** A root resolver holds `bind(0.0.0.0:53)`. An unprivileged
user runs `s=socket(AF_INET,SOCK_DGRAM,0);
setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,4); bind(s,0.0.0.0:53)`. The
bind succeeds (line 899 short-circuits on `s->reuseaddr`). Every
datagram for port 53 scores 0 for both sockets, the walk starts at
`g_afi_head` — the attacker's newer socket — and line 1672 keeps it.
The resolver goes permanently silent and the attacker reads and can
spoof-answer all DNS traffic: a full local hijack of a privileged
service port, with no privilege.

**Fix.** Require the incumbent to have set `SO_REUSEADDR` too, and
require matching euid, before allowing a duplicate bind; reject
`bind()` to a port < 1024 unless `current_process->euid == 0`; and when
duplicate binds are legitimately permitted, make the tie-break
deterministic and documented rather than "most recently created wins".

### API-02 (high). Multi-iovec `sendmsg()` on a UDP socket always fails `EFAULT`

**Requirement.** POSIX `sendmsg(2)`. RFC 768 is silent on
scatter/gather; the datagram-framing rationale for gathering is the
code's own SOCK-04 comment at `sys/net/af_unix.c:2237-2244`.

**Code.** The SOCK-04 fix gathers a datagram `sendmsg`'s iovecs into
one `kmalloc`'d kernel buffer (`sys/net/af_unix.c:2249`) and calls
`sys_sendto_impl(…, /*kernel_payload=*/1)`
(`sys/net/af_unix.c:2260-2263`). `sys_sendto_impl` honours
`kernel_payload` only on the AF_UNIX branch
(`sys/net/af_unix.c:2047-2055`); the AF_INET/AF_INET6 route at
`sys/net/af_unix.c:1956` and the no-addr fd-type route at `:1969` both
drop the flag and call `afinet_sendto()`. `afinet_sendto()` then does
`copyin((const uint8_t *)ubuf + total, kbuf, chunk)`
(`sys/net/af_inet.c:1404`), and `validate_user_addr()` rejects any
range whose end is above `KERN_BASE`
(`sys/kern/subr_copy.c:46-47`). `kmalloc` memory is kernel
direct-mapped at 0xC0000000+, so the copy can never succeed and
`afinet_sendto` returns `total ? total : -EFAULT` with `total == 0`.

**Failure.** `sendmsg()` on an AF_INET `SOCK_DGRAM` (or `SOCK_RAW`) fd
with `msg_iovlen >= 2` and a non-zero total length returns `-1/EFAULT`
and puts nothing on the wire, for every caller, however valid the user
buffers. This is exactly the libtirpc `svc_dg_reply` header+body
two-iovec reply the SOCK-04 comment names as the motivating case: UDP
RPC replies now fail outright instead of being split into two
datagrams, so rpcbind-style services answer nothing at all.

**Fix.** Plumb `kernel_payload` through the AF_PACKET/AF_INET/AF_INET6
routes — an `afinet_sendto_kbuf()` entry (or a flag) that skips the
`copyin` and calls `afinet_sendto_k()` directly with the gather buffer.

### API-03 (high). `writev(2)` on a connected UDP socket emits one datagram per iovec

**Requirement.** POSIX: `write`/`writev` on a `SOCK_DGRAM` socket emits
exactly one datagram. RFC 768 describes one send operation carrying
"the data" and says nothing about scatter/gather.

**Code.** The FreeBSD/NetBSD `sys_writev`
(`sys/exec/perso/compat.c:569-578`, wired at `perso_freebsd.c:110` and
`perso_netbsd.c:352`) loops calling `sys_write()` once per iovec —
`ssize_t r = sys_write(fd, kiov[i].iov_base, kiov[i].iov_len);`
(`sys/exec/perso/compat.c:572`). The Linux personality's
`linux_sys_do_uio` (`sys/exec/perso/perso_linux.c:662`) loops calling
`kern_write()` once per iovec and again per chunk
(`sys/exec/perso/perso_linux.c:724`). Each call lands in
`afinet_node_write_body`'s `SOCK_DGRAM` arm
(`sys/net/af_inet.c:676-700`), which builds a complete UDP header and
calls `ip4_output` once per call. One `writev()` becomes N datagrams,
with N separate Length fields.

This codebase already identified and fixed the bug class for `sendmsg`
— SOCK-04, `sys/net/af_unix.c:2245-2266` — but `writev(2)` never
received the same treatment, so the same application gets correct
framing through `sendmsg` and broken framing through `writev` on the
identical fd.

**Failure.** A syslog client does `connect(fd, {10.0.0.1, 514})` then
`writev(fd, {{"<14>Jan 1 ", 10}, {"body text", 9}}, 2)`. Two datagrams
leave the box: Length 18 carrying the PRI+timestamp, and Length 17
carrying an orphaned fragment with no PRI. The peer's `recvfrom()`
returns two records.

**Fix.** Give `sys_writev` and `linux_sys_do_uio` the SOCK-04
treatment: when `sock_fd_is_dgram(fd)` and `iovcnt > 1`, gather into
one kernel buffer and issue a single write, returning `EMSGSIZE` if the
total exceeds the datagram cap. Factor the existing gather in
`sys/net/af_unix.c:2245-2266` into a shared helper rather than
duplicating it a third time.

### API-04 (high). `SO_RCVTIMEO`/`SO_SNDTIMEO` are accepted and discarded; the blocking receive loop has no deadline

**Requirement.** POSIX `setsockopt(SO_RCVTIMEO)`. RFC 768 is silent on
timeouts, though it does state that *"delivery and duplicate protection
are not guaranteed"* — which is precisely why the timeout exists.

**Code.** `sys_setsockopt` validates the fd
(`sys/net/af_unix.c:2599-2600`) and then falls through to `return 0;`
(`:2616`) for every option except `SO_REUSEADDR`; the comment at
`:2604-2606` says so outright. `SO_RCVTIMEO` (20) and `SO_SNDTIMEO`
(21) are exported to userland by `include/sys/socket.h:151-152`, so
applications set them and are told they succeeded. Nothing stores them:
`afi_sock_t` (`sys/net/af_inet.c:115-148`) has no timeout field.
`afinet_recvfrom`'s datagram loop (`sys/net/af_inet.c:1477-1536`) has
exactly four exits — a queued datagram (`:1524-1525`), `-EAGAIN` when
`MSG_DONTWAIT` or `FNONBLOCK` is set (`:1529`), `-EINTR`
(`:1531-1534`), and 0 on close (`:1535`). There is no timed variant of
`afi_wait` (`sys/net/af_inet.c:502-530`): its `sleep_expiry`
(`:514-519`) is a ~50 ms wakeup backstop against a lost wakeup, not a
deadline — `afi_wait` returns 0 on expiry and the `for(;;)` re-checks
`s->count` and sleeps again, indefinitely.

The surface is also self-contradictory: `setsockopt(SO_RCVTIMEO)`
returns 0 while `getsockopt` of the same option returns `-ENOPROTOOPT`
(`sys/net/af_unix.c:2727`), so a caller that probes with `getsockopt`
is told the option does not exist while the setter claims it worked.

**Failure.** A resolver sets `SO_RCVTIMEO` to 2 s (returns 0), sends a
query, and the reply is lost on the wire. `recvfrom()` enters the loop
with `s->count == 0` and no non-blocking flag, sleeps, wakes on the
50 ms backstop, re-checks an empty ring and sleeps again. The 2-second
timeout never fires: `recv()` never returns and the process is wedged
until a signal arrives or the fd is closed.

**Fix.** Add `rcv_timeo`/`snd_timeo` (ticks, 0 = none) to
`afi_sock_t`, populate them from `sys_setsockopt` (struct timeval,
`copyin`), give `afi_wait` an absolute-deadline argument — the
`sys/kern/futex.c:476` and `sys/kern/posix_mqueue.c:221-234` paths
already show the `sleep_expiry` pattern — and return `-EAGAIN` when the
deadline passes with an empty ring. Answer `getsockopt` from the stored
value instead of `-ENOPROTOOPT`.

### API-05 (high). The socket-option surface is hardcoded to Linux numbering, so every option from a BSD-personality program is discarded

**Requirement.** POSIX `setsockopt`/`getsockopt`. RFC 768 is silent on
socket options.

**Code.** `sys_setsockopt` and `sys_getsockopt` are the single
implementation shared by every personality —
`sys/exec/perso/perso_native.c:94-95`, `perso_linux.c:617-620`,
`perso_freebsd.c:104`/`:108`, `perso_netbsd.c:471`/`:491` and
`perso_openbsd.c:207`/`:220` all dispatch to the same two functions —
and neither consults the calling process's personality. Both hardcode
Linux numbering: `level == 1 /*SOL_SOCKET*/`
(`sys/net/af_unix.c:2607`, `:2648`), `SO_REUSEADDR` as 2 (`:2607`),
`SO_TYPE` as 3 (`:2646`), `SO_ERROR` as 4 (`:2645`),
`SO_SNDBUF`/`SO_RCVBUF` as 7/8 (`:2708`). On the BSDs `SOL_SOCKET` is
0xFFFF and the option numbers are entirely different (`SO_REUSEADDR`
0x0004, `SO_SNDBUF` 0x1001, `SO_RCVBUF` 0x1002, `SO_RCVTIMEO` 0x1006,
`SO_ERROR` 0x1007, `SO_TYPE` 0x1008). For a FreeBSD or NetBSD binary
the level test never matches, `setsockopt` falls to `return 0;`
(`:2616`), and `getsockopt` takes the "Non-SOL_SOCKET levels: stay
lenient" path (`:2729-2730`) and returns success with a value of 0 for
every option.

**Failure.** A FreeBSD-personality RPC daemon — the `/perso/freebsd`
rpcbind/ToolTalk case this tree already exercises — calls
`setsockopt(fd, 0xFFFF, 0x0004 /*SO_REUSEADDR*/, &one, 4)`. It returns
0, `s->reuseaddr` stays 0, and the subsequent `bind()` of a port still
held by a previous instance returns `-EADDRINUSE`
(`sys/net/af_inet.c:899-900`), so the daemon fails to restart. The same
binary calling `getsockopt(fd, 0xFFFF, 0x1008 /*SO_TYPE*/)` gets
success with 0 from `sys/net/af_unix.c:2730` — exactly the "bad service
type" failure the `SO_TYPE` arm at `:2669-2679` was added to fix for
native binaries, still live for every BSD one.

**Fix.** Canonicalise level/optname at the personality boundary (a
small table in `perso_freebsd.c`/`perso_netbsd.c`/`perso_openbsd.c`),
or have `sys_setsockopt`/`sys_getsockopt` canonicalise from
`current_process`'s personality before the tests at
`sys/net/af_unix.c:2607` and `:2648`. Returning `-ENOPROTOOPT` instead
of 0 for unrecognised levels would at least make the failure visible.

### API-06 (medium). `bind()` accepts any address, so a socket bound to a non-local address is permanently dead with no error

**Requirement.** RFC 1122 3.2.1.3 / POSIX: `EADDRNOTAVAIL` for an
address not assigned to the host. RFC 768's User Interface asks only
for *"the creation of new receive ports"*.

**Code.** `afinet_bind` validates family, length and port collisions,
then stores the caller's address with
`memcpy(s->local_addr, &sin->sin_addr, 4);`
(`sys/net/af_inet.c:910`) and sets `s->bound = 1` (`:939`). It never
checks that the address is 0.0.0.0, a broadcast address, or configured
on a netdev. The demux then uses it as an exact match key:
`sock_score` returns -1 unless `memcmp(s->local_addr, daddr, alen) == 0`
(`sys/net/af_inet.c:1586-1589`). Combined with `ip4_input`'s acceptance
test (`sys/net/inet.c:340`), which can only ever produce
`daddr == dev->ip4_addr` or a broadcast, a socket bound to anything
else can never score a match.

**Failure.** A resolver calls `bind(fd, {AF_INET, 53, 127.0.0.53}, 16)`;
it returns 0 and `getsockname` reports 127.0.0.53:53. Every subsequent
`recvfrom` blocks forever, because no `ih->daddr` surviving
`sys/net/inet.c:340` can equal `0x3500007F`. There is no error at bind
time and no error at receive time. A bind to another host's address
behaves identically.

**Fix.** When `sin_addr` is non-zero, walk
`netdev_first()`/`netdev_next()` and require a match against a device's
`ip4_addr` (or its broadcast address, or 127/8 once IP-02 is fixed);
return `-EADDRNOTAVAIL` otherwise. Same for the AF_INET6 branch at
`sys/net/af_inet.c:925`.

### API-07 (medium). `connect()` on a UDP socket assigns no local port

**Requirement.** BSD/POSIX: `connect()` implicitly binds a local port.
RFC 768 does not describe `connect()`.

**Code.** `afinet_connect()` records the peer and sets
`s->connected = 1` (`sys/net/af_inet.c:1219-1220`, `:1253`) but, unlike
the TCP arm (which syncs the PCB's local endpoint back at
`:1235-1240`), never allocates a local port for a datagram socket.
`s->local_port` stays 0, and the demux rejects such a socket outright:
`if (s->local_port == 0 || s->local_port != dport) return -1;`
(`sys/net/af_inet.c:1583`). A port is allocated only lazily on the
first transmit (`sys/net/af_inet.c:1315-1319`, `:686-690`).

**Failure.** `connect(s, {10.0.0.1:5000}); recv(s, buf, n, 0)`. The
peer sends a datagram; `sock_score` returns -1 at
`sys/net/af_inet.c:1583`, `best` stays NULL, the datagram is dropped,
and `recv()` blocks forever. Separately, the very common "discover my
own source address" idiom — `connect(udp_fd, dest);
getsockname(udp_fd, &me)` — returns 0.0.0.0:0
(`sys/net/af_inet.c:1182`).

**Fix.** In `afinet_connect()`, for a `SOCK_DGRAM` socket with
`s->local_port == 0`, allocate via `afinet_alloc_ephemeral_free()` and
set `s->bound = 1` before returning, and fill `s->local_addr` from
`ip4_source_for(peer)`/`ip6_source_for(peer)`.

### API-08 (medium). `recv()` ignores `shutdown(SHUT_RD)` and blocks forever while `read()` returns EOF

**Requirement.** POSIX `shutdown(2)`/`recv(2)`. RFC 768 is silent.

**Code.** `afinet_shutdown()` sets `s->rd_shut = 1` and wakes blocked
readers (`sys/net/af_inet.c:980-983`). `afinet_node_read_body` honours
it — `if (s->rd_shut) return 0;` (`sys/net/af_inet.c:564`) — but
`afinet_recvfrom`, which is what `recv()`/`recvfrom()`/`recvmsg()`
reach through `recv_into_kbuf` (`sys/net/af_unix.c:1744`), never tests
it: `rd_shut` appears in `af_inet.c` only at lines 136, 536, 564 and
981. The function checks `!s` and `!buf` (`:1425-1427`) and then enters
the ring loop at `:1477`, whose only exits are a queued datagram,
`EAGAIN`, `EINTR` and `s->closed`. `afinet_node_poll` does not report
`POLLIN` for `rd_shut` either (`sys/net/af_inet.c:452-457`), so a
reader woken by the shutdown's `sched_wakeup` re-tests `s->count` (0)
and `!s->closed` and goes straight back to sleep.

**Failure.** Thread A blocks in `recv()`; thread B calls
`shutdown(s, SHUT_RD)` to unblock it. A is woken, finds
`s->count == 0` and `s->closed == 0`, and sleeps again. `recv()` never
returns, so the documented way to release a blocked datagram reader
does nothing — while `read()` on the same fd returns 0 immediately.

**Fix.** Test `s->rd_shut` in `afinet_recvfrom` at entry and after each
`afi_wait()` wake, returning 0, matching
`afinet_node_read_body:564`; report `POLLIN|POLLRDHUP` from
`afinet_node_poll` when it is set.

### API-09 (medium). A zero-length receive does not consume the datagram, so a `poll()`-driven drain loop spins

**Requirement.** POSIX `recv(2)`: a receive consumes one datagram.
Linux `udp_recvmsg` and FreeBSD `soreceive_dgram` both dequeue and
discard for a zero-length receive; that is what makes
`recv(fd,NULL,0,0)` the canonical discard idiom and
`recvmsg(…, MSG_PEEK|MSG_TRUNC)` with a zero-length iov the canonical
size probe.

**Code.** `do_recv()` short-circuits with `if (len == 0) return 0;`
(`sys/net/af_unix.c:1821`) *before* `recv_into_kbuf()` dispatches to
`afinet_recvfrom()`. The ring is never touched: `s->tail`/`s->count`
(`sys/net/af_inet.c:1496-1497`) do not advance and `*addrlen` is left
untouched. The `MSG_TRUNC`/`MSG_PEEK` handling at
`sys/net/af_inet.c:1495`/`:1523-1524` is unreachable when `len == 0`,
so the size probe reports 0 for *every* pending datagram. The same
short-circuit exists in the `recvmsg` scatter path
(`if (cap == 0) return 0;`, `sys/net/af_unix.c:2330`), and
`msg_iovlen == 0` falls through the loop at `sys/net/af_unix.c:2358`
without consuming anything. Meanwhile `afinet_node_poll` reports
`POLLIN` purely from `s->count > 0` (`sys/net/af_inet.c:455`), which a
legal 8-octet datagram does set (`sys/net/udp.c:25` admits `ulen == 8`;
`enqueue` stores `p->len = 0` and `count++` at
`sys/net/af_inet.c:1610-1613`).

**Failure.** A NAT keepalive (UDP header only, Length 8) arrives for a
bound socket. The application's event loop is the standard drain:
`poll()` → `POLLIN` → `recv(fd, buf, 0, MSG_TRUNC)` to learn the size /
discard it → `do_recv` returns 0 without touching the ring → `poll()`
still reports `POLLIN`. The process spins at 100% CPU forever and the
slot is never freed. The same probe against a 512-byte DNS reply
returns 0 instead of 512.

**Fix.** Move the `len == 0` short-circuit out of the datagram path:
for `SOCK_DGRAM`, let the call reach `afinet_recvfrom()` with `len` 0
so the datagram is dequeued (unless `MSG_PEEK`), the source address is
reported and `MSG_TRUNC` returns `p->truelen`. Keep the early return
for stream sockets.

### API-10 (medium). EOF leaves `*addrlen` at the 128-byte kernel bounce capacity

**Requirement.** POSIX `recvfrom(2)` `addrlen` in/out semantics. RFC
768's receive operation returns *"the data octets and an indication of
source port and source address"* — and since a legal zero-payload
datagram and a torn-down socket both return 0, that indication is the
only thing separating them.

**Code.** On the real-datagram path `afinet_recvfrom` writes it
correctly (`sys/net/af_inet.c:1501-1517`: `*addrlen = sizeof(struct
sin_kern)` = 16, port and address filled), so an empty datagram comes
back as "0 bytes from 192.0.2.1:4500, addrlen 16". But the EOF return
at `sys/net/af_inet.c:1535`
(`if (s->closed) { afi_rele_unlock(s, fl); return 0; }`) and the
`SOCK_STREAM` arm (`:1436-1447`) return without ever writing
`*addrlen`, and `do_recv` seeded it with the *bounce buffer's*
capacity, not the caller's: `socklen_t kaddrlen = sizeof(kaddr);` over
`uint8_t kaddr[128]` (`sys/net/af_unix.c:1811-1812`). The copy-out
block (`sys/net/af_unix.c:1843-1855`) then hands the caller
`*addrlen = 128` plus `min(128, user_cap)` zero bytes. The AF_UNIX arm
of `recv_into_kbuf` shows the intended contract — it explicitly does
`if (kaddrlen) *kaddrlen = 0;` (`sys/net/af_unix.c:1747-1748`) — the
AF_INET arm (`:1742-1743`) never does.

**Failure.** Thread A blocks in
`recvfrom(fd, buf, 1500, 0, &sa, &salen=16)`; thread B closes the
shared fd. A returns 0, and `do_recv` copies out 16 zero bytes with
`*salen = 128`. The application's standard discriminator — "0 with a
source address means an empty datagram, 0 with addrlen 0 means EOF" —
reports a datagram from 0.0.0.0:0, and any later use of the returned
128 (a `memcpy` of `*salen` bytes, or `sendto(…, sa, salen)`) reads 112
bytes past the 16-byte `sockaddr_in` the caller declared. The same
reaches every `recvfrom()` on a TCP socket.

**Fix.** Zero the out-parameter up front in `afinet_recvfrom`
(`if (addrlen) *addrlen = 0;`) so only the dequeue path sets it,
mirroring `recv_into_kbuf`'s AF_UNIX arm, and fill the peer address on
the `SOCK_STREAM` arm.

### API-11 (medium). The specific-destination address is discarded; `IP_PKTINFO` is accepted and does nothing

**RFC 1122 4.1.3.5**: *"When a UDP datagram is received, its
specific-destination address MUST be passed up to the application
layer."* RFC 768's User Interface requires only source port and source
address.

**Code.** `afinet_deliver_v4()` uses `daddr` for scoring and then
enqueues the source alone —
`enqueue(best, AF_INET, protocol, sport, &saddr, …)`
(`sys/net/af_inet.c:1676`; v6 twin at `:1736`). `afi_pkt_t`
(`sys/net/af_inet.c:101-113`) has no field for the destination, so the
information is gone by the time `afinet_recvfrom` runs; the recv path
fills in only `sin_addr`/`sin_port` from the stored source
(`sys/net/af_inet.c:1502-1517`). There is no ancillary-data path either
— `sys_recvmsg` populates `msg_control` only from the AF_UNIX
`SCM_RIGHTS` queue (`sys/net/af_unix.c:2391`) — and `sys_setsockopt`
returns 0 for every option it does not recognise
(`sys/net/af_unix.c:2616`), so an application that enables `IP_PKTINFO`
(`include/netinet/in.h:219`) or `IP_RECVDSTADDR` is told it succeeded
and then never receives the cmsg.

**Failure.** A DHCP/NTP/TFTP-style server binds 0.0.0.0:port and sets
`IP_PKTINFO`; `setsockopt` returns 0. A client broadcasts to
255.255.255.255:port. `recvmsg` returns the payload and the source
address but no cmsg and no indication that the datagram was addressed
to the broadcast address, so the server cannot tell which of its
addresses was the target. Its reply is then sourced from whatever
`ip4_source_for()` picks — the wrong interface on a multihomed host —
and the client discards it.

**Fix.** Add a destination field to `afi_pkt_t`, record `daddr` in
`enqueue()`, and expose it as an `IP_RECVDSTADDR`/`IP_PKTINFO` cmsg
from `recvmsg`. At minimum make `sys_setsockopt` return
`-ENOPROTOOPT` for IP-level options it does not implement.

### API-12 (medium). `IP_TTL` and `IP_TOS` are ignored, TTL is hardcoded 64, and `getsockopt(IP_TTL)` answers 0

**RFC 1122 4.1.4** requires the UDP/application interface to provide
the full IP/transport interface, which includes setting TTL and TOS.
RFC 768 does not mention either.

**Code.** `IP_TOS` (1) and `IP_TTL` (2) are exported by
`include/netinet/in.h:142-143`, but `setsockopt` with level
`IPPROTO_IP` never matches the single `level == 1` test at
`sys/net/af_unix.c:2607` and falls to `return 0;` at `:2616` —
accepted, stored nowhere (`afi_sock_t` has no ttl/tos field,
`sys/net/af_inet.c:115-148`), applied to nothing. `ip4_output`
hardcodes both on every transmit: `ih->tos = 0;`
(`sys/net/inet.c:241`) and `ih->ttl = 64;` (`sys/net/inet.c:245`).
These are the only writes to either field in the stack, and there is no
`IP_HDRINCL` path (the only mention is the comment at
`sys/net/af_inet.c:1301`). The read side is worse than silent:
`getsockopt(IPPROTO_IP, IP_TTL)` takes the "stay lenient" path at
`sys/net/af_unix.c:2729-2730` and returns success with 0 — not the real
64, and 0 is not a legal TTL.

**Failure.** A UDP traceroute sets `IP_TTL` to 1, 2, 3… per probe; each
`setsockopt` returns 0 so the tool believes it worked, but every probe
leaves with ttl=64, no router returns ICMP time-exceeded, and the tool
prints `* * *` for every hop forever. A VoIP or NTP sender setting
`IP_TOS` for DSCP marking emits tos=0 on every packet.

**Fix.** Add ttl/tos to `afi_sock_t` defaulting to 64/0, set them from
`setsockopt(IPPROTO_IP, …)` (validating 1..255 for TTL), thread them
into an `ip4_output` variant that takes them instead of hardcoding
`sys/net/inet.c:241`/`:245`, and answer `getsockopt` from the stored
values.

### API-13 (medium). IP options can be neither sent nor received

**RFC 1122 4.1.3.2** makes both directions a MUST: UDP must pass
received IP options up, and an application must be able to specify
options to send. RFC 768 says only, in its IP Interface section,
*"One possible UDP/IP interface would return the whole internet
datagram including all of the internet header in response to a receive
operation."*

**Code.** *Send*: `IP_OPTIONS` (4) is exported by
`include/netinet/in.h:145`; `setsockopt` at level `IPPROTO_IP` falls
straight to `return 0;` (`sys/net/af_unix.c:2616`) with nothing stored,
and `ip4_output` unconditionally writes
`ih->ihl_version = (4 << 4) | 5;` (`sys/net/inet.c:240`) and copies the
payload at exactly `pkt + sizeof(*ih)` (`sys/net/inet.c:251`), so a
20-byte header with no option space is structurally the only thing this
stack can emit. *Receive*: `ip4_input` parses the option-bearing header
length (`sys/net/inet.c:299`) and then hands UDP only what follows it —
`const uint8_t *l4 = pkt + hlen;` (`sys/net/inet.c:344`) — so the
option bytes are discarded before `udp_input` is called (`:351`). There
is no ancillary-data path that could carry them:
`sys_recvmsg` writes `msg_controllen = 0` for everything but the
AF_UNIX `SCM_RIGHTS` queue (`sys/net/af_unix.c:2391`, `:2457`).

**Failure.** A datagram arrives carrying a Record Route or Loose Source
Route option (IHL=8). It is validated, then delivered with the options
silently gone and no control message, so a server required to reverse
the source route cannot see it and answers via default routing — which
for a source-routed request sends the reply somewhere the client cannot
receive it. Symmetrically, a client that sets `IP_OPTIONS` is told it
succeeded and every datagram leaves with ihl=5.

**Fix.** At minimum, return `-ENOPROTOOPT` from
`setsockopt(IPPROTO_IP, IP_OPTIONS)` so the absence is detectable. A
real implementation stores the option block on the socket, has
`ip4_output` build a header of `sizeof(iphdr)+optlen` with IHL set
accordingly, and passes received option bytes up from
`sys/net/inet.c:344` for `recvmsg` to emit as
`IP_RECVOPTS`/`IP_RETOPTS`.

### API-14 (medium). `getsockopt(SO_RCVBUF)` invents 32768, and `getsockopt` never checks the fd is a socket

**Requirement.** POSIX `getsockopt(2)`. RFC 768 is silent on buffer
reporting.

**Code.** The `SO_SNDBUF`/`SO_RCVBUF` arm reads
`afunix_sock_t *us = afunix_from_fd(fd); int v = (us && us->type ==
SOCK_DGRAM) ? (192 * 1024) : 32 * 1024;`
(`sys/net/af_unix.c:2717-2719`). For an AF_INET fd `afunix_from_fd`
returns NULL, so every UDP socket reports `SO_RCVBUF` = `SO_SNDBUF` =
32768 — a number chosen (per the comment at `:2709-2716`) for the
AF_UNIX `SOCK_DGRAM` ring and applied to UDP by fallthrough. Neither
figure describes the implementation: the receive path admits exactly 32
datagrams (`sys/net/af_inet.c:1601`) whatever their size, so the true
byte capacity ranges from ~256 bytes of payload to 50304 bytes and is
never 32768. The value is not settable either
(`sys/net/af_unix.c:2616`), so a caller that reads 32768, doubles it,
writes it back and reads again has no way to detect the option is
inert. Separately, `sys_getsockopt` (`sys/net/af_unix.c:2634-2642`)
never checks that `fd` is a socket at all — unlike `sys_setsockopt`,
which does (`:2599-2600`) — so `getsockopt(SOL_SOCKET, SO_RCVBUF)` on a
regular file or directory returns 0 and writes 32768 into the caller's
int.

**Failure.** An RPC or NFS client sizes its outstanding-request window
from `getsockopt(SO_RCVBUF)` = 32768 and concludes it may have ~32 KiB
of replies in flight. With 200-byte replies that is 160 datagrams; the
socket accepts 32 and discards 128, silently (RES-03). Its own flow
control is calibrated against a number the kernel invented.

**Fix.** Report a value the implementation honours — once a
byte-bounded `rcvbuf` exists (RES-01), return that; until then return
the real admission capacity for the family and route the AF_INET case
away from the AF_UNIX constants. Add the `sock_fd_is_socket()` check to
`sys_getsockopt`.

### API-15 (medium). `SO_BROADCAST` is neither storable nor enforced

**Requirement.** BSD/POSIX convention, not an RFC. Reported as interop.

**Code.** `sys_setsockopt` handles exactly one option
(`sys/net/af_unix.c:2607`) and returns 0 for the rest (`:2616`);
`afi_sock_t` has no field for it — `reuseaddr`
(`sys/net/af_inet.c:130`) is the only option ever recorded. In the
other direction `sys_getsockopt` hard-codes `SO_BROADCAST` to 0
(`sys/net/af_unix.c:2721-2723`), so an application that sets the option
and reads it back to confirm is told the kernel refused it. And neither
`afinet_sendto_k` (`sys/net/af_inet.c:1304-1333`) nor
`afinet_node_write_body` (`:675-700`) nor `ip4_output` examines any
such flag, so the BSD/Linux `EACCES` guard is absent: an unprivileged
process is never stopped from addressing a broadcast destination.

**Failure.** A port following the standard idiom — set the option, then
`getsockopt` to verify — reads back 0, logs "broadcast not permitted on
this socket", and aborts before sending. Symmetrically, a process that
never sets the option can `sendto()` 10.0.2.255 with no error path, so
there is no point in the stack at which broadcast is either enabled or
denied.

**Fix.** Add a `broadcast` flag to `afi_sock_t`, wire `SO_BROADCAST`
into `sys_setsockopt` beside `SO_REUSEADDR` and into `sys_getsockopt`
in place of the hardcoded 0, and return `-EACCES` from the datagram
send paths when the destination is a broadcast address and the flag is
clear.

### API-16 (medium). `write()` on a torn-down socket returns a successful zero-byte transfer

**Requirement.** POSIX: `write()` either transfers bytes or fails. RFC
1122 4.1.4 defers the interface to the host.

**Code.** `afinet_node_write_body` opens with
`if (!s || s->closed) return 0;` (`sys/net/af_inet.c:666`), and
`afinet_node_write` does the same at `:651`. Zero is below VFS-28's
error threshold (`vfs_is_err_value`, `sys/vfs/vfs.c:532-534`, which
only recognises values ≥ `(size_t)-4095`), so `write_fs` reports a
clean 0-byte transfer and `sys_write` returns 0 to userland for a
non-zero request. The kernel already knows this is a trap: the sibling
branch at `sys/kern/syscall.c:483-489` returns `-EBADF` precisely
because "Userland write loops (zsh write_loop, glibc fwrite, etc.) only
check for ret<0, so returning 0 silently makes them spin forever." The
same reasoning was not carried over here. The state is reachable:
`afinet_node_close` zeroes `node->impl` and sets `s->closed`
(`sys/net/af_inet.c:738-741`) while a concurrent thread is between
`afinet_node_write`'s read of `node->impl` (`:650`) and
`write_body`'s re-read at `:665`; the NET-01 refcount keeps the memory
alive but does not stop `write_body` from observing the zeroed `impl`.

**Failure.** Thread A is inside `write(fd, buf, 512)` past
`sys/net/af_inet.c:650`; thread B calls `close(fd)`. A reaches `:665`,
reads `impl == 0`, and returns 0. glibc's `write_loop` sees 0 < 512,
does not advance, retries, and gets 0 forever — an unkillable-looking
userland spin with no errno set, instead of the `EBADF`/`EPIPE` the
caller would have handled.

**Fix.** Return `(size_t)-EBADF` (or `-EPIPE` for a peer-closed socket)
at `sys/net/af_inet.c:651` and `:666`, matching the `-EBADF` at
`sys/kern/syscall.c:489`. The read path's analogous returns are
semantically correct (0 = EOF) and should stay.

### API-17 (medium). A bound or connected `SOCK_RAW` socket is not filtered by address

**Requirement.** BSD `rip_input` compares `inp_faddr` against `ip_src`
and `inp_laddr` against `ip_dst`; Linux `raw_v4_input` compares
`inet->inet_daddr`/`inet_rcv_saddr`. RFC 768 governs only the UDP
demux.

**Code.** `sock_score()` short-circuits for `SOCK_RAW` and returns 0
whenever the protocol matches:

```
1576    if (s->type == SOCK_RAW)
1577        return (s->protocol == 0 || s->protocol == (int)proto) ? 0 : -1;
```

It never consults `s->local_addr` (set by `afinet_bind`,
`sys/net/af_inet.c:910`), `s->peer_addr`/`s->peer_port`/`s->connected`
(set by `afinet_connect`, `:1219-1220`), nor the packet's destination,
even though all of them are passed in as arguments. This is exactly the
defect the UDP-01 comment above the function
(`sys/net/af_inet.c:1545-1566`) describes and fixes for `SOCK_DGRAM`
one line later (`:1586-1595`); the fix was not extended to the RAW arm.

**Failure.** Root runs
`fd=socket(AF_INET,SOCK_RAW,IPPROTO_UDP); connect(fd,{10.0.2.2});
recv(fd,b,2048,0)`. Any host on the segment sends an IPv4 packet with
protocol 17 and src 10.0.2.99. `ip4_input` accepts it and calls
`afinet_deliver_v4(…, for_dgram=0)` (`sys/net/inet.c:370`);
`sock_score` returns 0 at `:1577` without looking at `s->connected`, so
`enqueue` queues the forged packet and `recv()` hands it to the caller
as though it came from the connected peer. Because the ring is 32 deep
and `enqueue` silently drops on overflow
(`sys/net/af_inet.c:1601`), 32 such packets also evict the traffic the
socket asked for.

**Fix.** In the RAW arm, before returning 0, apply the same address
tests the DGRAM arm uses: if `s->local_addr` is not wild require
`memcmp(s->local_addr, daddr, alen) == 0`; if `s->connected` and
`s->peer_addr` is not wild require `memcmp(s->peer_addr, saddr, alen)
== 0`. Leave the score at 0 so RAW keeps its fan-out.

### API-18 (medium). Ephemeral port allocation is a non-atomic counter plus a check-then-record outside the lock

**RFC 768, "Fields"**:

> Source Port is an optional field, when meaningful, it indicates the
> port of the sending process, and may be assumed to be the port to
> which a reply should be addressed in the absence of any other
> information.

768 is silent on allocation and uniqueness; a duplicate allocation is
what breaks the sentence above.

**Code.** `afinet_alloc_ephemeral()` performs a plain
read-modify-write on the global `g_ephemeral_next` —
`uint16_t port = g_ephemeral_next++;` (`sys/net/af_inet.c:188`) — with
no lock and no atomic, even though the comment at
`sys/net/af_inet.c:680-682` cites non-atomicity of that same counter as
the defect NET-07 was meant to fix.
`afinet_alloc_ephemeral_free()` (`:210-216`) then filters candidates
with `afinet_port_taken()`, which acquires and *releases* `afi_lock`
internally (`:874`, `:883`) before returning. Every caller records the
winner only afterwards, outside any lock: `:689-690`, `:721-722`,
`:908`, `:923`, `:1318-1319`, `:1345-1346`. `spinlock_release_irq`
restores IF, so the check-then-record window is preemptible even on UP.
The comment at `sys/net/af_inet.c:205-208` states the requirement
exactly — "Callers must also record the result in s->local_port BEFORE
anyone else can allocate" — and no caller can honour it, because no
critical section spans the pair.

**Failure.** Two threads each `sendto()` on a fresh unbound UDP socket.
Both execute `g_ephemeral_next++` at `sys/net/af_inet.c:188` reading
49152, both find `afinet_port_taken(49152)` false (neither has recorded
yet), and both set `local_port = 49152, bound = 1`. A reply for 49152
scores identically for both (`sys/net/af_inet.c:1585`), the strict
`score > best_score` at `:1672` keeps whichever is nearer `g_afi_head`,
and the other socket never receives its answer — permanently and
silently.

**Fix.** Hold `afi_lock` across allocate-check-record: add an
`afinet_port_taken_locked()` that assumes the lock, and have
`afinet_alloc_ephemeral_free` take `afi_lock` once, advance the
counter, scan, write `s->local_port`/`s->bound`, and release.

### API-19 (low). `shutdown(SHUT_WR)` on a UDP socket is a silent no-op

**Requirement.** POSIX: `write()` after `SHUT_WR` fails `EPIPE` and
raises `SIGPIPE`. RFC 768 is silent on `shutdown`.

**Code.** `afinet_shutdown` handles `SHUT_RD` by setting `s->rd_shut`
(`sys/net/af_inet.c:981`), honoured at `:564`. The `SHUT_WR` arm
(`:985-987`) does nothing at all unless `s->tcp` is set, and
`afi_sock_t` has no `wr_shut` member (the struct at `:115-148` declares
only `rd_shut`, `:136`). `afinet_node_write_body` has no corresponding
check anywhere between `:663` and `:700`. So `shutdown(fd, SHUT_WR)` on
a connected UDP socket returns 0 — the `ENOTCONN` guard at `:979`
passes — and every subsequent `write()` still builds and transmits a
datagram, returning the full byte count. `afinet_sendto_k` shares the
gap.

**Fix.** Add a `wr_shut` flag to `afi_sock_t`, set it in the
`SHUT_WR`/`SHUT_RDWR` arm at `sys/net/af_inet.c:985` for non-TCP
sockets, and check it at the top of `afinet_node_write_body` and
`afinet_sendto_k`, returning `(size_t)-EPIPE` plus `SIGPIPE` unless
`MSG_NOSIGNAL` was passed.

### API-20 (low). `sendto()` and `write()` disagree on the maximum raw datagram

**Requirement.** Internal consistency; RFC 768 imposes no
implementation maximum.

**Code.** For a `SOCK_RAW` socket the two entry points enforce
different limits. `afinet_sendto()` rejects any non-stream payload
larger than `AFI_DATA_MAX` = 1572 at `sys/net/af_inet.c:1393`, before
the RAW arm of `afinet_sendto_k` (`:1305-1309`), which has no check of
its own. `afinet_node_write_body`'s RAW arms (`:702-705` v4,
`:731-734` v6) have no check either and rely on `ip4_output`'s bound of
`NETDEV_MTU_MAX - sizeof(struct iphdr)` = 1580
(`sys/net/inet.c:231`) or `ip6_output`'s
`NETDEV_MTU_MAX - sizeof(struct ip6_hdr)` = 1560
(`sys/net/inet6.c:247`). So the same fd accepts a 1580-byte `write()`
and rejects a 1580-byte `sendto()` with `EMSGSIZE` — and over IPv6 the
inequality reverses, `write()` capping at 1560 below `sendto()`'s 1572.
`AFI_DATA_MAX` is the wrong cap for a raw socket in any case: it
subtracts a UDP header the raw caller supplies itself.

Note in passing that the privilege claim in the `ip4_output` comment at
`sys/net/inet.c:222-226` ("SOCK_RAW creation is not privileged") is
stale — `sys/net/af_inet.c:820` gates it on `euid == 0`.

**Fix.** Give the RAW arms an explicit, family-correct bound
(`NETDEV_MTU_MAX` minus the IP header this stack synthesizes) and apply
the same bound in `afinet_sendto`'s pre-allocation check instead of
`AFI_DATA_MAX`, so both entry points and both families agree. Correct
the stale comment.

## 2d. Resources and accounting

### RES-01 (high). The receive queue is bounded by 32 *datagrams*, not bytes, and `SO_RCVBUF` is inert

**Requirement.** RFC 1122 4.1.4. RFC 768 specifies no receive queueing
or flow control at all.

**Code.** `enqueue()` drops on
`if (s->count >= AFI_RING_LEN) return;  /* drop */`
(`sys/net/af_inet.c:1601`) with `AFI_RING_LEN` = 32 (`:80`). The bound
is a message count, and every slot is a fixed-size `afi_pkt_t`
(`sys/net/af_inet.c:101-113`) whose `data[AFI_DATA_MAX]` is 1572 bytes
(`:99`), so `sizeof(afi_pkt_t)` is 1596 and the ring is 51072 bytes.
Thirty-two 100-byte DNS answers occupy 3200 bytes of payload in a
~50 KiB buffer — 93.7% of the allocation is idle at the instant the
33rd datagram is discarded. There is no per-socket `rcvbuf` field in
`afi_sock_t` (`:115-148`) and no way to raise the limit:
`sys_setsockopt` acts only on `SO_REUSEADDR` and returns 0 for
everything else (`sys/net/af_unix.c:2590-2616`), so
`setsockopt(SO_RCVBUF)` reports success and changes nothing. Linux's
default 212992-byte rmem holds several hundred small datagrams; this
holds exactly 32 at any size.

**Failure.** A libtirpc `svc_dg`/rpcbind client (or a parallel DNS
sweep, or a syslog burst) has 64 requests outstanding on one socket.
The 64 small replies arrive back to back and are drained by a single
NIC RX interrupt before the process is rescheduled. `enqueue()` accepts
32 and takes the `return` at `sys/net/af_inet.c:1601` for the other 32,
having used 6.4 KiB of a 49.9 KiB ring. The application sees 32 replies
and 32 timeouts, calls `setsockopt(SO_RCVBUF, 1 MiB)`, gets 0, and the
next burst behaves identically.

**Fix.** Bound the ring by accumulated bytes (a per-socket
`so_rcvbuf`, default e.g. 64 KiB) rather than by slot count, and store
variable-length datagrams (chained slots, or a byte ring with
`[u16 len][addr][payload]` framing, as `af_unix.c` already does for its
rx ring). Wire `SO_RCVBUF`/`SO_SNDBUF` through `sys_setsockopt` into
that value, clamped to a system maximum.

### RES-02 (medium). Every AF_INET socket eagerly allocates a ~50 KiB 13-page contiguous ring, TCP included

**Requirement.** None — resource handling. Neither RFC 768 nor RFC 1122
4.1.4 imposes a buffering requirement.

**Code.** `afinet_socket()` allocates the ring before it knows the
socket type:
`s->ring = (afi_pkt_t *)kmalloc(sizeof(afi_pkt_t) * AFI_RING_LEN);`
(`sys/net/af_inet.c:839`), above the `if (type == SOCK_STREAM)` branch
at `:843`. At 1596 bytes per slot × 32 that is 51072 bytes, which
exceeds the largest UMA bucket (4096; `sys/vm/vm_kmem.h:7-9`), so every
`socket()` takes `kmalloc`'s large path and calls
`pmm_alloc_contiguous(13)` (`sys/vm/vm_kmem.c:145-147`) — thirteen
*physically contiguous* pages per socket.

The ring is dead weight for `SOCK_STREAM`: `afinet_node_read_body`
dispatches stream reads to `tcp_recv`/`tcp_recv_nb` and returns before
touching it (`sys/net/af_inet.c:566-575`), `afinet_recvfrom` does the
same (`:1436-1447`), and `sock_score` rejects any non-`SOCK_DGRAM`
socket outright (`:1581`) so nothing is ever enqueued into it. The
remote-reachable amplifier is `afinet_accept()`, which repeats the
allocation for each accepted connection (`sys/net/af_inet.c:1019`).
`MAX_FD` is 4096 (`sys/include/sys/proc.h:72`).

**Failure.** A remote peer opens TCP connections to a listening service
as fast as the server accepts them. Each `accept()` calls
`pmm_alloc_contiguous(13)` at `sys/net/af_inet.c:1019` for a ring the
connection will never use. At ~800 concurrent connections the kernel
has committed ~40 MiB of contiguous physical memory to unused rings;
well before the fd ceiling, `pmm_alloc_contiguous(13)` begins failing
on fragmentation even with plenty of free pages, `afinet_accept`
returns `-ENOMEM` (`:1020`), the service stops accepting, and unrelated
subsystems needing multi-page runs fail too. An unprivileged process
looping `socket(AF_INET, SOCK_STREAM, 0)` to `EMFILE` pins
4096 × 51072 ≈ 200 MiB the same way, with no per-process accounting.

**Fix.** Allocate the ring only for `SOCK_DGRAM`/`SOCK_RAW`, and only
on first bind/connect/recv, leaving it NULL for `SOCK_STREAM` — the
read paths already return before dereferencing it and `sock_score`
already rejects stream sockets, so the only other site needing a null
check is the `FIONREAD` arm at `sys/net/af_inet.c:242` (which already
tests `s->count > 0`, and `af_inet.c:166`/`:1044` already test
`if (s->ring)`). Separately, consider MTU-sized buffers allocated on
enqueue rather than one 13-page slab.

### RES-03 (medium). Full-ring drops are entirely unaccounted

**Requirement.** RFC 1213 MIB-II `udpInErrors`/`ipInDiscards` is the
conventional counter set; RFC 768 is silent on overflow accounting.

**Code.** `enqueue()`'s early `return` at `sys/net/af_inet.c:1601`
increments nothing. Grepping `sys/net` for drop accounting finds
exactly one counter pair — `s->dropped++; dev->rx_dropped++;`
(`sys/net/netdev.c:178-179`) for the AF_PACKET subscriber ring — i.e.
the sibling producer in the very same RX path does count its overflow,
so the pattern exists in the tree and was simply not applied to the
socket ring. There is no `udpInErrors`/`udpNoPorts`/`ipInDiscards`
equivalent anywhere, no `/proc/net` node, and `afinet_so_error()`
returns 0 unconditionally for every non-TCP socket
(`sys/net/af_inet.c:1164-1168`), so a UDP socket cannot surface
`ENOBUFS` either. The reader learns nothing out of band: the drop is
drop-newest, so the datagrams it eventually dequeues are intact and in
order, and the ring carries no gap marker.

**Failure.** An NFS-over-UDP or ToolTalk/rpcbind workload loses a reply
purely because the reader was descheduled past 32 queued messages. The
client retransmits on RTO and blames the network; there is no
`netstat -s`-style counter, no ifconfig-visible socket drop, no
`SO_ERROR` and no `/proc` counter, so the loss is invisible to the
application, to the admin and to any post-mortem. On a lossless link
(virtio/loopback) the operator concludes the stack cannot lose
datagrams and misattributes the stall to the peer.

**Fix.** Add per-socket (`so_rcvdrops`) and global
(`udp_in_errors`/`ip_in_discards`) counters at the drop site, mirroring
`sys/net/netdev.c:178-179`; expose them via a `/proc/net/snmp`-style
node.

### RES-04 (medium). `enqueue()` runs a 1572-byte memcpy and a full thread-registry walk with interrupts disabled

**Requirement.** None — resource handling on wire-controlled input.

**Code.** `afinet_deliver_v4` takes `afi_lock` with
`spinlock_acquire_irq` at `sys/net/af_inet.c:1653` and releases it at
`:1681`. `spinlock_acquire_irq` executes `pushf; pop; cli`
(`sys/include/sys/lock.h:67-72`), so interrupts are off for that whole
span, which is entered from hard IRQ context (`rtl_irq` →
`rtl_rx_drain` → `netdev_rx` (`sys/net/netdev.c:158`) →
`inet_eth_input` → `ip4_input` → `udp_input`
(`sys/net/inet.c:351`) → `afinet_deliver_v4`). Inside that span
`enqueue()` does a memcpy of up to `AFI_DATA_MAX` = 1572 bytes
(`sys/net/af_inet.c:1609`) and then `sched_wakeup(s->wait_chan)`
(`:1614`). `sched_wakeup` is not cheap: `sched_wakeup_n` takes
`thread_registry_lock()` — a second IRQ-off spinlock
(`sys/pm/sched.c:123-125`) — walks `FOREACH_THREAD` over every thread
in the system, and then calls `poll_notify()`, which takes
`pollreg_lock`, walks a hash bucket, and calls `sched_wakeup_n` once
per registered poller, i.e. a further full registry walk each. On the
`SOCK_RAW` fan-out path (`for_dgram == 0`, entered from
`sys/net/inet.c:370`), `enqueue` is called once per matching raw socket
(`sys/net/af_inet.c:1666`) inside the same critical section, so *k* raw
sockets multiply both the copy and the walk by *k*.

The same kernel's sibling producer deliberately does the opposite:
`netdev_rx` releases the per-subscriber lock (`sys/net/netdev.c:189`)
and only then calls `sched_wakeup` (`:190`), keeping the registry walk
outside the ring lock.

**Failure.** A TDE desktop session (several hundred threads) with two
tcpdump-style `SOCK_RAW` sockets open. A remote host floods
minimum-size UDP at a bound port. Every datagram makes the NIC ISR hold
`afi_lock` with `cli` in effect while it performs three 1572-byte
copies and three or more complete all-thread walks. The IRQ-off
duration is set by an off-box packet rate multiplied by local thread
count, starving the timer tick, the serial console and disk completion
interrupts.

**Fix.** Collect the `wait_chan` values to wake into a small on-stack
array under `afi_lock`, release the lock, then call `sched_wakeup` on
each — the shape `netdev_rx` already uses. Longer term, give each
socket its own ring lock so the global `afi_lock` covers only the list
walk, and move the payload copy out from under it.

### RES-05 (low). `FIONREAD` reads the ring unlocked and can copy out an uninitialized slot's length

**Requirement.** None; `FIONREAD` is not an RFC 768 concept.

**Code.** `afinet_ioctl`'s `FIONREAD` branch reads `s->count`
(`sys/net/af_inet.c:241`) and then `s->ring[s->tail].len` (`:242`) with
`afi_lock` not held, although both fields are mutated under `afi_lock`
from hard IRQ context (`enqueue`, `sys/net/af_inet.c:1612-1613`) and
from process context (`afinet_recvfrom`, `:1496-1497`;
`afinet_node_read_body`, `:590-591`). `afinet_node_poll:455` has the
same unlocked read of `s->count`. Neither field is volatile or atomic.
The consequence is sharper than a stale answer, because the ring is
never zeroed: `sys/net/af_inet.c:834` memsets only the `afi_sock_t`,
while the ring comes from a bare `kmalloc` at `:839`, and `kmalloc`
(`sys/vm/vm_kmem.c:93-140`) does not zero on either path. A slot never
written holds uninitialized kernel heap in its `len` field.

**Failure.** Two threads share one UDP fd. Thread B enters `FIONREAD`
and reads `s->count == 1` at `sys/net/af_inet.c:241`. Before it reads
line 242, thread A dequeues that last datagram (`:1496-1497`), leaving
`tail` at a slot never written since the `kmalloc`. B reads that slot's
`len` — uninitialized heap — and copies it out as an int in
0..65535. The caller sizes its next `read()`/`malloc()` from it, and 16
bits of uninitialized kernel heap reach userspace on every such race.

**Fix.** Take `afi_lock` around the count/tail/ring read in the
`FIONREAD` branch and around the `s->count` read in
`afinet_node_poll`, as `afinet_recvfrom` does. Independently, zero the
ring at `sys/net/af_inet.c:839` and `:1019` so no path can observe an
unwritten slot.

### RES-06 (low). Checksum-failure and zero-checksum drops leave no counter and no log line

**RFC 1122 4.1.3.4**: *"If a UDP datagram is received with a checksum
that is non-zero and invalid, UDP MUST silently discard the datagram."*
**RFC 8200 8.1**: *"IPv6 receivers must discard UDP packets containing
a zero checksum, and should log the error."*

**Code.** Both discards are correct: `sys/net/udp.c:46-48` (v4 bad
checksum) and `:51` (v6 zero checksum) `return` without any reply. The
gap is that neither leaves a trace. `udp_input()` discards its netdev
handle on its first line — `(void)dev;` (`sys/net/udp.c:21`) — which is
the only object in scope carrying counters (`netdev_t` has
`rx_packets`/`rx_bytes`/`tx_packets`/`tx_bytes`/`rx_dropped`/`tx_dropped`
at `sys/include/sys/netdev.h:57-62`, and other drop sites do use them:
`sys/net/netdev.c:179`, `sys/drivers/net/rtl8139.c:142`). There is no
UDP- or IP-layer statistics block anywhere in `sys/net`. And the file
includes `<kern/console.h>` at `sys/net/udp.c:12` and then uses nothing
from it — the only place the RFC 8200 log call would have gone. A
checksum failure is byte-for-byte indistinguishable from a short
datagram (`sys/net/udp.c:22`), a bad length field (`:25`), an unbound
port, a martian source (`sys/net/inet.c:320-333`), a dropped fragment
(`sys/net/inet.c:308`) — or from no packet having arrived.

**Failure.** A flaky switch port corrupts one byte in ~1 in 10^4
frames. Every affected datagram is dropped at `sys/net/udp.c:48`; an
NFS or DNS client sees intermittent timeouts while `rx_packets` climbs
normally and `rx_dropped` stays zero, because the frame was accepted by
the driver and by IP and only died at UDP. Nothing distinguishes "the
wire is corrupting data" from "the server is not answering". The v6
case is the same for a peer whose checksum offload is misconfigured:
100% of its traffic is discarded with no ICMP, no message and no
counter.

**Fix.** Stop discarding `dev` in `udp_input()` and bump a counter on
each of the four drop paths (short header, bad length, checksum
failure, v6 zero checksum). Either reuse `rx_dropped` or add a
`udp_stats` block (`InDatagrams`/`NoPorts`/`InErrors`/`InCsumErrors`,
the RFC 1213 set) exposed through procfs. Emit a rate-limited log line
naming the source address and destination port at `sys/net/udp.c:51`,
per RFC 8200 8.1.

## 2e. IPv6 and raw sockets (informational)

### U-06 (low). `udp_csum6()` leaves `check == 0` when source selection fails

**RFC 768, "Fields"**:

> If the computed checksum is zero, it is transmitted as all ones (the
> equivalent in one's complement arithmetic). An all zero transmitted
> checksum value means that the transmitter generated no checksum (for
> debugging or for higher level protocols that don't care).

**RFC 8200 8.1** revokes that permission for IPv6.

**Code.** `udp_csum6` zeroes `uh->check` (`sys/net/af_inet.c:636`) and
then returns early if `ip6_source_for()` fails (`:637`), leaving the
field at 0 — the wire encoding for "I computed no checksum". The
trailing comment asserts "unroutable; send fails", but that is not
enforced: both callers (`sys/net/af_inet.c:729` and `:1357`) call
`ip6_output()` unconditionally on the next line. `ip6_source_for`
(`sys/net/inet6.c:228-233`) and `ip6_output`
(`sys/net/inet6.c:236-240`) each perform their own independent
`route_for_v6()` lookup with nothing held between them, and both
callers run in preemptible process context. If a netdev is addressed or
brought up between the two lookups, the first fails and the second
succeeds, and a zero-checksum IPv6 datagram goes out — which this
kernel's own receiver rejects at `sys/net/udp.c:51`. Mitigating:
`afinet_socket` rejects AF_INET6 at `sys/net/af_inet.c:808`, so the
path is currently unreachable from userland — hence low.

**Fix.** Make `udp_csum6` return int and have both callers abort the
send with `-ENETUNREACH` on failure. Better, resolve the route and
source once and pass the chosen netdev/source down to `ip6_output` so
the two lookups cannot disagree (same shape as the U-03 fix).

### I-02 (info). `socket(AF_INET6, …)` is refused, so the entire v6 UDP demux is dead code

`afinet_socket()` refuses every non-AF_INET family up front —
`if (family != AF_INET) return -EAFNOSUPPORT;`
(`sys/net/af_inet.c:808`) — and the comment above it
(`sys/net/af_inet.c:801-807`) acknowledges the choice and says to lift
it "once inet6.c grows real v6 bind/connect". Consequently the v6 arm
of `sock_score` (`:1573` onward, called with AF_INET6 at `:1717`), the
DGRAM enqueue in `afinet_deliver_v6` (`:1735-1739`) and the v6 branch
of `udp_input` (`sys/net/udp.c:50-57`) are all unreachable: every IPv6
UDP datagram is length-checked and checksum-verified and then discarded
with no socket match and no ICMPv6 error. Reported as info because the
deviation is deliberate and commented. When AF_INET6 is enabled,
re-audit `sock_score`'s 16-byte address comparisons and the v6 delivery
arm — neither has ever been exercised — along with U-06, I-03, IP-09
and IP-10.

### I-03 (info). The IPv6 send cap is computed from the IPv4 header length

`AFI_DATA_MAX` subtracts a 20-byte IPv4 header
(`sys/net/af_inet.c:99`) but is used verbatim as the cap on the
AF_INET6 datagram send path — `sys/net/af_inet.c:1350` and `:710` both
do `if (len > AFI_DATA_MAX) return -EMSGSIZE;` before calling
`ip6_output`, which bounds against
`NETDEV_MTU_MAX - sizeof(struct ip6_hdr)` = 1560
(`sys/net/inet6.c:247`), 20 bytes lower. The socket layer admits a v6
payload of 1553..1572 bytes that the IPv6 output path rejects: work
done, then an error returned at a different limit than the socket layer
advertises. Currently unreachable (I-02). Fix: give the v6 path its own
cap (`NETDEV_MTU_MAX - 40 - 8`) and size the per-family `pkt[]` buffers
from it.

### I-04 (info). `setsockopt(IP_ADD_MEMBERSHIP)` returns success while recording nothing

`sys_setsockopt` acts on exactly one option and returns 0 at
`sys/net/af_unix.c:2616` for everything else, so `IP_ADD_MEMBERSHIP`
(35), `IP_DROP_MEMBERSHIP` (36), `IP_MULTICAST_IF` (32),
`IP_MULTICAST_TTL` (33) and `IP_MULTICAST_LOOP` (34) — all defined at
`include/netinet/in.h:148-152` — report success while the join is
discarded. The `optval` is not even copied in, so a malformed
`struct ip_mreq` is also reported as accepted. The deviation *is*
acknowledged: `sys/net/af_unix.c:2596-2597` says "the option itself may
still be a silent no-op", and `include/netinet/in.h:138-141` says
setsockopt "accepts them without erroring so callers compile and run".
Severity is info for that reason, but the consequence is that every
multicast application's error path is dead code and the symptom
surfaces much later as a silent absence of datagrams. Fix: until a
membership table exists, return `-ENOPROTOOPT` for `IPPROTO_IP` options
32-40 and `IPPROTO_IPV6` options 17-21, matching the `getsockopt`
path's own precedent at `sys/net/af_unix.c:2725-2727`.

### I-05 (info). A raw socket may forge any UDP source port

Nothing in the socket layer stops a raw socket originating a datagram
that claims another process's bound port; the only barrier is the
privilege check. The RAW send arms (`sys/net/af_inet.c:1305-1309`,
`:702-705`) hand the caller's buffer straight to `ip4_output` as an
opaque IP payload — nothing parses it as a UDP header, so
`afinet_port_taken` (`:869-885`) and the bind table are bypassed,
`udp_csum4` is never called, and the Length field is never checked
against the IP total length. The mitigations were verified:
`afinet_socket` rejects `SOCK_RAW` for a non-root euid at
`sys/net/af_inet.c:820-821` before allocating anything, and the gate
cannot be bypassed via AF_INET6 (`:808` returns `EAFNOSUPPORT` ahead of
it), via a protocol number (the gate is on type alone, ahead of the
protocol checks at `:825-828`), or via `SOCK_NONBLOCK`/`SOCK_CLOEXEC`
(`sys_socket` masks with `SOCK_TYPE_MASK` before calling,
`sys/net/af_unix.c:1190`/`:1194`). `IP_HDRINCL` is unimplemented and
`ip4_output` always overwrites the source address
(`sys/net/inet.c:248`), so the source *address* cannot be forged — only
the source port. This matches BSD and Linux raw-socket semantics and
the deviation is acknowledged at `sys/net/af_inet.c:811-818`.

### I-06 (info). Raw delivery is not gated on UDP checksum validity

Raw delivery happens from `ip4_input` at `sys/net/inet.c:370`, after
the protocol switch and entirely outside it. `udp_input` verifies the
pseudo-header checksum and returns without delivering when it fails
(`sys/net/udp.c:46-48`), but that return only suppresses the
`for_dgram=1` call at `sys/net/udp.c:49`; the separate `for_dgram=0`
call at `sys/net/inet.c:370` has already been scheduled by `ip4_input`
and reaches every `SOCK_RAW` socket whose protocol matches
(`sys/net/af_inet.c:1660-1667`). The same applies to broadcast
destinations, which the RAW arm does not gate — unlike the TCP arm,
which the IP-03 comment at `sys/net/inet.c:352-362` gates on
`for_bcast`. A raw `IPPROTO_UDP` socket therefore sees datagrams the
UDP layer correctly discarded, with ports parsed out of an unverified
header. This is exactly what BSD `rip_input` and Linux `raw_v4_input`
do, and RFC 1122 4.1.3.4's discard requirement binds the UDP module,
not a raw consumer, so it is recorded as info. If a stricter posture is
wanted, offer a per-socket opt-in (the `IPV6_CHECKSUM` analogue).

---

## What conforms

Not everything here is broken, and several of the things RFC 768
actually specifies are implemented correctly and were verified field by
field during this audit.

- **The datagram header is built correctly.** Source port, destination
  port and Length are written in network byte order with Length =
  `sizeof(udphdr) + payload` on every send path
  (`sys/net/af_inet.c:692-694`, `:724-726`, `:1325-1327`,
  `:1352-1354`), and the minimum of eight is structurally guaranteed.
- **The IPv4 pseudo-header checksum is arithmetically right.**
  `inet_csum_pseudo4()` was checked field by field against an
  independent reference: source address, destination address, zero
  byte, protocol and UDP length, over the UDP header and data with odd
  lengths zero-padded. RFC 768's "if the computed checksum is zero it
  is transmitted as all ones" rule is implemented exactly —
  `uh->check = c ? c : 0xFFFF;` (`sys/net/af_inet.c:630`, `:640`).
- **The IPv6 pseudo-header checksum is right too.**
  `inet_csum_pseudo6()` matches RFC 8200 8.1 bit for bit: 128-bit
  source, 128-bit destination, 32-bit upper-layer length as two 16-bit
  words, and 24 zero bits plus Next Header folded into one addend.
- **The receive-side checksum is verified, and correctly optional over
  IPv4 only.** `sys/net/udp.c:46-48` accepts an all-zero v4 checksum
  (RFC 768) and verifies any non-zero one, discarding silently on
  failure (RFC 1122 4.1.3.4); `sys/net/udp.c:51` rejects a zero
  checksum over IPv6 (RFC 8200 8.1). The UDP-03 comment at
  `sys/net/udp.c:27-42` states the rule correctly.
- **Length-field sanity is checked before use.** `udp_input` rejects a
  short header (`sys/net/udp.c:22`) and a Length below 8 or beyond the
  captured packet (`:25`) before the checksum is computed over it, so
  no wire length can drive a read past the buffer at that layer.
- **The 4-tuple demux is correct for unicast.** `sock_score()`
  (`sys/net/af_inet.c:1571-1597`) keys on family, type, protocol,
  destination port, bound local address and — for a connected socket —
  peer port and peer address, with a best-match rule rather than
  first-match. An unbound socket is not a catch-all, and a
  `SOCK_STREAM` socket can never match a datagram.
- **RAW and DGRAM delivery are properly separated.** The `for_dgram`
  flag (`sys/net/af_inet.c:1665`, `:1671`) ensures RAW sockets are fed
  only from `ip4_input` and DGRAM sockets only from `udp_input`, so no
  datagram is enqueued twice, and RAW correctly fans out to every
  subscriber while DGRAM goes to the single best match.
- **`MSG_PEEK` leaves the datagram queued** (`sys/net/af_inet.c:1495`),
  and `MSG_TRUNC` reports the datagram's true length rather than the
  copied length (`:1524`) — the semantics are right; only the missing
  clamp in `do_recv` (MEM-01) is wrong.
- **Non-blocking is honoured from both sources.** The datagram path
  respects `MSG_DONTWAIT` and the fd's `FNONBLOCK`
  (`sys/net/af_inet.c:1470-1475`, `:1529`), which is what a
  poll-driven UDP client requires.
- **Socket lifetime is safe.** The NET-01/SOCK-03 refcount is taken
  before the first dereference on both the read and write paths
  (`sys/net/af_inet.c:550-552`, `:652-654`) and the delivery walk runs
  entirely under `afi_lock` (`:1653-1681`), so a concurrent `close()`
  cannot free a socket out from under a sleeping reader or an IRQ-time
  enqueue.
- **Martian sources are rejected** (`sys/net/inet.c:318-333`),
  including a 127/8 source off the loopback device, a broadcast or
  multicast source, and a source equal to the link's broadcast address.
- **The IPv4 header is validated before use**: version, header length
  against the captured length, total length against both, and the
  header checksum (`sys/net/inet.c:298-305`).
- **Oversize arithmetic cannot wrap.** `ip4_output`
  (`sys/net/inet.c:231`) and `ip6_output` (`sys/net/inet6.c:247`) bound
  the payload by subtracting from the buffer size rather than adding to
  the length, so the near-2^32 wrap their comments describe is
  genuinely fixed for every `size_t` a caller can pass.
- **`SOCK_RAW` creation is privileged** (`sys/net/af_inet.c:820-821`),
  and the gate cannot be bypassed through family, protocol or type
  flags (see I-05).
- **The payload never crosses the kernel/user boundary unbounced on the
  send path.** `afinet_sendto()` copies the caller's buffer into kernel
  memory before any transmit path touches it
  (`sys/net/af_inet.c:1397-1412`), chunking streams and copying
  datagrams whole.
- **IPv6 multicast has its link-layer mapping**
  (`sys/net/inet6.c:269-273`) — which is what makes its absence on the
  IPv4 side (IP-05, IP-06) an oversight rather than a policy.
