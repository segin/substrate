# Intel HD Audio driver: final audit report

**Scope:** `sys/drivers/audio/hda.c`, plus the framework paths it relies on: `sys/drivers/audio/audio.c`, `sys/drivers/audio/oss.c` and `sys/drivers/audio/audio_fifo.h`.

**Method:** each finding below survived at least 2 of 3 independent adversarial verifications. Its severity is the most defensible value from the verifier votes. Where verification corrected part of a claim, the corrected version is the one stated here. All cited line numbers were re-checked against the current tree.

**Merged duplicates:**
- Three reports of the ring-counter wrap became H-1.
- Three reports of the halt-retire SDnSTS omission became H-17.
- Four reports about the writer sleep/wake protocol became H-10.
- Two FIFO-reset race reports became H-8.
- Two partial-detach leak reports became H-43.
- Two INTx-proof reports became H-42.
- Two AMD/ATI snoop reports became H-45.
- Two prebuffer start-policy reports became H-6.

## Summary

| Severity | Count |
|---|---|
| Critical | 0 |
| High | 2 |
| Medium | 14 |
| Low | 30 |
| **Total** | **46** |

The only critical claim was a kernel heap overflow from the FIFO-reset race. It was downgraded: every write path hands the backend at most 4 KiB per call, because the write syscall copies through a 4096-byte bounce buffer. That is far below the 256 KiB FIFO, so the overflow cannot happen. The race itself remains a medium finding (H-8).

---

## High

### H-1: Ring accounting wraps when the ring drains with a partial chunk left in the FIFO; the feeder wedges permanently
**Location:** `sys/drivers/audio/hda.c:1637` (also 1653, 1660, 1781, 1832-1847)

**What is wrong**
- `hda_feed()` computes `in_flight = writes_queued - slots_played` as an unsigned value and stops once it reaches 31.
- The completion (BCIS) handler increments `slots_played` on every completion, without condition.
- The only clamp is the park branch (`slots_played = writes_queued; halt_pending = 1`). It runs only when `audio_fifo_used() == 0`.
- Outside drain, only whole 4096-byte chunks are staged. Any write total that is not a multiple of 4 KiB leaves 1-4095 bytes in the FIFO.
- If the producer then pauses longer than the ring holds (about 0.7 s at 48 kHz stereo S16), the next completion of an unfilled slot makes `slots_played` exceed `writes_queued`. `in_flight` wraps to about 0xFFFFFFFF, and every later `hda_feed()` stops at once. That includes the interrupt-handler feed, `hda_kick()`, and the flush-tail feed in drain.
- `running` stays 1 and `halt_pending` stays 0, so nothing ever restarts or resets the counters.

**Correct hardware behaviour**
- With LVI=31, CBL = 32×4096 and IOC set on every BDL entry, an output stream raises SDnSTS.BCIS for every descriptor it fetches while RUN=1, whether or not software refilled that buffer (spec 3.3.36-3.3.39, 4.5).
- Host free/used accounting must therefore be based on the engine's real position (SDnLPIB, or the DMA position buffer), or must at least be clamped on every completion, whatever the software FIFO holds.

**Failure scenario** (QEMU intel-hda and the ALC255 machines alike)
1. A player writes 4608-byte MPEG frames, the user pauses for about a second, then resumes.
2. Silence plays from then on.
3. The writer fills the 256 KiB FIFO and blocks, woken about every 21 ms and blocked again.
4. In `hda_drain()`, the negative signed `in_flight`, cast to bytes, keeps "remaining" falling, so the 1.5 s stall detector never fires. `close()` blocks until the 60 s `HDA_DRAIN_POLL_MAX` ceiling.
5. Only close, flush, or a format change recovers.

**Fix sketch**
- Make `in_flight` signed (`int32_t`) everywhere.
- In the BCIS handler, after `hda_feed()`, run:
  ```c
  if ((int32_t)(writes_queued - slots_played) <= 0) {
      slots_played = writes_queued;
      halt_pending = 1;
  }
  ```
  regardless of the FIFO residue. After the halt is retired, `hda_kick()` re-stages the residue from slot 0.
- Better: read SDnLPIB (or enable DPLBASE) and compute the engine's current slot directly.

### H-2: 64-bit BAR0 above 4 GiB is truncated and the wrong physical page is mapped
**Location:** `sys/drivers/audio/hda.c:2571` (base from `sys/kern/pci.c` `pci_bar_base()`)

**What is wrong**
- `pci_iomap(pdev, 0, 16384)` takes its base from `pci_bar_base()`, which returns a `uintptr_t` and silently drops bits 63:32 on i386.
- Nothing rejects the truncated address, because RAM is not registered in the iomem resource tree.
- Memory decode is also enabled (line 2562) before the BAR is checked.
- The xHCI and EHCI drivers already relocate such BARs with `pci_relocate_bar32()`. This driver does not.

**Correct hardware behaviour**
- HDBARL/HDBARU (config 0x10/0x14) form a 64-bit memory BAR on every Intel PCH HD Audio function since ICH6, and on discrete-GPU HDMI audio functions.
- A kernel without PAE must move the BAR into the 32-bit hole while memory decode is off, verify the new base by readback, or refuse to attach. It must never map the truncated low dword.

**Failure scenario**
- UEFI firmware places the window at, for example, 0x6001110000.
- The driver maps physical 0x01110000, which is ordinary RAM.
- `hda_quiesce()` and the CRST sequence then write SDnCTL/SDnSTS bytes for up to 60 "streams", plus CORBCTL, RIRBCTL, INTCTL, WAKEEN, DPLBASE/DPUBASE and GCTL, into live kernel or user pages. The result is silent memory corruption at boot.

**Fix sketch**
- With decode off, call `pci_bar_base64(pdev, 0)`. If `>> 32` is non-zero, call `pci_relocate_bar32(pdev, 0)` and bail out on failure.
- Only then enable `PCI_COMMAND_MEMORY|MASTER` and call `pci_iomap`.
- Separately, make `pci_bar_base()`/`pci_iomap()` refuse any BAR whose upper dword is non-zero, to protect every caller.

---

## Medium

### H-3: Intel controllers assumed to snoop; DEVC.NSNPEN is never cleared
**Location:** `sys/drivers/audio/hda.c:2502` (early return at 2523)

**What is wrong**
- The snoop table treats vendor 0x8086 as "coherent without help".
- CORB, RIRB, BDL and the sample chunks come from `dma_alloc_coherent()`, which is ordinary write-back memory, and the driver flushes nothing.
- Config offset 0x78 is never read or written.

**Correct hardware behaviour**
- The Intel HDA function's Device Control register DEVC (0x78) has bit 11, NSNPEN. When set, the controller may set the No Snoop attribute on its bus-master requests.
- The 100-series and 300-series PCH datasheets give the DEVC default as 2800h (bit 11 set). The bit is not affected by D3hot→D0 or FLR, and only platform reset restores the default.
- A driver that uses cacheable buffers must clear the bit before any DMA is programmed.

