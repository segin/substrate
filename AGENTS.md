# AGENTS.md

## Project Description
This is the Substrate operating system project targeting x86 32-bit architecture (with x86_64 plans). The goal is to build a Unix-like system with a kernel, standard utilities, and libraries, capable of running native, Linux, and FreeBSD binaries via personality emulation.

## Technical Constraints & Standards
- **Architecture:** x86 32-bit (primary), x86 64-bit (planned/stubbed).
- **C ABI:** Standard Intel C ABI.
- **Toolchain:** Modern GCC (`-m32`, `-nostdlib`, `-fno-builtin`).
- **Userland Linker Flags:** `-m32 -nostdlib -fno-pie`.

## Recent Accomplishments

For the full detailed changelog, see `docs/CHANGELOG.md`.

### Kernel Core
- PMM Buddy Allocator (Phase 2), UMA-backed kmalloc, per-process pmap with COW
- MLFQ + SMP scheduler with per-CPU runqueues, work stealing, and CPU affinity
- Turnstiles (priority inheritance), hashed sleep queues, sleepq-backed mutex/semaphore
- BSD-style `lockmgr()` with shared/exclusive/upgrade/drain modes; vnode, namecache, and mount locking
- Process model: Swapper (PID 0) / Init (PID 1), process groups, sessions
- TTY signals (SIGINT/SIGQUIT/SIGTSTP), VMIN/VTIME support, per-process ctty
- 64-bit time_t, POSIX timestamp compliance (atime/mtime/ctime across VFS and ext2)
- Syscall tracing with typed arguments and personality detail
- Full FreeBSD-compatible thread syscall set: `thr_new` (455),
  `thr_exit` (431), `thr_self` (432), `thr_kill` (433), `thr_suspend`
  (442), `thr_wake` (443), `thr_join` (457), `thr_set_name` (464),
  `thr_kill2` (481).  Per-thread `sig_pending`/`sig_mask` already
  drove signal delivery; the new entry points expose it to libpthread
  for cancellation, naming, and parking.  `thread_t.name[16]` field
  and `THREAD_F_WAKE_PENDING` flag for race-free suspend/wake.
- Demand-paged user stack: `exec` maps only a small region (128 KiB)
  at the top of the stack and records `[ustack_limit, ustack_top)`
  on the process; the page-fault handler grows the stack one page
  at a time on access (8 MiB ceiling).  A process costs only the
  stack it touches instead of a fixed 4 MiB reservation, so deep
  fork/exec chains no longer exhaust RAM.
- `memtrack` (`sys/kern/memtrack.c`): per-call-site physical-page
  accounting — every `pmm_alloc_*` / `pmm_free_*` is charged to the
  caller's return address, giving a pages-allocated-vs-freed table
  per code path.  Exposed via `/proc/memtrack` and `sys_vm_slabs(2)`.
- `psignal()` discards a signal whose effective disposition is
  "ignore" instead of leaving it pending — a pending-but-ignored
  signal otherwise aborts every interruptible sleep.
- Per-process kernel stacks are 16 KiB (4 PMM pages).  8 KiB
  overflowed: a deep network TX syscall path (`sys_write` ->
  `tcp_send` -> ... -> `rtl_xmit`) can take a nested NIC IRQ that
  runs the whole RX -> IP -> TCP input path on the same stack, and
  the combined depth scribbled the adjacent `kmem-64` slab
  (`vm_object` / `vm_map_entry` structs) — surfacing as
  non-deterministic SIGSEGVs and panics in unrelated processes.
  `kern/kthread.c` stacks were already 16 KiB; `sched.c` now matches.
- VM kernel-heap corruption tripwires (`sys/vm/`): a `vm_object`
  magic canary (use-after-free / scribble / `ref_count` underflow),
  a buddy-allocator double-allocation detector (`PG_PMM_ALLOC`), a
  UMA per-item double-free guard, and `vm_map_audit` (validates every
  `entry->object` after each map mutation).  Each converts silent
  kernel-heap corruption into an immediate, located `panic()`.
- System V semaphores (`sys/kern/ipc_sem.c`): `semget`/`semop`/`semctl`
  (native syscalls 402/403/404).  A fixed `SEMMNI` set table with
  key->id lookup (per-slot sequence numbers reject stale ids),
  `ipc_perm` checks, atomic multi-op `semop` with interruptible
  blocking via sleepq (`IPC_NOWAIT`, `semncnt`/`semzcnt` waiter
  tracking, `EIDRM` on remove-while-blocked), the full `semctl`
  command set, and `SEM_UNDO` reversed at `proc_exit`.  The
  personality-agnostic core (`kern_sem*`) is wired into the Linux
  (`ipc(2)` multiplexer), FreeBSD (`semget`/`semop`/`__semctl`) and
  NetBSD (`____semctl50`) personalities via
  `sys/exec/perso/perso_ipc_sem.c`, each marshalling its own
  `semid_ds`/`semun` ABI.  Torture suite: `tests/lib/ipc/torture_sem.c`
  (71 scenarios; 70 pass / 1 root-skip on substrate).

### Networking
- TCP/IPv4 (`sys/net/tcp.c`): three-way handshake, the full close
  handshake (incl. in-order FIN consumption), retransmit + dup-ACK
  fast-retransmit + zero-window persist timer, and real `snd_wnd`
  send-side flow control — the unacked FIFO is bounded to one
  receive window.  Closed PCBs are reaped by the retransmit-timer
  kthread (the sole reaper), so there is no per-connection leak.
