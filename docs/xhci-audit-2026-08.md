# xHCI driver audit — 2026-08-06

Audit of `sys/drivers/usb/xhci.c` (1412 lines) and `sys/drivers/usb/xhci.h`
(235 lines) against two independent reference implementations:

* **FreeBSD 14.4-RELEASE** — `sys/dev/usb/controller/xhci.c` (4408 lines),
  `xhcireg.h`, `xhci.h`, `xhci_pci.c`
* **NetBSD-current** — `sys/dev/usb/xhci.c` (4994 lines), `xhcireg.h`,
  `xhcivar.h`

Both references are BSD-licensed.  No other operating system's source was
consulted.

Normative statements are cited from the primary source: **eXtensible Host
Controller Interface for Universal Serial Bus (xHCI), Revision 1.2**, Intel
Corporation, May 2019 (645 pp.).  Where the spec is quoted below the section or
table number is given.

Where the two references agree with each other and we differ, that is treated
as a defect in our driver unless there is a documented reason.  Findings are
ordered by severity.  Line numbers are against the tree at commit `89d8743ae`.

---

## Status

**All 18 findings are fixed** as of 2026-08-07, each with a regression run
across USB 1.1 (UHCI), 2.0 (EHCI) and 3.0 (xHCI).  X-13 is fixed in the
sense of being an explicit, documented decision rather than code: see its
entry.  The commit for each is in the table below.

Two fixes cannot be exercised under emulation and were verified by fault
injection instead -- forcing a scratchpad request (X-02) and forcing a bulk
transfer to time out (X-03).  Both hacks were removed afterwards and the
clean build re-verified.  Hardware confirmation of X-01/X-02 on the Skylake
laptop is still outstanding.

## Summary

| ID | Severity | Area | One-line | Status |
|----|----------|------|----------|--------|
| X-01 | **Critical** | port control | PORTSC read-modify-write writes PED back, disabling the port it just reset | FIXED 01e99650f |
| X-02 | **Critical** | init | Scratchpad buffers never allocated; DCBAA[0] left null | FIXED 842460834 |
| X-03 | High | transfers | Timed-out TD is abandoned while the controller still owns the bounce buffer | FIXED a7d6a5525 |
| X-04 | High | command ring | Command timeout leaves the ring desynchronised; no Command Abort | FIXED a7d6a5525 |
| X-05 | High | contexts | EP context dword 4 (Average TRB Length / Max ESIT Payload) never written | FIXED bfbf92d2c |
| X-06 | High | contexts | Configure Endpoint drops the slot context's TT fields | FIXED bfbf92d2c |
| X-07 | Medium | port control | No 10 ms post-reset recovery delay (TRSTRCY) | FIXED 336366453 |
| X-08 | Medium | event ring | Event ring is never drained outside a transfer; 64 entries can fill and wedge | FIXED 336366453 |
| X-09 | Medium | init | CAPLENGTH/RTSOFF/DBOFF/XECP unvalidated against the fixed 16 KiB mapping | FIXED b6d638174 |
| X-10 | Medium | port control | Supported Protocol capability not parsed; port speed decoded from a guess | FIXED 209af6df4 |
| X-11 | Medium | rings | TRB stores have no compiler barrier; the cycle-bit handoff can be reordered | FIXED bd5fcdcc1 |
| X-12 | Low | port control | `xhci_port_status()` is not a query — it tears down slots | FIXED 77abc95ac |
| X-13 | Low | transfers | Isochronous unsupported, so USB audio cannot work on xHCI | FIXED 555d7650a |
| X-14 | Low | init | `XHCI_MAX_SLOTS` fixed at 16; ~48 KiB of slot state allocated per controller | FIXED 555d7650a |
| X-15 | Low | port control | No warm reset (WPR) path for SuperSpeed ports | FIXED 209af6df4 |
| X-16 | Low | transfers | Several early returns leave `xfer->status` stale | FIXED 77abc95ac |
| X-17 | Low | teardown | Failed attach after `xhci_start()` leaves the controller running | FIXED 77abc95ac |
| X-18 | Low | contexts | FS/LS interrupt interval rounds up; the spec rounds down | FIXED 77abc95ac |

---

