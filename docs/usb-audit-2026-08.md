# USB stack audit — 2026-08

Audit of `sys/drivers/usb/` (8,333 lines: core, hub, UHCI, EHCI, xHCI, HID,
mass storage, UAS, audio, usbfs) against the FreeBSD (`sys/dev/usb/`) and
NetBSD (`sys/dev/usb/`) host stacks.

Every finding below was verified against at least one reference
implementation; the specific file and function consulted is cited. Findings
are ordered by severity. Nothing here is fixed yet.

Line numbers are as of `f8681365c`.

---

## HIGH

### USB-01 — xHCI slot context has no Route String: no device behind a hub can be addressed

`xhci.c:410-413` builds the input Slot Context with three fields:

```c
sc[0] = (1u << XHCI_SLOT_CTX_ENTRIES_SHIFT) | (speed << XHCI_SLOT_SPEED_SHIFT);
sc[1] = (uint32_t)port << XHCI_SLOT_RHPORT_SHIFT;
```

The Route String (`sc[0]` bits 19:0) is left zero. xHCI 1.1 §4.5.2 makes the
Route String the *only* thing that identifies a device downstream of a hub —
Root Hub Port Number alone names the port, not the device on it. With a zero
route string the controller directs Address Device and every subsequent
transfer at the port's directly-attached device, i.e. the hub itself.

NetBSD `xhci.c:3761` (`xhci_setup_route`) walks the parent chain to build the
route before any Address Device. It also sets fields substrate never writes:

| Field | NetBSD | substrate |
|---|---|---|
| Route String | `XHCI_SCTX_0_ROUTE_SET(route)` (3761) | absent |
| Hub bit | `XHCI_SCTX_0_HUB_SET` (3850) | absent |
| Number of Ports | `XHCI_SCTX_1_NUM_PORTS_SET` (3819) | absent |
| MTT | `XHCI_SCTX_0_MTT_SET` (3851) | absent |
| TT Hub Slot ID | `XHCI_SCTX_2_TT_HUB_SID_SET` (3853) | absent |
| TT Port Number | `XHCI_SCTX_2_TT_PORT_NUM_SET` (3854) | absent |

**Consequence:** on xHCI, only devices plugged directly into a root port work.
Anything behind an external hub fails to enumerate, and a low/full-speed
device behind a high-speed hub fails even with a correct route because the TT
fields are what tell the controller to use split transactions.

This is invisible under QEMU because every test so far attached devices
straight to root ports.

### USB-02 — EHCI describes every endpoint as high-speed with no split-transaction fields

`ehci.c:266-278` hardcodes the speed:

```c
uint32_t ec = (addr << EHCI_QH_ADDR_SHIFT) |
              (ep   << EHCI_QH_ENDPT_SHIFT) |
              EHCI_QH_EPS_HIGH |                    /* always */
              ((mpl & 0x7FF) << EHCI_QH_MPL_SHIFT);
```

and `ehci_run_qh()` (`ehci.c:233`) sets `endp_cap = EHCI_QH_MULT_ONE` — no hub
address, hub port, C-mask or S-mask.

NetBSD `ehci.c` (`ehci_open`) sets `EHCI_QH_SET_EPS(speed)` from the device's
actual speed, `EHCI_QH_CTL` for a full/low-speed *control* endpoint, and fills
`qh_endphub` with `HUBA`/`PORT`/`CMASK`/`SMASK`.

**Consequence:** a full- or low-speed device behind a high-speed hub cannot
transfer on EHCI. Root-port FS/LS devices are at least handed to a companion
controller (`ehci.c:113-116`, `140-144` set `EHCI_PORT_OWNER`) — but on a
modern Intel PCH there is no companion UHCI/OHCI, only an integrated
rate-matching hub reached through split transactions, so that escape hatch is
a dead end on exactly the hardware currently under test.

Together with USB-01 this means **no USB stack path in the tree can talk to a
low-speed device behind a hub.** That covers most USB keyboards.

### USB-03 — enumeration gives up after a single failed descriptor read

`usb.c:859-865` reads the first 8 bytes of the device descriptor once and
destroys the device on any error:

```c
ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dd, 8);
if (ret != USB_XFER_OK) {
    kprintf("usb: port %u: failed to get device descriptor (initial, err=%d)\n", ...);
    usb_free_device(dev);
    return -1;
}
```

