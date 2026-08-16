# Substrate Development Changelog

Detailed record of major implementation milestones. For current system status, see `AGENTS.md`.

## Kernel Core

### Memory Management
- **PMM Hardening:** Phase 1 boot memory detection with sanitization, total RAM reporting, and proper kernel bounds.
- **Per-Process Address Spaces:** Implemented `pmap_create()`, `pmap_destroy()`, `pmap_reference()`, `pmap_release()`, `pmap_fork()` with COW support. Global pmap list for TLB management. Full 3GB/1GB user/kernel split.
- **PMM Virtual Address API:** `pmm_alloc_block()` and `pmm_alloc_contiguous()` now return kernel virtual addresses (phys + 0xC0000000) instead of physical addresses. `pmm_free_block()` and `pmm_free_contiguous()` expect virtual addresses. Updated all callers in: `pmap.c`, `elf.c`, `sched.c`, `process.c`.
- **UMA Integration:** Integrated FreeBSD-style Universal Memory Allocator (UMA) for kernel memory allocation. `kmalloc`/`kfree` now backed by UMA zones via `vm_kmem.c`. Added `uma_startup()` before `kmem_init()`.
- **Demand-paged user stack:** `exec` maps only a small region (128 KiB) at the top of the stack and records `[ustack_limit, ustack_top)` on the process; the page-fault handler grows the stack one page at a time on access (8 MiB ceiling). A process costs only the stack it touches instead of a fixed 4 MiB reservation, so deep fork/exec chains no longer exhaust RAM.
- **memtrack** (`sys/kern/memtrack.c`): per-call-site physical-page accounting — every `pmm_alloc_*` / `pmm_free_*` is charged to the caller's return address, giving a pages-allocated-vs-freed table per code path. Exposed via `/proc/memtrack` and `sys_vm_slabs(2)`.
- **16 KiB kernel stacks:** per-process kernel stacks are 16 KiB (4 PMM pages). 8 KiB overflowed: a deep network TX syscall path (`sys_write` → `tcp_send` → … → `rtl_xmit`) can take a nested NIC IRQ that runs the whole RX → IP → TCP input path on the same stack, and the combined depth scribbled the adjacent `kmem-64` slab (`vm_object` / `vm_map_entry` structs) — surfacing as non-deterministic SIGSEGVs and panics in unrelated processes. `kern/kthread.c` stacks were already 16 KiB; `sched.c` now matches.
- **VM kernel-heap corruption tripwires** (`sys/vm/`): a `vm_object` magic canary (use-after-free / scribble / `ref_count` underflow), a buddy-allocator double-allocation detector (`PG_PMM_ALLOC`), a UMA per-item double-free guard, and `vm_map_audit` (validates every `entry->object` after each map mutation). Each converts silent kernel-heap corruption into an immediate, located `panic()`.

### Process & Scheduling
- **Process Model Refactor:** Separated Swapper (PID 0) and Init (PID 1). Enforced `PID == Main_TID` invariant. Added Process Group (`pgrp`) and Session support.
- **Scheduler Refactor (MLFQ):** Implemented Multilevel Feedback Queue with Realtime, Timeshare, and Idle classes.
- **SMP Scheduler:** Per-CPU runqueues, Work Stealing load balancing, CPU Affinity support, and IPI-based preemption.
- **Kernel Process:** Implemented Swapper/Idle (PID 0) with pageout daemon and idle loop responsibilities.
- **Context Switching:** Validated FPU Lazy Save and refined PCB for thread/process separation.
- **Init Safety:** Kernel now catches `init` process exit (e.g., from detached stdin) and enters an idle loop instead of panicking.
- **FreeBSD-compatible thread syscall set:** `thr_new` (455), `thr_exit` (431), `thr_self` (432), `thr_kill` (433), `thr_suspend` (442), `thr_wake` (443), `thr_join` (457), `thr_set_name` (464), `thr_kill2` (481). Per-thread `sig_pending`/`sig_mask` already drove signal delivery; the new entry points expose it to libpthread for cancellation, naming, and parking. Added a `thread_t.name[16]` field and a `THREAD_F_WAKE_PENDING` flag for race-free suspend/wake.

### Synchronization
- **Synchronization Primitives:** Implemented Turnstiles (Priority Inheritance) and Hashed Sleep Queues (O(1) lookup).
- **Synchronization Improvements:** Updated `mutex` and `semaphore` to use `sleepq` for robust thread sleeping (removed ad-hoc `sched_sleep`).
- **VFS Concurrency & Locking:** Implemented BSD-style `lockmgr()` lock manager (`sys/kern/lockmgr.c`) with `struct lock` supporting LK_SHARED, LK_EXCLUSIVE, LK_UPGRADE, LK_DOWNGRADE, LK_DRAIN, LK_NOWAIT, and priority inheritance via turnstiles. Refactored vnode locking (`vn_lock`/`vn_unlock`) to delegate to `lockmgr()`. Wired name cache rwlock and mount point rwlock.

