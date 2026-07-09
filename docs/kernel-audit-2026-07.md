# Substrate Kernel + Libraries Audit — July 2026

Full-tree defect audit of `sys/` (~167K lines) and `lib/c/` (~17.5K lines),
performed by fanning out parallel per-subsystem review passes (VM, filesystems,
and networking each audited **twice** for cross-validation). `lib/m/` and
`lib/sys/` audits are appended at the end.

Status legend: **[ ] open** · **[~] in progress** · **[x] fixed** · **[!] wontfix/deferred**

---

## Cross-cutting themes

Fix the *class*, not just the instance:

1. **Shared state mutated in IRQ + process context with no real lock.**
   `intr_disable` gives zero SMP exclusion, and even on UP the timer preempts any
   window where `preempt_count==0`. Instances span UMA slab lists, the `pv_entry`
   pool, VM page queues, AF_INET rings, ARP cache, UART TX ring, SCSI pools,
   keyboard modifiers, the ext2 node cache, and ext2 inode-table writes.
2. **Cache slots / objects handed out as bare pointers with no pin/refcount.**
   ext2 node cache, AF_INET sockets (vs AF_UNIX which *is* refcounted), TCP
   unacked segments, USB device structs on unplug.
3. **Untrusted on-disk / on-wire length fields used unbounded.**
   ext2 `name_len`/`rec_len`, ELF `PT_NOTE` `namesz`/`descsz`.
4. **Timeout paths reclaim hardware-owned DMA without quiescing the controller.**
   All three USB HCDs + AHCI.
5. **"Fixed once, missed elsewhere."** The `THREAD_BLOCKED`-holding-mutex fix
   landed in `pipe_wait`+mqueue but was missed in `posix_sem`/`ipc_sem`/`fifo_open`.
   The `tcp_lock` IRQ-sync landed in TCP but not AF_INET. Refcounting landed in
   AF_UNIX but not AF_INET. The bare-`-1`→`-errno` purge missed `acct`/`mmap`/`time`.

---

## CRITICAL

- **[ ] VM-01** `sys/vm/uma_core.c:855` — UMA slab layer is entirely unlocked: the
  fast path drops `intr_disable` *before* the slab slow path, so two allocators of
  one zone hand the **same item to two callers** → kernel-wide heap corruption.
  *(Both VM passes.)*
- **[ ] VM-02** `sys/vm/vm_fault.c:89` (+ `vm_object.c:151-218`) — `vm_fault` mutates
  shared object state under only the map **read** lock; two threads faulting one
  object recycle a frame while it is still being `pmap_enter`ed → silent corruption.
- **[ ] FS-01** `sys/fs/ext2/ext2.c:1263-1420` — ext2 256-slot node cache selects
  and populates slots with **no lock** → two lookups claim the same slot and
  `memcpy` different inodes into it (cross-file corruption). Root of the
  **reproduced** live-symlink double-allocation.
- **[ ] FS-02** `sys/fs/ext2/ext2.c:479-598` — `ext2_write_inode` does an
  unserialized read-modify-write of a whole inode-table block shared by many
  inodes; a co-resident inode's `i_links_count` gets clobbered to 0 → stale-slot
  logic + fsck treat a live inode as free. Second half of the reproduced bug.
- **[ ] DRV-01** `sys/drivers/usb/usb.c:837` + `usb_msc.c:532` — USB unplug frees
  the device + DMA buffers (`cbw`/`csw`/`udev`) with **no in-flight transfer
  quiescing** and no poll-thread join (HID joins; storage does not) → DMA into
  recycled memory / UAF.
- **[ ] DRV-02** `sys/drivers/usb/usbdevfs.c:184` — `usbdevfs_publish` stores
  `node->impl = dev` on `/dev/usb/...` nodes with **no unpublish path**; after
  `usb_free_device`, `lsusb`/libusb deref the freed (address-reusable) `dev` → UAF.