NetBSD `usb_subr.c:1492-1506` retries **ten times**, sleeping 200 ms between
attempts and re-resetting the port every fourth:

```c
	/* Try a few times in case the device is slow (i.e. outside specs.) */
	for (i = 0; i < 10; i++) {
		err = usbd_get_initial_ddesc(dev, dd);
		if (!err) break;
		usbd_delay_ms(dev, 200);
		if ((i & 3) == 3)
			usbd_reset_port(up->up_parent, port, &ps);
	}
```

FreeBSD `usb_device.c:1950-1957` calls `usbd_req_re_enumerate()` twice on
failure.

**Consequence:** any device slower than spec — which is most cheap hubs and
keyboards immediately after a port reset — fails to enumerate on the first
attempt and is never retried within a scan. This is precisely the failure
mode reported on the Lenovo C460.

### USB-04 — no second port reset before SET_ADDRESS

`usb.c:867-888` goes straight from the initial 8-byte read to
`usb_set_address()`. Both references reset the port again first; NetBSD
`usb_subr.c:1514-1516` is explicit about why:

```c
	/* Windows resets the port here, do likewise */
	if (up->up_parent)
		usbd_reset_port(up->up_parent, port, &ps);
```

Devices are widely tuned against the Windows sequence, so ones that need this
reset exist in quantity and behave erratically without it.

### USB-05 — `usbdevfs_meta_read` discloses kernel stack to userspace

`usbdevfs.c:118-140`:

```c
char meta[384];
n = snprintf(meta, sizeof(meta), "port=%u\nspeed=%u\n...");
for (int i = 0; i < dev->num_endpoints && n > 0 && n < (int)sizeof(meta); i++) {
    n += snprintf(meta + n, sizeof(meta) - (size_t)n,
                  "ep%x_max_packet_size=%u\n", ...);
}
if (n < 0 || off >= (size_t)n) return 0;
if (size > (size_t)n - off) size = (size_t)n - off;
memcpy(buffer, meta + off, size);
```

`snprintf` here returns the C99 would-have-written length — `emit_char()` in
`sys/lib/printf.c:296-302` increments `state->len` unconditionally and only
stores while `remaining > 1`. So a truncating call leaves `n > sizeof(meta)`.
The loop then stops, but `n` is never clamped, and the `memcpy` length is
derived from it.

Each endpoint line is ~26 bytes (`ep81_max_packet_size=1024\n`) and the header
~48, so 13 endpoints exceed 384. `USB_MAX_ENDPOINTS` is 16, and any device can
simply declare 16 endpoints.

**Consequence:** reading `/dev/usb/bus0/dev<addr>.meta` copies up to ~180
bytes past the end of a kernel stack buffer into a userspace read buffer. The
node is created by `kzalloc` so its mode/uid/gid are 0 — root-only today, so
this is an information leak rather than a privilege boundary crossing, but it
is a leak of arbitrary adjacent stack.

---

## MEDIUM

### USB-06 — `wMaxPacketSize` mult bits are never masked off

`usb.c:620` stores the raw descriptor field:

```c
ep->max_packet = ep_desc->wMaxPacketSize;
```

For high-speed (and SuperSpeed) interrupt and isochronous endpoints, bits 12:11
are "additional transactions per microframe", not part of the size. FreeBSD
`usb_transfer.c:502-517` splits them:

```c
	case USB_SPEED_HIGH:
		switch (type) {
		case UE_ISOCHRONOUS:
		case UE_INTERRUPT:
			xfer->max_packet_count += (xfer->max_packet_size >> 11) & 3;
			...
		}
		xfer->max_packet_size &= 0x7FF;
```

NetBSD uses `UE_GET_SIZE(a) ((a) & 0x7ff)` / `UE_GET_TRANS(a)` (`usb.h:307-308`)
everywhere it consumes the field.

Substrate's consumers differ in how much they get away with:

- `ehci.c:276` masks at the use site (`mpl & 0x7FF`) — the QH is safe, but the
  Mult field in `endp_cap` is hardcoded to 1, so a high-bandwidth endpoint
  silently runs at 1/2 or 1/3 of its declared bandwidth;
