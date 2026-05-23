# pixman 0.42.2 — substrate port

Pixman is the pixel-region and software-compositing helper library
shared by the entire X server stack and downstream toolkits (cairo,
GTK, ...).  Required dependency of xorg-server (used by the
kdrive / Xfbdev backend for all framebuffer paint ops).

## Build

    cd contrib/pixman
    ./fetch.sh
    ./build.sh

Produces `dist-pixman/usr/{lib,include}/`:

  * `libpixman-1.a`, `libpixman-1.so.0` — static + shared.
  * `include/pixman-1/pixman.h`, `pixman-version.h`.
  * `lib/pkgconfig/pixman-1.pc` for downstream `pkg-config --cflags
    --libs pixman-1`.

## Substrate-specific tweaks

* `config.sub` patched to accept `i386-unknown-substrate` (one-line
  addition to the OS-name allowlist).
* `configure` libtool dispatch sed'd at fetch time to treat
  `substrate*` like `linux*` for shared-library output (avoids
  carrying a brittle line-anchored patch across libtool versions).
* SIMD fast paths disabled (`--disable-mmx --disable-sse2
  --disable-ssse3 --disable-arm-*`).  Substrate's userland is
  baseline i486, no SSE.
* `--disable-openmp --disable-gtk --disable-libpng` — pixman's
  demo/utility programs pull these in; we only ship the lib.
* The `test/` and `demos/` subdirs aren't built — they pull in
  `<fenv.h>` and friends that surface a parse error in the gcc
  `include-fixed/fenv.h` shim for substrate (a separate libc/gcc
  bug worth fixing eventually).  `build.sh` builds + installs only
  the `pixman/` subdir, which is all downstream consumers need.

## Consumers

* `contrib/xorg-server` (kdrive Xfbdev) — primary motivation for
  this port.
* Eventually `cairo`, `gtk`, `qt` if those land.