- **[ ] KERN-01** `sys/kern/signal.c:285` → `sleepq.c:75` — `psignal()` runs in ISR
  context (timer tick, `^C`) and calls `sleepq_remove_thread()` → takes the sleepq
  bucket `sq_lock`, a plain non-IRQ-safe spinloop with no try/timeout → if the
  interrupted thread on that CPU holds the colliding bucket, **hard CPU lockup**.
- **[x] KERN-02** `sys/kern/ipc_sem.c:364` — SysV `semop` parks `THREAD_BLOCKED`
  holding `sem_lock` with no `intr_disable` window and no `sleep_expiry` fallback →
  permanent system-wide SysV-semaphore deadlock (the pipe/mqueue bug class).
- **[x] KERN-03** `sys/kern/posix_sem.c:377` — POSIX `sem_wait` has the same
  unprotected window → named-semaphore deadlock.
- **[ ] LIBC-01** `lib/c/stdio/stdio_core.c:141` — `fflush()` on an update-mode
  (`r+`) stream writes buffered **read** data back to the file:
  `fopen("f","r+"); fgetc(f); fclose(f)` silently corrupts the file (also via
  `fflush(NULL)` at `exit()`).

---

## HIGH

### Memory / arch
- **[ ] ARCH-01** `sys/arch/i386/sched.c:19` (+ `fpu/fpu_emu.c:36`) — lazy-FPU save
  is dead code: `fpu_save_context()` has no callers and CR0.TS is never re-armed
  after the first `#NM`; later processes run FP/SSE with no trap → cross-process
  float **corruption + SSE register disclosure**. *(2 passes.)*
- **[ ] PM-01** `sys/pm/process.c:482,490-497` — fork error paths double-destroy the
  child pmap (`vm_map_destroy` already frees `map->pmap`, then `pmap_release` runs)
  → UAF / double-free of the page directory.
- **[ ] VM-03** `sys/vm/vm_page.c:297-393` — global page queues and `pv_entry` pool
  unlocked (pagedaemon vs fault vs exit) → list corruption / two pages sharing a
  `pv_entry`.
- **[ ] VM-04** `sys/vm/uma_core.c:997` — `uma_reclaim` drains other CPUs' per-CPU
  buckets unsynchronized → double-allocation.
- **[ ] VM-05** `sys/vm/vm_syscalls.c:519` — `sys_brk` unserialized → concurrent
  sbrk lost-update truncates the heap and leaks the loser's frames.

### Security
- **[ ] SEC-01** `sys/exec/formats/elf.c:1379,1676` + `sbin/ld.so/ld_main.c` — setuid
  exec has no secure-exec hardening: `AT_SECURE` hardcoded 0, `ld.so` honors
  `LD_PRELOAD`/`LD_LIBRARY_PATH` unconditionally, `MNT_NOSUID` never checked →
  `LD_PRELOAD=evil.so ./setuid-root-bin` = local root.

### Filesystems
- **[x] FS-03** `sys/fs/ext2/ext2.c:2898` — `ext2_add_entry` unsigned
  `slack = rec_len - actual_size` underflows from untrusted `name_len` →
  **heap overflow** writing a dirent past the block buffer on any create.
- **[x] FS-04** `sys/fs/ext2/ext2.c:1660` — `ext2_readdir` copies `name_len` past the
  block bound → **OOB heap read** leaking kernel memory into userspace `d_name`.
- **[ ] FS-05** `sys/fs/ext2/ext2.c:3049` — finddir returns unpinned node-cache
  slots recycled mid-path-walk and mid-`ext2_rename` (derefs `old_node` after
  intervening recycling calls).

### Networking
- **[ ] NET-01** `sys/net/af_inet.c:437,960` — AF_INET UDP/RAW list + per-socket
  rings mutated in hard IRQ while `close()` frees nodes with IRQs on, no socket
  refcount → UAF/heap corruption under inbound traffic.
