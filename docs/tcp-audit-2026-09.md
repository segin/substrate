# TCP audit — 2026-09

Audit of substrate's TCP implementation against **RFC 793** (J. Postel,
September 1981, vendored at `docs/rfc/rfc793.txt`), with **RFC 1122**,
**RFC 5681**, **RFC 5961**, **RFC 6298** and **RFC 6528** brought in
where they supersede a 793 rule (see *"793 or 9293?"* at the end).

Files audited: `sys/net/tcp.c` (1751 lines — the whole protocol),
`sys/include/netinet/tcp.h` (the wire header), `sys/net/af_inet.c` (the
socket layer: bind/listen/accept/connect/shutdown, the read/write and
recv/send glue), `sys/net/af_unix.c` (which hosts `setsockopt`/
`getsockopt`/`sendmsg`/`recvmsg` for *all* families), `sys/net/inet.c`
(the IPv4 transmit/receive path TCP sits on), `sys/net/loopback.c` and
`sys/net/netdev.c` (the delivery contexts), `sys/arch/i386/idt.c` (the
preemption rule the locking model depends on), `sys/drivers/net/
{e1000,r8168,rtl8139}.c` (what TCP's interrupts-off transmits call
into), and `lib/c/src/socket.c` (the userland `sockatmark`).

**Read the RFC 793 result with the right expectations.** `tcp.c:2`
calls itself "TCP (RFC 793 subset)" and its header comment lists four
known gaps ("Still TBD: send window/cwnd, SACK, RTT-driven RTO, IPv6
transport", `tcp.c:27`). Three of those four are now wrong in the
implementation's favour — a send window and RFC 5681 congestion
control both exist — and the list omits the larger omissions this
audit found: TCP options, the urgent mechanism, the segment
acceptability test, segment trimming, reassembly, SWS avoidance, the
persist timer and the user timeout. **Part 7 is the list a reader
should plan work from.** Everything the file already fixed is in
Part 8, and it is a long list: this is not an unserious stack.

Method: multiple analysis lenses over the wire header, the state
machine, the window/reliability machinery, the urgent mechanism, the
user interface, connection identity and the concurrency model, each
finding re-verified by two or three independent agents working
different questions (does the code really do this / does the RFC
really require this / is it reachable). 21 candidate findings were
refuted during verification and are excluded. Near-duplicates raised
by different lenses are merged. **83 unique defects: 5 critical,
24 high, 45 medium, 6 low, 3 info.** Line numbers refer to the tree at
commit `1b235bf55`.

The highest-severity findings were re-read against the source before
write-up. MEM-01 through MEM-04 and SM-01 all hold as stated, and
MEM-05's instruction ordering was confirmed against `sys/net/tcp.o`.
One sub-claim did **not** hold and has been struck: a finder reported
that the retransmitted-FIN shortcut at `tcp.c:924` also discards the
ACK riding on that segment ("snd_una does not advance, our FIN stays
at unacked_head"). It does not — the ACK block at `tcp.c:827-904`
runs *before* line 924, so `snd_una` advances and the FIN is pruned
normally. The state-transition half of that finding is real and is
SM-01; the ACK half is dropped.

---

## Findings

| ID | Sev | Requirement | Summary | Site |
|----|-----|-------------|---------|------|
| MEM-01 | critical | none (concurrency) | `tcp_lock()` is a bare `cli`, and the loopback RX path runs the whole of `tcp_input` preemptibly with IF=1 — so it excludes nothing | `sys/net/tcp.c:65`, `sys/net/loopback.c:113` |
| MEM-02 | critical | RFC 793 3.9 CLOSED | `tcp_input()` holds the `tcp_find()` result across a preemption while the reaper frees it | `sys/net/tcp.c:1076` |
| MEM-03 | critical | RFC 793 3.9 ABORT | `tcp_send_impl()` sleeps on the send window without pinning the PCB | `sys/net/tcp.c:1464` |
| MEM-04 | critical | RFC 793 3.9 ABORT | `tcp_peek()` sleeps without pinning; the freed 32 KiB ring is copied out to userspace | `sys/net/tcp.c:1585` |
| SM-01 | critical | RFC 793 3.9 fifth check, CLOSING/LAST-ACK | the retransmitted-FIN shortcut returns before the CLOSING and LAST-ACK completion tests, wedging both states forever | `sys/net/tcp.c:924` |
| MEM-05 | high | RFC 793 3.9 fifth check, SYN-RECEIVED | `accept_count` is published before the `accept_q` slot is written; `accept()` can return an uninitialised heap word as a PCB | `sys/net/tcp.c:758` |
| MEM-06 | high | RFC 793 3.9 seventh step | `p->rx_count` is a non-atomic read-modify-write in the RX path; a lost `recv()` decrement corrupts the ring permanently | `sys/net/tcp.c:821` |
| MEM-07 | high | RFC 793 2.6 | `tcp_xmit_queue()` drops the lock, then transmits from a segment an ACK may already have freed | `sys/net/tcp.c:361` |
| MEM-08 | high | RFC 793 3.9 CLOSE, ESTABLISHED | `close()` racing the RX FIN handler overwrites FIN-WAIT-1 with CLOSE-WAIT, into a state no reaper collects | `sys/net/tcp.c:935` |
| SM-02 | high | RFC 793 3.9 first check | the segment-acceptability test does not exist; an unacceptable segment is never answered with an ACK, so keepalives and persist probes go unanswered | `sys/net/tcp.c:776`, `:1030` |
| SM-03 | high | RFC 793 3.9 seventh step | segment text is accepted in CLOSE-WAIT/CLOSING/LAST-ACK/TIME-WAIT, delivering data past EOF and desynchronising RCV.NXT | `sys/net/tcp.c:813` |
| SM-04 | high | RFC 793 3.4 | a RST in SYN-RECEIVED is honoured with no sequence validation — SEG.SEQ is not even passed to the handler | `sys/net/tcp.c:737` |
| SM-05 | high | RFC 793 3.9 first check | no segment trimming: a segment straddling RCV.NXT is discarded whole, and a partial accept desynchronises the connection permanently | `sys/net/tcp.c:813` |
| SM-06 | high | RFC 793 3.9 fourth check | a SYN arriving on a synchronized connection elicits neither RST nor challenge ACK, except in TIME-WAIT | `sys/net/tcp.c:1005` |
| SM-07 | high | RFC 793 3.9 LISTEN, second check | an ACK-bearing segment at a LISTEN socket draws no RST; the comment names a fallback path that does not exist | `sys/net/tcp.c:633` |
| SM-08 | high | RFC 793 3.9 second check | a connection killed by RST or by the retransmit budget is reported to `read()` as a clean end-of-file | `sys/net/tcp.c:1530` |
| WIN-01 | high | RFC 793 3.7 Managing the Window | the zero-window persist probe is charged to the retransmission budget, so a shut window aborts the connection with ETIMEDOUT | `sys/net/tcp.c:1456`, `:497` |
| WIN-02 | high | RFC 793 3.7 Managing the Window | a non-blocking sender never emits the probe, and `POLLOUT` is asserted unconditionally — permanent deadlock plus a 100% CPU spin | `sys/net/tcp.c:1450`, `:1347` |
| WIN-03 | high | RFC 793 3.7 Window Management Suggestions | the window-reopen ACK fires only when one single `read()` drains ≥ MSS, so an incremental reader never reopens a zero window | `sys/net/tcp.c:1520` |
| WIN-04 | high | RFC 793 3.9 RETRANSMISSION TIMEOUT | fast retransmit shares the RTO backoff exponent and the abort counter, and refreshes `sent_tick` — unbounded peer-driven resends, then a shift UB | `sys/net/tcp.c:894`, `:494` |
| HDR-01 | high | RFC 793 3.1 Checksum | the pseudo-header source address is not the address `ip4_output()` stamps; every segment of such a connection ships an invalid checksum | `sys/net/tcp.c:302`, `:615` |
| HDR-02 | high | RFC 793 3.1 Options | TCP options are never parsed; a peer's MSS option is discarded and segments are always sized 1460 | `sys/net/tcp.c:1054` |
| URG-01 | high | RFC 793 3.9 sixth check | the URG bit is never examined on input: no RCV.UP, no notification, urgent data delivered in-band and unmarked | `sys/net/tcp.c:1048` |
| URG-02 | high | RFC 793 3.9 SEND, ESTABLISHED | the send path can never set URG: `urg_ptr` is hardwired to 0 and there is no SND.UP | `sys/net/tcp.c:292` |
| API-01 | high | RFC 793 3.8 OPEN, CLOSED | `connect()` retried after a failed `connect()` reuses the dead TCB and its stale retransmit queue | `sys/net/af_inet.c:1205`, `sys/net/tcp.c:416` |
| API-02 | high | RFC 793 3.8 OPEN, SYN-SENT | `connect()` on a socket still in SYN-SENT restarts the handshake with a new ISS instead of reporting "connection already exists" | `sys/net/af_inet.c:1205` |
| API-03 | high | RFC 793 3.8 OPEN, LISTEN | `connect()` on a listening socket converts the PCB and permanently leaks every queued and half-open child | `sys/net/af_inet.c:1205`, `sys/net/tcp.c:1273` |
| API-04 | high | RFC 793 2.7 | `tcp_bind()` enforces no local-socket uniqueness and cannot fail; two PCBs can hold one 4-tuple | `sys/net/tcp.c:1174`, `sys/net/af_inet.c:869` |
| API-05 | high | RFC 793 2.2 Multiplexing | `tcp_find()`'s LISTEN scan takes the first list hit, with no preference for a specifically-bound listener over a wildcard one | `sys/net/tcp.c:572` |
| MEM-09 | medium | RFC 793 3.9 eighth check, FIN-WAIT-1 | TIME-WAIT is entered before its deadline is armed, and the timer has no non-zero guard, so 2 MSL can be skipped entirely | `sys/net/tcp.c:954`, `:484` |
| MEM-10 | medium | RFC 793 3.9 fifth check, LAST-ACK | the closing state is published, the lock dropped, and only then is the FIN sequenced | `sys/net/tcp.c:1634` |
| MEM-11 | medium | RFC 793 3.8 OPEN (passive) | `tcp_listen()` mutates `accept_q` unlocked, and shrinking the backlog orphans children already queued | `sys/net/tcp.c:1191` |
| MEM-12 | medium | RFC 793 2.7 | `tcp_port_taken()` walks the global PCB list unlocked from preemptible process context | `sys/net/tcp.c:1234` |
| MEM-13 | medium | RFC 793 3.9 LISTEN, third check | `tcp_in_listen()` links the new child into `g_tcp_pcbs` with a bare two-store sequence and no lock | `sys/net/tcp.c:675` |
| SM-09 | medium | RFC 793 3.9 fifth check, SYN-RECEIVED | SYN-RECEIVED never validates SEG.SEQ, discards text and FIN on the third ACK, and sends no RST for an unacceptable one | `sys/net/tcp.c:741`, `:1111` |
| SM-10 | medium | RFC 793 3.9 SYN-SENT, first check | an out-of-range ACK on a non-SYN segment draws no reset — figure 10's half-open discovery cannot happen | `sys/net/tcp.c:730` |
| SM-11 | medium | RFC 793 3.9 SYN-SENT, fourth check | simultaneous open is not implemented: a bare SYN in SYN-SENT is discarded | `sys/net/tcp.c:699` |
| SM-12 | medium | RFC 793 3.9 fifth check | "if the ACK bit is off drop the segment and return" is not implemented: text and FIN are processed from ACK-less segments | `sys/net/tcp.c:931` |
| SM-13 | medium | RFC 793 3.9 fifth vs seventh | processing order is inverted — text is committed to the ring before the ACK acceptability test rejects the segment | `sys/net/tcp.c:838` |
| SM-15 | medium | RFC 793 3.8 CLOSE | a retransmitted, already-consumed data segment arriving after `close()` aborts the connection with a RST | `sys/net/tcp.c:806` |
| WIN-05 | medium | RFC 5681 3.2 (for RFC 793 3.7) | duplicate-ACK detection tests neither SEG.LEN nor SEG.WND, so window updates and the peer's own data segments fire spurious fast retransmits | `sys/net/tcp.c:866` |
| WIN-06 | medium | RFC 793 3.7 Window Management Suggestions | no sender silly-window avoidance and no Nagle: any non-zero usable window produces a segment of exactly that size | `sys/net/tcp.c:1473` |
| WIN-07 | medium | RFC 793 3.7 Window Management Suggestions | no receiver silly-window avoidance: a nearly full ring advertises whatever handful of octets is left | `sys/net/tcp.c:290` |
| WIN-08 | medium | RFC 793 3.9 first check | no out-of-order queue: every acceptable segment above RCV.NXT is discarded, so loss recovery is one segment per round trip | `sys/net/tcp.c:813` |
| WIN-09 | medium | RFC 793 3.9 fifth check | a partially acknowledged segment is retransmitted verbatim from below SND.UNA, so its unacknowledged tail can never be delivered | `sys/net/tcp.c:400` |
| WIN-10 | medium | RFC 793 3.9 seventh step | after `shutdown(SHUT_RD)` the ring is never drained but data is still accepted, pinning the advertised window at zero for the connection's life | `sys/net/tcp.c:1497` |
| WIN-11 | medium | RFC 793 3.9 seventh step | the advertised right edge can move left: `ack_seq` and `window` are sampled non-atomically | `sys/net/tcp.c:288` |
| WIN-13 | medium | RFC 793 3.9 USER TIMEOUT | there is no user timeout, and any state with an empty retransmission queue has no deadline at all | `sys/net/tcp.c:491` |
| HDR-03 | medium | RFC 793 3.1 Maximum Segment Size | the MSS option is never sent: Data Offset is hardcoded to 5 on every segment including SYN and SYN-ACK | `sys/net/tcp.c:289` |
| HDR-04 | medium | RFC 793 3.1 Maximum Segment Size | `TCP_MSS` is a compile-time 1460 never clamped to the route's MTU, and no layer below enforces `dev->mtu` either | `sys/net/tcp.c:283`, `sys/net/inet.c:231` |
| HDR-05 | medium | RFC 793 3.3 ISN Selection | the ISN is raw CSPRNG output with no clock component, so successive incarnations of a 4-tuple do not advance | `sys/net/tcp.c:265` |
| URG-03 | medium | RFC 793 3.8 SEND | `send(..., MSG_OOB)` is accepted, discarded, and reported as success after sending the byte in-band | `sys/net/af_inet.c:1265` |
| URG-04 | medium | RFC 793 3.8 RECEIVE | `recv(..., MSG_OOB)` consumes and returns ordinary in-band stream data as if it were out-of-band | `sys/net/af_inet.c:1443` |
| API-06 | medium | RFC 793 3.9 OPEN, ESTABLISHED | `listen()` on a connected or connecting socket is accepted and silently rewrites the PCB into LISTEN | `sys/net/tcp.c:1180` |
| API-07 | medium | RFC 793 3.9 OPEN, CLOSED (passive) | `listen()` without `bind()` succeeds but creates a listener on port 0 that no SYN can ever match | `sys/net/af_inet.c:957` |
| API-08 | medium | RFC 793 3.9 OPEN, CLOSED | `tcp_connect_start()` is `void`: a failed SYN allocation and ephemeral-port exhaustion are both discarded | `sys/net/tcp.c:1254`, `:1276` |
| API-09 | medium | RFC 793 2.7 | `tcp_port_taken()` rejects a port on the local port alone, ignoring the foreign socket | `sys/net/tcp.c:1233` |
| API-10 | medium | RFC 793 3.9 OPEN, CLOSED | `connect()` accepts an unspecified foreign socket and blocks for the full ~63 s retransmit budget | `sys/net/af_inet.c:1219` |
| API-11 | medium | RFC 793 3.8 ABORT | there is no ABORT primitive, and `setsockopt(SO_LINGER, {1,0})` reports success while doing nothing | `sys/net/af_unix.c:2616` |
| API-12 | medium | RFC 793 3.8 ABORT | `accept()`'s three failure paths CLOSE an already-established child with a FIN instead of aborting it with a RST | `sys/net/af_inet.c:1013` |
| API-13 | medium | RFC 793 3.9 SEND, FIN-WAIT-1 … | SEND in the closing states returns ENOTCONN ("connection does not exist"), `POLLOUT` stays asserted, and no SIGPIPE is ever raised | `sys/net/tcp.c:1424`, `:1347` |
| API-14 | medium | RFC 793 3.9 SEND, SYN-SENT | a `send()` issued while the handshake is outstanding fails with ENOTCONN instead of being queued | `sys/net/tcp.c:1419` |
| API-15 | medium | RFC 793 3.9 CLOSE, SYN-SENT | `shutdown(SHUT_WR)` on a connecting socket is silently discarded; no FIN is ever sent | `sys/net/tcp.c:1717` |
| API-16 | medium | POSIX `send(2)` (for RFC 793 3.8 SEND) | `send()`/`sendto()` on a stream socket ignores `O_NONBLOCK` and `MSG_DONTWAIT` and parks the caller | `sys/net/af_inet.c:1275` |
| API-17 | medium | RFC 793 3.9 RECEIVE, LISTEN | `read(2)` on a listening TCP socket blocks forever instead of reporting ENOTCONN — `recv(2)` gets it right | `sys/net/af_inet.c:566` |
| API-18 | medium | RFC 793 3.9 OPEN, CLOSED | no authority check on the local socket in `bind()`: any process can claim a well-known port | `sys/net/af_inet.c:887` |
| API-19 | medium | RFC 793 3.5 Case 1/2 | a failed FIN allocation in the close path leaves the PCB in FIN-WAIT-1 or LAST-ACK with no FIN on the wire and no reaper | `sys/net/tcp.c:1631` |
| API-20 | medium | RFC 793 3.9 CLOSE, SYN-RECEIVED | CLOSE in SYN-RECEIVED sends neither FIN nor RST, stranding a peer that already completed its handshake | `sys/net/tcp.c:1679` |
| API-21 | medium | RFC 793 3.9 second check, SYN-RECEIVED | a SYN followed by a RST leaves a child PCB plus a 32 KiB ring that the backlog does not count and the reaper collects only a tick later | `sys/net/tcp.c:648` |
| RES-01 | medium | none (latency) | `tcp_timer_tick()` performs an unbounded number of full transmits with interrupts disabled, starving the RX path it exists to exclude | `sys/net/tcp.c:457` |
| RES-02 | medium | none (latency) | `tcp_close()`'s LISTEN arm emits a RST per orphaned child inline under the same interrupts-off lock, from a `close(2)` | `sys/net/tcp.c:1670` |
| RES-03 | medium | none (latency) | every arriving segment costs two full walks of the global PCB list in hard IRQ; a SYN costs three | `sys/net/tcp.c:564` |
| RES-04 | medium | none (latency) | e1000 and r8168 TX-descriptor spins are a flat 1,000,000 iterations with no interrupts-disabled adaptation | `sys/drivers/net/e1000.c:238` |
| RES-05 | medium | none (resources) | every `SOCK_STREAM` socket allocates a ~50 KiB datagram ring it can never use | `sys/net/af_inet.c:839` |
| SM-14 | low | RFC 793 3.9 eighth check | an arriving FIN signals no user in FIN-WAIT-1/FIN-WAIT-2, and the TIME-WAIT expiry wakes nobody | `sys/net/tcp.c:960` |
| WIN-12 | low | RFC 793 3.9 fifth check | SND.WL1/SND.WL2 do not exist, so a reordered same-ACK segment can install a stale send window | `sys/net/tcp.c:1095` |
| URG-05 | low | RFC 793 3.8 RECEIVE | `sockatmark()` returns 0 for every descriptor and issues no syscall; `SIOCATMARK` is ENOTTY | `lib/c/src/socket.c:138` |
| API-22 | low | RFC 793 3.8 OPEN, SYN-SENT | `s->connected` is set on the `-EINPROGRESS` path, so a socket in SYN-SENT reports EISCONN and hands out a peer name | `sys/net/af_inet.c:1242` |
| API-23 | low | RFC 793 3.9 CLOSE, SYN-SENT | CLOSE in SYN-SENT returns no "error: closing" and wakes nobody; a concurrent `connect()` then reports ECONNREFUSED | `sys/net/tcp.c:1683` |
| SEC-01 | low | RFC 793 3.6 | precedence and security/compartment are absent end to end, and the IP layer discards the TOS byte and IP options before TCP sees them | `sys/net/tcp.c:143`, `sys/net/inet.c:344` |
| WIN-14 | info | RFC 793 3.7 Retransmission Timeout | the RTO is a fixed constant with backoff, never derived from a measured round-trip time (acknowledged at `tcp.c:85`) | `sys/net/tcp.c:88` |
| URG-06 | info | RFC 793 3.1 Urgent Pointer | neither the 793 nor the RFC 1122 urgent-pointer convention is expressed, while the wire struct makes the stack look URG-capable | `sys/include/netinet/tcp.h:17` |
| API-24 | info | RFC 793 3.9 SEND, LISTEN | SEND in LISTEN does not convert the passive open to an active one — POSIX supersedes; recorded for completeness | `sys/net/tcp.c:1419` |

---

# Part 0 — concurrency and memory safety

Thirteen findings. MEM-01 is the root cause of six of the others; fix
it and MEM-05, MEM-06, MEM-08, MEM-09, MEM-11, MEM-12 and MEM-13 go
with it. MEM-02, MEM-03, MEM-04 and MEM-07 are separate holes that
survive a correct lock.

### MEM-01 (critical). `tcp_lock()` is a bare `cli`, and the loopback RX path runs the whole of `tcp_input` preemptibly with interrupts on

**Requirement.** None from RFC 793 directly; 3.9's SEGMENT ARRIVES is
specified as an ordered sequence of checks against one TCB whose
variables are read and rewritten as a unit — *"Segments are processed
in sequence. Initial tests on arrival are used to discard old
duplicates, but further processing is done in SEG.SEQ order"*
(`docs/rfc/rfc793.txt:4265-4268`). That is unachievable when one
segment's update interleaves with another's and with concurrent user
calls.

**Code.** The entire synchronisation design rests on one premise,
stated verbatim at `sys/net/tcp.c:59-63`:

> Substrate's RX path always runs with IRQs already disabled (it's an
> ISR), so on a uniprocessor it's enough for the process-context
> critical sections to disable local IRQs for the duration: that makes
> them atomic against RX.

`tcp_lock()`/`tcp_unlock()` are therefore nothing but
`intr_disable()`/`intr_restore()` (`sys/net/tcp.c:65-66`).

The premise is false for loopback, which is the dominant TCP path on
this system (X11, DCOP, libtirpc/`rpc.ttdbserver`, every 127.0.0.1
client). `lo_thread()` calls `intr_restore(f)` at
`sys/net/loopback.c:113` **before** `netdev_rx()` at
`sys/net/loopback.c:115`; `netdev_rx()` calls `inet_eth_input()` at
`sys/net/netdev.c:167` before taking any lock; and `ip4_input()`
dispatches to `tcp_input()` at `sys/net/inet.c:364`. So the whole
`tcp_input` → `tcp_in_{listen,syn_sent,syn_received,established}`
chain runs in an ordinary kthread with IF=1.

That context is fully preemptible. `sys/arch/i386/idt.c:272-281`
calls `sched_yield()` from the timer IRQ whenever the interrupted
context is kernel mode with `preempt_count_get() == 0` and
`THREAD_F_NO_PREEMPT` is clear. Nothing in the `tcp_input` call chain
raises `preempt_count`, and `grep -rn THREAD_F_NO_PREEMPT sys/` shows
that flag is set only by the exec path (`sys/exec/exec.c:104`) — never
for a kthread.

Consequently every process-context `tcp_lock()` region
(`tcp_recv_nb` `:1496`, `tcp_accept` `:1380`, `tcp_close` `:1621`,
`tcp_xmit_queue` `:347`, `tcp_alloc` `:1144`, `tcp_free` `:1157`) and
the timer kthread's entire walk (`:457-518`) excludes only hardware
interrupts. None of them excludes a loopback kthread that was
descheduled halfway through a TCB update.

It is not confined to loopback-carried connections either. Because
the lo kthread runs `tcp_input` with IF=1, a hardware NIC IRQ
(`sys/drivers/net/e1000.c:184`, `rtl8139.c:160`, `virtio_net.c:210`
all call `netdev_rx` from their hard-IRQ handlers) nests a *second*
complete `tcp_input` inside the first. Two concurrent RX flows over
one `g_tcp_pcbs` list is exactly what the design declares impossible,
and it corrupts PCBs that never carry a byte of loopback traffic.

**Failure.** A process opens a connection to 127.0.0.1. The lo kthread
is executing `tcp_in_established()` for a segment on it, holding no
lock with IF=1. The 250 Hz timer IRQ fires, `preempt_count` is 0, and
`sched_yield()` deschedules it with the TCB half-updated. The
application thread runs, enters `tcp_recv_nb()`/`tcp_close()`/
`tcp_send()`, takes `tcp_lock()` — which masks IRQs and has no effect
whatever on the descheduled kthread — and reads and writes the same
TCB fields. The kthread is later rescheduled and completes its
half-finished update on top of the socket call's writes. MEM-02,
MEM-05, MEM-06, MEM-08 and MEM-09 are the concrete instances.

**Fix.** Replace `tcp_lock()`/`tcp_unlock()` with a real IRQ-safe
spinlock (`spinlock_acquire_irq`/`spinlock_release_irq`, which
`sys/net/netdev.c:102-110` and `sys/net/af_inet.c` already use for
this reason) and take it around the whole of `tcp_input()` as well as
every socket-layer and timer critical section. Alternatively keep
interrupts disabled across the `netdev_rx()` call in `lo_thread()` —
but that is the weaker fix, since it leaves the nested-NIC-IRQ case
open. Correct the comment at `sys/net/tcp.c:49-66` either way: it is
the source of the false premise every other comment in the file
reasons from.

### MEM-02 (critical). `tcp_input()` holds the `tcp_find()` result across a preemption while the reaper frees it

**Requirement.** RFC 793 3.9, CLOSED state: *"If the state is CLOSED
(i.e., TCB does not exist) then all data in the incoming segment is
discarded"* (`docs/rfc/rfc793.txt:3985-3987`).

**Code.** `tcp_input()` resolves the PCB at `sys/net/tcp.c:1076` and
hands that raw pointer to a per-state handler
(`sys/net/tcp.c:1103-1124`). The comment at `sys/net/tcp.c:411-415`
claims this is safe — *"Keeping the free out of the RX path means
tcp_find() (which skips CLOSED) can never hand back a pointer that is
about to be freed underneath the caller"* — an argument that holds
only if the RX path is atomic between the `tcp_find()` and the last
dereference. On the loopback kthread it is not (MEM-01).

The reaper is the timer kthread: a PCB in `TCP_CLOSED` with
`p->detached` and `p->holds == 0` is freed at `sys/net/tcp.c:465`, and
a never-accepted child at `sys/net/tcp.c:473-475`. `tcp_free()`
(`sys/net/tcp.c:1152-1172`) `kfree()`s the 32 KiB `rxbuf` at `:1169`,
the `accept_q` at `:1170` and the PCB at `:1171`. The TCP-01 hold
mechanism does not cover this: `tcp_hold()` is taken only by
`tcp_connect` (`:1281`), `tcp_accept` (`:1375`) and `tcp_recv`
(`:1542`). `tcp_input()` never takes one, although it is now exactly
the kind of context the hold exists to protect.

Two transitions to CLOSED+detached are driven purely from process
context: `tcp_close()`'s SYN_SENT/SYN_RECEIVED arm (`:1678-1685`,
which sets `detached = 1` at `:1625` and `state = TCP_CLOSED` at
`:1683`), and `tcp_close()`'s LISTEN arm (`:1662-1672`), which sets
`q->detached = 1` at `:1665` and `tcp_kill_pcb(q)` at `:1671` for
every child.

**Failure.** A server listens on 127.0.0.1. The lo kthread enters
`tcp_input()`, `tcp_find()` returns child PCB `c` in SYN_RECEIVED at
`:1076`, and the kthread is preempted before `tcp_in_syn_received()`
runs. The application closes the listening fd: `tcp_close()` takes the
LISTEN arm, sets `c->parent = NULL`, `c->detached = 1`, and
`tcp_kill_pcb(c)` → CLOSED. Within 125 ms the timer kthread calls
`tcp_free(c)`. The lo kthread resumes and executes
`tcp_in_syn_received(c, ack, flags)` against freed memory: it writes
`c->state`, `c->snd_una`, `c->cwnd` into the freed slab and calls
`tcp_unacked_prune(c, ack)`, which dereferences and `kfree()`s
`c->unacked_head` read out of recycled heap contents. `kmalloc` does
not zero (`sys/vm/vm_kmem.c:111` passes only `M_NOWAIT`, and the kmem
zones are `UMA_ZONE_MALLOC`, not `UMA_ZONE_ZINIT`), so that is live
heap data, not zeroes.

