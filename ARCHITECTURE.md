# Architecture Overview

This document describes how Substrate is put together: what each part of
the tree is, how the parts depend on each other, and where the detailed
specifications live. Update it when the structure changes. Building and
running are covered in `README.md`; engineering rules in `AGENTS.md`.

## 1. Project Structure

```text
substrate/
├── sys/                  # The kernel
│   ├── arch/i386/        #   CPU, MMU, interrupts, boot, SMP discovery
│   ├── arch/x86-common/  #   code shared by both x86 ports (I/O APIC, port I/O)
│   ├── arch/x86_64/      #   x86-64 port: boots to long mode (milestone 0)
│   ├── boot/             #   in-tree BIOS bootloader (stage 1 asm + stage 2 C)
│   ├── core/             #   early initialisation
│   ├── kern/             #   scheduler, signals, time, sync, IPC, syscalls, PCI
│   ├── pm/               #   process lifecycle
│   ├── vm/               #   PMM, pmap, VM objects, paging
│   ├── vfs/  fs/         #   VFS, block cache, devfs/procfs/sysfs; ext2, FAT, exFAT, UDF, minix, 9P
│   ├── drivers/          #   storage, USB, audio, net, input, video, virtio
│   ├── net/              #   IPv4/IPv6, TCP/UDP, AF_INET/AF_UNIX/AF_PACKET
│   └── exec/             #   executable formats and personalities
├── lib/                  # Runtime libraries, each built as libX.a and libX.so.0
│                         #   c, sys, m, pthread, dl, rt, edit, pwdb, usb, ...
├── usr.lib/              # Support libraries: regex, elfobj, exvi, demangle, bc, ...
├── sbin/ld.so/           # The dynamic linker
├── bin/  sbin/           # Base commands and system tools (init, getty, sdm, ...)
├── usr.bin/  usr.sbin/   # Additional tools and administration commands
├── include/              # Userspace headers
├── contrib/              # Third-party ports: fetch.sh + build.sh + patches/ each
├── etc/                  # Target /etc, including the rc.d boot scripts
├── usr.man/              # Manual pages (man1 ... man9)
├── tests/                # Host and target test suites
├── linux/                # Linux-host bridges (binfmt_misc runner for Substrate ELFs)
├── docs/                 # Changelog, port catalogue, specs/ and design/ documents
├── tools/  scripts/      # Build helpers (contrib ordering check, sysroot sync)
├── .github/workflows/    # CI
├── build.sh              # Full build
├── build-rootfs.sh       # Assembles dist/ and bakes rootfs.img
└── run-networking.sh     # QEMU launcher
```