### Inter-Process Communication
- **System V IPC:** semaphores (`sys/kern/ipc_sem.c`) and shared memory (`sys/kern/ipc_shm.c`) — `semget`/`semop`/`semctl` and `shmget`/`shmat`/`shmdt`/`shmctl`, fixed tables with key→id lookup and per-slot sequence numbers, `ipc_perm` checks, interruptible blocking, `SEM_UNDO`.
- **POSIX Message Queues:** `sys/kern/posix_mqueue.c` — a named, cross-process kernel subsystem modelled on the System V IPC code. A fixed table of `MQ_OPEN_MAX` queues (each with an `ipc_perm`, seq/slot id, reference count, `mq_attr` state, and a priority-ordered message list) plus a `MQ_DESC_MAX` open-descriptor table (`mqd_t` = `seq*MQ_DESC_MAX + index`; per-open `O_NONBLOCK`). Blocking `mq_timedsend`/`mq_timedreceive` via an interruptible sleep queue honouring `O_NONBLOCK` (`EAGAIN`), an absolute `CLOCK_REALTIME` deadline (`ETIMEDOUT`), and `EIDRM` on unlink-while-blocked; messages ordered highest-priority-first, FIFO within a priority. Single-registration `mq_notify` delivers `SIGEV_SIGNAL` on an empty→non-empty transition when no receiver is blocked (one-shot; cleared on delivery, close, or unlink). Seven native syscalls (`SYS_MQ_OPEN`=409 … `SYS_MQ_GETSETATTR`=414, plus `SYS_MQ_CLOSE`=415) with typed trace names; `mq_init()` from `kmain`, `mq_proc_cleanup()` from `proc_exit`. Functional test `tests/lib/mqueue/mqtest.c` (19 assertions incl. priority ordering, EAGAIN, ETIMEDOUT, cross-process fork round trip, and `SIGEV_SIGNAL` delivery).

### Signals & TTY
- **TTY Integration:** Per-process controlling terminal support.
- **TTY Signals:** Implemented signal generation from TTY (`SIGINT`, `SIGQUIT`, `SIGTSTP`) and group signal delivery (`signal_send_group`).
- **Syscall Tracing:** Enhanced `syscall_trace` with names, typed arguments (int/hex/ptr/str), return values, and Personality details.
- **`psignal()` ignore-disposition discard:** a signal whose effective disposition is "ignore" is discarded rather than left pending — a pending-but-ignored signal otherwise aborts every interruptible sleep.
- **TTY write-side flow control:** the tty write path now blocks (rather than silently dropping output) when the output buffer overflows, draining once before it would drop. Fixes truncated `dmesg` piped to a pty.
- **PTY last-close linger buffer:** a pty delivers a writer's final bufferful across the last close via a per-pair linger buffer (`sys/drivers/console/pty.c`), so the master still reads the tail after the slave hangs up.

### Time
- **Time System:** 64-bit time_t, RTC driver, gettimeofday/clock_gettime syscalls.
- **Filesystem Timestamps:** Added atime/mtime/ctime tracking with atomic updates.

## VFS & Filesystems
- **VFS Hard Link Support:** Implemented `link` in VFS and hooked up `sys_link` across native, Linux, and FreeBSD personalities. Improved ABI detection for stack-based syscalls.
- **VFS Unlink Support:** Implemented `unlink` in VFS and hooked up `sys_unlink` across native, Linux, and FreeBSD personalities.
- **UDF Filesystem Driver:** Complete read-write UDF (Universal Disk Format) driver per ECMA-167/OSTA spec. On-disk structures in `udf.h`, read-only support in `udf.c`, write support in `udf_write.c`, with unit tests and man pages (`udf.4`, `udf.5`).
- **exFAT Filesystem Driver:** `sys/fs/exfat/` is a read-write exFAT driver. Read side: readdir / finddir / read with the up-case table loaded at mount for case-folded name comparison. Write side: write, truncate, mkdir, mknod / `O_CREAT`, unlink, rmdir, rename — maintaining the allocation bitmap, the FAT cluster chains, and directory entry-set `SetChecksum` + `NameHash`.
- **exFAT audit hardening (35 findings):** a full spec-conformance + correctness pass against the Microsoft exFAT Revision 1.00 specification. Memory safety and concurrency: the shared node cache no longer aliases the root across mounts or hands out torn/recycled slots, and every metadata mutation runs under a per-mount lock (previously a lock-free TOCTOU that cross-linked the volume under concurrent writers). On-read validation now enforced (all "SHALL verify" per spec): the §3.4 Boot Checksum (with backup-region fallback), the §3.1/§9 geometry bounds, the §6.3.3 directory `SetChecksum`, the §7.2.2 up-case `TableChecksum`, and the allocation-bitmap coverage bound — corrupt/hostile media now fails closed instead of driving mount-time DoS or mass entry-set deletion. Data-integrity: newly allocated data clusters are zeroed (freed-residue was readable as file contents, §7.6.5); `mv a A` no longer frees the file's own clusters; a file unlinked while open defers its chain free; rename refuses moving a directory into its own subtree; §8.1 delete ordering (entry before chain) restored; VolumeDirty is set at mount / cleared at unmount. Cluster indices carried as 64-bit (large-offset truncation), every FAT walk bounded (cyclic-chain DoS), invalid `FileName` characters and overlong-UTF-8 `/` injection rejected. Validated end-to-end: substrate-written volumes pass `fsck.exfat` clean. Detail in each `exfat-audit` commit.
- **ext2/3/4 audit hardening (78 findings):** a full conformance pass against the ext4 on-disk specification (kernel.org `Documentation/filesystems/ext4/ondisk/`), report in `docs/ext2-audit-2026-08.md`. Four defects were corruption-class on *default* `mkfs.ext4` images: lazy-init block groups were unmodeled, so the allocator read an uninitialized bitmap as authoritative and handed out the group's own backup superblock and GDT blocks (and, never clearing `BLOCK_UNINIT`, let Linux re-allocate the same blocks to another file); the maximal initialized extent length 32768 decoded as 0, so those ranges read as holes; `metadata_csum` volumes mounted read-write while only the per-inode checksum was ever recomputed, so the first free-count flush left the superblock, group descriptors, bitmaps and directory tails checksum-stale and the volume failed its own next mount; and `rename(a, a)` deleted the file. Write-side checksums are now complete (including a new `crc16` for the older `gdt_csum` scheme, which had been accepted but never implemented in either direction), extent-tree teardown makes `rm` work on ext4 files, and teardown ordering commits the emptied inode before freeing any block so a crash leaks rather than cross-links. Also: spec-correct `i_extra_isize` (the old value was misaligned and clobbered inline xattrs), dirent `file_type` gated on `INCOMPAT_FILETYPE`, `META_BG` refused rather than silently corrupted, 64BIT `s_blocks_count_hi` checked, symlink inline boundary, 32-bit uid/gid, both device-number encodings, creator credentials, immutable/append flags, relatime, real `st_nlink`/`st_blocks`, `s_state` management, `sync(2)` reaching deferred metadata via a new VFS `syncfs` op, reserved-block enforcement, xattr block refcount release, xattr permission gating, and partial truncate. Validated end-to-end: substrate-written volumes pass `e2fsck -fn` clean.
- **Block-level read cache:** the buffer cache is keyed at the block-device layer — `blkdev_do_read`/`do_write` (`sys/drivers/storage/blkdev.c`) route through `bio_dev_get`/`release` keyed by `(struct blkdev *, sector)` with read coalescing and write-through, so caching works automatically for every storage driver with no driver changes and the filesystems carry no caching logic. The old per-fs `bcache[]` array is gone; `blkdev_unregister` calls `bio_dev_purge`. See `docs/design/block-cache-consolidation.md`.
- **readdir/getdents byte-offset cookies:** `struct dirent` carries a `uint64_t d_off`; ext2 readdir is byte-offset based and getdents/getdents64 advance by it — fixes `rm -rf` needing multiple passes on large directories (the dir-index cookie no longer collides).

