# libXau on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/libxau
Pinned:    1.0.12
Tarball:   `https://www.x.org/releases/individual/lib/libXau-1.0.12.tar.xz`
SHA-256:   `74d0e4dfa3d39ad8939e99bda37f5967aba528211076828464d2777d477fc0fb`

## Why

`libXau` reads and writes the X authority file (`~/.Xauthority`):
the `XauGetBestAuthByAddr` / `XauReadAuth` / `XauWriteAuth` API.
`libxcb` links it to authorise X connections.

## Scope

- `libXau.so.6` + `libXau.a` → `/usr/lib/`
- `X11/Xauth.h`              → `/usr/include/X11/`
- `xau.pc`                   → `/usr/lib/pkgconfig/`

## Depends on

- `contrib/xorgproto` — `xproto.pc` + the `X11/*.h` headers.

## Substrate patches

- `0001-config-sub-substrate.patch` — teach `config.sub` the
  `substrate*` OS name.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool's
  generated `configure` has no `host_os` case for substrate, so
  without it `--enable-shared` still produces only a static `.a`.
  Adds `substrate*` alongside `linux*` in every libtool dispatch.

## Layout

    contrib/libXau/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
