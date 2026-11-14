# cpio

`usr.bin/cpio` is Substrate's `cpio(1)`: copy-out, copy-in and
pass-through in the newc, crc, odc, bin, hpodc, hpbin, ustar and tar
formats, with GNU cpio's options and safe extraction by default.

How it behaves is in its manual pages, `usr.man/man1/cpio.1`; the
formats are in `usr.man/man5/cpio.5`.

## Build

Target build:

```sh
make -C usr.bin/cpio
```

Host/native build:

```sh
make -C usr.bin/cpio NATIVE_BUILD=1
```

## Run tests

```sh
make -C tests/usr.bin/cpio run
```

This builds a host binary and runs every `tests/usr.bin/cpio/test_*.sh`.
`make -C usr.bin/cpio test` does the same after cleaning the build.
Where GNU cpio and GNU tar are installed, the tests also check that
they read our archives and we read theirs.
