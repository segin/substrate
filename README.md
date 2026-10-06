# Substrate

Substrate is a Unix-like operating system for x86. i386 is the primary
target and the most complete; a 64-bit (x86-64) kernel and userland build
and boot too (see below). The tree holds the whole system, not just a
kernel:

- **Kernel** — monolithic, with its own VM, VFS, network stack, and drivers
  for AHCI/IDE/NVMe/USB storage, UHCI/EHCI/xHCI, HD Audio/AC'97/SB16/USB
  audio, and Intel/Realtek/virtio Ethernet.
- **C library, dynamic linker and base userland** — `libc`, `libsys`,
  `libm`, `libpthread`, `/sbin/ld.so`, and the commands under `bin/`,
  `sbin/` and `usr.bin/`.
- **A native GNU toolchain** — binutils 2.46 and GCC 16.1, patched for the
  `i386-unknown-substrate` target. A cross compiler builds the system on a
  Linux host; the same toolchain, plus gdb, then runs *on* Substrate. A
  second cross toolchain targets `x86_64-unknown-substrate` for the 64-bit
  port.
- **227 third-party ports** under `contrib/`, each a patch series against an
  upstream release: zsh, ncurses, OpenSSL, curl, Python, Perl, Tcl/Tk, the
  X11 client stack with the `Xfbdev` framebuffer server, SDL3, GTK 2, TrueType
  fonts, and two desktop environments — CDE and TDE (Trinity).

Substrate also runs binaries built for other systems. The kernel picks an
**exec personality** for each program from its ELF OSABI byte (or its a.out
or Xenix `x.out` header):

| Personality | State |
|---|---|
| Native | Complete. |
| ELKS (16-bit Linux-like a.out) | Done: 16-bit protected mode through a per-process LDT; upstream ELKS `ps`/`meminfo` run. |
| SCO Xenix/286 (`x.out`) | Done: the shipped `cc` compiles and links, a 37-command sample of the SCO media runs clean, Microsoft Word 3.0 reaches its editing screen. |
| Xenix/386 | Active: the Bourne shell and basic utilities of SCO Xenix 386 2.2.3 and 2.3.4 run. |
| FreeBSD, NetBSD | Their dynamic linkers and libc come up; dynamically linked binaries run. |
| Linux | Active development. |
| System V Release 4 (i386 ELF) | Active: the vendor's shells and basic utilities run, static and dynamically linked against its own `libc.so.1`. |
| System V Release 3 (i386 COFF) | Active: AT&T Release 3.2.3 and INTERACTIVE UNIX 3.0 shells and utilities run, with `/shlib/libc_s`; each system's `cc` compiles and links a program that runs. |
| OpenBSD, SunOS 4.x | Early stage. |

## The 64-bit port

The x86-64 port (`sys/arch/x86_64`) is real, not a stub. Its kernel runs
the existing i386 userland through a 32-bit compatibility layer, and it
also runs **native 64-bit (LP64) processes**: a separate
`x86_64-unknown-substrate` C/C++ cross toolchain, a 64-bit dynamic linker
`/sbin/ld64.so` loading from `/lib64`, and the in-tree userland and nearly
every `contrib/` port rebuilt 64-bit. `./build64.sh` produces
`rootfs64.img`, an image whose kernel (`/vmunix64`) and userland are both
64-bit, which boots to a graphical login. i386 remains the primary and
most complete target; `docs/specs/arch_x86_64_core.md` details what the
64-bit port does and does not yet have.

`ARCHITECTURE.md` describes how the system fits together.

## Building

The build runs on a Linux host and produces a cross toolchain, the kernel,
the userland, every port, and a bootable disk image. A full build from a
clean checkout takes several hours and needs network access: each port
downloads its upstream source.

### Host requirements

On Debian or Ubuntu, this is the package list CI builds with
(`.github/workflows/bootstrap.yml`):

```sh
sudo apt-get install -y \
  build-essential gcc-multilib python3 \
  texinfo bison flex libfl-dev gawk curl wget xz-utils cmake \
  gettext pkg-config \
  e2fsprogs dosfstools mtools fakeroot \
  grub-common grub-pc-bin util-linux zstd \
  libmotif-dev libx11-dev libxt-dev libxext-dev libxmu-dev \
  libxpm-dev libxinerama-dev libxft-dev libtirpc-dev tcl-dev \
  libfontenc-dev libfreetype-dev zlib1g-dev \
  libjpeg-dev liblmdb-dev libpam0g-dev libutempter-dev \
  libice-dev libsm-dev libxrender-dev libxau-dev libxdmcp-dev \
  libcrypt-dev x11-xserver-utils xbitmaps gperf chrpath \
  libmagic-dev libglib2.0-dev libxml2-dev libxslt1-dev \
  libdbus-1-dev intltool meson ninja-build \
  autoconf-archive
```

Several ports run host-side generators (a native `tclsh`, CDE's and TDE's
code generators), which is why X11 and Tcl development packages are on the
list. Other distributions need the equivalents.

