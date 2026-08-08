# xHCI re-audit (pass 3) — 2026-08-07

Third audit pass over `sys/drivers/usb/xhci.c` / `xhci.h`, at commit
`419dd982d` (1948 lines).

Where the first two passes read the driver largely on its own terms, this one
is a **differential** read: every slot/endpoint context field, command choice
and register sequence was compared against the two reference drivers and then
against the normative text, in that order.

* **FreeBSD 14.4-RELEASE** — `sys/dev/usb/controller/xhci.c`, `xhci.h`,
  `xhcireg.h`, `xhci_pci.c`
* **NetBSD-current** — `sys/dev/usb/xhci.c`, `xhcireg.h`, `xhcivar.h`,
  `sys/dev/pci/xhci_pci.c`
* **xHCI Revision 1.2** (Intel, May 2019) — cited by section and table number.
  Also consulted: **EHCI Revision 1.0** (Intel, March 2002) for the companion
  controller's view of the shared USB2 ports.

No other operating system's source was consulted.

---

## Status of the earlier passes — read this first

**Pass 1 (`docs/xhci-audit-2026-08.md`, X-01..X-18): fixed.**  Ten commits,
`e1ee410cd`..`555d7650a`.  Each was re-verified present in the tree during this
pass.

**Pass 2 (`docs/xhci-audit-2026-08-pass2.md`, R-01..R-06): NOT fixed — all six
are still open.**  The last commit to touch `sys/drivers/usb/xhci.c` is
`555d7650a`, which *precedes* `f94df6c90` (the commit that added the pass-2
document).  That document ends in a "Suggested order" section, i.e. it is a
to-do list that was never worked.  Re-confirmed against the current source:

| ID | Severity | Still open? | Evidence in the tree today |
|----|----------|-------------|----------------------------|
| R-01 | **High** | yes | `xhci_abort_command()` (xhci.c:250-276) sets CA, polls CRR, rewinds the ring and republishes CRCR — it never reads the event ring, so the Command Ring Stopped / aborted-command completions it provokes are left for the next command to mistake for its own result |
| R-02 | Low | yes | `xhci_submit()` isochronous branch (xhci.c:1297) sets `ret` but never `xfer->status` |
| R-03 | Low | yes | `hc->nports` (xhci.c:1827-1831) is clamped to nothing but `!= 0`; `port_major[USB_MAX_ROOT_PORTS]` is indexed against it |
| R-04 | Low | yes | `link->control` written directly at xhci.c:272, bypassing `xhci_trb_commit()`, still with no comment saying why that is safe here |
| R-05 | Low | yes | only `xhci_submit()` calls `xhci_drain_events()` (xhci.c:1275); `xhci_port_gone()`, `xhci_set_hub()` and `xhci_set_ep0_mps()` take `submit_lock` and drive the controller without it |
| R-06 | Low | yes | `xhci_slot_for()` (xhci.c:809) returns `hc->addr_slot[addr]` without the `hc->slots[...] != NULL` guard the `enum_slot` branch has |

R-01 remains the highest-value fix in the driver and is not superseded by
anything below.

---

## Summary — new in this pass

| ID | Severity | Area | One-line |
|----|----------|------|----------|
| P3-01 | **High** | slot context | Slot Context Speed comes from the *root port's* PORTSC, not the device's own speed — wrong for every LS/FS device behind a hub |
| P3-02 | **High** | slot context | `xhci_set_hub()` uses Evaluate Context, which by spec cannot set Hub or Number of Ports; the bit is never set at all |
| P3-03 | Medium | slot context | TT Think Time never initialized on a high-speed hub's slot, which §6.2.2.2 requires whenever Hub=1 and Speed=HS |
| P3-04 | Low | header | `XHCI_TRB_ENT` is defined as `0x10`, which is the Chain bit; ENT is bit 1 |
| P3-05 | Low | slot context | Input Context Entries is set to the DCI being added rather than the highest configured DCI, so it can shrink |

