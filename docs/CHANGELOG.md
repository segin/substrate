# Substrate Development Changelog

Detailed record of major implementation milestones. For current system status, see `AGENTS.md`.

## Kernel Core

### Memory Management
- **PMM Hardening:** Phase 1 boot memory detection with sanitization, total RAM reporting, and proper kernel bounds.
- **Per-Process Address Spaces:** Implemented `pmap_create()`, `pmap_destroy()`, `pmap_reference()`, `pmap_release()`, `pmap_fork()` with COW support. Global pmap list for TLB management. Full 3GB/1GB user/kernel split.
- **PMM Virtual Address API:** `pmm_alloc_block()` and `pmm_alloc_contiguous()` now return kernel virtual addresses (phys + 0xC0000000) instead of physical addresses. `pmm_free_block()` and `pmm_free_contiguous()` expect virtual addresses. Updated all callers in: `pmap.c`, `elf.c`, `sched.c`, `process.c`.
- **UMA Integration:** Integrated FreeBSD-style Universal Memory Allocator (UMA) for kernel memory allocation. `kmalloc`/`kfree` now backed by UMA zones via `vm_kmem.c`. Added `uma_startup()` before `kmem_init()`.

### Process & Scheduling
- **Process Model Refactor:** Separated Swapper (PID 0) and Init (PID 1). Enforced `PID == Main_TID` invariant. Added Process Group (`pgrp`) and Session support.
- **Scheduler Refactor (MLFQ):** Implemented Multilevel Feedback Queue with Realtime, Timeshare, and Idle classes.
- **SMP Scheduler:** Per-CPU runqueues, Work Stealing load balancing, CPU Affinity support, and IPI-based preemption.
- **Kernel Process:** Implemented Swapper/Idle (PID 0) with pageout daemon and idle loop responsibilities.
- **Context Switching:** Validated FPU Lazy Save and refined PCB for thread/process separation.
- **Init Safety:** Kernel now catches `init` process exit (e.g., from detached stdin) and enters an idle loop instead of panicking.

### Synchronization
- **Synchronization Primitives:** Implemented Turnstiles (Priority Inheritance) and Hashed Sleep Queues (O(1) lookup).
- **Synchronization Improvements:** Updated `mutex` and `semaphore` to use `sleepq` for robust thread sleeping (removed ad-hoc `sched_sleep`).
- **VFS Concurrency & Locking:** Implemented BSD-style `lockmgr()` lock manager (`sys/kern/lockmgr.c`) with `struct lock` supporting LK_SHARED, LK_EXCLUSIVE, LK_UPGRADE, LK_DOWNGRADE, LK_DRAIN, LK_NOWAIT, and priority inheritance via turnstiles. Refactored vnode locking (`vn_lock`/`vn_unlock`) to delegate to `lockmgr()`. Wired name cache rwlock and mount point rwlock.

### Signals & TTY
- **TTY Integration:** Per-process controlling terminal support.
- **TTY Signals:** Implemented signal generation from TTY (`SIGINT`, `SIGQUIT`, `SIGTSTP`) and group signal delivery (`signal_send_group`).
- **Syscall Tracing:** Enhanced `syscall_trace` with names, typed arguments (int/hex/ptr/str), return values, and Personality details.

### Time
- **Time System:** 64-bit time_t, RTC driver, gettimeofday/clock_gettime syscalls.
- **Filesystem Timestamps:** Added atime/mtime/ctime tracking with atomic updates.

## VFS & Filesystems
- **VFS Hard Link Support:** Implemented `link` in VFS and hooked up `sys_link` across native, Linux, and FreeBSD personalities. Improved ABI detection for stack-based syscalls.
- **VFS Unlink Support:** Implemented `unlink` in VFS and hooked up `sys_unlink` across native, Linux, and FreeBSD personalities.
- **UDF Filesystem Driver:** Complete read-write UDF (Universal Disk Format) driver per ECMA-167/OSTA spec. On-disk structures in `udf.h`, read-only support in `udf.c`, write support in `udf_write.c`, with unit tests and man pages (`udf.4`, `udf.5`).

## Drivers

