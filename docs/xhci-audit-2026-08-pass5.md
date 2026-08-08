# xHCI re-audit (pass 5) — 2026-08-08

Fifth pass over `sys/drivers/usb/xhci.c` / `xhci.h` (2276 + 406 lines at
`9cab6b7fe`), against FreeBSD, NetBSD and xHCI 1.2.

The prime targets are the surfaces that did not exist when passes 1–4 ran: the
isochronous streaming added in `01287f3a7` and the hub slot-context work in
`ea5837183` — both written *during* this audit cycle, and granted no
presumption of correctness for it.  Because the iso path is driven by `uac`,
this pass deliberately crosses the HCD boundary and audits the
**contract** between `sys/drivers/usb/uac.c`, `usb.h`'s `usb_hcd_t` hooks, and
the two HCDs that implement them (`uhci.c`, `xhci.c`) — which is where all
three real findings turn out to live.

* **xHCI Revision 1.2** (Intel, May 2019) — §4.11.2.5 (Frame ID), §4.10.3.2
  (Ring Underrun/Overrun), §4.5.2 / §4.6.6 / Table 6-16 (hub-field latching and
  Add-flag semantics), §4.14.2.1.4 (Isochronous Scheduling Threshold).
* **FreeBSD 14.4-RELEASE** — `sys/dev/usb/controller/xhci.c`
* **NetBSD-current** — `sys/dev/usb/xhci.c`

## Status

**P5-01, P5-02 and P5-03 are fixed** as of 2026-08-08, same day:

| ID | Commit |
|----|--------|
| P5-01 | `830e17455` — usb: make the iso frame modulus a property of the HCD |
| P5-02 | `845b86231` — usb: latch hub fields before the downstream walk; stop idle iso endpoints |
| P5-03 | `845b86231` — as above |
| P5-04 | contract documented at both sites (`UAC_WINDOW`, `xhci_iso_schedule()`); no runtime check is possible without a dequeue, see the entry |
| P5-05 | **open** — accepted for now; the fix is to read HCSPARAMS2's IST and advertise a minimum lead the way `iso_frame_modulus` is advertised, and it should ride along with whatever next touches the iso contract |
| P5-06 | `usb.c`: Route String nibbles were tier-reversed — fixed, see below |

## P5-06 — High — the Route String was built tier-reversed (found post-close)

**`sys/drivers/usb/usb.c`, `usb_route_string()`**

Added after the pass closed, when the question "does this prove all devices
enumerate?" prompted testing the first topology outside the covered matrix — a
hub behind a hub — and it failed on the spot: the device two tiers deep died in
Address Device with a TRB Error, on every retry, in QEMU.

`ports[]` is collected nearest-the-device first, and the emit loop folded
`ports[0]` — the *deepest* tier — into bits 3:0.  The route string is defined
the other way: tier 1 (bits 3:0) is the topmost hop, the downstream port on
the hub attached to the root port (FreeBSD: `route |= port << (4*(depth-1))`).
For a keyboard on hub2 port 2, hub2 on hub1 port 4, the correct route is
`0x24`; the code produced `0x42`, sending the controller through hub1's empty
port 2.  A single hub yields one nibble, which reverses to itself — so every
one-tier test in passes 3–5 passed while every multi-tier topology was broken,
and pass 3's "Route String checked and found correct" entry was simply wrong:
it verified the clamp and the mask and misread the emit direction, and nothing
had ever executed the multi-tier path.

Fixed and verified at two and three tiers under `qemu-xhci` (keyboard at
`1.4.2` and at `1.4.3.5` enumerates, binds, boot reaches init).  The lesson
recorded for the next pass: an audit "found correct" claim about code with an
untested dominant path is worth exactly one boot with that path exercised.

P5-01's verification turned out stronger than expected: the wrap-arithmetic
half is pure software and reproduces under QEMU, and re-running the pass-4
tone test against the fix moved the capture from 0.475 s (stalled at the first
MFINDEX wrap — the pass-4 audit's "FIFO overrun" explanation for that number
was wrong) to 5.236 s spanning at least two wraps with zero silence and the
sample count conserved exactly.  P5-03's stop and doorbell-restart were both
traced live in a play/idle/replay run.  P5-02 remains verifiable only on real
hardware, like P3-01/P3-02 before it.