**Failure scenario**
- On 9d70, 9dc8 or 8c20, if firmware leaves NSNPEN at its default, the stream engine reads cache-stale sample data. Playback is audibly corrupted ("static"), and stale descriptor fetches are also possible.
- The attaching 9d70 shows CORB/RIRB traffic surviving, which is consistent with the main effect landing on stream data.

**Fix sketch**
For vendor 0x8086, next to the TCSEL write:
```c
v = pci_read_config16(...,0x78);
v &= ~0x0800;
pci_write_config16(...,0x78,v);
```
Then read it back and warn if bit 11 is still set. This is harmless on QEMU, where the register reads as zero.

### H-4: A late verb response after a timeout is credited to the next verb, and the rings are never resynchronised
**Location:** `sys/drivers/audio/hda.c:745`

**What is wrong**
- On timeout, `hda_send_verb_locked()` returns `-EIO`. It leaves the CORB entry queued and `rirb_rp` unchanged, and it keeps no per-codec outstanding-command count.
- If the codec answers later, the next verb (same codec address) consumes that stale entry, and every later response is one verb behind for the rest of the boot.
- RIRBSTS.RIRBOIS is acknowledged but never acted on.

**Correct hardware behaviour**
- Solicited responses are strictly in order and carry no sequence tag; only the codec-address field identifies them (4.4.1/4.4.2). The spec puts no upper bound on response latency.
- The host must keep an outstanding count per codec and discard any solicited response that arrives while the count is 0. Alternatively, after a timeout it must stop CORB/RIRB, reset CORBRP (bit 15 set then cleared, each verified) and RIRBWP (bit 15), clear RIRBSTS and restart.
- The wait should be bounded in time (tens of ms), not by a count of MMIO reads.

**Failure scenario**
- One slow GET_PARAMETER during the ~200-verb graph walk (for example right after SET_POWER_STATE D0) shifts every later response.
- Widget types, connection lists and amp capabilities are attributed to the wrong nodes, so the DAC/pin pairing is wrong.
- The attach line still looks healthy and playback is silent. The only clue is "N verb timeout(s)".

**Fix sketch**
- Add `pending[16]`: increment on enqueue, decrement on each matching solicited response, discard when `pending <= 0`.
- On timeout, zero `pending[cad]` and run the CORB/RIRB pointer-reset sequence.
- Bound the wait at about 50-100 ms of wall-clock time (see H-40).

### H-5: Attach is abandoned if the lowest STATESTS codec does not answer
**Location:** `sys/drivers/audio/hda.c:2734`

**What is wrong**
- The first VENDOR_ID probe goes only to the lowest set STATESTS bit.
- A timeout there tears down the whole controller.
- `hda_codec_configure()`, which already tolerates silent codecs at higher addresses, is never reached.

**Correct hardware behaviour**
- STATESTS (3.3.9) records which SDI lines signalled during link enumeration. It does not promise that each codec will answer verbs later. Display codecs fed by a powered-down graphics well, and phantom latched slots, are both known cases.
- Each address should be probed separately. Non-responders are logged and skipped, and attach fails only if none answer.

**Failure scenario**
- A silent or phantom slot sits below the analog codec. The controller is torn down and there is no `/dev/audio`, although the analog codec works.
- None of the listed boards has this layout, so the defect is latent.

**Fix sketch**
- Loop over `codec_mask` with `hda_try_verb(VENDOR_ID)` and clear the bits that time out.
- Return `-ENXIO` only if the mask ends up empty.
- Set `codec_addr` to the first codec that answered, and aim the interrupt-proof verb at it.

### H-6: Stream start policy: short writes with a residue never start, chunk-aligned writes start and restart with one slot
**Location:** `sys/drivers/audio/hda.c:1728-1729`

**What is wrong**
RUN is set only if `in_flight >= 8 || (in_flight > 0 && fifo_used == 0)`.
- **(a)** A write of less than 32 KiB that is not chunk-aligned (for example 10 KiB: two slots plus 2 KiB) meets neither condition. Nothing else arms RUN, because SNDCTL_DSP_POST is a no-op (`oss.c:347`), so the audio waits until 8 slots accumulate or the device is closed. The framework's conversion path almost never produces 4 KiB multiples.
- **(b)** A chunk-aligned write leaves the FIFO empty, so the engine starts with a single 21 ms slot.
- **(c)** After every underrun, the halt retire restarts through stop and SRST with only the slot that write supplied. The prebuffer is never rebuilt, and a producer paced at real time loops through stop/reset/restart, which is audible.

**Correct hardware behaviour**
- The engine needs no minimum fill; it plays whatever the BDL describes. The prebuffer is a latency policy.
- Staged data must start within a bounded time once the producer stops, padding the residue with silence as drain already does.
- A restart after an underrun should wait for a real cushion unless the stream has explicitly ended.

**Failure scenarios**
- A UI or game writes a 10 KiB click and keeps the fd open: nothing plays until about 32 KiB has queued, and then all the effects play back to back, seconds late.
- `cat` or a 4 KiB-fragment player on the ALC255 machine: every scheduling or disk stall longer than 21 ms causes a gap plus a reset click.

**Fix sketch**
- Restrict the empty-FIFO clause to the drain path, using a `flush` flag.
- Add a deferred task armed by `hda_write()`: if no write has arrived for about 20-40 ms and the engine is stopped with data staged or a residue present, pad the residue and start.
- Make SNDCTL_DSP_POST call the same "pad + start" backend op.
- After a halt retire, require `HDA_PREBUFFER_SLOTS` before restarting, unless the deferred task fires.

### H-7: Any later `/dev/dsp` open reprograms and discards a stream owned by another process
**Location:** `sys/drivers/audio/oss.c:420`

**What is wrong**
- `oss_node_open()` applies 8 kHz/U8/mono through `audio_apply_info()` on every open.
- `audio_negotiate()` then calls `hda_set_params()` with no `play_owner` check.
- If the negotiated SDnFMT differs from the running one (the ALC255 refuses 8 kHz and falls back to 48 kHz stereo), the driver clears RUN, discards the FIFO and ring, writes SDnFMT and rebinds the converter.
- `dev->current` becomes 8 kHz U8 mono, so the owner's next 16-bit stereo bytes are decoded as 8-bit mono and resampled.
- AUDIO_SETINFO on `/dev/audioctlN` reaches the same path.

**Correct hardware behaviour**
- SDnFMT and the converter's format/stream binding belong to whoever drives the stream, and may change only at a stream boundary that owner chose.
- Defaults for a new open must not touch an engine that is in use.

**Failure scenario**
- A plays 44.1 kHz music. A mixer or a second player opens `/dev/dsp`, whose writes would later fail with EBUSY anyway.
- About a second of queued audio is lost, then loud garbage plays until A sets its format again.

