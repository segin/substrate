# xHCI re-audit (pass 2) — 2026-08-07

Second audit pass over `sys/drivers/usb/xhci.c` / `xhci.h`, covering the code
that did not exist when the first audit was written.

The first pass (`docs/xhci-audit-2026-08.md`) audited the driver at commit
`89d8743ae` — 1412 lines — and its 18 findings were fixed across ten commits.
Those fixes added roughly 700 lines; the driver is now 1948 lines.  **None of
that new code had been reviewed.**  This pass audits it.

Same method and same ground truth as pass 1:

* **FreeBSD 14.4-RELEASE** — `sys/dev/usb/controller/xhci.c`, `xhci.h`,
  `xhcireg.h`
* **NetBSD-current** — `sys/dev/usb/xhci.c`, `xhcireg.h`
* Normative statements cited from **xHCI Revision 1.2** (Intel, May 2019).

No other operating system's source was consulted.

Scope is the diff `89d8743ae..HEAD` over `sys/drivers/usb/`: the nine new or
rewritten functions (`xhci_trb_commit`, `xhci_abort_command`,
`xhci_set_tr_dequeue`, `xhci_stop_ep`, `xhci_port_gone`, `xhci_do_reset`,
`xhci_drain_events`, `xhci_parse_protocols`, `xhci_alloc_scratchpad`), the
dynamic-slot conversion, and the header additions.

**Four of the six findings are in code written during the previous pass.  One
of them (R-01) substantially undermines the fix it belongs to.**

---

## Status

**All six findings are fixed** as of 2026-08-08, in three commits alongside
pass 3's:

| ID | Commit |
|----|--------|
| R-01 | `0f022fe1b` — xhci: consume the command ring abort's own completion events |
| R-02 | `33d2f2155` — xhci: close the two gaps pass 2 found in pass 1's fixes |
| R-03 | `0e3dbef9d` — xhci: clamp nports, commit the abort's link TRB, drain on every lock |
| R-04 | `0e3dbef9d` — as above |
| R-05 | `0e3dbef9d` — as above |
| R-06 | `33d2f2155` — as R-02 |

They sat open for a day: this document's closing section is a suggested order,
not a completion record, and nothing acted on it until the pass-3 audit
(`docs/xhci-audit-2026-08-pass3.md`) noticed that the last commit to touch
`xhci.c` predated the commit that added this file.

R-01 was fixed with both remedies the entry suggests, since they cover
different entry points: `xhci_abort_command()` now drains to the Command Ring
Stopped event, *and* `xhci_run_command_st()` skips completion codes 0x18/0x19
rather than returning them.  Verified by boot only — as the entry notes, a
healthy emulated boot never times out a command, so the abort path does not
run.

---

## Summary

| ID | Severity | Area | One-line |
|----|----------|------|----------|
| R-01 | **High** | command ring | Command Abort's own completion events are never consumed, so the *next* command reads one as its result |
| R-02 | Low | transfers | X-16 missed the isochronous branch — `xfer->status` still stale there |
| R-03 | Low | init | `nports` never clamped to `USB_MAX_ROOT_PORTS`; two arrays silently disagree with `hcd.nports` |
| R-04 | Low | rings | `xhci_abort_command()` rewrites the link TRB bypassing `xhci_trb_commit()` |
| R-05 | Low | event ring | Only `xhci_submit()` drains; three other paths drive the controller without it |
| R-06 | Low | slots | `xhci_slot_for()` NULL-guards `enum_slot` but not `addr_slot` |

---

## R-01 — High — Command Abort's own completion events are never consumed

**`sys/drivers/usb/xhci.c:250-276` (`xhci_abort_command`), `:279-301`
(`xhci_run_command_st`)**

X-04 added `xhci_abort_command()` so that a timed-out command could not
desynchronise the command ring.  It sets `CRCR.CA`, waits for `CRR` to clear,
rewinds the ring and republishes `CRCR`.  What it does **not** do is consume
the events the abort itself produces.

xHCI 1.2 §4.6.1.2, *Aborting a Command*:

> Aborting a command on the Command Ring shall perform the following
> operations:
> * If a command is currently executing:
>   * A Command Completion Event shall be generated for the aborted command
>     with its Completion Code set to **Command Aborted**.
>   * Advance the Command Ring Dequeue Pointer to point to the next Command TRB.
> * **Generate a Command Completion Event with the Completion Code set to
>   Command Ring Stopped** and the Command TRB Pointer set to the current value
>   of the Command Ring Dequeue Pointer.

So an abort leaves one or two Command Completion Events on the event ring.
`xhci_run_command_st()` matches purely on TRB type:

```c
if (XHCI_TRB_GET_TYPE(ec) == TRB_CMD_COMPLETE) {
    if (out_slot) *out_slot = XHCI_TRB_GET_SLOT(ec);
    return cc;
}
```

The next command issued after an abort therefore consumes the *Command Ring
Stopped* event as though it were its own completion, and returns completion
code 0x18 (or 0x19 for Command Aborted) instead of its real result.  Every
caller treats anything other than `XHCI_CC_SUCCESS` as failure, so:

* the command that follows a timeout reports a spurious failure;
* its real completion event is still queued, so the command after *that* is
  matched to the wrong request too;
* the ring stays one event out of step until something else drains it.

Which is precisely the desynchronisation X-04 exists to prevent — reintroduced
by X-04's own recovery path, one command later.

Both references define the code for exactly this reason:

```c
/* FreeBSD sys/dev/usb/controller/xhci.h:340 */
#define	XHCI_TRB_ERROR_CMD_RING_STOP	0x18
/* NetBSD sys/dev/usb/xhcireg.h:527 */
#define XHCI_TRB_ERROR_CMD_RING_STOP    0x18
```

**Fix.**  Either drain to the Command Ring Stopped event at the end of
`xhci_abort_command()` (bounded, discarding a Command Aborted on the way), or
teach `xhci_run_command_st()` to treat completion codes 0x18 and 0x19 as
"not mine, keep scanning".  The first is closer to the spec's model; the second
is more robust if an abort is ever issued from elsewhere.  Doing both is
cheap.

Note this is invisible under emulation for the same reason X-04 was: nothing
in a healthy QEMU boot ever times out a command, so the abort path never runs.
Fault injection would show it — force a command timeout the way X-03 was
verified, then watch the next command's completion code.

---

## R-02 — Low — X-16 missed the isochronous branch

**`sys/drivers/usb/xhci.c`, `xhci_submit()`**

X-16 fixed stale `xfer->status` by defaulting it to `USB_XFER_ERROR` at the top
of `xhci_control()` and `xhci_bulk()`.  The dispatch has a third arm:

```c
else
    /* ... isochronous is not implemented here ... */
    ret = USB_XFER_ERROR;
```

An isochronous transfer enters neither function, so it takes the one path X-16
does not cover and leaves `xfer->status` holding whatever the previous transfer
put there.  It is the same defect X-16 was written to remove, in the single
branch the fix stepped over.

**Fix.**  Set `xfer->status = USB_XFER_ERROR` in that arm, or once in
`xhci_submit()` before the dispatch.

---

## R-03 — Low — `nports` is never clamped to `USB_MAX_ROOT_PORTS`

**`sys/drivers/usb/xhci.c:1827-1831`; `sys/drivers/usb/usb.h:187`**

```c
hc->nports = XHCI_HCS1_MAXPORTS(hcs1);   /* 8-bit field: 0..255 */
...
if (hc->nports == 0) hc->nports = 1;
```

`USB_MAX_ROOT_PORTS` is 128, and it sizes both the new `port_major[]` array in
this driver and `usb_hcd_t.enum_fail[]` in the core.  `hcd.nports` is set from
the unclamped value.

