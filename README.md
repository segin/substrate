# Substrate

Substrate is a Unix-like operating system for x86 (32-bit i386 primary,
x86-64 in progress). It is a whole system, not just a kernel: the tree
contains the kernel, a C library and dynamic linker, base userland, a
patched GNU toolchain that runs *on* Substrate, and a large set of ported
third-party software (zsh, ncurses, OpenSSL, an X11 client stack, …).

A distinguishing goal is **binary compatibility via exec personalities**: the
kernel routes each ELF to a loader based on its OSABI byte, so native,
Linux, and FreeBSD binaries can run on the same kernel (native is complete;
Linux/FreeBSD are in active development).

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

## Building the toolchain

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

## Building the root filesystem image

`build-rootfs.sh` stages the target tree into `dist/` and bakes `rootfs.img`:

```sh
./build-rootfs.sh --dist           # build + stage substrate userland into dist/
./build-rootfs.sh --toolchain      # overlay the stage-2 GCC + binutils + ports onto dist/
./build-rootfs.sh --image          # bake rootfs.img (4 GiB ext2) from dist/
```

`--dist` wipes and repopulates `dist/`, so re-run `--toolchain` after it to
restore the compiler. `--toolchain` skips any staging tree that is absent
(e.g. a no-compiler bring-up image).

The all-in-one orchestrator builds the toolchain, every `contrib/` port in
dependency order, and the image from a clean checkout:

```sh
sudo ./build.sh                    # env knobs: SKIP_TOOLCHAIN, SKIP_CONTRIB, SKIP_IMAGE, ONLY="pkg ..."
```

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
