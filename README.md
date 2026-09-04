# Substrate

Substrate is a Unix-like operating system for x86 (32-bit i386 primary,
x86-64 in progress). It is a whole system, not just a kernel: the tree
contains the kernel, a C library and dynamic linker, base userland, a
patched GNU toolchain that runs *on* Substrate, and a large set of ported
third-party software (zsh, ncurses, OpenSSL, an X11 client stack, SDL2 with
an audio backend, the PsyMP3 player, gdb, …).

Recent work brings up audio and multimedia playback (a Sun-compatible
in-kernel audio stack behind AC'97 / Intel HDA backends, exposed through
SDL2's `/dev/audio` backend and the PsyMP3 music player), substantially
faster local X (a larger AF_UNIX socket buffer lifts an SDL UI from sub-1
to ~23 fps), and the toolchain pieces for C++ exceptions across shared
libraries (`dl_iterate_phdr` in the dynamic linker plus a shared `libgcc_s`;
a final toolchain rebuild is still pending).

A distinguishing goal is **binary compatibility via exec personalities**: the
kernel routes each executable to a loader from its ELF OSABI byte (or, for
a.out, its MID/flavor; for Xenix `x.out`, the CPU field of its header), so
binaries built for other systems run on the same kernel. Current state:

- **Native** — complete.
- **ELKS** (16-bit Linux-like a.out) — done: 16-bit protected-mode execution
  through a per-process LDT, ELKS `INT 0x80` syscall convention, signal
  callbacks, and a synthetic `/dev/kmem` so upstream ELKS `ps`/`meminfo` run.
- **SCO Xenix/286** — done: 16-bit protected-mode System V.2 programs in the
  segmented `x.out` format, each segment mapped into its own 64 KiB window
  with the LDT slot the linker baked into the binary, so a middle-model
  image's `lcall $0x47,$off` resolves as it did on hardware. The Xenix trap
  ABI is emulated (`int $5`, call number in AX with a sub-function in AH,
  arguments in BX/CX/SI/DI, results in AX:BX), along with V7 far-call signal
  frames, the 30-byte Xenix `struct stat`, the `termio` ioctl group, and
  synthesized V7 `struct direct` records for `read(2)` on a directory since
  Xenix/286 has no `getdents(2)`. A 37-command sample of the SCO media runs
  clean (37 ok, 0 crash, 0 noload), the shipped `cc` compiles and links end
  to end with correct floating point, and Microsoft Word 3.0 reaches its full
  editing screen. Xenix/86 binaries run under the same personality; Xenix/386
  (`x.out` via the `lcall $7,$0` gate) is a separate, active personality.
- **FreeBSD** and **NetBSD** — dynamic linking is up and running: their
  run-time linkers (`ld-elf.so.1` / `ld.elf_so`), TLS install, and libc come
  up, so dynamically-linked ELF binaries load and execute.
- **Linux** — active development (syscall surface and ELF/auxv process
  setup in place).
- **OpenBSD**, **SunOS 4.x**, and **SVR3/SVR4** — earlier-stage personalities.

See `docs/specs/personality_targets.md` and `docs/specs/personality_elks.md`;
Xenix/286 has its own pages, `usr.man/man4/sco_x286.4` (the personality) and
`usr.man/man4/xout286.4` (the executable format).

## Repository layout

```text
sys/          kernel (core, arch/i386, drivers, fs, vm, net, exec personalities)
bin/ sbin/    base user and system commands
usr.bin/      additional tools (ldd, sysctl, binutils-style utilities, …)
usr.sbin/     admin tools (useradd, sdm display manager, …)
lib/          target runtime libraries: libc, libsys, libm, libpthread, libdl
usr.lib/      reusable libraries (regex, elfobj, …)
sbin/ld.so/   native dynamic linker (PT_INTERP for every PIE)
include/      userspace headers
contrib/      third-party ports as patch series (toolchain, zsh, X11, …)
usr.man/      manual pages (man1..man9), installed to /usr/share/man
tests/        host- and target-runnable test suites
docs/         CHANGELOG and subsystem/tool specs (docs/specs/)
dist/         staged target root filesystem (input to the image bake)
```

`ARCHITECTURE.md` describes the system structure; `AGENTS.md` records the
engineering constraints; `docs/CHANGELOG.md` is the detailed changelog.

## Prerequisites

A Linux build host with:

- a multilib GCC/Clang that can target `-m32` (`gcc-multilib` on Debian/Ubuntu)
- GNU make, `binutils`
- `e2fsprogs` (the image is baked with `mke2fs` + `debugfs`)
- `qemu-system-i386` to run it
- optional: `zstd` (the tracked image may ship compressed as `rootfs.img.zst`)

The Substrate-native GNU toolchain (used to build the on-target `/usr/bin/gcc`
and the full userland) is built from `contrib/` — see *Building the toolchain*.

## Quick start

Build the kernel and decompress the tracked root filesystem image:

```sh
make -C sys                       # builds sys/kernel.bin and sys/kernel.multiboot
[ -f rootfs.img ] || zstd -d --keep rootfs.img.zst   # if only the compressed image is present
```

Boot is via QEMU's `-kernel` (multiboot); the kernel is **not** stored in the
image, so rebuild `sys/` and relaunch to iterate on it. Common flags:

```sh
qemu-system-i386 \
  -cpu qemu32,+sse,+sse2 \
  -machine pc,i8042=off \
  -m 512 \
  -kernel sys/kernel.multiboot \
  -serial mon:stdio \
  <STORAGE OPTION FROM BELOW>
```

`-machine pc,i8042=off` disables emulated PS/2; add
`-device piix3-usb-uhci -device usb-kbd -device usb-mouse` for input. KVM
(`-enable-kvm`) is much faster; pure TCG also works. `run-networking.sh` is a
convenience launcher that wires up disk, USB input, networking, audio, and the
framebuffer.

### Storage options

Substrate supports SATA (AHCI), IDE, and USB Mass Storage. **SATA is
preferred.** Each presents the disk under `/dev/storage/` with a different
name, so the `root=` value must match the bus you attach.

SATA / AHCI — `/dev/storage/sata0` (preferred):

```sh
  -append "root=/dev/storage/sata0 serial_debug" \
  -drive file=rootfs.img,format=raw,if=none,id=disk0 \
  -device ich9-ahci,id=ahci0 \
  -device ide-hd,bus=ahci0.0,drive=disk0
```

IDE / ATA — `/dev/storage/ide0`:

```sh
  -append "root=/dev/storage/ide0 serial_debug" \
  -drive file=rootfs.img,format=raw,if=ide
```

USB Mass Storage — `/dev/storage/scsi0` (USB-MSC attaches through the SCSI
mid-layer):

```sh
  -append "root=/dev/storage/scsi0 serial_debug" \
  -drive file=rootfs.img,format=raw,if=none,id=disk0 \
  -device piix3-usb-uhci,id=uhci0 \
  -device usb-storage,bus=uhci0.0,drive=disk0
```

Boot parameters (`root=`, `console=`, `video=`, `debug=`, …) are documented in
[`usr.man/man7/kernel_command_line.7`](usr.man/man7/kernel_command_line.7).

## Building

From a clean checkout the repo-root orchestrator builds everything in order —
the Substrate-native GNU toolchain, the kernel and userland, every `contrib/`
port in dependency order, and finally the bootable image:

```sh
./build.sh
```

No root is required: image building is fully unprivileged (mke2fs + debugfs,
no loopback mount). The only step that writes outside the tree is the cross
toolchain install under `/opt/substrate` — if that path is not writable by
you, create it owned by your user once
(`sudo install -d -o "$(id -un)" /opt/substrate`) or override with
`STAGE1_PREFIX=` to a user-owned path. Useful env knobs:
`SKIP_TOOLCHAIN=1`, `SKIP_CONTRIB=1`, `SKIP_IMAGE=1`, and
`ONLY="pkg1 pkg2 ..."` to (re)build only specific contrib ports.

To iterate without a full rebuild:

```sh
make -C sys                                   # just the kernel (booted via -kernel)
./build-rootfs.sh --dist --toolchain --image  # userland + overlays + rootfs.img
```

The kernel is not stored in the image, so kernel changes need only
`make -C sys` and a relaunch. The two stages the orchestrator drives —
the toolchain and the root filesystem image — are detailed next.

### Clean build from the ground up

`./build.sh` runs the whole pipeline in dependency order. Its stages:

| Stage | What | Skip with |
|-------|------|-----------|
| 0  | Substrate-native GNU toolchain (`contrib/build-toolchain.sh`) — stage-1 cross + stage-2 Canadian cross, into `/opt/substrate` | `SKIP_TOOLCHAIN=1` |
| 1a–1f | kernel (`sys/`), runtime libs (`lib/`, `usr.lib/`), `sbin/ld.so`, base userland (`bin/`, `sbin/`), helper tools (`usr.bin/`), then mirror native libs+headers into the cross sysroot | — |
| 2  | every `contrib/<pkg>` in dependency order — each `fetch.sh` (download + verify + extract + apply its `patches/` series) then `build.sh` (configure + cross-compile + stage into `dist-<pkg>/`); each result is mirrored into the cross sysroot so the next port's `configure` finds it | `SKIP_CONTRIB=1` |
| 3  | `build-rootfs.sh` — assemble `dist/`, overlay the stage-2 toolchain + ports, bake `rootfs.img` | `SKIP_IMAGE=1` |

For a *truly* clean build, remove the build artifacts first, then run the
orchestrator:

```sh
make -C sys clean                     # kernel objects
rm -rf dist dist-* contrib/*/build    # userland staging + extracted contrib trees
./build.sh                            # rebuild everything from source
```

Removing `contrib/*/build` forces every port to re-fetch and re-extract from
its upstream tarball (network required) and re-apply its substrate patch
series, which is the most thorough check that the ports still build from a
pristine source tree. To keep the validated toolchain and rebuild only the OS
on top of it, pass `SKIP_TOOLCHAIN=1`; to rebuild a single port use
`ONLY="<pkg>"`.

The cross sysroot under `/opt/substrate/i386-unknown-substrate` is populated
automatically during the build by `scripts/sync-sysroot.sh` (sourced by
`build.sh`). That script is also runnable standalone and idempotent, so it can
reconstruct the sysroot from existing `dist-<pkg>/` outputs without a rebuild —
useful after a fresh toolchain install:

```sh
scripts/sync-sysroot.sh               # mirror all dist-* + native libs/headers
```

### Toolchain

To produce a Substrate-native GNU toolchain — binutils + GCC patched for the
`i386-unknown-substrate` target — and an on-target `/usr/bin/gcc`:

```sh
contrib/build-toolchain.sh         # binutils + gcc, stage 1 (cross) and stage 2 (Canadian cross)
```

- **Stage 1** (cross) installs to `/opt/substrate/` (override with
  `STAGE1_PREFIX=`) as `i386-unknown-substrate-{gcc,as,ld,…}` for
  cross-compiling on the host.
- **Stage 2** (Canadian cross) produces a toolchain that runs *on* Substrate,
  staged into `dist-toolchain/` (binutils) and `/tmp/gcc-stage2-staging/` (gcc),
  ready to fold into the image.

Each `contrib/<pkg>/build.sh` can also be run individually. Patch series live
in `contrib/<pkg>/patches/`; nothing under `contrib/*/build/` is vendored —
`fetch.sh` downloads and patches upstream releases.

### Root filesystem image

`build-rootfs.sh` stages the target tree into `dist/` and bakes `rootfs.img`:

```sh
./build-rootfs.sh --dist           # build + stage substrate userland into dist/
./build-rootfs.sh --toolchain      # overlay the stage-2 GCC + binutils + ports onto dist/
./build-rootfs.sh --image          # bake rootfs.img (4 GiB ext2) from dist/
```

`--dist` wipes and repopulates `dist/`, so re-run `--toolchain` after it to
restore the compiler. `--toolchain` skips any staging tree that is absent
(e.g. a no-compiler bring-up image). For a clean-checkout build of all three
stages at once, use `./build.sh` (see [Building](#building) above).

## Testing

Kernel tests are not compiled in by default:

```sh
make -C sys KERNEL_TESTS=1         # build a test kernel
# then boot with  test=all  (or  test=<name>)  on the kernel command line
```

Host-runnable tests need no VM (built with the host toolchain via
`NATIVE_BUILD=1`):

```sh
make -C tests/sys                  # kernel-logic unit tests (host_test_*)
make -C tests/bin/grep             # per-utility suites under tests/bin/<prog>/
```

`NATIVE_BUILD=1` compiles a component against the **host** libc for off-target
validation; it never alters target ABI behavior. Target libraries
(`lib/c`, `lib/sys`, `crt0.S`, …) are Substrate-only.

## Contributing

- Keep commits small and scoped; update tests with behavior changes.
- Update `ARCHITECTURE.md` when structure changes and add a manual page for
  new kernel subsystems, syscalls, and libc/libm/libpthread entry points
  (see the directives in `AGENTS.md`).
- Don't let the kernel / userland / toolchain ABI contracts drift silently.

Read `AGENTS.md` before making non-trivial changes.