**Fix.** Hold the netstack lock across the whole of `tcp_input()`
(MEM-01's fix), or make `tcp_input()` take a `tcp_hold()` on the PCB
immediately after `tcp_find()` and `tcp_unhold()` before returning, so
the reaper's `p->holds == 0` test at `:465` and `:473` actually covers
the RX path.

### MEM-03 (critical). `tcp_send_impl()` sleeps on the send window without pinning the PCB

**Requirement.** RFC 793 3.9, second check the RST bit, ESTABLISHED /
FIN-WAIT-1 / FIN-WAIT-2 / CLOSE-WAIT: *"If the RST bit is set then,
any outstanding RECEIVEs and SEND should receive 'reset' responses.
All segment queues should be flushed. Users should also receive an
unsolicited general 'connection reset' signal. Enter the CLOSED state,
delete the TCB, and return."*

**Code.** TCP-01 introduced `tcp_hold()`/`tcp_unhold()`
(`sys/net/tcp.c:231-243`) precisely because the retransmit kthread is
the sole reaper and frees a detached PCB plus its 32 KiB `rxbuf` the
moment it reaches CLOSED with `holds == 0` (`sys/net/tcp.c:465`).
`tcp_connect` (`:1281`), `tcp_accept` (`:1375`) and `tcp_recv`
(`:1542`) all take the hold. `tcp_send_impl()` does not — `grep -n
tcp_hold sys/net/tcp.c` returns only those three call sites — yet it
blocks in `sched_sleep_until(p->send_chan, …)` at `sys/net/tcp.c:1464`
and, on every wake, re-dereferences `p->state`, `p->snd_nxt`,
`p->snd_una`, `p->cwnd` and `p->snd_wnd` (`:1419-1442`) and may call
`tcp_xmit_queue()`, which writes `p->snd_nxt` and links into
`p->unacked_tail`. `afinet_node_close()` (`sys/net/af_inet.c:744`)
calls `tcp_close()` unconditionally even while a blocked writer holds
a reference on the *socket*; the socket refcount does not protect the
PCB, and `tcp_close()` sets `p->detached = 1` (`sys/net/tcp.c:1625`).

**Failure.** Thread A does `write(fd, buf, 1 MiB)`; the peer's window
fills and A parks at `:1464`. Thread B calls `close(fd)` →
`tcp_close()`: `detached = 1`, ESTABLISHED → FIN_WAIT_1, FIN queued.
The peer answers with a RST (`tcp_kill_pcb` at `:797`) or the FIN
RTOs out (`:498`); state becomes CLOSED. On the next tick (≤ 125 ms)
`:465` sees `detached && holds == 0` and calls `tcp_free(p)`, which
`kfree()`s the 32 KiB ring and the PCB. A's `TCP_SLEEP_POLL` deadline
fires ≤ 64 ms later, it resumes at `:1466` and reads `p->state` /
`p->snd_nxt` / `p->snd_una` from the freed slab, then writes
`p->snd_nxt` and `p->unacked_tail` through `tcp_xmit_queue`. The RST
that drives the third step is under remote control.

**Fix.** Bracket `tcp_send_impl()` with `tcp_hold(p)`/`tcp_unhold(p)`
exactly as `tcp_recv` does, and have the woken caller return the PCB's
pending `so_error` rather than re-entering the loop.

### MEM-04 (critical). `tcp_peek()` sleeps without pinning, and copies the freed ring out to userspace

**Requirement.** RFC 793 3.9: *"Enter the CLOSED state, delete the
TCB, and return."*

**Code.** `tcp_peek()` (`sys/net/tcp.c:1585-1595`) is a line-for-line
twin of `tcp_recv()` (`:1540`) except that it omits the
`tcp_hold()`/`tcp_unhold()` pair `tcp_recv` takes at `:1542`/`:1554`.
It loops calling `tcp_peek_nb()` and sleeps at `:1590`, then re-enters
`tcp_peek_nb()`, which dereferences `p->shut_rd`, `p->rx_count`,
`p->state`, `p->rx_tail` and reads `p->rxbuf[tail]` for up to
`rx_count` bytes (`:1562-1572`). It is reachable from userspace via
`recv(fd, buf, len, MSG_PEEK)` on a blocking stream socket —
`sys/net/af_inet.c:1443-1445` routes `MSG_PEEK` straight to
`tcp_peek()`.

This is strictly worse than MEM-03, because `tcp_free()` `kfree()`s
`rxbuf` before the PCB (`sys/net/tcp.c:1169-1171`): the post-free read
of `p->rxbuf` follows a dangling pointer into a recycled 32 KiB slab
and those bytes are then copied out to the calling process. That is a
kernel heap disclosure on top of the use-after-free.

**Failure.** Thread A: `recv(fd, buf, 4096, MSG_PEEK)` on a socket
with an empty ring → `-EAGAIN` → sleeps at `:1590`. Thread B closes
the shared fd. The peer RSTs; `tcp_kill_pcb` → CLOSED; the timer's
`:465` (`detached && holds == 0`, because `tcp_peek` never took a
hold) calls `tcp_free`: `kfree(p->rxbuf, 32768)` then `kfree(p)`.
A wakes on its 64 ms poll deadline, calls `tcp_peek_nb(p, …)` again,
reads a non-zero `p->rx_count` out of the freed PCB and memcpy-loops
`p->rxbuf[tail]` — whatever the allocator has since put in that 32 KiB
block — into the user's buffer.

**Fix.** Add `tcp_hold(p)` at the top of `tcp_peek()` and
`tcp_unhold(p)` on every return path, mirroring `tcp_recv()`.

### MEM-05 (high). `accept_count` is published before the `accept_q` slot is written

**Requirement.** RFC 793 3.9, fifth check the ACK field, SYN-RECEIVED:
*"If SND.UNA =< SEG.ACK =< SND.NXT then enter ESTABLISHED state and
continue processing."*

**Code.** `tcp_in_syn_received()` publishes the child to the listener
at `sys/net/tcp.c:755-761` with `par->accept_q[par->accept_count++] =
p;`, taking no lock. `tcp_accept()` dequeues under `tcp_lock` at
`sys/net/tcp.c:1380-1389`, and its comment states the premise MEM-01
disproves: *"accept_q / accept_count are appended by
tcp_in_syn_received in IRQ context — dequeue with IRQs off so the
shift-down can't race an enqueue"* (`:1377-1379`).

The compiled form makes the ordering concrete. In `sys/net/tcp.o`,
`tcp_in_syn_received` emits:

```
ecc:  mov 0x64(%eax),%ebx        ; ebx = par->accept_q
ed2:  mov 0x6c(%eax),%eax        ; eax = par->accept_count
ed5:  lea 0x1(%eax),%ecx
edb:  mov %ecx,0x6c(%edx)        ; par->accept_count = count+1  (FIRST)
ede:  shl $0x2,%eax
ee1:  lea (%ebx,%eax,1),%edx
ee7:  mov %eax,(%edx)            ; accept_q[count] = p          (SECOND)
```

The count is stored before the slot. A preemption between `edb` and
`ee7` leaves the listener advertising a queued connection whose array
slot has never been written. The array is never zeroed: `tcp_listen()`
allocates it with a bare `kmalloc` at `sys/net/tcp.c:1209` and the
growth path at `:1199` copies only `accept_count` entries, and
`kmalloc` does not zero. The slot holds recycled heap bytes.
`tcp_accept()` then takes that value as a `tcp_pcb_t *`, writes
through it (`c->parent = NULL;` at `:1386`) and returns it;
`afinet_accept()` stores it as `c->tcp` and dereferences it at
`sys/net/af_inet.c:1035`. `tcp_close()`'s guard at
`sys/net/tcp.c:1612` only rejects pointers below `0xC0000000`, so any
recycled kernel-heap word sails through.

The converse interleaving loses the child instead: if `tcp_accept()`'s
shift-down and decrement land between `ed2` and `edb`, the RX side
writes back the pre-decrement count, leaving a duplicated pointer at
the tail that a later `accept()` hands out a second time — two
`afi_sock_t` owning one PCB, hence a double `tcp_close()`/`tcp_free()`.

**Fix.** Write the slot before publishing the index
(`par->accept_q[par->accept_count] = p; par->accept_count++;`) as a
minimum, and hold the netstack lock across the append so it cannot
interleave with `tcp_accept()`'s shift-down or with `tcp_free()`'s
`kfree` of `accept_q` (which happens outside the lock, at `:1170`).
Zero the array at `:1199` and `:1209` so a torn publish cannot yield a
wild pointer.

### MEM-06 (high). `p->rx_count` is a non-atomic read-modify-write in the RX path

**Requirement.** RFC 793 3.9, seventh step: *"When the TCP takes
responsibility for delivering the data to the user it must also
acknowledge the receipt of the data."* The same step also fixes the
window invariant: *"Once the TCP takes responsibility for the data it
advances RCV.NXT over the data accepted, and adjusts RCV.WND as
apporopriate to the current buffer availability. The total of RCV.NXT
and RCV.WND should not be reduced."*

**Code.** `tcp_in_established()` updates the ring counters at
`sys/net/tcp.c:813-823` with no lock; `tcp_recv_nb()` drains under
`tcp_lock` at `:1496-1526`. `rx_head` is owned by the writer and
`rx_tail` by the reader, so those are a safe SPSC pair. `rx_count` is
not: both sides read-modify-write it. The kernel is built with no
optimisation level (`Makefile.inc:53`/`:71`), so GCC emits a
three-instruction sequence, not a single interrupt-atomic `addl`. From
`sys/net/tcp.o`:

```
107f: mov 0x8(%ebp),%eax
1082: mov 0x3c(%eax),%edx     ; load p->rx_count
1085: mov -0xc(%ebp),%eax     ; accept_n
1088: add %eax,%edx
108a: mov 0x8(%ebp),%eax
108d: mov %edx,0x3c(%eax)     ; store p->rx_count
```

A preemption between `1082` and `108d` discards any `rx_count` update
the reader makes in the interval. `tcp_recv_nb`'s own
`p->rx_count -= n` (`:1509`) runs entirely under `cli`, so it is never
the one torn — it is always the one lost. `rx_count` therefore only
ever drifts upward, by exactly the number of bytes the reader drained
during the window.

**Failure.** A connection over 127.0.0.1 has 100 bytes pending and a
reader blocked in `recv()`. A 200-byte segment arrives; the lo kthread
copies it in, advances `rx_head`, loads `rx_count == 100` into `edx`,
and is preempted before the store. The reader drains 100 bytes under
`tcp_lock` and stores `rx_count = 0`. The kthread resumes and stores
`rx_count = 300`. Only 200 bytes are actually pending, so the next
`recv()` walks `rx_tail` past `rx_head` and re-delivers 100 bytes the
application has already consumed — duplicated stream data for a
segment the stack has already acknowledged.

Repeated drift pushes `rx_count` above `TCP_RING_LEN`, at which point
three unsigned expressions underflow to ~2^32.
`TCP_RING_LEN - p->rx_count` at `:770` makes
`tcp_seq_in_rcv_window()` accept essentially any sequence number,
defeating the RFC 5961 RST and SYN validation built on it at `:791`
and `:1005`. The same expression at `:815-816` stops clamping
`accept_n`, so arriving data overwrites unread ring contents while
`rcv_nxt` advances over it and it is still ACKed — silent,
acknowledged data loss. And `(uint16_t)(TCP_RING_LEN - p->rx_count)`
at `:290` advertises a garbage window.

**Fix.** Perform the ring write and the counter update under the same
netstack lock the reader takes (MEM-01's fix). `rx_count` must not be
read-modify-written from two contexts that do not exclude each other.

### MEM-07 (high). `tcp_xmit_queue()` transmits from a segment an ACK may already have freed

**Requirement.** RFC 793 2.6: *"When the TCP transmits a segment
containing data, it puts a copy on a retransmission queue and starts a
timer; when the acknowledgment for that data is received, the segment
is deleted from the queue."* And 3.9, fifth check, ESTABLISHED: *"If
the ACK acks something not yet sent (SEG.ACK > SND.NXT) then send an
ACK, drop the segment, and return."*

**Code.** `tcp_xmit_queue()` assigns `s->seq`, advances `snd_nxt` and
links `s` onto the unacked FIFO under `tcp_lock()`
(`sys/net/tcp.c:347-354`), releases the lock at `:355`, and only then
dereferences the segment at `:361`
(`tcp_xmit_raw(p, s->seq, flags, s->data, dlen)`), which memcpy()s
`s->data` into the wire buffer at `:293`. `tcp_send_impl()` runs in
process context with interrupts enabled and holds no other lock.

Because `snd_nxt` is advanced at `:349-351` *before* the transmit, the
acceptability test at `:838` (`if ((int32_t)(ack - p->snd_nxt) > 0)`)
already regards an ACK covering this un-transmitted segment as
acceptable — the opposite of what the RFC requires, since SND.NXT is
"send next" and an ACK beyond what has actually been sent must be
answered and dropped. `SEG.ACK == SND.NXT` passes, `snd_una` jumps to
it at `:847`, and `tcp_unacked_prune()` at `:848` unlinks and
`kfree()`s the segment (`:370-380`) while the producing thread still
holds `s`.

Two triggers. From the wire: a hostile or optimistically-ACKing peer
ACKs slightly beyond what it has received, and a hard NIC IRQ lands
between `:355` and `:361`. Over loopback: the timer kthread transmits
the segment first (`tcp_retx_head` at `:516` after the RTO) while the
producer sits preempted in that window, the peer legitimately ACKs it,
and the lo kthread prunes and frees it underneath the producer.

**Failure.** A read-use-after-free whose contents go on the network:
`tcp_xmit_raw` memcpy()s `dlen` bytes out of the freed slab into the
outbound segment, computes a valid TCP checksum over it (`:303`) and
transmits it. Since `kmalloc` neither zeroes nor quarantines, those
bytes are whatever now occupies the block — a kernel heap disclosure
to the peer, plus silent stream corruption.

One related case does **not** break, and is worth recording: the
NET-02 inline retransmit at `:516` is genuinely safe, because
`tcp_timer_tick()` holds `cli` across the entire walk (`:457-518`) and
nothing in `tcp_retx_head` → `tcp_xmit_raw` → `ip4_output` → `lo_xmit`
re-enables interrupts.

**Fix.** Copy `s->seq`/`s->flags`/`dlen` and the payload into locals
(or build the wire buffer) before releasing the lock, or assign the
sequence number under the lock and link onto the queue only after the
transmit. Separately, the rejection test at `:838` should compare
against the highest sequence actually transmitted, not against the
pre-advanced `snd_nxt`.

### MEM-08 (high). `close()` racing the RX FIN handler overwrites FIN-WAIT-1 with CLOSE-WAIT

**Requirement.** RFC 793 3.9 CLOSE Call, ESTABLISHED: *"Queue this
until all preceding SENDs have been segmentized, then form a FIN
segment and send it. In any case, enter FIN-WAIT-1 state."*

**Code.** Both `tcp_close()` and the FIN handler read `p->state`,
decide a transition, and write it back. `tcp_close()` does so under
`tcp_lock` (reads `st` at `sys/net/tcp.c:1626`, writes
`TCP_FIN_WAIT_1` at `:1629`), so it cannot itself be split. The RX
side can: `tcp_in_established()` evaluates `switch (p->state)` at
`:933` and writes `p->state = TCP_CLOSE_WAIT` at `:935` with nothing
held.

When the RX flow is split there, `tcp_close()` runs in the gap, sees
ESTABLISHED, transitions to FIN_WAIT_1 and queues a real FIN via
`tcp_xmit_queue()` at `:1631` — so the FIN is on the wire and
`snd_nxt` has consumed its sequence number. The RX flow then resumes
and stores CLOSE_WAIT over it.

Nothing recovers. The peer ACKs our FIN; the FIN_WAIT_1 → FIN_WAIT_2
arm at `:975` requires `TCP_FIN_WAIT_1`, the LAST_ACK completion at
`:1014` requires `TCP_LAST_ACK`, and the CLOSING arm at `:1022`
requires `TCP_CLOSING` — none match. The ACK merely prunes the FIN,
leaving `unacked_head` NULL. The PCB now sits in CLOSE_WAIT with
`p->detached == 1` (set at `:1625`) and an empty retransmit queue, so
`tcp_timer_tick()` skips it at every gate: not CLOSED (`:460`), not
FIN_WAIT_2 (`:479`), not TIME_WAIT (`:484`), and `if (!head) continue;`
at `:491` ends the iteration.

**Failure.** The PCB and its 32 KiB `rxbuf` are leaked for the
lifetime of the system, and the local port stays permanently
allocated — `tcp_port_taken()` (`:1233-1239`) only skips
`TCP_CLOSED` entries, so the ephemeral range is consumed one port per
occurrence. The peer, having received a FIN it will never see
completed, is stranded too. Both the timing of the peer's FIN and the
load that induces the preemption are remote-influenced.

**Fix.** MEM-01. Read-decide-write on `p->state` must be one critical
section on both sides.

### MEM-09 (medium). TIME-WAIT is entered before its deadline is armed, and the timer has no non-zero guard

**Requirement.** RFC 793 3.9, eighth check the FIN bit, FIN-WAIT-1:
*"If our FIN has been ACKed (perhaps in this segment), then enter
TIME-WAIT, start the time-wait timer, turn off the other timers"* —
one action, not two.

**Code.** Every transition into TIME_WAIT publishes the state first
and arms the deadline second: `sys/net/tcp.c:953-954` (FIN_WAIT_1 with
our FIN acked), `:961-962` (FIN_WAIT_2), `:1024-1025` (CLOSING). The
timer's expiry test at `:484` is
`if (p->state == TCP_TIME_WAIT && now >= p->time_wait_until)` with no
guard for an unarmed deadline — unlike the FIN_WAIT_2 reaper directly
above it at `:479-480`, which does guard (`p->fin_wait2_until &&
now >= …`). A PCB is zeroed at allocation (`:1134`, and `:657` for
children), so on the first entry `time_wait_until` is exactly 0 and
`now >= 0` is unconditionally true.

**Failure.** The lo kthread is preempted between `:953` and `:954`.
The timer kthread runs `tcp_timer_tick()`, `:484` matches, and `:487`
sets `TCP_CLOSED`; the PCB is freed on the next tick. The full
`TCP_TIME_WAIT_TICKS` (2 × 30 s, `:100-101`) is skipped entirely, so a
retransmitted FIN from the peer finds no PCB and draws a RST from
`tcp_send_rst()` (`:1077-1079`) instead of the re-ACK RFC 793
requires, and the 4-tuple becomes immediately reusable. A second,
smaller hazard sits in the same two lines: `time_wait_until` is a
`uint64_t` (`:174`) written as two 32-bit stores on i386, so the timer
can read a half-updated value once the tick counter exceeds 2^32.

**Fix.** Arm `time_wait_until` before storing the state at all three
sites, add the same non-zero guard the FIN_WAIT_2 path uses to `:484`,
and properly do both stores under the netstack lock.

### MEM-10 (medium). The closing state is published, the lock dropped, and only then is the FIN sequenced

**Requirement.** RFC 793 3.9, fifth check the ACK field, LAST-ACK:
*"The only thing that can arrive in this state is an acknowledgment of
our FIN. If our FIN is now acknowledged, delete the TCB, enter the
CLOSED state, and return."*

**Code.** In `tcp_close()`'s ESTABLISHED arm (`sys/net/tcp.c:1628-1631`)
and CLOSE_WAIT arm (`:1633-1636`) — and identically in
`tcp_shutdown_wr()` at `:1707-1715` — the new state is published,
`tcp_unlock()` is called, and only then is the FIN handed to
`tcp_xmit_queue()`. An RX landing in that window runs
`tcp_in_established()` against a PCB already in LAST_ACK whose
`snd_nxt` has not yet been advanced over the FIN and whose unacked
FIFO is still empty. The completion test at `:1014-1015` —
`ack == p->snd_nxt && !p->unacked_head` — is therefore satisfied by
any ordinary delayed or duplicate ACK of previously sent data, and
`tcp_kill_pcb()` moves the PCB to CLOSED.

**Failure.** `tcp_xmit_queue()` then transmits a FIN on behalf of a
connection that no longer exists, appending it to the unacked queue of
a PCB the reaper may free on its next tick (`:465`). The peer receives
the FIN but every later segment it sends draws a RST, because
`tcp_find()` skips CLOSED PCBs (`:565`), so it is stranded in
FIN-WAIT-2 until its own timeout. The equivalent race in the
ESTABLISHED arm lets an incoming FIN take `:952`'s TIME_WAIT branch
against the pre-FIN `snd_nxt`.

**Fix.** Reserve the FIN's sequence number under the same lock that
publishes the closing state — advance `snd_nxt` and link the segment
onto the unacked FIFO before unlocking, transmitting afterwards.

### MEM-11 (medium). `tcp_listen()` mutates the accept queue unlocked, and shrinking the backlog orphans queued children

**Requirement.** RFC 793 3.8 OPEN (passive): *"A transmission control
block (TCB) is created and partially filled in with data from the OPEN
command parameters."*

**Code.** Every other process-context mutator of the accept queue
brackets itself with `tcp_lock()` — `tcp_accept()` does so explicitly
at `sys/net/tcp.c:1380`. `tcp_listen()` (`:1180-1215`) takes no lock
on any path, while `tcp_in_syn_received()` writes
`par->accept_q[par->accept_count++]` at `:758`. Two defects follow.

1. The grow path `kfree()`s the old array at `:1202` and only then
   publishes the new pointer at `:1203` and the new cap at `:1204`. A
   handshake completing in that window writes through the freed
   `accept_q`, or indexes the new array against the old cap check.
2. The shrink path (`:1192-1197`) lowers `accept_cap` and truncates
   `accept_count` with no handling of the children at indices
   `>= backlog`: they are ESTABLISHED, still carry `->parent`, and are
   no longer in the queue, so `tcp_accept()` can never return them and
   the reaper's only child-free branch (`:473`) requires
   `TCP_CLOSED`.

**Failure.** `listen(fd, 32)`, ten connections queue up unaccepted,
then `listen(fd, 2)` to throttle: children at indices 2..9 are
unreachable, never CLOSED, never reaped, and leak
8 × (`sizeof(tcp_pcb_t)` + 32768) bytes while their peers believe the
connections are up.

**Fix.** Wrap the whole body of `tcp_listen()` in
`tcp_lock()`/`tcp_unlock()` (allocating the new array before taking
the lock), and on shrink reset and detach the children above the new
cap the way `tcp_close()`'s LISTEN arm already does at `:1662-1672`.

### MEM-12 (medium). `tcp_port_taken()` walks the global PCB list unlocked from preemptible process context

**Requirement.** RFC 793 2.7: *"To provide for unique addresses within
each TCP, we concatenate an internet address identifying the TCP with
a port identifier to create a socket which will be unique throughout
all networks connected together."*

**Code.** `tcp_port_taken()` (`sys/net/tcp.c:1233-1239`) is the only
walker of `g_tcp_pcbs` in the file that does not run under
`tcp_lock()`. Every other one does: `tcp_find` (`:564`, `:572`) is
called from the RX path, `tcp_timer_tick` brackets its walk at
`:457`/`:518`, `tcp_free` at `:1157`, `tcp_close`'s LISTEN arm at
`:1621`, `tcp_alloc`'s link at `:1144`. `tcp_port_taken` is reached
only from `tcp_alloc_ephemeral` (`:1241-1251`) → `tcp_connect_start`
(`:1254`) → `tcp_connect`/`tcp_connect_nb` → `afinet_connect`
(`sys/net/af_inet.c:1223-1224`), i.e. plain process context with
interrupts enabled. `tcp_hold(p)` at `:1281` pins only the *caller's*
PCB, not the nodes being walked. The scan is O(n) per candidate port
and O(n²) overall, so it is long enough to be preempted repeatedly —
and the thing it can schedule is the reaper kthread that `kfree()`s
PCBs at `:465` and `:474`.

**Failure.** A `connect()` standing on node X when the reaper frees X
reads `X->next` out of a freed slab. Best case the walk terminates
early and hands out a duplicate ephemeral port, so `tcp_find()`
delivers two connections' segments to one PCB — exactly the failure
TCP-07 was written to eliminate. Worst case it chases a recycled
pointer or loops on a cycle inside `connect(2)`.

**Fix.** Bracket the scan the way every other walker does — better,
around the whole `tcp_alloc_ephemeral()` loop so the chosen port
cannot be claimed between the check and the assignment. Since the
locked region would then be O(n²), replace the linear check with a
port-indexed bitmap or hash so the locked work is O(1) per candidate.

### MEM-13 (medium). `tcp_in_listen()` links the child into `g_tcp_pcbs` with no lock

**Requirement.** RFC 793 3.9, LISTEN, third check for a SYN: *"ISS
should be selected and a SYN segment sent of the form: `<SEQ=ISS>
<ACK=RCV.NXT><CTL=SYN,ACK>` … The connection state should be changed
to SYN-RECEIVED."*

**Code.** `tcp_in_listen()` creates the SYN-RECEIVED TCB and splices
it into the global list with a bare two-store sequence —
`c->next = g_tcp_pcbs; g_tcp_pcbs = c;` (`sys/net/tcp.c:675-676`) —
with no `tcp_lock()`, unlike `tcp_alloc()` (`:1144-1147`) and
`tcp_free()` (`:1157-1168`). The backlog count immediately above
(`:648-651`) walks the same list unlocked. Under MEM-01 the whole
function runs preemptibly on loopback and can additionally be nested
by a hardware RX interrupt.

**Failure.** The lo kthread has read `g_tcp_pcbs` into `c->next`
(`:675`) when a NIC RX interrupt arrives carrying a SYN for another
listener. The ISR runs `tcp_in_listen()` to completion, publishing its
own child `c2` as the list head. The kthread resumes and executes
`:676`, overwriting `g_tcp_pcbs` with `c`. `c2` is now unreachable:
`tcp_find()` cannot match its segments (so its peer is blackholed and
gets no RST), `tcp_timer_tick()` cannot retransmit or reap it, and
`tcp_free()` can never be reached for it — the PCB plus its 32 KiB
`rxbuf` leak permanently, once per occurrence, entirely under remote
control of SYN arrival timing.

**Fix.** Bracket the list insertion (and the `accept_q` append at
`:758`) in `tcp_lock()`/`tcp_unlock()`; `intr_disable`/`intr_restore`
nest correctly, so it is a no-op on a genuine ISR path and closes the
loopback-kthread window.

---

# Part 1 — the state machine (RFC 793 3.4, 3.5, 3.9)

Fifteen findings. All are remote-drivable; SM-01 is the only one that
leaks kernel memory on a single ordinary exchange.

### SM-01 (critical). The retransmitted-FIN shortcut returns before the CLOSING and LAST-ACK completion tests, wedging both states forever

**Requirement.** RFC 793 3.9, fifth check the ACK field
(`docs/rfc/rfc793.txt:4438-4452`):

> CLOSING STATE
>
>   In addition to the processing for the ESTABLISHED state, if the
>   ACK acknowledges our FIN then enter the TIME-WAIT state, otherwise
>   ignore the segment.
>
> LAST-ACK STATE
>
>   The only thing that can arrive in this state is an acknowledgment
>   of our FIN. If our FIN is now acknowledged, delete the TCB, enter
>   the CLOSED state, and return.

**Code.** `tcp_in_established()` handles a retransmitted FIN at
`sys/net/tcp.c:924-929`:

```c
if ((flags & TCP_FIN) && seq + (uint32_t)dlen + 1u == p->rcv_nxt) {
    if (p->state == TCP_TIME_WAIT)
        p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;
    tcp_send_ctl(p, TCP_ACK);
    return;
}
```

That `return` is unconditional, so a segment that is *both* a FIN
retransmission *and* the ACK of our own FIN never reaches the
LAST_ACK → CLOSED test at `:1014` or the CLOSING → TIME_WAIT test at
`:1022`. The ACK-processing block at `:827-904` has already run, so
our FIN is pruned and `unacked_head` is NULL; the PCB is left in
CLOSING (or LAST_ACK) with an empty retransmit queue.

Nothing can ever touch it again. `tcp_timer_tick()` (`:458-518`)
advances only TIME_WAIT (`:484`) and FIN_WAIT_2 (`:479`), frees only
`TCP_CLOSED` PCBs (`:460`), and `continue`s at `:491` when there is
nothing in the unacked queue. CLOSING and LAST_ACK are the only two
closing states with no independent deadline, and this is exactly how
they are entered. `tcp_close()` has already set `detached` (`:1625`),
so there is no owning socket either.

**Failure.** Simultaneous close (RFC 793 Figure 14). `close()` sets
`detached = 1` and FIN_WAIT_1 and sends FIN `seq=S`. The peer's FIN
arrives with `ack=X` not covering ours: `:952` fails, so we enter
CLOSING at `:956` and ACK. That ACK is lost. The peer receives our
FIN, enters CLOSING, and RTO-retransmits its FIN now carrying
`ack=S+1`. At `:845-848` that ACK prunes our FIN; at `:924`
`X + 0 + 1 == rcv_nxt` is true, so we re-ACK and `return`. `:1022` is
never evaluated. The peer gets the ACK, goes TIME_WAIT then CLOSED,
and sends nothing more. Our PCB sits in CLOSING for the life of the
system, never reaped, its 32 KiB ring never freed, its local port
never released. A peer need only retransmit its FIN once after
acknowledging ours — ordinary behaviour after a single lost ACK, and
trivially repeatable by a hostile peer to exhaust kernel memory at
~33 KiB per iteration.

**Fix.** Do not return from the retransmitted-FIN block: emit the ACK
(and re-arm TIME_WAIT), then fall through to the LAST_ACK and CLOSING
tests at `:1014`/`:1022` — or evaluate those two before the `:924`
shortcut. Separately, give CLOSING and LAST_ACK a bounded deadline in
`tcp_timer_tick()` so no closing state can be entered without a reaper
that can reach it.

### SM-02 (high). The segment-acceptability test does not exist, so an unacceptable segment is never answered

**Requirement.** RFC 793 3.9, first check sequence number, which
applies to SYN-RECEIVED, ESTABLISHED, FIN-WAIT-1, FIN-WAIT-2,
CLOSE-WAIT, CLOSING, LAST-ACK and TIME-WAIT
(`docs/rfc/rfc793.txt:4230-4240`):

> If an incoming segment is not acceptable, an acknowledgment should
> be sent in reply (unless the RST bit is set, if so drop the segment
> and return):
>
>     <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
>
> After sending the acknowledgment, drop the unacceptable segment and
> return.

3.3 adds: *"Note that when the receive window is zero no segments
should be acceptable except ACK segments."*

**Code.** `tcp_in_established()` (`sys/net/tcp.c:776`) serves all
eight synchronized states and never evaluates the four-row table. The
only window test in the file, `tcp_seq_in_rcv_window()` (`:769`), is
called from exactly one place — the RST branch at `:791`. Every other
step re-derives its own ad-hoc predicate: data needs `seq ==
p->rcv_nxt` (`:813`), the FIN needs `seq + dlen == p->rcv_nxt`
(`:931`).

The consequence with no workaround elsewhere is the missing
acknowledgment. The sole path that emits one for a rejected segment is
`:1030-1034`, guarded twice over:

```c
if (dlen && (p->state == TCP_ESTABLISHED ||
             p->state == TCP_FIN_WAIT_1 ||
             p->state == TCP_FIN_WAIT_2)) {
    tcp_send_ctl(p, TCP_ACK);
}
```

The `dlen` term means a `SEG.LEN == 0` segment outside the window
produces no reply in any state; the state term means an unacceptable
data segment in CLOSE_WAIT, CLOSING, LAST_ACK or TIME_WAIT produces
none either. The form emitted is correct —
`tcp_send_ctl()` (`:309-311`) sends `<SEQ=SND.NXT><ACK=RCV.NXT>
<CTL=ACK>` exactly as prescribed — it is simply not emitted in the
cases that matter.

**Failure.** An idle ESTABLISHED connection to a peer with
`SO_KEEPALIVE`. Per RFC 1122 4.2.3.6 the peer probes with `SEG.LEN=0`
and `SEG.SEQ = SND.NXT-1`, i.e. exactly `RCV.NXT-1` here —
deliberately unacceptable, because the reply is what proves liveness.
Trace it: no RST (`:779`), `:806` needs `dlen>0`, `:813` needs `dlen`,
the ACK branch at `:827` falls through to `p->last_ack = ack` at
`:898` and transmits nothing, the FIN tests at `:924`/`:931` need
`TCP_FIN`, and `:1030` needs `dlen`. Not one byte goes out. After its
probe count the peer declares the connection dead and resets it while
this side is fully alive. The same silence answers a BSD/Linux
zero-window persist probe, which carries `SEG.SEQ = SND.NXT-1` for the
same reason.

**Fix.** Add `tcp_seg_acceptable(p, seq, dlen)` implementing all four
rows (SEG.LEN and RCV.WND each zero/non-zero, including the
`SEG.SEQ+SEG.LEN-1` disjunct) using the existing wrap-safe signed
differences, and call it at the top of `tcp_in_established()` before
any other field is touched — and in `tcp_input()` before the
`snd_wnd` latch at `:1092`. On failure: if `TCP_RST` is set, return
silently; otherwise `tcp_send_ctl(p, TCP_ACK)` and return.

### SM-03 (high). Segment text is accepted after the peer's FIN

**Requirement.** RFC 793 3.9, seventh step, CLOSE-WAIT / CLOSING /
LAST-ACK / TIME-WAIT (`docs/rfc/rfc793.txt:4595-4601`): *"This should
not occur, since a FIN has been received from the remote side. Ignore
the segment text."*

**Code.** `tcp_input()` dispatches CLOSE_WAIT, CLOSING, LAST_ACK and
TIME_WAIT into `tcp_in_established()` (`sys/net/tcp.c:1116-1120`), and
the data-acceptance block at `:813-824` has no state guard at all:
`if (dlen && seq == p->rcv_nxt)` copies the payload into the ring,
advances `p->rcv_nxt` and wakes the reader in every one of those
states. The detached-PCB RST at `:806` covers CLOSING/LAST_ACK/
TIME_WAIT, which are normally detached — but CLOSE_WAIT, the common
case where the application has not yet closed, is by definition *not*
detached, so it is fully exposed.

**Failure.** Two harms. (1) The peer sends FIN at `X`; we consume it,
`rcv_nxt = X+1`, and the application reads 0 and treats the stream as
complete. A segment at `SEG.SEQ = X+1` with 10 bytes — from an
off-path injector (after a FIN, RCV.NXT is a single fixed value) or a
confused peer — is buffered at `:813` and handed to the still-open fd
*after* EOF. (2) `rcv_nxt` is now `X+11`, so the peer's genuine FIN
retransmission (`SEG.SEQ = X`, `SEG.LEN = 0`) matches neither `:924`
(`X+0+1 != X+11`) nor `:931` (`X != X+11`), and `:1030` excludes
CLOSE_WAIT — so it elicits nothing at all and the peer retransmits to
its R2 limit and aborts with ETIMEDOUT instead of closing cleanly.
That is precisely the silence the TCP-13 comment at `:914-923` was
written to eliminate. In TIME_WAIT it additionally defeats the 2 MSL
guarantee, since the state can no longer recognise the remote FIN it
exists to absorb.

**Fix.** Gate the block at `:813` on the state being ESTABLISHED,
FIN_WAIT_1 or FIN_WAIT_2 — the same list already used for the trailing
ACK at `:1030-1032` — and in the post-FIN states discard the text
while still sending the acknowledgment the FIN handling requires.

### SM-04 (high). A RST in SYN-RECEIVED is honoured with no sequence validation at all

**Requirement.** RFC 793 3.4, Reset Processing
(`docs/rfc/rfc793.txt:2396-2399`): *"In all states except SYN-SENT,
all reset (RST) segments are validated by checking their SEQ-fields.
A reset is valid if its sequence number is in the window."* 3.9 lists
SYN-RECEIVED under "first check sequence number", so that check runs
*before* the RST check.

**Code.** `tcp_in_syn_received()` is declared
`static void tcp_in_syn_received(tcp_pcb_t *p, uint32_t ack,
uint8_t flags)` (`sys/net/tcp.c:732`) and called as
`tcp_in_syn_received(p, ack, flags)` (`:1111`) — the segment's
sequence number is never handed to it, so no SEQ check is possible.
Its first action, at `:737-740`, is
`if (flags & TCP_RST) { tcp_kill_pcb(p, ECONNRESET); return; }`.

This is the one state the check was never wired into. ESTABLISHED
validates the RST against the receive window plus an RFC 5961 3.2
challenge ACK at `:791-797`; SYN_SENT validates it against the ACK at
`:682-697`, and the TCP-09 comment there says in terms *"The
challenge-ACK logic was already implemented for ESTABLISHED; SYN_SENT
was missed."* SYN_RECEIVED was missed too.

**Failure.** A server listens on a known port; a client connects and
its child PCB sits in SYN_RECEIVED between the SYN and the third ACK.
An off-path attacker sprays bare RSTs from the client's address,
iterating only the 16-bit source port — no sequence number, no ACK
field and no window need be guessed. Each match reaches `:737`,
`tcp_kill_pcb()` moves the child to CLOSED, and the reaper frees it
(`:473`) since it is not yet in the accept queue. The client's
completing ACK then matches no PCB, `tcp_input()` falls to `:1078`
and RSTs the client. The ISN randomisation of TCP-08 provides no
protection on this path.

**Fix.** Pass `seq` into `tcp_in_syn_received()` and require it to be
in the receive window per RFC 793, or — matching what the code already
does in ESTABLISHED — exactly `RCV.NXT` with a challenge ACK otherwise
(RFC 5961 3.2). `tcp_seq_in_rcv_window()` is already available at
`:769`.

### SM-05 (high). No segment trimming: a straddling segment is discarded whole, and a partial accept desynchronises permanently

**Requirement.** RFC 793 3.9, first check sequence number
(`docs/rfc/rfc793.txt:4265-4270`): *"Segments are processed in
sequence. Initial tests on arrival are used to discard old
duplicates, but further processing is done in SEG.SEQ order. If a
segment's contents straddle the boundary between old and new, only the
new parts should be processed."* The RFC states the implementation
technique too: *"One could tailor actual segments to fit this
assumption by trimming off any portions that lie outside the window
(including SYN and FIN), and only processing further if the segment
then begins at RCV.NXT."*

**Code.** The receive path tests `dlen && seq == p->rcv_nxt`
(`sys/net/tcp.c:813`) — an exact left-edge match. There is no trimming
anywhere in `tcp_in_established()`. The FIN tests at `:924` and `:931`
are exact equality on `seq + dlen` for the same reason, so a
straddling segment carrying a FIN loses the FIN as well.

The right edge compounds it: `:814-816` clamps `accept_n` to the ring
room and `:822` advances `rcv_nxt` only over what was taken, silently
discarding the tail with no record that a hole exists. Meanwhile
`tcp_retx_head()` (`:397-403`) always replays `s->seq` verbatim.

**Failure.** Two shapes, both fatal rather than merely wasteful.
(1) `RCV.NXT = 1600` after `[1100,1600)` was received and ACKed but
the ACK was lost. The peer's RTO fires and it retransmits a collapsed
segment `[1100,2100)` — which is what Linux's
`tcp_retrans_try_collapse` produces, and which RFC 793 3.7 explicitly
permits (*"segments sent do not have to match segments received"*).
`:813` fails, so all 1000 genuinely new bytes are dropped; `:1030`
ACKs 1600 again; the peer's queue head is unchanged, so its next RTO
retransmits the identical segment. The connection transfers nothing
further and dies at `TCP_MAX_RETX`. (2) Self-inflicted: `[100,200)`
arrives with 50 bytes of ring room, so `[100,150)` is buffered and
`rcv_nxt` becomes 150. Every retransmission from then on starts at
100 and is rejected whole, forever, even after the application drains
the entire ring. If that segment carried a FIN, the close never
completes either.

**Fix.** Trim before processing: if `(int32_t)(p->rcv_nxt - seq) > 0`,
advance `payload` and `seq` by the delta and reduce `dlen` by it
(dropping a trimmed SYN bit); clamp the right edge to
`rcv_nxt + rcv_wnd`, dropping the FIN bit if the FIN falls outside;
then run the existing `seq == rcv_nxt` logic, and the FIN tests at
`:924`/`:931`, on the trimmed values. Dropping the *whole* segment
when it exceeds the right edge is preferable to today's prefix
acceptance, since it leaves the peer's retransmission usable.

### SM-06 (high). A SYN arriving on a synchronized connection elicits nothing

**Requirement.** RFC 793 3.9, fourth check the SYN bit
(`docs/rfc/rfc793.txt:4540-4548`): *"If the SYN is in the window it is
an error, send a reset, any outstanding RECEIVEs and SEND should
receive 'reset' responses, all segment queues should be flushed, the
user should also receive an unsolicited general 'connection reset'
signal, enter the CLOSED state, delete the TCB, and return."* 3.4
figure 10 line 4 gives the out-of-window form: *"When the SYN arrives
at line 3, TCP B, being in a synchronized state, and the incoming
segment outside the window, responds with an acknowledgment indicating
what sequence it next expects to hear (ACK 100)."*

**Code.** `tcp_in_established()` serves ESTABLISHED, FIN_WAIT_1/2,
CLOSE_WAIT, CLOSING, LAST_ACK and TIME_WAIT (dispatch at
`sys/net/tcp.c:1113-1120`). Its only reference to `TCP_SYN` is at
`:1005`, gated on `p->state == TCP_TIME_WAIT`. In every other
synchronized state a bare SYN matches no branch: the RST bit is clear,
`dlen` is 0 so neither the data path nor the trailing ACK at `:1030`
fires, the ACK bit is clear, and there is no FIN — the function
returns having transmitted nothing.

The RFC 5961 4 substitution is already understood in this file: the
TIME_WAIT arm at `:1005-1008` implements exactly the challenge ACK,
and its comment cites RFC 5961 4. It was simply never applied to the
other seven states, which produce neither the 793 reset nor the 5961
challenge ACK.

**Failure.** A peer holding an ESTABLISHED connection crashes and
reconnects from the same source port (a client that `bind()`s a fixed
port, or any RPC-style service). Its SYN arrives at our still-
ESTABLISHED PCB. We answer nothing: the peer retransmits its SYN until
its `connect()` times out and can never learn the connection is stale,
and never gets the ACK that would make it send the RST of figure 10
line 5. Our PCB, with its 32 KiB ring, stays ESTABLISHED indefinitely
— there is no keepalive and no idle timeout — while the application
sits blocked in `recv()`. This is the receiving half of the same
exchange as SM-10, so neither end of a half-open connection can
discover it.

**Fix.** Lift the check out of the TIME_WAIT-only guard at `:1005` so
it applies to every state `tcp_in_established()` serves, sending the
RFC 5961 4 challenge ACK (preferred over 793's blind RST, and
consistent with what the TIME_WAIT arm already does).

### SM-07 (high). An ACK-bearing segment at a LISTEN socket draws no RST, and the comment names a path that does not exist

**Requirement.** RFC 793 3.9, LISTEN, second check for an ACK
(`docs/rfc/rfc793.txt:4046-4053`):

> Any acknowledgment is bad if it arrives on a connection still in the
> LISTEN state. An acceptable reset segment should be formed for any
> arriving ACK-bearing segment. The RST should be formatted as
> follows:
>
>     <SEQ=SEG.ACK><CTL=RST>

**Code.** `tcp_in_listen()` returns without transmitting for any
ACK-bearing segment: a non-SYN segment returns at
`sys/net/tcp.c:633`, and a SYN|ACK or SYN|FIN at `:634-640`. The
comment at `:635-638` says the RST is left to *"the unmatched-segment
path below"* — but there is no such path. `tcp_send_rst()` is reached
only when `tcp_find()` returned NULL (`:1077-1080`), and `tcp_find()`
*did* return the listener via its second loop (`:572-576`), after
which `tcp_input()` dispatches to `tcp_in_listen()` and returns at
`:1106`. The RST is never sent from any code path.

**Failure.** Having a listener on the port is what breaks recovery.
A client completes a handshake; the server-side child dies (SYN_RECEIVED
retransmit budget exhausted, or a reaped CLOSED child). The client,
still ESTABLISHED, sends data. Each segment carries ACK, so
`tcp_find()`'s exact-4-tuple loop misses the freed child and the LISTEN
loop returns the listener; `tcp_in_listen()` sees no SYN and returns
silently. The client receives no RST, cannot discover the connection
is gone, and retransmits into a black hole for its whole R2 budget —
63 s here, ~15 minutes on Linux — instead of failing immediately with
ECONNRESET. The *same* segment sent to a port with no listener at all
is correctly RST-ed at `:1078`. This is also figure 12 of 3.4 ("Old
Duplicate SYN Initiates a Reset on two Passive Sockets"), where the
RST from the passive socket is the only thing that returns the other
TCP from SYN-RECEIVED to LISTEN.

**Fix.** In `tcp_in_listen()`, for any non-RST segment carrying ACK,
emit `<SEQ=SEG.ACK><CTL=RST>`. `tcp_send_rst()` (`:581`) already
formats exactly that for the ACK case (`:606-610`), and
`tcp_in_listen()` already receives `saddr`/`daddr`/`sport`/`dport`, so
it can call it directly.

### SM-08 (high). A connection killed by RST or by the retransmit budget is reported to `read()` as a clean end-of-file

**Requirement.** RFC 793 3.9, second check the RST bit, ESTABLISHED /
FIN-WAIT-1 / FIN-WAIT-2 / CLOSE-WAIT: *"If the RST bit is set then,
any outstanding RECEIVEs and SEND should receive 'reset' responses.
All segment queues should be flushed. Users should also receive an
unsolicited general 'connection reset' signal."* And Timeouts, USER
TIMEOUT (`docs/rfc/rfc793.txt:4727-4729`): *"For any state if the user
timeout expires, flush all queues, signal the user 'error: connection
aborted due to user timeout' in general and for any outstanding
calls."*

**Code.** `tcp_kill_pcb()` records the abort reason in `p->so_error`
(ECONNRESET at `sys/net/tcp.c:797`, ETIMEDOUT at `:481` and `:498`)
and sets `TCP_CLOSED` (`:417`). But `tcp_recv_nb()`'s EOF test at
`:1530-1534` lumps `TCP_CLOSED` in with the states reached after an
orderly FIN and returns 0 without ever consulting `so_error`;
`tcp_peek_nb()` does the same at `:1575-1580`. `tcp_recv()` breaks out
on any non-EAGAIN return, and `afinet_node_read_body()` passes that 0
straight to `read()` (`sys/net/af_inet.c:567-568`), as does
`afinet_recvfrom()` (`:1446-1447`). The error survives only in
`p->so_error`, reachable exclusively via `getsockopt(SO_ERROR)` →
`afinet_so_error()` → `tcp_take_so_error()`
(`sys/net/af_inet.c:1164-1169`), which no ordinary stream reader
calls.

The send side does distinguish the two — `tcp_send_impl` returns
`-EPIPE` when `so_error` is set (`sys/net/tcp.c:1424`) — so only the
receive direction is silently mis-signalled. Bytes still in the ring
are also delivered rather than flushed, contrary to *"All segment
queues should be flushed."*

**Failure.** An HTTP client `read()`s a response body over a
connection the server aborts mid-transfer with a RST (a crash, an
nginx `reset_timedout_connection`, a middlebox reset). `:797` honours
the RST; the next `read()` drains whatever is buffered and then
returns 0. The application cannot distinguish this from an orderly
FIN, so a truncated body is accepted as complete — silent data loss
with a success return. The identical path turns an ETIMEDOUT abort
(peer unreachable, budget exhausted at `:497-498`) into a clean EOF:
a 3 MB prefix of a 10 MB object is committed to disk as the whole
file.

**Fix.** In `tcp_recv_nb()`/`tcp_peek_nb()`, before the EOF branch,
return `-p->so_error` when the PCB reached `TCP_CLOSED` with a
non-zero `so_error`. Only a state reached through an orderly FIN
exchange (CLOSE_WAIT/CLOSING/LAST_ACK/TIME_WAIT, and CLOSED with
`so_error == 0`) should yield 0. Flushing the ring in
`tcp_kill_pcb()` on the RST path satisfies the rest of the clause.

### SM-09 (medium). SYN-RECEIVED never validates SEG.SEQ, drops the third segment's text and FIN, and sends no RST for an unacceptable ACK

**Requirement.** RFC 793 3.9, fifth check the ACK field, SYN-RECEIVED
(`docs/rfc/rfc793.txt:4400-4412`): *"If SND.UNA =< SEG.ACK =< SND.NXT
then enter ESTABLISHED state and continue processing. … If the segment
acknowledgment is not acceptable, form a reset segment,
`<SEQ=SEG.ACK><CTL=RST>` and send it."* "First check sequence number"
lists SYN-RECEIVED among the states that must run the 3.3 test.

**Code.** `tcp_in_syn_received(p, ack, flags)` (`sys/net/tcp.c:1111`)
receives neither `seq` nor the payload. Three failures follow from one
signature:

1. No sequence check: the third segment of the handshake is accepted
   with any sequence number whatsoever.
2. `:741` tests `(flags & TCP_ACK) && ack == p->snd_nxt` and returns
   at `:762` immediately after the state transition and accept-queue
   hand-off, so data or a FIN riding on the third ACK is dropped
   without a trace, contrary to "continue processing" (which means run
   the seventh and eighth checks on that same segment). RFC 793 3.4
   states this is legitimate traffic: *"connection synchronization
   using data-carrying segments … is perfectly legitimate, so long as
   the receiving TCP doesn't deliver the data to the user until it is
   clear the data is valid."*
3. Anything that fails `:741` falls off the end of the function with
   no reply, where the RFC requires `<SEQ=SEG.ACK><CTL=RST>`. The
   equivalent reset *is* implemented for SYN_SENT at `:710`.

The acceptance test itself (`ack == snd_nxt`) is defensible — in
SYN_RECEIVED `snd_una == iss` and `snd_nxt == iss+1`, so it is exactly
the `SND.UNA < SEG.ACK =< SND.NXT` of RFC 9293 — it is the other half
of the rule that is missing.

**Failure.** (a) A client that coalesces its request onto the
handshake-completing ACK (legal, and what T/TCP-style and some
embedded stacks emit) has the bytes discarded and unacknowledged, so
its RTO (`TCP_RTO_BASE_TICKS`, 1 s) must expire before it retransmits:
a full second of dead time on every such connection. (b) An old
duplicate ACK from a previous incarnation is dropped instead of being
reset, so the half-open child keeps retransmitting SYN|ACK for the
full ~63 s budget while occupying a backlog slot (`:648-653` counts
SYN_RECEIVED children against the backlog), throttling real
connections.

**Fix.** Pass `seq`, `payload` and `dlen` into
`tcp_in_syn_received()`, run the 3.3 acceptability test first, send
`tcp_xmit_raw(p, ack, TCP_RST, NULL, 0)` when the ACK is outside
`(snd_una, snd_nxt]`, and after the ESTABLISHED transition fall
through into `tcp_in_established()`'s text/FIN processing.

### SM-10 (medium). SYN-SENT sends no RST for an out-of-range ACK on a non-SYN segment

**Requirement.** RFC 793 3.9, SYN-SENT, first check the ACK bit
(`docs/rfc/rfc793.txt:4084-4091`):

> If SEG.ACK =< ISS, or SEG.ACK > SND.NXT, send a reset (unless the
> RST bit is set, if so drop the segment and return)
>
>     <SEQ=SEG.ACK><CTL=RST>
>
> and discard the segment. Return.

**Code.** `tcp_in_syn_sent()` (`sys/net/tcp.c:680-730`) handles
exactly two shapes: RST (`:682-698`) and SYN|ACK (`:699`). It
validates SEG.ACK and emits `<SEQ=SEG.ACK><CTL=RST>` only *inside* the
`(flags & (TCP_SYN|TCP_ACK)) == (TCP_SYN|TCP_ACK)` branch
(`:708-712`). A segment carrying ACK but not SYN (and not RST) falls
off the end of the function at `:730` and is silently discarded. The
ACK-bit check is specified before and independently of the SYN-bit
check precisely so this case is covered; it is figure 10 line 5 of
3.4, the mechanism by which a restarted host tells a peer its
connection no longer exists.

**Failure.** Substrate crashes/reboots while a peer holds an
ESTABLISHED connection to it, then re-connects on the same 4-tuple.
The peer, still synchronized, answers `<SEQ=300><ACK=100><CTL=ACK>`
(figure 10 line 4). SEG.ACK is far below our ISS, so RFC 793 requires
`<SEQ=100><CTL=RST>`, which aborts the peer. We send nothing. Our SYN
is retransmitted 6 times, each answered by the same bare ACK, and
`connect()` fails with ETIMEDOUT after ~63 s; the peer's half-open
connection is never torn down. The same omission makes this stack's
*own* TIME_WAIT challenge ACK (`:1005-1008`) unrecoverable: the reply
that arm depends on is exactly the RST this handler never sends.

**Fix.** Hoist the existing range test at `:708-712` out of the
SYN-gated branch so it runs for any ACK-bearing, non-RST segment in
SYN_SENT.

### SM-11 (medium). Simultaneous open is not implemented

**Requirement.** RFC 793 3.9, SYN-SENT, fourth check the SYN bit
(`docs/rfc/rfc793.txt:4212-4217`): *"Otherwise enter SYN-RECEIVED,
form a SYN,ACK segment `<SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>` and send
it."* 3.4 draws the exchange in figure 8 (*"The procedure also works
if two TCP simultaneously initiate the procedure"*).

**Code.** `tcp_in_syn_sent()` handles only RST (`sys/net/tcp.c:682`)
and SYN|ACK (`:699`). A segment with SYN set and ACK clear — the
defining event of a simultaneous open — reaches `:730` and is dropped.
`RCV.NXT` is never set to `SEG.SEQ+1`, no SYN|ACK is sent, and the PCB
never leaves SYN_SENT.

**Failure.** Two hosts `connect()` to each other at the same time, as
TCP-based NAT hole punching and peer-to-peer rendezvous do
deliberately. Each receives the other's bare SYN in SYN_SENT and
discards it; both retransmit their own SYN on the RTO ladder; after
`TCP_MAX_RETX` (~63 s) both `connect()` calls fail with ETIMEDOUT
although the path is fully open in both directions.

**Fix.** Add the branch: on SYN with no ACK, set `p->rcv_nxt =
seq + 1`, move to `TCP_SYN_RECEIVED` and send a SYN|ACK from ISS
(retransmitting the queued SYN rather than queuing a second one),
leaving the existing SYN_RECEIVED handler to complete the handshake.

### SM-12 (medium). Data and FIN are processed from segments with the ACK bit off

**Requirement.** RFC 793 3.9, fifth check the ACK field
(`docs/rfc/rfc793.txt:4394-4396`): *"fifth check the ACK field, if the
ACK bit is off drop the segment and return"*. In every synchronized
state this is a hard gate placed before the URG, text and FIN steps;
only LISTEN and SYN-SENT may act on an ACK-less segment.

**Code.** `tcp_in_established()` never tests for the *absence* of
`TCP_ACK`. The ACK-dependent work is correctly wrapped in
`if (flags & TCP_ACK)` at `sys/net/tcp.c:827`, but the data path at
`:813` and the FIN paths at `:924` and `:931` run unconditionally.

**Failure.** An off-path attacker (or a buggy middlebox) that knows
the 4-tuple and can land a segment at RCV.NXT sends `<CTL=FIN>` with
no ACK bit and a garbage acknowledgment field. `:931` fires,
`rcv_nxt++`, the state becomes CLOSE_WAIT, the reader is woken with
EOF and an ACK goes out — a premature end-of-stream on a connection
the peer still considers open. A segment with no flags at all but a
payload at RCV.NXT is likewise buffered and delivered. RFC 793 would
have dropped both at step 5, which removes the attack entirely and
admits only data a conforming TCP could have sent.

**Fix.** At the top of `tcp_in_established()`, after the RST handling
and the SM-02 acceptability gate, `if (!(flags & TCP_ACK)) return;`.

### SM-13 (medium). Segment text is committed before the ACK field is checked

**Requirement.** RFC 793 orders the ACK check (fifth) strictly before
text processing (seventh), and the ESTABLISHED rule is a drop, not a
partial accept: *"If the ACK acks something not yet sent (SEG.ACK >
SND.NXT) then send an ACK, drop the segment, and return."*

**Code.** `tcp_in_established()` runs them the other way round. The
receive-ring copy is at `sys/net/tcp.c:813-824`; the ACK-acceptability
test is at `:838`, and its rejection returns at `:843`. By then the
payload has been copied into `rxbuf`, `rx_count` and `rcv_nxt` have
advanced, and `recv_chan` has been woken. The state-dependent ACK
handling (FIN_WAIT_1 → FIN_WAIT_2 at `:975`, CLOSING → TIME_WAIT at
`:1022`, LAST_ACK → CLOSED at `:1014`) is likewise reached only after
the text is committed.

**Failure.** A segment arrives with `SEG.SEQ = RCV.NXT`, 100 bytes of
payload, FIN set, and `SEG.ACK` one byte beyond `SND.NXT` (forged, or
badly reordered). `:813-824` commit all 100 bytes and advance
`rcv_nxt`; `:838` then decides the segment is unacceptable, ACKs and
returns at `:843` without processing the FIN. The peer's
retransmission of that segment now fails the `seq == rcv_nxt` test and
is discarded, so the FIN is never consumed and the connection never
closes — while the application has already received data from a
segment RFC 793 says must be dropped whole.

**Fix.** Move the whole `if (flags & TCP_ACK)` block from `:827` to
above the data-acceptance block at `:813`.

### SM-15 (medium). A retransmitted, already-consumed data segment after `close()` aborts the connection

**Requirement.** RFC 793 3.8, Close: *"Closing connections is intended
to be a graceful operation in the sense that outstanding SENDs will be
transmitted (and retransmitted), as flow control permits, until all
have been serviced."*

**Code.** The detached-PCB guard tests only `p->detached && dlen > 0`
(`sys/net/tcp.c:806`) — it does not require the data to be *new*. Any
segment carrying a payload, including a pure retransmission of bytes
already consumed into `rcv_nxt`, triggers `tcp_send_ctl(p, TCP_RST |
TCP_ACK)` and `tcp_kill_pcb()` (`:807-809`), which drops the PCB to
CLOSED and so abandons our own still-unacknowledged data and FIN — the
timer skips CLOSED PCBs at `:460`. Linux's equivalent
(TCPABORTONDATA) fires only for data beyond RCV.NXT, which preserves
the graceful-close guarantee for duplicates.

**Failure.** A client does `write(fd, request, n)` then `close(fd)`:
the PCB is in FIN_WAIT_1, detached, with the request segment and the
FIN still on the unacked queue. The server's earlier response segment
is retransmitted because our ACK was lost. It arrives with `dlen > 0`
at an already-consumed sequence number; `:806` fires, we RST the peer
and go CLOSED. Our request segment — if its first transmission was
lost — is now never retransmitted, so the server never receives the
request even though `close()` returned success and the RFC promises
those outstanding SENDs will be retransmitted until serviced.

**Fix.** Abort only when the segment carries data beyond `rcv_nxt`
(i.e. `(int32_t)(seq + dlen - p->rcv_nxt) > 0`); for a pure duplicate,
ACK it and let the close handshake finish.

### SM-14 (low). An arriving FIN signals no user in FIN-WAIT-1/2, and the TIME-WAIT expiry wakes nobody

**Requirement.** RFC 793 3.9, eighth check the FIN bit: *"If the FIN
bit is set, signal the user 'connection closing' and return any
pending RECEIVEs with same message, advance RCV.NXT over the FIN, and
send an acknowledgment for the FIN."* 3.5 adds: *"We assume that the
TCP will signal a user, even if no RECEIVEs are outstanding, that the
other side has closed."*

**Code.** The FIN handler wakes blocked receivers only on the
ESTABLISHED → CLOSE_WAIT transition
(`sched_wakeup(p->recv_chan)` at `sys/net/tcp.c:936`). The FIN_WAIT_1
branch (`:939-959`) and the FIN_WAIT_2 branch (`:960-964`) advance
`rcv_nxt` and ACK the FIN but signal nothing — and those are exactly
the states a half-closed socket sits in while its user blocks in
`read()` waiting to be told the peer has closed. The same omission
applies to the TIME_WAIT → CLOSED transition in the reaper at `:487`.

**Failure.** Bounded latency, not a wedge: `tcp_recv()` re-checks on
the `TCP_SLEEP_POLL` deadline (`:110`, ~62 ms) and `kern_poll()` has a
~50 ms backstop, so every half-close pays up to ~62 ms of dead time it
should not. The required signal is genuinely absent and the safety-net
timer is what covers for it.

**Fix.** Call `sched_wakeup(p->recv_chan)` in the FIN_WAIT_1 and
FIN_WAIT_2 arms as the ESTABLISHED arm already does, and on the
TIME_WAIT → CLOSED transition at `:487`.

---

# Part 2 — window management, reliability and congestion (RFC 793 3.7)

Fourteen findings. RFC 793 3.7 states most of this as "suggestions";
RFC 1122 4.2.3.3, 4.2.3.4 and 4.2.2.17 turn the same suggestions into
MUSTs, and RFC 5681 and RFC 6298 replace 793's retransmission sketch
outright. WIN-01 through WIN-04 are the four that break working
connections.

### WIN-01 (high). The zero-window persist probe is charged to the retransmission budget

**Requirement.** RFC 793 3.7, Managing the Window
(`docs/rfc/rfc793.txt:2694-2700`):

> The sending TCP must be prepared to accept from the user and send at
> least one octet of new data even if the send window is zero. The
> sending TCP must regularly retransmit to the receiving TCP even when
> the window is zero. Two minutes is recommended for the
> retransmission interval when the window is zero. This retransmission
> is essential to guarantee that when either TCP has a zero window the
> re-opening of the window will be reliably reported to the other.

RFC 1122 4.2.2.17 makes it explicit: a TCP MUST NOT abort a connection
because the window stays zero.

**Code.** `tcp_send_impl()` emits the required octet at
`sys/net/tcp.c:1451-1459` by calling `tcp_xmit_queue()`, which links
the probe into the *ordinary* unacked FIFO. The comment at
`:1452-1455` states the reasoning — *"Its RTO retransmissions keep
prodding the peer until it re-advertises a window, so no separate
persist timer is needed"* — which conflates two different timers. A
receiver at zero window discards the probe byte and ACKs with its
unchanged RCV.NXT, so `tcp_unacked_prune()` never retires the probe
and its `retx` counter only climbs. `tcp_timer_tick()` at `:497-499`
then treats that as a dead path:
`if (head->retx >= TCP_MAX_RETX) { tcp_kill_pcb(p, ETIMEDOUT); }`,
with `TCP_MAX_RETX == 6` (`:90`) and a 1 s base RTO doubling per
attempt (`:88`, `:494`). The whole budget is 1+2+4+8+16+32 = 63 s —
less than *one* of the RFC's recommended zero-window retransmission
intervals.

Substrate's own receiver demonstrates the mechanism end to end:
`tcp_seq_in_rcv_window()` admits the probe at `seq == rcv_nxt`
(`:769-774`), then `:813-822` clamps `accept_n` to
`TCP_RING_LEN - rx_count == 0`, leaves `rcv_nxt` unchanged, and
`:1030-1034` sends a bare ACK that does not cover the probe.

It is worse in practice: every ACK the peer sends for a probe carries
an unchanged ACK field, so it falls into the duplicate-ACK branch at
`:866` and a fast retransmit at `:894` burns a further `retx` credit
(WIN-04), reaching the abort in roughly 60 s.

**Failure.** A peer application stops reading for 70 seconds — a paused
media player, a logger blocked on disk, a process in a debugger — so
it advertises window 0 while continuing to ACK. The local writer
queues the one-byte probe; `head->retx` climbs on each RTO and on the
fast retransmits the probe ACKs provoke, and hits 6 at about
t = 60 s, whereupon `tcp_kill_pcb(p, ETIMEDOUT)` destroys a fully
healthy connection. `write()` returns `-EPIPE`. The peer resumes
reading at t = 70 s and finds the connection gone.

**Fix.** Mark the segment that was sent purely as a zero-window probe
(a flag on `tcp_seg_t`, set at `:1456`) and exempt it from the
`TCP_MAX_RETX` abort at `:497`: while `SND.WND == 0`, back the probe
interval off as now but clamp it to `TCP_RTO_MAX_TICKS` and keep
probing indefinitely. Restart the abort countdown only once the peer
has advertised a non-zero window and genuine unacked data is
outstanding.

### WIN-02 (high). A non-blocking sender never probes, and `POLLOUT` is asserted unconditionally

**Requirement.** RFC 793 3.7 (`docs/rfc/rfc793.txt:2694-2695`): *"The
sending TCP must be prepared to accept from the user and send at least
one octet of new data even if the send window is zero."*

**Code.** The only zero-window probe is the one-byte transmission at
`sys/net/tcp.c:1451-1459`, and the non-blocking early return sits
immediately *before* it: `if (nonblock) return sent ? (ssize_t)sent :
-EAGAIN;` at `:1450`. So the one path that satisfies the RFC's
requirement is reachable only from the blocking path.

Meanwhile `tcp_poll()` at `:1347-1350` sets `POLLOUT` for any
non-CLOSED, non-handshaking PCB with the comment *"No tx buffering
yet; treat as always-ready"*, and `afinet_node_poll()` hands that
straight back to userspace (`sys/net/af_inet.c:448-450`).

**Failure.** Two compounding failures. (1) A hot loop with no forward
progress and no wire activity: a non-blocking, `poll()`-driven server
(any event loop — an HTTP server, a proxy, libevent-style code) is
told the fd is writable on every iteration and gets `-EAGAIN` from
every `write()`, spinning at 100% CPU while the connection emits not
one segment. (2) Because the receiver-side window-update ACK
(`:1518-1525`) is a bare ACK sent once and never retransmitted — the
sort of unreliable single notification the RFC's probing requirement
exists to backstop — losing it deadlocks the connection *permanently*:
`in_flight == 0` means the unacked queue is empty, so
`tcp_timer_tick()` skips the PCB at `:491`, and neither side ever
times out.

**Fix.** Move the `in_flight == 0` probe above the non-blocking return
(or drive it from `tcp_timer_tick()` as a real persist timer) so the
octet goes out regardless of blocking mode, and gate `POLLOUT` in
`tcp_poll()` on real capacity — `min(snd_wnd, cwnd) > snd_nxt -
snd_una` — setting `*wait_chan = p->send_chan` when it is not
writable so `poll()` blocks instead of spinning.

### WIN-03 (high). The window-reopen ACK fires only when one single `read()` drains at least an MSS

**Requirement.** RFC 793 3.7, Window Management Suggestions
(`docs/rfc/rfc793.txt:2741-2745`): *"One suggestion for avoiding small
windows is for the receiver to defer updating a window until the
additional allocation is at least X percent of the maximum allocation
possible for the connection (where X might be 20 to 40)."* RFC 1122
4.2.3.3 turns this into the receiver SWS rule: advertise zero until at
least `min(MSS, buffer/2)` is available.

**Code.** `tcp_recv_nb()` is the only place that emits an unsolicited
window update. It snapshots `prev_count` before draining
(`sys/net/tcp.c:1502`), then at `:1518-1520`:

```c
size_t old_wnd = TCP_RING_LEN - prev_count;
size_t new_wnd = TCP_RING_LEN - p->rx_count;
if (new_wnd >= old_wnd + TCP_MSS && …)
```

Because `p->rx_count == prev_count - n` for this call's `n` bytes,
`new_wnd` is exactly `old_wnd + n` and the condition reduces
algebraically to `n >= TCP_MSS` — "this one `read()` consumed at least
1460 bytes". It is not "the window has grown by an MSS since the value
I last advertised", which is what the comment at `:1511-1517` intends
and what the RFC describes; the last advertised window is not recorded
anywhere (`p->rcv_wnd` at `:151` is assigned at `:669`/`:1138` and
never read).

**Failure.** A bulk transfer fills the 32 KiB ring while the reader is
briefly descheduled, so an ACK advertises window 0. The reader then
drains all 32768 bytes with 512- or 1024-byte `read()`s — any
stdio/telnet/line reader. Every call takes the `:1501` branch with
`n < 1460`, so no window update is ever transmitted; the ring is
empty and the peer still believes the window is shut. Nothing else
announces the reopening: the only other ACK generators (`:1030`,
`:935`) are reactive to an incoming segment, and a peer at zero window
sends nothing but persist probes. Against a Linux peer the transfer
stalls until its persist timer fires (the comment at `:1515` measures
this at "10+ seconds"); against a substrate peer the probe path hits
`TCP_MAX_RETX` and the connection is aborted (WIN-01).

**Fix.** Record the window last put on the wire in the PCB (set it in
`tcp_xmit_raw()`), and fire the update when
`(TCP_RING_LEN - rx_count) - last_adv_wnd >= min(TCP_MSS,
TCP_RING_LEN/2)` — measured against the advertised value, not against
the window at entry to this call. Fire it unconditionally on a 0 → non-zero
transition.

### WIN-04 (high). Fast retransmit shares the RTO backoff exponent and the abort budget, and refreshes `sent_tick`

**Requirement.** RFC 793 3.9, Timeouts, RETRANSMISSION TIMEOUT
(`docs/rfc/rfc793.txt:4734-4737`): *"For any state if the
retransmission timeout expires on a segment in the retransmission
queue, send the segment at the front of the retransmission queue
again, reinitialize the retransmission timer, and return."* RFC 6298
5.5 applies the exponential backoff only on timer expiry; RFC 5681 3.2
makes fast retransmit a distinct mechanism that must touch neither.

**Code.** `tcp_seg.retx` is overloaded as three things: the backoff
exponent (`sys/net/tcp.c:494`,
`rto = TCP_RTO_BASE_TICKS << head->retx`), the abort counter
(`:497-499`), and a plain retransmission count. `tcp_retx_head()`
(`:397-403`) unconditionally does `s->sent_tick = get_ticks(); s->retx++;`
and is the shared entry point for both the RTO path (`:516`) and the
fast-retransmit path (`:894`).

Three consequences. (1) Every fast retransmit doubles the surviving
RTO for that segment: after three dup-ACK recoveries the fallback
timeout has grown from 1 s to 8 s, the opposite of what fast
retransmit exists to achieve. (2) Six retransmissions from any mixture
of sources leave `head->retx == 6`, so the next timer expiry takes
`:497` and kills the connection with ETIMEDOUT although no
timeout-driven retransmission ever failed. (3) Because
`tcp_retx_head()` also refreshes `sent_tick`, the expiry test at
`:496` (`if (now - head->sent_tick < rto) continue;`) is never reached
while duplicate ACKs keep arriving — so the abort is *neutralized* and
`retx` has no upper bound at all. At `retx >= 64` the shift at `:494`
exceeds the width of `uint64_t`: undefined behaviour on wholly
wire-controlled input.

**Failure.** A peer sends three duplicate ACKs repeatedly. Each triple
takes `p->dup_ack` to `TCP_DUP_ACK_FAST` and fires `tcp_retx_head()`
at `:894`, resending up to a full MSS, bumping `retx` and refreshing
`sent_tick`. 192 such ACKs — well under a second on a 100 Mbit link —
produce 64 retransmits of one segment, ~96 KB emitted for ~7.7 KB
received, with the R2 abort never firing, and then a `<< 64` shift.
Combined with WIN-05's over-broad duplicate test, retransmissions the
wire never asked for can also drive a healthy connection to ETIMEDOUT.

**Fix.** Split the counter: an `rto_shift` incremented only in
`tcp_timer_tick()` on a genuine expiry, and a separate count (or an
RFC 6298 R2 deadline) for the abort decision. Give `tcp_retx_head()`
a flag distinguishing the two callers so the fast-retransmit path
leaves both the backoff and the budget untouched, cap the
fast-retransmit count per segment, and clamp the shift at `:494`
before applying it.

### WIN-05 (medium). Duplicate-ACK detection tests neither SEG.LEN nor SEG.WND

**Requirement.** The fast-retransmit policy here is the RFC 5681
substitution and the code names it (`sys/net/tcp.c:885`), so it is
audited against RFC 5681 3.2, whose §2 admits a segment as a duplicate
ACK only if, among other conditions, *"the incoming acknowledgment
carries no data"*, *"the SYN and FIN bits are both off"* and *"the
advertised window in the incoming acknowledgment equals the advertised
window in the last incoming acknowledgment"*. RFC 793's own wording is
narrower still: *"If the ACK is a duplicate (SEG.ACK < SND.UNA), it
can be ignored."*

**Code.** The test at `sys/net/tcp.c:866` is
`else if (ack == p->last_ack && p->unacked_head)` — it inspects
neither `dlen` nor `th->window`, and the ack equality is
`SEG.ACK == SND.UNA`, not `SEG.ACK < SND.UNA`.

**Failure.** Two shapes. (1) A pure window update — the exact segment
RFC 793 3.7 tells a receiver to send when its window reopens — repeats
the ACK number by construction, so three of them fire a fast
retransmit at `:894` and halve `cwnd` and `ssthresh` at `:889-893`.
RFC 5681's condition (e) exists precisely to exclude this. (2) On any
bidirectional connection the peer's own data segments carry a stale
ACK field while its delayed-ACK timer runs, so they are counted too: a
request/response protocol with one segment unacked and a five-segment
response has `dup_ack` reach 3 on the third response segment,
collapsing `cwnd` on a path with no loss at all and injecting
needless duplicate data.

**Fix.** Apply RFC 5681 §2's full test: require `dlen == 0`, no
SYN/FIN in `flags`, `ack == snd_una` with `snd_una != snd_nxt`, and
the segment's window equal to the previously advertised one — keeping
the previous window in the PCB alongside `last_ack`. A segment that
merely repeats the ACK but fails the test is a window update and must
leave the counter alone.

### WIN-06 (medium). No sender silly-window avoidance and no Nagle

**Requirement.** RFC 793 3.7, Window Management Suggestions
(`docs/rfc/rfc793.txt:2746-2748`): *"Another suggestion is for the
sender to avoid sending small segments by waiting until the window is
large enough before sending data. If the the user signals a push
function then the data must be sent even if it is a small segment."*
The RFC then describes the exact failure mode
(`:2757-2770`): *"If a segment containing a single data octet sent to
probe a zero window is accepted, it consumes one octet of the window
now available. If the sending TCP simply sends as much as it can
whenever the window is non zero, the transmitted data will be broken
into alternating big and small segments. … The suggestion here is that
the TCP implementations need to actively attempt to combine small
window allocations into larger windows."* RFC 1122 4.2.3.4 raises both
sender SWS avoidance and Nagle to MUSTs.

**Code.** `tcp_send_impl()` sizes each segment as
`chunk = min(len - sent, TCP_MSS, avail)`
(`sys/net/tcp.c:1471-1473`) and transmits immediately at `:1474`, with
no minimum-segment test and no coalescing of a small write against an
outstanding unacked segment. Any usable window, however small, is
consumed at once. Neither mechanism exists anywhere in the file.

**Failure.** A writer sends 32 KiB to a peer whose application reads
100 bytes at a time. The window reaches 0, the persist probe at
`:1456` places one octet, and from then on every ACK reopens the
window by roughly the 100 bytes just consumed. `:1473` takes
`avail = 100` and ships a 100-byte segment carrying 40 bytes of
TCP+IP header, then repeats: ~70% header overhead and one segment per
application read for the rest of the connection, with nothing in the
code able to climb back out. The same path makes an interactive client
emit one segment per keystroke.

**Fix.** Before the transmit at `:1474`, send only when the chunk is a
full MSS, or the usable window is at least half the largest window the
peer has ever advertised, or nothing is currently unacked (which also
preserves interactive latency), or the user's write is exhausted with
no unacked small segment outstanding. Track `max_snd_wnd` in the PCB
for the half-window condition.

### WIN-07 (medium). No receiver silly-window avoidance: the raw free-space count is advertised

**Requirement.** RFC 793 3.7 (`docs/rfc/rfc793.txt:2735-2740`):
*"Allocating a very small window causes data to be transmitted in many
small segments when better performance is achieved using fewer large
segments."*

**Code.** The window emitted at `sys/net/tcp.c:290` is
`(uint16_t)(TCP_RING_LEN - p->rx_count)` with no floor. Every segment
this stack sends — including the ACK generated for arriving data at
`:1030-1034` — publishes it verbatim, so when the ring is nearly full
the peer is invited to send 1..100 octets. The suppression in
`tcp_recv_nb()` (`:1520`) only withholds the *gratuitous* update ACK;
it does nothing about the value carried on the ACKs that go out
anyway. Implementing the rule requires remembering the last advertised
window in order not to shrink it — the field at `:151` that is never
read (WIN-03, WIN-11).

**Failure.** A slow consumer keeps the ring within a few hundred
octets of full. Each arriving segment is ACKed with, say, window=120;
the peer sends 120 octets; the next ACK advertises 60. The transfer
degenerates into the classic silly-window loop of tiny segments, each
costing a 40-byte header, while `cwnd` growth (bounded by `acked` at
`:858-865`) crawls.

**Fix.** Clamp the value at `:290`: advertise 0 unless the free space
is at least `min(TCP_MSS, TCP_RING_LEN/2)`, and never advertise a
value that would move `rcv_nxt + rcv_wnd` left of the last advertised
edge.

### WIN-08 (medium). No out-of-order reassembly queue

**Requirement.** RFC 793 3.9, first check sequence number
(`docs/rfc/rfc793.txt:4271`): *"Segments with higher begining sequence
numbers may be held for later processing."*

**Code.** There is no reassembly queue anywhere in `tcp.c` — the PCB
carries only the in-order ring (`rxbuf`/`rx_head`/`rx_tail`/
`rx_count`) and the unacked *send* FIFO. A segment that is acceptable
under the 3.3 test but not exactly at RCV.NXT is dropped at
`sys/net/tcp.c:813` and generates only a duplicate ACK at `:1030`.
`tcp_retx_head()` (`:397`) retransmits only the head of the unacked
queue, and fast retransmit re-arms after a single firing (`:895`), so
recovery from one loss is effectively one segment per dup-ACK burst or
per RTO. The header comment at `:168-169` refers to "TCP-11's missing
reassembly queue", so the gap is known.

"May be held" makes outright discard formally permissible, so this is
an omission rather than a violation — but it is the dominant term in
loss-recovery cost on this stack.

**Failure.** ESTABLISHED with a 64 KiB peer window. The peer sends
S1..S45 back to back and S1 is dropped. S2..S45 all arrive above
`rcv_nxt`, so each is discarded at `:813` and each produces a
duplicate ACK. Three duplicates fast-retransmit S1; S1 arrives and
`rcv_nxt` advances by one MSS — but S2..S45 were never stored, so the
peer must re-send all 44, at one segment per recovery event because
nothing here ever acknowledges past the hole. ~64 KiB of
already-delivered data is re-sent and the transfer stalls for tens of
round trips, with RFC 5681 `cwnd` collapse (`:508-515`) applied on
top.

**Fix.** Add a bounded per-PCB out-of-order queue — capped by bytes
(one receive window's worth) and by segment count so a remote peer
cannot grow it — holding acceptable segments above RCV.NXT, drained
into `rxbuf` whenever `rcv_nxt` advances.

### WIN-09 (medium). A partially acknowledged segment is retransmitted verbatim from below SND.UNA

**Requirement.** RFC 793 3.9 (`docs/rfc/rfc793.txt:4312-4315`): *"A
segment on the retransmission queue is fully acknowledged if the sum
of its sequence number and length is less or equal than the
acknowledgment value in the incoming segment."* 3.7
(`:2689-2692`) tells implementations to be prepared for peers that
shrink the window.

**Code.** `tcp_unacked_prune()` (`sys/net/tcp.c:368-384`) correctly
refuses to free a partly covered segment
(`if ((int32_t)(end - ack) > 0) break;`, `:377`) — but nothing ever
trims it: the entry keeps its original `s->seq` and `s->dlen`.
`tcp_retx_head()` (`:397-403`) then retransmits it in full from
`s->seq`, which after the partial ACK is strictly below `SND.UNA`
(set to the partial ack at `:847`).

This implementation's own receiver produces partial ACKs directly:
`tcp_in_established()` clamps acceptance to the free ring space
(`:814-816`) and advances `rcv_nxt` by that partial count. The
retransmission is then fatal rather than wasteful, because the receive
path requires exact equality (`:813`): a retransmission starting below
the peer's RCV.NXT is discarded whole, including the still-missing
tail it carries.

**Failure.** `SND.UNA = 1000` with a queued 1460-byte segment at
seq 1000. The peer's window has shrunk to 500, so it accepts
`[1000,1500)` and ACKs 1500. `prune()` keeps the entry; `snd_una`
becomes 1500. The RTO expires and `tcp_retx_head()` resends
`seq=1000, len=1460`. The peer's `rcv_nxt` is 1500, so the whole
segment is dropped; bytes 1500..2459 are never delivered; after six
such retries the connection is aborted with ETIMEDOUT.

**Fix.** In `tcp_unacked_prune()`, when a segment is partially
covered, advance it: `memmove` the payload up by `(ack - s->seq)`, set
`s->seq = ack` and reduce `s->dlen` by the same amount, clearing SYN
if it was the acknowledged part. Equivalently, have `tcp_retx_head()`
transmit only `[snd_una, s->seq + s->dlen)`.

### WIN-10 (medium). After `shutdown(SHUT_RD)` data is still accepted but never drained, pinning the window at zero

**Requirement.** RFC 793 3.9, seventh step: *"Once the TCP takes
responsibility for the data it advances RCV.NXT over the data
accepted, and adjusts RCV.WND as apporopriate to the current buffer
availability."* And 3.7: *"This retransmission is essential to
guarantee that when either TCP has a zero window the re-opening of the
window will be reliably reported to the other."*

**Code.** `tcp_shutdown_rd()` sets `p->shut_rd`
(`sys/net/tcp.c:1732`) and `tcp_recv_nb()` then returns EOF at
`:1497-1500` *before* touching the ring, so `rx_count` can never
decrease again. The receive path has no matching check:
`tcp_in_established()` keeps copying arriving data into the ring at
`:813-822` and keeps taking sequence-space responsibility for it by
advancing `rcv_nxt`. Within one ring-full the window computed at
`:290` reaches 0 and stays there permanently. The socket layer mirrors
the same early return (`sys/net/af_inet.c:564`), so nothing at either
layer consumes the data.

**Failure.** A process calls `shutdown(fd, SHUT_RD)` on a live
connection and keeps the fd open. The peer continues sending: 32 KiB
is buffered into a ring no one will ever read, and every ACK from then
on advertises window 0 — the re-opening the RFC says must be reliably
reported never happens. The peer's `write()` blocks until its own R2
abort, and a FIN sent with trailing data can never satisfy `:931`, so
the close never completes either. 32 KiB of kernel ring is held per
such connection until the fd is closed.

**Fix.** On SHUT_RD, discard arriving data instead of buffering it:
drop the ring contents, and in the accept path at `:813` advance
`rcv_nxt` over the data without storing it so the window stays open —
the BSD behaviour of acknowledging and discarding after a read-side
shutdown.

### WIN-11 (medium). The advertised right edge can move left

**Requirement.** RFC 793 3.9, seventh step: *"The total of RCV.NXT and
RCV.WND should not be reduced."* 3.7 adds: *"This, so called
'shrinking the window,' is strongly discouraged."*

**Code.** `tcp_xmit_raw()` reads `p->rcv_nxt` into `th->ack_seq` at
`sys/net/tcp.c:288` and then reads `p->rx_count` for `th->window` at
`:290` as two separate unprotected loads. The pair defines the
advertised right edge, and the invariant that keeps it from retreating
(`rcv_nxt += n` alongside `rx_count += n` at `:821-822`) holds only if
both are sampled at the same instant. They are not: `tcp_xmit_queue()`
transmits with IRQs deliberately enabled (`:358-360`), and
`tcp_recv_nb()` releases the lock at `:1510` before calling
`tcp_send_ctl()` at `:1524`.

**Failure.** The application `read()`s, `tcp_recv_nb()` unlocks at
`:1510` and calls `tcp_send_ctl()`; `tcp_xmit_raw()` latches
`ack_seq = rcv_nxt` at `:288`; the RX path then delivers a 1460-byte
in-order segment, executing `:821-822`; `:290` reads the new, smaller
`rx_count`. The emitted ACK pairs the OLD `rcv_nxt` with the NEW
window, so the right edge it advertises is 1460 octets left of the
previous one. A peer that tracks its usable window as
`SEG.ACK + SEG.WND` from the highest ACK — the RFC 793 3.7 refinement
this stack itself implements at `:1092-1097` — then under-sends; with
a nearly full ring the torn value can read 0 while free space exists,
and per WIN-03 no later update may ever correct it.

**Fix.** Sample `rcv_nxt` and the window together under the netstack
lock in `tcp_xmit_raw()` (or pass a snapshot in), and derive the
window from a stored `rcv_wnd` that is only ever moved so that
`rcv_nxt + rcv_wnd` is non-decreasing.

### WIN-13 (medium). No user timeout: a PCB with an empty retransmission queue has no deadline at all

**Requirement.** RFC 793 3.9, Timeouts, USER TIMEOUT
(`docs/rfc/rfc793.txt:4725-4729`): *"For any state if the user timeout
expires, flush all queues, signal the user 'error: connection aborted
due to user timeout' in general and for any outstanding calls, delete
the TCB, enter the CLOSED state and return."* 3.8 makes it a
per-connection parameter supplied to OPEN and modifiable by SEND.

**Code.** Substrate implements neither half. There is no user-timeout
field on `tcp_pcb_t` (`sys/net/tcp.c:143-210`), no socket option that
could set one (`grep -rn 'SO_RCVTIMEO\|SO_SNDTIMEO' sys/` finds
nothing), and `tcp_timer_tick()` has only three bounded paths: the
FIN_WAIT_2 deadline (`:479-483`), the TIME_WAIT deadline (`:484-489`),
and the per-segment retransmission budget. That budget is keyed
entirely on the unacked queue: `:490-491` is
`tcp_seg_t *head = p->unacked_head; if (!head) continue;`, so any PCB
with nothing outstanding escapes every timeout in the system.

The per-segment budget is also not the connection-level R2 the header
comment claims (`:78-85`: *"RFC 1122 4.2.3.5 wants the total budget
(R2) to be generous before abort … TCP_MAX_RETX of 6 gives
1+2+4+8+16+32 = 63s"*). `retx` lives in the individual `tcp_seg_t`
(`:139`) and `tcp_xmit_queue()` initialises it to 0 for every new
segment (`:331`), so the budget restarts from zero each time the head
is acknowledged and the next segment becomes head — and so does the
backoff at `:494`.

**Failure.** (a) A process blocks in `read()` on a connection whose
peer is powered off mid-stream, so no RST or FIN ever arrives and our
side has already ACKed everything (`unacked_head == NULL`).
`tcp_timer_tick()` `continue`s at `:491` on every tick forever;
`tcp_recv()` re-checks, finds ESTABLISHED, sleeps, repeats. `read()`
never returns and no `SO_RCVTIMEO` exists to bound it; only a signal
escapes. (b) On a lossy path where every segment needs five
retransmissions but each eventually gets through, throughput is a few
bytes per minute and the connection never aborts, holding the PCB and
its 32 KiB ring for as long as the peer trickles ACKs.

**Fix.** Add a per-PCB deadline armed when the queue goes from empty
to non-empty (and re-armed only when an ACK advances `snd_una`),
checked unconditionally *before* the `if (!head) continue;` at `:491`,
defaulted per RFC 1122 4.2.3.5's R2, and settable from userspace via
`SO_RCVTIMEO`/`SO_SNDTIMEO` or `TCP_USER_TIMEOUT`.

### WIN-12 (low). SND.WL1 and SND.WL2 do not exist

**Requirement.** RFC 793 3.9, fifth check the ACK field, ESTABLISHED:
*"If SND.UNA < SEG.ACK =< SND.NXT, the send window should be updated.
If (SND.WL1 < SEG.SEQ or (SND.WL1 = SEG.SEQ and SND.WL2 =< SEG.ACK)),
set SND.WND <- SEG.WND, set SND.WL1 <- SEG.SEQ, and set SND.WL2 <-
SEG.ACK."* The RFC's own note: *"The check here prevents using old
segments to update the window."* 3.7 explains why the ack alone is
insufficient (`docs/rfc/rfc793.txt:2723-2727`): *"In a connection with
a one-way data flow, the window information will be carried in
acknowledgment segments that all have the same sequence number so
there will be no way to reorder them if they arrive out of order."*

**Code.** `struct tcp_pcb` (`sys/net/tcp.c:143-210`) has no
`snd_wl1`/`snd_wl2`. The update at `:1092-1098` orders solely on
SEG.ACK, and its lower bound is `(int32_t)(ack - p->snd_una) >= 0` —
i.e. `SND.UNA =< SEG.ACK`, not the `SND.UNA < SEG.ACK` the RFC
requires for a window update — so two ACKs carrying the same
acknowledgment number but different windows (exactly what a pure
window update is) are indistinguishable, and whichever arrives last
wins regardless of which was sent last. SEG.SEQ is not consulted at
all, and the update runs in `tcp_input()` before the state dispatch
and before any sequence validation.

**Failure.** The peer emits `ACK(SEG.ACK=X, WND=0)` because its buffer
momentarily filled, then drains and emits `ACK(SEG.ACK=X, WND=65535)`.
The two are reordered in the network. Both pass `:1095`, so
`p->snd_wnd` ends at 0. In `tcp_send_impl()` (`:1440-1442`) `avail`
becomes 0 and, because `in_flight != 0`, the writer parks on
`send_chan` at `:1464` with no probe of its own (the probe at `:1456`
runs only when `in_flight == 0`). The write side stalls until the peer
happens to send another ACK; a forged duplicate does the same with one
packet.

**Fix.** Add `snd_wl1`/`snd_wl2` to `tcp_pcb_t`, initialise them from
the segment that establishes the connection, gate the assignment at
`:1097` on `(int32_t)(snd_wl1 - seq) < 0 || (snd_wl1 == seq &&
(int32_t)(snd_wl2 - ack) <= 0)`, update both on every accepted update,
and tighten the lower bound to `SND.UNA < SEG.ACK`.

### WIN-14 (info). The RTO is never derived from a measured round-trip time

**Requirement.** RFC 793 3.7, Retransmission Timeout
(`docs/rfc/rfc793.txt:2610-2614`): *"Because of the variability of the
networks that compose an internetwork system and the wide range of
uses of TCP connections the retransmission timeout must be dynamically
determined."*

**Code.** The RTO is `TCP_RTO_BASE_TICKS == 1 * HZ`
(`sys/net/tcp.c:88`) shifted left by the retransmission count
(`:494`) and clamped at 60 s (`:89`). Nothing measures elapsed time:
`tcp_seg_t` carries `sent_tick` (`:137`) but `tcp_unacked_prune()`
(`:368-384`) discards the segment without reading it, and the PCB has
no SRTT/RTTVAR fields. Both RFC 793 3.7's illustrative procedure and
its RFC 6298 replacement are absent. The 1 s base is RFC 6298 3.1's
mandated initial value, which is the correct starting point; the
estimator that should replace it after the first measurement never
does.

Reported at **info** because it is a known, documented gap: the
comment at `:85-86` says so in terms (*"A full SRTT/RTTVAR estimator
is still absent and remains on the task"*), as does the header list at
`:27`.

**Failure.** On loopback or a switched LAN a single dropped segment
that does not produce three duplicate ACKs — the last segment of a
response, or any loss with no data queued behind it — costs a full
second before `:516` retransmits, three orders of magnitude more than
a measured RTO. On a path whose RTT exceeds 1 s, every segment is
spuriously retransmitted on its first attempt, and each spurious
retransmit collapses `cwnd` to one MSS at `:513`.

**Fix.** Sample the RTT in `tcp_unacked_prune()` from the segment's
`sent_tick` when `retx == 0` (Karn's algorithm), maintain SRTT and
RTTVAR per RFC 6298 2.2-2.3, and compute
`RTO = SRTT + max(G, 4*RTTVAR)` clamped to `[1 s,
TCP_RTO_MAX_TICKS]`, applying the backoff at `:494` on top of the
computed value for timer expiries only.

---

# Part 3 — the wire header (RFC 793 3.1) and sequence numbers (3.3)

### HDR-01 (high). The pseudo-header source address is not the address `ip4_output()` stamps

**Requirement.** RFC 793 3.1, Checksum
(`docs/rfc/rfc793.txt:1168-1176`): *"The checksum also covers a 96 bit
pseudo header conceptually prefixed to the TCP header. This pseudo
header contains the Source Address, the Destination Address, the
Protocol, and TCP length. This gives the TCP protection against
misrouted segments. This information is carried in the Internet
Protocol and is transferred across the TCP/Network interface in the
arguments or results of calls by the TCP on the IP."*

**Code.** `tcp_xmit_raw()` builds the pseudo header with
`uint32_t csum_src = p->laddr ? p->laddr : ip4_source_for(p->raddr);`
(`sys/net/tcp.c:302`) and checksums with it (`:303`), then hands the
segment to `ip4_output()` (`:304`). `ip4_output()` takes no
source-address argument: it re-runs `route_for_v4()` on the
destination and writes `ih->saddr = dev->ip4_addr`
(`sys/net/inet.c:214`, `:248`). The two agree only when `p->laddr`
happens to equal the routed interface's address.

The TCP-29 comment directly above (`sys/net/tcp.c:294-301`) states the
correct rule — *"the pseudo-header source must be the address
ip4_output will put in the IP header, not p->laddr. On a multihomed
host they differ"* — but the fix it applied covers only the
`p->laddr == 0` case, and both `bind()` and `connect()` make
`p->laddr` non-zero before any segment is sent: `afinet_bind()` stores
the caller's address via `tcp_bind()`
(`sys/net/af_inet.c:910-913`), and for an unbound socket
`tcp_connect_start()` fills `p->laddr` from "the first netdev with an
`ip4_addr`" (`sys/net/tcp.c:1255-1267`) using a selection rule that
differs from `route_for_v4()` — it tests neither `NETDEV_IFF_UP` nor
subnet match and ignores the gateway route entirely.

`tcp_send_rst()` has the same defect and no fallback at all: it
checksums with `daddr`, the destination of the *offending* segment
(`sys/net/tcp.c:615`), while `ip4_output()` at `:616` re-routes toward
the peer and may pick a different device.

Because the checksum is the only end-to-end integrity check RFC 793
defines, a mismatch is not degraded service: the peer discards 100% of
the connection's segments and nothing on either side reports why —
the sender never learns, and a receiving substrate discards at
`:1070` without reply.

**Failure.** Single-NIC box (eth0 10.0.2.15/24 gw 10.0.2.2, lo
127.0.0.1). `bind(fd, {AF_INET, 0, 127.0.0.1})` then
`connect(fd, {AF_INET, 22, 10.0.2.2})`. `afinet_bind` →
`tcp_bind` sets `p->laddr = 127.0.0.1`, so `tcp_connect_start`'s
`if (!p->laddr)` guard at `:1255` is skipped. `tcp_xmit_raw`
checksums the SYN over pseudo header (127.0.0.1, 10.0.2.2) while
`ip4_output` stamps `ih->saddr = 10.0.2.15`. The peer recomputes from
the wire addresses, the checksum fails, it silently discards the SYN
and every retransmission, and `connect()` returns ETIMEDOUT ~63 s
later with no counter, no log and no ICMP. The multihomed case needs
no `bind()` at all: with eth0 (10.0.2.15/24, no gateway) and eth1
(192.168.1.5/24, gw 192.168.1.1), `connect()` to 8.8.8.8 makes
`tcp_connect_start` pick 10.0.2.15 (first netdev with an address)
while `route_for_v4()` returns eth1. And on a multihomed host every
unmatched-segment RST ships an invalid checksum — precisely when a
RST matters most.

**Fix.** Give the IPv4 output path an explicit source address (e.g.
`ip4_output_from(saddr, daddr, proto, payload, len)`, with
`ip4_output()` keeping today's behaviour by passing 0 for "let routing
choose"), and pass from both `tcp_xmit_raw()` (`:303`) and
`tcp_send_rst()` (`:615`) the very address they fed to
`inet_csum_pseudo4()`. Until that exists, at minimum make
`tcp_connect_start()` select `p->laddr` by calling
`ip4_source_for(raddr)` so it cannot disagree with `route_for_v4()`,
and have `afinet_bind()`/`tcp_bind()` reject with EADDRNOTAVAIL a
local address that routing will not honour.

### HDR-02 (high). TCP options are never parsed

**Requirement.** RFC 793 3.1, Options
(`docs/rfc/rfc793.txt:1219`): *"A TCP must implement all options."*
Of the MSS option: *"If this option is present, then it communicates
the maximum receive segment size at the TCP which sends this
segment."* RFC 1122 4.2.2.6 is equally explicit and equally
unimplemented, so this is not a case of following a later RFC instead.

**Code.** `tcp_input()` computes the header length at
`sys/net/tcp.c:1046`, bounds-checks it at `:1047`, and then jumps
straight over the option area: `dlen = len - hlen` (`:1053`) and
`payload = seg + hlen` (`:1054`). No byte between `seg + 20` and
`seg + hlen` is ever examined. There is no option-parsing loop
anywhere in the kernel — `grep -rn 'TCPOPT\|tcp_dooptions\|opt_kind'
sys/` returns nothing — and `tcp_pcb_t` (`:143-210`) has no
`snd_mss`/`rcv_mss` field, so there is nowhere to store a received MSS
even if it were read. Neither `tcp_in_listen()` (which builds the
child PCB at `:655-677`) nor `tcp_in_syn_sent()` is handed the option
bytes. The send size is the compile-time `TCP_MSS` of 1460 (`:70`),
clamped at `:283`, `:325` and `:1472`.

Two things the audit brief asked about, checked and clear: the classic
zero-length / over-long option-length loop is **not** present, because
no parse loop exists at all; the `hlen < sizeof(*th) || hlen > len`
guard at `:1047` is correct (Data Offset is 4 bits so `hlen <= 60`, and
`dlen` cannot underflow); and the checksum at `:1070` does cover the
option bytes, which is right.

**Failure.** A peer behind a PPPoE or IPsec tunnel, or a small
embedded stack, sends SYN with `<kind=2,len=4,mss=536>`. Substrate
ignores it, completes the handshake, and then emits 1460-byte segments
(1500-byte IP datagrams). `ip4_output()` does not fragment
(`sys/net/inet.c:244` sets `frag_off = 0`), so every full-size segment
is dropped by the first smaller-MTU hop. The unacked queue never
drains, the RTO ladder runs to `TCP_MAX_RETX`, and the connection
aborts with ETIMEDOUT — a `connect()` that succeeds followed by a
`write()` that hangs and dies, with no diagnostic.

**Fix.** Add a bounded option walk over `seg[20 .. hlen)` on segments
with SYN set: kind 0 (EOL) stops, kind 1 (NOP) advances one byte, any
other kind requires at least 2 remaining bytes and a length byte in
`[2, remaining]` (this is the bound that prevents the zero-length
loop), kind 2 with length 4 yields the MSS. Store it in a new
`snd_mss` field defaulted to 536 per RFC 1122 4.2.2.6 for a remote
peer, and use it in place of `TCP_MSS` at `:283`, `:325` and `:1472`.

### HDR-03 (medium). The MSS option is never sent

**Requirement.** RFC 793 3.1, Maximum Segment Size
(`docs/rfc/rfc793.txt:1251-1254`): *"This field must only be sent in
the initial connection request (i.e., in segments with the SYN control
bit set). If this option is not used, any segment size is allowed."*

**Code.** Every segment substrate transmits is built with a 20-byte
header. `tcp_xmit_raw()` writes
`th->doff_flags = __builtin_bswap16((uint16_t)((5u << 12) | flags))`
at `sys/net/tcp.c:289`, and the two RST builders do the same at `:608`
and `:613`. Those are the only three sites that set `doff_flags` for
output, and none is SYN-aware. The transmit buffer is declared
`uint8_t buf[TCP_MSS + sizeof(struct tcphdr)]` (`:282`), exactly sized
for a 20-byte header with no room for options. So both the SYN from
`tcp_connect_start()` (`:1276`) and the SYN|ACK from `tcp_in_listen()`
(`:677`) carry an empty option area.

**Failure.** A Linux or BSD client connects to a substrate server over
Ethernet. The SYN|ACK carries no MSS option, so the client applies the
RFC 1122 default send MSS of 536 and ships the whole session in
536-byte segments instead of 1460 — roughly 2.7× the segment count for
the life of every inbound transfer. In the other direction, "any
segment size is allowed" means a peer on a jumbo-frame or
loopback-style path may legitimately send segments larger than 1460;
those are accepted safely, since the ring copy is clamped at
`:815-816`, so this is an interop and throughput defect, not a
memory-safety one. Both RST paths correctly stay at Data Offset 5.

**Fix.** Give `tcp_xmit_raw()` an options-length parameter (or
special-case `flags & TCP_SYN`): emit `<kind=2,len=4,mss=…>`, set the
data offset to 6, and enlarge `buf[]` at `:282` by the option bytes.
The 4-byte option keeps the header 32-bit aligned, so no NOP/EOL
padding is needed.

### HDR-04 (medium). `TCP_MSS` is never clamped to the outgoing interface MTU, and no layer below enforces `dev->mtu`

**Requirement.** RFC 793 3.1: *"If this option is not used, any
segment size is allowed."* 793 supplies no path-MTU mechanism of its
own — the MSS option is the only segment-size control it defines — and
HDR-02/HDR-03 show it absent in both directions, leaving this constant
as the sole and unconditioned bound.

**Code.** `#define TCP_MSS 1460` (`sys/net/tcp.c:70`) hardcodes the
assumption of a 1500-byte link MTU minus 20-byte IPv4 and TCP headers.
It is the only bound on outbound segment size (`:283`, `:325`,
`:1472`) and nothing derives it from, or clamps it against, the
route's netdev. Nothing beneath TCP enforces the device MTU either:
`ip4_output` checks only
`payload_len > NETDEV_MTU_MAX - sizeof(struct iphdr)`
(`sys/net/inet.c:231`, i.e. 1580) and `eth_send` only
`payload_len > NETDEV_MTU_MAX` (`:133`, i.e. 1600). The `mtu` field
exists on `netdev_t` (`sys/include/sys/netdev.h:39`, defaulted to 1500)
and is writable by `SIOCSIFMTU` down to 68
(`sys/net/af_inet.c:366-367`), but it is read at exactly one place in
the whole kernel — `SIOCGIFMTU` at `sys/net/af_inet.c:363` — and never
on any transmit path. Output fragmentation does not exist
(`sys/net/inet.c:244`).

**Failure.** An operator runs `ifconfig eth0 mtu 576` (accepted, since
576 >= 68). TCP is unaffected and keeps clamping to 1460;
`ip4_output` builds a 1500-byte datagram (passes, 1480 <= 1580);
`eth_send` builds a 1514-byte frame (passes, 1500 <= 1600); the driver
hands a frame to a link that cannot carry it. Every full-sized data
segment is lost while the 40-byte handshake and pure ACKs get through,
so the connection establishes and then stalls until `TCP_MAX_RETX`.
The reverse case is wrong but harmless: loopback advertises MTU 16384
(`sys/net/loopback.c:124`) and TCP still uses 1460.

**Fix.** Resolve the route once at connection setup (`route_for_v4` /
`ip4_source_for` already exist at `sys/net/inet.c:205-209`) and cache
`dev->mtu - sizeof(struct iphdr) - sizeof(struct tcphdr)` in the PCB
as the effective send MSS, min'd with any peer-advertised value from
HDR-02. Independently, make `eth_send` reject
`payload_len > dev->mtu` so an oversized frame fails loudly at the
link layer.

### HDR-05 (medium). The ISN is raw CSPRNG output with no clock component

**Requirement.** RFC 793 3.3, Initial Sequence Number Selection
(`docs/rfc/rfc793.txt:1790-1793`): *"When new connections are created,
an initial sequence number (ISN) generator is employed which selects a
new 32 bit ISN. The generator is bound to a (possibly fictitious) 32
bit clock whose low order bit is incremented roughly every 4
microseconds."* The uniqueness argument rests on that monotonicity:
*"Since we assume that segments will stay in the network no more than
the Maximum Segment Lifetime (MSL) … we can reasonably assume that
ISN's will be unique."*

**Code.** `tcp_new_iss()` (`sys/net/tcp.c:265-271`) returns four bytes
straight from `random_get_bytes()`, falling back to an LCG over
`tcp_iss_seed`. It is used for both the active open
(`tcp_connect_start`, `:1270`) and the passive open
(`tcp_in_listen`, `:663`). There is no clock component, so the ISN of
a new connection bears no ordered relationship to the previous
incarnation of the same 4-tuple; on average half the time the new ISS
lands *below* the old incarnation's sequence space.

RFC 6528, which supersedes 793 here for security, does not license
pure randomness either: it specifies
`ISN = M + F(localip, localport, remoteip, remoteport, secret)`,
keeping the 4-microsecond timer `M` for exactly this reason. The
TCP-08 comment at `:250-263` documents the unpredictability motive but
not this trade-off. It matters more here than on most stacks, because
the other half of 3.3's protection — TIME-WAIT holding the 4-tuple —
is itself defeatable (API-04, MEM-09).

**Failure.** A 4-tuple is reused inside 2 MSL, which this stack allows
via the unchecked `tcp_bind()` and via a TIME_WAIT PCB that is
invisible to the EADDRINUSE test. The old incarnation had advanced to
sequence 0x1000_0000 with segments still in flight; the new
incarnation draws an ISS just above it. A delayed duplicate from the
old incarnation then falls squarely inside the new connection's
receive window and `tcp_in_established()` copies its payload into the
ring as current stream data (`:813-824`) — exactly the confusion 793's
clock-bound generator and the three-way handshake exist to prevent.

**Fix.** Implement RFC 6528: keep a boot-time secret key and return
`M + F(laddr, lport, raddr, rport, secret)`, where `M` is a monotonic
counter derived from `get_ticks()` scaled to roughly 4 µs and `F` is a
keyed hash. That keeps the unpredictability the current code has while
restoring the per-tuple ordering 3.3 depends on.

---

# Part 4 — the urgent mechanism (RFC 793 3.1, 3.7, 3.8, 3.9 step 6)

Six findings. This is a complete mechanism, mandatory in 793 and still
mandatory in RFC 1122 4.2.2.4 and RFC 9293 3.8.5 (RFC 6093 deprecates
*using* urgent data in new applications but still requires
implementations to support it), and it is absent end to end. It is
reported here rather than in Part 7 because URG-03 and URG-04 are not
omissions: they are active misbehaviour on defined API calls.

### URG-01 (high). The receive path never examines the URG bit

**Requirement.** RFC 793 3.9, sixth check the URG bit: *"If the URG
bit is set, RCV.UP <- max(RCV.UP,SEG.UP), and signal the user that the
remote side has urgent data if the urgent pointer (RCV.UP) is in
advance of the data consumed. If the user has already been signaled
(or is still in the 'urgent mode') for this continuous sequence of
urgent data, do not signal the user again."* 3.7 makes the delivery
obligation explicit: *"The TCP must asynchronously inform the user."*

**Code.** Step six is absent in its entirety. `sys/net/tcp.c:1048`
decodes the flag byte, which carries `TCP_URG`, and nothing downstream
tests that bit: `grep -rn TCP_URG sys/` finds only the `#define` at
`sys/include/netinet/tcp.h:27`, and `th->urg_ptr` appears only at its
declaration (`netinet/tcp.h:17`) and the one hardwired write at
`sys/net/tcp.c:292`. `tcp_input()` (`:1040-1124`) never byte-swaps or
reads the urgent pointer, so SEG.UP is not even extracted.
`tcp_pcb_t` (`:143-210`) has no RCV.UP field — `snd_una`, `snd_nxt`,
`rcv_nxt`, `rcv_wnd`, `snd_wnd`, `cwnd`, `ssthresh` are all present;
RCV.UP and SND.UP are the two RFC 793 TCB variables that are missing.
`tcp_in_established()` therefore queues urgent octets into `rxbuf`
exactly like ordinary data.

There is no notification channel either. `tcp_poll()` (`:1312-1361`)
never sets `POLLPRI`, so the `select()` exceptfds path in
`sys/kern/syscall.c` (which does map exceptfds ↔ POLLPRI) can never
fire on a TCP socket; no `SIGURG` is raised anywhere in `sys/net`
(`grep -rn SIGURG sys/net/` returns nothing, although
`sys/include/sys/fcntl.h:41` advertises `F_SETOWN` as the
"SIGIO/SIGURG owner"); and `SIOCATMARK` is not handled by
`afinet_ioctl`, falling to `default: return -ENOTTY;` at
`sys/net/af_inet.c:424`. Consequently RFC 793 3.8 RECEIVE's boundary
rule — *"data following the urgent pointer (non-urgent data) cannot be
delivered to the user in the same buffer with preceeding urgent
data unless the boundary is clearly marked for the user"* — is also
unimplementable: `tcp_recv_nb()` (`:1492-1537`) drains an arbitrary
contiguous run of the ring with no notion of a mark.

**Failure.** A peer sends `<SEQ=rcv_nxt><CTL=ACK,URG><URG_PTR=1>`
carrying one octet of urgent data — the telnet IAC/IP/DM interrupt
sequence, or an FTP ABOR mark — while 30 KB of bulk data is already
queued in the 32 KiB ring. `tcp_input` decodes `TCP_URG` into `flags`
and discards it; `tcp_in_established` appends the octet at the tail of
the ring behind the backlog. The application receives no `SIGURG`,
`select(exceptfds)` and `poll(POLLPRI)` never report the fd, and
`SIOCATMARK` fails with ENOTTY, so it sees the interrupt byte only
after reading the entire backlog ahead of it — precisely the backlog
the urgent mechanism exists to bypass. Interactive interrupt and flush
in telnet, rlogin and ftp are inoperative against this stack. The
bytes do still arrive in order, which is what `SO_OOBINLINE` would
give; it is the out-of-band *signal* that is lost.

**Fix.** Add `rcv_up` to `tcp_pcb_t`, extract SEG.UP in `tcp_input`
(`__builtin_bswap16(th->urg_ptr)`), and implement step six between the
ACK and text processing: on `TCP_URG` set
`rcv_up = max(rcv_up, seq + SEG.UP)` and, if `rcv_up` is in advance of
the data already consumed and the user has not yet been signalled for
this run, raise `SIGURG` to the socket's `F_SETOWN` owner and set
`POLLPRI` in `tcp_poll`. Clamp SEG.UP against the segment length
before use, since it is wire-controlled. Then make `tcp_recv_nb` stop
the copy at the mark, and add `SIOCATMARK` to `afinet_ioctl`.

### URG-02 (high). The send path can never set URG

**Requirement.** RFC 793 3.9, SEND Call, ESTABLISHED / CLOSE-WAIT:
*"If the urgent flag is set, then SND.UP <- SND.NXT-1 and set the
urgent pointer in the outgoing segments."* 3.7 adds that once armed
the pointer must be carried forward: *"The method employs a urgent
field which is carried in all segments transmitted."*

**Code.** `tcp_xmit_raw()` (`sys/net/tcp.c:280-307`) is the single
point through which every outbound segment is built, and it
unconditionally writes `th->urg_ptr = 0;` at `:292`. The `flags`
argument comes from `tcp_send_ctl()` (`:309-311`), `tcp_xmit_queue()`
(`:323-366`), `tcp_retx_head()` (`:397-407`) and the direct callers in
the state handlers, and `TCP_URG` is never among the bits any of them
pass. `tcp_pcb_t` has no `snd_up` member, so there is nowhere to
record SND.UP even if a caller wanted to. This stack cannot emit a
segment with URG set under any circumstance.

**Failure.** An FTP client on this stack issues the standard
out-of-band ABOR to cancel a large transfer. `tcp_xmit_raw` emits an
ordinary data segment with URG clear and `urg_ptr` 0. The RFC 793
server never enters urgent mode, never scans ahead for the ABOR, and
keeps streaming the file; the control connection stays blocked behind
the data transfer until it times out. The same failure breaks telnet's
interrupt-and-flush and rlogin's out-of-band window-change and
flow-control messages.

**Fix.** Add `snd_up` to `tcp_pcb_t`, thread an `urg` flag from the
socket layer through `tcp_send_impl`/`tcp_xmit_queue` to
`tcp_xmit_raw`, and when set do `SND.UP <- SND.NXT-1`, OR `TCP_URG`
into the flag word, and write
`th->urg_ptr = __builtin_bswap16(snd_up - seq + 1)` (the RFC 1122
last-octet form — see URG-06) on that segment and on every subsequent
one until SND.UNA passes SND.UP, including retransmissions out of
`tcp_retx_head`.

### URG-03 (medium). `send(..., MSG_OOB)` is accepted, discarded, and reported as success

**Requirement.** RFC 793 3.8, SEND: *"If the URGENT flag is set,
segments sent to the destination TCP will have the urgent pointer set.
The receiving TCP will signal the urgent condition to the receiving
process if the urgent pointer indicates that data preceding the urgent
pointer has not been consumed by the receiving process."*

**Code.** `MSG_OOB` is the socket-API spelling of that URGENT flag,
and the userland header advertises it (`include/sys/socket.h:75`
defines `MSG_OOB 0x0001`), so ported software compiles against it and
uses it. The kernel throws it away: `sys_send`/`sys_sendto_impl`/
`sys_sendmsg` (`sys/net/af_unix.c:1671`, `:1925`, `:2119`) pass
`flags` through unmodified to `afinet_sendto`, whose kernel-buffer
core opens with `(void)flags;` at `sys/net/af_inet.c:1265` and then,
for a stream socket, calls `tcp_send(s->tcp, buf, len)` (`:1275`) —
the ordinary in-band path. The call returns the byte count.

This is worse than an outright `ENOTSUP`: a silent success gives the
application no way to discover the mechanism is missing and fall back
to an in-band escape. (`sys_setsockopt` likewise returns 0 for
`SO_OOBINLINE` at `sys/net/af_unix.c:2616` while `sys_getsockopt`
rejects the same optname with `-ENOPROTOOPT`, so the two calls
disagree about whether the option exists.)

**Failure.** A ported rlogin/ftp/telnet client calls
`send(fd, &oob_byte, 1, MSG_OOB)` and gets 1 back. No URG segment is
generated (URG-02), the peer never enters urgent mode, and the client
proceeds believing the out-of-band signal was delivered — so it does
not fall back and simply hangs waiting for a response to an urgent
indication that was never sent.

**Fix.** Either route `MSG_OOB` into the TCP send path with the urgent
bit, or, until then, reject it explicitly —
`if (flags & MSG_OOB) return -EOPNOTSUPP;` for `SOCK_STREAM` in
`afinet_sendto_k` — so applications learn the mechanism is
unavailable instead of being told it worked.

### URG-04 (medium). `recv(..., MSG_OOB)` consumes and returns ordinary in-band stream data

**Requirement.** RFC 793 3.8, RECEIVE: *"If there is urgent data the
user will have been informed as soon as it arrived via a TCP-to-user
signal. … If the URGENT flag is on, additional urgent data remains.
If the URGENT flag is off, this call to RECEIVE has returned all the
urgent data, and the user may now leave 'urgent mode'."*

**Code.** The TCP arm of `afinet_recvfrom()`
(`sys/net/af_inet.c:1435-1448`) examines exactly two flag bits:
`MSG_DONTWAIT` at `:1441` and `MSG_PEEK` at `:1443`. `MSG_OOB` is not
tested, so a `recv` with `MSG_OOB` falls through to the unconditional
`return nb ? tcp_recv_nb(…) : tcp_recv(…)` at `:1446-1447` — the
ordinary in-order stream read. `tcp_recv_nb()`
(`sys/net/tcp.c:1492-1537`) advances `p->rx_tail` and decrements
`p->rx_count`, so those bytes are permanently removed from the stream.
Every stack this software is ported from returns EINVAL (Linux) or
EINVAL/ENOTCONN (BSD) when no out-of-band data is pending, and that
error is what applications use to detect "no urgent data here".

**Failure.** A ported application — the common idiom in ftp/rlogin/rsh
and in libraries that probe for OOB after a select — calls
`recv(fd, &mark, 1, MSG_OOB)`. On Linux it gets -1/EINVAL and
continues. Here it gets 1, and the first octet of the pending in-band
stream (the 'H' of "HTTP/1.1 200 OK", or the first byte of a length
prefix) is consumed and discarded as a phantom OOB mark. The next
ordinary `recv()` returns the stream shifted by one octet, so the
protocol framing desynchronises in a way that points nowhere near the
urgent mechanism.

**Fix.** Test `MSG_OOB` in the `SOCK_STREAM` arm before the `MSG_PEEK`
test and, until the urgent mechanism exists, return `-EINVAL`. Once
RCV.UP exists, return only the octet(s) at the mark and set `MSG_OOB`
in the returned `msg_flags`.

### URG-05 (low). `sockatmark()` returns 0 for every descriptor and issues no syscall

**Requirement.** RFC 793 3.8, RECEIVE: *"Format: RECEIVE (local
connection name, buffer address, byte count) -> byte count, urgent
flag, push flag"* — `sockatmark()` is the POSIX rendering of that
urgent-flag result.

**Code.** `lib/c/src/socket.c:138-143` implements it as
`(void)sockfd; return 0;` under the comment *"sockatmark — no
out-of-band data on AF_UNIX, always 0"*, and
`lib/c/src/posix_extra3.c:664-666` repeats that rationale. The premise
no longer holds: the same libc serves AF_INET sockets backed by
`sys/net/tcp.c`. The function issues no syscall at all, so it cannot
distinguish a TCP socket from an AF_UNIX one, cannot fail with EBADF
on a closed descriptor, and cannot fail with ENOTTY on a non-socket —
all three of which POSIX requires. There is no kernel-side fallback
either: `SIOCATMARK` reaches `default: return -ENOTTY;` at
`sys/net/af_inet.c:424`.

**Failure.** `sockatmark(fd)` on a pipe or a regular file returns 0
("a socket, not at the mark") instead of -1/ENOTTY, and the caller
proceeds down the socket path. The constant 0 is accidentally harmless
for TCP today only because no mark can ever exist; it becomes an
outright wrong answer the moment URG-01 is fixed.

**Fix.** Make `sockatmark()` issue
`ioctl(sockfd, SIOCATMARK, &v)` and propagate the kernel's errno, and
add a `SIOCATMARK` case to `afinet_ioctl` reporting whether the next
readable octet is at RCV.UP (returning 0 until URG-01 lands, but
failing correctly for a non-socket or a bad descriptor). Update the
stale AF_UNIX-only comments in both libc files.

### URG-06 (info). Neither the 793 nor the RFC 1122 urgent-pointer convention is expressed

**Requirement.** RFC 793 3.1, Urgent Pointer
(`docs/rfc/rfc793.txt:1206-1210`): *"This field communicates the
current value of the urgent pointer as a positive offset from the
sequence number in this segment. The urgent pointer points to the
sequence number of the octet following the urgent data. This field is
only be interpreted in segments with the URG control bit set."* RFC
1122 4.2.2.4 corrects this to the LAST octet (not last+1) and makes
that the mandatory reading, which is what every deployed stack emits.

**Code.** Substrate implements neither. On send, `tcp_xmit_raw` writes
`th->urg_ptr = 0;` unconditionally (`sys/net/tcp.c:292`) and never
sets `TCP_URG`. On receive, `th->urg_ptr` is never read: `tcp_input`
(`:1040-1124`) byte-swaps `source`, `dest`, `seq`, `ack_seq`,
`doff_flags` and `window`, and never touches the urgent pointer. So
the send/receive consistency question is vacuous — both sides are
absent, and there is no off-by-one to fix, only a choice that has not
been made.

**Failure.** Not a runtime failure today. The hazard is a future
one-sided implementation: `sys/include/netinet/tcp.h:17` declares the
`urg_ptr` wire field and `:27` defines `TCP_URG 0x20`, presenting the
header as a complete RFC 793 header and inviting a contributor to add
urgent handling on one side without noticing that the other encodes no
convention. Whoever wires up the receive side by reading 793 3.1 as
the header's own comment points them to implements
`RCV.UP = SEG.SEQ + SEG.UP` and is then one octet off against every
Linux, BSD and Windows peer.

**Fix.** When the mechanism is implemented, take the RFC 1122 4.2.2.4
reading on both sides, and say so in a comment on the `urg_ptr`
declaration so the two sides cannot drift apart. Clamp a received
SEG.UP to the segment's data length before deriving RCV.UP.

---

# Part 5 — the user interface (RFC 793 3.8, 3.9 call tables) and connection identity (2.7)

Twenty-four findings. The five highs are all connection-identity
defects: the PCB layer, which is the only layer that actually owns the
4-tuple, validates nothing.

### API-01 (high). `connect()` retried after a failed `connect()` reuses the dead TCB and its stale retransmit queue

**Requirement.** RFC 793 3.9, OPEN Call, CLOSED state
(`docs/rfc/rfc793.txt:3372-3374`): *"Create a new transmission control
block (TCB) to hold connection state information."* And, for the RST
path, 3.9 SYN-SENT: *"In either case, all segments on the
retransmission queue should be removed."*

**Code.** `afinet_connect()`'s only re-entry guard is
`if (s->tcp && s->connected) return -EISCONN;`
(`sys/net/af_inet.c:1205`). A `connect()` that fails returns at
`:1230` with `s->connected` still 0, so a retry on the same fd falls
through to `tcp_connect()`/`tcp_connect_nb()` (`:1223-1224`) and
thence to `tcp_connect_start()` (`sys/net/tcp.c:1253`), which reuses
the existing PCB: it picks a fresh ISS (`:1270-1272`) and queues a new
SYN, but nothing clears the previous attempt's unacked queue.
`tcp_kill_pcb()` (`:416-423`) only sets state and `so_error`, and
`tcp_unacked_free_all()` is called from exactly one place,
`tcp_free()` (`:1167`). `p->so_error` is also left set, which
additionally makes `tcp_send_impl()` report `-EPIPE` instead of
`-ENOTCONN` (`:1424`).

**Failure.** `connect()` to a dead host: the SYN is retransmitted
until `head->retx == TCP_MAX_RETX` and `tcp_kill_pcb(p, ETIMEDOUT)`
fires (`:497-499`). The application retries on the same fd — the
portable pattern. The queue is now `[old SYN(old_iss), new
SYN(new_iss)]`. On the next tick the timer reads `unacked_head` = the
OLD segment, whose `retx` is already `>= TCP_MAX_RETX`, and kills the
brand-new connection with ETIMEDOUT before any SYN|ACK could arrive —
`connect()` now fails instantly forever. In the ECONNREFUSED variant
(`retx == 0`) the timer instead retransmits the stale-ISS SYN onto a
live 4-tuple ~1 s later, which the peer answers with a RST. Even when
establishment wins the race, `tcp_unacked_prune()`'s signed comparison
(`:377`) keeps the stale head whenever
`(int32_t)(old_iss+1 - ack) > 0` — a coin flip, since `tcp_new_iss()`
is CSPRNG-random — after which no data segment is ever pruned, the
queue grows without bound, and the permanent `unacked_head` blocks the
FIN_WAIT_1 → FIN_WAIT_2 transition (which requires
`!unacked_head`, `:976`).

**Fix.** Call `tcp_unacked_free_all(p)` from `tcp_kill_pcb()` and
again at the top of `tcp_connect_start()`, and reset
`so_error`/`last_ack`/`dup_ack`/`cwnd`/`ssthresh`/`rcv_nxt` before
re-opening — or reject the retry outright unless the PCB is in CLOSED
with no prior attempt.

### API-02 (high). `connect()` on a socket in SYN-SENT restarts the handshake with a new ISS

**Requirement.** RFC 793 3.9, OPEN Call, SYN-SENT (and every other
non-CLOSED state), `docs/rfc/rfc793.txt:3437`: *"Return 'error:
connection already exists'."*

**Code.** The `s->connected` guard at `sys/net/af_inet.c:1205` is set
only on the success and `-EINPROGRESS` paths. `tcp_connect()`
(`sys/net/tcp.c:1279-1300`) returns `-EINTR` when a signal arrives
while the handshake is outstanding, leaving the PCB in SYN_SENT with a
live SYN queued and `s->connected == 0`. A subsequent `connect()` is
not rejected: `tcp_connect_start()` unconditionally overwrites
`p->iss`/`snd_una`/`snd_nxt` (`:1270-1272`), sets `TCP_SYN_SENT`
again, and queues a second SYN for the same 4-tuple with a different
ISN.

**Failure.** A blocking `connect()` is interrupted by SIGALRM and
returns EINTR; the application retries (the classic EINTR loop). Two
SYNs with different ISS are now outstanding on one 4-tuple. When the
SYN|ACK for the *first* arrives, `tcp_in_syn_sent()` validates it
against the NEW `snd_una`/`snd_nxt`, fails `:708-709`, and replies
`<SEQ=SEG.ACK><CTL=RST>` (`:710`) — tearing down the connection the
peer just created. The second handshake then also fails, because the
peer has torn its side down, so the connection can never be
established.

**Fix.** Guard `connect()` on the PCB's actual state, not on
`s->connected`: EALREADY for SYN_SENT/SYN_RECEIVED, EISCONN for
ESTABLISHED and later, and run `tcp_connect_start()` only from CLOSED.

### API-03 (high). `connect()` on a listening socket converts the PCB and leaks every child

**Requirement.** RFC 793 3.9, OPEN Call, LISTEN
(`docs/rfc/rfc793.txt:3396-3400`): *"If active and the foreign socket
is specified, then change the connection from passive to active,
select an ISS. Send a SYN segment, set SND.UNA to ISS, SND.NXT to
ISS+1. Enter SYN-SENT state."* The RFC permits the conversion — for a
TCB with no other connections attached to it.

**Code.** A listening socket has `s->connected == 0`, so the guard at
`sys/net/af_inet.c:1205` lets `connect()` through and neither
`sys_connect()` nor `afinet_connect()` consults `tcp_is_listening()`
(`sys/net/tcp.c:1365`). `tcp_connect_start()` overwrites `p->state`
with `TCP_SYN_SENT` at `:1273` while leaving `p->listen == 1`,
`p->accept_q` allocated, `p->accept_count` non-zero, and every child
PCB's `->parent` pointing at it. Nothing cleans them up:
`tcp_close()`'s child-teardown loop (`:1662-1673`) is reached only
from `case TCP_LISTEN:`, and the PCB is no longer in LISTEN. When the
ex-listener is finally reaped, `tcp_free()` (`:1164-1166`) only clears
each child's `->parent`. The child is then ESTABLISHED,
`detached == 0`, `parent == NULL`, `holds == 0` — a combination the
reaper never collects, since it frees only `TCP_CLOSED` PCBs.

**Failure.** `socket(); bind(:8080); listen(8);` a client connects and
its child lands in `accept_q`; the server then calls `connect()` on
the *listening* fd (no EOPNOTSUPP is returned) and later closes it.
The listener's PCB is reaped, but the child plus its 32 KiB ring leak
for the life of the boot, still on `g_tcp_pcbs`, still matched by
`tcp_find()` and still ACKing a peer into a ring nobody will ever
drain (window walks to zero and it stalls forever). `accept()` is now
rejected at `sys/net/af_inet.c:1000` because `tcp_is_listening()` is
false, and subsequent SYNs to the port find no LISTEN PCB and get
RSTs, so the service silently disappears. Repeating leaks
32 KiB + `sizeof(tcp_pcb_t)` per iteration from an unprivileged
process with no bound.

**Fix.** Reject `connect()` on a listening PCB at the socket layer
(`if (s->tcp && tcp_is_listening(s->tcp)) return -EOPNOTSUPP;`), which
is what POSIX and every BSD-derived stack do. If the RFC conversion is
genuinely wanted, `tcp_connect_start()` must first run the same
teardown as `tcp_close()`'s LISTEN arm over all children, then clear
`p->listen`/`accept_count` and free `accept_q`.

### API-04 (high). `tcp_bind()` enforces no local-socket uniqueness and cannot fail

**Requirement.** RFC 793 2.7: *"A connection is fully specified by the
pair of sockets at the ends"*; 2.2: *"A pair of sockets uniquely
identifies each connection."* 3.9 OPEN, for a TCB that is not CLOSED:
*"Return 'error: connection already exists'."*

**Code.** `tcp_bind()` (`sys/net/tcp.c:1174-1178`) is three
statements: store `laddr`, store `lport`, `return 0`. There is no scan
of `g_tcp_pcbs`, no comparison of the foreign half, and no error
return path at all, so the layer that owns the connection identity
never rejects anything. `tcp_find()` (`:564-570`) resolves a duplicate
purely by list position, and because `tcp_alloc()` prepends
(`:1144-1146`) the *newest* PCB wins every segment.

The socket-layer check that stands in for it is broken three ways.
`afinet_port_taken()` (`sys/net/af_inet.c:869-885`) walks the *socket*
list and counts an entry only when `o->bound` is set — but
`afinet_connect()` never sets `s->bound` when it syncs back the
kernel-assigned ephemeral port (`:1238`), nor does `afinet_accept()`
(`:1039`), so every connected and every accepted stream socket is
invisible to it. It is bypassed entirely by `SO_REUSEADDR` (`:899`)
and by the AF_INET6 arm (`:915-937`). And it compares only the port,
never the local address, so it is simultaneously too weak to prevent a
real collision and too strict to allow the legitimate
10.0.0.5:80 / 192.168.1.1:80 pair.

There is a fourth hole with the same root: `afinet_node_close()` sets
`s->closed` and unlinks the socket from `g_afi_head` (`:752-757`)
while `tcp_close()` merely marks the PCB detached
(`sys/net/tcp.c:1625`) and leaves it in FIN_WAIT_1/LAST_ACK/TIME_WAIT
until the reaper frees it. The whole class of PCBs that TIME-WAIT
exists to protect is therefore invisible to the only EADDRINUSE test
in the stack.

**Failure.** Socket A: `bind(10.0.0.5:5000)`, `connect(1.2.3.4:80)` —
PCB A is ESTABLISHED. Socket B: `SO_REUSEADDR`, `bind(10.0.0.5:5000)`
(the check is skipped at `:899`), `connect(1.2.3.4:80)`.
`tcp_connect_start()` keeps the bound `lport` (`:1254` allocates only
when `lport == 0`), producing PCB B with a byte-identical 4-tuple.
From that moment `tcp_find()` returns PCB B (list head) for every
arriving segment: A's SYN|ACK, its data and its ACKs are all fed to
B's state machine, A's `connect()`/`recv()` hang until ETIMEDOUT, and
B's sequence checks are driven by a stream it did not send. The
TIME-WAIT variant needs no `SO_REUSEADDR` at all: a daemon restarts
one second after closing, `bind()` finds nothing taken, a client
reconnects from a pooled source port, and `tcp_find()`'s first loop
matches the surviving LAST_ACK PCB rather than the new listener — the
SYN reaches `tcp_in_established()`, matches no branch there, and is
discarded, so the client blackholes.

**Fix.** Make the uniqueness test authoritative at the PCB layer: give
`tcp_bind()` a real conflict scan over `g_tcp_pcbs` and an
`-EADDRINUSE` return (same `lport` and overlapping `laddr`, on any
non-CLOSED PCB, detached or not), add the same 4-tuple check in
`tcp_connect_start()` before the SYN goes out, and propagate
`tcp_bind()`'s result through `afinet_bind()` (`:913` and `:936`
currently discard it). Set `s->bound` in `afinet_connect()` and
`afinet_accept()`. Keep the classic exception explicit: allow
rebinding over a TIME_WAIT PCB only for a listener with
`SO_REUSEADDR`.

### API-05 (high). `tcp_find()`'s LISTEN scan takes the first list hit

**Requirement.** RFC 793 2.2, Multiplexing: *"Concatenated with the
network and host addresses from the internet communication layer, this
forms a socket. A pair of sockets uniquely identifies each
connection."* 2.7 also provides for a passive OPEN with a fully
specified foreign socket (*"In this case, the match must be exact"*).

**Code.** The second loop in `tcp_find()` (`sys/net/tcp.c:572-576`)
returns the first PCB with `state == TCP_LISTEN`, `lport == dport` and
`laddr` either 0 or equal to `daddr`. There is no most-specific-match
rule, so when a wildcard listener and an address-specific listener
both exist the winner is decided by list position — and because every
PCB is prepended (`:1144-1146`, `:675-676`), that is simply the most
recently created one. The loop never inspects `raddr`/`rport` either,
so a fully specified passive OPEN cannot be honoured.

(The related worry about accepting a segment for an address this host
does not own is covered upstream: `ip4_input()` rejects any datagram
whose `daddr` is neither `dev->ip4_addr` nor the link broadcast,
`sys/net/inet.c:337-341`, and drops broadcast before `tcp_input`,
`:362-364`.)

**Failure.** sshd listens on 10.0.0.5:22. An unprivileged process
opens an AF_INET6 socket and binds `[::]:22`, which takes the
no-conflict-check path (`sys/net/af_inet.c:915-937`) and calls
`tcp_bind(pcb, 0, 22)` — a v4 wildcard PCB placed at the head of
`g_tcp_pcbs`. Every subsequent SYN to 10.0.0.5:22 is matched by the
attacker's listener, and sshd receives nothing. The same collision
arises benignly between two cooperating daemons using `SO_REUSEADDR`,
where delivery then depends on start-up order.

**Fix.** Score the LISTEN loop instead of returning on the first hit:
prefer `laddr == daddr` over `laddr == 0`, and prefer a listener whose
`raddr`/`rport` match the segment's source. Return the best-scoring
PCB, and make `bind()` reject a wildcard/specific overlap on the same
port unless `SO_REUSEPORT` semantics are explicitly requested.

### API-06 (medium). `listen()` on a connected or connecting socket rewrites the PCB into LISTEN

**Requirement.** RFC 793 3.9, OPEN Call, for SYN-SENT, SYN-RECEIVED,
ESTABLISHED, FIN-WAIT-1/2, CLOSE-WAIT, CLOSING, LAST-ACK, TIME-WAIT:
*"Return 'error: connection already exists'."*

**Code.** `afinet_listen()` (`sys/net/af_inet.c:957-962`) checks only
that the socket has a PCB, and `tcp_listen()`
(`sys/net/tcp.c:1180-1215`) assigns `p->state = TCP_LISTEN` and
`p->listen = 1` unconditionally from any state, leaving
`laddr`/`raddr`/`lport`/`rport`, `snd_nxt`, the receive ring and the
unacked queue of the live connection intact. No EINVAL/EISCONN is
returned anywhere.

**Failure.** `connect()` succeeds, then the process calls
`listen(fd, 5)`. The peer's next data segment still matches the exact
4-tuple in `tcp_find()`'s first loop (`:564-570`, which matches
regardless of state), so it is dispatched to `tcp_in_listen()`, which
discards anything carrying ACK (`:633-639`). Every byte the peer sends
is black-holed with no RST and no error; `accept()` blocks forever;
and `close()` takes the LISTEN arm (`:1638`), which walks non-existent
children and drops the PCB to CLOSED without emitting FIN or RST,
leaving the peer stranded in ESTABLISHED until its own timeout. Same
shape for `listen()` after a non-blocking `connect()`: the SYN|ACK is
routed to `tcp_in_listen()` and dropped, so the handshake can never
complete.

**Fix.** Reject `tcp_listen()` when `p->state != TCP_CLOSED` (except
the already-LISTEN backlog-change case the TCP-32 comment covers) and
map it to `-EINVAL` in `afinet_listen()`.

### API-07 (medium). `listen()` without `bind()` succeeds with local port 0

**Requirement.** RFC 793 3.9, OPEN Call, CLOSED state
(`docs/rfc/rfc793.txt:3372-3377`): *"Create a new transmission control
block (TCB) … Fill in local socket identifier, foreign socket,
precedence, security/compartment, and user timeout information. …
If passive enter the LISTEN state and return."*

**Code.** `afinet_listen()` (`sys/net/af_inet.c:957-962`) performs no
bind check and no implicit bind; `tcp_listen()`
(`sys/net/tcp.c:1180-1215`) never assigns `p->lport`. A socket never
`bind()`ed therefore enters LISTEN with `lport == 0`, and
`tcp_find()`'s listener scan requires `p->lport == dport` (`:573`),
which no real segment can satisfy. `listen()` still returns 0. The
ephemeral allocator needed to fix it already exists
(`tcp_alloc_ephemeral`, `:1241`), and the UDP path already auto-binds
on first use (`sys/net/af_inet.c:683-688`).

**Failure.** The standard ephemeral-server idiom — `socket();
listen(fd, 5); getsockname()` to learn the port and advertise it,
used by RPC callback services, FTP data channels and test harnesses —
returns success with port 0 from `afinet_getsockname()`
(`sys/net/af_inet.c:1179-1184`). The service advertises port 0,
`accept()` blocks forever, and every SYN that arrives is answered with
a RST by `tcp_send_rst()` (`sys/net/tcp.c:1078`). The application has
no indication it is not listening on anything.

**Fix.** In `afinet_listen()`, if `s->local_port == 0`, allocate a
free ephemeral port, record it in `s->local_port`, set `s->bound` and
push it into the PCB with `tcp_bind()` before calling `tcp_listen()`.

### API-08 (medium). `tcp_connect_start()` is `void`: allocation failure and port exhaustion are both discarded

**Requirement.** RFC 793 3.9, OPEN Call, CLOSED
(`docs/rfc/rfc793.txt:3389`): *"If there is no room to create a new
connection, return 'error: insufficient resources'."*

**Code.** `tcp_connect_start()` is declared `void`
(`sys/net/tcp.c:1253`) and discards both of its failure returns.

1. At `:1276` the return of `tcp_xmit_queue()` is ignored. That
   function returns `-ENOMEM` when its `kmalloc` fails (`:325-326`)
   and in that case transmits nothing and queues nothing — yet `:1273`
   has already set `TCP_SYN_SENT`. With an empty unacked queue the
   timer's `if (!head) continue;` (`:490-491`) skips the PCB on every
   tick, so the `TCP_MAX_RETX`/ETIMEDOUT abort that `tcp_connect()`
   relies on as its only timeout (`:1283-1284`) can never fire.
2. At `:1254` the return of `tcp_alloc_ephemeral()` is ignored; that
   helper returns 0 when the dynamic range is exhausted (`:1250`),
   leaving `p->lport == 0`, so the SYN goes out with source port 0 and
   every further exhausted `connect()` produces another PCB with
   `lport == 0` — all of them sharing one local socket and colliding
   in `tcp_find()`, reintroducing exactly the ambiguity TCP-07
   (`:1219-1232`) was written to remove.

**Failure.** (a) Under memory pressure the 20-byte `kmalloc` fails.
`connect()` returns neither ENOMEM nor ENOBUFS: a blocking caller
parks in `tcp_connect()`'s loop (`:1285-1298`) forever, since the
state stays SYN_SENT and no RTO exists to drive it to CLOSED; only a
signal breaks it out. A non-blocking caller gets EINPROGRESS and then
`poll()` never reports anything, because `tcp_poll()`'s SYN_SENT arm
(`:1320-1323`) returns 0 unconditionally. No SYN ever went on the
wire. (b) On exhaustion, `connect()` blocks for the full ~63 s budget
and returns ETIMEDOUT instead of the immediate EADDRNOTAVAIL the
caller should have received.

**Fix.** Make `tcp_connect_start()` return `int`: propagate `-ENOMEM`
from `tcp_xmit_queue()` and `-EADDRNOTAVAIL` when
`tcp_alloc_ephemeral()` returns 0, resetting `p->state` to
`TCP_CLOSED` before returning so the PCB is not stranded, and have
`tcp_connect()`/`tcp_connect_nb()` return that to the socket layer.

### API-09 (medium). `tcp_port_taken()` keys on the local port alone

**Requirement.** RFC 793 2.7: *"A local socket may participate in many
connections to different foreign sockets."*

**Code.** `tcp_port_taken()` (`sys/net/tcp.c:1233-1239`) returns 1 as
soon as any other non-CLOSED PCB has the same `lport`, without
comparing `raddr` or `rport`. Since RFC 793 defines connection
identity as the 4-tuple, a local port is legitimately reusable against
every distinct foreign socket; keying on the local port alone caps
outbound connections at 16384 *system-wide* instead of 16384 per
destination, and counts listeners and inbound-accepted children (which
inherit the listener's `lport`, `:661`) against that budget too. It is
simultaneously too permissive in one direction: it skips every
`TCP_CLOSED` PCB (`:1235`), including CLOSED-but-unreaped ones waiting
for the timer (`:460-476`) or still held by a blocked caller
(`p->holds != 0`), whose peer may not yet have processed the abort.

**Failure.** A crawler or proxy opens 16384 connections spread over
many distinct destinations — well within what the 4-tuple space
allows. The 16385th `connect()` finds `tcp_port_taken()` true for all
16384 candidates in `tcp_alloc_ephemeral()`'s sweep (`:1246-1249`),
which returns 0, and API-08 then opens the connection with local port
0. On a server, several thousand accepted children on port 80 consume
nothing of the ephemeral range in reality but are counted against it
here.

**Fix.** Key the test on the full 4-tuple: taken only when another
non-CLOSED PCB has the same `lport` AND an overlapping `laddr` AND the
same `(raddr, rport)` the caller is about to connect to — which means
moving the allocation after the remote is known. Treat a LISTEN PCB's
port as reserved wholesale, and include CLOSED-but-unreaped PCBs.

### API-10 (medium). `connect()` accepts an unspecified foreign socket

**Requirement.** RFC 793 3.9, OPEN Call, CLOSED
(`docs/rfc/rfc793.txt:3380-3383`): *"If active and the foreign socket
is unspecified, return 'error: foreign socket unspecified'; if active
and the foreign socket is specified, issue a SYN segment."*

**Code.** `afinet_connect()`'s AF_INET arm validates only the address
length and `sin_family` (`sys/net/af_inet.c:1216-1218`) before handing
the address to `tcp_connect{,_nb}` (`:1221-1224`). A zero address
and/or a zero port is never rejected, and `tcp_connect_start()` stores
it verbatim (`sys/net/tcp.c:1268-1269`) and issues a SYN.
`route_for_v4(0)` (`sys/net/inet.c:158-187`) does not special-case
0.0.0.0 either: it falls through to the default-gateway arm and
returns a real device, so the segment is genuinely ARPed for and put
on the wire with destination 0.0.0.0 port 0. Nothing can ever match
the resulting PCB on input, since `tcp_find()`'s exact-match arm
(`sys/net/tcp.c:567`) would need a segment from 0.0.0.0 port 0.

**Failure.** An application resolves a service name that yields no
port (a `getaddrinfo` result used without setting `sin_port`, or an
empty config field) and calls `connect()`. Instead of an immediate
error the thread blocks while the stack retransmits the SYN 6 times
over ~63 s before ETIMEDOUT — a silent stall that looks like a network
problem rather than a bad address. Each such call pins a PCB and an
ephemeral port for that whole period and emits 6 bogus frames; a loop
of them is a cheap local PCB-exhaustion lever.

**Fix.** Reject an unspecified foreign socket in `afinet_connect()`
before any PCB state is touched: a zero `sin_port` gives
EADDRNOTAVAIL, and `INADDR_ANY` should either be rejected or rewritten
to 127.0.0.1 as Linux does. The same check belongs on the AF_INET6
arm.

### API-11 (medium). No ABORT primitive, and `SO_LINGER {1,0}` reports success while doing nothing

**Requirement.** RFC 793 3.8, Abort: *"This command causes all pending
SENDs and RECEIVES to be aborted, the TCB to be removed, and a special
RESET message to be sent to the TCP on the other side of the
connection."* 3.9's ABORT Call table gives the segment:
`<SEQ=SND.NXT><CTL=RST>`.

**Code.** `tcp.c` exposes no abort entry point: `grep -n TCP_RST
sys/net/tcp.c` finds only inbound handling, `tcp_send_rst()` for
unmatched segments, and the listener-teardown loop at `:1670`. Every
user-initiated teardown routes through `tcp_close()`, whose
ESTABLISHED and CLOSE_WAIT arms (`:1628-1637`) always emit a graceful
FIN. The only API by which an application can request an abortive
close — `SO_LINGER` with `l_onoff=1, l_linger=0` — is not recognised
by `sys_setsockopt()`: its only special case is `SO_REUSEADDR`
(`sys/net/af_unix.c:2607-2615`) and everything else falls through to
the unconditional `return 0;` at `:2616`, even though
`include/sys/socket.h:159` publishes `struct linger` with documented
`close()` semantics. Related: RFC 1122 4.2.2.13 says a CLOSE issued
while received data is still pending SHOULD send a RST; `tcp_close()`
sends a FIN regardless of `p->rx_count`.

**Failure.** A server rejecting an abusive client sets
`SO_LINGER {1,0}` and `close()`s, expecting an immediate RST and no
TIME_WAIT. `setsockopt()` returns 0, `close()` sends a FIN, the PCB
walks FIN_WAIT_1 → TIME_WAIT and holds the 4-tuple for
`TCP_TIME_WAIT_TICKS` = 60 s (`sys/net/tcp.c:101`) — exactly the
outcome the option exists to prevent — and the client's half-open
connection is left up, with no error anywhere telling the server its
request was ignored. There is likewise no way to abort a connection
whose peer has stopped responding without waiting out the full ~63 s
budget.

**Fix.** Add `tcp_abort(pcb)` implementing the ABORT Call table —
`tcp_send_ctl(p, TCP_RST)` at SND.NXT for SYN_RECEIVED/ESTABLISHED/
FIN_WAIT_*/CLOSE_WAIT, `tcp_unacked_free_all()`, then
`tcp_kill_pcb(p, ECONNRESET)` so blocked SENDs/RECEIVEs get their
notification, and a plain delete for LISTEN/SYN_SENT and for
CLOSING/LAST_ACK/TIME_WAIT. Record `SO_LINGER` on the `afi_sock` and
have `afinet_node_close()` call `tcp_abort()` when
`l_onoff && !l_linger`; at minimum return ENOPROTOOPT rather than
claiming success.

### API-12 (medium). `accept()`'s failure paths CLOSE an established child instead of aborting it

**Requirement.** RFC 793 3.8, ABORT (quoted above); and 3.5's
guarantee that CLOSE is what breaks here: *"A TCP will reliably
deliver all buffers SENT before the connection was CLOSED so a user
who expects no data in return need only wait to hear the connection
was CLOSED successfully to know that all his data was received at the
destination TCP."*

**Code.** All three `accept()` error paths — `kmalloc` of the socket
(`sys/net/af_inet.c:1013`), `kmalloc` of the ring (`:1020`) and fd
installation (`:1047`) — dispose of an already ESTABLISHED child with
`tcp_close(cp)`, whose ESTABLISHED arm (`sys/net/tcp.c:1628-1632`)
emits an orderly FIN|ACK. But the child completed its handshake
already and its ring may hold data that `tcp_in_established()` has
acknowledged (`:813-824`, and the ACK carried on the FIN itself) —
data that is discarded when the PCB is freed. Discarding a connection
because the host ran out of memory or descriptors is an ABORT, which
the RFC requires be signalled with a RST. (Data arriving *later* does
get a RST via the detached check at `:806-810`; already-buffered,
already-ACKed data does not.)

**Failure.** A server under fd pressure; a client connects and
immediately writes a request. The handshake completes in the kernel
and the request lands in the child's ring and is ACKed. `accept()`
then fails at `:1043` with EMFILE, `tcp_close()` sends FIN|ACK, and
the client sees its request fully acknowledged followed by a clean
end-of-stream. The client reports a truncated response and, for a
non-idempotent request, cannot tell whether the server processed it —
instead of the unambiguous ECONNRESET a RST would produce.

**Fix.** Use the `tcp_abort()` of API-11 on these three paths, and
generally whenever a connection with unread received data is destroyed
(RFC 1122 4.2.2.13).

### API-13 (medium). SEND in the closing states returns ENOTCONN, `POLLOUT` stays asserted, and no SIGPIPE is raised

**Requirement.** RFC 793 3.9, SEND Call, FIN-WAIT-1 / FIN-WAIT-2 /
CLOSING / LAST-ACK / TIME-WAIT (`docs/rfc/rfc793.txt:3551`): *"Return
'error: connection closing' and do not service request."* The CLOSED
row gives the *other* error: *"error: connection does not exist"*.

**Code.** `tcp_send_impl()` collapses both into one test and picks the
errno from `p->so_error` alone:
`return p->so_error ? -EPIPE : -ENOTCONN;`
(`sys/net/tcp.c:1419-1425`). Every state the RFC calls "closing" has
`so_error == 0` after an ordinary half-close — `tcp_close()` and
`tcp_shutdown_wr()` set the state without setting it — so a `write()`
in FIN_WAIT_1/FIN_WAIT_2/CLOSING/LAST_ACK/TIME_WAIT reports ENOTCONN,
the code the RFC reserves for CLOSED. The two responses are inverted.

Two compounding defects. `tcp_poll()` sets `POLLOUT` for every state
except LISTEN/SYN_SENT/SYN_RECEIVED/CLOSED with the blanket
`if (events & POLLOUT) revents |= POLLOUT;` (`:1347-1350`), and
`POLLHUP` is added only for CLOSE_WAIT (`:1351`), so a socket that has
already sent its FIN is advertised as writable forever. And nothing in
the socket write path raises `SIGPIPE`: `grep -rn SIGPIPE sys/` finds
it only in `sys/fs/pipe.c:272`, never in `sys/net/`.

**Failure.** A client does `shutdown(fd, SHUT_WR)` and keeps the fd in
its poll set. `poll()` reports POLLOUT forever on a connection that
can never accept another byte, so an event loop that writes on POLLOUT
spins: every write returns ENOTCONN, which callers interpret as "never
connected" and log as a setup failure rather than "this side is
closing". A program relying on the default SIGPIPE disposition to
terminate on a broken stream instead loops on the error return.

**Fix.** Select the error from the state: `-ENOTCONN` for
CLOSED/LISTEN/SYN_SENT/SYN_RECEIVED, `-EPIPE` for the five closing
states (keeping `so_error`'s `-EPIPE` for the reset/timeout case).
Withhold `POLLOUT` (and set `POLLHUP`) once the local FIN has been
sent. Raise SIGPIPE on the EPIPE returns unless `MSG_NOSIGNAL`.

### API-14 (medium). `send()` while the handshake is outstanding fails instead of queueing

**Requirement.** RFC 793 3.9, SEND Call, SYN-SENT / SYN-RECEIVED:
*"Queue the data for transmission after entering ESTABLISHED state.
If no space to queue, respond with 'error: insufficient resources'."*

**Code.** `tcp_send_impl()` rejects every state that is not
ESTABLISHED or CLOSE_WAIT at `sys/net/tcp.c:1419-1425`, returning
`-ENOTCONN`. Both userland entry points into the stream write path —
`afinet_node_write_body()` (`sys/net/af_inet.c:667-671`) and
`afinet_sendto_k()`'s stream arm (`:1274-1276`) — hand the buffer
straight to `tcp_send{,_nb}`. There is no send buffer in the PCB at
all (only the unacked retransmit FIFO), so the mechanism the RFC
requires is simply absent.

**Failure.** `fcntl(O_NONBLOCK); connect() -> EINPROGRESS;
write(fd, req, n)` immediately — a pattern in TCP-Fast-Open-style and
simple event-loop clients, and the natural follow-up to the EISCONN
confusion of API-22. The write fails with ENOTCONN even though the
connection establishes milliseconds later, so the request is lost and
the client reports a transport error on a connection that is fine.

**Fix.** Add a pre-established send queue (or block until ESTABLISHED
on a blocking socket / return EAGAIN on a non-blocking one), and
report ENOBUFS only when it genuinely cannot be queued.

### API-15 (medium). `shutdown(SHUT_WR)` on a connecting socket is silently discarded

**Requirement.** RFC 793 3.9, CLOSE Call, SYN-SENT: *"Delete the TCB
and return 'error: closing' responses to any queued SENDs, or
RECEIVEs."* SYN-RECEIVED: *"If no SENDs have been issued and there is
no pending data to send, then form a FIN segment and send it, and
enter FIN-WAIT-1 state."*

**Code.** `afinet_shutdown()` passes its ENOTCONN check for a socket
in SYN_SENT, because `s->connected` was set on the EINPROGRESS path
(`sys/net/af_inet.c:979` and `:1242`), and calls `tcp_shutdown_wr()`
(`:986`). `tcp_shutdown_wr()` handles only ESTABLISHED and CLOSE_WAIT;
everything else falls into the default arm at
`sys/net/tcp.c:1717-1721`, which unlocks and returns 0 without
recording anything. The PCB keeps no "write side closed" flag, so once
the handshake completes the socket is a fully open, writable
ESTABLISHED connection and the requested FIN is never generated. The
comment at `:1718-1719` (*"SYN_SENT has nothing established to FIN;
the rest already sent their FIN"*) is wrong for both SYN_SENT and
SYN_RECEIVED, neither of which has sent a FIN.

**Failure.** Non-blocking client: `connect() -> EINPROGRESS`; the
application has nothing to send and calls `shutdown(fd, SHUT_WR)` to
signal end-of-request, then reads for the response. `shutdown()`
returns 0; the handshake completes; no FIN is ever transmitted; the
server blocks in `read()` waiting for a request terminator that will
never arrive, and both sides hang until an application-level timeout.

**Fix.** Record the pending half-close in the PCB and act on it: abort
from SYN_SENT per the RFC, send the FIN (FIN_WAIT_1) from
SYN_RECEIVED, and emit the deferred FIN on the transition to
ESTABLISHED.

### API-16 (medium). `send()`/`sendto()` on a stream socket ignores `O_NONBLOCK` and `MSG_DONTWAIT`

**Requirement.** RFC 793 3.9, SEND Call, ESTABLISHED: *"Segmentize the
buffer and send it with a piggybacked acknowledgment (acknowledgment
value = RCV.NXT). If there is insufficient space to remember this
buffer, simply return 'error: insufficient resources'."*

**Code.** `afinet_sendto_k()`'s stream arm calls the *blocking*
`tcp_send()` unconditionally (`sys/net/af_inet.c:1274-1276`); `flags`
is discarded at `:1265` (`(void)flags;`) and the fd's `FNONBLOCK` is
never consulted. Its exact counterpart on the receive side,
`afinet_recvfrom()`, honours both `MSG_DONTWAIT` and `FNONBLOCK`
(`:1439-1447`), and the `write()` adapter honours `FNONBLOCK` via
`afi_node_nonblock()` (`:667-670`) — so the `send()`/`sendto()` entry
point is the odd one out. With a full window `tcp_send_impl()` sleeps
on `p->send_chan` (`sys/net/tcp.c:1463-1464`) instead of returning,
the opposite of the RFC's "simply return" disposition.

**Failure.** A single-threaded event loop sets `O_NONBLOCK` and uses
`send()` (not `write()`). When the peer stops reading and the window
closes, `send()` blocks inside `tcp_send_impl()` instead of returning
EAGAIN. The loop can no longer service any other fd — including the
reads that would drain the peer and reopen the window — so the whole
process wedges until the connection times out: exactly the
self-deadlock the non-blocking write path documents and avoids at
`sys/net/tcp.c:1445-1450`.

**Fix.** Plumb the fd's `FNONBLOCK` and `MSG_DONTWAIT` into
`afinet_sendto_k()` and call `tcp_send_nb()` in that case, mirroring
`afinet_recvfrom()`.

### API-17 (medium). `read(2)` on a listening TCP socket blocks forever

**Requirement.** RFC 793 3.9, RECEIVE Call, LISTEN
(`docs/rfc/rfc793.txt:3617`): *"Queue for processing after entering
ESTABLISHED state. If there is no room to queue this request, respond
with 'error: insufficient resources'."* The RFC's guarantee is that
the queued RECEIVE is eventually satisfied, because in its model the
LISTEN TCB itself becomes ESTABLISHED.

**Code.** This stack uses the BSD clone model — `tcp_in_listen()`
spawns a separate child PCB (`sys/net/tcp.c:655-677`) and the listener
stays in `TCP_LISTEN` forever — so a RECEIVE queued against the
listener can never be satisfied. `afinet_recvfrom()` knows this and
guards for it (`if (tcp_is_listening(s->tcp)) return -ENOTCONN;`,
`sys/net/af_inet.c:1438`), but the `read(2)` path does not:
`afinet_node_read_body()` dispatches straight into `tcp_recv()` at
`:566-568` with no listening check. `tcp_recv_nb()` finds
`rx_count == 0` (a listener's ring is never written), finds
`TCP_LISTEN` in none of its EOF states, and returns `-EAGAIN`;
`tcp_recv()` then loops on `p->recv_chan`, which for a listener
nothing ever signals — `tcp_in_listen()`/`tcp_in_syn_received()` wake
`accept_chan` only.

**Failure.** `read(listen_fd, buf, n)` — directly, through a shell
redirect, or through any library that treats a socket fd uniformly —
parks the thread indefinitely (waking every `TCP_SLEEP_POLL` ticks
only to see `-EAGAIN` again), releasable only by a signal. The same
call made as `recv(listen_fd, …)` correctly returns ENOTCONN, so the
behaviour depends on which syscall the caller happened to use.

**Fix.** Add the same guard to `afinet_node_read_body()`:
`if (s->type == SOCK_STREAM && s->tcp && tcp_is_listening(s->tcp))
return (size_t)-ENOTCONN;` before the dispatch. (The
SYN-SENT/SYN-RECEIVED half of that RFC paragraph *is* satisfied — the
sleep/re-check loop genuinely queues the request until establishment.)

### API-18 (medium). No authority check on the local socket in `bind()`

**Requirement.** RFC 793 3.9, OPEN Call, CLOSED: *"If the caller does
not have access to the local socket specified, return 'error:
connection illegal for this process'."* 2.7 frames the same
requirement: *"There must be well-known sockets which the TCP
associates only with the 'appropriate' processes by some means… We
envision that processes may 'own' ports, and that processes can
initiate connections only on the ports they own."*

**Code.** `afinet_bind()` (`sys/net/af_inet.c:887-941`) validates the
address family and length and consults `afinet_port_taken()` for
AF_INET, but performs no check of the caller's authority over the
requested local socket — no reserved-port test, no euid test. Such
checks exist elsewhere in this very module when they are thought
necessary (`SOCK_RAW` requires euid 0 at `:820`, and the `SIOC*`
interface ioctls check at `:281` and `:337`), so the absence here is a
gap rather than a deliberate global policy. With the port-conflict
test itself bypassable (`SO_REUSEADDR` at `:899`, and the AF_INET6 arm
skipping it entirely), a well-known port is protected only for as long
as some live AF_INET socket happens to hold it.

**Failure.** An unprivileged process binds 0.0.0.0:22 during the
window before sshd starts, or after sshd's listener is closed for a
restart. `afinet_bind()` succeeds, `tcp_bind()` accepts
unconditionally (API-04) and `tcp_listen()` installs a LISTEN PCB.
Every SSH client that connects is handed to the unprivileged process,
which can speak enough of the protocol to harvest credentials; sshd's
own later `bind()` is the one that fails, and the operator sees only
"address already in use".

**Fix.** Reject a bind to a port below `IPPORT_RESERVED` (1024) from a
process whose euid is not 0, returning `-EACCES`, in both arms of
`afinet_bind()` — matching the check already applied to `SOCK_RAW` at
`:820`.

### API-19 (medium). A failed FIN allocation leaves the PCB in FIN-WAIT-1 or LAST-ACK with no FIN and no reaper

**Requirement.** RFC 793 3.5, Closing a Connection, Cases 1 and 2:
*"In this case, a FIN segment can be constructed and placed on the
outgoing segment queue. No further SENDs from the user will be
accepted by the TCP, and it enters the FIN-WAIT-1 state. … All
segments preceding and including FIN will be retransmitted until
acknowledged."*

**Code.** `tcp_close()` commits the state transition before, and
independently of, emitting the FIN: `sys/net/tcp.c:1629-1631` sets
FIN_WAIT_1 then calls `tcp_xmit_queue()`, and `:1634-1636` sets
LAST_ACK then calls it; `tcp_shutdown_wr()` does the same at
`:1708-1710` and `:1713-1715`. The return value is discarded in all
four places, but `tcp_xmit_queue()` returns `-ENOMEM` without
transmitting or queueing anything when its `kmalloc` fails
(`:325-326`). The PCB is then in a closing state with an empty unacked
FIFO and no FIN ever sent. `tcp_timer_tick()` cannot rescue it:
FIN_WAIT_1 and LAST_ACK have no deadline of their own, and the
retransmit path bails at `if (!head) continue;` (`:490-491`), so the
RTO budget that would otherwise produce ETIMEDOUT never runs. Since
`tcp_close()` has already set `detached = 1` (`:1625`) and only
`TCP_CLOSED` PCBs are freed (`:460-465`), the PCB and its 32 KiB ring
leak permanently.

**Failure.** Under kernel memory pressure an application closes an
established socket. Nothing is sent. On every subsequent tick the
timer sees FIN_WAIT_1 (matched at neither `:479` nor `:484`), reads
`unacked_head == NULL` and `continue`s. The PCB never reaches CLOSED,
is never freed, and no FIN or RST ever reaches the peer, which keeps
its half of the connection alive until its own keepalive or user
timeout — if it has one. Memory pressure is the trigger, so the first
failure makes the leak that makes the next one likelier.

**Fix.** Check the `tcp_xmit_queue()` result in
`tcp_close()`/`tcp_shutdown_wr()`; on failure fall back to an
unqueued RST plus `tcp_kill_pcb()` so the PCB reaches CLOSED and is
reaped, or leave the state unchanged so a later retry can send the
FIN. Independently, make `tcp_timer_tick()` enforce a deadline on
FIN_WAIT_1, CLOSING and LAST_ACK (see also SM-01).

### API-20 (medium). CLOSE in SYN-RECEIVED sends neither FIN nor RST

**Requirement.** RFC 793 3.9, CLOSE Call, SYN-RECEIVED
(`docs/rfc/rfc793.txt:3743`): *"If no SENDs have been issued and there
is no pending data to send, then form a FIN segment and send it, and
enter FIN-WAIT-1 state; otherwise queue for processing after entering
ESTABLISHED state."*

**Code.** `tcp_close()` lumps SYN_SENT and SYN_RECEIVED into one arm
(`sys/net/tcp.c:1678-1685`) that sets `TCP_CLOSED` and returns; the
comment (*"No established peer to FIN"*) is correct for SYN_SENT,
where the RFC does say to delete the TCB, and wrong for SYN_RECEIVED,
where the peer has already received our SYN|ACK and considers the
connection open. Nothing at all leaves this host.
`tcp_shutdown_wr()` has the identical gap (`:1717-1721`).

**Failure.** Latent today rather than live: only listener children
occupy SYN_RECEIVED and they are unreachable from the socket layer
(`tcp_in_syn_received()` enqueues a child to `accept_q` only once
ESTABLISHED, `:749-760`; SM-11 means no user socket enters
SYN_RECEIVED), and the LISTEN arm of `tcp_close()` does RST such
children at `:1670`. It becomes live the moment simultaneous open
(SM-11) or an accept-before-establishment path is added, at which
point the closed PCB goes to CLOSED and is reaped while the peer,
ESTABLISHED since it processed our SYN|ACK, blocks forever in
`read()` if the protocol has it speak second.

**Fix.** Split `TCP_SYN_RECEIVED` out of the SYN_SENT arm in both
functions: set `TCP_FIN_WAIT_1` and
`tcp_xmit_queue(p, TCP_FIN | TCP_ACK, NULL, 0)`, exactly as the
ESTABLISHED arm above it does.

### API-21 (medium). A SYN followed by a RST leaves a child PCB the backlog does not count

**Requirement.** RFC 793 3.9, second check the RST bit, SYN-RECEIVED:
*"If this connection was initiated with a passive OPEN (i.e., came
from the LISTEN state), then return this connection to LISTEN state
and return. The user need not be informed."*

**Code.** `tcp_in_listen()` allocates a full child `tcp_pcb_t` plus a
32 KiB `rxbuf` (`sys/net/tcp.c:655`, `:667`) on receipt of a bare SYN,
before the handshake completes. The backlog guard at `:648-653` counts
only `p->accept_count` plus children whose state is exactly
`TCP_SYN_RECEIVED`. When a RST arrives for such a child,
`tcp_in_syn_received()` (`:737`) calls `tcp_kill_pcb()`, which only
sets `TCP_CLOSED` — the RFC's "return this connection to LISTEN state",
i.e. destroy the record and give the backlog slot back, is not what
happens. The child now matches neither backlog term, so it is
invisible to the check, and it is freed only when `tcp_timer_tick()`
next runs (`:473`), up to `TCP_TIMER_PERIOD` = 125 ms later. The
backlog therefore caps concurrent *live* half-opens at 32 but places
no bound at all on zombie ones.

**Failure.** Against any listening socket, repeat from one source
address and port: send SYN (the kernel `kmalloc`s a PCB plus a 32768-byte
ring), then immediately RST (`:737` → CLOSED; the PCB stays allocated,
is skipped by the backlog count at `:650` and by `tcp_find()` at
`:565`, and waits for the reaper). At 10,000 pairs/second roughly
1,250 zombie PCBs are live at any instant ≈ 41 MiB of non-reclaimable
kernel heap; at line rate it is gigabytes. No sequence number needs
guessing and the same 4-tuple can be reused indefinitely. The linear
walks in `tcp_find()` (`:564`) and in the backlog loop (`:649`) run
with interrupts off and are O(N) in the zombie count, making the cost
super-linear (RES-03).

**Fix.** Free the child immediately in `tcp_in_syn_received()`'s RST
arm when it has a parent and is not in the accept queue (it has no fd
and no waiter), or count non-accept-queued children with `->parent`
set in the pending total regardless of state. Deferring the 32 KiB
`rxbuf` allocation until the child reaches ESTABLISHED also removes
the amplification factor.

### API-22 (low). `s->connected` is set on the `-EINPROGRESS` path

**Requirement.** RFC 793 3.9, OPEN Call, SYN-SENT: *"Return 'error:
connection already exists'"* — the in-progress case, which POSIX
spells EALREADY, is distinct from the established one.

**Code.** `afinet_connect()` sets `s->connected = 1` at
`sys/net/af_inet.c:1242` before returning `-EINPROGRESS`.
`s->connected` is the single "this socket has an established peer"
predicate used by the connect guard (`:1205`), `getpeername()`
(`:1193`), `shutdown()`'s ENOTCONN check (`:979`) and `sendto()`'s
destination resolution (`:1281`). While the handshake is still
outstanding the connection does not exist yet, so all four answer as
if it did. The comment at `:1189-1192` acknowledges the flag is only
approximate.

**Failure.** curl/openssh-style code: `fcntl(O_NONBLOCK); connect()
-> EINPROGRESS; poll(POLLOUT)`. A library layer calls `connect()`
again to test completion (POSIX allows this and mandates EALREADY
while in progress, EISCONN only once connected). It receives EISCONN,
concludes the handshake finished, and `write()`s — which reaches
`tcp_send_impl()` with the PCB in SYN_SENT and fails with ENOTCONN
(API-14). `getpeername()` in the same window also succeeds and hands
out an address for a connection that may still be refused. API-15 is
the third consequence.

**Fix.** Do not set `s->connected` on the EINPROGRESS path; derive the
predicate from the PCB state (via `tcp_endpoints()` or a new
`tcp_is_connected()`) so EALREADY, ENOTCONN and EISCONN follow the
real state machine.

### API-23 (low). CLOSE in SYN-SENT returns no "error: closing" and wakes nobody

**Requirement.** RFC 793 3.9, CLOSE Call, SYN-SENT
(`docs/rfc/rfc793.txt:3738`): *"Delete the TCB and return 'error:
closing' responses to any queued SENDs, or RECEIVEs."*

**Code.** The SYN_SENT arm of `tcp_close()`
(`sys/net/tcp.c:1678-1685`) sets `p->state = TCP_CLOSED` and unlocks.
It does not call `tcp_kill_pcb()`, so `so_error` is left at 0 and none
of `connect_chan`/`recv_chan`/`send_chan` is signalled — where the
LISTEN arm three cases above does exactly that via `tcp_kill_pcb()`
(`:1671`). Waiters are rescued only by the `TCP_SLEEP_POLL` safety
net, and when a blocked `tcp_connect()` re-checks, its CLOSED branch
at `:1287-1288` computes
`p->so_error ? -p->so_error : -ECONNREFUSED` and, with `so_error`
still 0, reports ECONNREFUSED.

**Failure.** Thread A calls `connect()` on a shared fd and blocks;
thread B closes that fd. After up to `TCP_SLEEP_POLL` ticks thread A
returns ECONNREFUSED — asserting that a peer refused a connection when
in fact the local application closed the descriptor. Nothing
distinguishes this from a genuine RST-on-SYN. A `tcp_recv()` blocked
on the same PCB sees `TCP_CLOSED` and reports EOF rather than an
error.

**Fix.** Replace the bare `p->state = TCP_CLOSED;` in that arm with
`tcp_kill_pcb(p, ECONNABORTED)` (after emitting the FIN that
SYN_RECEIVED needs, per API-20). That delivers the RFC's "error:
closing" as a distinguishable errno and wakes waiters immediately.

### API-24 (info). SEND in LISTEN does not convert the passive open to an active one

**Requirement.** RFC 793 3.9, SEND Call, LISTEN
(`docs/rfc/rfc793.txt:3496-3500`): *"If the foreign socket is
specified, then change the connection from passive to active, select
an ISS. Send a SYN segment, set SND.UNA to ISS, SND.NXT to ISS+1.
Enter SYN-SENT state."*

**Code.** `tcp_send_impl()`'s state gate (`sys/net/tcp.c:1419-1425`)
admits only ESTABLISHED and CLOSE_WAIT, so a send on a LISTEN PCB
returns `-ENOTCONN`.

Reported for completeness of the 3.9 SEND table rather than as a
defect to fix: the conversion depends on SEND carrying a foreign
socket, which `send(2)` does not, and POSIX requires exactly this
ENOTCONN. RFC 9293 3.10.2 retains the 793 wording, but no sockets-based
stack implements it. It is worth noting only because `connect()` *does*
take the conversion path in this stack (API-03), and does so unsafely
— so the two halves of one RFC mechanism are inconsistent here.

**Fix.** None required. Keep the ENOTCONN and treat POSIX as
superseding; a one-line comment at the state gate naming the SEND Call
LISTEN row would make the deviation explicit.

---

# Part 6 — interrupt latency and resource handling

No RFC mandates these, but they are all in TCP's own paths and all of
them degrade the reliability RFC 793 1.5 asks for (*"The TCP must
recover from data that is damaged, lost, duplicated, or delivered out
of order by the internet communication system"*) by dropping the very
segments that would drive recovery.

### RES-01 (medium). `tcp_timer_tick()` performs an unbounded number of full transmits with interrupts disabled

**Code.** `tcp_timer_tick()` takes `tcp_lock()` — an `intr_disable()`
(`sys/net/tcp.c:66`) — at `:457` and does not release it until `:518`.
Inside that region it walks all of `g_tcp_pcbs`, a list with no cap
anywhere in the file, and for every PCB whose RTO has expired calls
`tcp_retx_head(p)` at `:516`, which is a full inline transmit:
`tcp_xmit_raw` memcpy()s up to 1460 payload bytes (`:293`), runs
`inet_csum_pseudo4` over up to 1480 bytes as a 740-iteration byte-pair
loop (`sys/net/inet.c:43-65`), then `ip4_output` memcpy()s the whole
datagram again (`:247`), `eth_send` a third time (`:142`), and the
driver a fourth into its DMA buffer. The NET-02/NET-11 comment at
`sys/net/tcp.c:444-456` explains why the transmit moved inside the
lock (a genuine use-after-free) and records that the old 32-victim
batch cap was removed so *"every PCB whose RTO has expired is serviced
on this tick"* — but it reasons only about the UAF, never about how
long interrupts stay masked. Confirmed against the object file:
`tcp_retx_head` is a real call inside the locked region (`call
tcp_retx_head` at `tcp_timer_tick+0x215`, between `call tcp_lock` at
`+0x13` and `call tcp_unlock` at `+0x23f`).

The one thing this window excludes is the NIC RX interrupt — which is
where the arriving ACKs that would stop the retransmissions come from.

**Failure.** A link flap or congested path expires the RTO on N
connections at once (the common case: every PCB shares the same
`TCP_RTO_BASE_TICKS` and the same 125 ms tick, so they bunch). One
tick then does N inline transmits with IF=0 — ~6 KB of memcpy plus a
740-iteration software checksum each, tens of microseconds per segment
on the i486/Pentium-class target this kernel builds for. With N=100
that is several milliseconds of masked interrupts. The e1000 RX ring
is 32 descriptors (`sys/drivers/net/e1000.c:98`), about 384 µs of
buffering at gigabit line rate; the rtl8139 ring is 8 KiB
(`rtl8139.c:72`), roughly 600 µs at 100 Mb/s. Every frame arriving
after the ring fills is dropped by hardware, including the ACKs for
the segments just retransmitted, so those connections' RTOs expire
again next tick with `head->retx` incremented and the window grows —
self-amplifying until `TCP_MAX_RETX` kills them. The same window masks
the PIT, so `get_ticks()` stalls and the RTO arithmetic at `:493-495`
skews.

**Fix.** Do not transmit under the lock. Under `tcp_lock`, collect the
victims by bumping `p->holds` (which the reaper already honours at
`:465`/`:473`) into a bounded batch; drop the lock; transmit each with
interrupts enabled; release the holds. That gives the same
use-after-free protection NET-02 was after without an unbounded IF=0
region.

### RES-02 (medium). `tcp_close()`'s LISTEN arm emits a RST per child inline under the same lock

**Code.** `tcp_close()` takes `tcp_lock()` at `sys/net/tcp.c:1621` and
the LISTEN arm holds it across a walk of the entire `g_tcp_pcbs` list
(`:1662`) calling `tcp_send_ctl(q, TCP_RST | TCP_ACK)` at `:1670` for
every child — `tcp_xmit_raw` → `ip4_output` → `eth_send` →
`netdev_xmit` → driver, all with IF=0, from an ordinary `close(2)`.
The A45 comment at `:1649-1661` acknowledges the inline-under-lock
design and notes it *"drops the old 32-child cap"*, but, like NET-02,
reasons only about the use-after-free window. The list walk is O(total
PCBs) even though only children are acted on, and the loop is not
structurally bounded: `tcp_listen()`'s shrink path (`:1192-1197`)
truncates `accept_count` without clearing the children's `->parent`
(MEM-11), so children with `->parent == p` can outlive the cap.

**Failure.** A server with backlog 32 and 32 unaccepted children calls
`close()` on its listening fd. The syscall masks interrupts and issues
32 sequential RSTs through the full IP/Ethernet/driver path. On
rtl8139 each one can additionally burn the bounded TX-descriptor poll
— `rtl8139.c:209` drops the limit to 10000 PIO `inl()` reads when
`intr_enabled()` is false, which at roughly 1 µs per PCI port access
is up to 10 ms *per frame* — so a single `close()` can mask interrupts
for a large fraction of a second. Every frame the NIC receives in that
window past its ring is lost, including ACKs and data for every other
connection on the box, and the console, serial and timer IRQs are
equally starved.

**Fix.** Same shape as RES-01: under the lock, mark each child
detached and bump its `holds`, build a bounded list, drop the lock,
then send the RSTs with interrupts enabled and release the holds. The
hold is what makes it safe against the reaper, which is precisely the
race A45 describes.

### RES-03 (medium). Every arriving segment costs two full walks of the global PCB list in hard IRQ; a SYN costs three

**Code.** `tcp_find()` (`sys/net/tcp.c:558`) makes two separate linear
passes over `g_tcp_pcbs` — the 4-tuple pass at `:564` and the LISTEN
pass at `:572` — and is called once per segment from `tcp_input()`
(`:1076`). A SYN then pays a third full walk in `tcp_in_listen()`'s
backlog count (`:648-651`), which scans every PCB in the system to
find those whose `->parent` matches this listener. `g_tcp_pcbs` has no
cap and is a heap-scattered singly-linked list, so each pass is n
cache misses. n is not small in steady state: a PCB survives 60 s of
TIME_WAIT (`:100-101`) plus a further tick before the reaper collects
it, and holds its 32 KiB ring that whole time. The header comment at
`:5` acknowledges the data structure (*"Per-PCB list (linear; replace
with hash once profiling shows it)"*) but frames it as a throughput
question, not as a bound on how long the RX interrupt is masked.

**Failure.** A busy server accumulates ~1000 PCBs (active connections
plus 60 s of TIME_WAIT churn). Each arriving segment now costs ~2000
pointer dereferences before any protocol work — on the order of 200 µs
of masked interrupts per packet at 100 ns per miss. A stream of SYNs
costs ~3000 each. That exceeds the e1000's ~384 µs of RX buffering at
well under line rate, so frames are dropped indiscriminately: ACKs for
unrelated established connections are lost and their RTOs fire, adding
RES-01's window on top. The degradation is superlinear in n and needs
no forged packets. API-21 makes n cheap for a remote party to inflate.

**Fix.** Index the PCBs: a hash over the 4-tuple for the connected
table and a separate small table for listeners, so `tcp_find` is O(1).
Maintaining a per-listener child counter removes the `:649` walk
outright, and `tcp_child_in_accept_q()` (`:430-437`) can be replaced
by a flag on the child.

### RES-04 (medium). e1000 and r8168 TX spins are a flat 1,000,000 iterations with no interrupts-off adaptation

**Code.** `e1000_xmit` spins on the descriptor DD bit for up to
1,000,000 iterations (`sys/drivers/net/e1000.c:236-243`) and
`r8168_xmit` on `DESC_OWN` for the same count
(`sys/drivers/net/r8168.c:284-291`). Both comments claim the bound
exists so *"a wedged NIC returns an error instead of spinning forever
with interrupts off"* — but neither consults `intr_enabled()`, so the
bound is identical whether the caller can afford to wait or not.
rtl8139 got this right under RTL-06: `rtl8139.c:209` drops the limit
from 2,000,000 to 10,000 when `intr_enabled()` is false. The callers
that reach these with IF=0 are all in `tcp.c`: `tcp_retx_head` from
`tcp_timer_tick` under `tcp_lock` (`:516`), `tcp_send_ctl` from
`tcp_close`'s LISTEN arm (`:1670`), and every `tcp_send_ctl` /
`tcp_xmit_raw` call from `tcp_input`'s per-state handlers, which run
in hard-IRQ context on a NIC-delivered segment (`:727`, `:794`,
`:807`, `:842`, `:927`, `:937`, `:958`, `:963`, `:1006`, `:1033`).

This quantifies how bad RES-01's and RES-02's windows get: the timeout
is not a millisecond, it is a full 10^6-iteration poll of a
DMA-coherent descriptor word.

**Failure.** The TX ring wraps (a sustained upload, or a burst of
retransmits from one `tcp_timer_tick` pass) and the head descriptor is
still owned by the hardware. `tcp_timer_tick` is inside `tcp_lock`
with IF=0; `e1000_xmit` enters the spin and cannot exit for 1,000,000
reads of `d->status`. Even at an optimistic 10 ns per uncached read
that is 10 ms of masked interrupts for a single segment, entered once
per retransmitting PCB on that tick. The NIC's RX ring overflows two
orders of magnitude before the spin completes, so the ACKs that would
have retired the descriptor are dropped — the spin actively prevents
the event it is waiting for.

**Fix.** Mirror RTL-06: make the limit
`intr_enabled() ? 1000000u : <small>` in both drivers and return
`-EBUSY`/`-EIO` on the short bound so the upper layer's RTO
retransmits. The real fix is to stop calling into the driver with
interrupts disabled at all (RES-01, RES-02).

### RES-05 (medium). Every `SOCK_STREAM` socket allocates a ~50 KiB datagram ring it can never use

**Code.** `afinet_socket()` allocates
`sizeof(afi_pkt_t) * AFI_RING_LEN` unconditionally
(`sys/net/af_inet.c:839`) and `afinet_accept()` repeats it for each
accepted child (`:1019`). With `AFI_DATA_MAX = NETDEV_MTU_MAX - 28 =
1572` each `afi_pkt_t` is ~1596 bytes and the 32-slot ring is ~50 KiB.
A stream socket can never put anything in it: `sock_score()` rejects
any socket whose type is not `SOCK_DGRAM` before enqueue (`:1581`),
`afinet_node_read_body()` takes the TCP path at `:566-570`,
`afinet_recvfrom()` at `:1435-1448`, and `FIONREAD` at `:239-240`. So
each TCP connection costs ~83 KiB of kernel heap of which ~50 KiB is
unusable, on top of the PCB's own 32 KiB `rxbuf`
(`sys/net/tcp.c:1136`). (This is the same allocation the UDP audit
reported as RES-02; it is repeated here because the TCP accept path is
the second, per-connection instance of it.)

**Failure.** A server accepting 100 simultaneous connections burns
~5 MB of kernel heap no code path can read or write, on a 32-bit
kernel whose allocator serves the rest of the system. Under load the
extra pressure turns into the ENOMEM path at `:1020`, which then
aborts the newly accepted connection (API-12) — the "insufficient
resources" condition is manufactured by the allocation itself.

**Fix.** Allocate `s->ring` only for `SOCK_DGRAM`/`SOCK_RAW`; leave it
NULL for `SOCK_STREAM` (the ring accessors are already behind
type/tcp checks).

---

# Part 6b — precedence and security (RFC 793 3.6)

### SEC-01 (low). Precedence and security/compartment are absent end to end

**Requirement.** RFC 793 3.6
(`docs/rfc/rfc793.txt:2541-2546`): *"A connection attempt with
mismatched security/compartment values or a lower precedence value
must be rejected by sending a reset."* 3.2 lists both among the TCB
variables (*"Among the variables stored in the TCB are the local and
remote socket numbers, the security and precedence of the
connection"*, `:1322-1324`). 3.6 binds even a default-precedence
implementation (`:2558-2561`): *"Note that TCP modules which operate
only at the default value of precedence will still have to check the
precedence of incoming segments and possibly raise the precedence
level they use on the connection."*

**Code.** Three layers are missing, and each would have to be fixed
before the next could be.

1. `tcp_pcb_t` (`sys/net/tcp.c:143-210`) carries no
   security/compartment and no precedence field, so all three of the
   RFC's check sites are absent and none of the resets it mandates is
   reachable: `tcp_in_listen()` (`:621-677`) goes from the flag tests
   straight to backlog accounting with no security check;
   `tcp_in_syn_sent()` (`:680-729`) implements only the ACK and RST
   checks, with the whole of "third check the security and precedence"
   (`docs/rfc/rfc793.txt:4142-4174`) absent; and
   `tcp_in_established()` (`:776-1035`) has no step corresponding to
   `:4371-4387`.
2. The IP/TCP interface discards the inputs those checks compare.
   `ip4_input()` computes `hlen` from IHL (`sys/net/inet.c:299`) and
   passes only the layer-4 slice upward (`const uint8_t *l4 = pkt +
   hlen;` at `:344`, `tcp_input(ih->saddr, ih->daddr, l4, l4_len)` at
   `:360`), so any IP options — where a security option would live —
   are skipped without being parsed, and `grep -n 'ih->tos'
   sys/net/inet.c` finds a single hit, the assignment at `:241` in the
   *output* path, with no read anywhere. `tcp_input()`'s signature
   (`sys/net/tcp.c:1040`) takes only addresses, segment and length.
3. `ip4_output()` writes `ih->tos = 0;` unconditionally
   (`sys/net/inet.c:241`) and its prototype
   (`sys/include/net/inet.h:84`) takes no TOS argument, so
   `tcp_xmit_raw()`'s call at `sys/net/tcp.c:304` cannot pass one.
   Note the substitution first: TOS 0 is exactly the default RFC 793
   3.8 prescribes (`docs/rfc/rfc793.txt:3201-3202`), and the TTL of 64
   at `sys/net/inet.c:245` is the RFC 1122 default rather than 793's
   one minute, which is correct modern behaviour and not reported.
   What is absent is any way to *depart* from the default.

Graded **low** because RFC 793's security/compartment rests on the
IPv4 security option, which is historic, and RFC 9293 — which
obsoletes 793 — does not carry these requirements forward as MUSTs. It
is still a normative gap against the RFC this audit is against.

**Failure.** A strict RFC 793 peer opens a connection at a raised
precedence (per 3.8, *"The precedence for the connection is the higher
of the values requested in the OPEN call and received from the
incoming request"*). Substrate never inspects SEG.PRC, never raises a
TCB.PRC it does not have, and accepts the connection; a segment whose
security/compartment does not match is accepted as ordinary in-window
data and delivered to `recv()` instead of eliciting the reset the RFC
requires. In the reverse direction the peer, which does implement 3.4
case 3, sees every reply carry precedence 0 against its TCB.PRC, sends
a reset, and substrate's application gets a bare ECONNRESET with no
diagnosis available anywhere in the stack.

**Fix.** If the mechanism is to stay out, say so in the header comment
at `sys/net/tcp.c:1-28`, whose "Still TBD" list does not mention it,
so the omission is acknowledged rather than silent. If it is to go in:
add `sec`/`compartment`/`prc` fields to `tcp_pcb_t`; widen
`tcp_input()` so `ip4_input()` can pass the TOS byte and the IP option
area; give `ip4_output()` a `tos` argument (defaulting to 0 for the
existing ICMP/UDP/raw callers) and plumb the PCB's precedence through
`tcp_xmit_raw()`; and add the three check steps at the RFC's stated
positions — before the SYN handling in LISTEN, as step three in
SYN-SENT, and after the sequence check in the synchronized states
(`:4389-4392` explains why that ordering matters).

---

# Part 7 — mechanisms RFC 793 requires that are simply ABSENT

`tcp.c:2` calls itself a "793 subset" and `tcp.c:27` lists four known
gaps. This is the real list, ordered by what a reader should build
first. Each entry names the findings it closes, so the work can be
scheduled against the table above.

**Tier 1 — the stack is not safe without these.**

| # | Absent mechanism | Closes | Why first |
|---|------------------|--------|-----------|
| 1 | **A real lock.** `tcp_lock()` must exclude the loopback RX kthread, not just hardware interrupts. | MEM-01, MEM-02, MEM-05, MEM-06, MEM-08, MEM-09, MEM-11, MEM-12, MEM-13 | Nine findings, four of them memory-safety, all one root cause. Every other fix is written against a locking model that does not hold. |
| 2 | **`tcp_hold()` on the two blocking paths that lack it**, and on `tcp_input()`'s PCB. | MEM-02, MEM-03, MEM-04 | Three use-after-frees, one of which copies freed kernel heap to userspace. Mechanically small. |
| 3 | **A deadline on every closing state**, and no `return` that can skip a completion test. | SM-01, API-19 | Unbounded remote-driven kernel-memory leak from an ordinary lost ACK. |

**Tier 2 — RFC 793 3.9 steps that are not implemented at all.**

| # | Absent mechanism | Closes |
|---|------------------|--------|
| 4 | **The 3.3 four-case segment acceptability test**, run first, with the mandated `<SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>` reply for an unacceptable segment. | SM-02, SM-03, SM-13 |
| 5 | **Segment trimming**, left and right edge, including SYN and FIN. | SM-05, WIN-09 |
| 6 | **Step 4 (SYN bit) and step 5's ACK-off gate** in the synchronized states. | SM-06, SM-12 |
| 7 | **Step 6 (URG) in both directions**: RCV.UP, SND.UP, the URG bit on the wire, and a notification channel (SIGURG, POLLPRI, SIOCATMARK, MSG_OOB). | URG-01 … URG-06 |
| 8 | **RST generation from LISTEN and SYN-RECEIVED** (3.4 Reset Generation rules 1 and 2). | SM-07, SM-09 |
| 9 | **The SYN-SENT ACK-bit check as a first-class step** (figure 10 half-open discovery) and **simultaneous open** (figure 8). | SM-10, SM-11 |
| 10 | **Text and controls carried on the handshake segments**, queued rather than discarded. | SM-09, API-14 |

**Tier 3 — 3.7 machinery. Every item here is a MUST in RFC 1122.**

| # | Absent mechanism | Closes |
|---|------------------|--------|
| 11 | **A real persist timer**, decoupled from the retransmission budget and reachable from the non-blocking send path. | WIN-01, WIN-02 |
| 12 | **A recorded advertised window (`rcv_wnd` actually read)**, so window updates are measured against what went on the wire. | WIN-03, WIN-07, WIN-11 |
| 13 | **Separate counters for RTO backoff, fast-retransmit count and the abort budget.** | WIN-04 |
| 14 | **Sender SWS avoidance and Nagle.** | WIN-06 |
| 15 | **An out-of-order reassembly queue** (bounded by bytes and by segment count). | WIN-08 |
| 16 | **SND.WL1 / SND.WL2.** | WIN-12 |
| 17 | **SRTT / RTTVAR (RFC 6298 2.2-2.3) with Karn's algorithm.** | WIN-14 |
| 18 | **A user timeout**, per-connection and settable, checked before the empty-queue `continue`. | WIN-13 |

**Tier 4 — 3.1 header fields and 2.7 connection identity.**

| # | Absent mechanism | Closes |
|---|------------------|--------|
| 19 | **TCP option parsing and emission** (EOL/NOP/MSS), a per-PCB send MSS, and an MSS derived from the route's MTU. | HDR-02, HDR-03, HDR-04 |
| 20 | **A source address agreed between the pseudo header and the IP header** — i.e. an `ip4_output()` that takes one. | HDR-01 |
| 21 | **Local-socket uniqueness enforced at the PCB layer**, keyed on the 4-tuple, covering detached and TIME-WAIT PCBs, with a most-specific-match listener lookup. | API-04, API-05, API-09 |
| 22 | **An RFC 6528 ISN** (monotonic `M` plus a keyed hash), not a bare random draw. | HDR-05 |

**Tier 5 — 3.8 user-interface calls that do not exist.**

| # | Absent mechanism | Closes |
|---|------------------|--------|
| 23 | **ABORT**, wired to `SO_LINGER {1,0}` and to the `accept()` failure paths. | API-11, API-12 |
| 24 | **State-driven errors from SEND** ("connection closing" vs "connection does not exist"), plus SIGPIPE and a `POLLOUT` that means something. | API-13, API-14, API-15 |
| 25 | **Error returns from OPEN** — `tcp_connect_start()` must be able to fail. | API-08, API-10 |
| 26 | **Precedence and security/compartment** (3.6), including the IP-layer plumbing to carry them. | SEC-01 |

---

# Part 8 — what conforms

This stack is not a toy, and a reader who only skimmed Parts 0-6 would
get the wrong impression. Each item below was verified against the
source in this pass.

**Header and checksum.**
- The TCP checksum is verified on input **before any field is acted
  on** (`sys/net/tcp.c:1070`), covers the pseudo header, header,
  options and data, and correctly treats a zero checksum as simply a
  value to check rather than "not computed" — the TCP-02 comment at
  `:1056-1069` states the reasoning precisely. A bad segment is
  dropped silently, with no reply, as RFC 793 requires.
- Data Offset is bounds-checked at `:1046-1047` (`hlen < sizeof(*th)
  || hlen > len`), so `dlen` cannot underflow and there is no
  over-read of the option area.
- All sequence arithmetic uses wrap-safe `(int32_t)(a - b)` signed
  differences throughout, which is the correct idiom for RFC 793 3.3's
  modular sequence space.
- `tcp_send_rst()` (`:581-617`) emits both forms RFC 793 3.4
  prescribes — `<SEQ=SEG.ACK><CTL=RST>` when the offending segment
  carried ACK, `<SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK>` when it did
  not — and counts SYN and FIN in SEG.LEN (`:600-605`). The TCP-27
  comment records that both were previously wrong.
- A RST is never sent in reply to a RST (`:584`).

**State machine.**
- Only a clean SYN opens a connection at LISTEN: SYN|RST, SYN|ACK and
  SYN|FIN are all refused (`:632-640`, TCP-28), so a listener cannot
  be made to spawn half-open state by a malformed segment.
- SYN-SENT validates SEG.ACK before establishing and replies
  `<SEQ=SEG.ACK><CTL=RST>` for an unacceptable one (`:708-712`, A71).
- The upper bound `SEG.ACK <= SND.NXT` is enforced in the synchronized
  states with the RFC's required empty-ACK reply (`:838-843`, TCP-03).
- RST validation in ESTABLISHED implements RFC 5961 3.2 exactly:
  out-of-window drops silently, in-window-but-not-RCV.NXT yields a
  challenge ACK, only RCV.NXT resets (`:791-797`).
- RST validation in SYN-SENT requires an ACK that covers our SYN
  (`:682-697`, TCP-09).
- A FIN is consumed only when in order (`:931`), and a *retransmitted*
  FIN is re-acknowledged (`:924-929`, TCP-13) — the right idea, spoiled
  only by the `return` of SM-01.
- Simultaneous close reaches CLOSING correctly, and only an ACK that
  genuinely covers our FIN completes the close (`:952-957`, TCP-14).
- TIME-WAIT is a real 2×MSL of 60 s (`:100-101`, TCP-12), and a SYN
  arriving in TIME-WAIT gets the RFC 5961 4 challenge ACK
  (`:1005-1008`, TCP-24).
- FIN-WAIT-2 has a bounded deadline so an unfinished close cannot leak
  a PCB forever (`:479-483`, TCP-05).
- The reaper refuses to free a PCB a blocked caller is holding
  (`:465`, `:473`) and never frees from the RX path.

**Window and congestion.**
- The send window is honoured: `tcp_send_impl()` bounds in-flight
  bytes by `min(snd_wnd, cwnd)` (`:1433-1442`) — the header comment's
  "Still TBD: send window/cwnd" is out of date.
- RFC 5681 congestion control is implemented and correct in outline:
  slow start and congestion avoidance with wrap-safe clamps
  (`:852-865`), `cwnd` collapse to one MSS plus `ssthresh` halving on
  an RTO (`:508-515`), and `ssthresh`/`cwnd` halving on three
  duplicate ACKs (`:889-893`).
- The initial window is a deliberate, documented 3×MSS rather than RFC
  6928's IW=10 (`:722-726`, `:749-753`) — a conservative choice that
  is conformant with RFC 5681 3.1.
- The RTO's initial value is RFC 6298 3.1's mandated 1 s, with
  exponential backoff per attempt and a 60 s ceiling (`:88-89`,
  `:494-495`, TCP-06), replacing a set of constants that were all half
  their documented value.
- The duplicate-ACK counter is seeded at establishment so the *first*
  duplicate counts, and re-armed after firing so a later burst can
  fire again (`:872-882`, `:720`, `:747`, TCP-33).
- The receiver does send an unsolicited window-update ACK at all
  (`:1518-1525`) — the condition is wrong (WIN-03), the intent is
  right.

**Connection identity and resources.**
- Ephemeral ports are drawn from the CSPRNG in the IANA dynamic range
  and checked against live PCBs, replacing a `++next_eph` counter
  (`:1241-1251`, TCP-07).
- The ISN is drawn from the kernel CSPRNG per call, with a
  never-constant fallback, replacing a fixed-seed LCG (`:265-271`,
  TCP-08) — RFC 6528's *security* goal is met even though its
  monotonicity is not (HDR-05).
- `tcp_listen()` on an already-listening socket changes the backlog
  without leaking the old queue (`:1191-1207`, TCP-32).
- Closing a listener resets and detaches every child so no peer is
  left believing a connection is up (`:1662-1672`, A45).
- A never-accepted child that dies in the handshake is reaped rather
  than leaking its ring until the listener closes (`:473-475`,
  NET-04).
- `tcp_free()` orphans a listener's children under the lock so a later
  segment cannot dereference a freed parent (`:1164-1166`).

**Socket layer.**
- Broadcast and multicast TCP segments are discarded at
  `ip4_input()` (`sys/net/inet.c:337-341`, `:363`, IP-03) per RFC 1122
  4.2.3.10, closing a one-frame N-reflected-RST amplifier.
- `poll()` reports `POLLIN` — not merely `POLLHUP` — once the peer has
  closed and the ring is drained, which is what POSIX and Linux do and
  what libtirpc's `svc_vc` requires (`sys/net/tcp.c:1328-1345`).
- `poll()` advertises `recv_chan` whenever the caller asked for
  `POLLIN` and has no data, rather than gating on `!revents`
  (`:1358-1359`).
- A blocked `accept()` on a closed listener returns instead of
  sleeping forever (`:1399-1402`, TCP-19).
- `read()` propagates a negative return as `(size_t)-errno` rather
  than collapsing it to 0, so an EINTR'd recv is not forged into an
  EOF (`sys/net/af_inet.c:569-574`).
- `recv()` on a listening socket returns ENOTCONN and does not block
  (`:1436-1438`) — the `read()` path is the one that was missed
  (API-17).
- `accept()` copies the endpoints into the new socket so
  `getpeername()` works (`:1024-1041`).
- `setsockopt` copies `optval` in rather than dereferencing a user
  pointer in the kernel (`sys/net/af_unix.c:2610-2614`, NET-03).
- `ip4_output()`'s length check subtracts from the buffer size rather
  than adding to the payload length, so it cannot wrap
  (`sys/net/inet.c:216-231`).
- `tcp_close()` refuses a PCB pointer below `0xC0000000` rather than
  triple-faulting on a corrupted `->tcp` (`sys/net/tcp.c:1612-1615`).

---

# 793 or 9293?

This audit is against **RFC 793**, because the code is written against
793: `tcp.c:2` names it, the per-state handlers follow 3.9's SEGMENT
ARRIVES structure, and the `TCP-nn`/`NET-nn`/`A45`/`A71` comments cite
793 section numbers throughout. RFC 9293 obsoletes 793 and rolls in
the errata and the host requirements; where it differs, it is noted
below rather than used as the yardstick.

Substrate frequently **does** follow a later RFC in preference to 793,
and does so correctly. Those substitutions are not defects and are not
reported as such:

- **RFC 5961 3.2 in place of 793's in-window RST rule**
  (`sys/net/tcp.c:791-797`): an in-window RST that is not exactly at
  RCV.NXT draws a challenge ACK rather than tearing the connection
  down. This is strictly stronger than 793 and is the right call. The
  defect (SM-04) is that the same test was never wired into
  SYN-RECEIVED, not that the substitution was made.
- **RFC 5961 4's challenge ACK for a SYN in TIME-WAIT**
  (`:1005-1008`): again stronger than 793's blind reset. SM-06 is that
  it was applied to one state only.
- **RFC 6298 3.1 and 5.5 in place of 793 3.7's ALPHA/BETA sketch**
  (`:88-89`, `:494-495`): the 1 s initial RTO and per-attempt
  exponential backoff are exactly what 6298 mandates, and 793's
  illustrative `SRTT = (ALPHA * SRTT) + ((1-ALPHA) * RTT)` is
  correctly not implemented. WIN-14 is that 6298's *estimator* is
  missing too, so the initial value is never replaced.
- **RFC 5681 in place of nothing** (`:852-865`, `:508-515`,
  `:884-895`): 793 has no notion of congestion control at all. WIN-04
  and WIN-05 are failures against RFC 5681's own rules — §3.2's
  fast-retransmit definition and §2's five-condition duplicate-ACK
  test — not against 793.
- **RFC 6928's IW=10 deliberately declined** in favour of 3×MSS
  (`:722-726`): a documented conservative choice, conformant with
  RFC 5681 3.1.
- **RFC 1122 4.2.3.5's R1/R2 in place of 793's user timeout**: the
  substitution is legitimate; `TCP_MAX_RETX` is meant to be R2. WIN-13
  is that it is implemented per *segment*, not per connection, and
  that 793's settable timeout has no replacement at the API.
- **RFC 1122 4.2.2.13 for a SYN in TIME-WAIT**: the code documents
  declining the "accept a new incarnation" option and sending a
  challenge ACK instead (`:994-1004`). That is a permitted choice.
- **RFC 1122's TTL default of 64** rather than 793 3.8's "one minute"
  (`sys/net/inet.c:245`): correct modern behaviour, not reported.

Where a later RFC **raises** a 793 suggestion to a requirement, the
finding is graded against the stricter text and says so:

- RFC 1122 4.2.2.6 makes the MSS option a MUST in both directions
  (HDR-02, HDR-03).
- RFC 1122 4.2.3.3 and 4.2.3.4 make receiver SWS avoidance, sender SWS
  avoidance and Nagle MUSTs (WIN-03, WIN-06, WIN-07).
- RFC 1122 4.2.2.17 makes "MUST NOT abort a connection because the
  window stays zero" explicit (WIN-01).
- RFC 1122 4.2.2.4 keeps the urgent mechanism mandatory and fixes its
  one-octet ambiguity; RFC 6093 deprecates *using* it but still
  requires implementations to support it; RFC 9293 3.8.5 keeps both
  (URG-01 … URG-06).
- RFC 6528 supersedes 793 3.3's ISN generator for security reasons but
  retains its monotonic clock term, so HDR-05 is a failure against
  both.

The one place RFC 9293 makes this audit **more lenient** than a strict
793 reading is **SEC-01**: 9293 does not carry 793 3.6's precedence
and security/compartment checks forward as MUSTs, which is why a
mechanism that is entirely absent is graded low rather than high.