- Sockets (`sys/net/af_inet.c`, `af_unix.c`): the BSD socket surface
  — `socket`/`bind`/`listen`/`accept`/`connect`/`send`/`recv`,
  `shutdown(2)`, `O_NONBLOCK` accept, accept-backlog enforcement;
  AF_UNIX SOCK_STREAM/DGRAM with SCM_RIGHTS fd passing.
- Loopback (`sys/net/loopback.c`): a dedicated kthread drains the
  delivery ring so a TX never recurses into RX on the caller's
  kernel stack.
- `sbin/telnetd`: standalone telnet server, thread-per-connection
  (one process, not fork-per-connection) — each connection bridges
  the socket to a PTY running `/bin/login`.
- Tests under `tests/lib/c/`: `test_tcp.c` (functional),
  `torture_tcp.c` (12-scenario data-gathering deep-dive with
  `/proc/meminfo`-based leak quantification), `torture_socket.c`
  (telnetd-shape connection lifecycle, incl. PTY hang-up), and
  `repro_acceptloop.c` (fork-per-connection accept-loop regression).

### Filesystems
- VFS: link/unlink, readdir atime, chmod/chown ctime
- ext2: timestamp fixes (write ctime, add_entry/remove_entry parent timestamps)
- UDF: Complete read-write driver with unit tests and man pages
- Buffer cache (bio): hash lookup, queueing, delayed write, syncer kthread
- Block-level read cache: the buffer cache is keyed at the block-device
  layer — `blkdev_do_read`/`do_write` (`sys/drivers/storage/blkdev.c`)
  route through `bio_dev_get`/`release` keyed by `(struct blkdev *,
  sector)` with read coalescing and write-through, so caching works
  automatically for every storage driver with no driver changes and the
  filesystems carry no caching logic.  The old per-fs `bcache[]` array is
  gone; `blkdev_unregister` calls `bio_dev_purge`.  See
  `docs/design/block-cache-consolidation.md`.
- readdir/getdents byte-offset cookies: `struct dirent` carries a
  `uint64_t d_off`; ext2 readdir is byte-offset based and getdents/
  getdents64 advance by it — fixes `rm -rf` needing multiple passes on
  large directories (the dir-index cookie no longer collides).

### Drivers
- Framebuffer: linear FB, BGA, `vga=WxH@BPP` mode selection, multi-device registry
- Rendering: PSF1/PSF2/BDF/PCF font parsers, glyph cache, fb_fillrect/fb_copyarea/fb_imageblit, bold/italic/underline/strikethrough/reverse attributes
- VirtIO (block + 9P), PS/2 dual-channel, FPU lazy save
- PS/2 mouse: IntelliMouse (3-button + wheel) and IntelliMouse Explorer
  (5-button + wheel) detection via the Microsoft sample-rate knock
  sequence; 4-byte packet decoding; `BTN_SIDE` / `BTN_EXTRA` /
  `REL_WHEEL` emitted on the input layer
- PS/2 keyboard: runtime-switchable Set 1 (translated XT, default)
  and Set 2 (AT, native) scancode decoders with `0xF0` break-prefix
  state machine and an E0-prefix path that handles both encodings
- Intel HDA, AC'97, SB16, null audio backends (Sun-compat audio
  framework)
- USB HID boot-protocol mouse

### Boot & Architecture
- GRUB boot fix, early boot debugging, LAPIC identity mapping
- Shebang `#!` script execution, PT_TLS support
- EFI boot stub with GOP framebuffer

### Libraries & Build
- libsys syscall wrapper library, kernel library modularization
- libsys is the sole owner of the raw `syscall()` dispatcher and the
  `sys_*` typed wrappers (e.g. `sys_ioctl`, `sys_stat`, `sys_getpid`);
  `libc` no longer duplicates them, so static + dynamic links carry no
  colliding symbols.  `Makefile.bin.inc` links `-l:libsys.a` (in a
  `--start-group` with libc/libm) for static binaries and
  `-l:libsys.so.0` for dynamic ones; a binary that calls `syscall()`
  directly (`bin/ldtctl`) needs libsys on its own link line.
- Root filesystem staging (`dist/`), test framework (`tests/sys/`)
- Every library under `lib/` ships both static (`libX.a`) and shared
  (`libX.so.0`) builds from a single source tree via dual `.o` /
  `.pic.o` compile passes; `SHLIB_CFLAGS` / `SHLIB_LDFLAGS` defined
  in `Makefile.inc`.  The `libX.so` link-time symlink is install-only
  so it can't shadow `libX.a` in source-tree builds.
- `lib/c`, `lib/m`, `lib/sys` Makefiles auto-patch the OSABI byte of
  every produced `libX.so.0` to `ELFOSABI_SUBSTRATE` (0x40) via a
  one-byte `dd` post-step.  Required because host `cc -shared` stamps
  `ELFOSABI_SYSV` (0), which substrate's cross-ld rejects as "file
  in wrong format".  Build infrastructure picks up additions without
  ceremony.
- libc additions for the GCC toolchain bring-up: `putenv`,
  `localeconv` (POSIX "C" lconv), `htons` / `htonl` / `ntohs` /
  `ntohl` (real `__builtin_bswap` impls), socket / netdb / arpa-inet
  ENOSYS stub family for link-time satisfaction pending a real
  in-kernel sockets layer.
- libc errno hygiene: `malloc`, `calloc`, `realloc` now set
  `errno = ENOMEM` (or `EINVAL` for corrupted-header `realloc`) on
  failure; `malloc(0)` returns a unique 1-byte allocation rather
  than NULL (glibc/musl convention) — mandoc's `mandoc_malloc`
  treated NULL as fatal OOM and exited via `err(6, NULL)` printing
  `strerror(0)="Success"`.  The libc `mmap()` wrapper now detects
  negative-errno returns from the kernel and sets `errno` +
  returns `(void *)-1` instead of leaking the kernel error code
  to userspace as a pointer.