**Fix sketch**
- Apply OSS defaults only when `open_refs == 1`, or keep format state per open and apply it when that open first becomes `play_owner`.
- Have `audio_apply_info()` return `-EBUSY` when `play_owner` is set to another owner and the stream is running.

### H-8: FIFO reset in flush/set_params/close races the unlocked producer and corrupts `head`
**Location:** `sys/drivers/audio/hda.c:2159`, `hda.c:2335`, producer at `hda.c:2200`, `audio_fifo.h:62-85`

**What is wrong**
- `audio_fifo_write()` snapshots `head`, does a memcpy, then publishes `head + n`, without holding `feed_lock`.
- `audio_fifo_reset()` zeroes `head` and `tail` under `feed_lock`, which only excludes the interrupt-side consumer.
- The kernel preempts in kernel mode. If the reset lands between the producer's snapshot and its publish, `head` becomes a large stale count with `tail = 0`. `used` then exceeds `cap`, and `audio_fifo_free()` wraps.

**Correction from verification**
- The heap overflow is not reachable. The write syscall copies through a 4096-byte bounce buffer, the conversion path hands over at most 4096 bytes, and no other caller exceeds 64 KiB, so both memcpys stay inside the 256 KiB buffer.
- The real effects:
  - flow control is lost and the writer laps unplayed audio;
  - the feeder plays stale or overwritten ring contents;
  - GETOSPACE reports nonsense;
  - drain or close may run to the 60 s ceiling.

**Correct behaviour:** discarding queued PCM must be serialized against an append in progress, so that `0 <= head - tail <= cap` always holds.

**Failure scenario:** a sibling thread issues AUDIO_FLUSH or SNDCTL_DSP_RESET (or another process issues `audioctl -w play.rate=…`) while the player is preempted inside the copy.

**Fix sketch**
- Implement the reset as a consumer-side discard, `tail = head` under `feed_lock`, and never write `head` outside the producer. A late publish then adds at most one in-flight chunk instead of corrupting the indices.
- Optionally add a generation counter so the producer drops the chunk it just published if a reset happened during its copy.
- Clamp `audio_fifo_free()` to 0 when `used > cap` as defence in depth.

### H-9: Last-close teardown runs unserialized against a new open and its writer
**Location:** `sys/drivers/audio/hda.c:2069` (stop/reset at 2082-2098); `audio.c` `audio_node_close()`

**What is wrong**
- `audio_node_close()` drops `open_refs` to 0 and clears `play_owner`, releases the lock, then calls `hda_close()`.
- `hda_drain()` can sleep for up to 60 s. During that time a new process can open the device and stream.
- Each drain poll pads the newcomer's residue with zeros (`hda_feed(d, 1, …)`).
- When the drain stops (typically about 1.5 s after the stall detector trips), close clears RUN, zeroes the counters and resets the FIFO under the new stream. It also hits the H-8 race against the new writer.

**Correct behaviour**
- RUN=0 with readback, SDnSTS clear and the ring/FIFO reset are valid only when no producer can be active.
- The device must stay busy until teardown finishes, or teardown must abort if a new open arrives.

**Failure scenario:** player A exits after a 3 s clip and player B starts within the drain window. B's audio is chopped, then cut, then stalls for about 250 ms.

**Fix sketch**
- Add a `closing` flag that `audio_node_open()` waits on (or answers with EBUSY), cleared after `ops->close()` returns.
- Alternatively, make `hda_close()` re-check `open_refs` after the drain and skip the reset.

### H-10: Writer wait loop violates the sleep-queue protocol (lost wakeups, stranded BLOCKED state, possible use-after-free)
**Location:** `sys/drivers/audio/hda.c:2217-2223`; stop paths at 2153/2159 (`set_params`), 2335 (`flush`), 1870 (DESE)

**What is wrong**
- **(a) No self-dequeue.** `hda_write()` never calls `sleepq_remove_thread()` after `sched_sleep()`. When the ~250 ms fallback deadline fires, the thread is still linked. The next `sleepq_add()` appends it to itself (`sq_tail->next == t`, count 2). A later signal-driven removal then leaves `sq_head = t, sq_tail = NULL`, with `wait_chan` already cleared. Exit and reaper cleanup cannot find the entry, so the next completion's `sleepq_wake_all(d)` writes into a freed `thread_t`. Reaching this needs the self-cycle followed by a signal.
- **(b) Re-block after wake.** `sched_sleep()` sets the thread BLOCKED again unconditionally. If a completion's wake lands between `sleepq_add()` and `sched_sleep()`, the writer sleeps with no queue entry until the 250 ms fallback.
- **(c) Signal race.** A signal posted between the L2214 check and L2221 (where INTERRUPTIBLE is set) leaves the writer returning to user mode with state BLOCKED, still linked on `d`. A plain kill does not cause a use-after-free, because process exit unlinks every thread. Re-blocking on another channel before the next tick does splice the two wait lists.
- **(d) Stop paths never wake the writer.** `flush`, `set_params` and the DESE branch stop the engine without calling `sleepq_wake_all(d)`. With RUN clear no BCIS will follow, so every such stop forces the 250 ms fallback, which is exactly the path that triggers (a).

**Correct behaviour**
- Mark the thread interruptible before checking the condition and pending signals, register, and yield only if still registered. On every exit, unlink yourself and restore RUNNING.
- Every path that clears RUN or discards the ring must wake producers waiting for completion-driven space, because the hardware raises no further interrupts.
- A descriptor error is fatal for the stream: the controller clears RUN.

**Failure scenario**
- A second `/dev/dsp` open (H-7) stops the engine under a blocked writer. The writer returns after 250 ms still linked, re-adds itself into a self-cycle, and ^C then corrupts the sleep queue. A later completion interrupt writes into freed memory.
- Ordinary case: a 250 ms stall after every flush, format change or DESE.

**Fix sketch**
```c
current_thread->flags |= THREAD_F_INTERRUPTIBLE;
sleepq_add(d, current_thread);
if (audio_fifo_free(&d->fifo) == 0 && !sigpending())
    if (current_thread->wait_chan == d)
        sched_yield();          /* with own deadline */
sleepq_remove_thread(current_thread);
current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
```
Also add `sleepq_wake_all(d)` in `hda_flush()`, `hda_set_params()`, `hda_close()` and the DESE branch.

### H-11: Stop paths leave unplayed audio in the ring chunks, and a restarted stream replays it
**Location:** `sys/drivers/audio/hda.c:2331` (also `set_params` 2150-2159, `close` 2078-2096, halt retire 1705-1713)

