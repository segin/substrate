# dhclient audit — 2026-09

Audit of `sbin/dhclient/dhclient.c` (613 lines) against **RFC 2131**
(R. Droms, March 1997, vendored at `docs/rfc/rfc2131.txt`).  Option
semantics come from RFC 2132, which is cited but not vendored.  Line
numbers refer to the tree at `c052aa4c8`.

**What dhclient is.**  A one-shot program, run synchronously at boot by
`/etc/rc.d/20-network` for each interface that `/etc/network.conf` marks
`dhcp`.  It sends DHCPDISCOVERs over AF_PACKET, takes the first
DHCPOFFER, sends one DHCPREQUEST, waits up to 5 s for a DHCPACK, installs
the address, netmask and router with `SIOCSIF*` ioctls, writes
`/etc/resolv.conf`, and exits.  Nothing runs after that.

RFC 2131 describes a client that keeps running: the Figure 5 state machine
continues past BOUND into RENEWING, REBINDING and lease expiry, and it
restarts from INIT on a DHCPNAK or a failed request.  Most findings below
follow from dhclient implementing only the INIT → SELECTING → REQUESTING →
BOUND path, once, with no recovery.

**13 findings: 1 high, 6 medium, 4 low, 2 info.**  Each was checked
against both the code and the RFC text.  Two candidates were refuted,
and DHC-01 was withdrawn as intended behaviour; all three are listed at
the end, with what does conform.  The remaining IDs keep their original
numbers.

## Findings

| ID | Severity | Summary |
| --- | --- | --- |
| DHC-02 | high | The lease is never tracked: no renewal, no rebinding, and the address is used forever after the lease expires |
| DHC-03 | medium | DHCPREQUEST is sent once and never retransmitted; failure exits instead of restarting |
| DHC-04 | medium | A DHCPNAK is ignored; the client waits out its timeout and gives up |
| DHC-05 | medium | Retransmission is a fixed 2 s × 4, not the randomized exponential backoff §4.1 requires |
| DHC-06 | medium | The address, netmask and router are taken from the DHCPOFFER, not the DHCPACK |
| DHC-07 | medium | No check that the address is free, no DHCPDECLINE, no ARP announcement |
| DHC-08 | medium | Missing options leave the old netmask and gateway in place; install failures still report "bound" |
| DHC-09 | low | Option overload is not interpreted, repeated options are not concatenated, no Maximum Message Size |
| DHC-10 | low | Replies are not validated at the IP/UDP layer |
| DHC-11 | low | OFFER and ACK contents are not validated |
| DHC-12 | low | The domain-name and search-list options are written into resolv.conf unescaped |
| DHC-13 | info | The BROADCAST flag is always set, though the client can receive unicast |
| DHC-14 | info | Deadlines use the wall clock |

---

### DHC-02 (high). The lease is never tracked: no renewal, no rebinding, and the address is used forever after the lease expires

**Requirement.** RFC 2131 §4.4.5: the client keeps timers T1 and T2
(defaults 0.5 and 0.875 of the lease), renews with the leasing server at
T1 and rebinds at T2.  *"If the lease expires before the client receives
a DHCPACK, the client moves to INIT state, MUST immediately stop any
other network processing."*  §3.7: *"If the lease expires before the
client can contact a DHCP server, the client must immediately discontinue
use of the previous network address."*

**Code.** The parameter request list asks for option 51, the lease time
(`:260`), but nothing ever reads it: there is no `find_opt(...,
DHCP_OPT_LEASE, ...)` anywhere.  After installing the address,
`main()` returns (`:608`).  No process remains to renew, rebind or expire
the lease.

**Failure.** A host takes a one-hour lease and stays up for a day.  It
never renews.  After an hour the server considers the address free and
leases it to another machine, while the Substrate host keeps using it.
Result: a duplicate address that the server believes cannot exist.

**Fix.** Keep a process running after BOUND: record the lease start and
duration, sleep to T1 (option 58, or 0.5 × lease) and T2 (option 59, or
0.875 × lease), and send the unicast RENEWING and broadcast REBINDING
DHCPREQUESTs of Table 4 (ciaddr set, no server identifier, no requested
IP).  Retransmit per §4.4.5 (half the remaining time, minimum 60 s).  On
expiry, remove the address and restart from INIT.  Treat lease time
0xffffffff as infinite (§3.3).  Until that exists, the minimum honest
fix is to fail loudly rather than silently holding an expired lease.

