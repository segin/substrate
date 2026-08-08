# xHCI re-audit (pass 4) — 2026-08-08

Fourth pass over `sys/drivers/usb/xhci.c` / `xhci.h`.

Unlike pass 3, this one is **spec-first rather than differential**: it audits
the endpoint recovery path against the xHCI 1.2 endpoint state machine
(§4.8.3) and the command preconditions in §4.6.8 / §4.6.9, without reference to
what the BSDs happen to do.  That was deliberate — pass 3 established that the
port and link-state areas hold no divergences from FreeBSD or NetBSD, so the
remaining defects are the ones the references would not reveal.

* **xHCI Revision 1.2** (Intel, May 2019) — the normative source throughout.
* FreeBSD 14.4-RELEASE and NetBSD-current consulted only to confirm the
  completion-code constants.

## Status

**All three findings are fixed** as of 2026-08-08 in one commit, together with
the removal of `xhci_stop_ep()` that they made redundant.

| ID | Severity | One-line |
|----|----------|----------|
| P4-01 | **High** | Endpoint recovery dispatched on the completion code, not the endpoint's actual state, so it issued commands the spec rejects |
| P4-02 | **High** | The Error state (TRB Error) was never recovered from at all |
| P4-03 | Medium | Four of the seven completion codes that halt an endpoint were missing from `XHCI_CC_HALTS_EP` |

---

## P4-01 — High — recovery dispatched on the completion code, not the endpoint state

**`sys/drivers/usb/xhci.c`, `xhci_control()` and `xhci_bulk()` error paths**

Both error paths chose the recovery command by inspecting the completion code
that surfaced the failure:

```c
if (XHCI_CC_HALTS_EP(cc))
    (void)xhci_recover_ep(...);   /* Reset Endpoint + Set TR Dequeue */
else if (cc == 0)
    (void)xhci_stop_ep(...);      /* Stop Endpoint + Set TR Dequeue */
```

The completion code is a hint about what happened.  The authority on what the
endpoint *needs* is its EP State field in the output endpoint context, and the
state machine gives a different answer for each state — with the wrong command
not merely failing, but failing in a way that takes the Set TR Dequeue Pointer
after it down too, since both helpers return early:

> §4.6.8: "If the endpoint is not in the Halted state when a Reset Endpoint
> Command is executed: the xHC shall reject the command and generate a Command
> Completion Event with the Completion Code set to **Context State Error**."

> §4.8.3: "A **Stop Endpoint** Command received while an endpoint is in the
> **Halted** state shall have no effect and shall generate a Command Completion
> Event with the Completion Code set to **Context State Error**." — and the
> same sentence appears again for the Error state.

So each helper is rejected in precisely the states the other one is for, and a
rejection means the dequeue pointer is never moved past the dead TD.  The
controller then re-runs that TD the moment the doorbell rings, which is the
exact failure X-03 added this machinery to prevent.

**Demonstrated, not just reasoned.**  A fault injected into `xhci_bulk()` —
forcing one successful bulk transfer to report `XHCI_CC_STALL` — on a
root-on-USB-MSC boot:

```
XHCIFAULT: injecting STALL on slot 1 dci 4
XHCIFAULT: recover slot 1 dci 4: ep state=1
```

State 1 is **Running**.  A STALL completion code, and the endpoint is not
Halted at all.  The old code would have read `XHCI_CC_HALTS_EP(STALL)` as true,
issued Reset Endpoint into a Running endpoint, taken Context State Error, and
skipped the dequeue update.

**Fix.**  `xhci_recover_ep()` now reads the state and dispatches on it —
Halted → Reset Endpoint, Running → Stop Endpoint, Error/Stopped → nothing,
Disabled → give up — and every path that does anything ends at Set TR Dequeue
Pointer.  `xhci_stop_ep()` is gone; it was the Running branch of this function.

---

## P4-02 — High — the Error state was never recovered from

**`sys/drivers/usb/xhci.h`, `XHCI_CC_HALTS_EP`**

