# xHCI re-audit (pass 6) — 2026-08-09

Sixth pass, and the first run **against a live hardware failure**: the HP
Pavilion (Sunrise Point-LP, 8086:9d2f) boots the SD card, brings the
controller up (18 ports, 34-page scratchpad served), enumerates five of six
devices — and times out the Intel Bluetooth's config-descriptor read while the
Alcor card reader, the root device, never enumerates at all.  QEMU reproduces
none of it.

Scope: **every line this session changed**, `419dd982d..HEAD` over
`sys/drivers/usb/` (+730/−107), audited by three parallel worktree agents
against xHCI 1.2 (May 2019), with the PCI 3.0 spec, both BSDs and Linux as
cross-references.  The premise: five audit passes of code written largely by
the same process that audited it deserve an adversarial re-read, and the code
written *during* the fixes is the least-reviewed code in the driver.

Full working papers: `~/.cache/substrate-specs/audit-p6-{init,slotep,iso}.md`.

## Status

**All six findings fixed** in `6aa47fe83`, same day.  QEMU regressions
(root-over-USB-MSC, two-tier hub chain) pass with zero xHCI failures; the
decisive test is the next Pavilion boot.

| ID | Severity | One-line | Pavilion-relevant? |
|----|----------|----------|--------------------|
| P6-SLOT-01 | **High** | SET_ADDRESS reused an input context whose EP0 dequeue pointer still held the ring base — the BSR=0 Address Device rewound EP0 onto consumed TRBs | **Yes** — per-device, scales with pre-address traffic; matches the BT's post-address timeout |
| P6-INIT-01 | **High** | The HW-01 BAR-size probe wrote all-ones into a live, decode-enabled BAR before the BIOS handoff | **Yes** — present in the "nothing changed" retest; pokes firmware-owned state |
| P6-SLOT-02 | **High** | Recovery trusted one read of an EP State the spec says may lag the error; a Context State Error bounce abandoned the endpoint | **Yes** — one transient FS link error → EP0 dead for the session |
| P6-INIT-02 | Medium | No delay between asserting HCRST and the first register read-back; Intel erratum (NetBSD/FreeBSD/Linux all delay) | Possible — Sunrise Point is the errata silicon |
| P6-SLOT-03 | Low | Set TR Dequeue omitted SCT=1 for streamed endpoints (Table 6-68) | No (UAS only) |
| P6-ISO-01 | Low | P5-03's Ring-Underrun-flood premise was false (§4.11.2.3: one event, then the EP parks until the doorbell); comments retracted, mechanism kept | No |

## The three Highs, briefly

**P6-SLOT-01.**  §4.6.5: Address Device with BSR=0 "copies all fields of the
Input Endpoint 0 Context to the Output" — including the TR Dequeue Pointer.
The intercept at `xhci_control()` reused `s->in_ctx` untouched since
`xhci_setup_slot()`, so that field still named the ring base with DCS=1 while
EP0's real position had advanced through every pre-address descriptor read.
The command therefore rewound the controller onto TRBs it had already
executed, with cycle bits still reading as owned, and it re-ran the stale TDs
at the new address.  How much harm that does depends on how many pre-address
TDs exist — retries multiply them — making the failure *per-device*, and
QEMU's model recomputes its dequeue lazily so it cannot show it.  Fix: write
the current enqueue + cycle into the input EP0 context immediately before the
command (Linux's copy-forward, NetBSD achieves the same by resetting ring and
context together).

**P6-INIT-01.**  The first Pavilion fix (HW-01) called `pci_bar_size()` —
which writes `0xFFFFFFFF` into the BAR and restores it — *after* memory
decode was enabled and *before* the BIOS handoff, i.e. against a controller
SMM still owned and was actively driving as the firmware's boot-disk HCD.
PCI 3.0 §6.2.5.1 requires decode disabled around sizing; FreeBSD, NetBSD and
Linux all comply.  So the retest kernel carried a new way to anger the
firmware even as it fixed the window.  Fix: probe with decode off, before
first enable.

**P6-SLOT-02.**  §4.8.3: the output EP State write "may be delayed" relative
to the STALL or transaction error that caused it, and software "should not
depend on EP State."  P4-01's dispatch read the state once; a stale Running
answered with Stop Endpoint, which bounces with Context State Error on an
endpoint that had just become Halted, and the code returned −1 — no Reset, no
Set TR Dequeue, endpoint dead for the session.  Fix: a Context State Error
bounce now re-reads and re-dispatches (bounded at three laps; the bounce's own
round trip is ample settling time).

## Corrections to earlier passes

* **P5-03's justification is retracted** (mechanism kept): Ring Underrun is
  raised once on first empty-ring detection and the xHC removes the endpoint
  from the Pipe Schedule until the next doorbell (§4.11.2.3, §4.10.3.1).  The
  1000-events/s flood never existed.  The audit trail stands corrected here
  and in the three code comments.
* **P4's recovery design** was directionally right (state, not completion
  code) but incomplete without the §4.8.3 staleness caveat — see P6-SLOT-02.

## Verified correct by this pass (rollup)

Command-TRB field placement for every command built (§6.4.3, bit-for-bit
against FreeBSD's `xhci_cmd_*`); the LS=2/FS=1/HS=3/SS=4 speed map; §4.5.2
slot-context initialization; A0-only Configure Endpoint legality (Table 6-16
"shall be evaluated") with the P5-02 poll covering the stricter §4.5.2 latch
reading; the hub poll ordering after SET_CONFIGURATION; an independent
re-derivation of the route-string nibble order (P5-06's fix confirmed); BIOS
handoff semaphore byte offsets and the USBLEGCTLSTS RsvdP/RW1C mask
bit-for-bit against Table 7-5 and Linux; `XHCI_PORT_CLEAR` == FreeBSD's
`XHCI_PS_CLEAR`; `pci_bar_size()` MEM64 math (0x10000 for this BAR); reset
CNR/HCH gating; MFINDEX counting on an empty periodic schedule (§4.14.2 —
iso is sound at stream start); command-abort event ordering (§4.6.1.2 —
Command Aborted strictly precedes Command Ring Stopped); TBC/TLBPC=0 as the
computed values for single-packet FS isoch TDs; Set TR Dequeue DCS pairing
across arbitrary ring wraps; `XHCI_CC_HALTS_EP` complete and numerically
exact; the port survey side-effect-free.

## Open

P5-05 (IST vs scheduling lead) remains the only recorded-open finding across
all six passes.  Hardware confirmation of everything QEMU cannot model now
rides on the next Pavilion boot: the kernel prints `win=`, the handoff
outcome, and per-port PORTSC, so one photo decides HW-01, P6-INIT-01/02 and
the fate of the card reader.