There is **no overflow** — every index into either array is guarded
(`xhci.c:1549`, `:539`, and `usb.c`'s `fails` pointer).  I checked each one.
The consequence is quieter: on a controller reporting more than 128 ports,
ports 129 and above get no protocol classification (so their speed decode falls
back to the default table, undoing X-10 for them) and no enumeration-failure
tracking (so the "park after repeated failures" logic never engages).

No such controller is likely to exist.  But the driver currently carries three
different notions of how many ports there are, and the guards are what stands
between that and an out-of-bounds write.

**Fix.**  Clamp at attach with a message when it bites, so `hcd.nports`,
`port_major[]` and `enum_fail[]` all agree.

---

## R-04 — Low — `xhci_abort_command()` bypasses the ring-write discipline

**`sys/drivers/usb/xhci.c:268-272`**

X-11 established that every write handing a TRB to the controller goes through
`xhci_trb_commit()`, which brackets the control-word store with compiler
barriers.  `xhci_ring_alloc()` lays down the link TRB that way.  The rewind in
`xhci_abort_command()` does not:

```c
link->param = hc->cmd_ring.dma;
link->control = XHCI_TRB_TYPE(TRB_LINK) | XHCI_TRB_TC;
```

Harmless as written — the controller is stopped, `CRR` is clear, and `CRCR` is
republished afterwards, so there is no handoff to order.  But it is the one
ring write in the driver that sidesteps the rule, and an exception with no
comment explaining why is how the rule stops being one.

**Fix.**  Use `xhci_trb_commit(link, ...)`, or state in a comment why this site
does not need to.

---

## R-05 — Low — only `xhci_submit()` drains the event ring

**`sys/drivers/usb/xhci.c:504, 974, 1023, 1274`**

Four functions take `submit_lock` and drive the controller: `xhci_port_gone()`,
`xhci_set_hub()`, `xhci_set_ep0_mps()` and `xhci_submit()`.  X-08 added the
event drain to the last of them only.

This is not a command-matching problem — `xhci_run_command_st()` skips events
that are not Command Completions, so accumulated Port Status Change events are
stepped over.  It is a coverage gap in the ring-full protection X-08 exists to
provide: three of the four entry points can run a full command sequence against
an event ring that is already brim-full, which is the state X-08 was added to
prevent reaching.

(With R-01 unfixed these three are also the paths most likely to meet a stale
Command Completion, since none of them clears one first.)

**Fix.**  Drain once at the top of each, or factor the lock-plus-drain into a
small helper the four share.

---

## R-06 — Low — `xhci_slot_for()` guards one branch and not the other

**`sys/drivers/usb/xhci.c`, `xhci_slot_for()`**

```c
if (addr != 0 && hc->addr_slot[addr])
    return hc->addr_slot[addr];          /* no hc->slots[] NULL check */
if (hc->enum_slot &&
    hc->slots[hc->enum_slot] &&          /* checked here */
    hc->slots[hc->enum_slot]->port == usb_root_port(xfer->dev))
    return hc->enum_slot;
```

Since X-14 made slot state heap-allocated, `hc->slots[n]` can be NULL.  The
callers dereference the returned slot id immediately —
`&hc->slots[slot]->ep_ring[1]` in `xhci_control()`, `hc->slots[slot]` in
`xhci_bulk()` — so a stale `addr_slot` entry is a NULL dereference.

I traced every `xhci_free_slot()` caller: the two in `xhci_setup_slot()` free
slots that were never entered into `addr_slot`, and `xhci_port_gone()` clears
`addr_slot` and `enum_slot` before freeing.  **The invariant holds today.**

It is unenforced, though, and one new caller of `xhci_free_slot()` that forgets
the bookkeeping turns it into a page fault in the transfer path.  That the
`enum_slot` branch three lines below *is* guarded makes the omission read as
deliberate when it is not.

**Fix.**  Guard both branches identically.

---

## Checked in the new code and found correct

Recorded so this ground is not re-covered:

* **Scratchpad** — allocation and free sizes agree; both failure exits leave
  `xhci_teardown()` able to free exactly what was allocated; the PAGESIZE
  alignment check happens before the buffer is published in `DCBAA[0]`; the
  split `SPB_MAX` field is composed correctly.
* **`XHCI_PORT_CLEAR`** — preserves PP, PIC and the wake enables while clearing
  PED, PR, PLS, LWS, the change bits and WPR; matches both BSDs' `XHCI_PS_CLEAR`
  exactly.
* **`xhci_do_reset()`** — acknowledges PRC/WRC without writing PED back; the
  warm-reset retry re-reads and re-masks rather than reusing a stale value; WPR
  is inside the clear mask so it is set explicitly rather than inherited.
* **`xhci_parse_protocols()`** — bounds each block against both `nports` and
  `USB_MAX_ROOT_PORTS` before writing; requires dword 1 to read `"USB "` so a
  vendor block cannot be mistaken for a protocol block; the `off + 16` bound
  covers all three dwords read.
* **`xhci_drain_events()`** — bounded by the ring size, and tests the cycle bit
  directly rather than calling `xhci_wait_event()` with a zero timeout (which
  would spin to the next millisecond tick).
* **Dynamic slots** — `xhci_free_slot()` NULLs the table entry before `kfree`,
  and every `hc->slots[]` dereference other than R-06's is guarded.
* **`xhci_stop_ep()` / `xhci_set_tr_dequeue()`** — the factoring out of the
  shared tail is behaviour-preserving; no recursion is possible, since
  `xhci_abort_command()` does not itself issue commands.
* **Interval encoding** — re-verified numerically against the spec formula
  across `bInterval` 1..255.

---

## Suggested order

1. **R-01** — it is the only one that changes behaviour on real hardware, and
   it partly negates X-04.  Worth fixing and then fault-injecting a command
   timeout to confirm the following command still reports its own result.
2. **R-02**, **R-06** — one line each, and both are gaps in fixes that are
   otherwise complete.
3. **R-03**, **R-04**, **R-05** — consistency and robustness; no known trigger.