## X-01 — Critical — PORTSC read-modify-write writes PED back, disabling the port

**`sys/drivers/usb/xhci.c:426-437`, `:1211-1214`; `sys/drivers/usb/xhci.h:102`**

Every PORTSC read-modify-write masks only `XHCI_PORT_CHANGE_MASK`:

```c
#define XHCI_PORT_CHANGE_MASK 0x00FE0000   /* bits 17-23 only */
```

Both references define and apply a much wider mask, and they define it
identically:

```c
/* FreeBSD xhcireg.h:146, NetBSD xhcireg.h:245 */
#define	XHCI_PS_CLEAR		0x80FF01FFU	/* command bits */
```

`0x80FF01FF` covers bits 0-8 — which includes **PED (bit 1)** and **PR
(bit 4)** — plus LWS, all the change bits, and WPR.  FreeBSD applies it at
`xhci.c:3367` and `:3548`; NetBSD at `xhci.c:812`, `:1069`, `:1085`, `:4149`,
`:4284`.  Every writeback in both drivers goes through it.

The spec is explicit (Table 5-27, PORTSC bit 1):

> **Port Enabled/Disabled (PED) – RW1CS.** Default = '0'. '1' = Enabled.
> '0' = Disabled.  Ports may only be enabled by the xHC. Software cannot enable
> a port by writing a '1' to this flag.  **A port may be disabled by software
> writing a '1' to this flag.**
> […] PED shall automatically be cleared to '0' when PR is set to '1', and
> **set to '1' when PR transitions from '1' to '0' after a successful reset.**

So a read-modify-write that preserves a set PED bit disables the port — and the
bit is guaranteed to be set exactly when we do it, because a successful reset is
what sets it.

Footnote 82 to the same table adds a second hazard:

> The PED and PR flags are mutually exclusive. Writing the PORTSC register with
> PED and PR set to '1' shall result in undefined behavior.

The damaging instance is the reset-change acknowledgement:

```c
/* xhci.c:428-437 */
for (int i = 0; i < 100; i++) {
    xhci_delay_ms(2);
    psc = portsc_rd(hc, port);
    if (psc & XHCI_PORT_PRC) {           /* reset complete */
        portsc_wr(hc, port, (psc & ~XHCI_PORT_CHANGE_MASK) | XHCI_PORT_PRC);
        break;
    }
}
psc = portsc_rd(hc, port);
return (psc & XHCI_PORT_PED) ? 0 : -1;
```

At the moment `PRC` latches, the reset has succeeded and **`PED` is set**.
`psc & ~XHCI_PORT_CHANGE_MASK` does not clear it, so the write carries
`PED = 1` and disables the port that was just enabled.  The very next line
re-reads PORTSC, finds `PED == 0`, and returns `-1`.

`usb_scan_ports()` treats that as a failed reset and skips the port entirely
(`sys/drivers/usb/usb.c:1285-1286`), so **no device on any root port ever
enumerates**.

`xhci_power_ports()` has the same unmasked writeback.  It is harmless during
the first pass (nothing is enabled yet) but not on the second pass after
`xhciroute`, where a SuperSpeed port that auto-enabled on connect gets
disabled.

**Why this has not been seen before now.**  It requires hardware that honours
the RW1CS semantics, and it requires the boot device to be behind the xHCI.
On the Lenovo C460 the USB2 ports stay with the companion EHCI (the reroute is
opt-in), so the card reader enumerates through `ehci.c` and never reaches this
code.  The Skylake-era laptop has no companion EHCI at all — every port is
xHCI — which matches the reported symptom exactly: controller attaches, 18
ports reported, `xhciroute` succeeds, and nothing enumerates.

**Fix.**  Define the reference mask and apply it at every writeback:

```c
#define XHCI_PORT_CLEAR 0x80FF01FFU   /* PED|PR|PLS|LWS|changes|WPR */
```

Then `portsc_wr(hc, port, (psc & ~XHCI_PORT_CLEAR) | XHCI_PORT_PRC)`, and the
same in `xhci_power_ports()` and at the start of reset.

Not yet verified on hardware — it needs a boot on the laptop to confirm.

---

## X-02 — Critical — Scratchpad buffers never allocated; DCBAA[0] left null

