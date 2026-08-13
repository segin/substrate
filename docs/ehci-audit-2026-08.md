# EHCI driver audit — 2026-08

Differential audit of `sys/drivers/usb/ehci.c` (856 lines) + `ehci.h` against:

- **EHCI Specification for USB, Revision 1.0** (Intel, 3/12/2002) — read from the
  PDF, page-calibrated (PDF page = printed spec page + 10)
- **FreeBSD** `sys/dev/usb/controller/ehci.c` / `ehci_pci.c` / `ehcireg.h`
- **NetBSD** `sys/dev/usb/ehci.c` / `ehcireg.h`

Method: six analysis lenses (init/registers, port ops, qTD/QH construction,
async schedule, periodic/interrupt, BSD differential), every finding
adversarially verified by an independent agent that re-read the cited spec
pages and code. 41 findings raised, **32 confirmed, 9 refuted**. Severity is
graded for the real runtime targets: QEMU `usb-ehci` and ICH-class PCs. The
driver's synchronous polled single-transfer design is a given; findings are
spec violations / latent bugs in what it *does* do, not missing features.

## Confirmed defects (execution order)

| # | slug | sev | spec | one-line |
|---|------|-----|------|----------|
| 1 | qh-arm-park | blocker | 3.6.3, 4.10.2 | `ehci_run_qh` re-arms the live, advanceable async QH overlay_next-first: HC can seize the chain mid-update and the trailing `overlay_token=0` store scribbles an HC-owned overlay. Park the overlay HALTED (one atomic store), program, publish with one final token store. |
| 2 | halt-classify | minor | Table 3-16 | Every Halted retirement returns `USB_XFER_STALL`, conflating XactErr / babble / buffer-error / missed-uframe with a functional stall (drives wrong recovery: clear-halt vs reset). Classify by the error bits. |
| 3 | quiesce-split-recheck | minor | 4.8, spec p71-72 | Timeout path: (a) `ehci_quiesce_async` re-flips ASE even when ASS never deasserted (forbidden by the 4.8 ASE==ASS rule) and scribbles on memory the HC may still walk; (b) a completion landing between the last token sample and the stop is discarded as TIMEOUT. Split stop/restart, fail the controller if the stop fails, re-read outcome after stopping. |
| 4 | toggle-from-overlay | major | 4.10.3 p83-84 | Bulk/intr ending toggle computed by packet-count parity — misses a device's terminating ZLP (k·MPS short read = k+1 toggles) and is never run on timeouts. Read the QH overlay `dt` (guaranteed written back per 4.10.3; the qTD copy is NOT guaranteed per 4.10.4), gated on `current_qtd != 0`. |
| 5 | nrl-intr-zero | major | 4.9 p77, 4.10.3 p83 | RL=4 programmed into periodic interrupt QHs; 4.9: "Software **must** use this selection [RL=0] for interrupt endpoints". The periodic schedule has no reload machinery, so on throttling silicon a NAKing interrupt endpoint goes deaf after 4 NAKs. QEMU doesn't emulate the throttle — real-HW-only. |
| 6 | ctrl-altnext-status | minor/latent | 3.5.2 p41 vs 4.10.2 p81 | The spec contradicts itself on short-packet advance with alt_next=T (3.5.2: "will always use this pointer... retired due to short packet"; 4.10.2: T-bit falls back to Next). QEMU+ICH implement 4.10.2 so this is latent, but 3.5.2-literal silicon never executes the status stage after a short control IN (in-tree trigger: 11-byte hub-descriptor read answered with 9). Point the data qTD's alt_next at the status qTD — correct under both readings; what NetBSD does (FreeBSD uses a HALTED dummy). |
| 7 | shutdown-hcreset | major | Table 2-9, ch 5 | No shutdown path anywhere: across a warm reboot the still-running HC keeps DMAing the old kernel's schedule pages (untraceable early-boot corruption on real ICH) and port routing stays stolen from firmware. Add `.shutdown` (halt, CONFIGFLAG=0, HCRESET), add `device_shutdown_all()`, call it from `sys_reboot`. `device_shutdown()` existed but had zero callers. |
| 8 | port-reset-timeout | minor | 2.3.9, 4.2.2 | Stuck PR after the 50 ms wait falls through to a bogus OWNER handoff with PR still asserted (PED is meaningless before PR reads 0). Fault-path only; detect and fail. |
| 9 | kstate-ped-gate | minor | 2.3.9 Table 2-16 | Line Status is valid **only when PED=0**; the K-state companion handoff check runs unconditionally, so a re-reset of an enabled port can misroute a high-speed device to a (possibly absent) companion. |
| 10 | portsc-w1c-mask | minor | 2.3.9 | Four PORTSC read-modify-writes (both OWNER handoffs, reset-deassert, power-up loop) write back the CSC/PEC/OCC W1C change bits, silently acknowledging pending changes. Central `EHCI_PORT_CLEAR` mask. |
| 11 | usbsts-dead-hc | minor | 2.3.2 bit 4 | Interruptless driver never reads USBSTS in its poll loops, so Host System Error / unexpected HCHalted (HC clears RUN itself on PCI abort) burns the full timeout on the dead transfer and 1 s on every transfer thereafter, forever, silently. Throttled USBSTS check + one-shot diagnostic + fail-fast `hc_failed`. |
| 12 | start-run-verify | minor | 2.3.1/2.3.2 | `ehci_start` never confirms the controller left HALTED after setting RS; a stuck controller is registered as a live HCD. Poll HCHalted clear (both BSDs treat this as fatal: "run timeout"). Plus a FreeBSD-style diagnostic on the halt-timeout fall-through in `ehci_reset_controller`. |
| 13 | mpl-clamp | note | Table 3-19 | QH Max Packet Length masked 0x7FF but the architectural max is 0x400; malformed descriptor programs a forbidden value. Clamp. |
| 14 | port-enable-honest | note | 2.3.9 | "Software cannot enable a port by writing a one to this field" — `ehci_port_enable(…, 1)` writes PED=1 and reports success. Zero in-tree callers; make enable=1 report the truth. |
| 15 | bar64-relocate | note | — | 64-bit BAR0 accepted but the high dword is never read; an above-4G assignment maps truncated garbage. Mirror xhci.c's `pci_relocate_bar32` pattern. |