A TRB Error (completion code 5) does not halt an endpoint.  §4.8.3:

> "A TRB Error condition should cause a Running Endpoint to transition to the
> **Error** state.  A **Set TR Dequeue Pointer Command** shall be used to
> transition the endpoint to the Stopped state."

`XHCI_CC_TRB_ERROR` is not in `XHCI_CC_HALTS_EP`, and it is not zero, so the
old dispatch above did **neither** branch: nothing at all happened.  The
endpoint was left in the Error state, where by definition it "is not running",
and every subsequent transfer on it timed out forever — the same
endpoint-killed-for-the-session failure the halt handling exists to prevent,
just reached through a different door and never noticed because a healthy
emulated boot produces no TRB Errors.

Note also that Reset Endpoint — the instinctive fix — is exactly the wrong
command here: §4.6.8 rejects it unless the endpoint is Halted.  Set TR Dequeue
alone is the documented recovery, which is what the new state dispatch does.

---

## P4-03 — Medium — four halting completion codes were missing

**`sys/drivers/usb/xhci.h`, `XHCI_CC_HALTS_EP`**

The macro listed Babble, USB Transaction Error and Stall.  §4.8.3 gives the
full set:

> "A Halt condition, e.g. a Stall Error, **Invalid Stream Type Error**,
> **Invalid Stream ID Error**, Babble Detected Error, **Event Lost Error**, USB
> Transaction Error, or a **Split Transaction Error** detected on a USB pipe
> shall cause a Running Endpoint to transition to the Halted state."

Missing were Invalid Stream Type (10), Invalid Stream ID (34), Event Lost (32)
and Split Transaction (36).  Two of those are reachable in code this driver
already has: the stream-context path exists for UAS, and Split Transaction
Error is a USB2-behind-a-hub condition, which is the topology pass 3 spent its
two High findings on.

Less severe than it looks now that P4-01 is fixed — the state dispatch would
recover such an endpoint correctly regardless of what the macro says, because
it reads the state rather than trusting the code.  The macro is still worth
correcting: it is the driver's statement of which errors are recoverable, and
being wrong about that invites the next reader to reintroduce code-based
dispatch.

---

## Checked in this pass and found correct

**Set TR Dequeue Pointer preconditions.**  §4.8.3 permits it in Stopped, and
requires it for the Error → Stopped transition; both are branches the new
dispatch reaches it from.  The Dequeue Cycle State in bit 0 continues to be
taken from the ring's producer cycle, which is what makes the restart land on
the right TRB.

**Halt detection is not needed on the command ring.**  Endpoint states apply to
transfer rings; command-ring recovery is the separate Command Abort path, fixed
under R-01.

**Recovery after a timeout still quiesces.**  A timeout leaves the endpoint
Running, so the new dispatch issues Stop Endpoint — the same command the old
`cc == 0` branch did, now reached by observing the state rather than inferring
it.  The fault-injection boot shows this firing repeatedly and harmlessly on an
idle HID interrupt endpoint, which is pre-existing behaviour (each idle poll
that times out costs a Stop + Set TR Dequeue) and not a regression.

**Isochronous endpoints.**  The iso path added in `01287f3a7` never waits on a
transfer event and so never enters this recovery; Ring Underrun and Ring
Overrun are collected by `xhci_drain_events()` instead.  Missed Service Error
(23) is an iso condition that does not halt the endpoint and needs no action
here.

---

## Verification

Fault injection, then removal and re-verification, the method pass 1 used for
X-02 and X-03:

1. A forced `XHCI_CC_STALL` on the first successful bulk transfer plus a trace
   of the observed EP state, booted root-on-USB-MSC over `qemu-xhci`.  The
   trace shows the injected STALL landing on a Running endpoint, no
   `reset endpoint failed` / `stop endpoint failed` / `set TR dequeue failed`
   messages, and the boot reaching multi-user off that same USB disk — i.e. the
   endpoint was recovered and kept working.
2. Injection removed, clean rebuild, same boot repeated: no trace output, no
   xHCI failures, root mounted, multi-user reached.