**What is wrong**
- Flush, format change, close after an interrupted drain, and DESE retire all reset the counters but never zero the chunk pages. Slots are zeroed only as they complete.
- The next start stages slots 0..W-1 and runs the cyclic BDL over all 32 entries.
- Once the new data runs out, the halt waits for a process-context call. Because the counters then park at the same value, the interrupt handler keeps zeroing the same slot, and slots W+1..31 are replayed in a loop.

**Correct hardware behaviour:** while RUN=1 the engine fetches every descriptor within CBL cyclically. Every buffer it can reach must contain current data or silence before RUN is set.

**Failure scenario**
- SNDCTL_DSP_RESET to skip a track, then a 12 KiB effect with the fd left open: about 600 ms of the flushed music plays, and keeps looping.
- After a format change, the same stale data comes out as a burst of noise.

**Fix sketch:** in `hda_stream_start()` (process context), `memset` every chunk from `writes_queued` to 31 before setting RUN, or zero all 32 chunks in the four reset paths.

### H-12: EAPD is asserted only on the chosen pin
**Location:** `sys/drivers/audio/hda.c:1392`

**What is wrong:** a read-modify-write of verb F0Ch/70Ch sets EAPD (bit 1) only on the selected output pin. Every other EAPD-capable pin keeps its post-reset state, which is 0 after CRST.

**Correct hardware behaviour**
- On many boards the codec's EAPD pad, which powers the external class-D amplifier, follows the EAPD bit of a different pin widget (headphone, line-out or dock), or an OR of several.
- While the function group is in use, set EAPD (keeping BTL and L/R-swap, bits 0 and 2) on every pin whose Pin Capabilities bit 16 is set.

**Failure scenario:** the speaker pin is chosen but the amplifier is gated by another pin's EAPD. The stream runs, completions arrive, and the speakers are silent.

**Fix sketch:** iterate over every pin complex in the AFG and, for each with `PINCAP_EAPD`, write `(F0C & 0x07) | 0x02` via 70Ch, after the D0 settle (H-34).

### H-13: Pin association and sequence are ignored; only one output pin is ever driven
**Location:** `sys/drivers/audio/hda.c:1285`

**What is wrong**
- Pins are ranked by Default Device alone and exactly one pin is programmed.
- Default Association (bits 7:4) and Sequence (bits 3:0) are never decoded. Association 0 (reserved) and input-type device pins are accepted as rank-0 candidates.
- After CRST, every other output pin, including the headphone pin, has pin control 0 and muted amps.
- There is no jack detection, and this codec class does not mute the speakers in hardware.

**Correct behaviour (spec 7.3.3.31)**
- Group output pins by association, ignore association 0, and order each group by sequence.
- A headphone pin with sequence 0xF in the speaker association is the headphone redirect for that output.
- Route every pin of the selected association: set OUT_EN, add HP_EN on headphone pins, and unmute. Without presence detection, drive the headphone pin from the same mixer as the speaker.

**Failure scenario**
- On the ALC255 (speaker 0x14 through mixer 0x0c), headphone pin 0x21 is never routed or enabled. Plugged-in headphones are silent and the speakers keep playing.
- A second speaker (woofer) pin in the speaker association stays dark.

**Fix sketch**
- Decode association and sequence, and pick the lowest-numbered output association that contains the best pin.
- For every pin in that association, and for every HP-out pin, route to the chosen DAC or mixer (or a second DAC), set pin control, and unmute.
- Drop association-0 pins and input device types from candidacy.

### H-14: DSP-mode Intel controllers presented as subclass 01h are never probed
**Location:** `sys/drivers/audio/hda.c:2863`

**What is wrong:** the driver matches only class 04h, subclass 03h. Vendor/device ID and prog-if are ignored.

**Correct hardware behaviour**
- With the audio DSP enabled, firmware presents the same HDA function either as 040380h or as 040100h. BAR0 is still the standard HDA register block, and the legacy bring-up drives the analog codec normally.
- Known IDs that use the 040100h form include 9d71, 9dc8, 06c8, a0c8, 51c8 and 4dc8.

**Failure scenario:** a 9dc8 (or similar) machine whose firmware chooses 040100h gets no `hda:` line and no `/dev/audio`. The AC'97 probe skips it with "missing I/O BAR".

**Fix sketch**
- Add a device-ID table for vendor 0x8086 that accepts class 04h, subclass 01h for those IDs.
- Print prog-if in the attach line so DSP mode is visible in the log.

### H-15: When no pin/DAC pair is reachable, the driver still commits an unconnected pair and stops trying other codecs
**Location:** `sys/drivers/audio/hda.c:1327` (route result discarded at 1348)

**What is wrong**
- The reachability probe searches only to depth 2 (pin → DAC, or pin → mixer/selector → DAC).
- If it fails, the driver falls back to `pins[0]`/`dacs[0]` on a "hard-wired" theory and ignores the `-ENODEV` from the commit.
- It then sets OUT_EN, EAPD, amps and the stream binding, sets `have_path = 1`, and returns 0. `hda_codec_configure()` stops iterating, and the device registers.

**Correct hardware behaviour**
- A converter reaches a pin only through connection lists. Even a single hard-wired source is listed, with Connection List Length 1 (7.3.4.11).
- The search must follow mixers and selectors recursively, with a depth bound and a visited set. If nothing reaches an output pin, the codec has no output path.

**Failure scenario:** a codec with a pin → selector → mixer → DAC path registers `/dev/audio`, accepts every write, runs DMA, and stays permanently silent. No listed target has a path this deep.

**Fix sketch**
- Replace the 2-level probe with a DFS (depth of at most 10, visited bitmap) that records the selection index at each hop.
- If no pair is found, return `-ENODEV`. Check the return value of the commit.

### H-16: Writes block on a full PCM FIFO even on O_NONBLOCK descriptors
**Location:** `sys/drivers/audio/hda.c:2217`; `oss.c:347` (SNDCTL_DSP_NONBLOCK treated as a no-op)

**What is wrong**
- `hda_write()` never checks `current_thread->io_file->f_flag & FNONBLOCK`.
- The framework has no poll hook.
- SNDCTL_DSP_NONBLOCK claims the flag is "handled at VFS", but it is not.

**Correct behaviour:** for a non-blocking descriptor, copy what fits and return the short count, or `-EAGAIN` when nothing fits.

**Failure scenario:** an event-loop player that writes more than the free space stalls for about 0.34 s per 64 KiB, and up to about 1.4 s for a full FIFO, on every write.

**Fix sketch**
- Check FNONBLOCK before sleeping and return `total_consumed ? total_consumed : -EAGAIN`.
- Make SNDCTL_DSP_NONBLOCK set FNONBLOCK on the file.
- Add a poll op that wakes on BCIS.
- The conversion path must keep any unaccepted converted bytes (H-30) before this can safely return short counts.

---

## Low

### H-17: Halt retire stops the engine without clearing SDnSTS; a stale BCIS is credited to the next run
**Location:** `sys/drivers/audio/hda.c:1707`

