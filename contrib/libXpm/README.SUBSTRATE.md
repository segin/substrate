# libXpm on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/
Pinned:    3.5.19
Tarball:   `https://www.x.org/releases/individual/lib/libXpm-3.5.19.tar.xz`
SHA-256:   `ad3576d689221a39dc728f0e0dc02ca7bb6a0d724c9a77fd1bfa1e9af83be900`

## Why

libXpm is the X PixMap image library — part of the X library stack required to build
`contrib/xterm`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix so `--enable-shared` emits real `.so` files.

## Layout

    contrib/libXpm/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
