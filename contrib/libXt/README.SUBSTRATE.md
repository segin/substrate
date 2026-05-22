# libXt on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/
Pinned:    1.3.1
Tarball:   `https://www.x.org/releases/individual/lib/libXt-1.3.1.tar.xz`
SHA-256:   `e0a774b33324f4d4c05b199ea45050f87206586d81655f8bef4dba434d931288`

## Why

libXt is the X Toolkit Intrinsics — part of the X library stack required to build
`contrib/xterm`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix so `--enable-shared` emits real `.so` files.

## Layout

    contrib/libXt/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