- **[ ] NET-02** `sys/net/tcp.c:283,343` — `tcp_timer_tick` drops `tcp_lock` then
  derefs an unacked segment an incoming ACK can `kfree` → retransmit UAF.
- **[ ] NET-03** `sys/net/af_inet.c:566` + `af_unix.c:1876` — `getsockopt`/`bind`/
  `getsockname` deref raw **user pointers** without copyin/out → kernel fault/DoS.

### Drivers
- **[ ] DRV-03** `sys/drivers/usb/xhci.c:436` — transfer completion accepts *any*
  event-ring entry; a Port-Status-Change event is consumed as the transfer's
  completion → bogus length, desynced event stream.
- **[ ] DRV-04** `sys/drivers/usb/usb.c:830` — `dev->parent` is never assigned, so
  hub-downstream devices masquerade as root-port devices and the 250 ms hotplug
  scan can disconnect a working hub-attached disk.
- **[ ] DRV-05** `sys/drivers/storage/ahci/ahci.c:393` + `usb/ehci.c:195` +
  `uhci.c:597` — timeout paths reclaim controller-owned DMA memory without stopping
  the hardware → late completion DMAs into recycled memory (IDE does this right).
- **[ ] DRV-06** `sys/drivers/usb/ehci.c:195` — `ehci_run_qh` polls stale qTDs from
  prior transfers → spurious `USB_XFER_STALL` / burned timeouts on USB2 storage.
- **[ ] DRV-07** `sys/drivers/usb/ehci.c:294` — bulk data-toggle advanced once per
  qTD regardless of packet count → wrong DATA PID on even-packet transfers → USB2
  mass-storage reads hang.
- **[ ] DRV-08** `sys/drivers/storage/scsi/scsi.c:276` — SCSI request + device pools
  have zero locking on the SMP/preemptible I/O stack → cross-wired CDBs.

### libc
- **[ ] LIBC-02** `lib/c/src/cvt.c:97` — `fcvt_common` `memcpy`s the full number then
  truncates → buffer overflow for large magnitudes.
- **[ ] LIBC-03** `lib/c/stdio/printf.c:80` — `ftoa` casts `double`→`int64` with no
  range check → `printf("%f",1e19)` prints `INT64_MIN`; poisons `%e/%g/fcvt`.

### Scheduler / signals
- **[ ] KERN-04** `sys/pm/sched.c:697` + `sleepq.c:640` — `sched_wakeup_n` clears
  `wait_chan=NULL` **without dequeuing** from the sleepq → the waiter's self-unlink
  is skipped; a later wake pops the stale (or freed) `thread_t` → sleepq UAF.
- **[ ] KERN-05** `sys/kern/futex.c:1067` — `futex_lock_pi` sleeps with no
  post-enqueue re-read, no timeout, no interruptible flag → racing `unlock_pi` =
  **unkillable lost-wakeup hang** for PI mutexes.
- **[ ] KERN-06** `sys/kern/signal.c:1157,1412,1425` — `psignal_info`/`sys_kill(-1)`/
  pgrp walkers iterate `FOREACH_THREAD/PROC` unlocked while `wait4` reap frees nodes
  → UAF / wild-pointer signal delivery.
- **[ ] KERN-07** `sys/kern/signal.c:286` — `signal_interrupt_thread` sets
  `THREAD_READY` with no ZOMBIE/STOPPED guard → can resurrect a thread `proc_exit`
  is tearing down → "switch into dying context" CPU wedge.

---

## MEDIUM

### Filesystems
- **[x] FS-06** `sys/fs/pipe.c:569` — `fifo_open` reintroduces the
  THREAD_BLOCKED-holding-mutex deadlock (missing `intr_disable` window).
- **[ ] FS-07** `sys/fs/ext2/ext2.c:3042` — `ext2_rename` has no ancestor/self check
  → `rename("/a","/a/b")` creates a detached directory cycle.
