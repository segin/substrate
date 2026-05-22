# libxcb on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/libxcb
Pinned:    1.17.0
Tarball:   `https://www.x.org/releases/individual/lib/libxcb-1.17.0.tar.xz`
SHA-256:   `599ebf9996710fea71622e6e184f3a8ad5b43d0e5fa8c4e407123c88a59a6d55`

## Why

`libxcb` is the modern X C Binding — the low-level protocol layer
that `libX11` is built on top of.  It speaks the X11 wire protocol
over a socket (AF_UNIX or TCP).

## Scope

- `libxcb.so.1` + `libxcb.a`               → `/usr/lib/`
- `libxcb-<ext>.so.0` + `.a` (shm, render,
  randr, xfixes, xkb, … — 24 extensions)   → `/usr/lib/`
- `xcb/*.h` headers                        → `/usr/include/xcb/`
- `xcb*.pc`                                → `/usr/lib/pkgconfig/`

## Depends on

- `contrib/xcb-proto` — protocol XML + the `xcbgen` Python code
  generator (build-time only).
- `contrib/libXau` — `libXau.so.6` for connection authorisation.
- `contrib/xorgproto` — `X11/*.h`.

`build.sh` overrides `XCBPROTO_XCBINCLUDEDIR` / `XCBPROTO_XCBPYTHONDIR`
at make time so the host `xcbgen` run reads the freshly-staged
`xcb-proto`, not on-target paths.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix so `--enable-shared` emits real `.so` files.

## pthread-stubs

libxcb's `configure` lists `substrate` outside its no-pthread-stubs
case, so it `PKG_CHECK_MODULES`-requires a `pthread-stubs` package.
Substrate has no `libpthread-stubs` port — instead `pkgconfig/pthread-stubs.pc`
(supplied here, added to `PKG_CONFIG_LIBDIR`) resolves the dependency
to the real `-lpthread`.  libxcb uses only `pthread_mutex_*` /
`pthread_cond_*`, all implemented by substrate's libpthread, so
`libxcb.so.1` simply carries `DT_NEEDED libpthread.so.0`.

## Notes

libxcb's `xcb_auth.c` uses the POSIX `IN6_IS_ADDR_V4MAPPED` /
`IN6_IS_ADDR_LOOPBACK` macros; these were added to substrate's
`<netinet/in.h>` (it already had `struct in6_addr` + `AF_INET6`).
XDM-AUTHORIZATION-1 support is off — no `libXdmcp` port — which
`configure` handles gracefully (MIT-MAGIC-COOKIE-1 still works).

## Layout

    contrib/libxcb/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        pkgconfig/pthread-stubs.pc
        build/    ← NOT vendored
