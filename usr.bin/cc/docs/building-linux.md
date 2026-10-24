# Building Linux with substrate's cc, as and ld

The in-tree compiler, assembler and linker can build an unmodified
upstream Linux kernel for i386 or x86-64 that boots to a userspace
`/init`.  The
kernel is a hard test of the toolchain: GNU C extensions, inline assembly
of every kind, linker scripts, and 16-bit setup code that has to fit in
32 KiB.  These are the steps that do it.

Tested: Linux 7.3, `ARCH=i386` and `ARCH=x86_64`, `tinyconfig` plus the
options below, on a Linux host, booted in `qemu-system-i386` and
`qemu-system-x86_64`.  Other versions and configurations
have not been tried.  The kernel source is never patched; a kernel that
fails to build or boot is a bug in cc, as or ld, and is fixed there.

## 1. Build and install the tools

From the top of the tree:

```sh
./build-native-toolchain.sh --install
```

That puts `cc`, `as` and `ld` in `/opt/substrate/native/bin` (`PREFIX`
moves it; see the top-level `README.md`).  Below, `T` names that
directory:

```sh
T=/opt/substrate/native/bin
```

Give the kernel `cc` and `ld` by full path.  `cc` then runs the `as` beside
it; the kernel assembles its `.S` files through `CC`, so it needs no `AS`.
The rest of the kernel's build -- `HOSTCC` for its build-time helpers
(`fixdep`, kconfig), and `ar`, `nm` and `objcopy` -- stays the host's, so
the host needs gcc and binutils, and flex, bison, bc and perl as any
kernel build does.

## 2. Get the source

```sh
curl -LO https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.3.tar.xz
echo "87363c24f10e06321eb362765521094e94471f6bff8bce2d6b3e40f39f004953  linux-7.3.tar.xz" | sha256sum -c
tar -xJf linux-7.3.tar.xz
cd linux-7.3
```

## 3. Configure

Every `make` takes the same variables, so kconfig's compiler probes see
the compiler that builds:

```sh
K="ARCH=i386 CC=$T/cc LD=$T/ld HOSTCC=gcc"
make $K tinyconfig
```

`tinyconfig` cannot print or start a program.  Merge in what booting to
`/init` on a serial console needs -- printk, the 8250 serial console, an
uncompressed initramfs, ELF and `#!` binaries:

```sh
cat > boot.config <<'EOF'
CONFIG_PRINTK=y
CONFIG_EARLY_PRINTK=y
CONFIG_TTY=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_GZIP=n
CONFIG_RD_BZIP2=n
CONFIG_RD_LZMA=n
CONFIG_RD_XZ=n
CONFIG_RD_LZO=n
CONFIG_RD_LZ4=n
CONFIG_RD_ZSTD=n
CONFIG_BINFMT_ELF=y
CONFIG_BINFMT_SCRIPT=y
EOF
scripts/kconfig/merge_config.sh -m .config boot.config
make $K olddefconfig
```

(`$K` is unquoted on purpose, to split into four arguments.  zsh does not
split it; there, use `${=K}`.)

## 4. Build

```sh
make $K -j"$(nproc)" bzImage
```

The result is `arch/x86/boot/bzImage`.

## 5. An /init to boot to

A static i386 program with no C library, built by the same cc and ld,
that says it ran and asks to power the machine off.  Work beside the kernel
tree, not in it (it has an `init/` directory of its own):

```sh
mkdir ../boot && cd ../boot
```

```c
/* init.c */
static long sys(long nr, long a, long b, long c, long d) {
    long r;

    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return r;
}

void _start(void) {
    static const char msg[] = "\nINIT: userspace reached\n";

    sys(4, 1, (long)msg, sizeof(msg) - 1, 0);            /* write(1, msg, len) */
    sys(88, 0xfee1dead, 672274793, 0x4321fedc, 0);      /* reboot(POWER_OFF) */
    for (;;) {
        sys(1, 0, 0, 0, 0);                              /* exit(0) */
    }
}
```

