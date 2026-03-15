# Substrate

Substrate is a complete Unix operating system project.

It is not just a kernel repository. The tree contains:
- a native kernel (`sys/`)
- base userland (`bin/`, `sbin/`, `usr.bin/`)
- system libraries (`lib/`, `usr.lib/`)
- an in-tree C toolchain (`usr.bin/cc`, `usr.bin/as`, `usr.bin/ld`, `usr.lib/elfobj`)
- build/install staging for both target and host (`dist/`, `host_dist/`)

## Design Origin

Substrate started as an original monolithic i386 Unix-like kernel with:
- a conventional process/syscall model
- virtual memory, scheduler, VFS, and core drivers
- a minimal C userspace and bootstrap-oriented build flow

The original toolchain direction was equally explicit: own the C compilation path end-to-end (preprocess, compile, assemble, link), while keeping host bootstrap practical during bring-up.

That original design intent remains:
- Substrate is a system, not a single component.
- Kernel, libc, and toolchain are co-designed.
- Host bootstrap is a means, not the final architecture.

## Current Scope

### Kernel
- Monolithic kernel with primary i386 support and active x86_64 work.
- Core subsystems: VM/PMM/PMAP, scheduler, process manager, signal model, VFS, exec personalities.
- Driver stack includes storage, input, serial, framebuffer/video, and virtualization-facing devices.

### Userland
- Core Unix programs under `bin/` and `sbin/`.
- Expanded tool/user programs under `usr.bin/`.
- System headers in `include/`.

### Libraries
- C library and syscall wrappers under `lib/`.
- Additional runtime/tool libraries under `usr.lib/` (including `libelfobj` and demangling work).

### Toolchain
- `usr.bin/cc`: C compiler driver + frontend/middle/backends.
- `usr.bin/as`: assembler.
- `usr.bin/ld`: linker.
- `usr.lib/elfobj`: ELF object model and manipulation library shared by tooling.

## Planned Unix Binary Support

Substrate binary-compatibility personality targets are:
- Substrate native ABI (primary)
- Linux (planned/active personality support)
- FreeBSD (planned/active personality support)
- NetBSD (planned)
- OpenBSD (planned)
- Solaris / SVR4 family (planned)
- SunOS 4.x (Sun386i) compatibility path (planned)
- Microsoft Xenix personality `MS-X/86` (planned)
- Microsoft Xenix personality `MS-X/286` (planned)
- Microsoft Xenix personality `MS-X/386` (planned)
- SCO Xenix personality `SCO-X/86` (planned)
- SCO Xenix personality `SCO-X/286` (planned)
- SCO Xenix personality `SCO-X/386` (planned)
- SCO personality `SCO-U/3.2v2` (planned)
- SCO personality `SCO-U/ODT3` (planned)
- SCO personality `SCO-OSR5` (planned)
- iBCS2 compatibility layer targets (planned)
- ELKS (16-bit Linux-like) personality with Minix-style `a.out` loader path (planned)

## Repository Layout

```text
sys/         kernel source
bin/         base user commands
sbin/        system administration commands
usr.bin/     toolchain and additional user tools
lib/         target runtime libraries (libc, libsys, etc.)
usr.lib/     reusable tool/user libraries (elfobj, demangle, ...)
include/     userspace headers
tests/       test suites
docs/specs/  subsystem and tool specs
dist/        target rootfs staging
host_dist/   host-install staging for native validation tools
```

## Build Model

Substrate uses two build modes:

- Target build: builds for Substrate runtime environment.
- Host build (`NATIVE_BUILD=1`): builds host-runnable binaries for validation and bring-up.

Key rule: host mode is for validation/bootstrap only; target libraries and ABI behavior must remain Substrate-correct.

## Using The System

Build the tree:

```sh
make
```

Build the common kernel boot artifacts explicitly:

```sh
make -C sys kernel.bin kernel.zimage kernel.flp
```

Boot the tracked reference root filesystem image under QEMU:

```sh
qemu-system-i386 \
  -kernel sys/kernel.bin \
  -append "serial_debug root=/dev/storage/ide0 init=/bin/sh" \
  -serial stdio \
  -hda root.img \
  -m 2048
```

The same root image can also be used with:

- `sys/kernel.zimage` for direct BIOS-style boot
- `sys/kernel.flp` as a floppy bootloader artifact, with `root.img` attached as the primary IDE disk

Useful boot parameters are documented in [`kernel_command_line.7`](usr.man/man7/kernel_command_line.7).

## Constructing A Root Filesystem

Stage a complete target root tree into `dist/`:

```sh
make install DESTDIR=$PWD/dist
```

That staging tree is the source for a real bootable root image. Create a fresh ext2 image from it with host tools:

```sh
truncate -s 256M myroot.img
mkfs.ext2 -F myroot.img

tmpdir=$(mktemp -d)
sudo mount -o loop myroot.img "$tmpdir"
sudo cp -a dist/. "$tmpdir"/
sudo mkdir -p "$tmpdir"/dev "$tmpdir"/proc "$tmpdir"/sys "$tmpdir"/tmp
sudo chmod 1777 "$tmpdir"/tmp
sync
sudo umount "$tmpdir"
rmdir "$tmpdir"
```

Minimum practical contents:

- `/sbin/init`, or an alternate program supplied with `init=`
- `/bin/sh`
- `/dev`, `/proc`, `/sys`, `/tmp`
- the libraries and userland required by your chosen init path

If you want to start from the tracked reference image instead of building from `dist/`:

```sh
cp root.img myroot.img
```

then mount and modify `myroot.img` as needed.

`dist/` remains only the staged contents of a target filesystem tree. Disk images such as `root.img` are separate artifacts.

## Host Toolchain Install (`host_dist` / `host_install`)

Build host-runnable Substrate tools into `host_dist/`:

```sh
make host_dist
```

Install that tree to `/opt/substrate`:

```sh
sudo rm -rf /opt/substrate
sudo make HOST_DIST_PREFIX=/opt/substrate host_install
```

Use installed tools:

```sh
export PATH=/opt/substrate/usr/bin:/opt/substrate/bin:$PATH
which cc as ld
```

## Using Substrate Toolchain On Linux (for Linux binaries)

Use Substrate `cc/as/ld` from `/opt/substrate` first in `PATH`, then compile normally:

```sh
export PATH=/opt/substrate/usr/bin:/opt/substrate/bin:$PATH
cc -m64 hello.c -o hello64
cc -m32 hello.c -o hello32
```

To force exact backend tools for a build:

```sh
AS=/opt/substrate/usr/bin/as LD=/opt/substrate/usr/bin/ld cc -m64 hello.c -o hello64
```

Notes:
- `cc` currently uses host runtime objects/libraries for Linux linkage (`crt*.o`, `libgcc`, libc) via host `gcc` path discovery.
- `-m32` / `-m64` selects Linux i386 / x86_64 ABI output mode.
- `-v` or `-###` prints stage commands so you can verify Substrate tools are being invoked.

## Documentation

- `ARCHITECTURE.md`: system architecture and integration model.
- `AGENTS.md`: project constraints and engineering directives.
- `TASKS.md`: active execution plan/checklist.
- `docs/specs/`: detailed subsystem specifications.
- `usr.man/man7/rootfs.7`: root filesystem construction and boot examples.

## Contributing

Substrate expects production-quality engineering hygiene:
- small scoped commits
- tests with behavior changes
- architecture docs updated when structure changes
- no silent drift between kernel/userland/toolchain contracts

Read `AGENTS.md` before making non-trivial changes.