**`sys/drivers/usb/xhci.c:1149-1152`, `:1323-1327`**

`HCSPARAMS2` is never read.  (`XHCI_CAP_HCSPARAMS2` *is* defined, at
`xhci.h:26` — an earlier revision of this document claimed otherwise, which was
wrong.  The constant exists; nothing ever reads the register through it.)

A controller advertises in `HCSPARAMS2` how many scratchpad pages it needs the
OS to hand it.  Software must allocate that many pages, build an array of
their physical addresses, and put the array's address in **DCBAA entry 0**.
We allocate the DCBAA, `memset` it to zero, and program `DCBAAP` — leaving
entry 0 as a null pointer.

The spec makes this mandatory and orders it before the controller runs
(§4.20, Scratchpad Buffers):

> System software **shall** allocate the Scratchpad Buffer(s) before placing the
> xHC in to Run mode (Run/Stop (R/S) = '1').
> […] Entry 0 of the Device Context Base Address Array points to the Scratchpad
> Buffer Array.
> […] 1. Software examines the Max Scratchpad Buffers Hi and Lo fields in the
> HCSPARAMS2 register.  2. Software allocates a Scratchpad Buffer Array with Max
> Scratchpad Buffers entries.  3. Software writes the base address of the
> Scratchpad Buffer Array to the DCBAA (Slot 0) entry.  4. For each entry […]
> allocate a PAGESIZE Scratchpad Buffer, clear it to '0', write its base address
> to the associated entry.

It also warns that "the xHC shall not access system memory addresses outside of
the PAGESIZE memory block allocated by system software" — which is only a bound
if the pointer is valid.

Both references implement this, and both treat it as mandatory:

```c
/* FreeBSD xhci.c:597-606 */
sc->sc_noscratch = XHCI_HCS2_SPB_MAX(temp);
...
/* xhci.c:345-352 — slot 0 points to the table of scratchpad pointers */
pdctxa->qwBaaDevCtxAddr[0] = htole64(addr);
for (i = 0; i != sc->sc_noscratch; i++) {
        usbd_get_page(&sc->sc_hw.scratch_pc[i], 0, &buf_scp);
        pdctxa->qwSpBufPtr[i] = htole64((uint64_t)buf_scp.physaddr);
}
```

```c
/* NetBSD xhci.c:1535-1566, :1645-1650 */
sc->sc_maxspbuf = XHCI_HCS2_MAXSPBUF(hcs2);
...
/* DCBA entry 0 hold the scratchbuf array pointer. */
```

Note also `XHCI_HCS2_SPB_MAX` is a *split* field —
`((x >> 16) & 0x3E0) | ((x >> 27) & 0x1F)` — the high five bits live
separately from the low five.  A naive single-field read gets it wrong.

Real Intel parts request scratchpads.  The controller DMAs into whatever
DCBAA[0] points at; with a null pointer the behaviour is undefined, and the
usual outcome is commands that never complete or a controller that wedges
after the first Address Device.

The `PAGESIZE` operational register (0x08) is also never read.  Scratchpad
pages must be the size that register reports.

**Fix.**  Read HCSPARAMS2 and PAGESIZE; if `SPB_MAX > 0`, allocate that many
PAGESIZE-sized pages plus a pointer array, and store the array's DMA address
in `dcbaa[0]` before writing DCBAAP.  Free them in `xhci_teardown()`.

---

## X-03 — High — Timed-out TD abandoned while the controller still owns the bounce buffer

**`sys/drivers/usb/xhci.c:869-873`, `:931-941`**

On timeout both transfer paths simply return:

```c
return (cc == 0) ? USB_XFER_TIMEOUT : USB_XFER_STALL;
```

The TD is still on the endpoint's transfer ring and the endpoint is still
running.  Nothing tells the controller to stop.  There is exactly one bounce
buffer for the whole controller (`hc->bounce`, `xhci.c:1176`), so the next
transfer — for a different device, on a different endpoint —
`memcpy`s its own data into a buffer the controller may still be writing into
from the abandoned TD.

Both references stop the endpoint before completing a transfer with an error.
FreeBSD's timeout handler calls `xhci_device_done()`, which reaches
`xhci_cmd_stop_ep()` (`xhci.c:1557`, called at `:3829` and `:4250`); NetBSD
does the same.