**What is wrong**
- The halt-retire branch of `hda_kick()` stops the stream and zeroes the counters, but never writes BCIS|FIFOE|DESE back to SDnSTS. Close, flush, set_params and start all do.
- A BCIS that latched in the few µs before RUN was cleared (with the interrupt pending while IRQs are masked), or during the final frame, is credited to the fresh counters after the lock drops.
- The handler does not check `running`.

**Effects**
- Correction: chunk-aligned 4 KiB first writes start the engine inside the same critical section, and the start clears SDnSTS, so the common path is safe.
- **First write of 4 KiB or more, not chunk-aligned (for example 4608 bytes):** chunk[0], just staged, is zeroed and `slots_played` leads by one. Every later completion zeroes the slot being fetched, so the clip plays as near-silence.
- **First write under 4 KiB:** `slots_played = 1` against `writes_queued = 0`, which reproduces the H-1 wedge.
- The window is a few µs per 21 ms period.

**Correct behaviour:** BCIS, FIFOE and DESE are sticky RW1C bits that survive clearing RUN (3.3.36). After RUN reads back 0, write them back before resetting the position counters.

**Fix:** after `hda_stream_stop()` in the retire branch, write `HDA_SDSTS_BCIS|FIFOE|DESE`. In the handler, acknowledge but ignore BCIS while `!running`.

### H-18: Drain treats the final BCIS as "played" and stops the engine immediately, truncating the tail
**Location:** `sys/drivers/audio/hda.c:2293` (stop in `hda_kick()` 1705; close 2082)

**What is wrong:** the final completion sets `halt_pending` and wakes the drain. Drain's next `hda_kick()` clears RUN before the drained check, and `hda_close()` stops the engine again. The deferral meant to let the FIFO play out never happens.

**Correct behaviour:** BCIS means the last byte was fetched into the DMA FIFO, not played (3.3.36). Wait for LPIB to move into the following silence slot, or for one more completion, before clearing RUN.

**Failure:**
- QEMU: about 20-40 ms of every clip's tail is lost, because the emulated codec read-ahead buffer is dropped.
- Real PCH hardware: the SDnFIFOS-sized remainder (a few ms) is lost, heard as a click or clipped tail.

**Fix:** in `hda_drain()`, once `in_flight <= 0`, wait for one further BCIS (or LPIB past the end offset) before retiring the halt.

### H-19: A verb timeout returns a zero response, which the graph walk treats as valid data
**Location:** `sys/drivers/audio/hda.c:805`

**What is wrong:** `hda_send_verb()` returns 0 on timeout. A zero value decodes as an analog Audio Output widget with no digital bit, a "jack" port, and an empty connection list. So a timed-out caps read makes any node a DAC candidate.

**Correct behaviour:** a missing response must be distinguishable from 0x00000000. An all-ones sentinel decodes as type 0xF (vendor-defined) with the digital bit set, so it fails safe.

**Fix:** return 0xFFFFFFFF on timeout, or switch the walk to `hda_try_verb()` and skip the node.

### H-20: The feeder writes into the BDL slot under DMA when the running ring empties
**Location:** `sys/drivers/audio/hda.c:1653`

**What is wrong**
- When `in_flight == 0` and RUN is set, `next_idx == done + 1`, which is the descriptor the engine has just moved into.
- A feed from the interrupt handler or `hda_kick()` overwrites it while it is being fetched, so a few hundred bytes of the new chunk are skipped after a short zero run.
- This happens only at an underrun edge; the counters stay in step.

**Correct behaviour:** never rewrite the descriptor at the current LPIB position. Resume staging after it, or restart from entry 0.

**Fix:** when `in_flight == 0` and RUN is set, advance `writes_queued` and `next_idx` by one (leaving the current, zeroed slot alone), or retire through stop and restart.

### H-21: SDnSTS is acknowledged after processing, so a BCIS raised meanwhile is erased
**Location:** `sys/drivers/audio/hda.c:1878`

**What is wrong:** status is read at 1777, then a memset and up to 8 KiB of copying run, and only then are the captured bits written back. A completion that latches during that window is cleared without being counted. The effect is a one-slot lag, the same state the driver already tolerates from coalesced completions.

**Correct behaviour:** acknowledge the exact bits observed immediately after reading them, then act on the captured value.

**Fix:** move the `hda_write8(SD_STS, sdsts & (BCIS|FIFOE|DESE))` to immediately after the read.

### H-22: GETODELAY and play.seek omit up to 124 KiB queued in the DMA ring
**Location:** `sys/drivers/audio/hda.c:2381`

**What is wrong:** `hda_get_ospace()` reports only FIFO free space, so `fragsize × fragstotal − free` leaves out `(writes_queued − slots_played) × 4096` (up to about 650 ms at 48 kHz stereo).

**Correct behaviour:** output delay is the software bytes plus the bytes from SDnLPIB to the end of the last staged descriptor.

**Failure:** A/V sync is off by up to about two-thirds of a second. Close still drains correctly, so the claimed tail cut-off does not occur.

**Fix:** add a `get_odelay` op returning `fifo_used + max(0, in_flight) × 4096 − (LPIB % 4096)`.

### H-23: AUDIO_WSEEK and play.samples count bytes accepted, not bytes queued or consumed
**Location:** `sys/drivers/audio/audio.c:354` (increments at 873, 949)

**What is wrong:** WSEEK returns the running total of accepted bytes, which grows without bound, and `written − play.samples` is always 0.

**Correct behaviour:** WSEEK should be the bytes still queued ahead of the device. play.samples should advance block by block as data is consumed toward the hardware.

**Fix:** return the queued-bytes quantity (play.seek in bytes) for WSEEK, and advance `play.samples` from consumption, not from writes.

### H-24: Drain reports success after a signal or an engine stall
**Location:** `sys/drivers/audio/hda.c:2318`

**What is wrong:** every exit path returns 0: empty queue, signal, the 1.5 s stall limit, or the 60 s ceiling. AUDIO_DRAIN and SNDCTL_DSP_SYNC pass that 0 through.

**Fix:** return `-EINTR` on a pending signal and `-EIO` on stall or ceiling. `hda_close()` already ignores the result.

### H-25: Drain zero-pads the FIFO's partial chunk every 10 ms while another thread is still writing
**Location:** `sys/drivers/audio/hda.c:2280`

**What is wrong**
- Any opener (another thread, or another process) can drain while the owner writes.
- With a producer that runs near-empty and is paced at real time, each poll queues a padded half-slot, which inserts silence.
- The chopping typically lasts about 1.5 s, until the stall detector fires.
- A full-FIFO producer is unaffected.

**Correct behaviour:** pad a partial buffer only at a real end of stream.

**Fix:** use `flush_tail = 1` only when the caller is `play_owner` or no producer is active. Otherwise wait without padding.

### H-26: A deferred halt is never retired when the producer goes idle
**Location:** `sys/drivers/audio/hda.c:1847`