Build outputs that are not tracked: `dist/` (staged root filesystem),
`dist-overlay/dist-<pkg>/` (each port's staged files), `contrib/*/build/`
(downloaded and patched upstream sources), `rootfs.img`, and the cross
toolchain in `/opt/substrate`.

## 2. High-Level System Diagram

At run time:

```text
 programs (static, or dynamic: PIE or not)
     │  PT_INTERP
     ▼
 /sbin/ld.so ──── maps lib*.so.0, relocates, sets up TLS, runs initialisers
     │
     ▼
 libc · libm · libpthread · librt · libdl · ... ─── libsys (syscall stubs)
     │  system-call boundary (native, or a foreign personality's ABI)
     ▼
 kernel:  exec/personalities ── process & scheduler ── VM
          VFS + block cache ── filesystems
          network stack ── drivers ──► hardware (or QEMU)
```

At build time:

```text
 Linux host ──► stage 1: cross toolchain (/opt/substrate)
                    │
                    ├──► kernel, libraries, ld.so, userland ──► copied into the sysroot
                    ├──► stage 2: GCC/binutils that run on Substrate
                    └──► contrib ports, in dependency order ──► each copied into the sysroot
                                         │
                    build-rootfs.sh ◄────┘  dist/ ──► rootfs.img
```

The kernel, libraries and toolchain change together: an ABI change at any
boundary in the first diagram has to be made on both sides at once.

## 3. Core Components

### 3.1. Kernel

Monolithic, written in C with i386 assembly, built freestanding
(`-nostdlib -ffreestanding`). `make -C sys` produces `kernel.bin`
(text console, for QEMU `-kernel`), `kernel.fb.bin` (framebuffer; installed
as `/vmunix`) and `kernel.efi`.

**Processes and scheduling.** Every `process_t` is allocated dynamically and
found through a pid hash plus an all-processes list; there is no
process-table limit. SMP discovery parses the ACPI MADT (or MP tables) for
CPUs, I/O APICs and interrupt overrides; application processors are started
but then halt, so scheduling is single-CPU. `nosmp` keeps them from
starting but still registers the I/O APICs.

**Inter-process communication** (`sys/kern/`): System V semaphores
(`ipc_sem.c`) and shared memory (`ipc_shm.c`); POSIX message queues
(`posix_mqueue.c`) — a fixed table of named queues with priority-ordered
messages, per-queue `ipc_perm`, `SIGEV_SIGNAL` notification, and
interruptible, timeout-aware blocking — whose userspace side (`mq_*`) is in
`lib/rt`; POSIX semaphores (`posix_sem.c`); and futexes (`futex.c`).
Terminals and ptys are drivers, under `sys/drivers/console`.

**Memory** (`sys/vm/`): physical page allocator, a two-level i386 pmap with
the kernel in a direct map above `0xC0000000`, VM objects, and
demand-paged user stacks. Always-on corruption tripwires — a `vm_object`
magic canary, a buddy-allocator double-allocation check, a UMA
double-free guard, and a `vm_map` auditor — turn heap corruption into an
immediate panic at the point it happens. Kernel stacks are 16 KiB, enough
for the deepest nesting (the network transmit path interrupted by a
receive interrupt).

**Filesystems** (`sys/vfs/`, `sys/fs/`): ext2 read-write, including
extent-mapped and htree-indexed directories; FAT and exFAT read-write
(exFAT maintains its allocation bitmap, FAT chains, entry-set checksums and
up-case table, and passes host `fsck.exfat`); UDF read-write; minix; 9P;
and devfs, procfs, sysfs and shmfs, which the kernel mounts itself after
the root. The block cache (`sys/vfs/bio.c`) sits under the block-device
layer, keyed by device and sector with read coalescing and write-through,
so no filesystem or driver carries caching logic
(`docs/design/block-cache-consolidation.md`).

**Drivers** (`sys/drivers/`):

- *PCI and interrupts.* Drivers that need an interrupt (`hda`, `e1000e`,
  `r8168`) try MSI first, then the firmware's Interrupt Line, then
  `pci_route_intx()` (the conventional PIRQ swizzle onto an I/O APIC
  input), and prove each source with an interrupt the device raises before
  relying on it — UEFI firmware leaves Interrupt Line at 0xFF.
- *Storage.* AHCI (also binding Intel RST RAID-mode controllers), IDE,
  NVMe, floppy, ramdisk, virtio-blk and virtio-scsi, and USB mass storage
  (Bulk-Only and UAS) through a common SCSI mid-layer. Disks appear as
  `/dev/storage/<type><n>` (`sata0`, `ide0`, `scsi0`, ...) with partitions
  as `p<n>`.
- *USB.* UHCI, EHCI and xHCI host controllers; hubs; HID keyboards and
  mice; mass storage; USB Audio Class; hot-plug through the `usbhotplug`
  kernel process. Devices appear in `/proc/devtree` and under `/dev/usb`,
  which `lsusb` reads.
- *Audio.* A Sun-compatible framework (`/dev/audio`) with an OSS front end
  (`/dev/dsp`), over AC'97, Intel HD Audio, SB16, USB Audio and a null
  backend. Applications may write any linear PCM from 8 to 32 bits, 1 to 8
  channels, 4 to 192 kHz; the framework negotiates a format each backend
  accepts and converts to it, downmixing and resampling as needed. Each
  backend decouples `write()` from its DMA ring through a deep software
  FIFO refilled from the completion interrupt. The HDA stream engine is an
  explicit stop/reset/start state machine, because the specification makes
  RUN asynchronous and the descriptor registers writable only after a
  reset; its interrupt handler never writes the stream control register,
  deferring the halt to process context.
- *Network.* `rtl8139`, `e1000` (8254x), `e1000e` (82574 and the
  chipset-integrated I217/I218/I219), `r8168` (RTL8111/8168) and
  `virtio_net`, each registering a `netdev_t`.
- *Input and video.* PS/2 and USB HID input feed one evdev-style ring read
  as `/dev/input/event0`. Video: VGA text, a framebuffer console (inherited
  from the bootloader, Bochs/QEMU BGA, or virtio-gpu) with PSF/BDF fonts
  and virtual terminals.

**Network stack** (`sys/net/`): IPv4 and IPv6, TCP, UDP, ICMP, ARP, and
AF_INET, AF_UNIX and AF_PACKET sockets, with loopback. IPv4 input
reassembles fragments (a bounded 8-datagram table, 30 s timeout) before
dispatch, so UDP datagrams up to 65,507 bytes arrive whole; output never
fragments. TCP looks up connections through a hash on local port, remote
address and remote port, and listeners through a hash on local port.
Routing is per interface: each `netdev_t` has a primary address, netmask
and gateway plus up to eight aliases, and every "is this ours / on-link /
broadcast" question goes through the `ip4_dev_*` helpers so aliases count
everywhere. Output takes per-socket options (TTL, TOS, multicast,
don't-fragment, up to 40 bytes of IP options); a 16-entry path-MTU cache
fed by ICMP (RFC 1191) bounds don't-fragment datagrams, and TCP
resegments when it shrinks.

**Executables and personalities** (`sys/exec/`): the loader picks a
personality from the ELF OSABI byte, an a.out header's machine ID, or an
`x.out` header's CPU field, and each personality supplies the system-call
table and process setup for its ABI. Foreign binaries see their own
system's files through `/perso/<name>/` trees. The segmented 16-bit
personalities — ELKS (`elks_aout.c`) and SCO Xenix/286 (`xout286.c`) — run
in real LDT segments rather than a flattened address space, so the
selectors a 1980s linker baked into a binary resolve as they did on
hardware. `x.out` covers 8086, 80286 and 80386 Xenix under one magic
number; 80286 programs go to `xout286.c` (16-bit, `int $5` system calls)
and 80386 programs to `xout.c` (32-bit, `lcall $7,$0`). State of each
personality: `README.md`; specifications:
`docs/specs/personality_targets.md`, `docs/specs/personality_elks.md`,
`usr.man/man4/sco_x286.4`, `usr.man/man4/xout286.4`.

**x86-64.** `sys/arch/x86_64` is the 64-bit port, an addition alongside
i386, not a replacement: the personality and the bitness of a process are
independent. Both kernels build from the same tree; `make -C sys
ARCH=x86_64` builds the 64-bit one into its own object directory and
produces `kernel-x86_64.bin`, installed as `/vmunix64`. It boots through
multiboot 1 or 2 to the higher half with a direct map, and has its GDT
(including the 32-bit user code segment), TSS and IDT; the machine-
independent kernel, the 4-level pmap and the compat32 path that runs the
existing i386 userland are next. The userland stays 32-bit for now. Native
64-bit programs will use the FreeBSD/amd64-based ABI in
`docs/specs/abi-amd64.md`; the port itself is described in
`docs/specs/arch_x86_64_core.md`.

Kernel specifications, in `docs/specs/`:

| Area | Documents |
|---|---|
| Boot and initialisation | `kmain_init.md`, `arch_i386_boot.md`, `bootloader_ext2_boot.md` |
| Memory | `pmm.md`, `pmap.md`, `vm_subsystem.md` |
| Processes | `kern_process_exit.md`, `kern_pid1.md` |
| Filesystems | `fs_devfs.md`, `vfs_bio.md` |
| Devices and terminals | `driver_model.md`, `driver_tty.md`, `driver_vt.md`, `driver_fb_console.md`, `driver_virtio_gpu.md` |
| Personalities | `personality_targets.md`, `personality_elks.md` |

The ISA side of the driver model covers fixed legacy probes and ISA
Plug-and-Play isolation; activated PnP devices are registered on the ISA
bus with their resources, so UART, LPT and IDE bind through the driver
model. Virtual terminals take their geometry from the console backend and
reserve the last row for a kernel status line.

### 3.2. Dynamic linker (`sbin/ld.so`)

The kernel loads it as the `PT_INTERP` of every dynamically linked native
program, PIE or not. It:

- loads `DT_NEEDED` libraries recursively, searching the default
  directories and those listed in `/etc/ld.so.conf` and
  `/etc/ld.so.conf.d/`; `DT_RPATH` and `DT_RUNPATH` are not implemented;
- resolves symbols through `DT_GNU_HASH` or `DT_HASH` and applies the i386
  REL/JMPREL relocations (RELATIVE, GLOB_DAT, JMP_SLOT, 32, PC32, COPY,
  TLS_TPOFF);
- sets up variant-II thread-local storage, setting the GS base with the
  native `sys_set_gsbase` call, including for libraries loaded later with
  `dlopen`;
- runs `DT_INIT_ARRAY`, and `DT_FINI_ARRAY` at exit through a weak hook
  libc calls from `exit()`;
- provides `dlopen`/`dlsym`/`dlclose` and `dl_iterate_phdr(3)`;
- keeps function addresses canonical for non-PIE executables: when a
  program takes the address of a library function, every module sees the
  program's PLT stub as that address, so `&func` compares equal everywhere
  (Xt's `XtInherit*` machinery, and so CDE's front panel, depends on it);
- re-protects library text read-only and executable, and makes
  `PT_GNU_RELRO` regions read-only after relocation (W^X and RELRO).

Per-object flags stop non-idempotent relocations and run-once initialisers
from repeating when `dlopen` walks the loaded list again.
`dl_iterate_phdr` is what lets C++ exceptions unwind across shared-library
boundaries (see 3.5). Design: `docs/design/ld.so-design.md`; relocation
matrix: `docs/specs/ld.so-reloc-matrix.md`; kernel contract:
`docs/kernel-ldso-abi-substrate.md`.

### 3.3. Runtime libraries (`lib/`, `usr.lib/`)

Every library under `lib/` is built twice from one source tree:

- `libX.a`, compiled `-fno-pie` for static links;
- `libX.so.0`, compiled `-fPIC` and linked
  `-shared -Bsymbolic-functions -z now`, with soname `libX.so.0`.
  The `libX.so` link-time name is created only when installing, so that in
  the source tree `-lX` still finds the archive and a static link cannot
  pick up the shared object by accident.

Build flags are in `Makefile.inc` (`SHLIB_CFLAGS`, `SHLIB_LDFLAGS`).
Shared objects are branded with Substrate's ELF OSABI (64).

- **`libsys`** alone owns the raw `syscall()` entry and the typed `sys_*`
  wrappers, and every program links it (`-l:libsys.a` in a link group with
  libc/libm for static links, `-l:libsys.so.0` for dynamic ones). A program
  calling `syscall()` itself needs libsys on its link line, because the
  linker will not follow `libc.so.0`'s `DT_NEEDED` to satisfy an object
  file's reference.
- **`libc`** returns errors through `errno`; kernel calls return
  `-errno`, which libc negates.
- **`lib/rt`** (`-lrt`): POSIX message queues as shims over the kernel's
  queue calls, and POSIX asynchronous I/O (`aio_*`, `lio_listio`) as a
  userspace worker-thread pool over libpthread.
- **`lib/edit`**: line editing and history for shells and prompts.
- **`lib/pwdb`** (`<sys/pwdb.h>`): `/etc/passwd`, `/etc/group` and
  `/etc/shadow` parsing, atomic rewrite (write a copy, then rename),
  `flock` locking, ID allocation and name validation, shared by the
  account tools.
- **`usr.lib/elfobj`**: ELF reading for `nm`, `readelf`, `ar` and friends
  (`usr.lib/elfobj/README.md`, `ABI_POLICY.md`).
- **`usr.lib/exvi`**: the shared implementation of `ex` and `vi`.

`lib/c/arch/i386/crt0.S` serves static and dynamic programs alike: it
finds the GOT with `call`/`pop`, reaches `environ` through the GOT, and
calls `__stdio_init`, `main` and `exit` through the PLT. A static link
resolves those references directly; for a dynamic program ld.so fills the
GOT before `_start` runs.

### 3.4. Userland

- **Shell:** `/bin/sh` is zsh 5.9 (`contrib/zsh`), which switches to POSIX
  `sh` emulation when invoked as `sh`. The older in-tree `bin/sh` is kept
  but not built.
- **Terminal handling:** ncurses 6.4 with the full terminfo database; the
  old `lib/curses` stub is kept but not built.
- **Editors:** `ex`/`vi` with a shared engine in `usr.lib/exvi`
  (`docs/specs/exvi.md`, `docs/specs/exvi_conformance.md`), plus `nano`.
- **Manual pages:** Substrate's own pages in `usr.man/`, installed to
  `/usr/share/man`; `mandoc` provides `man`, `apropos` and `makewhatis`
  (database `/usr/share/man/mandoc.db`), and `less` is the pager.
- **Accounts:** `useradd`/`usermod`/`userdel`, `groupadd`/`groupmod`/
  `groupdel` and `groups`, all over `lib/pwdb`.
- **Init and services:** `sbin/init` runs the scripts in `/etc/rc.d` in
  name order: `00-fsck` checks the root and remounts it read-write,
  `03-fc-cache` brings the fontconfig cache up to date, and the rest start
  syslog, D-Bus, loopback, `rpcbind` and the ToolTalk database server,
  networking (DHCP through `dhclient`), `atd`, `telnetd`, `inetd`, `sshd`,
  `nginx`, and finally the display manager.
- **Display manager:** `sbin/sdm` runs an `Xfbdev` server and the `sgreet`
  login greeter, which checks `/etc/passwd` and `/etc/shadow` like `login`,
  offers the sessions listed in `/etc/sdm/sessions`, and starts the chosen
  one through `/etc/X11/Xsession` (`matwm2` by default). When the session
  ends, X is torn down and a fresh greeter starts.
- **`find`**: several dialects in one program (`docs/find/architecture.md`).

### 3.5. Toolchain

GNU binutils 2.46.0 (`contrib/binutils`) and GCC 16.1.0 (`contrib/gcc`),
patched for `i386-unknown-substrate`, built in two stages by
`contrib/build-toolchain.sh`: stage 1 is a cross toolchain on the Linux
host, stage 2 a Canadian cross that runs on Substrate and is installed in
the image as `cc`/`gcc`/`g++`/`as`/`ld`. C defaults to gnu17.

C++ exceptions work across shared-library boundaries: the GCC patches
enable `PT_GNU_EH_FRAME` lookup and `--eh-frame-hdr`, and build a shared
`libgcc_s`, so one unwinder finds every module's frame tables at throw time
through `dl_iterate_phdr`. A throw in a `.so` is caught in the program.
C++ code using `std::mutex` must link `-lpthread`. `libstdc++.so.6` is
usable and preferred over the static archive. `gdb` (`contrib/gdb`) runs
natively on top of `ptrace(2)`. Details: `docs/toolchain.md`,
`contrib/BUILD-TOOLCHAIN.md`.

### 3.6. Graphics and desktops

The X server is `Xfbdev` (the kdrive framebuffer server from
`contrib/xorg-server`), drawing to `/dev/fb0`. On top are the X11 client
libraries and applications, Motif, Tk, GTK 1 and 2, SDL3 with
`sdl2-compat` and `sdl12-compat`, the window managers `matwm2`, `twm` and
`ctwm`, and two desktops: CDE and TDE (Trinity). Text is drawn either with
the X core bitmap fonts (`font-misc-misc`, `font-adobe-*`, `font-bh-lucida`)
or, for Xft/cairo/Pango clients, with fontconfig over DejaVu (the default
sans, serif and monospace), Liberation (metric-compatible with Arial,
Times New Roman and Courier New), the Go fonts, and GNU Unifont as the
fallback for characters nothing else has.

### 3.7. Third-party ports (`contrib/`)

Each port is `contrib/<pkg>/` with `fetch.sh` (download, check the pinned
SHA-256, extract, apply `patches/`), `build.sh` (configure, cross-compile,
stage into `dist-overlay/dist-<pkg>/usr/`) and `README.SUBSTRATE.md`.
Upstream source is never committed. `build.sh` at the top level builds them
in the order of its `DEFAULT_CONTRIB` list, which
`tools/check-contrib-order.sh` checks against each port's declared
dependencies. The catalogue is `docs/contrib-ports.md`.

## 4. Data Stores

Persistent layouts the system and its build rely on:

- **`rootfs.img`** — a 4 GiB MBR-partitioned disk. GRUB's BIOS core sits in
  the gap after the MBR. Partition 1, at 1 MiB, is a FAT32 EFI System
  Partition labelled `sub-boot` with `BOOTX64.EFI` and `BOOTIA32.EFI`.
  Partition 2 is the ext2 root, labelled `sub-root`, holding `/vmunix`.
  The kernel finds its root with `root=LABEL=sub-root`. GRUB boots it
  `ro`, and `/etc/rc.d/00-fsck` checks it before remounting read-write.
- **`dist/`** — the staged root filesystem, assembled by `build-rootfs.sh`
  from the native build and every `dist-overlay/dist-<pkg>/` tree
  (`docs/specs/rootfs.md`).
- **The cross sysroot** — `/opt/substrate/i386-unknown-substrate/{lib,include}`,
  refreshed from the native libraries and each port's staging tree as the
  build goes, so later ports' `configure` scripts find earlier ones.
  GCC's `include-fixed` copies of headers shadow the sysroot's, so headers
  are mirrored there too (`scripts/sync-sysroot.sh`).
- **On the target:** accounts in `/etc/passwd`, `/etc/group`, `/etc/shadow`;
  login records in `/var/log/wtmp`; the fontconfig cache in
  `/var/cache/fontconfig`, built on the target at boot because the build
  host's fontconfig writes a newer cache format; the manual-page database
  `/usr/share/man/mandoc.db`.

## 5. External Interfaces

- **System-call ABIs.** The native ABI (`include/arch/i386/syscall.h`,
  catalogued in `docs/syscalls/native-syscall-catalog.md`) plus each
  personality's foreign ABI. Adding a native call means giving it a number
  in both `include/arch/i386/syscall.h` and `sys/arch/i386/syscall.h`,
  entries in the native personality's table, name list and argument specs
  (`perso_native.c`), and a line in the catalogue.
- **Executable formats** (`sys/exec/formats/`): ELF, a.out (including
  Linux ZMAGIC/QMAGIC/OMAGIC and ELKS), COFF, PE, Xenix `x.out`, and `#!`
  scripts.
- **Upstream sources.** Every port downloads a pinned release and checks
  its SHA-256 before use; for recently added ports the hash is
  cross-checked against another distribution's record of the same file.
- **The build host.** Package requirements are in `README.md`; the CI
  workflow is the reference for a clean Ubuntu host.

## 6. Deployment & Infrastructure

Ways the system boots:

| Path | How |
|---|---|
| QEMU direct | `-kernel sys/kernel.bin` with `-append "root=LABEL=sub-root"`; `run-networking.sh` wraps this. |
| BIOS | GRUB from the image's MBR loads `/vmunix` by multiboot2. |
| UEFI | `BOOTX64.EFI` (64-bit firmware) or `BOOTIA32.EFI` (32-bit) from the EFI partition, then the same GRUB configuration. |
| Real hardware | The image written to a USB stick or SD card; tested on several laptops, Haswell to Whiskey Lake, booting from USB storage. |

GRUB's menu has a normal entry, a serial-console verbose entry, and
diagnostic entries for USB bring-up and SMP.

CI (`.github/workflows/`): `ci.yml` runs on every push — the kernel, the
host test suite, the userland libraries and utilities, the per-utility
test scripts, and an image build. `bootstrap.yml` runs the full
`build.sh` from a clean Ubuntu runner, toolchain included (several hours;
the toolchain is cached between runs).

## 7. Security Considerations

- **Accounts.** Passwords are hashed in `/etc/shadow`; `login`, `sgreet`
  and `su` share one policy. Account changes rewrite the files atomically
  under a lock.
- **Memory protection.** Page 0 is unmapped. ld.so enforces W^X on library
  text and RELRO on relocated data. The kernel heap tripwires (3.1) stop
  silent corruption from propagating.
- **Kernel interfaces.** System calls validate user pointers through
  `copyin`/`copyout`; `/dev/kmem` is read-only; `O_NOFOLLOW` and
  `O_DIRECTORY` are enforced at open.
- **Randomness.** `/dev/random` and `/dev/urandom` come from a CSPRNG
  seeded from RDRAND/RDSEED where present, and from timer jitter where not;
  the TCP initial sequence numbers and IP identification fields use it.
- **Foreign binaries** look up their libraries and configuration under
  their own `/perso/<name>/` tree first, and are refused Substrate's
  dynamic-linker configuration, so a foreign dynamic linker does not pick
  up Substrate libraries.

## 8. Development & Testing Environment

Building and running: `README.md`. Test layers:

- **Host tests** (`NATIVE_BUILD=1`): kernel and library logic compiled
  with the host compiler against mocks, under `tests/` (`make -C tests`,
  which CI runs). These link the host's libc; target libraries are never
  changed to suit a host.
- **Kernel self-tests:** `make -C sys KERNEL_TESTS=1`, selected at boot
  with `test=`.
- **Target tests:** programs under `tests/sys/`, `tests/lib/`,
  `tests/bin/<program>/` and `tests/usr.bin/<tool>/`, cross-compiled and
  run in a guest; each prints a `Result:` line. Tests always live under
  `tests/`, never inside `lib/`.
- **Torture suites** stress pipes, signals, threads, shared memory and
  ptys, and run the same tests under the native, Linux and FreeBSD
  personalities.
- **Conformance:** the Open POSIX Test Suite (`contrib/posixtestsuite`);
  the toolchain is regression-tested by building large real-world ports.
- **Debugging:** QEMU's gdb stub (`run-networking.sh --debug`); see
  `.claude/skills/kernel-debugging/SKILL.md` for the crash workflow.

## 9. Future Considerations / Roadmap

- x86-64, added alongside i386: the MI kernel, 4-level pmap, compat32 for
  the i386 userland, then a native amd64 userland
  (`docs/specs/arch_x86_64_core.md`).
- Scheduling on application processors (they are started but idle).
- pmap: allocate page tables on demand instead of mapping 32 kernel page
  directory entries into every process.
- ld.so: `DT_RPATH`/`DT_RUNPATH`, `RTLD_NEXT`, full `dlclose` unmapping.
- The Linux personality's remaining system-call surface.
- Drivers the test laptops lack: Intel Wi-Fi, SD host controllers (SDHCI),
  I²C HID touchpads, and the e1000e PHY's Ultra Low Power exit.
- `gas` stamping Substrate's OSABI on object files itself.

## 10. Project Identification

- **Name:** Substrate
- **Target:** i386 (32-bit x86); x86-64 kernel in bring-up.
- **Toolchain triple:** `i386-unknown-substrate`
- **Licence:** © 2026 Kirn Gill II, all rights reserved (`LICENSE`);
  third-party ports keep their own licences.

## 11. Glossary

| Term | Meaning |
|---|---|
| Personality | A kernel module giving executables of another system (Linux, FreeBSD, Xenix, ...) their native system-call ABI and process setup. |
| OSABI | The ELF header byte that names a binary's target OS; Substrate's is 64, and the kernel dispatches personalities on it. |
| Stage 1 / stage 2 | The cross toolchain that runs on the build host, and the native one built with it that runs on Substrate. |
| Sysroot | `/opt/substrate/i386-unknown-substrate`, the headers and libraries the cross compiler builds against. |
| `dist/`, `dist-overlay/` | The staged root filesystem, and each port's staging tree that is merged into it. |
| `Xfbdev` | The X server, drawing directly to the framebuffer. |
| `sdm`, `sgreet` | The display manager and its login greeter. |
| `/perso/<name>` | The directory tree a foreign personality's programs see as their libraries and configuration. |
| PCH | Intel's platform controller hub (the chipset), which integrates storage, USB, audio and Ethernet controllers. |