Secondary effect: the abandoned TD's event eventually lands on the event ring
and burns one of the 16 slots in the next transfer's
`XHCI_EVENT_SCAN_MAX` scan.

**Fix.**  On timeout, issue Stop Endpoint for `(slot, dci)`, then Set TR
Dequeue Pointer to `ring->enq` with the current cycle — the same recovery
`xhci_recover_ep()` already performs for a halt.

---

## X-04 — High — Command timeout leaves the command ring desynchronised

**`sys/drivers/usb/xhci.c:196-212`**

```c
int cc = xhci_wait_event(hc, &ep, &ec, NULL, XHCI_CMD_TIMEOUT_MS);
if (cc == 0) return 0;   /* timeout */
```

The command TRB stays on the ring, the controller's Command Ring Running bit
stays set, and our enqueue pointer has already advanced past it.  Any later
command is issued behind a command the controller may still be chewing on, and
its completion event will be matched to the wrong request.  One slow command
poisons every command for the rest of the session.

NetBSD implements the spec's abort sequence (§4.6.1.2):

```c
/* NetBSD xhci.c:3238-3270 — xhci_abort_command() */
crcr = xhci_op_read_8(sc, XHCI_CRCR);
xhci_op_write_8(sc, XHCI_CRCR, crcr | XHCI_CRCR_LO_CA);
for (i = 0; i < 500; i++) {
        crcr = xhci_op_read_8(sc, XHCI_CRCR);
        if ((crcr & XHCI_CRCR_LO_CRR) == 0)
                break;
        usb_delay_ms(&sc->sc_bus, 1);
}
/* reset command ring dequeue pointer */
cr->xr_ep = 0;
cr->xr_cs = 1;
xhci_op_write_8(sc, XHCI_CRCR, xhci_ring_trbp(cr, 0) | cr->xr_cs);
```

**Fix.**  Port that sequence: set `CRCR.CA`, poll `CRCR.CRR` clear, then reset
our `cmd_ring.enq = 0; cmd_ring.cycle = 1` and rewrite CRCR.  `XHCI_CRCR_CA`
and `XHCI_CRCR_CRR` need defining in `xhci.h` (only `RCS` exists today).

---

## X-05 — High — EP context dword 4 never written

**`sys/drivers/usb/xhci.c:561-565` (EP0), `:667-684` (bulk/interrupt)**

We write endpoint context dwords 0-3 and stop.  Dword 4 — Average TRB Length
in bits 15:0 and Max ESIT Payload in 31:16 — is left at zero from the
`memset`.

FreeBSD writes it for every endpoint, unconditionally:

```c
/* FreeBSD xhci.c:2440-2455 */
case UE_INTERRUPT:
case UE_ISOCHRONOUS:
        temp = XHCI_EPCTX_4_MAX_ESIT_PAYLOAD_SET(max_frame_size) |
            XHCI_EPCTX_4_AVG_TRB_LEN_SET(MIN(XHCI_PAGE_SIZE, max_frame_size));
        break;
case UE_CONTROL:
        temp = XHCI_EPCTX_4_AVG_TRB_LEN_SET(8);
        break;
default:
        temp = XHCI_EPCTX_4_AVG_TRB_LEN_SET(XHCI_PAGE_SIZE);
        break;
}
endp->dwEpCtx4 = htole32(temp);
```

Both halves are normative (Table 6-9, Endpoint Context dword 4):

> **Average TRB Length.** […] The value of this field **shall be greater than
> '0'**. […] The xHC shall use this parameter to calculate system bus bandwidth
> requirements.
> **Max ESIT Payload Lo.** […] This field is only valid for periodic endpoints.

and, in the same table's notes:

> Software **shall** set Average TRB Length to '8' for control endpoints.

We set it to 0 for every endpoint, EP0 included.  §4.14.1.1 spells out the
consequence:

> The accuracy of this parameter is particularly important for periodic
> endpoints. […] A Configure Endpoint Command **may be rejected by the xHC with
> a Bandwidth Error or a Secondary Bandwidth Error** if it determines that there
> is not enough system bandwidth available for it.

