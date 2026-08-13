# USB HCD refactoring plan — 2026-08

Design-panel output: four independent lenses (EHCI internal structure,
cross-HCD unification, API/error model, testability) proposed 32 refactorings
against sys/drivers/usb/{uhci,ehci,xhci}.c after the EHCI spec audit
(docs/ehci-audit-2026-08.md); an adversarial critic then verified every
file:line claim against the tree, merged overlaps, and ranked what survived.
None were killed for false premises; three were corrected (noted inline).
Suggestions only -- nothing here has been implemented.

Key cross-driver facts the plan rests on (independently verified):

- xhci.c:1615 and :1720 collapse every non-timeout failure into
  USB_XFER_STALL (Babble, Transaction Error, TRB Error, Context State
  Error...).  usb_msc picks clear-halt vs reset recovery on that
  distinction, and usb_hid/usb_hid_mouse permanently latch
  ctl_poll_refused on STALL -- so a single transient transaction error on
  the HP Pavilion's xHCI can silently kill a HID device's input.  UHCI
  classifies but tests STALLED before the cause bits (uhci.c:459-468).
  EHCI got this right in [ehci-audit 2]; the other two did not.
- Only EHCI has a .shutdown hook.  UHCI keeps its frame list pointed at a
  live QH and xHCI leaves CRCR/DCBAAP/ERSTBA programmed across a warm
  reboot -- the exact DMA-into-reused-memory hazard [ehci-audit 7] fixed,
  still open on the controller that carries the Pavilion's root disk.
- The hc_failed dead-controller latch is EHCI-private; a dead xHC re-logs
  errors forever while every transfer burns its timeout, a dead UHCI
  costs 5 s per transfer forever, and usb_hotplug keeps resetting ports
  on both.
- The HOST_TEST harness pattern is proven in-tree (nvme.c +
  host_test_nvme.c; isa.c port-I/O hooks) and ehci_op_rd/wr is a single
  register funnel, so the audit semantics QEMU cannot produce (HSE,
  stuck PR, NakCnt, W1C discipline) are host-testable.

The full critic verdict follows verbatim.

---

FEASIBILITY REVIEW — USB HCD REFACTORING PANEL (32 proposals, verified against tree @ main 8315f5fe2)

VERIFICATION SUMMARY
Every proposal's file:line claims were checked against sys/drivers/usb/{ehci.c,ehci.h,uhci.c,uhci.h,xhci.c,xhci.h,usb.c,usb.h}, usb_hub.c, usb_msc.c, usb_hid.c, usb_hid_mouse.c, uas.c, sys/kern/{pci.h,pci.c,device.h,device.c,isa.c}, and tests/sys/. No proposal has a fatally false premise; none killed. Three corrections that change shape or grading:

- HCD-02 (incomplete premise): kern/pci.c ALREADY has `pci_iomap()` (kern/pci.h:77, kern/pci.c:559) doing bar-type check + pci_bar_size + request_region + ioremap with a max_len clamp. Neither ehci.c nor xhci.c uses it, and the proposal doesn't mention it. The right move is to extend pci_iomap (decode-off sizing per P6-INIT-01, 64-bit read + pci_relocate_bar32, PCI_COMMAND_MEMORY|MASTER enable), not add a parallel `pci_map_bar_mem`. Also note pci_bar_size() itself does the all-ones probe WITHOUT disabling decode — centralizing the decode-off discipline is the actual win.
- HCD-11 (impact overstated): the core control wrappers never set timeout_ms (usb.c:264 memset; verified nothing sets it for control), so no control transfer today carries a short deadline on ANY controller — the "5 s per idle poll on UHCI" scenario cannot occur yet. It is a one-line latent-parity fix, payoff LOW (was medium). Real, but it only bites when API-03 (or any timed control caller) lands.
- EHCI-R8 (wrong reason, right conclusion): ep0.mult is not "never set" — usb.c:213 explicitly sets `dev->ep0.mult = 1`. That makes the fold trivially behavior-identical, stronger than the proposal's argument.

Minor line drift only (premises hold): EHCI-R4's bulk `len - residue` is at ehci.c:580 (residue at 579); HCD-01's usb_hub loops are at usb_hub.c:146-151, 218-223, 253-259 (three sites, one with computed duration — still fits usb_delay_ms(ms)).

