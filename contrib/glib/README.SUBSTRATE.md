# GLib 2.56.4 — substrate port

GLib 2.56.4 is the **last autotools release** of GLib (2.58+ is meson-only and
substrate has no meson/ninja cross-build), so 2.56.x is the version to target.

## Status: builds (static libraries)

`./fetch.sh && ./build.sh` produces, in `dist-glib/usr/`:

- `lib/lib{glib,gobject,gio,gmodule,gthread}-2.0.a`
- `lib/pkgconfig/{glib,gobject,gio,gio-unix,gmodule,gthread,...}-2.0.pc`
- `include/glib-2.0/…` (full headers) + `lib/glib-2.0/include/glibconfig.h`
- `bin/{gio,gdbus,gdbus-codegen,glib-compile-resources,glib-compile-schemas,
  glib-genmarshal,glib-mkenums,glib-gettextize,gapplication,gio-querymodules}`

Verified: a GObject program (which pulls in libffi closures) links and the
staged tools are valid substrate i386 binaries.

**Shared libraries are not built yet** — a shared `libgobject-2.0.so` would
need `libffi.so` as a DT_NEEDED, and libffi is currently staged static-only
(see `../libffi/README.SUBSTRATE.md`).  Build libffi shared first to enable
`--enable-shared` glib.

## Build

```sh
( cd ../libffi && ./fetch.sh && ./build.sh )     # -> dist-libffi (mandatory, GObject FFI)
( cd ../zlib   && ./fetch.sh && ./build.sh )     # -> dist-zlib
( cd ../libiconv && ./fetch.sh && ./build.sh )   # -> dist-libiconv
./fetch.sh        # download + sha256 + extract + config.sub fixup + patches
./build.sh        # cross-configure + make + stage into dist-glib/usr/
```

## Substrate prerequisites (landed outside this directory)

Getting GLib to build pulled several gaps in substrate's own libraries and
headers, all committed separately:

- **libpthread**: added `pthread_condattr_init`/`setclock`/`getclock` (+ a
  per-cond clock so GCond's CLOCK_MONOTONIC timedwait is honored),
  `pthread_attr_*`, the TSD key API (`pthread_key_create`/`get`/`setspecific`/
  `delete` with on-exit destructors, stored in an initial-exec `__thread`
  vector), and a writer-preferring `pthread_rwlock_*`.
- **headers**: `IN6_IS_ADDR_MC_*` macros, `struct ip_mreq_source`,
  `IPV6_JOIN_GROUP`/`IPV6_LEAVE_GROUP`, `SOMAXCONN` (`<netinet/in.h>`,
  `<sys/socket.h>`), `GETSHORT`/`GETLONG`/`PUTSHORT`/`PUTLONG`
  (`<arpa/nameser.h>`); dropped the conflicting int32 `pthread_rwlock_t`
  placeholder from `<sys/types.h>`.
- **libc**: `inet_aton()`.

## Port-specific handling (in fetch.sh / build.sh / patches)

- `fetch.sh` teaches the bundled `config.sub` about the `substrate` triplet.
- `build.sh` builds `-std=gnu11` (2018 code uses `bool` as an identifier, a
  C23 keyword under GCC 16) and relaxes the GCC-16 `-Werror=format-*` /
  `-Werror=incompatible-pointer-types` promotions back to warnings.
- `build.sh` pins `PKG_CONFIG_LIBDIR` to the substrate dependency trees so the
  cross build can't pick up host libraries (it was silently enabling
  HAVE_LIBELF from the host's libelf).
- `substrate-cross.cache` seeds the `AC_TRY_RUN` answers configure refuses to
  guess when cross-compiling.  Caveat: configure rewrites env vars into its
  cache file, so build.sh feeds it a throwaway copy stripped of `ac_cv_env_*`.
- `patches/0001-gunixmounts-substrate-stub.patch` gives gunixmounts.c an empty
  mount-table implementation for substrate (no getmntent) instead of `#error`.
- `build.sh` neutralizes the automake `py-compile` helper before install:
  glib 2.56's copy `import`s the `imp` module, removed in Python 3.12+, and it
  only byte-compiles the installed gdbus-codegen scripts.

## Remaining

- Shared libraries (needs libffi.so).
- gdbus-codegen runs on the target via the installed Python scripts; substrate
  has no Python yet, so D-Bus code generation is not usable in-place.