- **[x] FS-08** `sys/fs/ext2/ext2.c:337` — `ext2_write_block` has no block-range
  bounds check (the read path does) → a corrupt block pointer writes off-range.
- **[x] FS-09** `sys/fs/ext2/ext2.c:2498,2667` — `free_block`/`free_inode` bump the
  free counts without checking the bit was set → a double-free inflates counts →
  later over-allocation.
- **[ ] FS-10** `sys/vfs/bio.c:702` — `bio_dev_invalidate` clears `B_DELWRI`
  unconditionally, discarding pending dirty writeback.

### VM
- **[ ] VM-06** `sys/vm/vm_map.c:1062` — `vm_map_clip` silently returns on OOM, so
  `vm_map_protect` applies protection to the whole unclipped entry (RELRO regression).
- **[ ] VM-07** `sys/vm/vm_map.c:804` — `vm_map_insert` COW-shadow path leaks the
  shadow on failure and lets the caller free the backing object under a live pointer.
- **[ ] VM-08** `sys/vm/vm_fault.c:271` — `PG_BUSY` never cleared on the prefault
  read-ahead page / COW source page → those pages become permanently unreclaimable.
- **[ ] VM-09** `sys/vm/vm_object.c:171` — in-place COW strands one physical frame
  per fault for split anon regions (ref-0, list-less, never freed).
- **[ ] VM-10** `sys/vm/uma_core.c:167` — `uma_bucket_alloc` reads/increments the
  bucket index outside the depot lock → two CPUs install the same bucket (SMP).
- **[ ] VM-11** `sys/vm/vm_kmem.c:217` — `kfree` large path reads `hdr->size` *after*
  freeing the pages → UAF read / corrupted accounting.
- **[ ] VM-12** `sys/vm/vm_syscalls.c:276,457,474` — `sys_mmap` returns bare
  `(void*)-1` instead of `-ENOMEM` (libc maps it to EPERM).

### Scheduler / signals / IPC
- **[ ] KERN-08** `sys/kern/ptrace.c:117` — `PTRACE_ATTACH` has no credential check
  (any process ptraces any other → privesc/infoleak) and its check/set race +
  no hold on the tracee.
- **[ ] KERN-09** `sys/kern/futex.c:1108` — `pi_state.owner` is a bare cached
  `thread_t*` with no hold → UAF when a PI owner dies without unlocking.
- **[ ] KERN-10** `sys/kern/lockmgr.c:96` — post-sleep reacquisition order
  (`lk_interlock` then `interlock`) inverts the caller's order → ABBA deadlock once
  APs schedule.
- **[ ] KERN-11** `sys/kern/futex.c:883` — PI priority inheritance uses "larger =
  higher", inverted for SCHED_TIMESHARE, so a boost *lowers* the owner's weight.
- **[ ] KERN-12** `sys/kern/acct.c:49,52` — `kern_acct` returns bare `-1` instead of
  `-ENOENT`/`-EISDIR`.

### exec / arch
- **[ ] EXEC-01** `sys/exec/formats/elf.c:76` — `elf_note_detect_os` computes
  `desc_off` from untrusted `namesz`/`descsz` with only a wrap-vulnerable guard →
  OOB read off the stack buffer → panic.
- **[ ] EXEC-02** `sys/exec/formats/elf.c:1524` — `elf_execve` early-returns on the
  E2BIG arg-count path without freeing `image` (~8.5 KiB) or the fd → leak per call.
- **[ ] ARCH-02** `sys/arch/i386/syscall.c:110` — `sys_set_thread_area` writes
  `entry_number` through the raw user pointer with no fault trap → panic on a
  read-only user page.
- **[ ] EXEC-03** `sys/exec/perso/freebsd/freebsd_sig.c:32` — `SA_ONSTACK` tested
  only via `SS_DISABLE`, so a thread with a zero-init alt-stack gets `esp=NULL` →
  SIGSEGV instead of frame delivery.