- `ehci.c:350` uses the **unmasked** value for the data-toggle packet-count
  math, so the ending toggle is wrong for such an endpoint;
- `xhci.c:419` and `xhci.c:494` write it **unmasked** into the EP context Max
  Packet Size field — a declared `0x1400` becomes 5120, which is not a legal
  MPS and will fail Configure Endpoint;
- `uhci.c` is full/low-speed only, where the mult bits are always zero — safe.

Most affected: USB audio (`uac.c`), where high-bandwidth isochronous endpoints
are the norm.

### USB-07 — a failed SET_ADDRESS leaks the USB address permanently

`usb.c:873-888`:

```c
int a = usb_addr_alloc();          /* marks the bit used */
...
ret = usb_set_address(dev, addr);
if (ret != USB_XFER_OK) {
    usb_free_device(dev);          /* -> if (dev->address) usb_addr_free(...) */
    return -1;
}
```

`usb_set_address()` (`usb.c:404-417`) only assigns `dev->address` **on
success**, so on failure `dev->address` is still 0 and `usb_free_device()`
(`usb.c:196-197`) skips the `usb_addr_free()`. Every failed SET_ADDRESS burns
one of the 127 addresses for the lifetime of the boot.

The retry cap (`USB_ENUM_MAX_TRIES`) bounds this per port, but a machine with
several ports that report a device they cannot enumerate — again, the C460 —
walks the address space down over time and eventually hits
`usb: address space exhausted`.

### USB-08 — `USBDEVFS_CONTROL` IN transfers never return data

`usbdevfs.c:79-87`:

```c
ret = usb_control_transfer(dev, ct.bRequestType, ct.bRequest, ...);
if (ret > 0 && ct.data != NULL) {
    size_t n = (size_t)(ret < (int)len ? ret : len);
    copyout(kbuf, ct.data, n);
}
```

`usb_control_transfer()` returns a `USB_XFER_*` status, not a byte count, and
`USB_XFER_OK` is **0**. `ret > 0` is therefore never true on success, so no
data is ever copied out and the ioctl reports 0 bytes transferred.

Only the cached-descriptor fast path above it (`usbdevfs.c:55-77`) works, which
is why `lsusb` appears functional — it needs nothing else. Any libusb client
issuing a string-descriptor or class request gets silent success with an
untouched buffer. The transfer *is* performed on the wire; only the result is
dropped.

### USB-09 — only UHCI honours the per-transfer timeout

`usb_transfer_t.timeout_ms` (`usb.h:396`) exists so an interrupt-endpoint poll
can give up in a few milliseconds instead of sitting on the bulk timeout —
`usb_hid.c:363-367` relies on exactly that. Only `uhci.c:761` reads it:

```c
xfer->timeout_ms ? xfer->timeout_ms : 5000
```

`ehci.c` uses a fixed `EHCI_XFER_TIMEOUT_MS` (1000, `ehci.c:35`) and `xhci.c` a
fixed `XHCI_CMD_TIMEOUT_MS` (1000, `xhci.c:35`).

**Consequence:** each idle HID poll blocks a full second on EHCI and xHCI
instead of the requested short window. With the hot-plug kthread also issuing
control transfers per hub port, the USB thread spends nearly all its time in
timeouts.

### USB-10 — EHCI runs interrupt endpoints on the async schedule

`ehci.c:369-370` dispatches interrupt transfers into the bulk path:

```c
else if (xfer->ep && xfer->ep->type == USB_EP_TYPE_INTERRUPT)
    ret = ehci_bulk_transfer(hc, xfer);   /* single-qTD IN, same path */
```

EHCI places interrupt endpoints in the *periodic* schedule with an S-mask
selecting the microframes to poll, which is what implements `bInterval`. On
the async schedule the endpoint is retried as fast as the schedule cycles and
`bInterval` is ignored entirely. It functions — the device NAKs until it has
data — but it burns async bandwidth continuously and gives no interval
guarantee.

### USB-11 — no Evaluate Context after learning the real EP0 max packet size

`xhci.c:416-420` programs the EP0 context from `xfer->ep->max_packet`, which at
slot-setup time is the core's *guess* (`usb.c:856`: 8 for low speed, 64
otherwise). xHCI 1.1 §4.3.4 requires re-issuing the endpoint context via
**Evaluate Context** once `bMaxPacketSize0` is known.