Key spot-verifications that the high-stakes claims rest on, all TRUE:
- xhci.c:1615 and :1720 collapse every non-success cc into `(cc==0)?TIMEOUT:STALL`; XHCI_CC_STALL=6 exists (xhci.h:276); xhci_recover_ep already runs unconditionally at 1596/1719, so reclassification does not change recovery.
- uhci.c:459-468 tests STALLED before the cause bits; uhci_poll_td (413-500) never reads USBSTS; uhci_port_reset returns 0 unconditionally (379); uhci_port_enable RMW at 392-399 is unmasked (W1C local const at 336); uhci_control_transfer hardcodes 5000 (648); no .shutdown in uhci_pci_driver (1157-1161) or xhci_pci_driver (2751-2753); no LEGSUP reference anywhere in uhci.c.
- usb_hcd_t (usb.h:502-629) has no kdev, no dead flag; struct device (device.h) has no driver-private pointer; device_shutdown_all at device.c:536; port_enable has ZERO callers (grep: only 3 impls + 3 assignments + decl).
- No HCD produces USB_XFER_SHORT or USB_XFER_NAK (grep-verified); nothing outside the HCDs reads xfer->status (wrappers are stack-local + memset, use return value).
- HOST_TEST precedent is real: nvme.c hooks (59/72/94), host_test_nvme.c TU-include at :317 with mock clock at :227 and phys-registry DMA at :258; isa.c:11-19 port-I/O macro hooks; host_test_ac97.c:9-14 admits to the mirror-copy anti-pattern; ehci_op_rd/wr (ehci.c:87-94) really are the single funnel for all operational-register access (cap-region derefs are attach-path only, which the harness skips).
- ehci intr vs async poll order genuinely differs (intr: scan, deadline:691, probe:694; async: probe:341, scan:355, deadline:365) — EHCI-R1's flagged micro-divergence is exactly right, and the affected case (HC dies same iteration deadline lapses) additionally skips intr's [EHCI-03] late recheck; must be named in the commit.
- EHCI-R5's equivalence argument holds: the parked overlay is HALTED and skipped at Fetch-QH, so bulk reading it after ehci_async_restart (584-602) equals capturing before restart; the timeout park preserves dt (405).