### Networking
- **[ ] NET-04** `sys/net/tcp.c:337` — half-open (`SYN_RECEIVED`) child PCBs that
  time out are never freed (32 KiB rxbuf each) → half-open-flood DoS.
- **[ ] NET-05** `sys/net/tcp.c:531` + `inet.c:182` — `tcp_input` (hard IRQ) reaches
  `sched_yield()` via ARP-miss on a real NIC → sleep in interrupt context.
- **[ ] NET-06** `sys/net/tcp.c:472` — RST ignored in `SYN_RECEIVED` → half-open
  child lingers retransmitting SYN-ACK.
- **[ ] NET-07** `sys/net/af_inet.c:898` — UDP ephemeral port `++g_ephemeral_next`
  bypasses the wrap guard → port 0 / privileged ports after wrap; non-atomic.
- **[ ] NET-08** `sys/net/af_inet.c:1053` — `afinet_deliver_v4` applies the
  "looks like IP" heuristic to bare UDP datagrams → misparse when the source-port
  high byte is `0x4X`.

### Drivers
- **[ ] DRV-09** `sys/drivers/console/uart/uart.c:524` — UART TX ring indices
  read-modify-written from process + IRQ context with no lock → `count` underflow
  spews the ring / wedges the console.
- **[ ] DRV-10** `sys/drivers/console/tty.c:884` — non-canonical read never arms a
  timed wakeup → `MIN=0,TIME>0` read blocks forever when the line goes idle.
- **[ ] DRV-11** `sys/drivers/audio/audio.c:395` — `/dev/audio` exclusivity keyed
  per-process, not per-thread → two threads of one process race the SPSC FIFO.
- **[ ] DRV-12** `sys/drivers/audio/ac97.c:414` + `hda.c:563` — `_close` resets
  ring/FIFO state without `feed_lock`, racing the IRQ feeder (SMP).
- **[ ] DRV-13** `sys/drivers/storage/blkdev.c:194` — a write to the raw disk node
  doesn't invalidate the partition's cached copy of the same sector → stale reads;
  `blkdev_geom_read` also bypasses the cache and the `dead` check.
- **[ ] DRV-14** `sys/drivers/storage/scsi/scsi_dev.c:289` — partition blkdevs (and
  their bio caches + mounts) are never torn down when the parent disk detaches.
- **[ ] DRV-15** `sys/drivers/virtio/virtio_blk.c:86` — `virtio_blk_setup`'s ring
  math can never succeed → the driver never reaches DRIVER_OK (dead code).
- **[ ] DRV-16** `sys/drivers/usb/usb_msc.c:90` — 128 KiB direct chunk exceeds the
  EHCI (20 KiB) / xHCI (64 KiB) per-transfer limits → large I/O fails.
- **[ ] DRV-17** `sys/drivers/usb/uas.c:165` — UAS status IUs are never matched
  against the command tag → a late status IU is misattributed.
- **[ ] DRV-18** `sys/drivers/usb/uhci.c:597` — on timeout, TDs are freed and the
  data buffer unmapped within the HC's current-frame window → DMA into a reissued TD.
- **[ ] DRV-19** `sys/drivers/usb/xhci.c:253` — `xhci_setup_slot` leaks slot + DMA
  contexts on any partial failure → 16 flaky enumerations exhaust all slots.
- **[ ] DRV-20** `sys/drivers/usb/usb.c:27,837` — the `/proc/devtree` device node and
  usbdevfs nodes are never removed on disconnect → leak per hotplug cycle.

### libc
- **[ ] LIBC-04** `lib/c/src/stdlib.c:803` — `strtoul("-2")` returns the wrong value
  and a spurious `ERANGE` (64-bit clamp applied to a negated in-range value).
