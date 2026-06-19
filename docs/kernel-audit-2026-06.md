# Substrate kernel audit — June 2026

Six-subsystem code audit (scheduler/sync, VM/pmap, VFS/FS, net/IPC, drivers,
syscalls/exec/arch). Findings are verified against the code; each line notes
file:line and a fix. Ordered by priority. P0 = desktop responsiveness
(the "slow/stuttery, low user-CPU%" symptom — latency / kernel-CPU bound).

## P0 — desktop responsiveness (latency / kernel-CPU)

- [ ] **poll/select lost-wakeup → up to ~50 ms per round-trip** (THE root cause;
  found independently by the scheduler *and* networking auditors).
  `kern_poll` (sys/kern/syscall.c:~2003-2056) discards the per-fd wait channel
  (`(void)this_chan`), sleeps on the single global `&g_poll_wake_chan` with an
  `HZ/20` (~48 ms) backstop, and `sched_poll_wake_pollers()` only readies
  threads already `THREAD_BLOCKED`. A wake that lands while the poller is
  RUNNING mid-scan is lost and recovered only by the backstop. The X server is
  poll-driven, so every event that races the scan stalls ~50 ms.
  **Fix:** prepare-to-wait — set `THREAD_BLOCKED` *before* a final fd re-scan so
  a racing wake flips the thread READY (re-runs immediately); a wake can no
  longer be lost. Do NOT use a global re-scan-on-any-seq-change (spins under a
  128-poller herd). Then the backstop can be raised/removed.
- [ ] **AHCI holds a spinlock across the whole polled DMA** (ahci.c:446-499) —
  `preempt_disable()` for the entire transfer; no completion IRQ. Every SATA
  page-fault/program-load freezes scheduling system-wide for the I/O.
  **Fix:** IRQ-driven completion + `sched_sleep_until`; lock only slot setup.
  (Interim: `sched_yield()` in the poll loop.)
- [ ] **virtio-blk pure-spins on the used ring, no timeout, no IRQ**
  (virtio_blk.c:175) — full CPU burn per block read + infinite-hang hazard.
  **Fix:** used-event IRQ + `sched_sleep_until` with deadline → -EIO. (Interim:
  `sched_yield()` in the loop.)
- [ ] **NVMe spins on the CQ phase bit** (nvme.c:606) — has a timeout but burns
  a core per I/O; per-I/O `dma_alloc_coherent`+memcpy bounce (nvme.c:914).
- [ ] **ext2 metadata flush storm** (ext2.c:2324/2375/2439/2543 via
  `ext2_flush_super`) — every block/inode alloc/free synchronously rewrites the
  primary superblock **and every sparse-super backup**, write-through (the
  `B_DELWRI`/syncer write-back path in bio.c is dead code). Writing one small
  file = several full superblock+backup flush storms straight to disk.
  **Fix:** coalesce super/bgd flushes (mark dirty in-core, flush on syncer
  tick/unmount); don't flush under the directory node lock (ext2.c:2683).
- [ ] **VFS lookup has no live name cache** (vfs_cache.c is dead code) — every
  open/stat/exec does an uncached per-component `finddir` walk + a per-component
  `mountlist` linear scan + `vfs_mount_lock` round-trip (vfs.c:553-566), and
  non-native (TDE/X/Linux) processes re-walk the path 2-3× via double `/perso/`
  shadowing (vfs.c:479-504). Dominant read-side kernel CPU for a file-bound
  desktop. **Fix:** wire a name cache into `vfs_lookup`; fold the mount check.
- [ ] **vm_fault O(n) map-entry scan per page fault** (vm_fault.c:85-94) — hand
  walk instead of the existing splay-tree `vm_map_lookup_entry`. Every minor
  fault pays it under the read lock. **Fix:** use `vm_map_lookup_entry`.
- [ ] **read_fs/write_fs stamp 64-bit timestamps per op** (vfs.c:289 read,
  vfs.c:301 write) — get_time() is now cheap (fixed) but still unconditional;
  add relatime/noatime semantics to drop the per-read atime write.
- [ ] **Keyboard LED update busy-spins inside IRQ1, IF=0** (keyboard.c:330 via
  handle_lock_key; also vt.c VT-switch) — ~5 PS/2 PIO handshakes (each up to
  100k spins) on every Caps/Num/Scroll-Lock and Alt+Fn. **Fix:** defer LED I/O
  to a kthread / next process-context read.
- [ ] floppy `fdc_wait_irq` spins despite a working IRQ wake channel
  (floppy.c:156 — one-line: sleep on `ctlr->irq_seen`); scsi_delay_ms /
  fdc_delay_ms busy-spin (should `sched_sleep_until`).
- [ ] af_inet `afi_node_nonblock` linear-scans all 4096 fds per TCP read/write
  (af_inet.c:350) — stash O_NONBLOCK on `io_file` like AF_UNIX does.
- [ ] **DONE** get_time/get_uptime[_ms] per-call 64-bit divide (committed 899ef01c).
- [ ] **DONE** uhci_poll_td pure pause-spin → yield (committed b764bd97).

## P1 — correctness / data-loss

- [ ] **CRITICAL: ext2 group-descriptor flush uses 32-byte stride on 64BIT
  (desc_size=64) filesystems** (ext2.c:396,406) — corrupts the on-disk GDT on
  any INCOMPAT_64BIT mount (the driver accepts these). Use `fs->desc_size`.
- [ ] **ext2 FS-wide allocator has no SMP serialization** (ext2.c:2268+/2341+/
  2380+/2465+) — two CPUs growing different files can hand the same block to two
  inodes + lose `bg_free_*` updates. Add an `ext2_fs_t`-level alloc mutex.