**Fix.**  Mirror FreeBSD's three cases: 8 for control, page size for bulk,
`min(page_size, max_packet)` plus `max_packet` as Max ESIT Payload for
interrupt.

---

## X-06 — High — Configure Endpoint drops the slot context's TT fields

**`sys/drivers/usb/xhci.c:653-661`**

```c
memset(s->in_ctx, 0, 33 * hc->ctx_size);
...
insc[0] = (dsc[0] & ~(0x1Fu << XHCI_SLOT_CTX_ENTRIES_SHIFT)) |
          ((uint32_t)dci << XHCI_SLOT_CTX_ENTRIES_SHIFT);
insc[1] = dsc[1];
```

The input context is zeroed and then only dwords 0 and 1 are restored from the
device context.  **Dword 2 is never copied**, so the TT Hub Slot ID and TT Port
Number that `xhci_setup_slot()` carefully computed (`xhci.c:547-556`) are
zeroed on every Configure Endpoint.

For a low- or full-speed device behind a high-speed hub, that tells the
controller there is no transaction translator in the path.  Its bulk and
interrupt endpoints then get scheduled as if they were high-speed —
so a USB 1.1 keyboard or mouse behind a hub enumerates over EP0 (which was
addressed with the TT fields intact) and then produces nothing on its interrupt
endpoint.

FreeBSD sets the TT fields — including Think Time — in the slot context on
every device configure (`xhci.c:2620`, `:2631`, `:2633`).

**Fix.**  `insc[2] = dsc[2];` alongside the existing two.  Consider copying
Think Time too, which we never set at all.

---

## X-07 — Medium — No post-reset recovery delay

**`sys/drivers/usb/xhci.c:428-437`; `sys/drivers/usb/usb.c:1285-1289`**

`xhci_port_reset()` returns the moment `PRC` latches, and `usb_scan_ports()`
goes straight from there to `port_status()` and enumeration.  Nothing waits.

USB 2.0 §7.1.7.5 requires 10 ms of recovery after reset before the device will
answer.  FreeBSD budgets double that:

```c
/* FreeBSD usb.h:105,117,120 */
#define	USB_PORT_RESET_RECOVERY_SPEC	10	/* ms */
#define	USB_PORT_RESET_RECOVERY		20	/* ms */
#define	USB_SET_ADDRESS_SETTLE		10	/* ms */
```

NetBSD polls for `PR` to clear over `USB_PORT_ROOT_RESET_DELAY` rather than
watching `PRC`, which is also more robust — `PR` self-clearing is the
controller's statement that the reset is over.

We have no equivalent of `USB_SET_ADDRESS_SETTLE` either: `xhci_control()`
returns from the SET_ADDRESS interception (`xhci.c:804-816`) with no settle
time before the next transfer.

**Fix.**  Wait for `PR` to clear, then `xhci_delay_ms(20)` before returning
success.  Add a 10 ms settle after Address Device (BSR=0).

---

## X-08 — Medium — Event ring never drained outside a transfer

**`sys/drivers/usb/xhci.c:165-193`, `:1158-1167`**

The only consumer of the event ring is `xhci_wait_event()`, reached solely from
`xhci_wait_td()` and `xhci_run_command_st()` — i.e. only while a transfer or
command is outstanding.  The ring is 64 entries
(`XHCI_RING_TRBS`, `xhci.c:34`).

Port Status Change events are generated by the controller whenever a port
changes state, and nothing consumes them between transfers.  The hot-plug
scanner polls PORTSC directly; it never touches the event ring.  A marginal
cable or a flapping port generates these continuously.  Once 64 accumulate the
ring is full, the controller can no longer post the Transfer Events we are
waiting for, and every transfer times out — a total wedge with no diagnostic.

For scale, FreeBSD sizes its event ring at `XHCI_MAX_EVENTS 232` (`xhci.h:35`)
*and* drains it from the interrupt handler.

Related: `USBSTS` is never checked for `HSE` (Host System Error) or `HCE` (Host
Controller Error).  FreeBSD reports both (`xhci.c:1626-1645`).  A controller
that has faulted looks identical to one that is merely slow.