MERGES
- HCD-03 + API-01 = one proposal (API-01 is the superset: adds hotplug-scan skip + one-shot reporting).
- HCD-06 + API-02 + T3-item-1 + T4-item-2 = one classification-parity proposal; T3/T4 supply its regression tests.
- HCD-11 folds into API-03 (do the one-liner immediately, the policy change separately).
- EHCI-R2 + R3 are internals of EHCI-R1 (R3 standalone only if R1 stalls).
- EHCI-R6's stride helper and T2(b) must coordinate: extract as pure `ehci_intr_stride(uint8_t bi, uint8_t speed)` (T2's signature) so it is table-testable.
- HCD-04 + HCD-05 = one item (04 is 05's dispatch plumbing).
- EHCI-R4 + R8 + R9 = one mechanical-helpers commit series.

=======================================================================
TIER 1 — DO SOON (ranked)
=======================================================================

RANK 1. Uniform STALL-vs-transport-error classification (API-02 + HCD-06, tests from T3/T4)
  Effort: small | Risk: low (xHCI) / medium-on-hardware (UHCI reorder) | Payoff: high
  Callers verifiably change behavior on this distinction (usb_msc.c:244/278/301 runs clear-halt only on STALL; usb_hid.c:436-441 and usb_hid_mouse.c:321-325 permanently latch ctl_poll_refused on STALL), and 2 of 3 drivers misreport: xHCI maps Babble/TxErr/TRBErr/ContextState to STALL at xhci.c:1615 and 1720 (its own comment at 1597-1606 concedes it), UHCI tests STALLED before the cause bits at uhci.c:459-468. Extract `xhci_xfer_status(cc)` used at both sites and `uhci_td_status(cs)` with cause-bits-first ordering, mirroring ehci_halt_status (ehci.c:283-289); document the STALL/ERROR/TIMEOUT taxonomy beside USB_XFER_* (usb.h:101-106). Classifiers stay per-driver (three different encodings) — the proposal is right that only the policy is shared.
  First step: add `xhci_xfer_status()` and switch xhci.c:1615/1720; commit; then the UHCI reorder as its own commit flagged for hardware smoke on the HP Pavilion's UHCI companions.

RANK 2. Dead-controller latch in usb_hcd_t + core fail-fast (API-01, absorbing HCD-03)
  Effort: medium | Risk: low | Payoff: high
  Premises all verified: hc_failed is EHCI-private (ehci.c:51-54, set 341-353/370-375/694-703, checked 764-770); xhci_drain_events detects HSE/HCE/HCH but only prints — and is called on every submit/iso path (xhci.c:2014, 1847, 1958, 2002), so a dead xHC re-logs forever while every transfer burns its timeout; uhci_poll_td never reads USBSTS, so a halted UHCI costs 5 s per transfer forever; usb_hotplug_scan (usb.c:1664-1729) keeps resetting ports on a dead HCD. Purely a fail-fast/report-once layer over the unchanged polled design. The two-field split (HCD-written hc_failed, core-owned hc_failed_reported) is the right ownership.
  First step: add the two fields to usb_hcd_t next to enum_fail (usb.h:525), gate the five usb.c wrappers (261/300/326/838/391) and the hotplug scan, port EHCI's private field over; then the xHCI 0-to-1-transition set in xhci_drain_events; the UHCI throttled-probe addition lands with T3's test (Rank 4).

RANK 3. host_test_ehci harness + pure-calculator tables (T1 + T2)
  Effort: medium | Risk: low (zero kernel object-code change) | Payoff: high
  The pattern is proven in-tree (nvme.c hooks + host_test_nvme.c TU-include; isa.c port-I/O hooks), ehci_op_rd/wr really is the single funnel, and the enumerated targets ([EHCI-INIT-03] HSE mid-poll, [ASYNC-04] refused stop with no-scribble assertion, [EHCI-03] late-completion window, [PORT-02] stuck PR, [PORT-01] Line-Status gating, W1C write discipline, [ehci-audit 5] RL=0) are exactly the audit semantics QEMU cannot produce — today pinned by comments only. T2's table tests (ehci_halt_status, ehci_intr_stride extraction, endp_char/endp_cap, fill_qtd + the alt_next-to-STATUS assertion) ride in the same TU and correctly ban the ac97-style mirror-copy. Honest stated limitation: overlay store ORDERING stays a review property.
  First step: the #ifdef HOST_TEST hook in ehci_op_rd/ehci_op_wr + a skeleton host_test_ehci.c that builds ehci_hc_t by hand and passes one trivial ehci_halt_status table; wire into tests/sys/Makefile HOST_TESTS; grow scenarios from there. Do the R6/T2 stride extraction (pure `ehci_intr_stride(bi, speed)`) in this series so the table tests target real code.

RANK 4. UHCI parity fixes with tests-first (T3 + HCD-07 + HCD-08 + HCD-11 one-liner, feeding API-01/API-02's UHCI halves)
  Effort: medium | Risk: medium (live UHCI companions on real test hardware — which is exactly why tests come first) | Payoff: high
  All four gaps verified: unmasked RMW in uhci_port_enable (uhci.c:392-399) W1C-acks pending CSC/PEC (missed hot-plug, same class as ehci-audit 10) with the mask buried as a function-local const (336) — a directive-11 violation; uhci_port_reset returns 0 unconditionally (379) so the enum_fail ladder (usb.c:1699-1706) never learns; no dead-HC detection in the poll loop (444-455); control hardcodes 5000 (648) vs bulk honoring the field (760-761). uhci_readw/writew/writel (98-111) is a clean hook funnel with the isa.c precedent.
  First step: add the uhci HOST_TEST hook + host_test_uhci.c with FAILING tests for classification order, dead-HC, honest reset, and W1C-masked writes; then land each fix with its now-green test. HCD-11's one-liner and UHCI_XFER_TIMEOUT_MS naming ride along. Correction: state in the commit that the control-timeout fix is latent (nothing sets control timeout_ms yet).

RANK 5. kdev in usb_hcd_t + usb_hcd_by_kdev + UHCI/xHCI shutdown hooks (HCD-04 + HCD-05)
  Effort: medium | Risk: medium (reboot-path, real-hardware-verified only) | Payoff: high
  Verified: only EHCI registers .shutdown; UHCI points all 1024 frame-list entries at its async QH permanently (uhci.c:260-262) and xHCI leaves CRCR/DCBAAP/ERSTBA live (xhci.c:2393-2412) across a warm reboot — the exact hazard ehci.c:1108-1117 documents. struct device carries no private pointer, usb.c already owns the usb_hcd_list registry, and ehci_hcs[]/EHCI_MAX_HCS exists only to work around that — pure-move deletion. xHCI's halt already exists verbatim in xhci_teardown (2487-2493); factor to xhci_halt() and share.
  First step: HCD-04 alone (kdev field + lookup + delete ehci_hcs[]), boot-verify in QEMU; then the two shutdown hooks, verified on the HP Pavilion (QEMU resets device models itself, as the proposal honestly states).

RANK 6. Shared qTD poll loop (EHCI-R1, with R2 scan struct + R3 dead-probe helper as its internals)
  Effort: medium | Risk: medium | Payoff: medium-high (downgraded from high: ~50 duplicated lines and one divergence trap, not a structural win)
  Both loop clones, both scan clones, and both verbatim dead-probe copies verified; the helper-detects/caller-cleans split matches xhci_drain_events' division; the equivalence arguments check out including the one flagged divergence (unified check order changes which classification wins when the HC dies in the deadline-lapse iteration, and additionally skips intr's [EHCI-03] recheck in that corner — an error path either way, but it must be named in the commit). Do AFTER Rank 3 so the harness pins DONE/HALTED/TIMEOUT/HC_DEAD behavior before and after.
  First step: land R3 (ehci_check_hc_dead + EHCI_DEADCHECK_MASK, keeping the vmexit comment) standalone — small, immediately deletes a verbatim clone — then R2+R1 as one commit with the T1 suite green across it.

=======================================================================
TIER 2 — DO OPPORTUNISTICALLY
=======================================================================

RANK 7. EHCI intr decomposition (EHCI-R6, stride signature per T2). small/low/medium-high. All three seams verified as pure code motion; the unlink/drain protocol (708-725) genuinely deserves a name and named constants. Pairs naturally with Rank 6; the stride extraction should already have happened in Rank 3.

RANK 8. EHCI mechanical helpers batch (EHCI-R4 + R8 + R9). small/low/medium. All duplication sites verified. Keep R4's two flagged variations exactly as proposed (control's actual_length=len branch stays in the caller; the intr clamp is provably dead for a conformant HC per the Total-Bytes count-down argument — say so in the commit). R9's always-preserve-dt park is behavior-identical at both sites (intr's preserved bit is dead state, overwritten at 677) and removes a real future-edit trap. R8's mult fold: cite usb.c:213 (ep0.mult=1) as the reason it is identical, not "never set".