## Drivers

### Video & Framebuffer
- **Framebuffer:** Implemented native linear framebuffer driver (`fb.c`) with Multiboot support and bitmap font console. Added Bochs Graphics Adapter (BGA) native driver support via `video=bga`.
- **Framebuffer Mode Selection (`vga=`):** Added `vga=WxH@BPP` kernel command line parameter for framebuffer mode selection across all video drivers. Supports legacy CGA/EGA/Hercules/VGA modes, BGA set_mode, multi-framebuffer device registry (`/dev/fb0`..`/dev/fb7`), and GRUB framebuffer inheritance.
- **Framebuffer Rendering Subsystem:** Full rendering pipeline in `sys/drivers/video/`. PSF1/PSF2 font parsers (`psf.c`) with auto-detection and Unicode table extraction. BDF/PCF bitmap font parsers (`bdf_pcf.c`) with hex-to-binary glyph conversion and PCF TOC navigation. Font glyph cache (`font_cache.c`) with FNV-1a hash table (256 buckets) and UTF-8 Unicode mapping from PSF1/PSF2 tables. Blitting operations (`fb_ops.c`): `fb_fillrect()` with ROP_COPY/ROP_XOR, `fb_copyarea()` with overlap-safe memmove, `fb_imageblit()` for mono/color images — all with 32bpp fast paths and generic putpixel fallback, plus viewport clipping. Character rendering attributes (`fb_console.c`): `fb_putc_attr()` supports bold (shift-and-OR double-strike), italic (quarter-height shear transform), underline, strikethrough, and reverse video.

### Input & Storage
- **PS/2 Subsystem:** Expanded PS/2 controller driver to support dual-channel (Mouse/Aux) operation.
- **VirtIO Drivers:** Implemented Core VirtIO, Block Device (virtio-blk), and 9P Transport (virtio-9p) drivers.
- **PS/2 mouse:** IntelliMouse (3-button + wheel) and IntelliMouse Explorer (5-button + wheel) detection via the Microsoft sample-rate knock sequence; 4-byte packet decoding; `BTN_SIDE` / `BTN_EXTRA` / `REL_WHEEL` emitted on the input layer.
- **PS/2 keyboard:** runtime-switchable Set 1 (translated XT, default) and Set 2 (AT, native) scancode decoders with a `0xF0` break-prefix state machine and an E0-prefix path that handles both encodings.
- **Block device registration size print:** `blkdev_register` prints its size as a 64-bit value, so a 4 GiB disk no longer logs "0 bytes".

### USB
- **USB device enumeration / `lsusb`:** USB devices are now exposed both under
  `/proc/devtree` and as `/dev/usb` nodes via `sys/drivers/usb/usbdevfs.c`, so
  `lsusb` enumerates attached devices end-to-end.
- **USB HID mouse:** boot-protocol HID mouse support.

## Architecture & Boot
- **ACPI RSDP from the bootloader (UEFI SMP fix):** `smp_discovery.c` only ever
  scanned the legacy 0xE0000-0xFFFFF BIOS window for the RSDP, which does not
  exist on UEFI firmware — so a UEFI boot printed `SMP: ACPI RSDP not found.`,
  found no MADT, registered no IO-APIC and stayed uniprocessor forever, even on
  SMP hardware.  The multiboot2 parser in `main.c` was explicitly discarding the
  `ACPI_OLD`/`ACPI_NEW` tags that GRUB hands us for exactly this purpose.  It now
  *copies* the RSDP out of those tags (it must copy — the multiboot info is freed
  during boot) and exposes it as `multiboot_get_acpi_rsdp()`; `smp_discovery.c`
  falls back to it when the legacy scan comes up empty.  UEFI now reports
  `SMP: ACPI RSDP from bootloader (UEFI).`, finds the MADT, registers the
  IO-APIC and brings APs online.  BIOS boot is unchanged (legacy scan still wins).