NetBSD does exactly that — `xhci_update_ep0_mps()` (`xhci.c:3465`, issuing
`XHCI_TRB_TYPE_EVALUATE_CTX` at 3491) is called at `xhci.c:3027` right after
the initial descriptor read.

Substrate defines `TRB_EVAL_CONTEXT` (`xhci.h:147`) and never uses it. A
full-speed device with an 8-byte EP0 is addressed with a context claiming 64.

### USB-12 — SuperSpeed `bMaxPacketSize0` is an exponent, used as a literal

`usb.c:867` assigns it directly:

```c
dev->ep0.max_packet = dd.bMaxPacketSize0;
```

For SuperSpeed, `bMaxPacketSize0` is `log2(size)` — the only legal value is 9,
meaning 512. NetBSD `xhci.c:3013-3023`:

```c
		if (USB_IS_SS(speed)) {
			if (dd->bMaxPacketSize != 9) { ...; dd->bMaxPacketSize = 9; }
			USETW(dev->ud_ep0desc.wMaxPacketSize, (1 << dd->bMaxPacketSize));
		} else
			USETW(dev->ud_ep0desc.wMaxPacketSize, dd->bMaxPacketSize);
```

Substrate would use an EP0 max packet of 9. Currently latent: the core has no
`USB_SPEED_SUPER` at all (`usb.h:31-33`) and `xhci_port_status()`
(`xhci.c:295-296`) reports every connected port as high-speed, so a real
SuperSpeed device is mis-described from the start.

### USB-13 — device descriptor contents are never validated

After the full read (`usb.c:901-908`) substrate accepts whatever came back.
NetBSD `usb_subr.c:1535-1547` rejects a descriptor whose `bDescriptorType` is
not `UDESC_DEVICE` or whose `bLength` is under 18, and `usb_subr.c:1519-1527`
forces `bMaxPacketSize0` to 64 for high speed (spec §5.5.3).

Substrate's only check is `usb.c:868-869` (`max_packet == 0` → 8). A glitched
read that returns plausible garbage yields an EP0 packet size like 0x2A, and
every later control transfer is then framed wrongly — which looks like a
device that "sometimes fails to enumerate" rather than a bad read.

### USB-14 — hub port state is polled by control transfer, not the interrupt endpoint

`usb_hub_scan_ports()` (`usb_hub.c:259-309`) issues a `GET_STATUS` control
transfer per port, per scan, at the hot-plug thread's 4 Hz. With the raised
limits (16 hubs × 15 ports) that is up to 960 control transfers a second.

Both references use the hub's status-change interrupt endpoint, which reports a
bitmap only when something actually changes. Substrate parses the endpoint
during enumeration but never opens it.

Combined with USB-09 (each transfer able to block a full second on EHCI/xHCI)
this is what turns one unenumerable port into a console-flooding,
throughput-destroying loop.

---

## LOW

### USB-15 — `bCBWCBLength` is written before the CDB length is clamped

`usb_msc.c:180-183`:

```c
cbw->bCBWCBLength = cdb_len;
if (cdb_len > 16)
    cdb_len = 16;
memcpy(cbw->CBWCB, cdb, cdb_len);
```

The `memcpy` is safe, but the CBW carries the unclamped length on the wire.
BOT restricts `bCBWCBLength` to 1–16; a device receiving more is required to
treat the CBW as invalid, stalling both endpoints and dragging the driver
through reset recovery. Latent while every caller stays within 16 bytes.

### USB-16 — CSW residue is trusted without bound

`usb_msc.c:303-304` stores `csw->dCSWDataResidue` unvalidated. BOT requires
residue ≤ `dCBWDataTransferLength`; a device reporting more makes the caller's
`data_len - residue` underflow. Both references range-check it.

### USB-17 — data-phase loop trusts `actual` not to exceed the chunk

`usb_msc.c:232-236` and `264-268` do `remaining -= actual` with no check that
`actual <= chunk_size`. No current HCD can return more than requested, so this
is defensive only — but the failure mode is an unbounded loop over a wrapped
`remaining`.

### USB-18 — `USBDEVFS_CLAIMINTERFACE` and friends are unchecked no-ops