**What is wrong:** with the fd open and no writes, the engine cycles zeroed slots forever. That is about 47 interrupts per second, each doing an IRQ-masked memset and a wake. The link and codec never idle.

**Correct behaviour:** stop the stream in non-ISR context within a bounded time (clear RUN and read it back, then clear SDnSTS).

**Fix:** arm a deferred task from the park branch that calls the halt-retire code a few slot periods later.

### H-27: A failed stream start is swallowed and the writer retries forever
**Location:** `sys/drivers/audio/hda.c:1730`

**What is wrong**
- If SRST is never acknowledged, `hda_kick()` only prints, and `running` stays 0. The writer sleeps with no completion source.
- Every 250 ms fallback re-runs stop and SRST under `feed_lock` with IRQs masked (about 10,000 MMIO reads) and prints again.
- `write()` never returns an error.

**Correct behaviour:** a descriptor that will not acknowledge SRST (3.3.35) is unusable. The caller must get an I/O error.

**Fix:** latch `d->stream_error = -EIO`. Have `hda_write()` and `hda_drain()` return it. Do not retry until flush or close (or a controller reset).

### H-28: `set_params` with an unchanged format discards buffered PCM when the stream is idle
**Location:** `sys/drivers/audio/hda.c:2132`

**What is wrong:** only `fmt == d->fmt && running` is treated as a no-op. With the stream idle, the call resets the ring and FIFO and rebinds the same format.

**Triggers:** SETFMT, SPEED, CHANNELS, STEREO, or AUDIO_SETINFO with only gain or blocksize changed. SETFRAGMENT does not trigger it.

**Fix:** treat `fmt == d->fmt` as a no-op whether or not RUN is set, since SDnFMT is rewritten from `d->fmt` at every start anyway.

### H-29: The converter format is bound after `feed_lock` is dropped, so SDnFMT and the converter can diverge
**Location:** `sys/drivers/audio/hda.c:2172`

**What is wrong:** two concurrent `set_params` calls can interleave so that SDnFMT ends as Y while the converter keeps X. The result is wrong-speed or wrong-channel playback until the next format change.

**Correct behaviour:** stream descriptor format and Converter Format (verb 2h) must always describe the same stream. Program both in one serialized step.

**Fix:** take a sleepable per-device mutex across the SDnFMT write and `hda_codec_bind_stream()`. Have `hda_stream_start()` take the same mutex, or re-send verb 2h itself.

### H-30: A short backend write on a signal discards converted PCM but reports it as written
**Location:** `sys/drivers/audio/audio.c:936`

**What is wrong:** on `rc < outn`, `conv_buf[rc..outn)` is dropped while `done += pending` counts its input as consumed. The trigger is a caught signal (or a stop signal) while the writer is blocked on a full FIFO. The skip is up to 4 KiB, and play.samples drifts.

**Fix:** keep the unaccepted tail (`conv_pending` offset and length) and hand it to the backend first on the next write. Alternatively, count only the input that maps to accepted output.

### H-31: OSS DSP_RESET flushes the backend but keeps the converter's partial-frame carry
**Location:** `sys/drivers/audio/oss.c:341`

**What is wrong**
- Unlike AUDIO_FLUSH (`audio.c:273`), RESET never calls `audio_conv_reset()`.
- Any frame size of 2 bytes or more can leave a carry, for example 24-bit packed stereo with 4096-byte writes. 8-bit mono cannot.
- After a seek and reset, every input frame is decoded at an offset, which produces noise.

**Fix:** call `audio_conv_reset(&dev->conv)` before `dev->ops->flush(dev)`.

### H-32: `hw_chan_min` is hard-coded to 2 and exceeds `hw_chan_max` on a mono converter
**Location:** `sys/drivers/audio/hda.c:2828`

**What is wrong**
- `HDA_AW_CHAN_COUNT()` is never 0, so the `: 2` fallback at 2829 is dead code.
- Either a mono converter (Chan Count 0) or a timed-out caps read gives `dac_max_chan = 1`, so `min = 2 > max = 1`.
- Negotiation then always fails: SETINFO and SPEED return EINVAL, and the registration fallback records 44.1 kHz while the hardware runs at 48 kHz, so playback is 8.8% fast.

**Fix:** set `hw_chan_min = min(2, dac_max_chan)`. Detect a caps-read timeout (H-19) and fall back to stereo in that case.

### H-33: 48 kHz is refused when the capability word omits bit 6
**Location:** `sys/drivers/audio/hda.c:1161`

**What is wrong:** spec 7.3.4.7 makes 48 kHz mandatory, and it is also the default the converter is bound to at attach, yet the driver gates it on R7. A partial capability word, or a Format Override converter word without R7, disables the one guaranteed rate. No listed codec omits it.

**Fix:** accept 48000 unconditionally and test the bits only for the other ten rates.

### H-34: No settle or PS-Act readback after D0; widgets between DAC and pin are never powered
**Location:** `sys/drivers/audio/hda.c:1343` (AFG D0 at 1217)

**What is wrong**
- Only the AFG, DAC and pin get D0. The mixer or selector in between does not.
- Control writes follow the widget D0 writes with no explicit settle.

**Correct behaviour (7.3.3.10)**
- AFG to D0, wait about 100 µs, then every widget in the function group to D0, then wait about 1 ms before committing amp, pin-control, EAPD and converter settings.
- Widgets with Power Control (caps bit 10) hold their own state.
- PS-Act polling is optional; a fixed settle is enough.

**Impact:** low on the ALC255, where the path mixers have no Power Control. It matters on codecs whose selectors or mixers do.

**Fix:** loop over the AFG subnodes with SET_POWER_STATE D0, then `timer_busywait_ms(1)` before the route commit.

### H-35: No vendor initialisation hook for ALC255/ALC256 (headset-jack coefficient)
**Location:** `sys/drivers/audio/hda.c:1402` (no codec-ID dispatch; the vendor ID is used only for the banner)

**What is wrong**
- Realtek ALC255/256 (0x10ec0255/0x10ec0256) need vendor coefficient programming through processing node 0x20 (index register 0x46, bits 13:12) for headset-jack behaviour. The driver has no coefficient verbs and no per-codec init.
- Verifiers disagreed on the polarity of bits 13:12: whether they must be set to enable headphone output or act as a jack pull-down control.
- The speaker path (0x14 → 0x0c → DAC 0x02) does not depend on this coefficient.

**Fix:**
1. Keep the 32-bit codec vendor ID in `hda_dev_t` and add a vendor-init hook that runs after the generic commit.
2. Add the SET_COEF_INDEX (5h), GET_PROC_COEF (Ch) and SET_PROC_COEF (4h) verbs, which depend on H-36.
3. Before choosing a polarity, check the coefficient value with headphones plugged in on the ALC255 machine.