- **FPU State Tracking:** Lazy FPU context switching with FXSAVE/FXRSTOR.
- **Early Boot Debugging:** Added early GDT+IDT handler in `main.c` using `early_uart_print()` for exception debugging before full console is available.
- **LAPIC Early Mapping:** Added LAPIC identity-mapping (0xFEC00000-0xFFFFFFFF) in `boot.S` page tables with PCD flag for MMIO.
- **GRUB Boot Fix:** Fixed multiboot header video mode field offsets (were at 12-24, spec requires 32-44). Kernel now boots through GRUB for the first time.
- **PT_TLS Support:** ELF loader now handles PT_TLS segment, allocates TLS block, sets GDT entry 6 for GS-based TLS access.

## Exec & Personality
- **SCO-X/286 personality (`PERS_SCO_X286`):** runs SCO Xenix/286 binaries — 16-bit protected-mode System V.2 programs in the segmented `x.out` format. New loader `sys/exec/formats/xout286.c` maps each x.out segment into its own naturally-aligned 64 KiB linear window and fills the LDT slot the linker baked into the binary with a 16-bit (D/B=0), byte-granular descriptor, so a middle-model image's `lcall $0x47,$off` resolves exactly as it did on hardware; the first data segment is DGROUP (DS==ES==SS) with the break growing up from bss and the stack down from the top of the same 64 KiB. `sys/exec/perso/perso_sco_x286.c` emulates the Xenix syscall ABI: the trap is `int $5` (caught as the #GP it raises against a DPL-0 gate), the call number is in AX with a sub-function in AH (call 40 is the Xenix multiplexer — `brkctl`, `ftime`, `nap`, `rdchk`, `chsize`, `locking`, `stkgrow`; call 57 is `utssys`), arguments are in BX/CX/SI/DI, and results come back in AX with the high half in BX (`getpid`/`getppid`, `getuid`/`geteuid`, `pipe` and `wait` all use that pair). `fork` is told apart in the child by BX==0, staged into the trap frame before the fork since the child never re-enters the handler. Translates Xenix↔substrate `open` flags (including for `fcntl(F_GETFL/F_SETFL)`), the termio ioctl group and its 17-byte `struct termio`, the 30-byte Xenix `struct stat`, signal numbers, and — because Xenix/286 has no `getdents(2)` — synthesizes 16-byte V7 `struct direct` records for `read(2)` on a directory. V7-style signal delivery pushes a far-call frame (signo, interrupted CS:IP) on the program's own 16-bit stack so the handler's `lret` resumes the interrupted instruction. x.out is now dispatched by `x_cpu` rather than magic alone, since the 8086/80286/80386 targets share magic 0x0206. Verified with Xenix `echo`, `cat`, `ls -l`, `sync`, `csh` and `vi` (middle model, full-screen); Microsoft Word 3.0 for Xenix loads, initializes its terminal, grows its heap via `brkctl`, paints its banner, forks and reaps a child shell, and echoes keystrokes. Documented in `usr.man/man4/sco_x286.4` and `usr.man/man4/xout286.4`.
- **`proc_alloc_fd()` returns the lowest free descriptor:** it scanned from a rotating `next_fd` hint, so a process that had opened and closed a few files got an arbitrary descriptor from `open`/`dup`/`pipe`/`F_DUPFD` instead of the lowest one POSIX requires. The canonical redirect-without-dup2 idiom — `saved = dup(0); close(0); open(file); … close(0); dup(saved)` — therefore failed to put anything back on fd 0: SCO Xenix `vi` does exactly that and ended up with a permanently dead stdin (EBADF on every later read and ioctl). The hint survives only as bookkeeping; the scan always starts at descriptor 0.
- **Shebang Script Execution:** Implemented `#!` (shebang) handler in exec subsystem (`sys/exec/formats/script.c`). Scripts with `#!/path/to/interpreter` are now properly executed by extracting the interpreter and re-dispatching. Supports optional interpreter argument, recursion depth limit (4), and DOS line endings.