RANK 9. Naming pass (EHCI-R7 + HCD-10's shared USB_BIOS_HANDOFF_WAIT_MS). small/low/medium. All constants and placements verified; the header/tunable split correctly applies directive 11 (EHCI_QTD_BYTES_MASK/CERR_MAX beside the shifts at ehci.h:129-131). Zero-object-code-change is checkable by disassembly diff as claimed. Fold HCD-10's constant harmonization in here; the rest of HCD-10 is an accepted no-build judgment (see below).

RANK 10. Transfer-result struct for toggle readback (EHCI-R5). medium/medium/medium (payoff downgraded from high — the coupling it removes is real but narrow). The equivalence argument verified, including the restart-then-read subtlety. This touches the audited [EHCI-04] path in both variants; sequence it AFTER Ranks 3 and 6 so the host harness pins ending-toggle behavior across the change. Fixing the ehci_run_qh parameter order rides along.

RANK 11. Submit-contract documentation + core status stamping (API-05). small/low/medium. Every premise grep-verified (early exits without status at ehci.c:504/562/630, uhci.c:566-567/701/1025-1026; SHORT never produced; NAK synthesized only by usb_hid; the usb.h:706-710 doc is wrong about NAK). Core stamping `xfer.status = ret` at the five submit call sites retires the whole X-16 bug class. Do together with Rank 1's usb.h taxonomy comment — they are the same documentation session.

RANK 12. usb_delay_ms consolidation (HCD-01). small/low/medium. Nine copies verified (3 functions + 4 uhci open-coded + 3 usb_hub, one with computed duration — still fits). Pure move, correct home (usb.c core, prototype beside usb_register_hcd).

RANK 13. Timeout-default centralization (API-03, absorbing what's left of HCD-11). small/medium/medium. The 1000/5000/1000 asymmetry verified; direction-of-change argument (bulk lengthens to what UHCI proved out, control shortens to what two of three already do) is sound; failure-path latency shift is the honest risk and the 3-controller QEMU smoke covers it. Do after Rank 1 and Rank 4 so classification and UHCI honesty land first.

RANK 14. Drop port_enable from usb_hcd_t (API-04). small/low/low. Zero callers verified by grep. Preserve the Table 2-16 "software cannot enable a port" knowledge in the removal commit message.

RANK 15. Zero-length UHCI transfers + toggle-on-STALL ownership (API-06). small/low/low. Both divergences verified (uhci.c:701 rejects ZLP that ehci.c:570 / xhci.c:1682-1704 accept; uhci.c:775 flips toggle on STALL where EHCI defers to usb_clear_halt's reset at usb.c:534-535). Fold into the Rank 4 UHCI series if convenient — same files, same test harness.

RANK 16. xHCI ring/event unit tests (T4, minus the classifier — that is Rank 1's). medium/low/medium. The wait_event/ring_push arithmetic is genuinely pure-plus-one-hook and uncovered; the missed-cycle-flip failure mode is real. Its explicit pricing-out of a fake xHC device model is correct and should be recorded (see big-ticket).

RANK 17. device_del-mid-I/O torture variant (T5c). small/low/low. Correct that qtest cannot exercise the guest driver; the QMP hot-plug approach matches established project practice (usb-host hot-plug rule). Cheap --snapshot harness variant across all three HC models; exercises Rank 1's classification for free.

RANK 18. PCI mem-BAR bring-up helper (HCD-02, RESHAPED). medium/medium/medium. Premise incomplete as noted: extend the existing pci_iomap()/pci_bar_size() in kern/pci.c with decode-off sizing, 64-bit read + relocation, and PCI_COMMAND_* enable, then move both attaches onto it (xHCI pure move; EHCI passes min=max=0x1000 to keep its mapping identical). The named-macro fix for the 0x0002|0x0004 literals (ehci.c:1058, xhci.c:2581) is worth doing even if the rest waits.

=======================================================================
TIER 3 — BIG-TICKET, NEEDS ITS OWN DECISION
=======================================================================

- UHCI BIOS legacy handoff (HCD-09). small code, but unverifiable under QEMU and its failure mode is a wedged boot on BIOS machines — the same class of firmware interaction that already required an opt-in for XUSB2PR on the C460. Verified UHCI has no LEGSUP handling and the 0x8F00-to-0xC0 write is the standard practice. Schedule it into a dedicated HP Pavilion hardware session together with Rank 5's shutdown-hook verification, gated by the existing nousbhandoff flag, with a boot-tested fallback plan.
- qtest-socket backing for the HOST_TEST hooks (T5a). Real benefit (catches wrong-offset/sequence bugs an in-process fake mirrors back), real cost (guest-phys DMA plumbing + clock_step driving). Only after the in-process fake (Rank 3) has demonstrably paid out.
- Fake xHC device model for end-to-end xhci_control/xhci_bulk host tests (priced out in T4). Correctly rejected as larger than the driver's testable core; record the decision so it is not re-litigated.
- Multi-qTD bulk chains / resident periodic QHs (EHCI-R10's revisit clause). The only trigger for revisiting the no-chain-builder judgment; would also be the moment the polled-design constraint itself gets re-priced.

=======================================================================
TIER 4 — REJECTED / ACCEPTED AS NO-BUILD JUDGMENTS
=======================================================================

- EHCI-R10 (qTD chain builder): ACCEPT THE JUDGMENT — do not build. Verified: 8-slot pool, exactly two chain shapes, the alt_next decision is per-shape and load-bearing where it sits, and the uhci contrast (real pool machinery at uhci.c:119-173) is apt. Record in a comment or the commit landing Rank 8 so the abstraction is not re-proposed.
- HCD-10 (shared BIOS-semaphore wait): ACCEPT THE ANTI-UNIFICATION — verified that transport (PCI-config vs MMIO), capability walk (EECP config chain w/ 0x40 floor + guard 32 vs xECP dword-scaled MMIO chain bounded by mmio_size + guard 64), and SMI disarm semantics (CTLSTS=0 vs RsvdP-preserving) all differ. Only the shared timeout constant survives (folded into Rank 9).
- No proposal rejected for a false premise. The three corrections (HCD-02 reshape, HCD-11 latency-claim downgrade, EHCI-R8 rationale fix) are recorded in the rankings above and must reach the eventual commit messages so the record stays honest.

CROSS-CUTTING SEQUENCING NOTE
Nothing in the accepted set fights the synchronous one-transfer-under-submit_lock design: API-01/API-02 are classification/fail-fast layers over it, the T-series mocks it, and the only proposals that would have touched it (fake xHC, qtest backing, resident periodic QHs) are correctly parked as big-ticket. The single most important ordering constraint: Rank 3's harness before Ranks 6 and 10 (the audited-path refactors), and Rank 4's failing tests before API-02's UHCI reorder reaches real hardware.