### H-36: The verb packer treats the 4-bit coefficient verbs (4h/5h/Ch/Dh) as 12-bit verbs
**Location:** `sys/drivers/audio/hda.c:107`

**What is wrong:** `hda_verb_is_short()` recognises only 2h, 3h, Ah and Bh. Set Processing Coefficient with a value above 0xFF would lose its high byte. The defect is latent: no coefficient verb is issued today.

**Fix:** add 0x4, 0x5, 0xC and 0xD to the short-verb set, and correct the comment and the host test.

### H-37: The output pin keeps firmware IN_EN and VREF bits when OUT_EN is set
**Location:** `sys/drivers/audio/hda.c:1375`

**What is wrong**
- The RMW only ORs in OUT_EN and HP_EN, so a retaskable or rank-0 jack could be driven with mic bias and its input path enabled.
- HP_EN is set from Configuration Default alone, without checking the pin's HP-drive capability.
- On the ALC255 speaker pin this has no effect.

**Correct behaviour (7.3.3.13):** clear IN_EN (bit 5) and VRefEn (bits 2:0), set OUT_EN, and set HP_EN only if the pin caps allow headphone drive.

**Fix:** `ctrl = (ctrl & ~0x27) | 0x40 | (hp && (pincap & HP_DRV) ? 0x80 : 0)`.

### H-38: Function-group type is masked to 7 bits, so vendor-defined group 0x81 matches as audio
**Location:** `sys/drivers/audio/hda.c:1207`

**What is wrong:** NodeType is bits 7:0 (7.3.4.4). No listed codec exposes a 0x81 group.

**Fix:** compare `(t & 0xFF) == 0x01`.

### H-39: Quiesce writes RUN=0 but never verifies it cleared before dropping CRST
**Location:** `sys/drivers/audio/hda.c:458` (CRST at 486-491)

**What is wrong:** spec 3.3.7 requires CORB, RIRB and all stream RUN bits to be verified as 0 before CRST# is written 0. The driver's own comment quotes this, but the code does not check. The exposure is a warm handoff with engines still running.

**Fix:** after each clear, poll SDnCTL.RUN, CORBCTL.CORBRUN and RIRBCTL.DMAEN to 0, bounded by `HDA_STREAM_TIMEOUT`/`HDA_RING_TIMEOUT`. Log, rather than abort, on failure.

### H-40: The verb wait spins up to 20,000 MMIO reads with interrupts masked
**Location:** `sys/drivers/audio/hda.c:745` (`HDA_VERB_TIMEOUT` at 59)

**What is wrong**
- `verb_lock` is an IRQ-masking spinlock, and the budget counts reads, not time.
- A verb that gets no answer costs about 6-20 ms on a PCH (more under a hypervisor) with interrupts off. At HZ=250 (4 ms ticks), that loses several timer ticks.
- A codec that never answers costs only one budget, because attach and configure bail on the first silent verb. A codec that dies partway through the walk costs tens of budgets.
- The interrupt handler never takes `verb_lock`, so masking interrupts buys nothing.

**Correct behaviour:** a response arrives within one or two 20.8 µs frames. Poll RIRBWP with a short delay between reads, under a lock that does not mask interrupts, bounded in time (tens of ms).

**Fix:** replace the spinlock with a sleepable mutex (or `spinlock_acquire` without IRQ masking), poll with a ~10 µs delay up to a deadline, and mark the codec dead after N consecutive timeouts.

### H-41: A silent MSI abandons the attach with no fallback to INTx
**Location:** `sys/drivers/audio/hda.c:2786`

**What is wrong:** if the MSI proof fails, the driver detaches. It never tries the firmware Interrupt Line or the routed GSI, and it has no per-device "MSI broken" list. Some NVIDIA chipset and GPU HD Audio functions are known to deliver MSI unreliably. None of the listed targets is affected.

**Fix:** on proof failure, disable MSI, free the vector, clear INTx-disable, retry with `pci_get_irq()` and then `pci_route_intx()` with the same proof, and fail only after that. Start known-bad IDs on INTx.

### H-42: The INTx routing proof counts other devices' interrupts on a shared line as success
**Location:** `sys/drivers/audio/hda.c:1902` (proof at 2763-2803)

**What is wrong:** `intr_count++` runs before the GIS check, and the handler is `IRQF_SHARED`. Any co-sharer's assertion during the probe window passes a wrong PIRQ-swizzle guess. This is reachable only when there is no MSI and Interrupt Line is 0xFF; MSI vectors are exclusive.

**Correct behaviour:** count only an interrupt taken while INTSTS.GIS is set, with RIRBSTS.RINTFL latched by the probe exchange.

**Fix:** increment the proof counter only inside the `GIS && RINTFL` branch. Repeat the provoke-and-wait cycle 2-3 times.

### H-43: Partial detach frees the DMA rings without a verified stop, keeps the BAR mapping, and leaves bus mastering on
**Location:** `sys/drivers/audio/hda.c:2420` (CORBCTL write at 2426)

**What is wrong**
- CORBCTL and RIRBCTL are written 0, RUN is never read back, and the pages are freed with CORBLBASE/RIRBLBASE still pointing at them and Bus Master Enable still set.
- `iounmap(d->mmio)` is never called.
- The one-frame RIRB write-after-free window is theoretical. The leak is bounded at 16 KiB of the 16 MiB ioremap window per failed controller per boot.
- Correction: the BAR resource itself stays owned by the PCI layer and is reused, so it does not leak.

**Fix:** before freeing, lower CRST and read it back as 0 (or poll the RUN bits to 0), then clear `PCI_COMMAND_MASTER`, free the memory, and call `iounmap(d->mmio)`.

### H-44: A failed attach on the routed-INTx path never frees the IDT vector
**Location:** `sys/drivers/audio/hda.c:2443`

**What is wrong:** `pci_unroute_intx()` masks the pin but does not release the vector from `pci_route_intx()`, and `irq_free_vector()` is called only for MSI. The same gap affects the NIC drivers. It can be reached only without MSI, and costs one vector per failed controller.

**Fix:** call `irq_free_vector(d->irq); d->irq = -1;` after `pci_unroute_intx()`, or do this inside `pci_unroute_intx()` for all callers.

### H-45: AMD/ATI snoop: config 0x42 is written on GPU HDMI functions, and a failed or unknown snoop still runs with cacheable DMA
**Location:** `sys/drivers/audio/hda.c:2503` and `hda.c:2535`

**What is wrong**
- The 0x42 bit-1 enable (bits 2:0 masked) exists only on chipset (SB450/SB600-class and FCH) HD Audio functions.
- The driver applies it to every 0x1002 and 0x1022 function, including discrete-Radeon HDMI audio such as 1002:aac0. That function has no such register and does not snoop at all.
- A failed readback, or an unknown vendor, only logs a warning, and attach continues with write-back rings.
- The comment claiming uncached memory cannot be allocated is wrong: `PTE_PCD` and `ioremap()` exist.
- On aac0 the impact is small, because its HDMI-only codecs are rejected and no stream ever runs.