## Libraries & Userland
- **librt (POSIX realtime):** New `lib/rt/` producing `librt.a` + `librt.so.0` (DT_NEEDED `libpthread.so.0` + `libc.so.0`, OSABI-branded). Satisfies `-lrt`. Provides the POSIX message-queue wrappers (`mq_open`/`mq_close`/`mq_unlink`/`mq_send`/`mq_receive`/`mq_timedsend`/`mq_timedreceive`/`mq_notify`/`mq_getattr`/`mq_setattr` — thin shims over the seven native mq syscalls) and POSIX asynchronous I/O (`aio_read`/`aio_write`/`aio_error`/`aio_return`/`aio_suspend`/`aio_cancel`/`aio_fsync`/`lio_listio`). AIO is a userspace worker-thread pool over libpthread (the glibc/librt model, no kernel support): a fixed pool pulls submitted `struct aiocb` requests from a FIFO and runs the blocking `pread`/`pwrite`/`fsync`, storing the result on the aiocb; `SIGEV_NONE`/`SIGEV_SIGNAL`/`SIGEV_THREAD` completion notification, `EINPROGRESS`/`ECANCELED` semantics, and `lio_listio` `LIO_WAIT`/`LIO_NOWAIT` group completion. New headers `<mqueue.h>`, `<aio.h>`, and `struct sigevent`/`SIGEV_*` in `<signal.h>`. Functional test `tests/lib/aio/aiotest.c` (20 assertions: write/read round trip, fsync, lio_listio, cancel, SIGEV_THREAD).
- **libsys Library:** Created `lib/sys/` syscall wrapper library with `syscall.S` (raw i386 int 0x80), `syscall.h` (SYS_* constants), and typed wrappers (`vm86()`). Supports mmap, munmap, mprotect, brk syscalls.
- **Kernel Library Refactor:** Modularized `sys/kern/lib.c` into `sys/lib/string.c`, `printf.c`, and `div64.c`.
- **Kernel sprintf Enhancements:** Added printf flags: `-`, `+`, ` `, `#`, `0`, numeric width, and conversions: d/i/u/o/x/X/p/s/c.
- **grep / egrep / fgrep (POSIX.1-2024 + GNU/BSD):** Rewrote `bin/grep/` as a
  multi-file implementation (`grep.c`, `grep_opts.c`, `grep_pattern.c`,
  `grep.h`) conformant with POSIX.1-2024, with the common GNU/BSD extensions
  (BSD wins on conflict): dialects `-E`/`-F`/`-G` plus `egrep`/`fgrep` via
  `argv[0]`; `-e`/`-f` pattern sources; `-i`/`-v`/`-w`/`-x`; output control
  `-c`/`-l`/`-L`/`-m`/`-o`/`-q`/`-s`; prefixes `-n`/`-b`/`-H`/`-h`/`--label`;
  context `-A`/`-B`/`-C`/`-NUM` with `--` group separators; recursion
  `-r`/`-R` with `-d` and `--include`/`--exclude`; binary handling
  `-a`/`-I`/`--binary-files`; `-z` NUL data; and `--color`.  POSIX bracket
  character classes (`[[:alpha:]]` etc.) are translated to ASCII member sets
  at the grep layer since the engine lacks them; `-w`/`-x` are enforced via
  match offsets / anchored wrapping.  Spec with EARS requirements + user
  stories in `docs/specs/grep-spec.md`; man pages `grep.1`/`egrep.1`/`fgrep.1`;
  host + ASAN test suite under `tests/bin/grep/` (93 cases, ASAN leak-clean).
  POSIX BRE back-references `\1`..`\9` are supported (see the regex engine
  entry); locale collating/equivalence classes remain the only POSIX feature
  absent (Substrate is C/POSIX-locale, ASCII).
- **regex engine (`usr.lib/regex`):** Fixed a correctness bug whereby a
  compiled pattern matched the first input string and then never again — the
  per-thread visited marker (`nfa_state.last_list_id`) lives on the shared NFA
  program but the match-time `list_id` counter restarts each call, so stale
  stamps from a prior match suppressed the new match's threads.
  `nfa_capture_match()` now clears the stamps at the start of every match.
  This affected every consumer of the engine (including the previous grep).
- **regex engine — BRE back-references:** Added POSIX `\1`..`\9` support to the
  safe engine.  A back-reference compiles to a new `NFA_BACKREF` node; because
  the construct is non-regular the DFA/Pike-VM path cannot model it, so
  patterns containing a back-reference are matched by a new recursive
  backtracking matcher (`nfa_bt`/`safe_regex_backtrack`) over the same NFA,
  bounded by the `match_steps` budget and a recursion-depth cap (clean
  `MATCH_TIMEOUT` on overrun, never a hang/crash).  ERE keeps `\N` literal
  (BSD).  Also fixed capture-group numbering to follow the opening parenthesis
  (POSIX) rather than the closing one, which nested-group captures and
  back-references depend on.
- **regex engine — charclass leak:** `nfa_free()` now releases the
  `regex_charclass` owned by each `NFA_CLASS` state (transferred from the AST
  at compile time), fixing a bounded per-pattern compile-time leak.
- **libsys sole ownership of `syscall()`:** libsys owns the raw `syscall()`
  dispatcher and the `sys_*` typed wrappers (e.g. `sys_ioctl`, `sys_stat`,
  `sys_getpid`); `libc` no longer duplicates them, so static + dynamic links
  carry no colliding symbols. `Makefile.bin.inc` links `-l:libsys.a` (in a
  `--start-group` with libc/libm) for static binaries and `-l:libsys.so.0` for
  dynamic ones; a binary that calls `syscall()` directly (`bin/ldtctl`) needs
  libsys on its own link line.
- **Dual static/shared library builds:** every library under `lib/` ships both
  static (`libX.a`) and shared (`libX.so.0`) builds from a single source tree
  via dual `.o` / `.pic.o` compile passes; `SHLIB_CFLAGS` / `SHLIB_LDFLAGS` in
  `Makefile.inc`. The `libX.so` link-time symlink is install-only so it can't
  shadow `libX.a` in source-tree builds.
- **OSABI branding of shared libs:** `lib/c`, `lib/m`, `lib/sys` Makefiles
  auto-patch the OSABI byte of every produced `libX.so.0` to
  `ELFOSABI_SUBSTRATE` (0x40) via a one-byte `dd` post-step — host `cc -shared`
  stamps `ELFOSABI_SYSV` (0), which substrate's cross-ld rejects as "file in
  wrong format".
- **libc toolchain bring-up additions:** `putenv`, `localeconv` (POSIX "C"
  lconv), `htons`/`htonl`/`ntohs`/`ntohl` (real `__builtin_bswap` impls), and a
  socket / netdb / arpa-inet ENOSYS stub family for link-time satisfaction.
- **libc errno hygiene:** `malloc`/`calloc`/`realloc` set `errno = ENOMEM` (or
  `EINVAL` for a corrupted-header `realloc`) on failure; `malloc(0)` returns a
  unique 1-byte allocation rather than NULL (glibc/musl convention). The libc
  `mmap()` wrapper detects negative-errno kernel returns and sets `errno` +
  returns `(void *)-1` instead of leaking the kernel error code as a pointer.