- [ ] ext2 directory `i_blocks` double-counted on growth (ext2.c:899 + 2811).
- [ ] **scheduler priority direction is inverted** (sched.c:334 picks the
  *highest* number; nice/decay/turnstile use *lowest* = highest). Dormant today
  only because the MLFQ decay/interactivity engine is **never called** (dead
  code — sched_decay.c / sched_interactivity.c have no live callers). nice(2)
  vs setpriority(2) also disagree (compat.c:159 vs syscall.c:4028). Fix the
  direction + unify the convention, THEN wire decay into sched_tick.
- [ ] COW fast-path reads non-atomic `vm_page` ref_count lock-free (pmap.c:2101;
  vm_page hold/unhold are plain ++/--) — SMP corruption window. Make atomic.
- [ ] FIFO open() blocking loops use the unsafe sleepq-add-while-holding-lock
  pattern the pipe_wait fix removed (pipe.c:557-563,592-597). Route via pipe_wait.
- [ ] VFS mountlist walked lock-free while unmount kfree()s the entry
  (vfs.c:555 vs 1223) — UAF window. Take vfs_mount_lock around the walk.

## P2 — security

- [ ] **psignal() runs in timer-IRQ ctx but takes the non-IRQ-safe sleepq
  spinlock** (signal.c:656/664 → sleepq.c:154 sq_lock) — same-CPU self-deadlock
  (invisible freeze). Give the IRQ wake path a try-acquire / deferred AST.
- [ ] **`sig > NSIG` off-by-one → `sigprop[32]` OOB read** (signal.c:540/603/
  743/670/809), reachable via `kill(pid,32)`. Change `>` to `>=` (5 sites).
- [ ] **FreeBSD personality signal layer has NO signo translation or sigaction
  struct marshalling** (perso_freebsd.c:58/130/135) — wrong-signal delivery +
  garbled `struct sigaction` (native 12 B vs FreeBSD 24 B, flags/mask swapped).
  Clone the (correct) NetBSD wrappers.
- [ ] sys_mmap returns flat `(void*)-1` for all errors → all become EPERM
  (vm_syscalls.c:235-313). Return distinct negative errnos.
- [ ] user-triggerable panic on bad user ESP after a syscall (syscall.c:586) —
  deliver SIGSEGV, don't panic (DoS).
- [ ] sigreturn lets user set NT (eflags bit 14) despite the comment
  (arch/i386/signal.c:544); #DF handler has no task gate/IST (idt.c vec 8) —
  a kstack overflow double-faults → triple-fault reboot, no panic.
- [ ] kern_sigsuspend doesn't strip SIGKILL/SIGSTOP from the temp mask
  (signal.c:264); gettimeofday tail-padding info leak (time.c:469).

## P3 — leaks / minor

- [ ] AF_UNIX socket struct (~4.4 KB) intentionally leaked on close
  (af_unix.c:529) — ref-count vs in-flight sleepq waiters, then kfree.
- [ ] exec image+fd leaked on the E2BIG arg path (elf.c:1464).
- [ ] TCP timer constants calibrated for HZ=128 but HZ=250 (tcp.c:68) — derive
  from get_hz(); the 32 ms TCP_SLEEP_POLL backstop has the same lost-wake class.
- [ ] MAX_SYMLINK_DEPTH=4 < POSIX min 8 (vfs.c) — move ppath off-stack, raise.
- [ ] per-invlpg global atomic stats counter in the fault hot path (pmap.c:1553).

## Completed (June 2026 follow-up round)

ext2:
- [x] CRITICAL 64-bit-desc GDT stride (ext2.c:396) — stride by fs->desc_size (commit 5fbd1c57)
- [x] SMP/UP allocator race — per-fs alloc_lock around alloc/free (commit 5fbd1c57,
      verified ext2_concurrent 4x40 files, 0 mismatches)
- [x] metadata flush storm — deferred/coalesced super+bgd flush (commit 891cc9c0)

VFS:
- [x] per-component mount spinlock — skipped for non-mountpoint nodes (commit 9e18a7ca)
- [x] ".." across a mount root — escapes to the mountpoint's parent, /proc/../etc
      now works like Linux (commit 1b8b50a1)
- [x] finddir name cache — the audit was wrong that there is "no live name cache":
      ext2 has a per-directory name->inode dcache (ctx->dcache).  It cached only
      FOUND entries, so absent-name lookups (linker/PATH probing many dirs, the
      /perso/ shadow walk) re-ran a full linear scan every time.  Added NEGATIVE
      caching (sentinel inode 0xFFFFFFFF; existing add/remove invalidation drops
      it on create) + bumped 16->32 slots.  Measured on a TDE boot: hit rate
      85% -> 94%, absent-name full scans down ~88% (commit f6f999ee).  The cache
      lives in ext2 (node lifetime is managed there; stores inodes, not pointers),
      sidestepping the VFS-level fs_node UAF problem entirely.
- [ ] IMAGE (not kernel): the rootfs advertises dir_index but no directory has
      EXT2_INDEX_FL set (Flags 0x0) — debugfs/e2tools don't build htree indexes,
      so positive lookups in big dirs (/usr/lib, ...) are linear scans
      (htree_hit=0).  `e2fsck -D rootfs.img` or building indexes at image
      creation would make positive lookups O(log) — the remaining load-time win,
      free, no kernel change.  (substrate's htree read handles single-level
      indexes; multi-level falls back to linear.)
- [ ] STILL OPEN: the /perso/ double-walk for non-native processes (vfs.c:479) and
      the metadata_csum recompute-on-flush (ext2.c) — both lower priority.