**Correct behaviour:** apply 0x42 only to the chipset device IDs. For functions that do not snoop, or whose snoop setting cannot be verified, map the rings and buffers uncached, or refuse to attach.

**Fix:**
- Key the ATI/AMD row on device ID.
- On a failed or unknown snoop setting, alias the CORB, RIRB, BDL and chunk pages through an uncached mapping before programming any base register, or fail the attach.

### H-46: The controller is never moved to PCI power state D0 before MMIO access
**Location:** `sys/drivers/audio/hda.c:2562`

**What is wrong**
- There is no PM capability lookup, PMCSR check, or 10 ms D3hot→D0 wait, here or in `sys/kern/pci.c`.
- A function handed over in D3hot fails cleanly with "controller will not enter reset".
- The "STATESTS reads 0xFFFF" path is unreachable, because CRST fails first.
- A platform reset restores D0, so the trigger is rare.

**Fix:** in generic PCI enumeration (preferred) or before the first BAR access:
1. Find capability 01h.
2. If PMCSR[1:0] is not 0, write 0 and wait at least 10 ms.
3. If No_Soft_Reset is 0, restore the BARs and command register.

---

## Per-target exposure

### QEMU intel-hda + hda-duplex (the only configuration where playback is verified)
Rates and MSI work here. Most likely or most visible problems:
- **H-1:** paused players wedge, and close takes 60 s.
- **H-6:** short effects never start; aligned writes restart with one slot after every underrun.
- **H-11:** stale audio replays after RESET or a format change.
- **H-18:** about 20-40 ms of every clip's tail is truncated.
- **H-7:** a second `/dev/dsp` open garbles the first stream.
- **H-8 and H-9:** concurrent flush or close corrupts the stream.
- **H-10:** 250 ms stalls after stop paths, and possible sleep-queue corruption.
- **H-28, H-31, H-22 and H-23:** lost or incorrectly reported buffered data.

Hardware-specific findings (H-2, H-3, H-12, H-13, H-14) do not apply.

### Sunrise Point-LP laptops (Fujitsu Skylake-U 8086:9d70 with a Realtek ALC255, attach observed as afg=1 dac=2 pin=20; the HP Pavilion dev host, 8086:9d71 with a Realtek ALC295)
- **H-3:** NSNPEN defaults to 1 per the datasheet (DEVC 2800h). If firmware leaves it set, write-back sample buffers are read without snooping: the first suspect for static or corrupt playback on real hardware.
- **H-1, H-6, H-17, H-18 and H-20:** ring and drain defects show up as gaps, clicks, truncated tails and wedges.
- **H-13 and H-12:** headphone pin 0x21 is never enabled, so headphones are silent and the speakers keep playing. Only one pin's EAPD is asserted.
- **H-35:** no ALC255/ALC295 vendor init for the headset jack.
- **H-4, H-19, H-40, H-34:** threats if any codec response is slow after D0.
- **H-2:** a risk if firmware ever places BAR0 above 4 GiB. The dev host's BAR is below 4 GiB (0xb1328000).

### Whiskey Lake / Cannon Point-LP (8086:9dc8, prog-if 80h, UEFI Interrupt Line 0xFF; a log from the MSI-capable kernel is still pending)
- **H-2:** high risk. Cannon Point-era UEFI firmware commonly places the 16 KiB HDA window above 4 GiB. That would cause boot-time memory corruption, not just a failed attach.
- **H-14:** this box presents 040380h and so is matched, but a firmware DSP-mode change to 040100h would make it vanish.
- **H-3:** the DEVC default here is also 2800h.
- **H-41:** if MSI does not arrive, there is no INTx fallback.
- **H-5, H-46:** latent threats to attach.
- Once attached, the same playback set as Sunrise Point applies: H-1, H-6, H-12, H-13.

### Haswell laptop (Lynx Point 8086:8c20 analog, Haswell display audio 8086:0c0c, AMD 1002:aac0 HDMI; all three hit "no usable IRQ line" on the old kernel)
- **8c20:**
  - **H-3:** NSNPEN on Lynx Point; firmware handling differs between generations.
  - **H-2:** BAR placement risk.
  - **H-12, H-13:** pin and EAPD wiring on an unknown codec.
  - **H-1, H-6:** playback path.
- **0c0c:** the display codec with its graphics power well down is silent. Attach fails cleanly after one timeout (H-40 for one budget), then H-43 and H-44 leak resources.
- **1002:aac0:**
  - **H-45:** the stray 0x42 write and the misleading snoop warning; the function does not snoop, and the driver has no uncached fallback.
  - **H-43:** leaks on the expected attach failure, since this function has HDMI-only codecs.
  - **H-46:** not ensuring D0 is a latent risk.
  - `HDA_MAX_CONTROLLERS` is 2, but slots are reused only after a failed attach, so 8c20 still registers.

---

## Areas examined and found correct
- **Register map and stream descriptor addressing:** GCAP-derived ISS/OSS/BSS, output descriptor base `0x80 + ISS×0x20`, SDnCTL/STS/LPIB/CBL/LVI/FIFOS/FMT/BDPL/BDPU offsets.
- **Stream descriptor programming:** SRST asserted and released with readback before BDL/CBL/LVI/FMT/stream tag are programmed, then RUN|IOCE.
- **BDL entry layout:** 64-bit address, length, IOC in bit 0 of the flags dword, 128-byte alignment.
- **CORB/RIRB bring-up:** size selection from CORBSZCAP/RIRBSZCAP, CORBRP reset handshake (bit 15 set and cleared, each verified), RIRBWP reset, RINTCNT, and CORBRUN/RIRBDMAEN start with readback.
- **Controller reset:** CRST low/high with readback, the reset hold, and codec discovery wait before STATESTS is read.
- **Verb encoding:** 12-bit verb with 8-bit payload; the four 4-bit verbs 2h/3h/Ah/Bh with 16-bit payload (aside from the missing coefficient verbs in H-36).
- **SDnFMT encoding:** base 44.1/48 kHz, multiplier and divisor table, bits-per-sample and channel fields; matching Converter Format (verb 2h) and Stream/Channel (706h) encoding.
- **Widget capability decoding:** type, digital bit, channel count extension, amp-override and connection-list bits.
- **Connection list parsing:** short and long forms, range expansion, list length.
- **Amp gain/mute verb layout:** output/input/left/right/index fields.
- **TCSEL clear to TC0 on Intel controllers.**
- **MSI programming:** 32-bit message address with upper dword 0, compatible with controllers that reject 64-bit MSI addresses.
- **Selection rules:** digital converters and digital pins skipped when choosing an analog path; HDMI-only codecs correctly rejected so the device is not registered.