- **libpthread torture suite** (`tests/lib/pthread/`): portable POSIX
  `torture_kernel.c` runs on Linux/FreeBSD/macOS/substrate as a cross-OS
  baseline; 8 scenarios target specific scheduler/threading bug classes (storm,
  fpu, wakeup, signals, mutex_fair, tls, massive, lockord). Plus a
  substrate-specific `torture_pthread.c` for libpthread surface correctness.
- **`bin/top`:** procps-grade `top(1)` — multi-source snapshot
  (`top_snapshot.c`), five-line summary + sortable process table
  (`top_render.c`), column sorting (`top_sort.c`), interactive terminal control
  with guaranteed restore and SIGWINCH resize (`top.c`). RSS comes from
  `pmap_resident_count`; the `%Cpu(s)` line is held to 80 columns. Unit tests
  under `tests/bin/top/`; man page `usr.man/man1/top.1`.
- **`bin/df`:** sizes its columns dynamically to the actual data (each column
  as wide as its widest cell).
- **`sbin/sdm` display manager:** `sdm` supervises `Xfbdev` + the `sgreet` Xlib
  greeter; `sgreet` authenticates like `bin/login` and offers a session chooser
  driven by `/etc/sdm/sessions` (`Label = command` lines; F1 cycles). The chosen
  command is exported as `$SDM_SESSION`, which `/etc/X11/Xsession` execs as the
  session leader (`${SDM_SESSION:-matwm2}`).

## Drivers
- **Audio framework — encoding conversion:** `audio_validate_prinfo()` had
  always accepted ULAW, ALAW, the unsigned encodings and the big-endian
  ones, but no backend ever looked at the field — the bytes went to the
  hardware as though they were signed 16-bit LE, so anything else played
  as noise.  `sys/drivers/audio/audio.c` now converts on the way through
  (G.711 expansion for the companded laws, recentre-and-widen for
  unsigned, byteswap for big-endian), so every backend sees one format.
  Because 8-bit sources widen to 16, `ops->set_params` is given the
  *converted* format while `AUDIO_GETINFO` keeps reporting what the
  application selected — which also lets 8-bit encodings play on codecs
  that offer no 8-bit format at all (QEMU's HDA codec advertises 16-bit
  only).  A stream that is already SLINEAR_LE takes an untouched fast
  path.  Verified: `tests/sys/host_test_audio` (G.711 anchors and
  monotonicity over both halves, widening, byteswap, odd-tail handling,
  format mapping) and a QEMU boot playing mu-law at 16 kHz, which renders
  6.144 s of audio to within 1% — where the same bytes previously
  produced 3.07 s of noise.  That test binary had also been failing to
  link since the OSS frontend landed and was silently stale; it now
  builds.