---

## P3-01 — High — Slot Context Speed is read from the root port, not the device

**`sys/drivers/usb/xhci.c:752-759`**

```c
uint32_t *sc = (uint32_t *)in_slot_of(hc, s->in_ctx);
uint32_t speed = (portsc_rd(hc, port) & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
...
sc[0] = (1u << XHCI_SLOT_CTX_ENTRIES_SHIFT) | (speed << XHCI_SLOT_SPEED_SHIFT) |
        (route & XHCI_SLOT_ROUTE_MASK);
```

`port` is `usb_root_port(xfer->dev)` (xhci.c:1058) — deliberately the root
port, which is correct for the Root Hub Port Number field in `sc[1]`.  But the
Speed field is not a property of the root port; it is a property of the
*device*.  For anything behind a hub the two differ, and what gets written is
the speed the root port trained at, i.e. the speed of the hub.

A full-speed device behind a high-speed hub is therefore described to the
controller as high-speed — while `sc[2]`, immediately below, correctly fills in
the TT Hub Slot ID and TT Port Number, which are only meaningful for a
low/full-speed device.  The slot context contradicts itself.

**What the references do.**  Both take the speed from the device.

FreeBSD (`xhci.c:2567-2596`) switches on `udev->speed`:

```c
switch (udev->speed) {
case USB_SPEED_LOW:
        temp |= XHCI_SCTX_0_SPEED_SET(2);
        ...
case USB_SPEED_FULL:
        temp |= XHCI_SCTX_0_SPEED_SET(1);
```

NetBSD (`xhci.c:3620-3634`) does the same through a helper:

```c
uint8_t speed = dev->ud_speed;
...
cp[0] = XHCI_SCTX_0_CTX_NUM_SET(dci) |
        XHCI_SCTX_0_SPEED_SET(xhci_speed2xspeed(speed));
```

