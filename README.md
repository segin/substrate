# Substrate

Substrate is a complete Unix operating system project.

It is not just a kernel repository. The tree contains:
- a native kernel (`sys/`)
- base userland (`bin/`, `sbin/`, `usr.bin/`)
- system libraries (`lib/`, `usr.lib/`)
- a native dynamic linker (`sbin/ld.so`)
- an in-tree homebrew C toolchain (`usr.bin/cc`, `usr.bin/as`, `usr.bin/ld`, `usr.lib/elfobj`)
- a vendored GNU toolchain port — GNU binutils + GCC patched for the
  substrate target, built as a Canadian cross to run on substrate itself
  (`contrib/binutils`, `contrib/gcc`, `contrib/build-toolchain.sh`)
- build/install staging for both target and host (`dist/`, `host_dist/`,
  `dist-toolchain/`)

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
Substrate ships two parallel C toolchains, both native to the substrate
target:

- **Homebrew in-tree toolchain** — built from scratch inside the tree:
  - `usr.bin/cc`: C compiler driver + frontend/middle/backends.
  - `usr.bin/as`: assembler.
  - `usr.bin/ld`: linker.
  - `usr.lib/elfobj`: ELF object model and manipulation library shared
    by tooling.

- **GNU toolchain port** — upstream binutils + GCC patched for the
  substrate target, vendored as patch series against the unmodified
  upstream releases (nothing in `contrib/*/build/` is checked in):
  - `contrib/binutils` — GNU binutils 2.46.0 patch series adding the
    `elf32-i386-substrate` / `elf64-x86-64-substrate` BFD output vecs,
    the `elf_i386_substrate` ld emulation, and the `ELFOSABI_SUBSTRATE`
    OSABI byte.
  - `contrib/gcc` — GCC 16.1.0 patch series adding the
    `i386-unknown-substrate` target and a `libstdc++-v3` OS port.
  - `contrib/build-toolchain.sh` — single orchestrator that fetches +
    patches + builds both at stage 1 (Linux-hosted cross) and stage 2
    (Canadian cross — substrate-hosted, runs inside the rootfs).

Stage-1 install lands at `/opt/substrate/` and provides
`i386-unknown-substrate-{gcc,g++,ld,as,ar,nm,objdump,...}` for
cross-compilation on the build host.  Stage 2 installs into
`dist-toolchain/usr/` and is folded into the rootfs image so the
substrate target ships with a working `/usr/bin/gcc`,
`/usr/bin/g++`, `/usr/bin/ld`, and the rest.

The wire-level identifier for substrate-produced binaries is
`ELFOSABI_SUBSTRATE = 64` stamped into `e_ident[EI_OSABI]` by the
substrate BFD vec.  The kernel personality dispatch keys off that
byte to route each ELF to its loader.

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

## Testing

The kernel test suite is **not** compiled into the kernel by default. To build a test kernel:

```sh
make -C sys KERNEL_TESTS=1
```

Boot with `test=all` (or `test=<name>`) on the command line to run tests.

Host-runnable tests (`host_test_*`) can be built and run on the development machine without booting the kernel:

```sh
make -C tests/sys
```

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
  -drive file=rootfs.img,format=raw,if=ide \
  -append "root=/dev/storage/ide0 init=/sbin/init serial_debug" \
  -cpu pentium3 \
  -serial mon:stdio \
  -m 2048
```

`-cpu pentium3` (or higher) is required when the rootfs ships
GCC: cc1 was compiled with default `-march=pentiumpro` tuning and
emits CMOV instructions that the default QEMU i386 CPU rejects with
SIGILL.  For a kernel-only image without GCC, the default CPU is
fine.

The same root image can also be used with:

- `sys/kernel.zimage` for direct BIOS-style boot
- `sys/kernel.flp` as a floppy bootloader artifact, with `rootfs.img`
  attached as the primary IDE disk

Useful boot parameters are documented in
[`kernel_command_line.7`](usr.man/man7/kernel_command_line.7).

## Constructing A Root Filesystem

The full reproducible bootstrap from a clean checkout is driven by
two scripts.

### Step 1 — build the toolchain

If you want substrate to carry a working `/usr/bin/gcc` (which a real
userland wants), build both stages of the GNU toolchain port:

```sh
contrib/build-toolchain.sh                        # binutils + gcc, stage 1 + stage 2
```

Stage 1 installs into `/opt/substrate/` (override with
`STAGE1_PREFIX=...`); stage 2 stages into
`./dist-toolchain/usr/` and (for gcc) `/tmp/gcc-stage2-staging/usr/`
ready for the next step to fold into the image.  Each
`contrib/<name>/build.sh` can also be invoked individually for
targeted rebuilds.

### Step 2 — stage the rootfs and bake an image

```sh
./build-rootfs.sh --dist           # populate ./dist/ from substrate sources
./build-rootfs.sh --toolchain      # overlay stage-2 GCC + binutils onto dist/usr/
./build-rootfs.sh --image          # bake rootfs.img (default 4 GiB ext2)
```

`--toolchain` is optional — skip it for a minimal rootfs without
GCC/binutils, useful for kernel-bring-up images.  It will refuse
to run if neither stage-2 staging tree exists; run
`contrib/build-toolchain.sh --stage=2` first.

The resulting `rootfs.img` contains:

- substrate kernel, ld.so, libc / libsys / libm / libpthread / ...
  (all OSABI=ELFOSABI_SUBSTRATE)
- the homebrew toolchain (`cc`, `as`, `ld.i386`, ...)
- (if `--toolchain` was run) GNU binutils + GCC at `/usr/bin/`
  and `/usr/libexec/gcc/i386-unknown-substrate/16.1.0/`

### Minimum manual contents

If you'd rather build a rootfs the old-fashioned way without
`build-rootfs.sh`:

- `/sbin/init`, or an alternate program supplied with `init=`
- `/bin/sh`
- `/dev`, `/proc`, `/sys`, `/tmp`
- the libraries and userland required by your chosen init path

`dist/` remains only the staged contents of a target filesystem
tree.  Disk images such as `rootfs.img` are separate artifacts.

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
