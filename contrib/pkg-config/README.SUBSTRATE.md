# pkg-config 0.29.2 on Substrate

freedesktop `pkg-config` — the compile-flag query tool that reads the
`*.pc` metadata files contrib ports install under `/usr/lib/pkgconfig`
and emits the `-I` / `-L` / `-l` flags a downstream build needs.

## Build

```
./fetch.sh        # download + verify + extract + patch config.sub
./build.sh        # cross-compile -> dist-pkg-config/usr/bin/pkg-config
```

## Substrate notes

- **`--with-internal-glib`.** pkg-config depends on glib, but glib in
  turn ships `.pc` files that want pkg-config — a bootstrap cycle.  The
  tarball bundles a glib 2.x subset; `--with-internal-glib` compiles it
  statically into the `pkg-config` binary so the tool stands alone.
  The resulting executable needs only `libc.so.0`, `libpthread.so.0`,
  and `libiconv.so.2` at runtime (all present in the image).

- **Cross config cache (`substrate-cross.cache`).** The internal glib's
  `configure` probes ABI facts with `AC_TRY_RUN`, which can't execute an
  i386-substrate binary on the build host.  The cache seeds the answers
  (downward-growing stack, no symbol underscore, POSIX `*_r` reentrant
  shapes, C99 printf).

- **`config.sub`.** pkg-config 0.29.2 ships a 2015-vintage `config.sub`
  whose OS whitelist uses the leading-dash form (`-aros*`); `fetch.sh`
  teaches both it and the internal glib's copy the `substrate` token.

- **`-std=gnu11`.** The bundled glib uses `bool` as a struct field name,
  which GCC 16's default C23 mode treats as a reserved keyword.

- **`--with-pc-path`.** The default `.pc` search path is baked to
  `/usr/lib/pkgconfig:/usr/share/pkgconfig:/usr/local/...` so a bare
  `pkg-config --cflags foo` on-target finds the installed metadata
  without `PKG_CONFIG_PATH` set.

- **`--disable-host-tool`** suppresses the `${host}-pkg-config` symlink;
  substrate only wants `/usr/bin/pkg-config`.

## Verified on substrate

```
pkg-config --version                  -> 0.29.2
pkg-config --modversion glib-2.0      -> 2.56.4
pkg-config --cflags glib-2.0          -> -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include
pkg-config --libs gobject-2.0         -> -lgobject-2.0 -lglib-2.0   (Requires: resolved)
pkg-config --libs zlib                -> -lz
```