To run what you build you also need `qemu-system-i386`, and for UEFI boots
`qemu-system-x86_64` plus OVMF firmware.

### Full build

```sh
./build.sh
```

`build.sh` runs four stages in order:

| Stage | What it does |
|---|---|
| 1 | Builds the cross toolchain (`contrib/build-toolchain.sh`) into `/opt/substrate`. |
| 2 | Builds the kernel (`sys/`), libraries (`lib/`, `usr.lib/`), `sbin/ld.so`, userland and manual pages, then copies the libraries and headers into the cross toolchain's sysroot. Then builds the stage-2 toolchain, which runs on Substrate. |
| 3 | Builds every `contrib/<pkg>` in dependency order. Each port's `fetch.sh` downloads, verifies and patches the upstream source; its `build.sh` cross-compiles it into `dist-overlay/dist-<pkg>/`, which is copied into the sysroot for the ports after it. |
| 4 | `build-rootfs.sh` assembles `dist/` and bakes `rootfs.img`. |

Nothing needs root except creating `/opt/substrate`. If you can't write
there, create it once as your user:

```sh
sudo install -d -o "$(id -un)" /opt/substrate
```

or put the toolchain elsewhere with `STAGE1_PREFIX=/path`.

Options, all environment variables:

| Variable | Effect |
|---|---|
| `STAGE1_PREFIX=dir` | Install the cross toolchain in `dir` (default `/opt/substrate`). |
| `JOBS=n` | Parallel jobs (default: `nproc`). |
| `SKIP_TOOLCHAIN=1` | Reuse the toolchain already in `$STAGE1_PREFIX`. |
| `SKIP_CONTRIB=1` | Skip every port: kernel, userland and image only. |
| `ONLY="pkg1 pkg2"` | Build only these ports. The kernel, userland and image stages still run. |
| `SKIP_IMAGE=1` | Stop after staging `dist/`. |
| `SKIP_GRUB=1` | Use the host's GRUB instead of building `contrib/grub`. |

The results:

| Output | What it is |
|---|---|
| `rootfs.img` | 4 GiB bootable disk: an MBR with GRUB for BIOS boot, a FAT32 EFI partition labelled `sub-boot` (GRUB for 64- and 32-bit UEFI), and the ext2 root, labelled `sub-root`, holding the kernel as `/vmunix`. |
| `sys/kernel.bin` | Kernel for QEMU's `-kernel` (text console). |
| `sys/kernel.fb.bin` | Framebuffer kernel; the image's `/vmunix`. |
| `/opt/substrate/` | Cross toolchain: `i386-unknown-substrate-gcc` and friends, with the sysroot under `i386-unknown-substrate/`. |
| `dist/` | The staged root filesystem the image is baked from. |

### The 64-bit image

```sh
./build64.sh
```

The counterpart of `build.sh` for x86-64: it builds the
`x86_64-unknown-substrate` cross toolchain, the kernel and userland 64-bit,
the `contrib/` ports for the 64-bit target (all but TDE), and bakes and
boot-tests `rootfs64.img` — a 4 GiB image labelled `sub-root64`/`sub-boot64`
whose only kernel is `/vmunix64`. `SKIP_TOOLCHAIN=1` reuses an installed
64-bit toolchain; `ONLY64="pkg …"` selects which ports to build.

### Rebuilding parts

The kernel alone (the project always cleans before a kernel build):

```sh
make -C sys clean && make -C sys
```

QEMU's `-kernel` boot (below) loads `sys/kernel.bin` directly, so a new
kernel needs no image rebuild. For BIOS/UEFI boots, copy
`sys/kernel.fb.bin` into the image as `/vmunix`, or rebuild the image.

One or more ports, reusing the toolchain and re-baking the image:

```sh
SKIP_TOOLCHAIN=1 ONLY="tk font-dejavu" ./build.sh
```

Or just stage a port, without the other stages:
`cd contrib/<pkg> && ./fetch.sh && ./build.sh`. Its files land in
`dist-overlay/dist-<pkg>/`.

The userland and image without rebuilding ports:

```sh
./build-rootfs.sh --dist --toolchain --image
```

`--dist` stages the native userland into `dist/` (wiping it first), so pass
`--toolchain` with it to add back the stage-2 toolchain and the ports.
`--image` bakes `rootfs.img` from `dist/`.

If the sysroot is lost or stale, for example after reinstalling the
toolchain, `scripts/sync-sysroot.sh` rebuilds it from the existing
`dist-overlay/dist-*` trees without recompiling anything.

### Clean rebuild

```sh
make -C sys clean
rm -rf dist dist-overlay contrib/*/build
./build.sh
```

Removing `contrib/*/build` makes every port download and patch its source
again, which checks that the patch series still apply to pristine upstream
trees. Add `SKIP_TOOLCHAIN=1` to keep the existing toolchain.

## Running

### Under QEMU with `run-networking.sh`

```sh
./run-networking.sh --user --kvm --snapshot
```

This boots `sys/kernel.bin` with `rootfs.img` on AHCI, USB keyboard and
mouse, AC'97 audio, and user-mode networking (guest 10.0.2.15, gateway
10.0.2.2). `--snapshot` throws away writes to the image; leave it off to
keep changes. The most useful options:

| Option | Effect |
|---|---|
| `--boot=bios`, `--boot=uefi`, `--boot=uefi32` | Boot the image through GRUB instead of `-kernel`. |
| `--gfx` | Framebuffer console at 1024x768. |
| `--virtio`, `--ide`, `--ums`, `--uas` | Put the root disk on virtio-blk, IDE, USB storage or USB Attached SCSI instead of AHCI. |
| `--audio=hda`, `sb16`, `usb` | Emulate a different sound device. |
| `--keyboard=ps2`, `--mouse=ps2` | PS/2 input instead of USB. |
| `--usb-version=2.0`, `3.0` | EHCI or xHCI instead of UHCI. |
| `--64` | Boot the 64-bit image (`rootfs64.img`, `sys/kernel-x86_64.bin`) under `qemu-system-x86_64`. |
| `--debug` | Verbose kernel log on serial, and QEMU's gdb stub on port 1234. |

Without `--user` it bridges onto a host NIC with macvtap, which needs sudo
and a wired interface. `./run-networking.sh --help` documents everything.

### Under QEMU directly

```sh
qemu-system-i386 -cpu qemu32,+sse,+sse2 -m 512 \
  -kernel sys/kernel.bin -append "root=LABEL=sub-root" \
  -drive file=rootfs.img,format=raw,if=none,id=disk0 \
  -device ich9-ahci,id=ahci0 -device ide-hd,bus=ahci0.0,drive=disk0 \
  -serial mon:stdio
```

`root=LABEL=sub-root` finds the root partition whatever bus the disk is on.
Kernel options (`root=`, `console=`, `vga=`, `serial_debug`, `nosmp`, …)
are described in `usr.man/man7/kernel_command_line.7`. Add `-accel kvm` for
speed; plain emulation also works.

### On real hardware

`rootfs.img` boots on PCs as is, through BIOS or UEFI (64- or 32-bit).
Write it to a USB stick or SD card:

```sh
sudo dd if=rootfs.img of=/dev/sdX bs=4M conv=fsync   # destroys /dev/sdX
```

The GRUB menu offers a normal boot, a serial-console boot with verbose
logging, and diagnostic entries for USB bring-up and SMP.

## Testing

Host-side tests build with the host compiler (`NATIVE_BUILD=1`) and need no
VM:

```sh
make -C tests                    # the host suite CI runs
make -C tests/sys host_test_audio && tests/sys/host_test_audio
```

Kernel self-tests are compiled in on request:

```sh
make -C sys clean && make -C sys KERNEL_TESTS=1
# boot with test=all (or test=<name>) on the kernel command line
```

Target-side test programs live under `tests/` next to the code they cover
(`tests/sys/`, `tests/lib/`, `tests/bin/<program>/`, `tests/usr.bin/<tool>/`).
Each prints a `Result:` line; build it with the cross compiler and run it in
a guest.

`NATIVE_BUILD=1` compiles a component against the host's libc for testing
only. The target libraries (`lib/c`, `lib/sys`, `crt0.S`) are never changed
to suit a host.

## Repository layout

```text
sys/            kernel: arch/, kern/, vm/, vfs/ + fs/, drivers/, net/, exec/
lib/            runtime libraries: libc, libsys, libm, libpthread, libdl, librt, ...
usr.lib/        support libraries: regex, elfobj, exvi, demangle, ...
sbin/ld.so/     the dynamic linker
bin/ sbin/      base commands and system tools
usr.bin/        additional tools: readelf, nm, ldd, seq, env, mktemp, slex, ...
usr.sbin/       administration: useradd, groupadd, ...
include/        userspace headers
contrib/        third-party ports: fetch.sh + build.sh + patches/ per package
usr.man/        manual pages, installed to /usr/share/man
etc/            target /etc, including the rc.d boot scripts
tests/          host and target test suites
docs/           changelog, port catalogue, subsystem specifications
tools/ scripts/ build helpers
build.sh        full build
build-rootfs.sh image assembly
run-networking.sh  QEMU launcher
```

## Documentation

| File | Contents |
|---|---|
| `ARCHITECTURE.md` | How the system is structured. |
| `AGENTS.md` | Engineering rules for working in the tree. |
| `docs/CHANGELOG.md` | Detailed history. |
| `docs/contrib-ports.md` | Every port, with its version and Substrate notes. |
| `docs/toolchain.md` | The GNU toolchain port. |
| `docs/specs/` | Subsystem specifications. |
| `usr.man/` | Manual pages for Substrate's own commands, calls and devices. |

## Contributing

- Keep each commit to one change, with the tests that cover it.
- Add a manual page for any new kernel subsystem, native system call, or
  libc/libm/libpthread function, and update `ARCHITECTURE.md` when the
  structure changes.
- Read `AGENTS.md` before making non-trivial changes.

## License

Copyright © 2026 Kirn Gill II. All rights reserved; see `LICENSE`.
Third-party ports keep their own licences.
