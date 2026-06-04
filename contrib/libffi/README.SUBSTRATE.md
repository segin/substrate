# libffi 3.4.6 — substrate port

libffi is the foreign-function-interface backend that **GObject (GLib)**
requires; GLib's `configure` errors out without `ffi.h`.  i386 sysv is a
first-class libffi target, so this cross-builds cleanly with autotools.

## Build

```sh
./fetch.sh          # download + sha256-verify + extract + patch
./build.sh          # cross-compile, stage into dist-libffi/usr/
```

Artifacts land in `dist-libffi/usr/`:
`lib/libffi.a`, `lib/libffi.la`, `include/ffi.h`, `include/ffitarget.h`.

## Patches

- `0001-config-sub-substrate.patch` — teach the bundled `config.sub` to
  accept the `i386-unknown-substrate` triple (the standard one-token
  insertion used across contrib).

## Status / notes

- **Builds:** yes. The x86 sysv FFI (`src/x86/sysv.S` + `src/x86/ffi.c`)
  and the generic core compile and archive for i386-substrate.
- **Static only:** libtool produced `libffi.a` but no `libffi.so` for the
  substrate host (shared-lib autodetection declined this host).  GLib links
  libffi statically, so this is sufficient; producing a shared
  `libffi.so.8` is a follow-up (likely a libtool/`--enable-shared` tweak).
- Consumed by `contrib/glib` via `LIBFFI_CFLAGS`/`LIBFFI_LIBS` pointing at
  `dist-libffi/usr`.
