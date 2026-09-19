# Python on Substrate

CPython 3.14.7, cross-built for substrate (i386, ELFOSABI_SUBSTRATE).

* Upstream: <https://www.python.org/>
* Pinned version: **3.14.7**
* License: PSF

## Build

```sh
./fetch.sh     # download, verify, extract, apply patches/series
./build.sh     # host interpreter, then the cross build, staged into dist-python
```

The pinned SHA-256 is the digest inside upstream's sigstore bundle for the
tarball (python.org publishes no checksum text on its release pages).

## Two stages

A cross build of CPython needs an interpreter of the *same* major.minor to run
its own build steps (freezing modules, compiling the standard library).
`build.sh` builds one from this very tarball into `build/host-prefix` and
passes it as `--with-build-python`, rather than using whatever the build
machine ships -- this host has 3.14, CI's runner has something else, and
configure rejects a mismatch.

## Dependencies

libffi (ctypes), zlib, bzip2, openssl (ssl, hashlib), ncurses (curses),
sqlite3, expat (pyexpat) and uuid, all staged in the cross sysroot first;
`build.sh` asserts on each header.

## What it produces

`bin/python3.14` (plus `python3`, `idle3`, `pydoc3` and the `-config`
scripts), `lib/libpython3.14.so.1.0`, and the standard library under
`lib/python3.14` with 57 extension modules in `lib-dynload`, including
`_ssl`, `_hashlib`, `_sqlite3`, `_curses`, `_curses_panel` and `zlib`.

## Modules not built

`_dbm`, `_gdbm`, `_lzma`, `_zstd`, `_tkinter` and `readline` -- none of gdbm,
xz, zstd or Tk is ported, and CPython looks for libedit through
`editline/readline.h` and `libedit.pc`, neither of which the sysroot has
(it ships a small readline-compatible header over libedit instead).

## Patch

`0001-configure-take-the-linux-path-on-substrate.patch`.  CPython limits cross
builds to a list of known systems and stops with "cross build not supported
for i386-unknown-substrate" while computing MACHDEP.  Substrate is ELF with
GNU ld, a glibc-shaped libc, /proc, /dev/urandom and /dev/ptmx, so the Linux
branch of each of the 110 remaining `ac_sys_system` tests is the right one.
`sys.platform` therefore reports `linux`; `uname(2)` still reports substrate.

`--disable-ipv6` for the same honesty in the other direction: substrate
rejects `AF_INET6` at socket() time on purpose (`sys/net/af_inet.c`), and
configure cannot run its getaddrinfo test under cross-compilation.

## Substrate-side fixes this port needed

Each was fixed in substrate rather than patched around here:

- 64-bit atomics in libc (`__atomic_*_8`, `__atomic_is_lock_free`).  OpenSSL
  built with threads needs them and the i486 baseline has no `cmpxchg8b`.
- `AT_EACCESS` in `<fcntl.h>`, with `faccessat()` now refusing flags it
  cannot honour instead of ignoring them (`posixmodule.c`).
- `_SC_TTY_NAME_MAX` and `TTY_NAME_MAX`, with the matching `sysconf()` case.
- A declaration for `accept4()`, which libc had always implemented but never
  declared, so `socketmodule.c` found the symbol and then failed to compile.
- OpenSSL rebuilt with threads: CPython's `_ssl` and `_hashopenssl` refuse to
  build against an OpenSSL without `OPENSSL_THREADS`.