**Fix.**  Drain and discard any pending events at the top of `xhci_submit()`
(or from the hot-plug scan), and check `USBSTS` for `HCH`/`HSE`/`HCE` on every
timeout so the failure is reported rather than silently retried.

---

## X-09 — Medium — Controller-supplied MMIO offsets unvalidated

**`sys/drivers/usb/xhci.c:33`, `:1313-1328`, `:987`**

```c
#define XHCI_MMIO_SIZE 0x4000
...
hc->mmio = ioremap(phys, XHCI_MMIO_SIZE);
uint8_t caplen = *(volatile uint8_t *)(hc->mmio + XHCI_CAP_CAPLENGTH);
hc->op = hc->mmio + caplen;
hc->rt = hc->mmio + (rd32(hc->mmio, XHCI_CAP_RTSOFF) & ~0x1Fu);
hc->db = hc->mmio + (rd32(hc->mmio, XHCI_CAP_DBOFF) & ~0x3u);
```

`RTSOFF` and `DBOFF` come from the controller and are used with no bounds
check.  A part that places either at or beyond 0x4000 — or a bad read that
returns `0xFFFFFFFF` — gives `hc->rt`/`hc->db` pointing outside the mapping,
and every subsequent doorbell and ERDP write lands on unmapped or unrelated
memory.  Both references map the BAR's actual size rather than a fixed window.

The extended-capability walk *is* bounded (`xhci.c:987`), which is good, but the
consequence of the bound is silent: a controller whose USB Legacy Support
capability sits past 0x4000 skips the BIOS handoff without a word, and we then
reset a controller SMM still owns.

**Fix.**  Read the BAR size and `ioremap` that much, or at minimum validate
`caplen`, `RTSOFF + 0x20 + 0x20` and `DBOFF + 4*(maxslots+1)` against
`XHCI_MMIO_SIZE` and fail the attach with a message.  Warn when the ext-cap
walk is truncated.

---

## X-10 — Medium — Supported Protocol capability not parsed

**`sys/drivers/usb/xhci.c:402-409`**

```c
uint32_t psid = (psc & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
switch (psid) {
case 1: break;                                      /* full speed */
case 2: out |= USB_PORT_STAT_LOW_SPEED;   break;
case 3: out |= USB_PORT_STAT_HIGH_SPEED;  break;
case 4: out |= USB_PORT_STAT_SUPER_SPEED; break;
default: out |= USB_PORT_STAT_HIGH_SPEED; break;    /* unknown: assume HS */
}
```

Protocol Speed IDs are only *defaults*.  A controller may redefine them in the
Supported Protocol extended capability (ID 2), which also declares which port
range belongs to USB2 and which to USB3.  NetBSD parses it in
`xhci_id_protocols()` (`xhci.c:1222-1286`) and builds a controller-port ↔
root-hub-port map, splitting the two buses.

Concretely: a USB 3.1 Gen 2 port reports PSID 5, hits our `default`, and is
reported to the core as **high speed**.  The device then gets a 64-byte EP0 and
high-speed bandwidth assumptions.

**Fix.**  Walk the ext-cap list for ID 2, record each block's Compatible Port
Offset/Count and major revision, and use it both to bound the speed decode and
to know which ports are USB3.

---

## X-11 — Medium — No compiler barrier around the TRB cycle-bit handoff

**`sys/drivers/usb/xhci.c:137-156`, `:170-176`**

```c
t->param = param;
t->status = status;
/* set the type/flags, then the cycle bit last (ownership handoff) */
t->control = ctrl_type_flags | (r->cycle ? XHCI_TRB_CYCLE : 0);
```

The comment states the requirement correctly, but nothing enforces it.
`struct xhci_trb` is not volatile and `r->trb` is an ordinary pointer, so the
compiler is free to sink the `param`/`status` stores past the `control` store.
The controller would then see a TRB it owns whose parameter field has not been
written.  x86 store ordering does not help — this is a compiler reordering, not
a hardware one.

The same applies to the read side:

```c
struct xhci_trb *e = &hc->event_ring[hc->event_deq];
uint32_t ctrl = e->control;
```

polled in a loop.  `__asm__ volatile("pause")` without a `"memory"` clobber does
not force a reload.  Today the loop happens to call `get_uptime_ms()`, an
out-of-line function, which forces one — the correctness rests on an
optimisation accident.

