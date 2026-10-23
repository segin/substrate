# Building Linux with substrate's cc, as and ld

The in-tree compiler, assembler and linker can build an unmodified
upstream Linux kernel for i386 that boots to a userspace `/init`.  The
kernel is a hard test of the toolchain: GNU C extensions, inline assembly
of every kind, linker scripts, and 16-bit setup code that has to fit in
32 KiB.  These are the steps that do it.

Tested: Linux 7.3, `ARCH=i386`, `tinyconfig` plus the options below, on a
Linux host, booted in `qemu-system-i386`.  Other versions and configurations
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
that says it ran and powers the machine off:

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
gcc -O2 -o gen_init_cpio usr/gen_init_cpio.c
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
    -kernel arch/x86/boot/bzImage -initrd initramfs.cpio \
    -append "console=ttyS0 earlyprintk=serial,ttyS0,115200 panic=-1"
```

The kernel's log ends with `Run /init as init process`, then
`INIT: userspace reached`, and qemu exits when `/init` powers off.

## Limits to know

- **Setup code size.**  `arch/x86/boot/setup.ld` requires the 16-bit setup
  code to end by 0x8000, its bss included.  With cc as of 2026-10-23 it
  ends at 0x7fe0: 32 bytes spare.  A change that makes cc's i386 code
  larger fails the link with `Setup too big!`.
- **i386 only.**  `ARCH=x86_64` has not been tried.

## When it does not boot

- **Before the serial console is up**, the log is still in memory.  Start
  qemu with `-s -S`, attach gdb to `vmlinux` (`target remote :1234`), let
  it run until it stops, then `dump binary memory log.bin &__log_buf
  ((char *)&__log_buf)+(1<<17)` and read `log.bin` with `strings`.
- **A silent reset** (a triple fault): `-d int,cpu_reset -D qemu.log`
  records the exceptions before it.
- **A wrong section, size or offset**: compile the same preprocessed file
  with gcc, assemble or link the same input with GNU as or ld, and compare
  the objects (`readelf -a`, `objdump -dr`).  A difference narrows to one
  construct; it gets a test under `tests/usr.bin/cc/` (or `as/`, `ld/`)
  before it is fixed.
