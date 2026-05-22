# libSM on substrate

Upstream:  https://gitlab.freedesktop.org/xorg/lib/
Pinned:    1.2.6
Tarball:   `https://www.x.org/releases/individual/lib/libSM-1.2.6.tar.xz`
SHA-256:   `be7c0abdb15cbfd29ac62573c1c82e877f9d4047ad15321e7ea97d1e43d835be`

## Why

libSM is the X Session Management client library — part of the X library stack required to build
`contrib/xterm`.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-libtool-configure-substrate-shared-libs.patch` — libtool
  `host_os` fix so `--enable-shared` emits real `.so` files.

## Layout

    contrib/libSM/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