**Spec.**  §4.5.2 lists Speed under the values derived from the device, and
§6.2.2 Table 6-4 bits 23:20 define it.  Note that xHCI **1.2 deprecates the
field** ("This field is deprecated in this version of the specification and
shall be Reserved") — but 1.0 and 1.1 do not, and both BSDs still populate it
for exactly that reason.

**Failure scenario.**  A USB 2.0 hub on a root port, with a full-speed or
low-speed device below it — a keyboard, a mouse, a serial adapter.  On an
xHCI 1.0/1.1 controller (Panther Point, Lynx Point — including the Lenovo C460
this driver already has hardware-specific workarounds for) the xHC schedules
the device as high-speed: no low/full-speed transactions are generated through
the hub's TT, and every transfer to that device fails or times out.  The device
enumerates (EP0 traffic to the *hub* is fine) and then does nothing.  Invisible
under QEMU, whose xHCI model does not use the field.

**Fix.**  Map `xfer->dev->speed` through to the xHCI encoding rather than
passing the PORTSC PSID through.  Substrate's `USB_SPEED_*` values
(`usb.h:31-34`) are LOW=0, FULL=1, HIGH=2, SUPER=3 and do **not** match the
xHCI encoding (1=FS, 2=LS, 3=HS, 4=SS), so this needs a small table, not a
cast.  `dev->speed` is set at `usb.c:1030`, before any control transfer
reaches `xhci_setup_slot()`, so it is available.

---

## P3-02 — High — `xhci_set_hub()` uses a command that cannot set the Hub bit

**`sys/drivers/usb/xhci.c:960-998`**

```c
/* Evaluate Context looks at the Add flags: slot context only (A0). */
icc[0] = 0;
icc[1] = 0x1;

sc = (uint32_t *)in_slot_of(hc, hc->slots[slot]->in_ctx);
sc[0] |= XHCI_SLOT_HUB;
sc[1] = (sc[1] & 0x00FFFFFFu) | ((uint32_t)nports << XHCI_SLOT_NPORTS_SHIFT);

cc = xhci_run_command(hc, hc->slots[slot]->in_ctx_dma,
                      XHCI_TRB_TYPE(TRB_EVAL_CONTEXT) | ((uint32_t)slot << 24), NULL);
```

The command returns Success and nothing happens.  §6.2.2.3 (Evaluate Context
Command Usage) is explicit about the scope of the command:

> A 'valid' Input Slot Context for an Evaluate Context Command requires the
> Interrupter Target and Max Exit Latency fields to be initialized.  **Only
> these fields shall be evaluated** when the xHC receives an Evaluate Context
> Command that flags the Slot Context (i.e. Add Context 0 flag set to '1').
> [...] **Only the Output Interrupter Target and Max Exit Latency fields are
> updated by the Evaluate Context Command.**

Hub and Number of Ports belong to the *Configure Endpoint* command.  §6.2.2.2
(Configure Endpoint Command Usage):

> A 'valid' Input Slot Context for a Configure Endpoint Command requires the
> Context Entries field to be initialized [...].  **The Hub field shall also be
> initialized.**  If Hub = '1' and Speed = High-Speed, then the TT Think Time
> (TTT) and Multi-TT (MTT) fields shall be initialized. [...] **If Hub = '1',
> then the Number of Ports field shall be initialized**, else Number of
> Ports = '0'.

And §4.5.2 makes the window a narrow one:

> After entering the Addressed state for the first time from the Enabled or
> Default states, the values of the Output Slot Context hub related fields
> (Hub, TTT, MTT, and Number of Ports) shall be initialized by the xHC by the
> **first Configure Endpoint Command** to transition the Slot from the
> Addressed to the Configured state.  To change the Output Slot Context hub
> related fields, a Slot must first be transitioned through the Enabled or
> Default state.

So there is exactly one opportunity to set these, and this driver spends it
elsewhere.  The second half of the bug closes the loop: the only Configure
Endpoint this driver ever issues is `xhci_ensure_ep()`, which rebuilds the
input slot context from the *output* device context —

```c
memset(s->in_ctx, 0, 33 * hc->ctx_size);
...
insc[0] = (dsc[0] & ~(0x1Fu << XHCI_SLOT_CTX_ENTRIES_SHIFT)) |
          ((uint32_t)dci << XHCI_SLOT_CTX_ENTRIES_SHIFT);
```

— where the Hub bit is still clear, because the Evaluate Context that was
supposed to set it did nothing.  The `sc[0] |= XHCI_SLOT_HUB` written into the
input context by `xhci_set_hub()` is discarded by that `memset` as well.  The
Hub bit is never set on any slot, by any path.

The comment above the function states the requirement correctly ("xHCI will not
route a transfer past a slot that does not declare itself one") — only the
command is wrong.

**Ordering is favourable.**  `usb_set_hub()` is called from `usb_hub_attach()`
(`usb_hub.c:468`) *before* `usb_hub_enumerate_ports()`, and the hub's
status-change interrupt endpoint is not used until after that — so at
`set_hub()` time the slot is still in the Addressed state and its first
Configure Endpoint has not yet been issued.  The correct sequence is still
available.

**Failure scenario.**  Any external hub on real silicon.  Devices below it
enumerate through EP0 (which is addressed with a correct Route String) and then
misbehave in controller-specific ways: on parts that use the Hub bit for
downstream bandwidth and TT scheduling, low/full-speed devices below the hub
get no service.  Compounds with P3-01, which describes the same devices at the
wrong speed.  QEMU's xHCI does not model hub-aware scheduling, so this passes
in emulation.

**Fix.**  Record the hub state on the slot (`is_hub`, `nports`, and the TTT
from the hub descriptor) rather than pushing it at the controller immediately,
and have `xhci_ensure_ep()` OR those fields into `insc[0]`/`insc[1]` on every
Configure Endpoint it builds — §6.2.2.2 requires the Hub field to be
initialized on *every* such command, not just the first.  For a hub whose
interrupt endpoint is never opened, issue a Configure Endpoint with only A0
set from `xhci_set_hub()` itself to make the Addressed→Configured transition.
Keep `xhci_set_ep0_mps()` on Evaluate Context — that one is correct, and is
precisely what §4.3 step 7.x prescribes.

---

## P3-03 — Medium — TT Think Time never initialized on a high-speed hub

**`sys/drivers/usb/xhci.c:960-998`, `sys/drivers/usb/usb_hub.c:443-452`**

§6.2.2.2, quoted above: "If Hub = '1' and Speed = High-Speed, then the TT Think
Time (TTT) and Multi-TT (MTT) fields shall be initialized."  TTT is slot
context dword 2 bits 17:16, and comes from the TT Think Time sub-field of the
hub descriptor's `wHubCharacteristics` (USB 2.0 Table 11-13).

`xhci_set_hub()` takes only `nports`; the hub descriptor is parsed in
`usb_hub_attach()`, which reads `bNbrPorts` and `bPwrOn2PwrGood` out of it and
discards `wHubCharacteristics`.  `xhci.h` has no TTT macro at all.  TTT is
therefore left at 0 — "at most 8 FS bit times" — on every hub.

A hub that requires 16, 24 or 32 bit times of think time gets its TT
transactions scheduled too tightly.  The result is intermittent rather than
total: transaction errors on low/full-speed devices under load, on some hubs
and not others.

Not independently fixable — it lands on the same Configure Endpoint that P3-02
has to introduce — but it is a separate spec requirement and needs the hub
descriptor field plumbed through `usb_set_hub()`, so it is recorded separately.

**MTT is correctly left at 0** and is *not* part of this finding: the spec
conditions MTT on the Multi-TT interface "hav[ing] been enabled with a Set
Interface request", and substrate never issues one for a hub (`SET_INTERFACE`
appears only in `uac.c`).  FreeBSD sets MTT from the parent hub's
`bDeviceProtocol`, but FreeBSD also selects the multi-TT interface first.

---

## P3-04 — Low — `XHCI_TRB_ENT` holds the Chain bit's value

**`sys/drivers/usb/xhci.h:185-187`**

```c
#define XHCI_TRB_ENT         0x00000010   /* evaluate next TRB / chain-ish */
#define XHCI_TRB_ISP         0x00000004   /* interrupt on short packet */
#define XHCI_TRB_CH          0x00000010   /* chain bit */
```

Per §4.11.1.1 and Table 6-22 the transfer TRB control word is: bit 0 Cycle,
**bit 1 Evaluate Next TRB (ENT)**, bit 2 ISP, bit 3 No Snoop, **bit 4 Chain
(CH)**, bit 5 IOC, bit 6 IDT.  `XHCI_TRB_ENT` should be `0x02`; as written it
is a duplicate of `XHCI_TRB_CH`.

Latent only — neither macro is referenced anywhere in `xhci.c`, so nothing is
miscompiled today.  It becomes a live bug the moment multi-TRB TDs are added
(a data stage larger than one TRB, or isochronous), which is exactly when
someone reaches for these two macros.  The comment "chain-ish" suggests the
value was taken from the wrong row of the table.

Note `XHCI_TRB_TC` (`0x02`) is correct for its own use: bit 1 of a **Link** TRB
is Toggle Cycle, which is a different field at the same offset in a different
TRB type.

**Fix.**  `#define XHCI_TRB_ENT 0x00000002`, and note the Link-TRB aliasing
next to `XHCI_TRB_TC`.

---

## P3-05 — Low — Input Context Entries can shrink below a configured endpoint

**`sys/drivers/usb/xhci.c:891-894`**

```c
insc[0] = (dsc[0] & ~(0x1Fu << XHCI_SLOT_CTX_ENTRIES_SHIFT)) |
          ((uint32_t)dci << XHCI_SLOT_CTX_ENTRIES_SHIFT);
```

Context Entries is set to the DCI of the endpoint being added.  §6.2.2.2
requires "the index of the last valid Endpoint Context **that is defined by the
target configuration**", and Table 6-4 bits 31:27 define it as "the index of
the last valid Endpoint Context within this Device Context structure".

Endpoints are configured lazily, in whatever order the class driver first
touches them.  A device that is driven high-DCI-first — e.g. a CDC-style
function whose interrupt IN (EP3 IN, DCI 7) is polled before its bulk OUT
(EP1 OUT, DCI 2) — writes Context Entries = 2 on the second Configure Endpoint,
below the DCI 7 context that is still live.

No Parameter Error results: the Add flag is `1 << dci` and Context Entries is
that same `dci`, so the "DCI of an Add Context flag greater than Context
Entries" rule (§4.6.6) is never tripped, and §4.6.6 has the xHC recompute the
*output* Context Entries itself from what is actually valid.  Which is why this
is Low rather than higher, and why NetBSD gets away with the identical
construction (`xhci.c:3633`, `XHCI_SCTX_0_CTX_NUM_SET(dci)`).

FreeBSD does not rely on that.  It keeps a running maximum per device
(`xhci.c:2288-2298`):

```c
/* figure out the maximum number of contexts */
if (x > sc->sc_hw.devs[index].context_num)
        sc->sc_hw.devs[index].context_num = x;
else
        x = sc->sc_hw.devs[index].context_num;
```

**Fix.**  Track the highest DCI configured on the slot and write
`max(existing, dci)`.  `dsc[0]`'s existing Context Entries field is already
being read, so `max()` against it is a one-line change.

---

## Checked in this pass and found correct

**Data structure alignment and boundaries — all of Table 6-1 satisfied.**
`dma_alloc_coherent()` (`sys/kern/dma.c:41-75`) rounds every request up to whole
4096-byte pages and serves it from `pmm_alloc_contiguous()`, so every allocation
is page-aligned and physically contiguous.  Checked row by row:

| Structure | Required align | Must not span | Substrate | OK |
|-----------|----------------|---------------|-----------|-----|
| DCBAA | 64 | PAGESIZE | 520 B, page-aligned | yes |
| Device Context | 64 | PAGESIZE | 1024/2048 B, page-aligned | yes |
| Input Context | 64 | PAGESIZE | 1056/2112 B, page-aligned | yes |
| Stream Context array | 16 | PAGESIZE | 64 B, page-aligned | yes |
| Transfer Ring segment | 16 | 64KB | 1024 B, page-aligned | yes |
| Command Ring segment | 64 | 64KB | 1024 B, page-aligned | yes |
| Event Ring segment | 64 | 64KB | 1024 B, page-aligned | yes |
| ERST | 64 | none | 64 B, page-aligned | yes |
| Scratchpad array | 64 | PAGESIZE | page-aligned | yes |
| Scratchpad buffers | PAGESIZE | — | explicitly re-checked at xhci.c:1602 | yes |

The driver is relying on a property of the allocator rather than asserting it,
but the property holds and `xhci_alloc_scratchpad()` already re-checks the one
case (a controller PAGESIZE above 4K) where it would not.

**No 64KB data-buffer boundary rule to violate.**  Worth stating because EHCI
*does* have one and the two drivers sit side by side.  xHCI §3.2.1 and §4.11.2:
data buffers "may be byte aligned and reference from 1 to 64K bytes of
contiguous physical data", and "any buffer pointed to by a Normal, Data Stage,
or Isoch TRB in a TD may be any size between 0 and 64K bytes".  The 64KB
boundary rule applies to **TRB rings**, not to the buffers they point at.  The
64 KiB bounce buffer at `XHCI_BOUNCE_SIZE` is fine wherever it lands.

**Control transfers correctly use three separate TDs.**  §4.11.2.2: the Setup,
Data and Status Stage TRBs "provide a 1:1 mapping to the respective USB Control
transfer stages".  They are distinct TDs, so the absence of a Chain bit between
them in `xhci_control()` is correct, not an omission.  Setup Stage carries
length 8 with IDT set (§6.4.1.2.1); the Status Stage direction is inverted
against the data stage and forced IN when there is no data stage
(`xhci.c:1112`), which matches §6.4.1.2.3.

**TD Size is correctly zero.**  Every TD this driver builds is a single Transfer
TRB, and §4.11.2.4 requires TD Size = 0 in the last TRB of a TD.  The status
dword is written as the raw length, and since `len <= 65536` fits the 17-bit
TRB Transfer Length field, TD Size (bits 21:17) and Interrupter Target
(bits 31:22) both land as 0.

**Transfer Event residue arithmetic is the right way round.**
`XHCI_TRB_GET_XLEN` masks 24 bits (§6.4.2.1: status bits 23:0 are the transfer
length *remaining*, 31:24 the completion code), and both call sites compute
`actual_length = len - residue` with an underflow guard.

**Interrupter initialization order.**  ERSTSZ, then ERDP, then ERSTBA
(`xhci.c:1655-1657`).  ERSTBA last is what §4.9.4 requires — writing it is what
makes the interrupter live.  Single 64-entry segment with no Link TRB, and
`xhci_wait_event()` wraps the consumer index and flips `event_cycle` at the
wrap, which is correct for an event ring (event rings are not Link-TRB rings).

**Ring exhaustion is not reachable.**  `xhci_ring_push()` has no ring-full
check, which would be a defect in an asynchronous driver.  This one is
synchronous under `submit_lock` and never has more than three TRBs outstanding
on a 63-usable-entry ring; on the timeout path `xhci_stop_ep()` →
`xhci_set_tr_dequeue()` explicitly re-syncs the controller's dequeue pointer to
`ring->enq`.

**`xhci_set_ep0_mps()` uses the right command.**  Evaluate Context with A1
only, which is verbatim what §4.3 step 7.x prescribes for a Max Packet Size
correction after the first descriptor read.  (Contrast P3-02.)

**Route String.**  `usb_route_string()` (`usb.c:607-631`) emits tier 1 in bits
3:0, clamps each tier to 15 per §4.5.2, and masks to 20 bits — matches
FreeBSD's construction at `xhci.c:2530-2560`.

**Max Burst Size left at 0** is legal (one packet per burst) and is a
throughput limitation on SuperSpeed bulk endpoints, not a correctness bug.
Raising it requires parsing the SuperSpeed Endpoint Companion Descriptor,
which the core does not currently do.

**BIOS handoff and extended-capability walking.**  Bounded iteration, `0xFFFFFFFF`
termination, window bounds re-checked per entry, RsvdP preservation in
USBLEGCTLSTS, and the handoff correctly ordered before `xhci_reset()`.

**Reset sequencing.**  CNR wait, halt with HCH confirmation, then HCRST —
§5.4.1 requires HCHalted before HCRST and the driver refuses to assert it
otherwise, with a distinct diagnostic per failure mode.

---

## Suggested order

1. **R-01** (pass 2) — unchanged from that document's recommendation; it is
   still the only open finding that corrupts command results on live hardware.
2. **P3-01** and **P3-02** together — they describe the same devices (anything
   below an external hub) and both land in the slot context.  Fixing either
   alone leaves that topology broken.  **P3-03** rides along with P3-02's
   Configure Endpoint work.
3. **R-02**, **R-06** (pass 2) — one line each.
4. **P3-04**, **P3-05**, then **R-03**, **R-04**, **R-05** — latent or
   defensive; no known trigger in the current code.

Verifying 2 needs an external USB 2.0 hub with a low- or full-speed device
below it, on real xHCI silicon.  QEMU will not reproduce any of it: its xHCI
model uses neither the Slot Context Speed field nor the Hub bit.