- **[ ] LIBC-05** `lib/c/stdio/stdio_core.c:80` — the global open-FILE list is
  mutated by `fdopen`/`fclose` and walked by `fflush(NULL)` with no lock.
- **[ ] LIBC-06** `lib/c/src/stdlib.c:103` — `abort()` is `_exit(134)`; never raises
  SIGABRT, so handlers don't run and the parent sees WIFEXITED not WIFSIGNALED.
- **[ ] LIBC-07** `lib/c/stdio/stdio_core.c:725` — `pclose` returns `WEXITSTATUS`
  instead of the raw wait status POSIX requires.
- **[ ] LIBC-08** `lib/c/stdio/stdio_core.c:113` — `fclose` ignores the flush result
  → data lost to ENOSPC/EIO at close is reported as success.
- **[ ] LIBC-09** `lib/c/stdio/scanf.c:226` — `%a` on `"0"` consumes the leading `0`
  and returns EOF instead of assigning 0.0.
- **[ ] LIBC-10** `lib/c/src/time/time.c:456` — `mktime` treats the tm as UTC (no TZ)
  and breaks pre-1970 dates.

---

## LOW

- **[ ] DRV-21** `sys/drivers/console/tty.c:1177` — TTY permission failures return
  bare `-1`, which aliases the driver-forward sentinel (op silently forwarded).
- **[ ] DRV-22** `sys/drivers/input/keyboard.c:308` — PS/2 + USB-HID share non-atomic
  modifier globals → transient wrong-modifier characters.
- **[ ] NET-09** `sys/net/arp.c:35` — ARP cache read/written across IRQ/process with
  no lock → torn MAC read.
- **[ ] NET-10** `sys/net/af_unix.c:1828` — `getsockname` signed-underflow write with
  a user `addrlen` of 0/1.
- **[ ] NET-11** `sys/net/tcp.c:341` — retransmit victim list silently capped at 32.
- **[ ] ARCH-03** `sys/arch/i386/signal.c:597` — `sigreturn` EFLAGS filter doesn't
  mask NT despite the comment.
- **[ ] FS-11** `sys/fs/ext2/ext2.c:3314` — `ext2_symlink` slow path leaks the data
  block on a short write and leaves the cache slot populated.
- **[ ] FS-12** `sys/fs/ext2/ext2.c:3446` — `ext2_unlink`/`rmdir`/`mkdir` deref
  `dir->impl` before the `!dir` NULL guard.
- **[ ] LIBC-11** `lib/c/stdio/printf.c:116` — `etoa` normalizes before rounding →
  `printf("%e",9.9999999)` emits `10.000000e+00`.
- **[ ] LIBC-12** `lib/c/src/dirent.c:48` — `readdir` refill doesn't negate the
  kernel's `-errno` → a real failure is indistinguishable from EOF, stale errno.
- **[ ] KERN-13** `sys/kern/acct.c:100` + `time.c:391,396,425` — bare `-1` on
  (currently unreachable) NULL guards.
- **[ ] KERN-14** `sys/kern/ioremap.c:15` + `ksyms.c:24` + `isa.c:16` — manual
  `extern` prototypes in .c files (project directive violation).
- **[ ] VM-13** `sys/arch/i386/pmap.c:1123` — large-page PT page freed via
  `vm_phys_free_page`, bypassing `pmm_free_block` memtrack accounting.
- **[ ] VM-14** `sys/vm/uma_core.c:986` — `uma_zfree` slow path decrements `uz_count`
  with no `>0` guard → underflow to ~4 billion on a stray free.
- **[ ] ARCH-04** `sys/arch/i386/pmap.c:1752` — TLB-shootdown globals unlocked → two
  concurrent shootdowns clobber each other's target/ack state.
- **[ ] VM-15** `sys/vm/vm_map.c:1128` — `vm_map_protect` ignores `pmap_protect`'s
  return, leaving PTEs unchanged for the COW / non-current-pmap cases.

---

## Verified NOT defective (excluded after investigation)

