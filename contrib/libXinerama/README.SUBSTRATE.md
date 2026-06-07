# libXinerama 1.1.5 — substrate port

The Xinerama client extension library (`XineramaQueryScreens`,
`XineramaIsActive`, ...).  X clients use it to discover multi-head screen
geometry.  Ported because **CDE** requires it (`configure` hard-errors with
"libXinerama not found"; dtwm/dtsession query screen layout through it).

## Build

```sh
./fetch.sh        # download + verify + extract
./build.sh        # cross-build -> dist-libXinerama/usr/{lib,include}
```

Depends only on the already-staged X client chain (xorgproto — which
ships the `xineramaproto` headers — libX11, libXext).

## Substrate notes

Mirrors `contrib/libXext/build.sh`.  Three substrate adjustments are applied
to the bundled autotools files at build time (idempotent, so no patch
series is needed):

- **config.sub** — add `substrate*` to the recognized-OS list so
  `i386-unknown-substrate` passes host validation.
- **libtool (generated `configure`)** — treat `substrate*` like `linux*`
  in every host_os dispatch, or `--enable-shared` emits only `.a` files.
- **`xorg_cv_malloc0_returns_null=no`** — the `XORG_CHECK_MALLOC_ZERO`
  run-test can't execute under cross-compilation; substrate's `malloc(0)`
  returns a unique non-NULL pointer, so the answer is "no".

Produces `libXinerama.so.1` (+ static) with the OSABI byte stamped to
`ELFOSABI_SUBSTRATE`.