## Verified clean — do not re-litigate

- Init ordering (CTRLDSSEG/USBINTR/lists/USBCMD/CONFIGFLAG-last per 4.1),
  FLS/ITC legality, one-shot RUN|ASE|PSE write with ASE==ASS==0, USBSTS needs
  no init-time ack (Table 4-1 post-reset defaults).
- Quiesce-by-ASE-disable is a spec-sanctioned reclamation handshake (4.8.2);
  the doorbell is the *other* sanctioned method, not the only one.
- PED=1 ⇒ high-speed inference; 50 ms PR hold (spec floor 10 ms) + 2 ms
  completion bound; K-state handoff concept (gate fixed by item 9).
- qTD buffer-pointer programming (page 0 offset + successive 4K pages), the
  20480-byte limit (exactly legal because `dma_alloc_coherent` is
  page-granular — if that changes, the length caps must subtract the
  first-page offset), residue read from the qTD (4.10.4 guarantees Total
  Bytes/CErr/Status write-back), control-stage toggles (SETUP=0, data=1,
  status=1 with DTC), CERR=3.
- S-mask=0x02 / C-mask=0x38 split-interrupt pattern (legal Case-1 layout,
  same shape as the spec's own example shifted one µframe; no FSTN needed).
  Frame-list link/unlink protocol incl. the 2-frame FRINDEX drain.
- HS bInterval 1–3 floored to 1 ms polling: deliberate, matches BSD practice
  for a polled driver.
- Legacy BIOS handoff walk; no PCI quirks needed for QEMU/ICH targets.

## Refuted (do not re-introduce)

- "HCRESET after halt-wait timeout is a defect" — FreeBSD does the identical
  fall-through (its printf is triage, not correctness), NetBSD doesn't poll
  HCHalted at all; reset-anyway is the correct escalation. Kept as a
  diagnostic-only improvement (item 12).
- "NRL=4 on the H=1 head QH violates the spec" — no such prohibition exists
  for *async* QHs; 4.9's RL=0 mandate is for interrupt endpoints only.
- "OWNER handoff should check HCSPARAMS N_CC first" — handoff of a FS/LS
  device on a companion-less controller routes the port to nothing, which is
  exactly where a device the EHCI cannot serve belongs; no defect.
- "Missing compiler barrier before the frame-list link publish" — the link
  store is a single volatile-qualified u32 through the coherent mapping;
  ordering is already guaranteed on i386 TSO for that pattern.
- "CTRLDSSEG write should be gated on the 64-bit capability" — writing 0 is
  a legal no-op either way (BSDs gate it as style, not correctness).
- "OS semaphore should be asserted even when the BIOS semaphore reads clear"
  — ch 5 requires the claim only to *take* ownership from a BIOS that holds
  it; SMI disarm below covers the rest.

Fixes land one commit per item, each regression-tested with
`ehcitest` (usb-ehci: HID via QMP injection on the periodic schedule +
root-on-usb-storage on the async schedule) — baseline before any fix:
31/31 reports, both boots PASS.