- Per-CPU runqueue / work-stealing races (`sched_smp.c`, `runqueue.c`): unreachable
  dead code today — `sched_enqueue` never runs with an empty runqueue.
- `ts_lock` missing `preempt_disable` (`turnstile.c:43`): masked — every turnstile
  call site runs under the `lk_interlock` spinlock.
- Block cache cross-device collision: ruled out — cache is per-(vnode,blkno) keyed
  and write-through coherent (`bio_dev_invalidate` on write).
- ELF PT_LOAD loader: thoroughly hardened (phoff/filesz/vaddr overflow, overlap,
  kernel-space rejection).
- Buddy allocator (`phys_mem.c`): internally consistent, correctly lock/IRQ-protected.
- `calloc`/`fread`/`fwrite` overflow checks; `strlcpy`/`snprintf` truncation: correct.

---

## What's solid

ELF loader hardening; the PMM buddy allocator + `0xC0000000` virtual/physical
discipline; the always-on heap tripwires; TCP modular-sequence arithmetic and the
single-reaper PCB model; AF_UNIX refcount + SCM_RIGHTS accounting; FAT/exFAT cycle
guards; the sleepq/turnstile/mutex/futex *core* discipline (enqueue-before-release,
re-check-after-wake) with its OPTS-driven fixes; the buffer cache's write-through
coherence and per-(vnode,blkno) keying; framebuffer bounds checks; libc
`calloc`/`snprintf`/`strlcpy` correctness. No banned unsafe string functions appear
outside the libc implementations themselves.

---

## lib/m/ and lib/sys/ findings

libsys is clean — the `syscall.S` stack/ABI discipline is correct (the documented
nr-slot / return-address bugs are genuinely fixed, esp is balanced), and every
typed wrapper routes negative-errno through `__sysret`. libm's core is solid. The
real defects are a family of premature overflow thresholds plus low-impact C23
edge cases.

- **[ ] MATH-01** `lib/m/src/math_exp.c:107` — MEDIUM — `exp2()` overflow guard at
  `x > 1023.0`, but `log2(DBL_MAX) ≈ 1023.9999`, so `exp2(1023.5)` returns INFINITY
  + ERANGE instead of the representable `≈1.27e308`.
- **[ ] MATH-02** `lib/m/src/math_exp.c:508` — MEDIUM — `pow()` uses the same too-low
  `yl2x > 1023.0` cutoff → `pow(2.0, 1023.5)` returns INFINITY + ERANGE for a finite
  result.
- **[ ] SYS-01** `lib/sys/select.c:51` — LOW/MEDIUM — poll-timeout conversion done in
  64-bit then stored into `int poll_timeout`; a `tv_sec` above ~24 days truncates to
  a negative int → `poll` treats it as infinite → `select()` blocks forever.
- **[ ] MATH-03** `lib/m/src/math_totalorder.c:98,115` — LOW — `totalorderl`/
  `totalordermagl` cast `long double`→`double`, collapsing 11 mantissa bits → two
  distinct 80-bit values report order-equal (violates IEEE totalOrder).
- **[ ] MATH-04** `lib/m/src/math_totalorder.c:299,317` — LOW — `setpayloadl` sets
  byte 9 bit 7 (the **sign** bit, not the mantissa MSB) → every result is a negative
  pseudo-NaN; `setpayloadsigl` shares a payload-duplication bug.
- **[ ] MATH-05** `lib/m/src/math_narrowing.c:183` — LOW — `ffma(double,...)` casts
  operands to float before `fmaf`, discarding precision before the multiply → C23
  narrowing contract violated.
- **[ ] MATH-06** `lib/m/src/math_trig.c:296,304` — LOW — `sinh`/`cosh` inherit
  `exp`'s overflow at `x>709.78`; true overflow is `~710.47`, so inputs in
  `(709.78, 710.47)` spuriously return INFINITY + ERANGE.