### DHC-03 (medium). DHCPREQUEST is sent once and never retransmitted; failure exits instead of restarting

**Requirement.** RFC 2131 §3.1 step 5: *"The client times out and
retransmits the DHCPREQUEST message if the client receives neither a
DHCPACK or a DHCPNAK message. ... If the client receives neither a
DHCPACK or a DHCPNAK message after employing the retransmission
algorithm, the client reverts to INIT state and restarts the
initialization process."*

**Code.** The DHCPREQUEST is built and sent once (`:463-467`).  The client
then waits a fixed 5 s for an ACK (`:470-490`); on timeout it prints
"no ACK received within 5s" and returns 1 (`:610-612`).  The DISCOVER
phase, by contrast, retransmits (`:404-452`).

**Failure.** One lost REQUEST or ACK frame (common just after link-up,
the case the DISCOVER loop's own comment gives as its reason to
retransmit) leaves the machine with no lease for the rest of the boot.
The server has meanwhile committed the binding, so the address is held
for nobody.

**Fix.** Retransmit the DHCPREQUEST with the §4.1 backoff (DHC-05),
e.g. 4 attempts (the RFC's example, about 60 s total).  If that fails,
go back to INIT and start again with a new DISCOVER, rather than exiting.

### DHC-04 (medium). A DHCPNAK is ignored; the client waits out its timeout and gives up

**Requirement.** RFC 2131 §3.1 step 5: *"If the client receives a
DHCPNAK message, the client restarts the configuration process."*
Figure 5: REQUESTING --DHCPNAK--> INIT.

**Code.** The ACK loop discards every message whose type is not DHCPACK
(`:489`); `DHCP_NAK` (6) is not even defined (`:60-63`).

**Failure.** The server NAKs the request (the offered address was
reallocated in the meantime, or two servers raced).  The client ignores
the NAK, waits the full 5 s, and exits without a lease.  An immediate
restart would very likely have succeeded.

**Fix.** Define `DHCP_NAK`.  On a NAK carrying the current xid, return to
INIT and send a new DISCOVER.  Pair this with DHC-03's restart path.

### DHC-05 (medium). Retransmission is a fixed 2 s × 4, not the randomized exponential backoff §4.1 requires

**Requirement.** RFC 2131 §4.1: *"The client MUST adopt a retransmission
strategy that incorporates a randomized exponential backoff algorithm"*.
The example is 4 s ± 1, then 8 s ± 1, doubling up to 64 s.  §4.4.1: *"The
client SHOULD wait a random time between one and ten seconds to
desynchronize the use of DHCP at startup."*

**Code.** `DHCP_DISCOVER_TRIES` 4 and `DHCP_DISCOVER_WAIT` 2.0
(`:56-57`) give four DISCOVERs exactly 2 s apart, 8 s in all, with no
randomization.  There is no startup delay.

**Failure.** After a power failure every host on a segment boots at
once, and their DISCOVERs stay in lockstep.  More directly, 8 s total is
shorter than many real servers take to answer.  A server that ICMP-probes
the address before offering it (§3.1 step 2 SHOULD) often takes over a
second per probe, so a slow server is given up on at boot.

**Fix.** Use a randomized exponential backoff, e.g. 4, 8, 16, 32 s,
each ± 1 s from `arc4random_uniform()`.  Keep a total bound so boot isn't
stalled indefinitely, and consider a short random initial delay.  If an
8 s boot bound is a deliberate choice, document it as a deviation.

### DHC-06 (medium). The address, netmask and router are taken from the DHCPOFFER, not the DHCPACK

**Requirement.** RFC 2131 §3.1 step 4: the selected server *"commits the
binding ... and responds with a DHCPACK message containing the
configuration parameters for the requesting client"*.  *"The 'yiaddr'
field in the DHCPACK messages is filled in with the selected network
address."*  Parameters in the ACK *"SHOULD NOT conflict"* with the offer.
That is a SHOULD on the server, not a guarantee the client can rely on.

**Code.** `offered_ip`, `subnet` and `router` are read from the
DHCPOFFER (`:434-440`) and installed as they are (`:527-529`).  The
DHCPACK is parsed only for DNS, domain and search list (`:503-522`).  Its
`yiaddr` and its options 1 and 3 are ignored.

**Failure.** A server includes the router or netmask only in the ACK, or
changes a parameter between OFFER and ACK.  The host installs the
offer's values: no default route, or the wrong mask.

**Fix.** Configure from the DHCPACK: its `yiaddr`, options 1 and 3, and
every other option.  Use the offer only to choose a server and fill in
the REQUEST.

### DHC-07 (medium). No check that the address is free, no DHCPDECLINE, no ARP announcement

**Requirement.** RFC 2131 §3.1 step 5 and §4.4.1: *"The client SHOULD
perform a check on the suggested address to ensure that the address is
not already in use"*, e.g. an ARP request with sender IP 0.  *"If the
network address appears to be in use, the client MUST send a DHCPDECLINE
message to the server"*, and should wait at least 10 s before
restarting.  *"The client SHOULD broadcast an ARP reply to announce the
client's new IP address."*

**Code.** None of this exists.  The address is installed as soon as the
ACK arrives (`:527`).  No DHCPDECLINE message is defined.

**Failure.** A statically configured host already uses the leased
address, or a server's lease database has been lost.  Substrate installs
the address anyway and runs with a duplicate; the server is never told,
so it keeps handing the address out.

**Fix.** Before installing, send an ARP probe (RFC 5227 form) through the
AF_PACKET socket dhclient already has open, and wait briefly for a reply.
If one arrives, broadcast a DHCPDECLINE (Table 5: requested IP, server
identifier, ciaddr 0), wait 10 s and restart.  Otherwise install the
address and broadcast a gratuitous ARP.

### DHC-08 (medium). Missing options leave the old netmask and gateway in place; install failures still report "bound"

**Requirement.** RFC 2131 §3.5: *"most of the parameters have defaults
defined in the Host Requirements RFCs; if the client receives no
parameters from the server that override the defaults, a client uses
those default values."*

**Code.** The netmask and gateway are set only if the server sent them
(`:528-529`).  Otherwise whatever the interface already had stays.  At
boot that is the address, /24 mask and 10.0.2.2 gateway `inet_init()`
hard-codes for QEMU SLIRP (`sys/net/inet.c`, `inet_init`).  `set_ipv4()`
prints an ioctl failure but carries on (`:166-168`), and `main()` then
prints "bound" and returns 0 (`:599-608`).

**Failure.** On a real network whose server omits option 3, the host
binds its leased address and keeps a default route through 10.0.2.2, an
address that doesn't exist on that network.  If `SIOCSIFADDR` fails, the
host has no address, but rc.d is told dhclient succeeded.

**Fix.** With no option 1, derive the mask from the address class (RFC
1122 3.3.1.1 behaviour) rather than keeping the old one.  With no option
3, remove the existing gateway.  Make any failed ioctl fatal (non-zero
exit, no "bound").

### DHC-09 (low). Option overload is not interpreted, repeated options are not concatenated, no Maximum Message Size

**Requirement.** RFC 2131 §4.1: when the 'option overload' option (52) is
present, the 'file' and then 'sname' fields *"MUST be interpreted"* as
options.  *"The client concatenates the values of multiple instances of
the same option into a single parameter list"*.  §3.5: *"The client
SHOULD include the 'maximum DHCP message size' option"*.

**Code.** `find_opt()` scans only the `options` field and returns the
first instance of a code (`:321-341`).  It never looks at option 52,
`sname` or `file`.  No option 57 is sent.

**Failure.** A server with many options overloads `sname`/`file`, or
splits a long router or DNS list across two instances; the client
silently loses the options stored there.  Without option 57 the server
must assume a 576-octet limit, which is what pushes it into overloading.

**Fix.** Parse `options`, then `file` and `sname` when option 52 says so.
Concatenate repeated instances.  Send option 57 with 1500 minus the IP
and UDP headers (the receive buffer is 1600 octets).

### DHC-10 (low). Replies are not validated at the IP/UDP layer

**Requirement.** None in RFC 2131 directly.  dhclient receives raw frames
on AF_PACKET, bypassing the kernel's IP and UDP checks, so it must make
those checks itself.

**Code.** The receive loops (`:417-429`, `:474-486`) check only the
ethertype, the IP protocol, the UDP destination port and the BOOTP
`op`/`xid`.  They don't check:

- the IP version;
- that the header length is at least 20 (`hlen` can be 0-60);
- that the packet is not a fragment;
- the IP header checksum;
- the UDP checksum;
- the UDP source port (67);
- the UDP length: `bootp_len` comes from the frame length (`:428`), so
  anything after the UDP payload is parsed as options.

**Failure.** Mostly hardening: a corrupted or malformed frame with a
matching xid can be taken as a DHCP reply.  Reads stay inside `rxbuf`
(`find_opt` is bounded by `len`), so this is a correctness issue, not a
memory-safety one.

**Fix.** Require IPv4, `hlen >= 20`, no MF flag and zero fragment offset,
a valid header checksum, source port 67, and a UDP checksum that is
either 0 or valid.  Bound the BOOTP length by the UDP length field.

### DHC-11 (low). OFFER and ACK contents are not validated

**Requirement.** RFC 2131 Table 3 (the server side) makes 'server
identifier' a MUST in DHCPOFFER and DHCPACK, and §3.1 step 3 requires the
client's DHCPREQUEST to carry it.

**Code.** If the OFFER has no option 54, `server_id` stays 0 (`:435-436`)
and the REQUEST carries server identifier 0.0.0.0 (`:251-252`).
`yiaddr` is not checked beyond non-zero (`:404`, `:453`).  The ACK's
server identifier is not compared with the server that was selected.

**Failure.** A malformed or non-conforming offer is followed through.
The client installs 255.255.255.255, a multicast or a loopback address if
offered one, or requests with a server identifier that matches no server.

**Fix.** Discard offers without a 4-octet server identifier and offers
whose `yiaddr` is 0, broadcast, multicast, loopback or class E.  Require
the ACK's server identifier to match the selected server.

### DHC-12 (low). The domain-name and search-list options are written into resolv.conf unescaped

**Requirement.** None in RFC 2131.  RFC 2132 §3.17 defines option 15 as
a domain name, which a newline can't be part of.

**Code.** Option 15 is copied raw (`:508-515`) and printed with `%s` into
`/etc/resolv.conf` (`:543`).  Search-list labels (option 119) are copied
byte by byte (`:574-576`).

**Failure.** A DHCP server, or anyone who can answer DHCP on the segment,
sends option 15 `"example.com\noptions ..."` or a search label containing
a newline, and adds arbitrary lines to resolv.conf.  A rogue server can
already choose the nameservers, so this mainly widens what it can set.

**Fix.** Accept only hostname characters (letters, digits, `-`, `.`) in
option 15 and in each label, and drop the option otherwise.

### DHC-13 (info). The BROADCAST flag is always set, though the client can receive unicast

**Requirement.** RFC 2131 §4.1: *"A client that can receive unicast IP
datagrams before its protocol software has been configured SHOULD clear
the BROADCAST bit to 0."*

**Code.** `flags` is always 0x8000 (`:241`).  dhclient reads on
AF_PACKET, and `netdev_rx()` hands every received frame to AF_PACKET
subscribers whatever its IP destination (`sys/net/netdev.c`), so a
unicast reply to `chaddr`/`yiaddr` would reach it.

**Failure.** Every OFFER and ACK for this host goes to every host on the
segment.  It works, but it's the broadcast load the RFC asks a capable
client to avoid.

**Fix.** Clear the flag.  Only after checking that a unicast reply
really does reach the socket, e.g. that nothing on the input path drops
frames for an IP address the interface doesn't yet have.

### DHC-14 (info). Deadlines use the wall clock

**Code.** `now_sec()` is `gettimeofday()` (`:138-142`), and every OFFER
and ACK deadline is computed from it (`:413`, `:470`).

**Failure.** If the clock is stepped during boot (RTC read, NTP), a wait
is cut short or stretched.  Harmless today; it would matter for DHC-02's
lease timers.

**Fix.** Use `clock_gettime(CLOCK_MONOTONIC)`.

---

## Refuted, withdrawn, and what conforms

- **DHC-01, withdrawn: hostname-derived client identifier.**  Option 61
  is type 0 followed by the hostname (`sbin/dhclient/dhclient.c:281-286`),
  so two hosts still carrying the stock `/etc/hostname` (`agar`) would
  present the same identifier.  This is intended: the hostname is meant
  to be set per site, and the identifier deliberately ties the lease to
  it (RFC 2131 §4.2 explicitly allows a DNS name as the client
  identifier).  Keeping identifiers unique within a subnet is part of
  that site configuration, not dhclient's job.
- **Predictable xid.** `srand(time)` looks like it gives two clients
  booted in the same second the same xid.  It doesn't:
  `lib/c/src/stdlib.c` `srand()` keys ChaCha20 with the seed XORed with a
  per-process `arc4random` secret.  Using `arc4random()` directly would
  still be clearer.
- **`secs` is always 0.**  Table 5 allows "0 or seconds since DHCP
  process started", and §3.1's rule that the REQUEST repeat the
  DISCOVER's `secs` is met trivially.

Conforms (RFC 2131 §3.1, §4.1, §4.4.1, Tables 4 and 5):

- DISCOVER and REQUEST are broadcast to 255.255.255.255 from source
  0.0.0.0, UDP 68 → 67.
- `op`, `htype`, `hlen`, `hops`, `ciaddr`, `yiaddr`, `siaddr`, `giaddr`
  and `chaddr` are as Table 5 specifies, and the magic cookie is present.
- The REQUEST in SELECTING carries the offer's xid, a requested IP equal
  to the offer's `yiaddr`, and a server identifier.
- The same parameter request list is sent in DISCOVER and REQUEST.
- Offers with a different xid are discarded, as is an ACK arriving while
  selecting (§4.4.1).
- The message ends with an 'end' option.

## Checklist

One item per finding, to work through one at a time as with
`docs/ip-audit-2026-09-22.md`.  Regression tests belong in a wire-harness
DHCP server (`tests/lib/net/wire/`), which can send OFFER, ACK and NAK
frames exactly as each case needs.

- [ ] **DHC-02** Track the lease: T1/T2 renew and rebind, expiry drops the address and restarts
- [x] **DHC-03** Retransmit DHCPREQUEST; on exhaustion return to INIT -- 4 REQUESTs on `retx_delay()`, then INIT with a new xid, up to `DHCP_INIT_ATTEMPTS` (3); `request-retx`, `request-restart` in `test_dhclient.py`
- [x] **DHC-04** Handle DHCPNAK: restart from INIT -- a NAK with the current xid ends the REQUEST phase and re-enters INIT at once; `nak` in `test_dhclient.py`
- [x] **DHC-05** Randomized exponential backoff for DISCOVER and REQUEST -- `retx_delay()` (4, 8, 16, 32 s, capped at 64, each +-1 s); DISCOVER uses it now, REQUEST with DHC-03.  Four DISCOVERs in the foreground, about 60 s, by choice (the RFC 3.1 example).  `backoff` in `test_dhclient.py`
- [x] **DHC-06** Configure from the DHCPACK, not the DHCPOFFER -- `install_lease()` reads `yiaddr`, options 1 and 3 from the ACK; the OFFER supplies only the server and the requested address; `ack-config` in `test_dhclient.py`
- [ ] **DHC-07** ARP-probe the address; DHCPDECLINE if it's taken; gratuitous ARP when bound
- [ ] **DHC-08** Defaults for missing netmask/router; fail on ioctl errors
- [ ] **DHC-09** Option overload, concatenation, Maximum Message Size
- [ ] **DHC-10** Validate IP/UDP headers on received frames
- [ ] **DHC-11** Validate server identifier and yiaddr in OFFER/ACK
- [ ] **DHC-12** Sanitize option 15/119 before writing resolv.conf
- [ ] **DHC-13** Clear the BROADCAST flag once unicast reception is confirmed
- [x] **DHC-14** Monotonic clock for deadlines -- `now_sec()` reads `CLOCK_MONOTONIC`; `clock-step` in `tests/lib/net/wire/test_dhclient.py` (DISCOVERs 0.2 s apart before, one retransmission delay after)
