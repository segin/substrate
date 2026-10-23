# Linux kernel, built by substrate's own toolchain

Upstream Linux 7.3, unmodified -- no patch series -- built for i386 with
the in-tree C compiler (`usr.bin/cc`), assembler (`usr.bin/as`) and
linker (`usr.bin/ld`), and an initramfs whose `/init` (`init/init.c`) is
built by the same tools.  It is a test of the toolchain: the kernel and
its 16-bit setup code are among the largest and most demanding C and
assembly a compiler for this target sees.

    ./fetch.sh             # download, verify (SHA-256) and extract
    ./build.sh             # tinyconfig + substrate-boot.config, bzImage, /init, initramfs
    ../../tests/linux-kernel/boot.sh   # qemu-system-i386: /init must say PASS

`build.sh` runs on a Linux host (the kernel's own build helpers --
fixdep, kconfig -- are host programs, built with `HOSTCC`, gcc by
default) and needs the native tools installed in
`/opt/substrate/native/bin` (`make -C usr.bin/{cc,as,ld} NATIVE_BUILD=1`
and their install), or `SUBSTRATE_NATIVE` naming another directory.
Outputs land in `build/out/`.

The configuration is `make tinyconfig` with `substrate-boot.config`
merged over it: printk, the 8250 serial console, an uncompressed
initramfs and ELF and script binaries.  `boot.sh` boots the bzImage in
qemu with the serial console on stdio and checks that `/init` printed
`SUBSTRATE-LINUX-INIT: PASS` (it then powers the machine off).

What the setup code costs: `arch/x86/boot/setup.ld` requires the 16-bit
setup to end by 0x8000, bss included.  It ends at 0x7fe0 with this
compiler (2026-10-23) -- 32 bytes to spare.  A change to cc's i386 code
that grows it fails the link with "Setup too big!".