- libpthread torture suite (`tests/lib/pthread/`): portable POSIX
  `torture_kernel.c` runs on Linux/FreeBSD/macOS/substrate alike as a
  cross-OS baseline; 8 scenarios target specific scheduler/threading
  bug classes (storm, fpu, wakeup, signals, mutex_fair, tls,
  massive, lockord).  Plus a substrate-specific `torture_pthread.c`
  for libpthread surface correctness.

### Toolchain (Substrate-Native GNU Toolchain)
- **Stage 1 (cross)**: binutils 2.46.0 + GCC 16.1.0 patched for
  the substrate target, built on a Linux host.  Installs into
  `/opt/substrate` (`STAGE1_PREFIX`) as
  `i386-unknown-substrate-{gcc,g++,as,ld,ar,nm,objdump,...}`.  Used
  to cross-compile substrate userland.
- **Stage 2 (Canadian cross)**: same binutils + GCC sources, but
  built with the stage-1 cross compiler to produce substrate-ELF
  binaries that run *on* substrate itself.  Installs into
  `dist-toolchain/usr/` (binutils) and `/tmp/gcc-stage2-staging/usr/`
  (gcc) as `/usr/bin/{gcc,g++,ld,as,ar,nm,objdump,readelf,strip,
  ranlib,size,strings,addr2line,c++filt,elfedit,gprof,ld.bfd}`,
  `/usr/libexec/gcc/i386-unknown-substrate/16.1.0/{cc1,cc1plus,lto1,
  lto-dump,collect2,lto-wrapper}`, and
  `/usr/lib/gcc/i386-unknown-substrate/16.1.0/{libgcc.a,libgcov.a,
  crtbegin*.o,crtend*.o}`.
- **Bootstrap orchestrator**: `contrib/build-toolchain.sh` drives
  all four phases (binutils stage 1, gcc stage 1, binutils stage 2,
  gcc stage 2) with idempotent fetch + patch + build.  Per-component
  scripts at `contrib/binutils/build.sh` and `contrib/gcc/build.sh`
  can also be run individually.
- **ELFOSABI_SUBSTRATE = 64**: architecture-specific OSABI byte
  stamped by every substrate-target BFD output vec
  (`elf32-i386-substrate`, `elf64-x86-64-substrate`).  The vec has
  `ELF_OSABI_EXACT = 0` so it accepts SysV (OSABI=0) input objects
  during bootstrap, but every executable/DSO it produces carries
  OSABI=64.  This is the wire-level identifier the kernel exec
  personality dispatch uses to route a binary to its loader.
- Patch series for both binutils and gcc lives in
  `contrib/{binutils,gcc}/patches/` and is reapplied by `fetch.sh`.
  Nothing in `contrib/*/build/` is vendored.
- Image bootstrap chain (committed end-to-end):
  ```
  contrib/build-toolchain.sh        # stage 1 cross + stage 2 native
  ./build-rootfs.sh --dist          # substrate userland into dist/
  ./build-rootfs.sh --toolchain     # overlay stage-2 toolchain
  ./build-rootfs.sh --image         # bake 4 GiB rootfs.img
  ```
  Or the all-in-one orchestrator that drives all of the above
  plus every contrib port in dependency order:
  ```
  sudo ./build.sh                   # repo-root, from clean checkout
                                    # env knobs: SKIP_TOOLCHAIN,
                                    # SKIP_CONTRIB, SKIP_IMAGE,
                                    # ONLY="pkg1 pkg2 ..."
  ```
  After stage 1 and after each contrib build, `build.sh` mirrors
  the produced libs + headers into the cross-toolchain sysroot at
  `${STAGE1_PREFIX}/i386-unknown-substrate/{lib,include}` and the
  matching gcc include-fixed snapshot, so the next layer's
  configure probes find them.

### Userland Tools & Display Manager
- `bin/top`: procps-grade `top(1)` — multi-source snapshot
  (`top_snapshot.c`), five-line summary + sortable process table
  (`top_render.c`), column sorting (`top_sort.c`), interactive
  terminal control with guaranteed restore and SIGWINCH resize
  (`top.c`).  RSS comes from `pmap_resident_count`; the `%Cpu(s)`
  line is held to 80 columns (procps field layout).  Unit tests under
  `tests/bin/top/`; man page `usr.man/man1/top.1`.
- `sbin/sdm` display manager: `sdm` supervises `Xfbdev` + the
  `sgreet` Xlib greeter; `sgreet` authenticates like `bin/login` and
  offers a session chooser driven by `/etc/sdm/sessions`
  (`Label = command` lines; F1 cycles).  The chosen command is
  exported as `$SDM_SESSION`, which `/etc/X11/Xsession` execs as the
  session leader (`${SDM_SESSION:-matwm2}`).

### Userland Ports (contrib/)

Every third-party userland lives under `contrib/<pkg>/` as a
patch series against an upstream tarball — never vendored
source.  Standard layout per port: `fetch.sh` (download +
SHA-verify + extract + apply), `build.sh` (configure + make +
stage into `dist-<pkg>/usr/`), `patches/` series, `series`
manifest, `README.SUBSTRATE.md`.  Current set:

- **GNU make 4.4.1** (`contrib/make/`)
- **GNU sed 4.9** (`contrib/sed/`)
- **OpenBSD expr** (`contrib/expr/`) — single-file BSD port
  alongside the OpenBSD tr port at `bin/tr/`.