`usbdevfs.c:98-106` returns 0 for `SETCONFIGURATION`, `CLAIMINTERFACE`,
`SETINTERFACE`, `RESET` and `DISCONNECT` without doing or recording anything.
A userspace client believes it owns an interface a kernel class driver is
actively using. Separately, `USBDEVFS_CONTROL` will happily issue
`SET_ADDRESS` or `SET_CONFIGURATION`, desynchronising the kernel's cached
descriptors and endpoint table from the device. Linux usbfs enforces interface
claims and blocks these requests; substrate's only protection is the node's
mode-0 permissions.

### USB-19 — the global device table has no locking

`usb_devices[]` and `usb_addr_bitmap[]` (`usb.c:64-69`) are touched by the
hot-plug kthread, by enumeration, and by class-driver detach paths with no
lock. Single-CPU scheduling makes this survivable today; it is a genuine
hazard once the APs actually schedule.

### USB-20 — `usb_hotplug_scan` cannot throttle ports above 31

`usb.c:1131-1132` only takes a failure counter for `port < USB_MAX_ROOT_PORTS`
(32). An `hcd->nports` above that leaves the high ports with `fails == NULL`
and therefore no retry cap — the infinite re-probe loop that
`USB_ENUM_MAX_TRIES` exists to prevent. No controller in the tree reports more
than 31 root ports today, so this is latent.

### USB-21 — a USB 3.x hub is asked for a USB 2.0 hub descriptor

`usb_hub_attach()` (`usb_hub.c:351-356`) always requests descriptor type
`USB_DT_HUB` (0x29). SuperSpeed hubs answer only to `0x2A`
(`USB_DT_SS_HUB`) and stall 0x29, so a USB 3 hub fails to attach. Moot while
the core cannot represent SuperSpeed at all (USB-12), but it will surface the
moment that is fixed.

### USB-22 — `usb_set_configuration(0)` is treated as configured

`usb.c:419-434` sets `configured = 1` for any successful SET_CONFIGURATION,
including configuration 0 — which by spec *un*configures the device and
returns it to Address state. A device whose `bConfigurationValue` is 0 is
recorded as configured while being anything but.

---

## Notes on what was checked and found sound

- Hot-unplug teardown ordering (`usb.c:1064-1101`) — children first, then
  driver detach, then devtree and usbfs nodes, then free. Matches the
  ordering both references use, and the recursion is correctly bounded by the
  tier limit.
- The `USB_ENUM_MAX_TRIES` parking logic in both `usb.c` and `usb_hub.c` —
  correct, including the "clear on disconnect" path.
- Endpoint-to-interface attribution (`usb.c:555-597`) and the composite-device
  driver matching (`usb.c:676-796`) — this is closer to the BSD model than the
  old first-interface-only behaviour and has no defects I could find.
- `usb_get_string()`'s two-step header-then-body read (`usb.c:362-402`) —
  correct and, notably, safer than reading 255 bytes up front.
- UHCI low-speed handling (`uhci.c:571`, `604`, `628`) — the `UHCI_TD_CTRL_LS`
  bit is set from `dev->speed` on every TD of a control transfer, correctly.
- EHCI qTD quiesce-on-timeout (`ehci.c:197-223`) — stops the schedule and waits
  for `ASS` to clear before reclaiming descriptors, which is the right
  ordering.
- xHCI slot teardown (`xhci.c:335-378`) — frees stream contexts, per-stream
  rings and per-DCI rings, not just EP0's.

## Suggested order of work

1. **USB-03 + USB-04** (retry loop and the pre-SET_ADDRESS reset). Small,
   self-contained, no new infrastructure, and the most likely cure for the
   C460's enumeration failures.
2. **USB-05** (stack disclosure). One-line clamp.
3. **USB-07** (address leak). One-line fix.
4. **USB-08** (usbfs IN transfers return no data). Needs a byte count out of
   the transfer layer.
5. **USB-09** (per-transfer timeouts in EHCI/xHCI). Mechanical.
6. **USB-06** (mask the mult bits, plumb the count separately).
7. **USB-01 + USB-02** (route string, TT fields, EHCI split transactions).
   These are the large ones — they need the core to track each device's
   parent hub address, its port on that hub, and the nearest upstream
   high-speed hub, none of which exists today. `usb_device_t.parent` is
   already the right hook.