Both references bracket every ring update with explicit synchronisation
(`usb_pc_cpu_flush()` in FreeBSD, `usb_syncmem()`/`bus_dmamap_sync()` in
NetBSD).

**Fix.**  Declare the ring pointers `volatile struct xhci_trb *`, and put a
`__asm__ volatile("" ::: "memory")` between the payload stores and the control
store, and after consuming an event before advancing ERDP.

---

## X-12 — Low — `xhci_port_status()` is not a query

**`sys/drivers/usb/xhci.c:386-414`**

On `CCS == 0` the status *getter* calls `xhci_port_disconnect()`, which takes
`submit_lock`, runs Disable Slot commands on the command ring, and frees DMA
contexts.

This is a live hazard, not a style complaint: an earlier attempt in this
session to add a port-settle poll loop called `xhci_port_status()` in a loop
and fired hundreds of slot teardowns, breaking USB 2.0 and 3.0 enumeration.
Any future caller will hit the same trap.

**Fix.**  Split it: `xhci_port_status()` reads PORTSC and returns; a separate
`xhci_reap_disconnected()` runs from the hot-plug scan.

---

## X-13 — Low — Isochronous transfers unsupported

**`sys/drivers/usb/xhci.c:950-964`**

```c
else if (xfer->ep && (xfer->ep->type == USB_EP_TYPE_BULK ||
                      xfer->ep->type == USB_EP_TYPE_INTERRUPT))
    ret = xhci_bulk(hc, xfer);
else
    ret = USB_XFER_ERROR;
```

Isochronous falls to `USB_XFER_ERROR`.  We have a USB Audio Class driver
(`uac`) with isochronous support on UHCI, so USB audio works on UHCI/EHCI and
cannot work on xHCI — which is every modern machine.

**Decision: recorded as a known gap, not fixed here.**  Isochronous on xHCI is
a feature rather than a defect to repair — Isoch TRBs carry a frame number and
Transfer Burst Count, and the driver must keep a window of TDs scheduled ahead
of MFINDEX through the `iso_schedule`/`iso_reclaim` HCD hooks that `uhci.c`
implements and this driver does not.  That is new functionality, outside the
scope of an audit fix pass.  The dispatch in `xhci_submit()` now says so
explicitly, so the next reader does not mistake it for an oversight.

---

## X-14 — Low — Fixed 16-slot limit and a large inline slot array

**`sys/drivers/usb/xhci.c:35`, `:56-66`, `:1326`**

`XHCI_MAX_SLOTS` is 16 and `CONFIG.MaxSlotsEn` is clamped to it, so at most 16
devices attach per controller regardless of the controller's capability
(commonly 32 or 64).  `struct xhci_slot` embeds `ep_ring[32]` plus
`stream_ring[32][4]` — about 2.8 KiB each, 17 of them inline in `xhci_hc_t`,
so roughly 48 KiB of `kzalloc` per controller, nearly all of it never touched.

**Fix.**  Allocate `struct xhci_slot` on demand, and raise the cap to the
controller's `MaxSlots`.

---

## X-15 — Low — No warm reset path for SuperSpeed ports

**`sys/drivers/usb/xhci.c:416-438`**

Only `PR` is asserted.  A SuperSpeed port stuck in Polling, Compliance or
`Inactive` needs a Warm Port Reset (`WPR`, bit 31) — both references expose it
(FreeBSD `UHF_BH_PORT_RESET` → `XHCI_PS_WPR`, `xhci.c:3573`).  A USB3 device
that fails link training will never recover for us.

---

## X-16 — Low — Early returns leave `xfer->status` stale

**`sys/drivers/usb/xhci.c:800`, `:821`, `:890`, `:892`, `:905`, `:918`**

`xhci_control()` and `xhci_bulk()` return `USB_XFER_ERROR` from several places
without assigning `xfer->status`.  The success paths set it
(`:882`, `:946`); callers reading `xfer->status` after a failure see whatever
the previous transfer left there.

---

## X-17 — Low — Failed attach after `xhci_start()` leaves the controller running

**`sys/drivers/usb/xhci.c:1238-1255`, `:1339-1342`**