- **bash-equivalent shell: zsh 5.9** (`contrib/zsh/`) — system
  `/bin/sh` is a symlink to `/usr/bin/zsh`; argv[0] detection
  puts zsh in POSIX sh emulation when invoked that way.  The
  in-tree `bin/sh/` is retained but disabled at the `bin/Makefile`
  SUBDIRS level — zsh covers everything autoconf needs.
- **ncurses 6.4** (`contrib/ncurses/`) — full terminfo backend.
  Replaces the link-time stub `lib/curses/` (kept in-tree but
  disabled at `lib/Makefile` SUBDIRS).  Brings tic / tput /
  clear / reset / tset / infocmp + the 2851-entry upstream
  terminfo database under `/usr/share/terminfo/`.  Substrate's
  hand-rolled `bin/clear` and `bin/reset` are retained as
  fallbacks for the no-ncurses embedded profile.
- **bzip2 1.0.8** (`contrib/bzip2/`)
- **gzip** (`contrib/gzip/`)
- **libarchive 3.7.7** + bsdtar (`contrib/libarchive/`)
- **OpenSSL 3.x** (`contrib/openssl/`)
- **curl** (`contrib/curl/`)
- **libiconv 1.17** (`contrib/libiconv/`)
- **mpg123** (`contrib/mpg123/`)
- **tzdata 2024a** (`contrib/tzdata/`)
- **inetutils** (`contrib/inetutils/`) — telnetd, ping, etc.
- **zlib 1.3.1** (`contrib/zlib/`) — DEFLATE/gzip runtime, pulled
  in as a dependency of mandoc.
- **mandoc 1.14.6** (`contrib/mandoc/`) — substrate's man-pager
  toolchain (`mandoc`, `man`, `makewhatis`, `apropos`, `whatis`).
  Cross-compile probe results are overridden via `configure.local`
  (HAVE_FTS, HAVE_REALLOCARRAY, HAVE_STRSEP, HAVE_STRCASESTR,
  HAVE_MKSTEMPS = 1; HAVE_WCHAR, HAVE_DIRENT_NAMLEN = 0).
  Reads/writes the `mandoc.db` index at `/usr/share/man/mandoc.db`.
- **less 692** (`contrib/less/`) — system `$PAGER` (also wired as
  `more`).  Configured with `--with-regex=posix` against
  libregex; tinfo/pcre auto-detection is suppressed via
  `ac_cv_lib_*=no`.
- **qman 1.5.1** (`contrib/qman/`) — fetched but not yet
  buildable on substrate (needs meson, cog, libbsd, ncursesw).
  Tracked under `contrib/qman/README.SUBSTRATE.md`.
- **X11 client library stack** — the six packages that build
  Xlib, in dependency order:
  - **xorgproto 2024.1** (`contrib/xorgproto/`) — X protocol
    headers (`X.h`, `Xproto.h`, `keysymdef.h`, extensions).
  - **xcb-proto 1.17.0** (`contrib/xcb-proto/`) — XCB protocol
    XML + the `xcbgen` Python generator (build-time only).
  - **libXau 1.0.12** (`contrib/libXau/`) — X authority file
    (`~/.Xauthority`) library; `libXau.so.6`.
  - **xtrans 1.6.0** (`contrib/xtrans/`) — X transport-layer
    `.c`/`.h` files compiled into libX11 (header-only port).
  - **libxcb 1.17.0** (`contrib/libxcb/`) — X C Binding;
    `libxcb.so.1` + 24 extension libraries.  A bundled
    `pkgconfig/pthread-stubs.pc` resolves the pthread-stubs
    dependency to substrate's real `-lpthread`.
  - **libX11 1.8.12** (`contrib/libX11/`) — Xlib; `libX11.so.6`
    + `libX11-xcb.so.1`.  Built `--enable-xthreads` (1.8 nests
    non-threading code inside `#ifdef XTHREADS`); uses only
    pthread mutex/cond/self, no TLS keys.
  All build shared + static.  Porting them added the POSIX
  `IN6_IS_ADDR_*` macros to `<netinet/in.h>`, a `pthread_key_t`
  type to `<pthread.h>`, and an `#ifndef bzero` guard in
  `<strings.h>`.
- **X toolkit + xterm** — `libXext` 1.3.7, `libICE` 1.1.2,
  `libSM` 1.2.6, `libXt` 1.3.1, `libXmu` 1.3.1, `libXpm` 3.5.19,
  `libXaw` 1.0.16 (Athena widgets), **`xterm` 410**
  (`contrib/xterm/`) and **`xauth` 1.1.5** (`contrib/xauth/`, the
  X authority / `MIT-MAGIC-COOKIE-1` tool).  xterm uses the core X
  bitmap fonts + Athena toolbar (Xft/freetype disabled).  No X
  server is ported — these are client-side; functional use needs
  an X server over TCP.
- **Window managers** — `matwm2` (`contrib/matwm2/`, the default
  session leader), **`twm` 1.0.12** (`contrib/twm/`, autotools) and
  **`ctwm` 4.1.0** (`contrib/ctwm/`, CMake; USE_JPEG/XRANDR/M4 off,
  HAS_REGEX pre-seeded against `libregex`, `lrand48`→`random` patch).
- **X bitmap fonts** — `font-misc-misc` 1.1.3 (the `fixed`/`9x15`
  misc family) and **`font-adobe-75dpi` / `font-adobe-100dpi`** 1.0.4
  (helvetica/times/courier/...).  Ports stage the `.bdf` sources
  verbatim (substrate has no `bdftopcf`; libXfont reads BDF directly)
  with a generated `fonts.dir`.  The adobe ports also DERIVE
  ISO8859-1 single-byte variants from the ISO10646-1 masters: the X11
  `en_US.UTF-8` `XLC_FONTSET` binds its Latin slots
  (`ISO8859-1:GL`/`:GR`) to 1-byte fonts, and with only 2-byte
  ISO10646-1 fonts present libX11's `XmbDrawString` pairs bytes into
  bogus `XChar2b` indices → tofu boxes (the "twm font bug").  See the
  port READMEs.