- **Intel HDA — full audit against the 1.0a specification (Aug 2026):**
  `sys/drivers/audio/hda.{c,h}` reviewed line by line against the
  *High Definition Audio Specification, Revision 1.0a* and the FreeBSD
  (`sys/dev/sound/pci/hda`) and NetBSD (`sys/dev/hdaudio`) drivers.
  Thirty-one defects fixed across sixteen commits.  Highlights:
  - **Sample rates were silently wrong.**  `hda_encode_format()` computed
    BASE/MULT/DIV arithmetically, but SDnFMT only permits MULT ×1–×4 and
    DIV ÷1–÷8 (table 40), and it never emitted a nonzero MULT *and* DIV
    together — so 32 kHz (48 k ×2 ÷3) was unreachable.  Worse, its
    fallback returned a valid 48 kHz encoding rather than an error, so
    8000, 11025, 16000 and 32000 all played at 48 kHz, three to six times
    too fast, with nothing on the path able to notice.  Replaced with a
    table of every legal encoding; status now returns separately from the
    value, because 48 kHz 8-bit mono legitimately encodes to 0x0000.
  - **Codec setup.**  AMP_CAPS NumSteps was read from the StepSize field
    (14:8, not 22:16); widget amp and format parameters are now taken
    from the function group unless the widget sets the corresponding
    override bit (7.3.4.6); input amps along the routed path are unmuted
    (the classic "routed correctly, still silent" case on Realtek and
    Conexant parts); connection lists are parsed per their actual form
    with ranges expanded, where long-form entries had been truncated to
    8 bits; EAPD is read-modify-written; `SET_CONNECTION_SELECT` is no
    longer sent to single-connection widgets.
  - **Stream engine.**  RUN does not drop on write (4.5.4, ≤40 µs) and was
    never polled; CBL was rewritten on a merely-stopped engine where
    3.3.38 requires a reset first; and BCIS means *fetched into the DMA
    FIFO*, not played (3.3.36), so halting on the final completion
    truncated every clip.  Stop/reset/start are now sequenced properly and
    the halt is deferred out of the ISR, which 4.5.6 asks for anyway.
  - **Controller bring-up.**  DMA engines are quiesced before CRST as
    3.3.7 requires, the link reset pulse is held, and codec enumeration
    waits the mandated 521 µs (25 frames) instead of a ~50 µs spin loop.
    CORB/RIRB are sized from the capability mask and their reset and RUN
    readbacks verified.
  - **Interrupts.**  INTSTS is read-only (table 15) — the write "to clear"
    it did nothing; the handler now re-reads until GIS clears, which
    FreeBSD documents as necessary to avoid wedging a level-triggered
    shared INTx permanently.  FEIE/DEIE are enabled and errors counted.
  - **Robustness.**  Verbs are serialised and responses filtered by codec
    address and the unsolicited flag; a failed attach unwinds its IRQ and
    DMA instead of leaving a live handler pointing at reused softc memory;
    every codec is tried, not just the first; digital pins are skipped;
    and formats the converter does not advertise are refused rather than
    rendered wrongly.
  Verified: `tests/sys/host_test_hda` (rate table round-trip, connection
  list parsing incl. long-form ranges), clean `make -C sys`, and boot under
  QEMU on both `intel-hda` (ICH6) and `ich9-intel-hda` with playback at
  44.1/32/16 kHz rendering to within 1.3% of the exact expected duration —
  where the pre-fix driver would have played 16 k and 32 k at 48 kHz — and
  an unsupported rate (8 kHz, which QEMU's codec does not advertise)
  correctly refused.  Driver documentation: `usr.man/man4/hda.4`.
- **AC'97 / Intel HDA — IRQ-driven ring refill:** Both audio drivers now
  decouple the `write()` producer from the DMA-ring consumer with a deep
  software PCM FIFO (`sys/drivers/audio/audio_fifo.h`, a single-producer /
  single-consumer byte ring).  `write()` appends to the FIFO and blocks only
  when it fills (256 KB ≈ 1.4 s); the per-slot completion IRQ (AC'97 BCIS /
  HDA BCIS) stages FIFO data into the BDL ring autonomously (`ac97_feed` /
  `hda_feed`), so the controller keeps playing across scheduling jitter under
  CPU load instead of underrunning to silence.  The feeder is serialised by an
  IRQ-safe `feed_lock` (held with local interrupts masked) against the IRQ
  handler, the priming path, and other CPUs.  Underrun-to-halt resyncs the
  in-flight counters so the stream restarts cleanly.  Verified: FIFO host unit
  test (`tests/sys/host_test_audio_fifo`), clean target build, and boot with
  AC'97 + intel-HDA attached with no panic/deadlock.
- **AC'97 — restart the halted bus master from the IRQ handler:** The AC'97
  bus master halted at LVI (the DCH "DMA controller halted" condition) under
  QEMU, and although the BCIS completion IRQ refilled the BDL ring it never
  restarted the halted BM — only the `write()` producer did.  Playback
  therefore stalled after ~10 ms with PCM stuck in the FIFO, surfacing as
  glitchy/silent audio and as applications pacing to the audio clock running
  slow.  Factored the restart logic into `ac97_kick_locked()`
  (`sys/drivers/audio/ac97.c`) and now call it from BOTH the producer and the
  BCIS IRQ handler, and the feeder always refills the ring.  Verified by
  capturing a 441 Hz tone with `qemu -audiodev wav`: continuous,
  correctly-pitched 3.0 s playback.

## Networking
- **AF_UNIX socket buffer — 4 KiB -> 256 KiB (fixes catastrophically slow
  local X):** `AFUNIX_BUF_SIZE` (`sys/net/af_unix.c`) was 4096, so Xlib's
  `XPutImage` (substrate has no MIT-SHM, so SDL2 falls back to it) chopped
  each 640x480 window update into roughly 300 buffer round-trips — about
  2 MB/s and ~0.8 fps for an SDL UI, presenting as "the entire UI is slow,
  not CPU-bound".  Raising the per-socket ring to 256 KiB took AF_UNIX
  throughput from 2.0 to 134 MB/s and an SDL 640x480 frame from 1227 to
  ~44 ms (~0.8 -> ~23 fps).
- **TCP/IPv4** (`sys/net/tcp.c`): three-way handshake, the full close handshake
  (incl. in-order FIN consumption), retransmit + dup-ACK fast-retransmit +
  zero-window persist timer, and real `snd_wnd` send-side flow control (the
  unacked FIFO is bounded to one receive window). Closed PCBs are reaped by the
  retransmit-timer kthread (the sole reaper), so there is no per-connection leak.
- **Sockets** (`sys/net/af_inet.c`, `af_unix.c`): the BSD socket surface —
  `socket`/`bind`/`listen`/`accept`/`connect`/`send`/`recv`, `shutdown(2)`,
  `O_NONBLOCK` accept, accept-backlog enforcement; AF_UNIX SOCK_STREAM/DGRAM
  with SCM_RIGHTS fd passing.
- **Loopback** (`sys/net/loopback.c`): a dedicated kthread drains the delivery
  ring so a TX never recurses into RX on the caller's kernel stack.
- **`sbin/telnetd`:** standalone telnet server, thread-per-connection (one
  process, not fork-per-connection) — each connection bridges the socket to a
  PTY running `/bin/login`. Tests under `tests/lib/c/`: `test_tcp.c`,
  `torture_tcp.c` (12-scenario leak deep-dive), `torture_socket.c` (telnetd-shape
  lifecycle), and `repro_acceptloop.c` (fork-per-connection regression).

## Dynamic Linking & Toolchain
- **Cross toolchain emitted no `PT_GNU_EH_FRAME` (all C++ exceptions aborted):**
  every binary the cross g++ produced had an `.eh_frame` section but no
  `.eh_frame_hdr` / `PT_GNU_EH_FRAME` segment, so libgcc's `dl_iterate_phdr`
  unwinder found no unwind table for the module and *every* throw out of a main
  executable reached `std::terminate` — even a `try`/`catch (...)` in the same
  function.  PsyMP3 aborted on a missing `.lrc` lyrics sidecar for this reason,
  despite the probe being wrapped in `catch (...)`.  Cause: the hand-installed
  `<libdir>/specs` override that adds `--copy-dt-needed-entries`/`-rpath-link`
  is appended to `link_spec` *before* gcc prepends `LINK_EH_SPEC`, so it silently
  dropped the `--eh-frame-hdr` that patch 0010 adds (the built-in `LINK_SPEC`
  half, `-m elf_i386_substrate`, still came through, which hid the loss).  The
  override now re-states `%{!static:--eh-frame-hdr}` itself and is generated by
  the tracked `contrib/gcc/install-specs.sh` instead of being untracked host
  state.  Verify with `readelf -l <binary> | grep GNU_EH_FRAME`.
- **`dl_iterate_phdr(3)` (done — verified end-to-end):** Added the
  POSIX/glibc `dl_iterate_phdr` surface to the native dynamic linker.  ld.so
  now records each loaded object's runtime program-header table (`phdr`/`phnum`
  in `sbin/ld.so/ld.h`, populated in `sbin/ld.so/ld_load.c` and
  `sbin/ld.so/ld_main.c`) and exports `__ldso_dl_iterate_phdr`
  (`sbin/ld.so/ld_dl.c`) that walks them.  A libc bridge in
  `lib/c/src/dl_iterate_phdr.c` and a new `include/link.h` expose the standard
  entry point.  This is the runtime half of the C++ cross-DSO exception fix
  below, and is verified end-to-end alongside it.