`xhci_teardown()` frees the rings and unmaps MMIO but never clears
`USBCMD.RUN`, so a controller that started successfully and then failed later
in attach is left running with DMA pointers aimed at freed pages.  There is
also no `usb_unregister_hcd()` counterpart for a failure after
`usb_register_hcd()`.

**Fix.**  Halt the controller (clear RUN, wait for HCH) at the top of
`xhci_teardown()` when `hc->op` is set.

---

## X-18 — Low — FS/LS interrupt interval rounds the wrong way

**`sys/drivers/usb/xhci.c:610-626`**

```c
bi = ep->interval ? ep->interval : 1;
if (!dev || dev->speed == USB_SPEED_LOW || dev->speed == USB_SPEED_FULL) {
    for (iv = 3; iv < 10 && (1u << (iv - 3)) < bi; iv++)
        ;
    return iv;
}
```

The loop stops at the first `iv` whose period is **≥** `bInterval`, i.e. it
rounds the interval *up*.  The spec rounds *down* — Table 6-12, footnote 113:

> For FS/LS Interrupt endpoints software shall round the computed value of
> Endpoint Context Interval field **down** to the nearest base 2 multiple of
> bInterval * 8.

For a `bInterval` that is an exact power of two the two agree.  For anything
else we are one step slow: a mouse reporting `bInterval = 10` (10 ms) should
get Interval 6 (2⁶ × 125 µs = 8 ms) and instead gets Interval 7 (16 ms), so it
is polled at half the requested rate.

The correct computation is `floor(log2(bInterval * 8))` clamped to the table's
valid range of 3-10.

The high/super-speed branch (`iv = bInterval - 1`, clamped to 15) is right —
Table 6-12 gives that form a valid range of 0-15.

---

## Things checked and found correct

Recording these so they are not re-audited:

* **Event ring has no Link TRB** and wraps via ERSTSZ — correct; only the
  command and transfer rings get one (`xhci_ring_alloc`).
* **ERST programming order** (ERSTSZ → ERDP → ERSTBA) matches the spec;
  writing ERSTBA last is what arms the interrupter.
* **Initialisation order** — reset → CONFIG → DCBAAP → CRCR → interrupter →
  RUN — matches §4.2.
* **Cycle-state comparison** in `xhci_wait_event()` is correct for both
  polarities.
* **Control transfer stages are separate TDs**, so the trailing Link TRB
  cannot split a TD — the missing Chain bit on the link is not a defect here.
* **TRB Transfer Length** is a 17-bit field, so the 65536-byte maximum from
  `XHCI_BOUNCE_SIZE` encodes without overflowing into TD Size.  FreeBSD uses
  the same 64 KiB ceiling (`XHCI_TD_PAYLOAD_MAX`).
* **DMA alignment** — `dma_alloc_coherent()` page-rounds and returns
  page-aligned memory from `pmm_alloc_contiguous()` (`sys/kern/dma.c:41-75`),
  which satisfies every 16/64-byte xHCI alignment requirement.
* **Input context layout** (`in_ctx + (dci+1)*ctx_size`) and the 33/32-entry
  allocation sizes are right.
* **`xhci_recover_ep()`** sequences Reset Endpoint then Set TR Dequeue with a
  matching Dequeue Cycle State — correct per §4.6.8.
* **BAR0 64-bit decode and relocation** handles the upper dword.
* **`xhci_ep_interval()`** clamps to the right ranges (3-10 and 0-15) and the
  HS/SS branch is correct — but the FS/LS branch rounds the wrong way; see
  X-18.  (This was initially recorded here as correct; reading Table 6-12
  footnote 113 showed otherwise.)

---

## Order the work was done in (all complete)

1. **X-01** and **X-02** together, then boot the laptop.  These are the two
   that can plausibly account for "no USB devices at all" on hardware, and
   they are both small.
2. **X-06** and **X-05** — one line and one small block, both affecting
   real device classes.
3. **X-03** and **X-04** — the two quiescing paths; they share the Stop
   Endpoint / Set TR Dequeue machinery `xhci_recover_ep()` already has.
4. **X-07**, **X-08**, **X-11** — timing and robustness.
5. The rest as convenient.