- **luit** (`contrib/luit/`) — Unicode/locale ISO-2022 filter that
  bridges a UTF-8 locale to a legacy-encoded child; xterm spawns it.
- **CDE** (`contrib/cde/`) — the Common Desktop Environment, cross-built
  from the cdesktopenv tree.  `hosttools/build.sh` builds the build-host
  programs CDE's configure/build need (rpcgen, mksh-as-ksh, compress,
  sessreg, mkfontdir, bdftopcf, onsgmls, tradcpp); `build.sh` assembles a
  Motif + X11 + libXinerama + libXScrnSaver + Tcl + libtirpc sysroot,
  configures `-D__linux__`, host-builds the in-tree generator tools
  (lineToData, mk_fonts_alias), and cross-builds.  Prerequisite ports:
  **libXScrnSaver** (libXss), **libtirpc** (Sun RPC for ToolTalk),
  **lmdb**, **libjpeg**, **Tcl**, **mksh**.  The CDE core desktop builds
  end-to-end — all libraries, ToolTalk, and the programs dtwm, dtfile,
  dtsession, dtterm, dtpad, dtstyle, dtcalc, dtmail, dtcm, dtprintinfo,
  dtsearchpath, dtspcd, dtscreen, dtsr, dticon, dtcreate, dtlogin, ...
  The full desktop now **comes up live**: dtsession starts ToolTalk
  (needs the kernel msg_name fix + the /etc/hosts hostname->127.0.0.1
  mapping), dtwm decorates clients and **draws the Front Panel** (clock,
  calendar, file manager, mail, workspace switch, trash, ...), and dtterm
  renders cleanly.  Three substrate fixes were needed beyond the build:
  the ld.so canonical-PLT fix (function-pointer equality — see Dynamic
  Linking Phase 4g; dtwm's front-panel widget class otherwise aborts with
  "Unresolved inheritance operation"), the libc `MB_CUR_MAX` fix (it was
  hardcoded 4 while substrate is a single-byte locale, so dtterm took the
  `wchar_t`/`XwcDrawString` path and drew each ASCII cell as a glyph + 3
  tofu boxes), and `contrib/cde/install-localized-types.sh` (+ `cdemerge.py`,
  a `merge(1)` replica) which expands the `%|nls|` placeholders and installs
  the `/usr/dt/appconfig/types` Front Panel + datatype/action database that
  the skipped `localized`/`types` clusters never staged.
  dtappbuilder (dtbuilder) + ttsnoop now build too: their `*_ui.c/_ui.h`
  are generated at build time by RUNNING dtcodegen, which links Motif —
  `hosttools/build.sh` builds a native `dtcodegen-host` against the
  build host's Motif (e.g. Arch `openmotif`) in a separate native CDE
  objdir (`hosttools/cde-host`, `-static-libtool-libs` so it is
  relocatable), and `build.sh` swaps it over the cross-built wrapper
  before src/ab and ttsnoop run the generator.  Static-link fixups it
  applies: `MRESOURCELIB=-lMrm` (referenced by dtbuilder_LDADD, never
  set by configure), libABil's yacc globals renamed (collide with
  Motif libUil.a's), ttsnoop's local `_tt_sigset` renamed (collides
  with libtt.a's).  Without a host Motif both programs are skipped, as
  before.  Still deferred (each a separate host-tooling effort,
  documented in `build.sh`): dtksh (ksh93 AST mamake cross-build),
  dtinfo + dtdocbook (SGML pmaker chain), tttypes/types (host
  tt_type_comp), the rest of `localized` (only the C-locale types
  slice is staged), dthelp parser.  The Motif port (`contrib/motif/`)
  builds libUil via Motif's WML meta-compiler (host wml/wmluiltok) and
  installs the uil/ headers.
- **ext2 toolset** — `e2fsprogs` 1.47.2 (`contrib/e2fsprogs/`):
  mke2fs / e2fsck / tune2fs / debugfs / resize2fs / ... plus the
  static libext2fs / libcom_err / libe2p / libss / libuuid /
  libblkid; and `e2tools` 0.1.0 (`contrib/e2tools/`) —
  e2cp / e2ls / e2mkdir / e2rm / e2ln / e2mv / e2tail for
  manipulating unmounted ext2/3/4 images.

### Dynamic Linking
- `/sbin/ld.so` (Substrate native dynamic linker, `sbin/ld.so/`):
  - Phase 1: bootstrap, asm `_start`, auxv walk, AT_ENTRY handoff.
  - Phase 2: parse program PT_DYNAMIC, summarize DT_*, self-relocate
    (R_386_RELATIVE).
  - Phase 3: load DT_NEEDED libs via `mmap` + MAP_FIXED, build symbol
    scope, apply REL/JMPREL relocations (R_386_RELATIVE / GLOB_DAT /
    JMP_SLOT / 32 / PC32) with eager binding.  DT_GNU_HASH preferred,
    DT_HASH fallback.  Search paths `/lib`, `/usr/lib`, `/usr/local/lib`.
  - Phase 4a: recursive (BFS) DT_NEEDED traversal — transitive deps
    pulled in automatically.
  - Phase 4b: DT_INIT and DT_INIT_ARRAY execution in dependency
    order (deepest deps first, program last).
  - Phase 4c: per-thread TLS — variant-II layout (TCB at TP, data
    at negative offsets), PT_TLS images copied into mmap'd block,
    GS base installed via the new native `sys_set_gsbase` syscall
    (SYS_SET_GSBASE = 274).  Local-exec / initial-exec models work;
    GD/LD models deferred until libpthread needs them.
  - Phase 4d: R_386_COPY support — `ld_resolve_skip` finds the
    source-of-truth in a SHARED library (not the executable
    receiving the copy); relocator memcpy's bytes sized via
    st_size.  PIE binaries don't emit COPY (they prefer GLOB_DAT
    to keep .text relocation-free) but the path is in place for
    non-PIE callers.
  - Phase 4e: runtime dlopen / dlsym / dlclose.  ld.so exports
    `__ldso_dlopen` / `__ldso_dlsym` / `__ldso_dlclose` with
    default visibility and registers itself as an ld_obj_t at
    the tail of the loaded-object scope so libdl's weak refs
    resolve.  libdl (`lib/dl/`) provides POSIX wrappers; static-
    linked binaries that pull libdl in without ld.so see NULL
    stubs that return 0 / -1 instead of crashing.
  - Phase 4f: DT_FINI_ARRAY at exit() time.  ld.so exports
    `__ldso_run_fini` (mirror of init_arrays in REVERSE init
    order with each object's array walked backwards); libc's
    `exit()` calls it via a weak ref before _exit syscall.
  - Phase 4g: canonical function addresses (function-pointer
    equality across the exe/DSO boundary).  A non-PIE executable
    that takes the *address* of a shared-library function gets a
    PLT stub from the static linker and the symbol is emitted UND
    but with `st_value` = that PLT entry (call-only UND funcs keep
    `st_value` 0).  `resolve_pred` treats such a program (list-head)
    symbol as a canonical definition at the PLT address and hands
    it to every *other* module, so `&func` is identical everywhere;
    the requesting object is threaded through `ld_resolve_req` so
    the program's own JMP_SLOT still binds the real defining DSO
    (else `GOT[f]` would point at the program's own PLT stub and a
    call self-loops).  Without this, libXt's `XtInherit*` class-
    method inheritance (`method == &_XtInherit`) failed for widget
    classes compiled into a non-PIE exe — CDE's dtwm aborted the
    whole desktop with "Unresolved inheritance operation" building
    its front panel.
  - Per-object guards: `relocated` / `initialized` / `finalized`
    booleans on ld_obj_t prevent the non-idempotent
    R_386_RELATIVE (`*p += base`) and the run-once init/fini
    arrays from firing twice when dlopen re-walks the loaded list.
- `lib/c/arch/i386/crt0.S` is now PIC-safe (`%ebx`/GOT setup, `@PLT`
  for calls, `environ@GOT` for data) so the same `crt0.o` works for
  both static and PIE binaries.
- Specs: `docs/design/ld.so-design.md`, `docs/specs/ld.so-reloc-matrix.md`,
  `docs/kernel-ldso-abi-substrate.md`, `docs/specs/abi-i386.md`.


## Current Status

### PMM API Usage (CRITICAL)
**`pmm_alloc_block()` returns VIRTUAL address (kernel direct-mapped):**
```c
void *page_virt = pmm_alloc_block();  // Returns 0xC0000000+

// For memory access: use directly
memset(page_virt, 0, 4096);           // CORRECT
*((uint32_t*)page_virt) = 0xDEADBEEF; // CORRECT

// For pmap_enter/hardware: convert to physical
uint32_t page_phys = (uint32_t)page_virt - 0xC0000000;  // REQUIRED
pmap_enter(pmap, user_va, page_phys, flags);

// WRONG patterns:
memset((void*)((uint32_t)page + 0xC0000000), ...);  // Double offset!
pmap_enter(pmap, va, (uint32_t)page_virt, ...);     // Virtual to pmap!
```

**`pmm_free_block()` expects VIRTUAL address:**
```c
pmm_free_block(page_virt);  // CORRECT - pass virtual

// If you have physical (e.g., from pmap_extract):
uint32_t phys = pmap_extract(pmap, va);
void *virt = (void*)(phys + 0xC0000000);
pmm_free_block(virt);  // CORRECT - convert first
```

## Substrate System Patterns (CRITICAL)
- **Device Naming:** Storage devices reside in `/dev/storage/` and use a `type`+`instance` pattern:
    - `ide0`, `ide1`, ... (IDE)
    - `sata0`, `sata1`, ... (SATA)
    - `scs0`, `scsi0`, ... (SCSI)
- **Mounting:**
    - The `mount` utility usage is: `mount <device> <mount_point> <filesystem_type>`.
    - `fstab` entries follow the same logic as the `mount` command.
    - The kernel automatically mounts `/proc`, `/sys`, and `/dev` after the root filesystem is established.
- **Initialization:**
    - `sbin/init` (or `etc/init.sh` copied to `/sbin/init`) should focus on starting system services and getty, as basic pseudo-filesystems are kernel-managed.

## Directives
1.  **Architecture Maintenance:** **CRITICAL:** Always read `ARCHITECTURE.md` before starting complex tasks. Update `ARCHITECTURE.md` if your changes impact the system structure or design.
    - **Format:** Maintain the structure and format defined in `ARCHITECTURE.md` (based on the template at `https://architecture.md/`).
    - **Frequency:** Update `ARCHITECTURE.md` whenever major components are added, refactored, or when directory structures change.
2.  **Code Style:** Adhere to standard kernel coding styles (similar to BSD/Linux) for C and C++.
3.  **Documentation:** Keep documentation close to the code.
4.  **Safety:** Always verify file contents before replacing.
5.  **Build System:** Maintain the recursive Makefile structure. Ensure `make -C sys`, `make -C lib/c`, and `make -C bin` always pass.
6.  **Git Operations:** Use `git mv` and `git rm` for file operations to preserve history.
7.  **TASKS.md Work Methodology:** Complete ONE checkbox at a time, update docs/specs/database/unit/property/fuzzing tests as applicable, commit, push. This applies to ALL checkboxes in `TASKS.md`, not just PMAP work.
8.  **Memory Management:** Always prefer `AGENTS.md` over `GEMINI.md` if both are present. Ensure `GEMINI.md` is not merely a symbolic link to `AGENTS.md` before treating it as separate.
9.  **Documentation Standards:** All new kernel subsystems, system calls (native personality only), and system library calls (libc, libdl, libm, libg, libpthread) MUST include corresponding Man Page documentation. Follow Linux `man-pages` style:
    - Store manual pages under `usr.man/man<section>/`, not `docs/man/`.
    - On-target install path is `/usr/share/man/man<section>/`
      (matching contrib ports and mandoc's default tree).
      `usr.man/Makefile` installs to `$(DESTDIR)/usr/share/man/`;
      `build-rootfs.sh install_to_dist()` runs
      `make -C usr.man install` so the pages are staged into
      `dist/` for `--image` to pick up.  MAN_DIRS covers
      man1..man9 (including man8 for sysadmin tools).
    - **LIBRARY:** Required for user-mode calls.
    - **SEE ALSO:** Required section.
    - **ERRORS:** Required for APIs returning error codes via `errno`. Document separately from RETURN VALUE.
    - **EXAMPLE/EXAMPLES:** Use "EXAMPLE" for single, "EXAMPLES" for multiple.
10. **Host Builds vs Target Builds (CRITICAL):**
11. **Header & Macro Standards (CRITICAL):**
12. **Autonomous Execution (CRITICAL):**
    - **Use `#askuser`:** When communicating with the user for clarification, confirmation, or feedback, always use `#askuser` rather than plain freeform prompting.
    - Default to autonomous mode: do not stop for routine confirmations.
    - Make best-effort decisions and continue immediately unless an operation is destructive (`reset`, `checkout --`, force-push) or truly blocked.
    - In dirty worktrees, commit only files related to the active task and ignore unrelated modifications.
    - Do not pause to report routine repo-state warnings; continue and summarize decisions in commit messages.
    - **No Manual Externs:** Never manually `extern` functions or variables in C files (especially syscalls). Always include the appropriate header.
    - **No Relative Includes:** Avoid relative include paths (e.g., `"../include/foo.h"`). Use include paths set in the Makefile and `<foo.h>`.
    - **Macros in Headers:** Do not define constants or macros in C files if they are arguably part of an interface or shared. Put them in headers.
    - **Target Build:** Compiles code for the Substrate kernel and userland. Uses `-nostdlib` and the project's own `lib/c/`, `lib/sys/`, etc.
    - **Host Build (`NATIVE_BUILD=1`):** Compiles code to run on the host OS (Linux, BSD, etc.) for testing purposes. Uses the **HOST OS's libc and system libraries**, NOT Substrate's.
    - **NEVER** modify `lib/c/`, `lib/sys/`, `crt0.S`, `syscall.S`, or other core libraries to support Linux or any host OS. These are for Substrate only.
    - Host builds link against the host's standard C library (e.g., glibc on Linux) via normal `cc` invocation without `-nostdlib`.
    - The `bin/sh/Makefile` and similar use `NATIVE_BUILD=1` to toggle between target and host compilation modes.

## Directory Structure Overview
- `sys/`: Kernel source.
    - `core/`: Main entry (`kmain`).
    - `arch/`: Architecture specific (`i386`, `x86_64`).
        - `i386/pmap.c`: Virtual memory management (pmap layer)
        - `i386/gdt.c`: GDT with verified segments (0x1B/0x23/0x33)
        - `i386/fpu/`: FPU emulation and state management
    - `drivers/`: Hardware drivers (`video`, `serial`, `input`, `storage`).
    - `fs/`: Filesystems (`ext2`, `fat`, `minix`, `exec`).
    - `kern/`: Kernel core subsystems (Scheduler, Time, Acct).
    - `exec/perso/`: Personality implementations.
    - `sys/`: System headers (`proc.h`).
- `bin/`: User-space utilities (`ls`, `sh`, `vi`, etc.). Test sources live in `tests/bin/<program>/`; each program's `Makefile` references them via `TESTS = ../../tests/bin/<program>`.
- `include/`: Userspace C library headers (shared by all libraries).
- `lib/`:
    - `c/`: LibC implementation **(Substrate target ONLY, never for Linux/host)**.  Builds `libc.a` AND `libc.so.0`.
    - `sys/`: System call wrapper library (libsys). Raw `syscall()` and typed wrappers **(Substrate target ONLY)**.
    - `m/`: Math library — `libm.a` + `libm.so.0`.
    - `edit/`: Command-line editing — `libedit.a` + `libedit.so.0` (DT_NEEDED libc.so.0).
    - `pthreads/`: Threading support — `libpthread.a` + `libpthread.so.0`.
    - `pwdb/`: passwd / group / shadow database helpers — `libpwdb.a`
      + `libpwdb.so.0`.  Public interface in `<sys/pwdb.h>`.  Used
      by the `useradd` / `usermod` / `userdel` / `groupadd` /
      `groupmod` / `groupdel` admin tools under `usr.sbin/` and
      by `bin/groups`.  Provides `pwdb_lock`, `pwdb_unlock`,
      `pwdb_atomic_rewrite` (write to `<file>~` + rename),
      `pwdb_next_free_id`, `pwdb_split`, `pwdb_valid_name`,
      `pwdb_today_days`, `pwdb_require_root`, `pwdb_die`.
    - `dbm/`: Database library (currently broken upstream — missing `SEEK_SET` include in `dbm.c`).
- `sbin/`: System binaries.
    - `ld.so/`: **Substrate native dynamic linker.**  Position-independent
      ET_DYN with no DT_NEEDED.  Loaded by the kernel at AT_BASE =
      0x40000000 for every PIE binary that lists `/sbin/ld.so` in its
      PT_INTERP.  Exec ABI documented in `docs/kernel-ldso-abi-substrate.md`.
- `contrib/`: Third-party components as patch series against
  upstream releases — nothing in `contrib/*/build/` is vendored,
  `fetch.sh` downloads + patches.
    - `binutils/`: GNU binutils 2.46.0 patch series + build script.
      Adds the `elf32-i386-substrate` / `elf64-x86-64-substrate` BFD
      output vecs, the `elf_i386_substrate` ld emulation, the
      ELFOSABI_SUBSTRATE constant in `include/elf/common.h`, and
      readelf's name-resolution for OSABI=64.
    - `gcc/`: GCC 16.1.0 patch series + build script.  Configures
      `i386-unknown-substrate` as a target and a libstdc++-v3 OS
      port at `libstdc++-v3/config/os/substrate/`.
    - `build-toolchain.sh`: orchestrates both at stages 1 and 2.
    - `ext2-boot/`: third-party BIOS ext2 bootloader (submodule).
- `tests/`:
    - `bin/`: Per-program test suites for `bin/` utilities.
    - `lib/pthread/`: portable POSIX `torture_kernel.c` +
      substrate-specific `torture_pthread.c` (scheduler stress + libpthread surface).
    - `lib/m/`: unit + property tests for libm including
      `test_bessel.c` / `prop_bessel.c` and the rest.

### Kernel Debugging
When debugging kernel crashes (including triple faults):

1. **QEMU with GDB:**
   ```bash
   # Terminal 1: Start QEMU with debugging, -no-reboot stops on triple fault
   qemu-system-i386 -kernel sys/kernel.bin -no-reboot -s -S
   
   # Terminal 2: Connect GDB
   gdb -ex "file sys/kernel.bin" -ex "target remote :1234"
   ```

2. **Single-Step Debugging:**
   - Use `si` (step instruction) one at a time in gdb
   - Use `break <function>` to set breakpoints
   - Use `info registers` to check CPU state
   - When QEMU hits triple fault with `-no-reboot`, it halts and gdb shows connection closed

3. **Exception Trapping:**
   - Set breakpoints on IDT handlers: `break isr_common_stub`, `break double_fault_handler`
   - Use QEMU monitor (`Ctrl+Alt+2`) for low-level CPU inspection

4. **Debugging Principles:**
   - **Never recreate code** - always restore from git history when reverting changes
   - **Single-step from crash point** - triple faults don't return to gdb, so step one instruction at a time
   - **Check BSS/stack** - large static arrays can cause stack overflow or memory corruption

### Debugging Note
If the kernel hangs in `hlt`, check `eflags` bit 9. If `IF=1`, the IRQ may be masked at the PIC or the controller state is stuck.

## Known Issues
- Interrupt responsiveness: `Ctrl+F9` debug dump sometimes fails during idle states.
- **PMAP Memory Overhead:** 32 identity-mapped kernel PDEs (0-31) in userspace consume 128KB+ per process.

## Next Steps
- Refactor PMAP to dynamically allocate page tables (reduce 128KB overhead)
- Implement mmap() syscall with personality driver integration
- Flesh out 9P filesystem logic implementation
- /sbin/ld.so remaining work: per-thread TLS for additional
  threads (currently only the initial thread is set up;
  pthread_create needs libpthread to allocate per-thread blocks
  via the same layout); GD/LD TLS models (need __tls_get_addr,
  only matters once dlopen-loaded libs use them); RTLD_NEXT
  semantics (need to know which object the caller was in);
  proper dlclose unmap + reverse-dependency safety; dlerror
  string buffer.
- Switch userland binaries to dynamic linking incrementally — start
  with non-bootstrap-essential ones (`bin/echo` is a good first
  candidate) and verify before expanding.
- Fix `lib/dbm/dbm.c` (missing `SEEK_SET`/`SEEK_CUR` include) so
  libdbm.{a,so.0} build cleanly.
- **Make gas stamp ELFOSABI_SUBSTRATE on output `.o` files.**
  Currently gas emits `ELFOSABI_SYSV` (0); we sidestep with
  `ELF_OSABI_EXACT=0` in the substrate BFD vec.  Real fix is in
  `gas/config/obj-elf.c` (or a new gas/config/te-substrate.h)
  to set the OSABI byte when the target is `i386-unknown-substrate`.
- Record telnet/ssh pts logouts in wtmp.  Console getty logins are
  covered — init writes the `DEAD_PROCESS` record when it reaps a
  getty line — but telnetd/sshd sessions are supervised by those
  daemons and would each need to write their own pts logout.
- Build a shared `libstdc++.so.6` once a real userland is up
  (currently shipping `libstdc++.a` only — `--disable-shared`).
  Requires ld.so to handle C++ symbol versioning, vague linkage
  dedup, and TLS GD/LD models.
- Rebuild stage 2 GCC with `--with-arch=i486` to drop the
  pentium-pro default and produce binaries that run on plain i486
  QEMU CPUs.