- **C++ cross-DSO exceptions — shared `libgcc_s` + PT_GNU_EH_FRAME unwinding
  (done — verified end-to-end):** Root cause: substrate's gcc
  statically linked libgcc into every module, each carrying its own DWARF FDE
  registry, and libgcc was not built to consult `dl_iterate_phdr`, so a C++
  exception thrown inside a shared library could not unwind back into its
  caller — it reached `terminate()`/abort instead.  This made exceptions
  uncatchable across the exe/DSO boundary (breaking TagLib/PsyMP3 and the
  broader C++ desktop).  Fixed in
  `contrib/gcc/patches/0010-libgcc-pt-gnu-eh-frame-substrate.patch`: define
  `USE_PT_GNU_EH_FRAME` for `__substrate__` in libgcc
  (`unwind-dw2-fde-dip.c`, `crtstuff.c`), add `--eh-frame-hdr` via the canonical
  `LINK_EH_SPEC` hook (`LINK_SPEC` did not reach ld), add `t-slibgcc` so g++
  links the shared `libgcc_s.so` (a single FDE registry shared across all
  modules), and `thread_file=posix` so libstdc++ has `std::mutex`.  Paired with
  the `dl_iterate_phdr(3)` runtime support above.  Verified end-to-end: a throw
  in a `.so` is caught in the exe (rc=0), and TagLib reads a FLAC's metadata
  instead of aborting.  Follow-up: gthr-posix uses hard (non-weak) pthread refs,
  so C++ programs using `std::mutex` must link `-lpthread`.
- **gdb runs natively on substrate:** The GNU debugger (`contrib/gdb/`, stripped
  in `contrib/gdb/build.sh`) runs end-to-end on the target, backed by the libsys
  `ptrace` PEEK bridge in `lib/sys/ptrace.c`.
- **`/sbin/ld.so` phase history:** the native dynamic linker (`sbin/ld.so/`)
  landed in phases — (1) bootstrap + auxv handoff, (2) self-relocate + parse
  program PT_DYNAMIC, (3) DT_NEEDED load via `mmap`+MAP_FIXED and REL/JMPREL
  relocations (RELATIVE / GLOB_DAT / JMP_SLOT / 32 / PC32, eager binding,
  DT_GNU_HASH preferred with DT_HASH fallback), (4a) recursive BFS DT_NEEDED
  traversal, (4b) DT_INIT_ARRAY in dependency order with `environ` published
  before the constructor pass, (4c) variant-II per-thread TLS with the GS base
  set via `sys_set_gsbase` (274; local-exec/initial-exec only, GD/LD deferred),
  (4d) R_386_COPY, (4e) runtime dlopen/dlsym/dlclose, (4f) DT_FINI_ARRAY at
  `exit()` via a libc weak hook, (4g) canonical function addresses
  (function-pointer equality across the exe/DSO boundary — required by Xt's
  `XtInherit*` class-method machinery). Per-object relocated/initialized/
  finalized guards keep the non-idempotent R_386_RELATIVE and the run-once
  init/fini arrays from firing twice. `crt0.S` is PIC-safe so one `crt0.o`
  serves both static and PIE links. Specs: `docs/design/ld.so-design.md`,
  `docs/specs/ld.so-reloc-matrix.md`, `docs/kernel-ldso-abi-substrate.md`,
  `docs/specs/abi-i386.md`.

## Userland Ports — Multimedia
- **SDL 2.30.9 (`contrib/sdl2/`):** Ported with the X11 video driver and the
  NetBSD `/dev/audio` (Sun/SADA) audio backend.  Depends on FreeType2
  (`contrib/freetype/`).
- **PsyMP3 (`contrib/psymp3/`):** Music player, pinned to a specific upstream
  commit with a vendored patch series.  Pulls in its codec dependencies, each a
  standalone port: `libogg` (`contrib/libogg/`), `libvorbis`
  (`contrib/libvorbis/`), `libopus` (`contrib/libopus/`), `speex`
  (`contrib/speex/`), `faad2` (`contrib/faad2/`), `taglib` (`contrib/taglib/`)
  and `spandsp` (`contrib/spandsp/`).  Together these bring audio/multimedia
  playback to the substrate userland.

## Build & Testing
- **Build System:** Root filesystem generation in `dist/`. Fixed `dist` directory generation to include standard Unix hierarchy.
- **Test Framework:** Comprehensive kernel test runner (`tests/sys/`). Tests are **not** compiled into the kernel by default; use `make -C sys KERNEL_TESTS=1` for test builds. Host-runnable tests (`host_test_*`) are built separately with `make -C tests/sys`.
