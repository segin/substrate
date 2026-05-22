# libX11 (Xlib) on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/libx11
Pinned:    1.8.12
Tarball:   `https://www.x.org/releases/individual/lib/libX11-1.8.12.tar.xz`
SHA-256:   `fa026f9bb0124f4d6c808f9aef4057aad65e7b35d8ff43951cef0abe06bb9a9a`

## Why

`libX11` is Xlib — the classic X11 client API (`XOpenDisplay`,
`XCreateWindow`, the event loop, …).  It is layered on `libxcb`
and is what the overwhelming majority of X clients link against.

## Scope

- `libX11.so.6` + `libX11.a`            → `/usr/lib/`
- `libX11-xcb.so.1` + `.a` (Xlib/XCB bridge) → `/usr/lib/`
- `X11/*.h` headers                     → `/usr/include/X11/`
- `x11.pc`, `x11-xcb.pc`                → `/usr/lib/pkgconfig/`
- locale / compose data                → `/usr/share/X11/locale/`

## Depends on

- `contrib/xorgproto` — `X11/*.h`, `keysymdef.h`.
- `contrib/xtrans` — transport `.c` files compiled into libX11.
- `contrib/libxcb` — `libxcb.so.1` (and transitively `libXau`).

## Substrate build choices

- **Thread support is ON** (`--enable-xthreads`, the default).
  libX11 1.8 nests non-threading code (`<sys/ioctl.h>`,
  `_Xglobal_lock`) inside `#ifdef XTHREADS`, so `--disable-xthreads`
  does not build.  libX11 uses only pthread mutex / cond / self —
  no TLS keys — all implemented by substrate's libpthread.
- configure's `host_os` case has no `substrate` branch, so two
  values are passed explicitly:
  - `XTHREADLIB=-lpthread` — link `libX11.so` against libpthread.
  - `XTHREAD_CFLAGS=-D_POSIX_THREAD_SAFE_FUNCTIONS` — selects the
    5-argument POSIX `getpwnam_r` / `getpwuid_r` in `X11/Xos_r.h`
    (substrate's signature) instead of the 4-arg draft form.
    This mirrors what configure does for netbsd.
- `--with-keysymdefdir` points the `makekeys` host tool at the
  staged xorgproto headers.  `CC_FOR_BUILD=gcc` builds `makekeys`
  with the host compiler.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix for shared libraries.

## Companion core-header changes

Porting libX11 surfaced two gaps in substrate's own headers, fixed
alongside this port:

- `<strings.h>` — guard the legacy `bzero` prototype with
  `#ifndef bzero` (X11's `Xfuncs.h` redefines `bzero` as a
  function-like macro; modern glibc dropped the prototype).
- `<pthread.h>` — add the `pthread_key_t` type (X11's `Xthreads.h`
  typedefs `xthread_key_t` from it unconditionally).  Type only —
  substrate's libpthread implements no TLS-key API.

## Layout

    contrib/libX11/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
