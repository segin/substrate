# GNU MPFR on Substrate

MPFR 4.2.2, the correctly-rounded multiple-precision binary floating-point
library.  Built on top of GMP (`contrib/gmp/`), which must be staged first —
`build.sh` refuses to run without `gmp.h` in the cross sysroot.

Ported because **gdb requires it**: `contrib/gdb/build.sh` configures with
`--with-gmp=$SR --with-mpfr=$SR`, and gdb's configure hard-fails with
"Building GDB requires GMP 4.2+, and MPFR 3.1.0+" when either is missing.

## Substrate-specific notes

No patches.  `build.sh` applies the two one-line retrofits every autotools
port of this vintage needs, in the extracted tree rather than as a patch,
because both are mechanical rewrites of generated files:

- `config.sub` does not know the `substrate` OS and rejects the host triplet.
- The bundled libtool's `case $host_os` shared-library branches do not treat
  an unknown GNU-ish OS as shared-capable, so `--enable-shared` silently
  produces only `libmpfr.a`.  Substrate is given the `linux*` treatment,
  which yields `version_type=linux` and a proper soname.

`libmpfr.la` is deleted from the cross sysroot after install, for the same
reason `contrib/gmp/` deletes `libgmp.la`: the archive records
`libdir='/usr/lib'`, so a later cross-link's libtool resolves `-lmpfr` to the
**build host's** `/usr/lib/libmpfr.so` and ld reports

    /usr/lib/libgmp.so: error adding symbols: file in wrong format

The `dist-overlay/dist-mpfr` copy keeps its `.la`, where `/usr/lib` is the
correct target path.

## Verifying

    readelf -h dist-overlay/dist-mpfr/usr/lib/libmpfr.so.6.2.2

should report `ELF32`, `Intel 80386`, and `OS/ABI: <unknown: 40>`
(`ELFOSABI_SUBSTRATE`), and

    readelf -d dist-overlay/dist-mpfr/usr/lib/libmpfr.so.6.2.2

should show `SONAME libmpfr.so.6` with `NEEDED` on `libgmp.so.10`,
`libc.so.0` and `libpthread.so.0`.