```sh
$T/cc -m32 -O0 -ffreestanding -fno-pic -nostdlib -static -Wl,-e,_start -o init init.c
```

Pack it into an initramfs with the kernel's own `gen_init_cpio`:

```sh
gcc -O2 -o gen_init_cpio ../linux-7.3/usr/gen_init_cpio.c
cat > initramfs.list <<EOF
dir /dev 755 0 0
nod /dev/console 600 0 0 c 5 1
file /init $PWD/init 755 0 0
EOF
./gen_init_cpio initramfs.list > initramfs.cpio
```

## 6. Boot

```sh
qemu-system-i386 -m 128 -nographic -no-reboot \
    -kernel ../linux-7.3/arch/x86/boot/bzImage -initrd initramfs.cpio \
    -append "console=ttyS0 earlyprintk=serial,ttyS0,115200 panic=-1"
```

The kernel's log ends with `Run /init as init process`, then
`INIT: userspace reached`.  This configuration has no ACPI, so the power
off request ends in `System halted instead`; leave qemu with Ctrl-A X.

## Limits to know

- **Setup code size.**  `arch/x86/boot/setup.ld` requires the 16-bit setup
  code to end by 0x8000, its bss included.  With the register allocator
  (2026-10-24) `setup.bin` is about 22 KiB, against 0x7fe0 bytes before
  it; a change that makes cc's i386 code much larger fails the link with
  `Setup too big!`.

## x86-64

The same steps with `ARCH=x86_64` in `K`, the same `boot.config`, and an
`/init` that enters the kernel with `syscall`:

```c
/* init64.c */
static long sys(long nr, long a, long b, long c) {
    long r;

    __asm__ volatile("syscall" : "=a"(r) : "a"(nr), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}

void _start(void) {
    static const char msg[] = "\nINIT: userspace reached\n";

    sys(1, 1, (long)msg, sizeof(msg) - 1);              /* write(1, msg, len) */
    sys(169, 0xfee1dead, 672274793, 0x4321fedc);         /* reboot(POWER_OFF) */
    for (;;) {
        sys(60, 0, 0, 0);                                /* exit(0) */
    }
}
```

```sh
$T/cc -m64 -O2 -ffreestanding -fno-pie -nostdlib -static -Wl,-e,_start -o init init64.c
qemu-system-x86_64 -m 256 -nographic -no-reboot \
    -kernel ../linux-7.3/arch/x86/boot/bzImage -initrd initramfs.cpio \
    -append "console=ttyS0 earlyprintk=serial,ttyS0,115200 panic=-1"
```

A kernel stack is 16 KiB, with no guard page in this configuration: a
function cc does not take through its register allocator (see
`CC_MIR_STATS` in cc(1)) keeps each value in a stack slot of its own, and
one with a frame of many KiB overruns the stack silently.  To see the
largest frames, look for each function's `sub $N, %rsp` in
`objdump -d vmlinux`.

## When it does not boot

- **Before the serial console is up**, the log is still in memory.  Start
  qemu with `-s -S`, attach gdb to `vmlinux` (`target remote :1234`), let
  it run until it stops, then `dump binary memory log.bin &__log_buf
  ((char *)&__log_buf)+(1<<17)` and read `log.bin` with `strings`.
- **A silent reset** (a triple fault): `-d int,cpu_reset -D qemu.log`
  records the exceptions before it.
- **A fault that comes and goes**: build the kernel with gcc as well
  (a copy of the tree, the same `.config`), and link mixes of the two
  sets of objects -- copy one set's `.o` files over the other's, `touch`
  them and rerun `make`, which relinks without recompiling -- booting each
  a few times, to halve the cc-built objects down to the one at fault.
  An answer that moves as the halves change points at the stack or
  memory being overrun, not at one function.
- **A wrong section, size or offset**: compile the same preprocessed file
  with gcc, assemble or link the same input with GNU as or ld, and compare
  the objects (`readelf -a`, `objdump -dr`).  A difference narrows to one
  construct; it gets a test under `tests/usr.bin/cc/` (or `as/`, `ld/`)
  before it is fixed.