| ID | Severity | Area | One-line |
|----|----------|------|----------|
| P5-01 | **High** | uac ↔ xHCI | Frame modulus mismatch: uac schedules mod 1024, xHCI's Frame ID is defined mod 2048 — audio stalls ~1 s of every 2.048 s on spec-faithful hardware |
| P5-02 | Medium | hub fields | `xhci_set_hub()`'s A0-only Configure Endpoint is not the latch point the spec defines; the hub's slot is still Addressed (Hub bit unlatched) throughout downstream enumeration |
| P5-03 | Medium | iso idle | Nothing ever stops the iso endpoint: an idle stream leaves it Running with an empty ring, generating a Ring Underrun event per 1 ms interval into a 64-entry event ring |
| P5-04 | Low | iso ring | The 48-packet window fitting the 63-usable-TRB ring is an implicit cross-module contract with no check on either side |
| P5-05 | Low | iso lead | `UAC_LEAD` (4 frames) is never validated against the controller's IST; both BSDs read it from HCSPARAMS2 |

---

## P5-01 — High — uac and xHCI disagree on the frame modulus

**`sys/drivers/usb/uac.c:74,163-166,215`, `sys/drivers/usb/xhci.c:1521,1563`,
`sys/drivers/usb/uhci.c:938-941`**

Three moduli are in play and only two of them agree:

```c
/* uac.c:74  — sized for the controller uac was written against */
#define UAC_NFRAMES               1024U   /* UHCI frame-list size */

/* uac.c:163 — wrap detection adds 1024 per wrap */
if (fr < d->last_fr) {
    d->frame_hi += UAC_NFRAMES;     /* frame counter wrapped */
}

/* uac.c:215 — the target frame is masked mod 1024 before the HCD sees it */
usb_iso_schedule(d->udev, &d->iso_ep,
    (uint16_t)(d->sched & (UAC_NFRAMES - 1)), ...);
```

UHCI's frame space genuinely is 1024: `uhci_hcd_frame_number()` returns
`FRNUM & (UHCI_FRAME_LIST_SIZE - 1)` and its `iso_schedule` indexes a
1024-entry frame list.  xHCI's is not.  `xhci_frame_number()` returns
MFINDEX bits 13:3 — an 11-bit value — and §4.11.2.5 is explicit:

> "The Frame ID value is calculated as the modulus of **2048**, i.e. the size
> of the Frame Index portion of the MFINDEX register."

Two distinct breakages follow:

1. **The masked Frame ID.**  Whenever the true frame index is in
   [1024, 2047], uac's mod-1024 target differs from it by 1024.  On a
   controller with the Contiguous Frame ID Capability — **mandatory** for
   xHCI 1.1/1.2 — a non-matching Frame ID is by definition a gap:

   > "To induce a gap in the data stream of a Running Isoch endpoint, software
   > simply specifies a gap in the Frame IDs assigned to the TDs, and **the xHC
   > will pause the data stream until the Frame ID matches** the Frame Index of
   > the MFINDEX register."

   So the stream plays for the ~1.024 s the two moduli agree, then stalls for
   ~1.024 s until MFINDEX wraps back into agreement.  Audio through xHCI
   alternates second-on / second-off, forever.

2. **The wrap arithmetic.**  `frame_hi += 1024` on a counter that wraps at
   2048 makes `dev_frame` jump *backwards* by 1023 at every wrap.  The feeder's
   window test `(int32_t)(d->sched - (dev_frame + UAC_WINDOW)) < 0` then reads
   as "already 1023 ahead", so it schedules nothing for the next ~975 frames —
   an independent ~1 s dropout with the same period, phase-shifted from the
   first.

Both BSDs handle the frame space at its full width: NetBSD masks with
`XHCI_MFINDEX_GET(~0)` (`xhci.c:4645-4655`), FreeBSD keeps `xfer->isoc_next`
in the same modulus.