### Video & Framebuffer
- **Framebuffer:** Implemented native linear framebuffer driver (`fb.c`) with Multiboot support and bitmap font console. Added Bochs Graphics Adapter (BGA) native driver support via `video=bga`.
- **Framebuffer Mode Selection (`vga=`):** Added `vga=WxH@BPP` kernel command line parameter for framebuffer mode selection across all video drivers. Supports legacy CGA/EGA/Hercules/VGA modes, BGA set_mode, multi-framebuffer device registry (`/dev/fb0`..`/dev/fb7`), and GRUB framebuffer inheritance.
- **Framebuffer Rendering Subsystem:** Full rendering pipeline in `sys/drivers/video/`. PSF1/PSF2 font parsers (`psf.c`) with auto-detection and Unicode table extraction. BDF/PCF bitmap font parsers (`bdf_pcf.c`) with hex-to-binary glyph conversion and PCF TOC navigation. Font glyph cache (`font_cache.c`) with FNV-1a hash table (256 buckets) and UTF-8 Unicode mapping from PSF1/PSF2 tables. Blitting operations (`fb_ops.c`): `fb_fillrect()` with ROP_COPY/ROP_XOR, `fb_copyarea()` with overlap-safe memmove, `fb_imageblit()` for mono/color images — all with 32bpp fast paths and generic putpixel fallback, plus viewport clipping. Character rendering attributes (`fb_console.c`): `fb_putc_attr()` supports bold (shift-and-OR double-strike), italic (quarter-height shear transform), underline, strikethrough, and reverse video.

### Input & Storage
- **PS/2 Subsystem:** Expanded PS/2 controller driver to support dual-channel (Mouse/Aux) operation.
- **VirtIO Drivers:** Implemented Core VirtIO, Block Device (virtio-blk), and 9P Transport (virtio-9p) drivers.

### USB
- **USB device enumeration / `lsusb`:** USB devices are now exposed both under
  `/proc/devtree` and as `/dev/usb` nodes via `sys/drivers/usb/usbdevfs.c`, so
  `lsusb` enumerates attached devices end-to-end.

## Architecture & Boot
- **FPU State Tracking:** Lazy FPU context switching with FXSAVE/FXRSTOR.
- **Early Boot Debugging:** Added early GDT+IDT handler in `main.c` using `early_uart_print()` for exception debugging before full console is available.
- **LAPIC Early Mapping:** Added LAPIC identity-mapping (0xFEC00000-0xFFFFFFFF) in `boot.S` page tables with PCD flag for MMIO.
- **GRUB Boot Fix:** Fixed multiboot header video mode field offsets (were at 12-24, spec requires 32-44). Kernel now boots through GRUB for the first time.
- **PT_TLS Support:** ELF loader now handles PT_TLS segment, allocates TLS block, sets GDT entry 6 for GS-based TLS access.

## Exec & Personality
- **Shebang Script Execution:** Implemented `#!` (shebang) handler in exec subsystem (`sys/exec/formats/script.c`). Scripts with `#!/path/to/interpreter` are now properly executed by extracting the interpreter and re-dispatching. Supports optional interpreter argument, recursion depth limit (4), and DOS line endings.

## Libraries & Userland
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

## Drivers
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

## Dynamic Linking & Toolchain
- **`dl_iterate_phdr(3)` (done — verified end-to-end):** Added the
  POSIX/glibc `dl_iterate_phdr` surface to the native dynamic linker.  ld.so
  now records each loaded object's runtime program-header table (`phdr`/`phnum`
  in `sbin/ld.so/ld.h`, populated in `sbin/ld.so/ld_load.c` and
  `sbin/ld.so/ld_main.c`) and exports `__ldso_dl_iterate_phdr`
  (`sbin/ld.so/ld_dl.c`) that walks them.  A libc bridge in
  `lib/c/src/dl_iterate_phdr.c` and a new `include/link.h` expose the standard
  entry point.  This is the runtime half of the C++ cross-DSO exception fix
  below; it is implemented but not yet verified end-to-end, pending the
  matching libgcc/g++ rebuild.
- **C++ cross-DSO exceptions — shared `libgcc_s` + PT_GNU_EH_FRAME unwinding
  (done — verified end-to-end):** Root cause: substrate's gcc
  statically linked libgcc into every module, each carrying its own DWARF FDE
  registry, and libgcc was not built to consult `dl_iterate_phdr`, so a C++
  exception thrown inside a shared library could not unwind back into its
  caller — it reached `terminate()`/abort instead.  This made exceptions
  uncatchable across the exe/DSO boundary (breaking TagLib/PsyMP3 and the
  broader C++ desktop).  Fix being landed in
  `contrib/gcc/patches/0010-libgcc-pt-gnu-eh-frame-substrate.patch`: define
  `USE_PT_GNU_EH_FRAME` for `__substrate__` in libgcc
  (`unwind-dw2-fde-dip.c`, `crtstuff.c`), add `--eh-frame-hdr` to the substrate
  `LINK_SPEC`, and add `t-slibgcc` so g++ links the shared `libgcc_s.so` (a
  single FDE registry shared across all modules).  Paired with the
  `dl_iterate_phdr(3)` runtime support above.  Not yet verified — the libgcc
  and g++ rebuild that activates it is still in progress.
- **gdb runs natively on substrate:** The GNU debugger (`contrib/gdb/`, stripped
  in `contrib/gdb/build.sh`) runs end-to-end on the target, backed by the libsys
  `ptrace` PEEK bridge in `lib/sys/ptrace.c`.

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
