# file

Ian Darwin's and Christos Zoulas's `file(1)` and its library, libmagic
(<https://www.darwinsys.com/file/>).

Upstream: <https://astron.com/pub/file/>
Pinned version: **file-5.45**
License: BSD-2-Clause (see `build/file-5.45/COPYING`).
Substrate vendoring: tarball, no patches.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-overlay/dist-file/usr/` with `bin/file`,
`lib/libmagic.{a,so.1}`, `include/magic.h`, the compiled database
`share/misc/magic.mgc` and the man pages, and copies libmagic and
`magic.h` into the cross sysroot for its consumers (tdelibs, nano).

## Substrate-specific overrides

- **The magic database is compiled by a host build of the same version.**
  `magic.mgc` is written by a runnable `file -C`, and the compiled format
  is tied to the version: 5.45 reads only format 18.  The cross-built
  `file` cannot run on the build host, and the host's own `file(1)` (5.48
  writes format 21) produces a database the target rejects with
  "supports only version 18 magic files".  `build.sh` therefore builds a
  static host `file` from a second copy of the tarball under
  `build/host/` and passes it as `FILE_COMPILE`.
- `LIBS=-lregex`: substrate keeps POSIX `regcomp`/`regexec` in libregex,
  not libc.  libtool drops that dependency from the shared library, so
  `build.sh` relinks `libmagic.so.1` from its archive with libregex in
  `DT_NEEDED`, and brands it `ELFOSABI_SUBSTRATE`.
- Compression support (zlib, bzip2, xz, zstd, lzip) and seccomp are
  disabled.

libmagic maps the database and then `mprotect()`s it read-only; libc's
`mprotect()` must reach the kernel's `SYS_MPROTECT` for `file` to run.