**Why the pass-4 verification did not catch it.**  The `isoplay` capture ran
for under half a second of stream time, and QEMU's xHCI does not enforce the
Frame ID window.  The tone analysis proved data integrity through the pipe, not
frame placement.

**Fix.**  The modulus is a property of the controller, so advertise it there:
a `iso_frame_modulus` field on `usb_hcd_t` (1024 from UHCI, 2048 from xHCI),
used by uac for the wrap increment.  The mask on the schedule call is simply
dropped — `d->sched` truncated to `uint16_t` is exact mod both 1024 and 2048
(65536 is a multiple of each), and both HCDs already mask to their own modulus
internally (`uhci.c` explicitly, `xhci.h`'s `XHCI_TRB_FRAME_ID` by
construction).

---

## P5-02 — Medium — the A0-only Configure Endpoint is not the spec's latch point

**`sys/drivers/usb/xhci.c`, `xhci_set_hub()`; `sys/drivers/usb/usb_hub.c`,
`usb_hub_attach()` / `usb_hub_bringup_port()`**

P3-02 replaced `xhci_set_hub()`'s Evaluate Context with a Configure Endpoint
carrying only the slot add flag (A0), no endpoint adds or drops.  Table 6-16
supports reading that as "the slot context shall be evaluated" — but the note
that defines *when the hub fields latch* (§4.5.2) is narrower:

> "the values of the Output Slot Context hub related fields (Hub, TTT, MTT, and
> Number of Ports) shall be initialized by the xHC by the first Configure
> Endpoint Command **to transition the Slot from the Addressed to the
> Configured state**."

A Configure Endpoint that adds nothing performs no such transition — §4.6.6's
processing rules leave a slot with no enabled endpoints in the Addressed state.
Neither BSD ever issues a degenerate A0-only Configure Endpoint, so there is no
reference behaviour to lean on; a strict controller is entitled to evaluate the
input slot context for *validity* and still not latch the hub fields.

The gap matters because of ordering.  `usb_hub_attach()` calls `usb_set_hub()`
and then walks straight into `usb_hub_enumerate_ports()`, and the hub's
downstream ports are managed entirely over EP0 control transfers
(`usb_hub_bringup_port()` — GET_PORT_STATUS, SET_FEATURE(PORT_RESET), ...).
The hub's status-change interrupt endpoint is not touched until the first
hot-plug poll, so under the lazy `xhci_ensure_ep()` model the *real* Configure
Endpoint — the one §4.5.2 defines as the latch point — does not happen until
after every downstream device has already enumerated.  On a controller that
ignores the A0-only command's hub bits, all of P3-02's failure modes return
untouched.

**Fix.**  Make the spec-defined latch happen before the downstream walk: after
`usb_set_hub()`, issue one throwaway interrupt-IN poll on the hub's
status-change endpoint.  That drives `xhci_ensure_ep()` → a genuine Configure
Endpoint (adding the interrupt endpoint, carrying the hub fields, performing
Addressed→Configured).  On UHCI/EHCI it is one harmlessly NAK'd read.  The
A0-only command stays as a best-effort fallback for a hub with no interrupt
endpoint.

---

## P5-03 — Medium — an idle iso stream leaves the endpoint Running on an empty ring

**`sys/drivers/usb/xhci.c` (iso hooks), `sys/drivers/usb/uac.c` (idle path)**

§4.10.3.2:

> "For Isoch Out transfers, the xHC shall generate a Ring Underrun Transfer
> Event **if the Transfer Ring is empty when an active interval boundary is
> reached**."

An interval boundary arrives every 1 ms whether or not software has anything to
send.  When playback ends, uac's feeder stops scheduling (`active = 0` →
`uac_reclaim_all()`), but nothing tells the HCD the stream is over: the
endpoint stays Running with an empty ring, and the controller posts a Ring
Underrun event every millisecond, forever.  The 64-entry event ring fills in
64 ms.

On a machine with any other USB traffic this is masked — every `submit()`
drains the event ring first ([X-08]/[R-05]), and a HID poll does so several
times a second.  On a quiet bus (audio playback on a box with no USB input
devices), the ring wedges full: the controller posts an Event Ring Full Error
and drops events, and the next transfer's completion is the thing most likely
to be lost — surfacing as a spurious timeout and a recovery cycle whose cause
is nowhere near the endpoint that provoked it.

The choice not to set IOC on Isoch TRBs ([X-13] commit) anticipated exactly
this flood from *successful* TDs; the underrun case is its complement and needs
the stream actually stopped.

**Fix.**  An optional `iso_stop(hcd, dev, ep)` hook on `usb_hcd_t`, called by
uac when the stream idles (and NULL for UHCI, whose reclaim already empties its
frame-list slots).  The xHCI implementation is a state-checked quiesce under
`submit_lock` — precisely `xhci_recover_ep()`, whose Running branch issues Stop
Endpoint + Set TR Dequeue.  A Stopped endpoint restarts on the next doorbell
(§4.8.3), which is exactly what `xhci_iso_schedule()` rings, so resume needs no
extra code.

---

## P5-04 — Low — the iso window/ring-capacity contract is implicit

`UAC_WINDOW` (48) outstanding single-TRB TDs against a ring with 63 usable
entries works, but nothing states or checks it on either side; the iso path has
no ring-full detection (the driver never learns the controller's dequeue
without an event, and Isoch TRBs carry no IOC).  A larger window, a second iso
consumer on the same endpoint, or a smaller `XHCI_RING_TRBS` would silently
overwrite TRBs the controller has not consumed.  Recorded as a constraint to
assert rather than a bug to fix: `xhci_iso_schedule()` cannot cheaply know the
dequeue, but the contract ("outstanding iso TDs per endpoint must stay below
`XHCI_RING_TRBS - 2`") belongs in both files' comments, and uac's window is the
number to check against it.

## P5-05 — Low — `UAC_LEAD` is never validated against IST

§4.11.2.5: the Start Frame ID is `MFINDEX + IST + 1`; a TD scheduled closer
than the Isochronous Scheduling Threshold to the current frame may be skipped.
Both BSDs read IST from HCSPARAMS2 and honour it (FreeBSD `sc_ist`,
`xhci.c:595` and the range check at 2051-2059; NetBSD `sc_isthresh`,
`xhci.c:4653-4655`).  Substrate neither reads HCSPARAMS2's IST field nor
exposes it; uac's fixed 4-frame lead happens to exceed the common 1-2
microframe IST but nothing enforces that against a controller declaring IST in
frames (HCSPARAMS2 bit 3), where values up to 8 frames are legal.

---

## Checked in this pass and found correct

* **TBC/TLBPC = 0** on the Isoch TRB is correct for the only stream shape uac
  produces: one packet per interval at full speed (no bursts, so
  "number of bursts - 1" = 0 and "packets - 1 in the last burst" = 0).
  High-bandwidth high-speed iso (Mult/burst) would need both computed, but no
  consumer exists.
* **CErr = 0 on iso endpoint contexts** (Table 6-9), interval mapping for FS
  iso (bInterval-1+3), and Max ESIT Payload = MPS — re-verified as landed in
  `01287f3a7`.
* **No SIA** remains the right call given uac computes placement; with P5-01
  fixed the Frame ID it computes is finally the one the controller compares.
* **The iso path takes `submit_lock` and drains events first** — the [R-05]
  discipline covers the new entry point.
* **`xhci_ensure_ep()` from the iso path** reuses the ordinary lazy-configure
  road, including hub fields and Context Entries max — no divergence.
* **Event-loss during recovery** — investigated to a negative result in pass 4;
  not re-opened.

---

## Verification note

P5-01's stall cannot be reproduced under QEMU (its xHCI does not enforce the
Frame ID window); the fix is verified by inspection against §4.11.2.5 plus a
regression boot proving audio still flows.  P5-02 has the same
real-hardware-only caveat as P3-01/P3-02.  P5-03's flood is observable in
principle under QEMU with a quiet bus; the fix is verified by the absence of
underrun spam in a `--usb-audio` boot's drain path after playback ends.
